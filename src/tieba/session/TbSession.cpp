#include "TbSession.hpp"

#include "util/TbMd5.hpp"

#include <QCoreApplication>
#include <QDebug>
#include <QScopedPointer>
#include <QSettings>
#include <QRegExp>
#include <QUrl>
#include <QMap>
#include <QStringList>
#include <QVariantMap>

namespace tieba {

namespace {

const char kOrganization[] = "BBTieba";
const char kApplication[] = "BBTieba";

const char kKeyBduss[] = "session/bduss";
const char kKeyStoken[] = "session/stoken";
const char kKeyUid[] = "session/uid";
const char kKeyUserName[] = "session/user_name";
const char kKeyNickName[] = "session/nick_name";
const char kKeyPortrait[] = "session/portrait";
const char kKeyTbs[] = "session/tbs";

/* ---- 多账号 ---- */
const char kKeyAccounts[] = "accounts/list";
const char kKeyCurrentId[] = "accounts/current";

} // anonymous namespace

TbSession::TbSession(QObject *parent)
    : QObject(parent)
    , m_needsProfileRefresh(false)
    , m_loading(false)
{
    QCoreApplication::setOrganizationName(QLatin1String(kOrganization));
    QCoreApplication::setApplicationName(QLatin1String(kApplication));
}

TbSession::~TbSession()
{
}

bool TbSession::isLoggedIn() const
{
    return !m_bduss.isEmpty();
}

void TbSession::setSettingsFileForTest(const QString &path)
{
    m_settingsFilePath = path;
}

QSettings *TbSession::makeSettings() const
{
    if (m_settingsFilePath.isEmpty())
        return new QSettings();
    return new QSettings(m_settingsFilePath, QSettings::IniFormat);
}

void TbSession::setNeedsProfileRefresh(bool value)
{
    if (m_needsProfileRefresh == value)
        return;
    m_needsProfileRefresh = value;
    emit changed();
}

QString TbSession::extractCookieValue(const QString &text, const QString &name)
{
    QString trimmed = text.trimmed();
    if (trimmed.isEmpty() || name.isEmpty())
        return QString();
    if (trimmed.startsWith(QLatin1String("Cookie:"), Qt::CaseInsensitive))
        trimmed = trimmed.mid(7).trimmed();
    if (!trimmed.contains(QLatin1Char('=')) && trimmed.contains(QLatin1Char('%')))
        trimmed = QUrl::fromPercentEncoding(trimmed.toUtf8());

    // 一次识别 Cookie、表单和换行；不能先把表单整串当作一个 Cookie 值。
    const QStringList parts = trimmed.split(QRegExp(QLatin1String("[;&\\r\\n]")),
                                            QString::SkipEmptyParts);
    for (int i = 0; i < parts.size(); ++i) {
        const QString part = parts.at(i).trimmed();
        const int eq = part.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        if (part.left(eq).trimmed().compare(name, Qt::CaseInsensitive) == 0)
            return QUrl::fromPercentEncoding(part.mid(eq + 1).trimmed().toUtf8());
    }

    return QString();
}

QString TbSession::normalizeBdussInput(const QString &text)
{
    QString value = extractCookieValue(text, QLatin1String("BDUSS"));
    if (value.isEmpty())
        value = QUrl::fromPercentEncoding(text.trimmed().toUtf8());
    // 修复旧版保存的「裸 BDUSS + &STOKEN=...」形式。
    const int delimiter = value.indexOf(QRegExp(QLatin1String("[;&\\r\\n]")));
    if (delimiter >= 0)
        value = value.left(delimiter).trimmed();
    if (value.contains(QLatin1Char('%')))
        return QString();
    return looksLikeBduss(value) ? value : QString();
}

bool TbSession::looksLikeBduss(const QString &value)
{
    const QString v = value.trimmed();
    // BDUSS 实测长度约 190 字符，字符集为 base64url + 部分符号
    if (v.size() < 50)
        return false;
    for (int i = 0; i < v.size(); ++i) {
        const QChar c = v.at(i);
        // '=' 只能是裸值末尾的 Base64 padding，不能是其它字段的等号。
        if (c == QLatin1Char('=')) {
            if (v.size() - i > 2)
                return false;
            for (int j = i; j < v.size(); ++j)
                if (v.at(j) != QLatin1Char('='))
                    return false;
            return true;
        }
        const bool ok = (c >= QLatin1Char('a') && c <= QLatin1Char('z'))
                        || (c >= QLatin1Char('A') && c <= QLatin1Char('Z'))
                        || (c >= QLatin1Char('0') && c <= QLatin1Char('9'))
                        || c == QLatin1Char('-') || c == QLatin1Char('_')
                        || c == QLatin1Char('=') || c == QLatin1Char('+')
                        || c == QLatin1Char('/') || c == QLatin1Char('.')
                        || c == QLatin1Char('~') || c == QLatin1Char('%');
        if (!ok)
            return false;
    }
    return true;
}

/* ==================== 多账号 ==================== */

QString TbSession::accountIdFor(const QString &bduss)
{
    /*
     * id = "b" + md5(BDUSS) 前 12 位。
     * 加前缀是为了和将来可能用 uid 当 id 的记录区分开（形如 "123456789"）。
     */
    if (bduss.isEmpty())
        return QString();
    return QLatin1String("b") + TbMd5::hex(bduss.toUtf8()).left(12);
}

QString TbSession::maskCredential(const QString &bduss)
{
    if (bduss.isEmpty())
        return QString();
    if (bduss.size() <= 12)
        return QString::fromUtf8("…");

    // 只露头尾：界面上显示 "00AC6E94…4D5A"，够用户认出是哪一份就行
    return bduss.left(8) + QString::fromUtf8("…") + bduss.right(4);
}

int TbSession::indexOfId(const QString &id) const
{
    if (! id.isEmpty()) {
        for (int i = 0; i < m_accounts.size(); ++i) {
            if (m_accounts.at(i).toMap().value(QLatin1String("id")).toString() == id)
                return i;
        }
        return -1;
    }

    /*
     * id 为空：按"当前 BDUSS"认 —— 那一瞬间只可能是它。
     * （老版本升级上来的记录、或者调用方还没来得及设 currentId 的情况。）
     */
    if (! m_bduss.isEmpty()) {
        for (int i = 0; i < m_accounts.size(); ++i) {
            if (m_accounts.at(i).toMap().value(QLatin1String("bduss")).toString()
                    == m_bduss)
                return i;
        }
    }
    return -1;
}

QVariantMap TbSession::publicAccount(const QVariantMap &raw) const
{
    QVariantMap row = raw;

    // ★ 明文不出这个类（要明文走 credential()）
    row.remove(QLatin1String("bduss"));
    row.remove(QLatin1String("stoken"));

    row.insert(QLatin1String("masked"),
               maskCredential(raw.value(QLatin1String("bduss")).toString()));
    row.insert(QLatin1String("isCurrent"),
               ! m_bduss.isEmpty()
               && raw.value(QLatin1String("bduss")).toString() == m_bduss);
    return row;
}

QVariantList TbSession::accounts() const
{
    QVariantList out;
    for (int i = 0; i < m_accounts.size(); ++i)
        out.append(publicAccount(m_accounts.at(i).toMap()));
    return out;
}

QVariantList TbSession::otherAccounts() const
{
    QVariantList out;
    for (int i = 0; i < m_accounts.size(); ++i) {
        const QVariantMap row = publicAccount(m_accounts.at(i).toMap());
        if (row.value(QLatin1String("isCurrent")).toBool())
            continue;
        out.append(row);
    }
    return out;
}

QVariantMap TbSession::currentAccount() const
{
    if (m_bduss.isEmpty())
        return QVariantMap();

    const int idx = indexOfId(m_currentId);
    if (idx < 0)
        return QVariantMap();

    return publicAccount(m_accounts.at(idx).toMap());
}

QString TbSession::credential(const QString &id) const
{
    const int idx = indexOfId(id);
    if (idx < 0)
        return QString();
    return m_accounts.at(idx).toMap().value(QLatin1String("bduss")).toString();
}

void TbSession::rememberCurrentAccount()
{
    if (m_bduss.isEmpty())
        return;

    const QString id = accountIdFor(m_bduss);
    int idx = indexOfId(id);

    QVariantMap acc = (idx >= 0) ? m_accounts.at(idx).toMap() : QVariantMap();
    acc.insert(QLatin1String("id"), id);
    acc.insert(QLatin1String("bduss"), m_bduss);
    if (! m_stoken.isEmpty())
        acc.insert(QLatin1String("stoken"), m_stoken);
    if (! m_uid.isEmpty())
        acc.insert(QLatin1String("uid"), m_uid);
    if (! m_userName.isEmpty())
        acc.insert(QLatin1String("userName"), m_userName);
    if (! m_nickName.isEmpty())
        acc.insert(QLatin1String("nickName"), m_nickName);
    if (! m_portrait.isEmpty())
        acc.insert(QLatin1String("portrait"), m_portrait);
    if (! m_tbs.isEmpty())
        acc.insert(QLatin1String("tbs"), m_tbs);

    if (idx >= 0)
        m_accounts.replace(idx, acc);
    else
        m_accounts.append(acc);

    m_currentId = id;
    save();
    emit accountsChanged();
}

bool TbSession::switchTo(const QString &id)
{
    const int idx = indexOfId(id);
    if (idx < 0)
        return false;

    const QVariantMap acc = m_accounts.at(idx).toMap();
    const QString bduss = normalizeBdussInput(acc.value(QLatin1String("bduss")).toString());
    if (bduss.isEmpty())
        return false;

    /*
     * ★ 换号 = 把当前凭证整套换掉。
     *   资料（uid/昵称/头像）也一并换成这个账号上次记住的 ——
     *   这样切换后界面上立刻就是新账号的名字，不用等网络。
     */
    m_bduss = bduss;
    m_stoken = acc.value(QLatin1String("stoken")).toString();
    m_uid = acc.value(QLatin1String("uid")).toString();
    m_userName = acc.value(QLatin1String("userName")).toString();
    m_nickName = acc.value(QLatin1String("nickName")).toString();
    m_portrait = acc.value(QLatin1String("portrait")).toString();
    m_tbs = acc.value(QLatin1String("tbs")).toString();
    m_currentId = acc.value(QLatin1String("id")).toString();

    // 这个账号的资料可能还是空的（从没拉到过），标一下让上层去补
    m_needsProfileRefresh = m_uid.isEmpty();

    save();
    emit changed();
    emit accountsChanged();
    return true;
}

bool TbSession::removeAccount(const QString &id)
{
    const int idx = indexOfId(id);
    if (idx < 0)
        return false;

    const bool wasCurrent =
        ! m_bduss.isEmpty()
        && m_accounts.at(idx).toMap().value(QLatin1String("bduss")).toString() == m_bduss;

    m_accounts.removeAt(idx);

    if (wasCurrent) {
        // 删掉的正是当前在用的：登录态一起清掉，别攥着已删的凭证继续发请求
        m_bduss.clear();
        m_stoken.clear();
        m_uid.clear();
        m_userName.clear();
        m_nickName.clear();
        m_portrait.clear();
        m_tbs.clear();
        m_currentId.clear();
        m_needsProfileRefresh = false;
        emit changed();
    }

    save();
    emit accountsChanged();
    return true;
}

bool TbSession::setCredentials(const QString &bduss, const QString &stoken)
{
    const QString resolvedBduss = normalizeBdussInput(bduss);

    if (resolvedBduss.isEmpty())
        return false;

    QString resolvedStoken = stoken.trimmed();
    if (resolvedStoken.isEmpty()) {
        resolvedStoken = extractCookieValue(bduss, QLatin1String("STOKEN"));
        if (resolvedStoken.isEmpty())
            resolvedStoken = extractCookieValue(bduss, QLatin1String("stoken"));
    }

    /*
     * 换凭证 = 换人：资料（uid/昵称/头像）先清空。
     * 不清的话，粘了另一个人的 BDUSS 之后，界面上会顶着上一个账号的昵称
     * 直到新资料回来 —— 就是 NeteaseMusic 注释里写的那个坑。
     */
    m_bduss = resolvedBduss;
    m_stoken = resolvedStoken;
    m_uid.clear();
    m_userName.clear();
    m_nickName.clear();
    m_portrait.clear();
    m_tbs.clear();
    m_needsProfileRefresh = true;

    /*
     * ★ 立刻记进账号列表（**不等校验**）。
     *
     *   和 NeteaseMusic 一样：凭证先落盘，校验是异步的。
     *   好处是断网时粘贴的账号也不会丢 —— 列表里有一条，联网后自动补上昵称。
     *   校验回来（拉到资料）时 rememberCurrentAccount() 会把昵称头像补齐。
     */
    rememberCurrentAccount();

    save();
    emit changed();
    return true;
}

void TbSession::setProfile(const QString &uid, const QString &userName,
                           const QString &nickName, const QString &portrait)
{
    m_uid = uid;
    m_userName = userName;
    m_nickName = nickName;
    m_portrait = portrait;
    m_needsProfileRefresh = false;

    // 资料属于当前账号，顺带写回列表（下次不联网也能显示昵称头像）
    rememberCurrentAccount();

    save();
    emit changed();
}

void TbSession::setTbs(const QString &tbs)
{
    if (m_tbs == tbs)
        return;
    m_tbs = tbs;
    rememberCurrentAccount();
    save();
    emit changed();
}

void TbSession::logout()
{
    /*
     * ★ 多账号之后语义变了：退出登录 = **只清当前登录态**，账号留在列表里。
     *
     *   和 NeteaseMusic 一致：退出后原账号会出现在「已登录」列表，
     *   随时点一下就能登回来。真要抹掉一个账号走 removeAccount()。
     */
    m_bduss.clear();
    m_stoken.clear();
    m_uid.clear();
    m_userName.clear();
    m_nickName.clear();
    m_portrait.clear();
    m_tbs.clear();
    m_currentId.clear();
    m_needsProfileRefresh = false;

    save();
    emit changed();
    emit accountsChanged();
}

void TbSession::load()
{
    m_loading = true;

    QScopedPointer<QSettings> settings(makeSettings());
    m_bduss = settings->value(QLatin1String(kKeyBduss)).toString();
    m_stoken = settings->value(QLatin1String(kKeyStoken)).toString();
    m_uid = settings->value(QLatin1String(kKeyUid)).toString();
    m_userName = settings->value(QLatin1String(kKeyUserName)).toString();
    m_nickName = settings->value(QLatin1String(kKeyNickName)).toString();
    m_portrait = settings->value(QLatin1String(kKeyPortrait)).toString();
    m_tbs = settings->value(QLatin1String(kKeyTbs)).toString();

    m_accounts = settings->value(QLatin1String(kKeyAccounts)).toList();
    m_currentId = settings->value(QLatin1String(kKeyCurrentId)).toString();

    // 旧版曾把后续表单字段一起存进 BDUSS；只修复能明确提取出有效值的记录。
    const QString storedBduss = m_bduss;
    const QString normalizedBduss = normalizeBdussInput(storedBduss);
    const QString previousCurrentId = m_currentId;
    bool repaired = false;
    int repairedAccounts = 0;
    if (!normalizedBduss.isEmpty() && normalizedBduss != storedBduss) {
        m_bduss = normalizedBduss;
        if (m_stoken.isEmpty())
            m_stoken = extractCookieValue(storedBduss, QLatin1String("STOKEN"));
        repaired = true;
    }
    QVariantList accounts;
    QMap<QString, int> seen;
    for (int i = 0; i < m_accounts.size(); ++i) {
        QVariantMap account = m_accounts.at(i).toMap();
        const QString oldId = account.value(QLatin1String("id")).toString();
        const QString oldValue = account.value(QLatin1String("bduss")).toString();
        const QString normalized = normalizeBdussInput(oldValue);
        if (!normalized.isEmpty() && normalized != oldValue) {
            account.insert(QLatin1String("bduss"), normalized);
            account.insert(QLatin1String("id"), accountIdFor(normalized));
            if (account.value(QLatin1String("stoken")).toString().isEmpty())
                account.insert(QLatin1String("stoken"), extractCookieValue(oldValue, QLatin1String("STOKEN")));
            if (oldId == previousCurrentId)
                m_currentId = account.value(QLatin1String("id")).toString();
            repaired = true;
            ++repairedAccounts;
        }
        if (!normalized.isEmpty()) {
            const QString key = accountIdFor(normalized);
            if (seen.contains(key)) {
                if (oldId == previousCurrentId)
                    accounts[seen.value(key)] = account;
                repaired = true;
                continue;
            }
            seen.insert(key, accounts.size());
        }
        accounts.append(account);
    }
    m_accounts = accounts;

    /*
     * ★ 老版本升上来的兼容：以前只有一份裸 BDUSS，没有账号列表。
     *   这里把它补成第一条账号记录 —— 这样升级不会把已经登录的人挤下线。
     */
    if (m_accounts.isEmpty() && !m_bduss.isEmpty()) {
        QVariantMap acc;
        acc.insert(QLatin1String("id"), accountIdFor(m_bduss));
        acc.insert(QLatin1String("bduss"), m_bduss);
        acc.insert(QLatin1String("stoken"), m_stoken);
        acc.insert(QLatin1String("uid"), m_uid);
        acc.insert(QLatin1String("userName"), m_userName);
        acc.insert(QLatin1String("nickName"), m_nickName);
        acc.insert(QLatin1String("portrait"), m_portrait);
        acc.insert(QLatin1String("tbs"), m_tbs);
        m_accounts.append(acc);
        m_currentId = acc.value(QLatin1String("id")).toString();
        save();
    }

    if (repaired) {
        save();
        qWarning("TbSession: normalized stored credential oldLength=%d newLength=%d repairedAccounts=%d",
                 storedBduss.size(), m_bduss.size(), repairedAccounts);
    }

    // 有登录态但没资料，说明上次登录后没来得及拉资料
    m_needsProfileRefresh = isLoggedIn() && m_uid.isEmpty();

    m_loading = false;
    emit changed();
    emit accountsChanged();
}

void TbSession::init()
{
    load();

}

void TbSession::save() const
{
    if (m_loading)
        return;

    QScopedPointer<QSettings> settings(makeSettings());
    settings->setValue(QLatin1String(kKeyBduss), m_bduss);
    settings->setValue(QLatin1String(kKeyStoken), m_stoken);
    settings->setValue(QLatin1String(kKeyUid), m_uid);
    settings->setValue(QLatin1String(kKeyUserName), m_userName);
    settings->setValue(QLatin1String(kKeyNickName), m_nickName);
    settings->setValue(QLatin1String(kKeyPortrait), m_portrait);
    settings->setValue(QLatin1String(kKeyTbs), m_tbs);

    // 多账号
    settings->setValue(QLatin1String(kKeyAccounts), m_accounts);
    settings->setValue(QLatin1String(kKeyCurrentId), m_currentId);

    settings->sync();
}

} // namespace tieba
