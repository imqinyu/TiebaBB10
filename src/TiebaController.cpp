#include "TiebaController.hpp"

#include "net/TbCookieStore.hpp"
#include "net/TbHttpClient.hpp"
#include "net/TbSigner.hpp"
#include "session/TbSession.hpp"
#include "util/TbImageCache.hpp"
#include "util/TbJson.hpp"
#include "util/TbString.hpp"
#include "util/TbTr.hpp"
#include "model/TbThreadLibrary.hpp"
#include "api/TbFavoriteService.hpp"
#include "model/TbUserDataCache.hpp"
#include "util/TbThreadNavigation.hpp"

#include <bb/cascades/ArrayDataModel>
#include <bb/system/Clipboard>
#include <bb/system/SystemToast>
#include <bb/system/SystemUiPosition>
#include <bb/system/InvokeManager>
#include <bb/system/InvokeRequest>
#include <bb/system/InvokeReplyError>
#include <bb/system/InvokeTargetReply>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTimer>
#include <QDebug>
#include <QVariant>

using namespace bb::cascades;
using namespace tieba;

namespace {

QVariantList modelRows(ArrayDataModel *model)
{
    QVariantList rows;
    for (int i = 0; model && i < model->size(); ++i) rows.append(model->value(i));
    return rows;
}

bool syncRows(ArrayDataModel *model, const QVariantList &rows)
{
    bool changed = false;
    while (model->size() > rows.size()) { model->removeAt(model->size() - 1); changed = true; }
    for (int i = 0; i < rows.size(); ++i) {
        if (i >= model->size()) { model->append(rows.at(i)); changed = true; }
        else if (model->value(i) != rows.at(i)) { model->replace(i, rows.at(i)); changed = true; }
    }
    return changed;
}

bool samePrefix(const QVariantList &oldRows, const QVariantList &firstPage)
{
    if (oldRows.size() < firstPage.size()) return false;
    QVariantMap oldPayload, newPayload;
    oldPayload.insert("items", oldRows.mid(0, firstPage.size()));
    newPayload.insert("items", firstPage);
    return TbUserDataCache::equivalent(oldPayload, newPayload);
}

/*! 业务请求标识，用于 QML 收尾动画 */
const char kWhatForums[] = "forums";
const char kWhatForumPage[] = "forumPage";
const char kWhatThread[] = "thread";
const char kWhatSubPosts[] = "subPosts";

/*
 * 设置的默认值。
 * VisualStyle::Type：Bright=1 / Dark=2（和 QML 里的 VisualStyle.Bright/Dark 一致）。
 */
const int kVisualStyleBright = 1;
const int kVisualStyleDark = 2;
/*! 默认图片缓存上限（MB） */
const int kDefaultCacheLimitMb = 512;

// Keep emoticon paths on the segment that owns them, including reply previews.
QVariantList cachedTextSegments(const QVariantList &segments, TbImageCache *cache)
{
    QVariantList result;
    for (int i = 0; i < segments.size(); ++i) {
        QVariantMap segment = segments.at(i).toMap();
        if (segment.value(QLatin1String("kind")).toString() == QLatin1String("emoticon")) {
            const QString url = segment.value(QLatin1String("url")).toString();
            segment.insert(QLatin1String("localImagePath"),
                           cache && !url.isEmpty() ? cache->pathFor(url) : QString());
        }
        result.append(segment);
    }
    return result;
}

QVariantMap cachedPostText(QVariantMap post, TbImageCache *cache)
{
    post.insert(QLatin1String("displaySegments"), cachedTextSegments(
        post.value(QLatin1String("contentSegments")).toList(), cache));
    QVariantList previews = post.value(QLatin1String("subPostPreview")).toList();
    for (int i = 0; i < previews.size(); ++i) {
        QVariantMap reply = previews.at(i).toMap();
        reply.insert(QLatin1String("displaySegments"), cachedTextSegments(
            reply.value(QLatin1String("contentSegments")).toList(), cache));
        previews[i] = reply;
    }
    post.insert(QLatin1String("subPostPreview"), previews);
    return post;
}

/*! 把 QVariantMap 列表灌进 ArrayDataModel */
void fillModel(ArrayDataModel *model, const QVariantList &items)
{
    if (!model)
        return;
    model->clear();
    if (!items.isEmpty())
        model->append(items);
}

/*!
 * 把一个 QString 转成合法的 JSON 字符串字面量（**含首尾引号**）。
 *
 * 为什么要自己写：吧名里可能有引号、反斜杠，直接拼进 JSON 会让文件损坏，
 * 下次读缓存就失败。中文不用特殊处理 —— UTF-8 本来就能直接放在 JSON 里。
 */
QByteArray jsonString(const QString &value)
{
    QByteArray out;
    out += '"';
    const QByteArray utf8 = value.toUtf8();
    for (int i = 0; i < utf8.size(); ++i) {
        const char c = utf8.at(i);
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:
            if ((unsigned char)c < 0x20) {
                // 其它控制字符用 \u00XX 形式（JSON 不允许裸控制字符）
                out += "\\u00";
                out += QByteArray::number((unsigned char)c, 16).rightJustified(2, '0');
            } else {
                out += c;
            }
            break;
        }
    }
    out += '"';
    return out;
}

} // anonymous namespace

TiebaController::TiebaController(QObject *parent, const QString &dataDirectory)
    : QObject(parent)
    // 设置项：声明位置在 private 段最前面，初始化顺序要和声明顺序一致
    // （ gcc 的 -Wreorder 会提醒，而 tests\run-tests.ps1 把编译器的 stderr
    //   当错误看，所以顺序必须写对 ）
    , m_theme(kVisualStyleBright)
    , m_cacheLimitMb(kDefaultCacheLimitMb)
    , m_hideVideoThreads(false)
    , m_showFollowedOnly(false)
    , m_cookieStore(new TbCookieStore())
    , m_httpClient(new TbHttpClient(this))
    , m_session(new TbSession(this))
    , m_api(0)
    , m_forums(0)
    , m_forumInfoVersion(0)
    // 多账号（顺序要和头文件里的声明一致，否则 -Wreorder 会提醒）
    , m_otherAccounts(0)
    , m_accountsVersion(0)
    , m_openRequestTimer(0)
    , m_loginRequestId(-1)
    , m_profileProbeRequestId(-1)
    , m_threads(0)
    , m_posts(0)
    , m_subPosts(0)
    , m_uiDataVersion(0)
    , m_streamVersion(0)
    , m_userPageVersion(0)
    , m_threadLibrary(new TbThreadLibrary(this, dataDirectory.isEmpty() ? QString() : dataDirectory + "/thread-library.dat"))
    , m_favoriteService(0)
    , m_userDataCache(new TbUserDataCache(this, dataDirectory.isEmpty() ? QString() : dataDirectory + "/user-cache"))
    , m_threadNavigationVersion(0)
    , m_jumpFloor(0)
    , m_jumpLow(1)
    , m_jumpHigh(0)
    , m_threadTotalPages(0)
    , m_threadOnlyOp(false)
    , m_contextVersion(0)
    , m_loading(false)
    , m_hasMore(false)
    , m_forumListRequestId(-1)
    , m_forumPageRequestId(-1)
    , m_threadRequestId(-1)
    , m_pendingForumPageAppend(false)
    , m_pendingThreadAppend(false)
    , m_pendingSubPostsAppend(false)
    , m_subPostsRequestId(-1)
    , m_lastPage(0)
    , m_imageCache(0)
    , m_imageCacheVersion(0)
    , m_imageRefreshPending(false)
    , m_toast(0)
    , m_invokeManager(0)
{
    m_cacheDirOverride = dataDirectory;
    if (!dataDirectory.isEmpty()) m_session->setSettingsFileForTest(dataDirectory + "/session.ini");
    m_httpClient->setCookieStore(m_cookieStore);
    m_api = new TbApiService(m_httpClient, m_session, this);
    m_favoriteService = new TbFavoriteService(m_httpClient, m_session, this);
    if (!dataDirectory.isEmpty()) m_favoriteService->setCacheDirectoryForTest(dataDirectory + "/user-cache");
    connect(m_favoriteService, SIGNAL(operationFinished(QString,bool,bool,QString)),
            this, SLOT(onFavoriteOperation(QString,bool,bool,QString)));

    // 图片下载缓存：BB10 的 ImageView 不能加载网络图片（见 imagePath 的说明）
    m_imageCache = new TbImageCache(this);
    if (!dataDirectory.isEmpty()) m_imageCache->setCacheDir(dataDirectory + "/images");

    /*
     * Toast 与 InvokeManager 都用**常驻对象**（见头文件里的说明）：
     * 它们都是异步 UI/框架对象，局部变量会在 show()/invoke() 返回后立刻析构，
     * 导致提示不显示、意图不执行。
     */
    m_toast = new bb::system::SystemToast(this);
    m_invokeManager = new bb::system::InvokeManager(this);
    connect(m_imageCache, SIGNAL(ready()), this, SLOT(onImageReady()));

    /*
     * 图片路径刷新节流器。
     *
     * 图片会接连下完，这里攒 80ms 刷一次，避免每张图下完都全表扫一遍模型
     * （一页 30 个楼层都有图时，那是 30 次 O(N) 遍历 + 30 次 map 拷贝，
     *   正是"多图时卡"的主因之一）。
     */
    m_imageRefreshTimer = new QTimer(this);
    m_imageRefreshTimer->setSingleShot(true);
    m_imageRefreshTimer->setInterval(80);
    connect(m_imageRefreshTimer, SIGNAL(timeout()), this, SLOT(refreshImagePaths()));

    // 注意：必须是 Cascades 的 ArrayDataModel。ListView 的 dataModel 只接受
    // bb::cascades::DataModel*，用 Qt 的 QAbstractListModel 会黑屏。
    m_forums = new ArrayDataModel(this);
    m_threads = new ArrayDataModel(this);
    m_posts = new ArrayDataModel(this);
    m_subPosts = new ArrayDataModel(this);
    m_otherAccounts = new ArrayDataModel(this);
    const QStringList streamKinds = QStringList() << "follow" << "recommend" << "hot" << "reply" << "mention";
    for (int i = 0; i < streamKinds.size(); ++i) {
        m_streamModels.insert(streamKinds.at(i), new ArrayDataModel(this));
        m_streamStates.insert(streamKinds.at(i), StreamState());
    }
    connect(m_api, SIGNAL(streamLoaded(int,QString,QVariantMap)),
            this, SLOT(onStreamLoaded(int,QString,QVariantMap)));
    connect(m_api, SIGNAL(userPageLoaded(int,QString,QString,QVariantMap)),
            this, SLOT(onUserPageLoaded(int,QString,QString,QVariantMap)));
    connect(this, SIGNAL(forumsChanged()), this, SLOT(onUiDataChanged()));
    connect(this, SIGNAL(threadsChanged()), this, SLOT(onUiDataChanged()));
    connect(this, SIGNAL(postsChanged()), this, SLOT(onUiDataChanged()));
    connect(this, SIGNAL(subPostsChanged()), this, SLOT(onUiDataChanged()));
    connect(this, SIGNAL(streamsChanged()), this, SLOT(onUiDataChanged()));

    connect(m_api, SIGNAL(forumListLoaded(int,tieba::TbForumListResult)),
            this, SLOT(onForumListLoaded(int,tieba::TbForumListResult)));
    connect(m_api, SIGNAL(forumPageLoaded(int,tieba::TbPcForumPageResult)),
            this, SLOT(onForumPageLoaded(int,tieba::TbPcForumPageResult)));
    connect(m_api, SIGNAL(threadContentLoaded(int,tieba::TbPcThreadResult)),
            this, SLOT(onThreadContentLoaded(int,tieba::TbPcThreadResult)));
    connect(m_api, SIGNAL(subFloorsLoaded(int,tieba::TbPcSubFloorResult)),
            this, SLOT(onSubFloorsLoaded(int,tieba::TbPcSubFloorResult)));
    connect(m_api, SIGNAL(loginVerified(int,QVariantMap)),
            this, SLOT(onLoginVerified(int,QVariantMap)));
    connect(m_api, SIGNAL(requestFailed(int,QString,QString,QVariantMap)),
            this, SLOT(onRequestFailed(int,QString,QString,QVariantMap)));

    connect(m_session, SIGNAL(changed()), this, SLOT(onSessionChanged()));

    /*
     * 账号列表一变（登录 / 切换 / 删除 / 退出）就同步模型 ——
     * 这样各处入口不用每个都记得手动刷一遍（NeteaseMusic 也是这个做法）。
     */
    connect(m_session, SIGNAL(accountsChanged()), this, SLOT(rebuildAccounts()));
    connect(m_threadLibrary, SIGNAL(storageError(QString)), this, SLOT(onLibraryStorageError(QString)));

    /*
     * 子页推/弹页请求的延迟器。
     *
     * ★ 为什么要延迟 200ms：菜单（应用菜单 / 长按菜单）关闭时有个 peek 动画，
     *   那段动画会被 NavigationPane 当成"向左滑返回"，刚推上去的页面会立刻
     *   被弹回来 —— 表现就是"同一下要点两次才进得去"。
     *   等动画走完再通知，就没这事了。
     */
    m_openRequestTimer = new QTimer(this);
    m_openRequestTimer->setSingleShot(true);
    m_openRequestTimer->setInterval(200);
    connect(m_openRequestTimer, SIGNAL(timeout()), this, SLOT(onOpenRequestTimeout()));

    /*
     * 设置：构造时就读回来，这样 QML 首次绑定 tieba.theme / cacheLimitMb
     * 拿到的就是上次的值（主题在 main.qml 的 onCreationCompleted 里应用）。
     */
    loadSettings();
    applyCacheLimit();
}

TiebaController::~TiebaController()
{
    delete m_cookieStore;
}

/* ============================ 属性访问 ============================ */

ArrayDataModel *TiebaController::forums() const { return m_forums; }
ArrayDataModel *TiebaController::threads() const { return m_threads; }
ArrayDataModel *TiebaController::posts() const { return m_posts; }
ArrayDataModel *TiebaController::subPosts() const { return m_subPosts; }

QVariantMap TiebaController::uiUserProfile(const QVariantMap &row, bool own)
{
    const QVariantMap account = m_session->currentAccount();
    const QVariantMap author = row.value(QLatin1String("author")).toMap();
    QString id = own ? account.value(QLatin1String("uid")).toString()
                     : row.value(QLatin1String("authorId")).toString();
    if (!own && id.isEmpty())
        id = author.value(QLatin1String("id")).toString();
    QString name = own ? account.value(QLatin1String("nickName")).toString()
                       : row.value(QLatin1String("authorName")).toString();
    if (name.isEmpty())
        name = own ? account.value(QLatin1String("userName")).toString()
                   : author.value(QLatin1String("displayName")).toString();
    QString avatar = own ? TbString::avatarUrl(account.value(QLatin1String("portrait")).toString())
                         : row.value(QLatin1String("authorAvatarUrl")).toString();
    if (!own && avatar.isEmpty())
        avatar = row.value(QLatin1String("avatarUrl")).toString();
    if (!own && avatar.isEmpty())
        avatar = author.value(QLatin1String("avatarUrl")).toString();
    QVariantMap profile;
    profile.insert(QLatin1String("id"), id);
    profile.insert(QLatin1String("name"), name);
    profile.insert(QLatin1String("userName"), own
                   ? account.value(QLatin1String("userName")).toString()
                   : author.value(QLatin1String("name")).toString());
    profile.insert(QLatin1String("avatarUrl"), avatar);
    profile.insert(QLatin1String("localAvatarPath"), imagePath(avatar));
    profile.insert(QLatin1String("level"), row.value(QLatin1String("authorLevel"), 0));
    profile.insert(QLatin1String("isSelf"), own || (!id.isEmpty()
                   && id == account.value(QLatin1String("uid")).toString()));
    return profile;
}

void TiebaController::onUiDataChanged()
{
    ++m_uiDataVersion;
    emit uiDataChanged();
}

QVariantList TiebaController::loadedUiItems(const QString &kind, const QString &query,
                                            const QString &userId, const QString &forumName)
{
    QVariantList items;
    const QString term = query.trimmed();
    // 禁止把别人的关注吧误显示成自己的关注列表。
    if (kind == QLatin1String("forum")) {
        if (!userId.isEmpty() && userId != m_session->uid())
            return items;
        for (int i = 0; i < m_forums->size(); ++i) {
            QVariantMap row = m_forums->value(i).toMap();
            if (!row.value(QLatin1String("name")).toString().contains(term, Qt::CaseInsensitive))
                continue;
            row.insert(QLatin1String("resultKind"), kind);
            items.append(row);
        }
    } else if (kind == QLatin1String("thread")) {
        QSet<QString> seen;
        const ArrayDataModel *models[] = { m_threads, followedFeed(), recommendedFeed(), hotFeed() };
        for (int m = 0; m < 4; ++m) {
          for (int i = 0; i < models[m]->size(); ++i) {
            QVariantMap row = models[m]->value(i).toMap();
            const QString rowForum = m == 0 ? m_currentForumName : row.value("forumName").toString();
            if (!forumName.isEmpty() && forumName != rowForum) continue;
            if (!userId.isEmpty() && row.value(QLatin1String("authorId")).toString() != userId)
                continue;
            const QString text = row.value(QLatin1String("title")).toString() + QLatin1Char(' ')
                               + row.value(QLatin1String("abstractText")).toString();
            if (!text.contains(term, Qt::CaseInsensitive))
                continue;
            const QString id = row.value("id").toString();
            if (seen.contains(id)) continue;
            seen.insert(id);
            row.insert(QLatin1String("forumName"), rowForum);
            row.insert(QLatin1String("resultKind"), kind);
            items.append(row);
          }
        }
    } else if (kind == QLatin1String("user")) {
        QSet<QString> seen;
        const ArrayDataModel *models[] = { m_threads, m_posts, m_subPosts, followedFeed(), recommendedFeed(), hotFeed(), replyNotifications(), mentionNotifications() };
        for (int m = 0; m < 8; ++m) {
            for (int i = 0; i < models[m]->size(); ++i) {
                const QVariantMap source = models[m]->value(i).toMap();
                QVariantMap profile = uiUserProfile(source);
                const QString id = profile.value(QLatin1String("id")).toString();
                const QString name = profile.value(QLatin1String("name")).toString();
                const QString key = id.isEmpty() ? QLatin1String("name:") + name : id;
                if (name.isEmpty() || seen.contains(key)
                        || (!name.contains(term, Qt::CaseInsensitive) && !id.contains(term)))
                    continue;
                seen.insert(key);
                profile.insert(QLatin1String("resultKind"), kind);
                items.append(profile);
            }
        }
    }
    return items;
}

bool TiebaController::loggedIn() const
{
    return m_session->isLoggedIn();
}

QString TiebaController::nickName() const
{
    return m_session->nickName();
}

/* ============================ 状态维护 ============================ */

void TiebaController::clearModel(ArrayDataModel *model)
{
    if (model)
        model->clear();
}

void TiebaController::setLoading(bool value)
{
    if (m_loading == value)
        return;
    m_loading = value;
    emit loadingChanged();
}

void TiebaController::setError(const QString &message, const QVariantMap &detail)
{
    m_errorDetail = detail;
    emit errorDetailChanged();

    if (m_errorMessage == message)
        return;
    m_errorMessage = message;
    emit errorMessageChanged();
}

QVariantMap TiebaController::buildApiErrorDetail(const QString &code,
                                                 const QString &message,
                                                 const QString &raw,
                                                 int httpStatus,
                                                 const QString &tag) const
{
    QVariantMap detail;
    detail.insert(QLatin1String("tag"), tag);
    detail.insert(QLatin1String("code"), code);
    detail.insert(QLatin1String("message"), message);
    detail.insert(QLatin1String("raw"), raw);
    detail.insert(QLatin1String("httpStatus"), httpStatus);
    return detail;
}

void TiebaController::finishRequest(const QString &what, bool success)
{
    setLoading(m_loginRequestId >= 0);
    emit requestFinished(what, success);
}

ArrayDataModel *TiebaController::streamModel(const QString &kind) const
{
    return m_streamModels.value(kind, 0);
}

QVariantMap TiebaController::userPageState(const QString &uid, const QString &kind) const
{
    QVariantMap state = m_userPageStates.value(uid + QLatin1Char('|') + kind);
    if (!state.contains("items")) state.insert("items", QVariantList());
    if (!state.contains("profile")) state.insert("profile", QVariantMap());
    if (!state.contains("loading")) state.insert("loading", false);
    if (!state.contains("loaded")) state.insert("loaded", false);
    if (!state.contains("hasMore")) state.insert("hasMore", false);
    if (!state.contains("page")) state.insert("page", 0);
    if (!state.contains("error")) state.insert("error", QString());
    if (kind == "threads" && m_hideVideoThreads) {
        QVariantList visible;
        const QVariantList rows = state.value("items").toList();
        for (int i = 0; i < rows.size(); ++i)
            if (!rows.at(i).toMap().value("isVideo").toBool() || rows.at(i).toMap().value("isTop").toBool()) visible.append(rows.at(i));
        state.insert("items", visible); state.insert("count", visible.size());
    }
    return state;
}

void TiebaController::loadUserPage(const QString &uid, const QString &kind, bool append)
{
    if (uid.isEmpty() || (kind != QLatin1String("profile") && kind != QLatin1String("threads")
        && kind != QLatin1String("forums") && kind != QLatin1String("follows"))) return;
    const QString key = uid + QLatin1Char('|') + kind;
    if (!m_userPageStates.contains(key)) {
        const QVariantMap cached = m_userDataCache->read(m_session->currentAccountId(), "userpage." + uid + "." + kind);
        if (cached.value("hit").toBool()) {
            QVariantMap restored = cached.value("payload").toMap();
            restored.insert("loaded", true); restored.insert("loading", false); restored.insert("cached", true);
            restored.insert("error", QString());
            m_userPageStates.insert(key, restored);
        }
    }
    QVariantMap state = m_userPageStates.contains(key) ? m_userPageStates.value(key) : userPageState(uid, kind);
    if (state.value("loading").toBool()) return;
    if (append && (!state.value("loaded").toBool() || !state.value("hasMore").toBool())) return;
    const int page = append ? state.value("page").toInt() + 1 : 1;
    const int request = m_api->fetchUserPage(uid, kind, page);
    state.insert("loading", request >= 0);
    state.insert("error", request >= 0 ? QString() : TbTr("E8AFB7E6B182E5A4B1E8B4A5"));
    state.insert("requestedPage", page);
    state.insert("retryAppend", append);
    state.insert("requestId", request);
    m_userPageStates.insert(key, state);
    if (request >= 0) m_userPageRequests.insert(request, key);
    ++m_userPageVersion;
    emit userPagesChanged();
    if (kind != QLatin1String("profile") && !append
        && !userPageState(uid, "profile").value("loading").toBool())
        loadUserPage(uid, "profile");
}

void TiebaController::onUserPageLoaded(int requestId, const QString &uid, const QString &kind,
                                      const QVariantMap &result)
{
    const QString key = uid + QLatin1Char('|') + kind;
    if (m_userPageRequests.value(requestId) != key || !m_userPageRequests.contains(requestId)) return;
    m_userPageRequests.remove(requestId);
    QVariantMap &state = m_userPageStates[key];
    state.insert("loading", false);
    state.insert("requestId", -1);
    if (result.contains("error")) {
        state.insert("error", result.value("error"));
    } else {
        QVariantMap profile = result.value("profile").toMap();
        const QString returnedId = profile.value("id").toString();
        if (!returnedId.isEmpty() && returnedId != uid) {
            state.insert("error", TbTr("E8BF94E59B9EE794A8E688B7E4B88EE8AFB7E6B182E4B88DE4B880E887B4"));
        } else {
            if (!profile.isEmpty()) {
                profile.insert("id", uid);
                QVariantMap &profileState = m_userPageStates[uid + QLatin1String("|profile")];
                QVariantMap merged = profileState.value("profile").toMap();
                for (QVariantMap::const_iterator it = profile.begin(); it != profile.end(); ++it)
                    merged.insert(it.key(), it.value());
                profileState.insert("profile", merged);
            }
            const bool append = state.value("retryAppend").toBool();
            const QVariantList incoming = result.value("items").toList();
            const QVariantList oldRows = state.value("items").toList();
            const bool keepCached = !append && state.value("loaded").toBool() && !incoming.isEmpty()
                && (result.value("hasMore").toBool() || oldRows.size() == incoming.size()) && samePrefix(oldRows, incoming);
            QVariantList items = append || keepCached ? oldRows : QVariantList();
            QSet<QString> seen;
            for (int i = 0; i < items.size(); ++i) seen.insert(items.at(i).toMap().value("id").toString());
            int added = 0;
            for (int i = 0; i < incoming.size(); ++i) {
                QVariantMap row = incoming.at(i).toMap();
                const QString id = row.value("id").toString();
                if (!id.isEmpty() && seen.contains(id)) continue;
                if (!id.isEmpty()) seen.insert(id);
                const QString avatar = row.value("avatarUrl", row.value("authorAvatarUrl")).toString();
                if (!avatar.isEmpty()) row.insert("localAvatarPath", imagePath(avatar));
                items.append(row);
                ++added;
            }
            state.insert("items", items);
            state.insert("count", items.size());
            if (!keepCached) {
                state.insert("page", state.value("requestedPage"));
                state.insert("hasMore", result.value("hasMore").toBool() && (!append || added > 0));
            }
            state.insert("cached", false);
            state.insert("loaded", true);
            state.insert("error", QString());
            state.insert("private", result.value("private", false));
            state.insert("message", result.value("message", QString()));
            m_userDataCache->save(m_session->currentAccountId(), "userpage." + uid + "." + kind, state);
            if (!profile.isEmpty()) m_userDataCache->save(m_session->currentAccountId(), "userpage." + uid + ".profile", m_userPageStates.value(uid + "|profile"));
        }
    }
    ++m_userPageVersion;
    emit userPagesChanged();
}

QVariantMap TiebaController::streamState(const QString &kind) const
{
    const StreamState state = m_streamStates.value(kind);
    QVariantMap result;
    result.insert("loaded", state.loaded);
    result.insert("loading", state.loading);
    result.insert("hasMore", state.hasMore);
    result.insert("retryAppend", state.append);
    result.insert("error", state.error);
    result.insert("page", state.page);
    result.insert("count", streamModel(kind) ? streamModel(kind)->size() : 0);
    result.insert("cached", state.cached);
    return result;
}

void TiebaController::ensureStream(const QString &kind)
{
    if (!m_streamStates.contains(kind) || !loggedIn()) return;
    const StreamState state = m_streamStates.value(kind);
    if (!state.loaded && !state.loading && state.error.isEmpty()) loadStream(kind);
}

void TiebaController::loadStream(const QString &kind, bool append)
{
    if (!m_streamStates.contains(kind)) return;
    StreamState &state = m_streamStates[kind];
    if (state.loading) return;
    if (!loggedIn()) {
        state.error = QString::fromUtf8("请先登录账号");
        ++m_streamVersion;
        emit streamsChanged();
        return;
    }
    if (append && (!state.loaded || !state.hasMore)) return;
    if (!state.loaded && (kind == "reply" || kind == "mention")) {
        const QVariantMap cached = m_userDataCache->read(m_session->currentAccountId(), "stream." + kind);
        if (cached.value("hit").toBool()) {
            const QVariantMap payload = cached.value("payload").toMap();
            state.sourceRows = payload.value("items").toList();
            state.page = payload.value("page", 1).toInt(); state.hasMore = payload.value("hasMore").toBool();
            state.cursor = payload.value("cursor").toString(); state.requestUnix = payload.value("requestUnix").toString();
            state.loaded = true; state.cached = true;
        }
    }
    state.append = append;
    state.requestedPage = append ? state.page + 1 : 1;
    state.loading = true;
    state.error.clear();
    updateStreamRows(kind);
    state.requestId = m_api->fetchStream(kind, state.requestedPage,
                                        append ? state.cursor : QString(),
                                        append ? state.requestUnix : QString());
    ++m_streamVersion;
    emit streamsChanged();
}

void TiebaController::onStreamLoaded(int requestId, const QString &kind, const QVariantMap &result)
{
    if (!m_streamStates.contains(kind) || m_streamStates.value(kind).requestId != requestId) return;
    StreamState &state = m_streamStates[kind];
    ArrayDataModel *model = streamModel(kind);
    const QVariantList rows = result.value("items").toList();
    const bool keepCached = !state.append && state.loaded && !rows.isEmpty()
        && (result.value("hasMore").toBool() || state.sourceRows.size() == rows.size()) && samePrefix(state.sourceRows, rows);
    if (!state.append && !keepCached) state.sourceRows.clear();
    state.seen.clear();
    for (int i = 0; i < state.sourceRows.size(); ++i) state.seen.insert(state.sourceRows.at(i).toMap().value("key").toString());
    int unique = 0;
    for (int i = 0; i < rows.size(); ++i) {
        QVariantMap row = rows.at(i).toMap();
        const QString key = row.value("key").toString();
        if (key.isEmpty() || state.seen.contains(key)) continue;
        state.seen.insert(key);
        ++unique;
        state.sourceRows.append(row);
    }
    if (!keepCached) state.page = state.requestedPage;
    state.requestId = -1;
    state.loaded = true;
    state.loading = false;
    state.error.clear();
    if (!keepCached) {
        state.cursor = result.value("cursor").toString();
        state.requestUnix = result.value("requestUnix").toString();
        state.hasMore = result.value("hasMore").toBool() && unique > 0;
    }
    state.cached = false;
    updateStreamRows(kind);
    if (kind == "reply" || kind == "mention") {
        QVariantMap payload;
        payload.insert("items", state.sourceRows); payload.insert("page", state.page); payload.insert("hasMore", state.hasMore);
        payload.insert("cursor", state.cursor); payload.insert("requestUnix", state.requestUnix);
        m_userDataCache->save(m_session->currentAccountId(), "stream." + kind, payload);
    }
    if (kind == "follow" && state.cursor.isEmpty()) state.hasMore = false;
    qWarning("TiebaController: stream=%s page=%d received=%d unique=%d shown=%d hasMore=%d",
             qPrintable(kind), state.page, rows.size(), unique, model->size(), state.hasMore);
    ++m_streamVersion;
    emit streamsChanged();
}

/* ============================ 初始化 / 登录 ============================ */

void TiebaController::init()
{
    m_session->init();

    // 接口版本与屏幕信息。真机上机型/分辨率对某些接口的风控有影响，
    // 这里给稳定默认值，需要时在 QML 里调 setClientVersion()。
    m_api->setClientVersion(QLatin1String("7.2.0.0"));
    m_api->setScreenInfo(240, 1280, 720);
    m_api->setPageSize(30);

    /*
     * 排查阶段默认打开图片下载日志。
     *
     * 为什么默认开：图片不显示的可能原因很多（缓存目录不可写、下载失败、
     * 路径没传进模型……），而界面上什么都看不出来。开着日志，控制台里
     * 直接能看到卡在哪一步。稳定之后可以改回 false。
     */
    if (m_imageCache) {
        /*
         * 并发上限。
         *
         * ★ 性能关键：一个贴的第一页有 30 个楼层，如果每层都有图，
         *   不限流就会**同时**发出 30 个 HTTP 请求，把设备网络和 UI 都拖住，
         *   表现为"多图时卡"。限成 4 个并行、其余排队，体感明显更顺。
         */
        m_imageCache->setMaxConcurrent(4);
        m_imageCache->setVerbose(true);
        qWarning("TbImageCache: 详细日志已开启\n%s", qPrintable(imageDiag()));
    }

    emit loginChanged();

    /*
     * ★★ 首屏加载放在**这里**（C++ 里），不要交给 QML。
     *
     * 为什么：QML 那边没法监听 context property（tieba）的信号。
     *   我先试过在 main.qml 根部写 `onLoggedInChanged:` ——
     *   真机报 "Cannot assign to non-existent property"。
     *   改成 `onLoginChanged:`（信号的真名）**还是同样的错**。
     *
     *   原因：QML 的 `onXxx` 处理器要求**该组件自己**有 xxx 信号。
     *   `tieba` 是 setContextProperty 注入的对象，它的信号属于那个对象，
     *   不属于 TabbedPane —— 所以无论如何都合成不出这个处理器。
     *   （Cascades 也没有 Qt Quick 的 `Connections` 类型可用。）
     *
     *   在 C++ 里调用就没有这些问题：时序自己说了算，也不依赖 QML 的特性。
     *
     * 时序上这里是安全的：applicationui.cpp 先 createRootObject() 再调 init()，
     * 所以模型变化会正常触发已经建立好的 QML 绑定。
     *
     * loadForums() 会先loadForumsFromCache()（立刻显示上次的结果），
     * 再发一次网络请求刷新 —— 所以第二次启动起是"秒开"的。
     */
    if (m_session->isLoggedIn()) {
        qWarning("TiebaController: init() 完成，开始加载吧列表（缓存优先）");
        loadForums();
    } else {
        qWarning("TiebaController: init() 完成，但未登录，暂不加载吧列表");
    }
}

bool TiebaController::login(const QString &bdussOrCookie, const QString &stoken)
{
    if (m_loginRequestId >= 0)
        return false;
    const QString normalized = TbSession::normalizeBdussInput(bdussOrCookie);
    qWarning("TiebaController: login inputLength=%d normalizedLength=%d hasCookieKey=%d semicolons=%d ampersands=%d",
             bdussOrCookie.size(), normalized.size(), bdussOrCookie.contains(QLatin1String("BDUSS="), Qt::CaseInsensitive) ? 1 : 0,
             bdussOrCookie.count(QLatin1Char(';')), bdussOrCookie.count(QLatin1Char('&')));
    if (normalized.isEmpty()) {
        m_lastLoginError = TbTr("E6B2A1E79C8BE68782E8BF99E6AEB5E587ADE8AF812EE8AFB7E7B298E8B4B4E4BBA52042445553533D20E5BC80E5A4B4E79A84E695B4E6AEB520436F6F6B69652CE68896E79BB4E68EA5E7B298E8B4B4E5AE8CE695B4E79A8420424455535320E580BC2E");
        emit lastLoginErrorChanged();
        return false;
    }

    m_lastLoginError.clear();
    emit lastLoginErrorChanged();

    m_pendingLoginBduss = normalized;
    m_pendingLoginStoken = stoken.trimmed();
    if (m_pendingLoginStoken.isEmpty())
        m_pendingLoginStoken = TbSession::extractCookieValue(bdussOrCookie, QLatin1String("STOKEN"));
    setError(QString());
    setLoading(true);
    m_loginRequestId = m_api->verifyLogin(m_pendingLoginBduss, m_pendingLoginStoken);
    return true;
}

void TiebaController::onLoginVerified(int requestId, const QVariantMap &profile)
{
    if (requestId != m_loginRequestId || profile.value("uid").toString().isEmpty())
        return;
    const QString bduss = m_pendingLoginBduss;
    const QString stoken = m_pendingLoginStoken;
    m_session->setCredentials(bduss, stoken);
    resetForAccountSwitch();
    const QString name = profile.value("userName").toString();
    const QString nick = profile.value("nickName").toString();
    m_session->setProfile(profile.value("uid").toString(), name,
                         nick.isEmpty() ? name : nick, profile.value("portrait").toString());
    m_session->setTbs(profile.value("tbs").toString());
    m_lastLoginError.clear();
    emit lastLoginErrorChanged();
    emit loginChanged();
    m_openRequestKind = QLatin1String("loginDone");
    scheduleOpenRequest();
    finishRequest(QLatin1String("account.login"), true);
    loadForums();
}

int TiebaController::credentialInputLength(const QString &input) const
{
    return TbSession::normalizeBdussInput(input).size();
}

void TiebaController::logout()
{
    // 只清当前登录态，账号列表保留（详见头文件的说明）
    m_session->logout();
    resetForAccountSwitch();
    emit loginChanged();
}

void TiebaController::onSessionChanged()
{
    emit loginChanged();
}

/* ============================ 多账号 ============================ */

QVariantMap TiebaController::currentAccount() const
{
    QVariantMap row = m_session->currentAccount();
    if (row.isEmpty())
        return row;

    /*
     * 头像：portrait（"tb.1.xxx"）-> 完整 URL -> 本地缓存路径。
     * ★ 必须给**本地路径**：BB10 的 ImageView 不认 http/https
     *   （见 imagePath 的说明），ListItemComponent 里又调不到 tieba.imagePath()。
     */
    const QString url = TbString::avatarUrl(row.value(QLatin1String("portrait")).toString());
    if (!url.isEmpty()) {
        row.insert(QLatin1String("avatarUrl"), url);
        if (m_imageCache)
            row.insert(QLatin1String("localAvatarPath"), m_imageCache->pathFor(url));
    }
    return row;
}

QVariantMap TiebaController::currentAccountUi() const
{
    const QVariantMap account = currentAccount();
    QVariantMap view;
    const char *keys[] = { "id", "nickName", "masked", "localAvatarPath" };
    for (int i = 0; i < 4; ++i)
        view.insert(QLatin1String(keys[i]), account.value(QLatin1String(keys[i])).toString());
    return view;
}

int TiebaController::otherAccountCount() const
{
    return m_otherAccounts ? m_otherAccounts->size() : 0;
}

void TiebaController::rebuildAccounts()
{
    QVariantList rows;
    const QVariantList others = m_session->otherAccounts();
    for (int i = 0; i < others.size(); ++i) {
        QVariantMap row = others.at(i).toMap();
        const QString url =
            TbString::avatarUrl(row.value(QLatin1String("portrait")).toString());
        if (!url.isEmpty()) {
            row.insert(QLatin1String("avatarUrl"), url);
            const QString local = m_imageCache ? m_imageCache->pathFor(url) : QString();
            if (!local.isEmpty())
                row.insert(QLatin1String("localAvatarPath"), local);
        }
        rows.append(row);
    }

    /*
     * ★ 能不整表重建就不重建。
     *
     *   这里踩过一次大的：模型在 ListView 正在遍历时被 clear()+append() 换掉，
     *   直接堆损坏闪退（真机日志 "free malloc object that is not allocated"）。
     *   账号列表很短，但刷新还会因为头像下载完成被触发，所以还是逐项比对：
     *   只有**内容真的变了**的那几项才 replace()。
     */
    if (m_otherAccounts->size() == rows.size()) {
        for (int i = 0; i < rows.size(); ++i) {
            if (m_otherAccounts->value(i).toMap() != rows.at(i).toMap())
                m_otherAccounts->replace(i, rows.at(i));
        }
    } else {
        m_otherAccounts->clear();
        for (int i = 0; i < rows.size(); ++i)
            m_otherAccounts->append(rows.at(i));
    }

    ++m_accountsVersion;
    emit accountsChanged();
}

bool TiebaController::switchAccount(const QString &id)
{
    if (id.isEmpty())
        return false;

    if (!m_session->switchTo(id)) {
        qWarning("TiebaController: switchAccount(%s) 失败：列表里没有这个账号",
                 qPrintable(id));
        return false;
    }

    qWarning("TiebaController: 已切换到账号 %s", qPrintable(id));

    // 换人了：先把上一个账号的数据清干净，再拉新账号的
    resetForAccountSwitch();
    emit loginChanged();
    loadForums();
    return true;
}

bool TiebaController::removeAccount(const QString &id)
{
    // 先看删的是不是当前在用的（删完就查不出来了）
    const bool wasCurrent =
        m_session->currentAccount().value(QLatin1String("id")).toString() == id;

    if (!m_session->removeAccount(id))
        return false;

    if (wasCurrent) {
        // 凭证已经被清掉了，内存里的登录态也收干净
        resetForAccountSwitch();
        emit loginChanged();
    }
    return true;
}

QString TiebaController::accountCredential(const QString &id) const
{
    /*
     * ★ 明文只在这里出去一次，取完立刻交给剪贴板。
     *   不打日志、不进模型 —— 这是多账号功能的安全底线。
     */
    return m_session->credential(id);
}

QString TiebaController::clipboardText() const
{
    bb::system::Clipboard clipboard;
    const QByteArray data = clipboard.value(QLatin1String("text/plain"));
    if (data.isEmpty())
        return QString();
    const QString text = QString::fromUtf8(data);
    qWarning("TiebaController: clipboard bytes=%d textLength=%d normalizedLength=%d",
             data.size(), text.size(), credentialInputLength(text));
    return text;
}

void TiebaController::requestOpenLogin()
{
    m_openRequestKind = QLatin1String("login");
    scheduleOpenRequest();
}

void TiebaController::clearOpenRequest()
{
    m_openRequestKind.clear();
}

void TiebaController::scheduleOpenRequest()
{
    // 连续点两次只保留最后一次：start() 会重置计时
    m_openRequestTimer->start();
}

void TiebaController::onOpenRequestTimeout()
{
    ++m_openRequestVersion;
    emit openRequestChanged();
}

void TiebaController::resetForAccountSwitch()
{
    m_forumSourceRows.clear();
    m_threadNavigation.clear(); m_jumpPages.clear(); m_jumpFloor = 0;
    m_jumpPostId.clear(); m_jumpSubPostId.clear();
    ++m_threadNavigationVersion; emit threadNavigationChanged();
    const QList<int> statsRequests = m_forumStatsRequests.keys();
    m_forumStatsRequests.clear();
    for (int i = 0; i < statsRequests.size(); ++i) m_api->cancel(statsRequests.at(i));
    const QList<int> userRequests = m_userPageRequests.keys();
    m_userPageRequests.clear();
    m_userPageStates.clear();
    for (int i = 0; i < userRequests.size(); ++i) m_api->cancel(userRequests.at(i));
    ++m_userPageVersion;
    emit userPagesChanged();
    const QStringList kinds = m_streamModels.keys();
    for (int i = 0; i < kinds.size(); ++i) {
        m_streamModels.value(kinds.at(i))->clear();
        m_streamStates[kinds.at(i)] = StreamState();
    }
    // 换账号后旧的吧列表就不该再出现，缓存一起清掉
    clearForumsCache();

    clearModel(m_forums);
    clearModel(m_threads);
    clearModel(m_posts);
    clearModel(m_subPosts);
    emit forumsChanged();
    emit threadsChanged();
    emit postsChanged();
    emit subPostsChanged();

    if (!m_forumInfo.isEmpty()) {
        m_forumInfo.clear();
        ++m_forumInfoVersion;
        emit forumInfoChanged();
    }
    if (!m_threadInfo.isEmpty()) {
        m_threadInfo.clear();
        emit threadInfoChanged();
    }

    m_forumAccum.clear();
    m_forumSeenIds.clear();
    m_forumPage = 1;
    m_lastPage = 0;

    m_currentForumName.clear();
    m_currentThreadId.clear();
    emit currentForumNameChanged();
    emit currentThreadIdChanged();

    if (m_hasMore) {
        m_hasMore = false;
        emit hasMoreChanged();
    }

    // 在途请求一律作废：它们带着**上一个账号**的凭证，回来也该丢掉
    m_loginRequestId = -1;
    m_pendingLoginBduss.clear();
    m_pendingLoginStoken.clear();
    m_forumListRequestId = -1;
    m_forumPageRequestId = -1;
    m_threadRequestId = -1;
    m_subPostsRequestId = -1;
    m_profileProbeRequestId = -1;
    m_pendingForumPageAppend = false;
    m_pendingThreadAppend = false;
    m_pendingSubPostsAppend = false;

    setLoading(false);
    setError(QString());
    ++m_streamVersion;
    emit streamsChanged();
}

void TiebaController::applyAccountFromResult(const tieba::TbPcForumPageResult &result)
{
    if (!result.user || result.user->id().isEmpty())
        return;

    const QString uid = result.user->id();
    const QString userName = result.user->name();
    // 展示用昵称优先 name_show，没有则回落 name
    const QString nickName = result.user->nameShow().isEmpty()
                                 ? userName : result.user->nameShow();

    // 没变化就不写盘（这个回调每次进吧都会来一次）
    if (m_session->uid() == uid && m_session->nickName() == nickName
            && m_session->portrait() == result.user->portrait()) {
        if (!result.tbs.isEmpty() && m_session->tbs() != result.tbs)
            m_session->setTbs(result.tbs);
        return;
    }

    qWarning("TiebaController: 拿到当前账号资料 uid=%s 昵称=%s",
             qPrintable(uid), qPrintable(nickName));
    m_session->setProfile(uid, userName, nickName, result.user->portrait());

    if (!result.tbs.isEmpty() && m_session->tbs() != result.tbs)
        m_session->setTbs(result.tbs);
}

/* ============================ 我关注的吧 ============================ */

void TiebaController::loadForums()
{
    if (!m_session->isLoggedIn()) {
        setError(TbTr("E3808CE68891E585B3E6B3A8E79A84E590A7E3808DE99C80E8A681E799BBE5BD952EE8AFB7E58588E7B298E8B4B42042445553532E"));
        emit requestFinished(QLatin1String(kWhatForums), false);
        return;
    }

    /*
     * ★ 先上缓存，再刷新。
     *
     * 为什么要这样：每次启动都干等一次网络请求，界面先是空白几秒。
     * 有缓存就立刻显示（体感是"秒开"），同时后台照常请求，
     * 成功后覆盖模型和缓存；失败就保留缓存内容，不至于变空。
     */
    const bool hadCache = loadForumsFromCache();

    setError(QString());
    // 有缓存时不显示 loading 转圈，避免列表已经出来了还转
    setLoading(! hadCache);

    /*
     * ★ 用分页接口拿**完整**关注列表。
     *
     * 原来用的是 fetchForumList()（/c/f/forum/forumrecommend），实测有 **99 条上限**：
     * 返回 99 条就停（最后一条是 freebsd），关注更多的用户后面的吧永远出不来。
     * 换成 /c/f/forum/like（带 page_no/page_size），下面从第 1 页开始，
     * onForumListLoaded 里按 has_more 继续翻，翻完一次性填模型。
     */
    startForumPaging();
}

void TiebaController::startForumPaging()
{
    m_forumAccum.clear();
    m_forumSeenIds.clear();
    m_forumPage = 1;
    /*
     * page_size 给 200，但实测服务端**会把一页限制在 ~50 条**
     * （page_size=50 时第 1 页 50 条、第 2 页 49 条，两页 has_more 都是 1）。
     * 所以还是得靠 has_more 翻页；这里给大值只是为了减少页数。
     */
    m_forumListRequestId = m_api->fetchForumListPaged(m_forumPage, 200);
}

void TiebaController::refreshForums()
{
    if (!m_session->isLoggedIn()) {
        setError(TbTr("E3808CE68891E585B3E6B3A8E79A84E590A7E3808DE99C80E8A681E799BBE5BD952EE8AFB7E58588E7B298E8B4B42042445553532E"));
        emit requestFinished(QLatin1String(kWhatForums), false);
        return;
    }

    // 用户主动刷新：不管缓存新不新，直接请求
    setError(QString());
    setLoading(true);
    startForumPaging();
}

/*!
 * 当前吧的信息（吧详情页用）。
 *
 * m_forumInfo 是 onForumPageLoaded 里存下来的 forum 对象（见那里的解析），
 * 这里只做一件事：把头像的**本地缓存路径**补上，因为 BB10 的 ImageView
 * 不能加载网络图片（见 imagePath 的说明）。
 */
QVariantMap TiebaController::currentForumDetail()
{
    QVariantMap map = m_forumInfo;

    const QString avatar = map.value(QLatin1String("avatarUrl")).toString();
    map.insert(QLatin1String("remoteImageUrl"), avatar);
    map.insert(QLatin1String("localAvatarPath"),
               (avatar.isEmpty() || !m_imageCache)
                   ? QString() : m_imageCache->pathFor(avatar));
    return map;
}

/* ============================ 设置 ============================ */
/*!
 * 设置目录（<base>/config）。
 *
 * 和吧列表缓存同一个 base（见 forumCacheFile 的说明），只是文件名不同。
 */
QString TiebaController::configDir() const
{
    QString base = m_cacheDirOverride;
    if (base.isEmpty())
        base = QDir::homePath() + QLatin1String("/bbtieba");
    if (base.isEmpty())
        return QString();

    const QString dir = base + QLatin1String("/config");
    QDir d(dir);
    if (!d.exists() && !d.mkpath(QLatin1String("."))) {
        qWarning("TiebaController: 无法创建配置目录 %s", qPrintable(dir));
        return QString();
    }
    if (m_cacheDirOverride.isEmpty() || !m_legacyConfigDirForTest.isEmpty()) {
        const QString oldDir = m_legacyConfigDirForTest.isEmpty()
            ? QDir::tempPath() + QLatin1String("/bbtieba/config/")
            : m_legacyConfigDirForTest + QLatin1Char('/');
        const QStringList files = QStringList() << QLatin1String("settings.ini")
                                               << QLatin1String("forums.json");
        for (int i = 0; i < files.size(); ++i) {
            const QString oldFile = oldDir + files.at(i);
            const QString newFile = dir + QLatin1Char('/') + files.at(i);
            if (QFile::exists(oldFile)) {
                // Preserve a newer persistent file and prevent old data being
                // resurrected after the user clears the cache.
                if (QFile::exists(newFile) || QFile::copy(oldFile, newFile))
                    QFile::remove(oldFile);
            }
        }
    }
    return dir;
}

void TiebaController::loadSettings()
{
    const QString dir = configDir();
    if (dir.isEmpty())
        return;

    QSettings s(dir + QLatin1String("/settings.ini"), QSettings::IniFormat);

    const int theme = s.value(QLatin1String("theme"), kVisualStyleBright).toInt();
    // 只认 1 / 2 两个合法值，其它（手改坏了、旧版本）一律回落到亮色
    m_theme = (theme == kVisualStyleDark) ? kVisualStyleDark : kVisualStyleBright;

    const int limit = s.value(QLatin1String("cacheLimitMb"), kDefaultCacheLimitMb).toInt();
    m_cacheLimitMb = (limit > 0) ? limit : kDefaultCacheLimitMb;

    m_hideVideoThreads =
        s.value(QLatin1String("hideVideoThreads"), false).toBool();
    m_showFollowedOnly =
        s.value(QLatin1String("showFollowedOnly"), false).toBool();

    qWarning("TiebaController: 设置已载入 theme=%d cacheLimitMb=%d 隐藏视频贴=%d 只看关注吧=%d",
             m_theme, m_cacheLimitMb, (int)m_hideVideoThreads, (int)m_showFollowedOnly);
}

void TiebaController::saveSettings() const
{
    const QString dir = configDir();
    if (dir.isEmpty())
        return;

    QSettings s(dir + QLatin1String("/settings.ini"), QSettings::IniFormat);
    s.setValue(QLatin1String("theme"), m_theme);
    s.setValue(QLatin1String("cacheLimitMb"), m_cacheLimitMb);
    s.setValue(QLatin1String("hideVideoThreads"), m_hideVideoThreads);
    s.setValue(QLatin1String("showFollowedOnly"), m_showFollowedOnly);
    s.sync();
}

void TiebaController::setTheme(int value)
{
    const int want = (value == kVisualStyleDark) ? kVisualStyleDark : kVisualStyleBright;
    if (m_theme == want)
        return;
    m_theme = want;
    saveSettings();
    emit settingsChanged();
}

void TiebaController::setCacheLimitMb(int value)
{
    const int want = (value > 0) ? value : kDefaultCacheLimitMb;
    if (m_cacheLimitMb == want)
        return;
    m_cacheLimitMb = want;
    saveSettings();
    applyCacheLimit();
    emit settingsChanged();
    emit cacheUsageChanged();
}

void TiebaController::setHideVideoThreads(bool value)
{
    if (m_hideVideoThreads == value)
        return;
    m_hideVideoThreads = value;
    saveSettings();
    updateForumRows();
    const QStringList kinds = m_streamStates.keys();
    for (int i = 0; i < kinds.size(); ++i) updateStreamRows(kinds.at(i));
    ++m_streamVersion; emit streamsChanged();
    ++m_userPageVersion; emit userPagesChanged();
    emit settingsChanged();
}

void TiebaController::updateStreamRows(const QString &kind)
{
    if (!streamModel(kind)) return;
    const QVariantList source = m_streamStates.value(kind).sourceRows;
    QVariantList visible;
    for (int i = 0; i < source.size(); ++i) {
        QVariantMap row = source.at(i).toMap();
        if (kind != "reply" && kind != "mention" && m_hideVideoThreads && row.value("isVideo").toBool() && !row.value("isTop").toBool()) continue;
        if (kind == "recommend" && m_showFollowedOnly && !row.value("isFollowedForum").toBool()) {
            bool followed = false;
            for (int n = 0; n < m_forums->size(); ++n)
                if (m_forums->value(n).toMap().value("name").toString() == row.value("forumName").toString()) { followed = true; break; }
            if (!followed) continue;
        }
        const QString avatar = row.value("remoteAvatarUrl").toString();
        row.insert("localAvatarPath", avatar.isEmpty() ? QString() : imagePath(avatar));
        visible.append(row);
    }
    syncRows(streamModel(kind), visible);
}

void TiebaController::updateForumRows()
{
    QVariantList visible;
    for (int i = 0; i < m_forumSourceRows.size(); ++i) {
        QVariantMap row = m_forumSourceRows.at(i).toMap();
        if (m_hideVideoThreads && row.value("isVideo").toBool() && !row.value("isTop").toBool()) continue;
        row.insert("localAvatarPath", imagePath(row.value("remoteAvatarUrl").toString()));
        row.insert("localImagePath", imagePath(row.value("remoteImageUrl").toString()));
        visible.append(row);
    }
    if (syncRows(m_threads, visible)) emit threadsChanged();
}

void TiebaController::setShowFollowedOnly(bool value)
{
    if (m_showFollowedOnly == value)
        return;
    m_showFollowedOnly = value;
    saveSettings();
    updateStreamRows("recommend");
    ++m_streamVersion; emit streamsChanged();
    emit settingsChanged();
}

void TiebaController::applyCacheLimit()
{
    if (!m_imageCache)
        return;
    // MB -> 字节。用 qint64 算：512MB 已经超过 int 能安全表示的范围的一半，
    // 直接 int 乘法在 32 位上会溢出。
    const qint64 bytes = (qint64)m_cacheLimitMb * 1024LL * 1024LL;
    m_imageCache->setMaxCacheBytes(bytes);
}

/*! 字节数 -> "12.3 MB" / "1.05 GB"（设置页显示用） */
static QString humanSize(qint64 bytes)
{
    const double mb = (double)bytes / (1024.0 * 1024.0);
    if (mb >= 1024.0)
        return QString::number(mb / 1024.0, 'f', 2) + QLatin1String(" GB");
    return QString::number(mb, 'f', 1) + QLatin1String(" MB");
}

int TiebaController::cacheUsagePercent() const
{
    if (m_cacheLimitMb <= 0 || !m_imageCache)
        return 0;
    const qint64 limit = (qint64)m_cacheLimitMb * 1024LL * 1024LL;
    const qint64 used = m_imageCache->cacheBytes();
    if (used <= 0 || limit <= 0)
        return 0;
    const int percent = (int)((used * 100LL) / limit);
    return percent > 100 ? 100 : percent;
}

QString TiebaController::cacheUsageText() const
{
    if (!m_imageCache)
        return QString();
    const qint64 used = m_imageCache->cacheBytes();
    const QString limitText = QString::number(m_cacheLimitMb) + QLatin1String(" MB");
    return QString::fromUtf8("已用 ") + humanSize(used)
           + QLatin1String(" / ") + limitText
           + QString::fromUtf8("（") + QString::number(cacheUsagePercent())
           + QString::fromUtf8("%）");
}

void TiebaController::refreshCacheUsage()
{
    if (m_imageCache)
        m_imageCache->invalidateCacheBytes();
    emit cacheUsageChanged();
}

void TiebaController::clearImageCache()
{
    if (!m_imageCache) {
        showToast(TbTr("E7BC93E5AD9820E69CAAE5B0B1E7BBAA"));
        return;
    }
    const qint64 before = m_imageCache->cacheBytes();
    m_imageCache->clearCache();
    qWarning("TiebaController: 已清除图片缓存（原 %s）",
             qPrintable(humanSize(before)));
    showToast(TbTr("E5B7B2E6B885E999A4E59BBEE78987E7BC93E5AD9820") + humanSize(before));
    emit cacheUsageChanged();
}

/* ------------------------- 吧列表本地缓存 ------------------------- */
/*!
 * 缓存文件路径：<DataLocation>/config/forums.json
 *
 * 用 QDesktopServices::DataLocation（Qt 4.8 取应用私有可写目录的官方办法），
 * 在 BB10 上会落在 /accounts/1000/appdata/<appid>/data 下面 —— 持久、
 * 且卸载 App 时会被清理，不需要额外权限。
 */
QString TiebaController::forumCacheFile() const
{
    // QtCore homePath is private persistent storage on BB10. Reuse the
    // settings directory and its upgrade migration; no QtGui dependency.
    const QString dir = configDir();
    return dir.isEmpty() ? QString() : dir + QLatin1String("/forums.json");
}
/* ---------------------- 测试钩子实现 ---------------------- */

void TiebaController::setCacheDirForTest(const QString &dir, const QString &legacyConfigDir)
{
    m_cacheDirOverride = dir;
    m_legacyConfigDirForTest = legacyConfigDir;
    if (m_userDataCache) m_userDataCache->setDirectory(dir + "/user-cache");
    if (m_favoriteService) m_favoriteService->setCacheDirectoryForTest(dir + "/user-cache");
    /*
     * ★ 换目录后要**重新读一次设置**。
     *
     * loadSettings() 只在构造函数里跑，那时 m_cacheDirOverride 还是空的
     * （指向默认目录）。测试里是先构造、再 setCacheDirForTest()，
     * 不重读的话拿到的还是默认目录里那份 —— 表现是"设置写了但读不回来"。
     * 对生产代码没有影响（只有测试会调这个方法）。
     */
    loadSettings();
}

void TiebaController::setSessionFileForTest(const QString &path)
{
    m_session->setSettingsFileForTest(path);
    // 换道之后重新读一遍：此时拿到的才是新文件里的内容
    m_session->load();
    rebuildAccounts();
}

QString TiebaController::forumCacheFileForTest() const
{
    return forumCacheFile();
}

void TiebaController::setForumsModelForTest(const QVariantList &forumMaps)
{
    fillForumsModel(forumMaps);
}

int TiebaController::forumsForTestCount() const
{
    return m_forums ? m_forums->size() : 0;
}

QVariantList TiebaController::forumsModelForTest() const
{
    QVariantList out;
    if (!m_forums)
        return out;
    for (int i = 0; i < m_forums->size(); ++i)
        out.append(m_forums->value(i));
    return out;
}

/*!
 * 从缓存加载吧列表。
 *
 * 文件格式（自己定义的，很简单）：
 *     { "savedAt": <unix 秒>, "forums": [ {..TbForum 字段..}, ... ] }
 *
 * 过期判断用文件里的 savedAt 而不是 mtime —— 更可靠，也方便测试。
 * 注意：**过期也会加载**，只是调用方知道该照常刷新。这里不做"过期就不显示"，
 * 因为陈旧的总比空白好，而且调用方无论如何都会去请求一次。
 */
bool TiebaController::loadForumsFromCache()
{
    const QString file = forumCacheFile();
    if (file.isEmpty() || !QFile::exists(file))
        return false;

    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning("TiebaController: 缓存打不开 %s", qPrintable(file));
        return false;
    }
    const QByteArray raw = f.readAll();
    f.close();

    const TbJson root = TbJson::parse(raw);
    if (!root.isObject()) {
        qWarning("TiebaController: 缓存不是合法 JSON，忽略并删除");
        clearForumsCache();
        return false;
    }

    const TbJson arr = TbJson::array(root, QLatin1String("forums"));
    if (arr.size() <= 0) {
        qWarning("TiebaController: 缓存里没有吧列表，忽略");
        return false;
    }

    QVariantList maps;
    for (int i = 0; i < arr.size(); ++i) {
        const TbJson node = arr.at(i);
        if (!node.isObject())
            continue;
        QVariantMap m;
        m.insert(QLatin1String("id"), TbJson::str(node, QLatin1String("id")));
        m.insert(QLatin1String("name"), TbJson::str(node, QLatin1String("name")));
        m.insert(QLatin1String("level"),
                 TbJson::integer(node, QLatin1String("level"), 0));
        m.insert(QLatin1String("avatarUrl"),
                 TbJson::str(node, QLatin1String("avatarUrl")));
        m.insert(QLatin1String("isSigned"),
                 TbJson::boolean(node, QLatin1String("isSigned"), false));
        // levelText 是 TbForum::toVariantMap 里算出来的展示字段，这里同样补上
        m.insert(QLatin1String("levelText"),
                 QString::number(m.value(QLatin1String("level")).toInt()));
        maps.append(m);
    }

    if (maps.isEmpty())
        return false;

    fillForumsModel(maps);

    /*
     * 打印缓存年龄。
     *
     * 注意格式串用 %d 而不是 %lld —— Windows MinGW 的 printf 不认 "ll"，
     * 会报 "unknown conversion type character 'l' in format"。
     * 时间差先夹到 int 范围，避免溢出。
     */
    const qint64 savedAt = (qint64)TbJson::integer(root, QLatin1String("savedAt"), 0);
    qint64 ageSec = (savedAt > 0)
                        ? (qint64)QDateTime::currentDateTime().toTime_t() - savedAt
                        : -1;
    if (ageSec > 2147483647LL)
        ageSec = 2147483647LL;
    qWarning("TiebaController: 吧列表缓存命中 %d 个（%d 秒前保存）",
             maps.size(), (int)ageSec);
    return true;
}

/*! 把当前吧列表写进缓存 */
void TiebaController::saveForumsToCache() const
{
    if (!m_forums || m_forums->size() <= 0)
        return;

    const QString file = forumCacheFile();
    if (file.isEmpty())
        return;

    // 只存渲染需要的字段：模型里还塞了 localAvatarPath/remoteImageUrl 等
    // 运行时字段，存进去没意义，而且下次路径可能不同。
    QVariantList out;
    for (int i = 0; i < m_forums->size(); ++i) {
        const QVariantMap src = m_forums->value(i).toMap();
        QVariantMap m;
        m.insert(QLatin1String("id"), src.value(QLatin1String("id")));
        m.insert(QLatin1String("name"), src.value(QLatin1String("name")));
        m.insert(QLatin1String("level"), src.value(QLatin1String("level")));
        m.insert(QLatin1String("avatarUrl"), src.value(QLatin1String("avatarUrl")));
        m.insert(QLatin1String("isSigned"), src.value(QLatin1String("isSigned")));
        out.append(m);
    }

    /*
     * 手写 JSON 而不是用 QJsonDocument —— 那样得从 QVariant 逐层转成
     * QJsonObject，代码量更大；这里结构固定，直接拼更直观。
     * 注意字符串要转义（吧名里可能有引号/反斜杠/中文）。
     */
    QByteArray json;
    json += "{\"savedAt\":";
    json += QByteArray::number((qint64)QDateTime::currentDateTime().toTime_t());
    json += ",\"forums\":[";
    for (int i = 0; i < out.size(); ++i) {
        const QVariantMap m = out.at(i).toMap();
        if (i > 0)
            json += ",";
        json += "{\"id\":";
        json += jsonString(m.value(QLatin1String("id")).toString());
        json += ",\"name\":";
        json += jsonString(m.value(QLatin1String("name")).toString());
        json += ",\"level\":";
        json += QByteArray::number(m.value(QLatin1String("level")).toInt());
        json += ",\"avatarUrl\":";
        json += jsonString(m.value(QLatin1String("avatarUrl")).toString());
        json += ",\"isSigned\":";
        json += m.value(QLatin1String("isSigned")).toBool() ? "true" : "false";
        json += "}";
    }
    json += "]}";

    QFile f(file);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning("TiebaController: 缓存写不了 %s", qPrintable(file));
        return;
    }
    f.write(json);
    f.close();
    qWarning("TiebaController: 吧列表已缓存 %d 个 -> %s", out.size(), qPrintable(file));
}

void TiebaController::clearForumsCache() const
{
    const QString file = forumCacheFile();
    if (!file.isEmpty() && QFile::exists(file))
        QFile::remove(file);
}

/*!
 * 用给定的吧列表填充模型（缓存与网络共用）。
 *
 * 这里做两件网络路径和缓存路径都必须一致的事：
 *   1. 记下 remoteImageUrl（refreshImagePaths 重算本地路径要用）
 *   2. 解析 localAvatarPath，并顺带把没缓存的头像入队下载
 */
void TiebaController::fillForumsModel(const QVariantList &forumMaps)
{
    QVariantList items;
    for (int i = 0; i < forumMaps.size(); ++i) {
        QVariantMap map = forumMaps.at(i).toMap();
        const QString avatar = map.value(QLatin1String("avatarUrl")).toString();
        map.insert(QLatin1String("remoteImageUrl"), avatar);
        map.insert(QLatin1String("localAvatarPath"),
                   (avatar.isEmpty() || !m_imageCache)
                       ? QString() : m_imageCache->pathFor(avatar));
        items.append(map);
    }

    fillModel(m_forums, items);
    emit forumsChanged();
}

void TiebaController::onForumListLoaded(int requestId,
                                        const tieba::TbForumListResult &result)
{
    if (requestId != m_forumListRequestId) {
        qDeleteAll(result.forums);
        return;
    }
    m_forumListRequestId = -1;

    m_lastRaw = result.raw;
    emit lastRawChanged();

    if (!result.errorCode.isEmpty() && result.errorCode != QLatin1String("0")) {
        const QString message = result.errorMessage.isEmpty()
                                    ? TbParsers::describeErrorCode(result.errorCode)
                                    : result.errorMessage;
        setError(message, buildApiErrorDetail(result.errorCode, message, result.raw,
                                             result.httpStatus,
                                             QLatin1String(kWhatForums)));

        qDeleteAll(result.forums);
        m_forumAccum.clear();
        finishRequest(QLatin1String(kWhatForums), false);
        return;
    }

    /*
     * ★ 分页累加（这是"关注吧列表加载不全"的修复点）
     *
     * /c/f/forum/like 一页只给 ~50 条，关注数多的用户必须翻页。
     * 这里把本页结果追加进累加器，并**按 id 去重**（实测页之间可能重叠）：
     *     has_more 为真且没到页数上限 -> 请求下一页（先不填模型，避免闪烁）
     *     否则                        -> 一次性填模型 + 写缓存
     */
    int added = 0;
    int duplicated = 0;
    for (int i = 0; i < result.forums.size(); ++i) {
        tieba::TbForum *f = result.forums.at(i);
        const QString id = f->id();
        // id 为空的记账用吧名，免得同一条被无限累加
        const QString key = id.isEmpty() ? (QLatin1String("n:") + f->name()) : id;
        if (m_forumSeenIds.contains(key)) {
            ++duplicated;
            continue;
        }
        m_forumSeenIds.insert(key);
        m_forumAccum.append(f->toVariantMap());
        ++added;
    }

    // 原始对象数据已取出，可以释放
    qDeleteAll(result.forums);

    qWarning("TiebaController: 吧列表第 %d 页 收到 %d 条（新增 %d，重复 %d），累计 %d，has_more=%d",
             m_forumPage, added + duplicated, added, duplicated,
             m_forumAccum.size(), result.hasMore ? 1 : 0);

    const bool more = result.hasMore && m_forumPage < kForumMaxPages;
    if (more) {
        ++m_forumPage;
        m_forumListRequestId = m_api->fetchForumListPaged(m_forumPage, 200);
        return;
    }

    if (result.hasMore)
        qWarning("TiebaController: 吧列表达到页数上限 %d，可能仍未取完", kForumMaxPages);

    /*
     * 构造吧列表模型项。
     *
     * ★ 为什么由 C++ 塞 localAvatarPath，而不是让 QML 调 tieba.imagePath()：
     *   ListView 的 ListItemComponent 是**独立上下文**，在里面访问页面的
     *   context property（tieba）会报 ReferenceError: Can't find variable。
     *   一律由 C++ 解析好路径、当**数据字段**传下去，QML 只读 ListItemData。
     *
     *   顺带 pathFor() 会把没缓存过的头像入队下载，下载完由
     *   onImageReady() -> refreshImagePaths() 刷新模型。
     *
     *   填充逻辑抽到 fillForumsModel()，因为**缓存恢复也要走同一套处理**
     *   （否则从缓存进来时头像路径和 remoteImageUrl 就丢了）。
     */
    fillForumsModel(m_forumAccum);

    // ★ 拉取成功 -> 写缓存，下次启动就能立刻显示
    saveForumsToCache();

    /*
     * ★ 顺手把"我是谁"补上。
     *
     *   吧列表响应里**没有**用户信息，所以拿第一个关注的吧去打一次吧页 ——
     *   吧页响应带 user（id/name/portrait）+ anti.tbs（见 TbPcParsers）。
     *   这是目前唯一能确认"当前账号是谁"的途径，代价是一次很轻的请求。
     */
    maybeProbeAccountProfile();

    finishRequest(QLatin1String(kWhatForums), true);
}

void TiebaController::maybeProbeAccountProfile()
{
    if (!m_session->isLoggedIn())
        return;
    // 已经有昵称了就不用再打一次
    if (!m_session->nickName().isEmpty() && !m_session->uid().isEmpty())
        return;
    if (m_profileProbeRequestId >= 0)
        return;                     // 已经有一个在途了
    if (m_forums->size() <= 0)
        return;

    const QString name = m_forums->value(0).toMap()
                             .value(QLatin1String("name")).toString();
    if (name.isEmpty())
        return;

    qWarning("TiebaController: 用「%s」吧打一次吧页，取当前账号资料",
             qPrintable(name));

    /*
     * ★ 记下这个请求 id：它的响应**不能**灌进贴子模型（用户没打开这个吧），
     *   见 onForumPageLoaded 里对 m_profileProbeRequestId 的处理。
     */
    m_profileProbeRequestId = m_api->fetchForumPagePc(name, 1);
}

/* ============================ 吧页 ============================ */

void TiebaController::loadForumPage(const QString &forumName, int page,
                                    int sortType, bool append, bool goodOnly)
{
    if (forumName.trimmed().isEmpty()) {
        setError(TbTr("E590A7E5908DE4B88DE883BDE4B8BAE7A9BA"));
        emit requestFinished(QLatin1String(kWhatForumPage), false);
        return;
    }

    if (!append) {
        /*
         * ★ 换吧时**先清空贴子列表**（和 loadThread 里修"切贴显示上一个贴"同一招）。
         *
         * 不清的话：点进第二个吧时 ListView 仍绑着上一个吧的 threads，
         * 会一直显示上一个吧的贴子直到新数据回来 —— 就是"切换吧仍然显示
         * 上一个吧内容"。
         *
         * 只在**吧真的换了**时清：同一个吧内刷新（刷新按钮/切筛选）保留旧内容，
         * 不然每次刷新都白屏一下。
         */
        const bool forumChanged = (m_currentForumName != forumName);
        if (forumChanged) m_forumSourceRows.clear();
        if (forumChanged) {
            clearModel(m_threads);
            emit threadsChanged();
            // 吧信息也一起清，否则新吧的标题栏会短暂显示上一个吧的信息
            if (! m_forumInfo.isEmpty()) {
                m_forumInfo.clear();
                ++m_forumInfoVersion;
                emit forumInfoChanged();
            }
        }

        m_currentForumName = forumName;
        m_currentThreadId.clear();
        emit currentForumNameChanged();
        emit currentThreadIdChanged();
        m_lastPage = 0;

        if (m_hasMore) {
            m_hasMore = false;
            emit hasMoreChanged();
        }
    }

    setError(QString());
    setLoading(true);
    m_pendingForumPageAppend = append;
    m_forumPageRequestId = m_api->fetchForumPagePc(forumName, page, sortType,
                                                      goodOnly);
}

void TiebaController::onForumPageLoaded(int requestId,
                                        const tieba::TbPcForumPageResult &result)
{
    if (m_forumStatsRequests.contains(requestId)) {
        const QString name = m_forumStatsRequests.take(requestId);
        if (result.forumInfo && (result.errorCode.isEmpty() || result.errorCode == "0")) {
            m_forumMemberCounts.insert(name, result.forumInfo->memberCountText());
            ++m_contextVersion;
            emit contextChanged();
        }
        qDeleteAll(result.threads);
        delete result.forumInfo;
        delete result.pageInfo;
        delete result.user;
        return;
    }
    /*
     * ★ 只为"取当前账号资料"发的那次吧页请求（见 maybeProbeAccountProfile）。
     *
     *   它是 C++ 自己发起的，用户并没有打开这个吧 ——
     *   所以**绝不能**把它的贴子灌进 m_threads（那会让关注页之外的界面
     *   莫名其妙多出一个吧的内容），只取 user / tbs 就走。
     */
    if (requestId == m_profileProbeRequestId) {
        m_profileProbeRequestId = -1;
        applyAccountFromResult(result);
        qDeleteAll(result.threads);
        delete result.forumInfo;
        delete result.pageInfo;
        delete result.user;
        return;
    }

    if (requestId != m_forumPageRequestId) {
        // 过期响应（用户已经切到别的吧了），丢弃
        qDeleteAll(result.threads);
        delete result.forumInfo;
        delete result.pageInfo;
        delete result.user;
        return;
    }
    m_forumPageRequestId = -1;

    // 顺手把"当前账号是谁"更新一下（吧页响应里带，见 applyAccountFromResult）
    applyAccountFromResult(result);
    delete result.user;

    m_lastRaw = result.raw;
    emit lastRawChanged();

    if (!result.errorCode.isEmpty() && result.errorCode != QLatin1String("0")) {
        const QString message = result.errorMessage.isEmpty()
                                    ? TbParsers::describeErrorCode(result.errorCode)
                                    : result.errorMessage;
        setError(message, buildApiErrorDetail(result.errorCode, message, result.raw,
                                             result.httpStatus,
                                             QLatin1String(kWhatForumPage)));
        qDeleteAll(result.threads);
        delete result.forumInfo;
        delete result.pageInfo;
        delete result.user;
        m_pendingForumPageAppend = false;
        finishRequest(QLatin1String(kWhatForumPage), false);
        return;
    }

    // ★ 顺序很重要：先把 forumInfo / pageInfo 放好，再灌模型。
    //   因为灌模型会立刻触发 QML 的 ListScrollStateHandler（onAtEndChanged），
    //   而 QML 里要读 tieba.pageInfo.currentPage。如果那时 pageInfo 还是空 map，
    //   QML 在 undefined 上取属性会直接把进程打挂
    //   （SIGSEGV in QString::operator=，实测过）。
    if (result.forumInfo) {
        m_forumInfo = result.forumInfo->toVariantMap();
        m_forumMemberCounts.insert(result.forumInfo->name(), result.forumInfo->memberCountText());
        ++m_contextVersion;
        emit contextChanged();
        delete result.forumInfo;
        ++m_forumInfoVersion;
    emit forumInfoChanged();
    }
    if (result.pageInfo) {
        m_pageInfo = result.pageInfo->toVariantMap();
        m_lastPage = result.pageInfo->currentPage();
        const bool more = result.pageInfo->hasMore();
        delete result.pageInfo;

        if (m_hasMore != more) {
            m_hasMore = more;
            emit hasMoreChanged();
        }
        emit pageInfoChanged();
    }

    /*
     * 视频贴过滤（设置页"隐藏视频贴"）。
     *
     * 和安卓版 BlockSettings 的 blockVideo 一致：过滤发生在**列表装配时**，
     * 而不是请求参数里（服务端没有这个开关）。
     * 置顶贴不参与过滤 —— 置顶是吧务行为，用户设"不看视频"不该影响它，
     * 安卓那边也是先 filter 再拼置顶的。
     */
    QVariantList items;
    int hiddenVideo = 0;
    for (int i = 0; i < result.threads.size(); ++i) {
        tieba::TbThread *t = result.threads.at(i);
        if (m_hideVideoThreads && t->isVideo() && !t->isTop()) ++hiddenVideo;
        QVariantMap row = t->toVariantMap();
        row.insert(QLatin1String("forumName"), m_currentForumName);
        row.insert(QLatin1String("remoteAvatarUrl"), t->avatarUrl());
        row.insert(QLatin1String("localAvatarPath"), imagePath(t->avatarUrl()));
        row.insert(QLatin1String("remoteImageUrl"), t->firstImageUrl());
        row.insert(QLatin1String("localImagePath"), imagePath(t->firstImageUrl()));
        items.append(row);
    }
    qDeleteAll(result.threads);
    if (hiddenVideo > 0)
        qWarning("TiebaController: 按设置隐藏了 %d 个视频贴", hiddenVideo);

    // 追加模式：保留已有列表（用发起请求时记录的意图，而不是分页标志）
    if (m_pendingForumPageAppend) m_forumSourceRows.append(items);
    else m_forumSourceRows = items;
    updateForumRows();
    m_pendingForumPageAppend = false;
    emit threadsChanged();

    finishRequest(QLatin1String(kWhatForumPage), true);
}

/* ============================ 贴子楼层 ============================ */

void TiebaController::loadThread(const QString &threadId, int page,
                                 bool onlyOriginalPoster, bool reverse, bool append)
{
    requestThreadPage(threadId, page, onlyOriginalPoster, reverse, append, false);
}

QVariantMap TiebaController::threadNavigationState(const QString &threadId) const
{
    QVariantMap result;
    result.insert("page", 1); result.insert("hasMore", false); result.insert("loading", false);
    result.insert("reverse", false); result.insert("targetIndex", -1); result.insert("targetVersion", 0);
    result.insert("error", QString());
    if (threadId == m_currentThreadId) {
        for (QVariantMap::const_iterator it = m_threadNavigation.begin(); it != m_threadNavigation.end(); ++it) result.insert(it.key(), it.value());
    }
    return result;
}

void TiebaController::jumpThreadFloor(const QString &threadId, const QString &input, bool onlyOp, bool reverse)
{
    const int floor = TbThreadNavigation::positiveFloor(input);
    if (!floor) { showToast(QString::fromUtf8("请输入有效的正整数楼层")); return; }
    if (threadId == m_currentThreadId && !m_threadNavigation.value("loading").toBool()) {
        QVariantList rows;
        for (int i = 0; i < m_posts->size(); ++i) rows.append(m_posts->value(i));
        const int index = TbThreadNavigation::locateFloor(rows, floor);
        if (index >= 0) {
            m_threadNavigation.insert("targetIndex", index);
            m_threadNavigation.insert("targetVersion", m_threadNavigation.value("targetVersion").toInt() + 1);
            ++m_threadNavigationVersion; emit threadNavigationChanged(); return;
        }
    }
    m_jumpFloor = floor; m_jumpLow = 1; m_jumpHigh = 0; m_jumpPages.clear();
    m_jumpPostId.clear(); m_jumpSubPostId.clear();
    requestThreadPage(threadId, TbThreadNavigation::estimatedPage(floor), onlyOp, reverse, false, true);
}

void TiebaController::openThreadPost(const QString &threadId, const QString &postId, const QString &subPostId, bool onlyOp, bool reverse)
{
    if (postId.isEmpty() && subPostId.isEmpty()) { loadThread(threadId, 1, onlyOp, reverse); return; }
    m_jumpFloor = 0; m_jumpPages.clear(); m_jumpPostId = postId; m_jumpSubPostId = subPostId;
    requestThreadPage(threadId, 1, onlyOp, reverse, false, true);
}

void TiebaController::requestThreadPage(const QString &threadId, int page,
                                 bool onlyOriginalPoster, bool reverse, bool append, bool preserveJump)
{
    if (!preserveJump) { m_jumpFloor = 0; m_jumpPostId.clear(); m_jumpSubPostId.clear(); m_jumpPages.clear(); }

    if (threadId.trimmed().isEmpty()) {
        setError(TbTr("E8B4B4E5AD9020696420E4B88DE883BDE4B8BAE7A9BA"));
        emit requestFinished(QLatin1String(kWhatThread), false);
        return;
    }

    if (!append) {
        /*
         * ★ 切换贴子时**先把楼层模型清空**。
         *
         * 不清的话：点开第二个贴时，ListView 仍绑着上一个贴的 posts，
         * 会一直显示上一个贴的内容，直到新数据回来（明显的一闪）。
         * 清空后先露出空列表，新数据到达再填 —— 体感是"立刻切换"。
         *
         * 只在**贴子真的换了**时清：同一贴内刷新（刷新按钮/只看楼主）时
         * 保留旧内容更舒服，不然每次刷新都白屏一下。
         */
        const bool threadChanged = (m_currentThreadId != threadId);
        if (threadChanged) {
            m_threadNavigation.clear();
            m_currentThreadId = threadId;
            emit currentThreadIdChanged();

            clearModel(m_posts);
            emit postsChanged();

            // 贴子信息也一起清，否则新贴的标题栏会短暂显示上一个贴的标题
            if (! m_threadInfo.isEmpty()) {
                m_threadInfo.clear();
                emit threadInfoChanged();
            }
        }
        m_lastPage = 0;

        if (m_hasMore) {
            m_hasMore = false;
            emit hasMoreChanged();
        }
    }

    setError(QString());
    setLoading(true);
    m_pendingThreadAppend = append;
    m_threadOnlyOp = onlyOriginalPoster;
    if (!append) {
        m_threadNavigation.insert("targetIndex", -1);
        m_threadNavigation.insert("hasMore", false);
    }
    m_threadNavigation.insert("page", qMax(1, page));
    m_threadNavigation.insert("reverse", reverse);
    m_threadNavigation.insert("loading", true);
    m_threadNavigation.insert("error", QString());
    m_jumpPages.insert(qMax(1, page));
    m_threadRequestId = m_api->fetchThreadContentPc(threadId, page,
                                                    onlyOriginalPoster, reverse, m_jumpPostId.isEmpty() ? m_jumpSubPostId : m_jumpPostId, m_jumpSubPostId);
    ++m_threadNavigationVersion; emit threadNavigationChanged();
}

void TiebaController::onThreadContentLoaded(int requestId,
                                            const tieba::TbPcThreadResult &result)
{
    if (requestId != m_threadRequestId) {
        qDeleteAll(result.posts);
        delete result.threadInfo;
        delete result.pageInfo;
        delete result.forumInfo;
        return;
    }
    m_threadRequestId = -1;

    m_lastRaw = result.raw;
    emit lastRawChanged();

    if (!result.errorCode.isEmpty() && result.errorCode != QLatin1String("0")) {
        const QString message = result.errorMessage.isEmpty()
                                    ? TbParsers::describeErrorCode(result.errorCode)
                                    : result.errorMessage;
        setError(message, buildApiErrorDetail(result.errorCode, message, result.raw,
                                             result.httpStatus,
                                             QLatin1String(kWhatThread)));
        qDeleteAll(result.posts);
        delete result.threadInfo;
        delete result.pageInfo;
        delete result.forumInfo;
        m_pendingThreadAppend = false;
        m_threadNavigation.insert("loading", false);
        m_threadNavigation.insert("error", message);
        ++m_threadNavigationVersion; emit threadNavigationChanged();
        finishRequest(QLatin1String(kWhatThread), false);
        return;
    }

    if (result.anchorResolved) {
        QVariantList resolvedRows;
        for (int i = 0; i < result.posts.size(); ++i) resolvedRows.append(result.posts.at(i)->toVariantMap());
        const int resolvedIndex = TbThreadNavigation::locatePost(resolvedRows, m_jumpPostId, m_jumpSubPostId);
        const int resolvedFloor = resolvedIndex >= 0 ? resolvedRows.at(resolvedIndex).toMap().value("floor").toInt() : 0;
        qDeleteAll(result.posts); delete result.threadInfo; delete result.pageInfo; delete result.forumInfo;
        if (resolvedFloor > 0) {
            jumpThreadFloor(m_currentThreadId, QString::number(resolvedFloor), m_threadOnlyOp, m_threadNavigation.value("reverse").toBool());
        } else {
            const QString message = QString::fromUtf8("目标回复已删除或无法定位");
            m_threadNavigation.insert("loading", false); m_threadNavigation.insert("error", message);
            m_jumpFloor = 0; m_jumpPostId.clear(); m_jumpSubPostId.clear();
            ++m_threadNavigationVersion; emit threadNavigationChanged(); showToast(message);
            finishRequest(QLatin1String(kWhatThread), false);
        }
        return;
    }
    const QVariantMap navigationPage = result.pageInfo ? result.pageInfo->toVariantMap() : QVariantMap();
    // ★ 与 onForumPageLoaded 同理：pageInfo / threadInfo 必须先于模型更新就位，
    //   否则 QML 的滚动绑定会读到空 map。
    if (result.threadInfo) {
        const QVariantMap previous = m_threadInfo;
        m_threadInfo = result.threadInfo->toVariantMap();
        if (result.forumInfo) m_threadInfo.insert("forumName", result.forumInfo->name());
        if (previous.value("id") == m_threadInfo.value("id")) {
            m_threadInfo.insert("authorName", previous.value("authorName"));
            m_threadInfo.insert("forumName", previous.value("forumName"));
        }
        for (int i = 0; i < m_threads->size(); ++i) {
            const QVariantMap row = m_threads->value(i).toMap();
            if (row.value("id") == m_threadInfo.value("id")) {
                if (m_threadInfo.value("authorName").toString().isEmpty()) m_threadInfo.insert("authorName", row.value("authorName"));
                if (m_threadInfo.value("forumName").toString().isEmpty()) m_threadInfo.insert("forumName", row.value("forumName"));
                break;
            }
        }
        for (int i = 0; i < result.posts.size(); ++i) {
            if (result.posts.at(i)->floorNumber() == 1) {
                m_threadInfo.insert("authorName", result.posts.at(i)->authorName());
                break;
            }
        }
        delete result.threadInfo;
        emit threadInfoChanged();
        ++m_contextVersion;
        emit contextChanged();
        if (!m_pendingThreadAppend) m_threadLibrary->recordVisit(m_threadInfo);
    }
    if (result.pageInfo) {
        m_pageInfo = result.pageInfo->toVariantMap();
        m_lastPage = result.pageInfo->currentPage();
        const bool more = result.pageInfo->hasMore();
        delete result.pageInfo;

        if (m_hasMore != more) {
            m_hasMore = more;
            emit hasMoreChanged();
        }
        emit pageInfoChanged();
    }

    // 楼层里嵌套了楼中楼（TbPost*），转 map 之后才能释放
    m_threadTotalPages = navigationPage.value("totalPage").toInt();
    if (!navigationPage.isEmpty()) m_threadNavigation.insert("page", qMax(1, navigationPage.value("currentPage", 1).toInt()));
    m_threadNavigation.insert("hasMore", navigationPage.value("hasMore", false));
    QVariantList posts;
    for (int i = 0; i < result.posts.size(); ++i)
        posts.append(result.posts.at(i)->toVariantMap());
    /*
     * ★ 展开：正文一项 + 每张图各一项。
     *
     * Cascades 的 ListItemComponent 里没法遍历数组（没有 Repeater，
     * 在 item 里再套 ListView 又会崩），所以"一个楼层多张图"必须在
     * C++ 侧拆成多个模型项，QML 那边一项永远只有一张图。
     */
    QVariantList items;
    expandImageItems(posts, items);

    if (m_pendingThreadAppend)
        m_posts->append(items);
    else
        fillModel(m_posts, items);
    m_pendingThreadAppend = false;
    emit postsChanged();

    int target = -1;
    if (m_jumpFloor > 0) target = TbThreadNavigation::locateFloor(items, m_jumpFloor);
    else if (!m_jumpPostId.isEmpty() || !m_jumpSubPostId.isEmpty()) target = TbThreadNavigation::locatePost(items, m_jumpPostId, m_jumpSubPostId);
    int nextPage = 0;
    if (m_jumpFloor > 0 && target < 0 && m_jumpPages.size() < 20)
        nextPage = TbThreadNavigation::nextFloorPage(m_jumpFloor, m_threadNavigation.value("page").toInt(), m_threadTotalPages,
            items, m_threadNavigation.value("reverse").toBool(), m_jumpLow, m_jumpHigh);
    if (m_jumpPages.contains(nextPage)) nextPage = 0;
    if (target >= 0) {
        m_threadNavigation.insert("targetIndex", target);
        m_threadNavigation.insert("targetVersion", m_threadNavigation.value("targetVersion").toInt() + 1);
    } else if (!nextPage && (m_jumpFloor > 0 || !m_jumpPostId.isEmpty() || !m_jumpSubPostId.isEmpty())) {
        const QString message = QString::fromUtf8("目标楼层或回复不存在、已删除或不在当前筛选中");
        m_threadNavigation.insert("error", message);
        showToast(message);
    }
    m_threadNavigation.insert("loading", nextPage > 0);
    ++m_threadNavigationVersion; emit threadNavigationChanged();

    // 释放：先删楼中楼，再删楼层本身
    for (int i = 0; i < result.posts.size(); ++i) {
        qDeleteAll(result.posts.at(i)->children());
        delete result.posts.at(i);
    }
    delete result.forumInfo;

    if (nextPage > 0) {
        requestThreadPage(m_currentThreadId, nextPage, m_threadOnlyOp, m_threadNavigation.value("reverse").toBool(), false, true);
    } else {
        m_jumpFloor = 0; m_jumpPostId.clear(); m_jumpSubPostId.clear();
        finishRequest(QLatin1String(kWhatThread), true);
    }
}

/* ============================ 楼中楼 ============================ */

void TiebaController::loadSubPosts(const QString &threadId, const QString &postId,
                                   int page)
{
    if (threadId.isEmpty() || postId.isEmpty()) {
        setError(TbTr("E6A5BCE4B8ADE6A5BCE8AFB7E6B182E7BCBAE5B091E58F82E695B0"));
        emit requestFinished(QLatin1String(kWhatSubPosts), false);
        return;
    }

    setError(QString());
    setLoading(true);
    /*
     * ★ 记录这次是"翻页追加"还是"从头替换"。
     *
     * 第 1 页 = 进页面/刷新 -> 替换；
     * 第 2 页起 = 滚到底自动加载 -> **追加**。
     *
     * 为什么必须区分：以前这里无脑 fillModel()（整表重建），
     * 结果滚到底触发翻页时，新一页**替换**了原来那页 ——
     * 表现就是"滑到底突然没有楼中楼了"。
     * （同样的问题在 onThreadContentLoaded 里早就用 m_pendingThreadAppend 解决了，
     *   楼中楼这边漏了。）
     */
    m_pendingSubPostsAppend = (page > 1);
    if (!m_pendingSubPostsAppend) {
        // 进页/刷新时立即清掉上一层的回复，不能等网络响应后才替换。
        clearModel(m_subPosts);
        emit subPostsChanged();
        m_pageInfo.clear();
        m_lastPage = 0;
        emit pageInfoChanged();
        if (m_hasMore) {
            m_hasMore = false;
            emit hasMoreChanged();
        }
    }
    m_subPostsRequestId = m_api->fetchSubFloorsPc(threadId, postId, page);
}

void TiebaController::onSubFloorsLoaded(int requestId,
                                        const tieba::TbPcSubFloorResult &result)
{
    // 只认最后一次请求（用户快速滚动时可能有多个在途）
    if (requestId != m_subPostsRequestId) {
        qDeleteAll(result.posts);
        delete result.pageInfo;
        return;
    }
    m_subPostsRequestId = -1;

    m_lastRaw = result.raw;
    emit lastRawChanged();

    if (!result.errorCode.isEmpty() && result.errorCode != QLatin1String("0")) {
        const QString message = result.errorMessage.isEmpty()
                                    ? TbParsers::describeErrorCode(result.errorCode)
                                    : result.errorMessage;
        setError(message, buildApiErrorDetail(result.errorCode, message, result.raw,
                                             result.httpStatus,
                                             QLatin1String(kWhatSubPosts)));
        qDeleteAll(result.posts);
        delete result.pageInfo;
        m_pendingSubPostsAppend = false;
        finishRequest(QLatin1String(kWhatSubPosts), false);
        return;
    }

    /*
     * ★ pageInfo / hasMore 必须先于模型更新就位。
     *
     * 以前这里**根本没设 m_hasMore** —— 而 SubPostsPage.qml 的
     * ListScrollStateHandler 是靠 `tieba.hasMore` 判断要不要继续翻页的。
     * 不设的话要么永远不翻（hasMore 一直是上一次贴子页留下的值），
     * 要么翻错。所以这里按贴子页的写法补齐。
     */
    if (result.pageInfo) {
        m_pageInfo = result.pageInfo->toVariantMap();
        m_lastPage = result.pageInfo->currentPage();
        const bool more = result.pageInfo->hasMore();
        delete result.pageInfo;

        if (m_hasMore != more) {
            m_hasMore = more;
            emit hasMoreChanged();
        }
        emit pageInfoChanged();
    } else {
        delete result.pageInfo;
    }

    QVariantList items;
    for (int i = 0; i < result.posts.size(); ++i) {
        QVariantMap row = cachedPostText(result.posts.at(i)->toVariantMap(), m_imageCache);
        const QString avatar = row.value(QLatin1String("authorAvatarUrl")).toString();
        row.insert(QLatin1String("remoteAvatarUrl"), avatar);
        row.insert(QLatin1String("localAvatarPath"), imagePath(avatar));
        items.append(row);
    }
    qDeleteAll(result.posts);

    // ★ 第 2 页起用 append（增量），只有第 1 页才整表替换
    if (m_pendingSubPostsAppend)
        m_subPosts->append(items);
    else
        fillModel(m_subPosts, items);

    m_pendingSubPostsAppend = false;
    emit subPostsChanged();

    finishRequest(QLatin1String(kWhatSubPosts), true);
}

/* ============================ 错误与调试 ============================ */

void TiebaController::onRequestFailed(int requestId, const QString &tag,
                                      const QString &message,
                                      const QVariantMap &detail)
{
    if (m_forumStatsRequests.contains(requestId)) {
        m_forumStatsRequests.remove(requestId);
        return;
    }
    if (tag.startsWith(QLatin1String("userpage."))) {
        if (!m_userPageRequests.contains(requestId)) return;
        QVariantMap &state = m_userPageStates[m_userPageRequests.take(requestId)];
        state.insert("loading", false);
        state.insert("error", message);
        state.insert("requestId", -1);
        ++m_userPageVersion;
        emit userPagesChanged();
        return;
    }
    if (tag.startsWith(QLatin1String("stream."))) {
        const QString kind = tag.mid(7);
        if (!m_streamStates.contains(kind) || m_streamStates.value(kind).requestId != requestId) return;
        StreamState &state = m_streamStates[kind];
        state.requestId = -1;
        state.loading = false;
        state.error = message;
        ++m_streamVersion;
        emit streamsChanged();
        return;
    }
    // 注意：PC 端请求用的 tag 带 ".pc" 后缀（forum.page.pc / thread.content.pc），
    // 这里必须两种都认，否则 PC 请求在网络层/解析层失败时会掉进最后的 else，
    // 被误判成楼中楼失败。
    if (tag == QLatin1String("account.login")) {
        if (requestId != m_loginRequestId)
            return;
        m_loginRequestId = -1;
        m_pendingLoginBduss.clear();
        m_pendingLoginStoken.clear();
        m_lastLoginError = message;
        emit lastLoginErrorChanged();
        setError(message, detail);
        finishRequest(QLatin1String("account.login"), false);
    } else if (tag == QLatin1String("forum.page") || tag == QLatin1String("forum.page.pc")) {
        if (requestId != m_forumPageRequestId)
            return;
        m_forumPageRequestId = -1;
        m_pendingForumPageAppend = false;
        setError(message, detail);
        finishRequest(QLatin1String(kWhatForumPage), false);
    } else if (tag == QLatin1String("thread.content")
               || tag == QLatin1String("thread.content.pc")) {
        if (requestId != m_threadRequestId)
            return;
        m_threadRequestId = -1;
        m_pendingThreadAppend = false;
        setError(message, detail);
        m_threadNavigation.insert("loading", false);
        m_threadNavigation.insert("error", message);
        m_jumpFloor = 0; m_jumpPostId.clear(); m_jumpSubPostId.clear();
        ++m_threadNavigationVersion; emit threadNavigationChanged();
        finishRequest(QLatin1String(kWhatThread), false);
    } else if (tag == QLatin1String("forum.list")
               || tag == QLatin1String("forum.list.like")) {
        if (requestId != m_forumListRequestId)
            return;
        m_forumListRequestId = -1;
        setError(message, detail);
        finishRequest(QLatin1String(kWhatForums), false);
    } else if (tag == QLatin1String("thread.subfloors")) {
        setError(message, detail);
        finishRequest(QLatin1String(kWhatSubPosts), false);
    } else {
        // 兜底：仍然把错误显示出来，但不猜是哪个请求失败了
        setError(message, detail);
    }
}

void TiebaController::clearError()
{
    setError(QString(), QVariantMap());
}

void TiebaController::setDebugEnabled(bool enabled)
{
    m_api->setDebugEnabled(enabled);
}

bool TiebaController::debugEnabled() const
{
    return m_api->isDebugEnabled();
}

QString TiebaController::debugLog() const
{
    return m_api->debugLog().join(QLatin1String("\n"));
}

void TiebaController::clearDebugLog()
{
    m_httpClient->clearDebugLog();
}

QString TiebaController::clientVersion() const
{
    return m_api->clientVersion();
}

void TiebaController::setClientVersion(const QString &version)
{
    m_api->setClientVersion(version);
}

/*!
 * 取图片的本地缓存路径（未下载完返回空串，并触发下载）。
 *
 * ★ 为什么不在 QML 里直接把远程 URL 给 ImageView：
 *   真机日志实测，BB10 的图片加载器**不支持任何网络方案**：
 *       "Unsupported scheme (http)  used in url (...). Image loading aborted."
 *       "Unsupported scheme (https) used in url (...). Image loading aborted."
 *   设备上也没有 Qt 的 imageformats 插件可以补。所以必须在 C++ 里下载成
 *   本地文件，再把路径交给 ImageView（官方 imageloader 示例就是这个做法）。
 */
QString TiebaController::imagePath(const QString &url)
{
    if (!m_imageCache)
        return QString();
    return m_imageCache->pathFor(url);
}

/*! 图片缓存诊断信息（排查"图片不显示"用，日志里直接能看到卡在哪一步） */
QString TiebaController::imageDiag() const
{
    if (!m_imageCache)
        return QLatin1String("imageCache: NULL");

    QStringList lines;
    lines << QString(QLatin1String("cacheDir : %1")).arg(m_imageCache->cacheDir());
    lines << QString(QLatin1String("cached   : %1")).arg(m_imageCache->cachedCount());
    lines << QString(QLatin1String("active   : %1 / %2"))
                 .arg(m_imageCache->activeCount())
                 .arg(m_imageCache->maxConcurrent());
    lines << QString(QLatin1String("queued   : %1")).arg(m_imageCache->queuedCount());
    lines << QString(QLatin1String("version  : %1")).arg(m_imageCacheVersion);
    lines << QString(QLatin1String("verbose  : %1"))
                 .arg(m_imageCache->isVerbose() ? QLatin1String("on")
                                                : QLatin1String("off"));
    return lines.join(QLatin1String("\n"));
}

/*!
 * 打开图片下载的详细日志。
 * 排查"图片不显示"时打开它，控制台会打出每一步（开始下载/完成/失败/超时）。
 */
void TiebaController::setImageDebug(bool on)
{
    if (m_imageCache)
        m_imageCache->setVerbose(on);
}

/*!
 * 把一个楼层的 map 展开成**一个或多个**列表项，并补上图片的本地路径。
 *
 * ★ 为什么要展开（多图显示的关键）：
 *   Cascades 的 ListItemComponent 里**没法遍历数组** —— 没有 Repeater，
 *   在 ListItemComponent 内再套 ListView 又很不可靠（试过，会崩/空白）。
 *   所以一个楼层有多张图时，不能在一个 item 里循环渲染。
 *
 *   解法：把"楼层"在 C++ 里拆成多个模型项 ——
 *       第 1 项：正文（含作者/楼层/时间头、楼中楼预览）
 *       第 2..N 项：每张图各占一项（只画图）
 *   这样一个 item 永远只有一张图，QML 侧不需要任何循环。
 *
 * 顺带在这一步触发图片下载（pathFor 会入队），所以调用它等于"开始下载本页图片"。
 */
void TiebaController::expandImageItems(const QVariantList &posts,
                                       QVariantList &out)
{
    if (!m_imageCache) {
        out = posts;
        return;
    }

    for (int i = 0; i < posts.size(); ++i) {
        QVariantMap map = cachedPostText(posts.at(i).toMap(), m_imageCache);
        const QString avatar = map.value(QLatin1String("authorAvatarUrl")).toString();
        map.insert(QLatin1String("remoteAvatarUrl"), avatar);
        map.insert(QLatin1String("localAvatarPath"), imagePath(avatar));

        const QString remote = map.value(QLatin1String("firstImageUrl")).toString();
        // 远程地址留一份：图片下完后要重算本地路径，得知道原始 URL
        map.insert(QLatin1String("remoteImageUrl"), remote);

        // 一项只有一个"格子"，用它区分是正文项还是图片项
        map.insert(QLatin1String("isImageItem"), false);
        // 只有正文项画作者/楼层/时间头
        map.insert(QLatin1String("postHead"), true);
        map.insert(QLatin1String("localImagePath"), QString());

        /*
         * 收集这个楼层所有的图片。
         *
         * contentSegments 里 kind=="image" 的段落按出现顺序排；
         * images 是 toVariantMap 已经整理好的 [{url,originUrl,width,height}]，
         * 两者顺序一致，所以直接遍历 images 即可。
         */
        QStringList imageUrls;
        const QVariantList images = map.value(QLatin1String("images")).toList();
        for (int k = 0; k < images.size(); ++k) {
            const QString u = images.at(k).toMap()
                              .value(QLatin1String("url")).toString();
            if (!u.isEmpty())
                imageUrls.append(u);
        }
        // 兜底：images 为空但有 firstImageUrl
        if (imageUrls.isEmpty() && !remote.isEmpty())
            imageUrls.append(remote);

        // Keep plainText for copying/searching, but omit the generated image
        // fallback when this floor already has real image rows. Literal text
        // such as "[图片]" written by the author must remain visible.
        QString displayText = map.value(QLatin1String("plainText")).toString();
        bool hasWrittenText = false;
        const QVariantList segments = map.value(QLatin1String("contentSegments")).toList();
        for (int k = 0; k < segments.size(); ++k) {
            const QVariantMap segment = segments.at(k).toMap();
            const QString kind = segment.value(QLatin1String("kind")).toString();
            if (kind == QLatin1String("emoticon")
                    || ((kind == QLatin1String("text") || kind == QLatin1String("link")
                         || kind == QLatin1String("user"))
                        && !segment.value(QLatin1String("text")).toString().trimmed().isEmpty()))
                hasWrittenText = true;
        }
        if (!imageUrls.isEmpty() && !hasWrittenText)
            displayText.remove(TbTr("5BE59BBEE789875D"));
        map.insert(QLatin1String("displayText"), displayText);

        map.insert(QLatin1String("rowType"), QLatin1String("post"));
        out.append(map);

        // 每张图各一项（放在正文后面，阅读顺序和原贴一致）
        for (int k = 0; k < imageUrls.size(); ++k) {
            QVariantMap imgItem;
            // 图片项也带上这些字段，方便 QML 统一取值
            imgItem.insert(QLatin1String("id"), map.value(QLatin1String("id")));
            imgItem.insert(QLatin1String("floor"), map.value(QLatin1String("floor")));
            imgItem.insert(QLatin1String("rowType"), QLatin1String("image"));
            imgItem.insert(QLatin1String("isImageItem"), true);
            imgItem.insert(QLatin1String("postHead"), false);
            imgItem.insert(QLatin1String("remoteImageUrl"), imageUrls.at(k));
            // ★ pathFor 会顺手把没缓存的图入队下载
            imgItem.insert(QLatin1String("localImagePath"),
                           m_imageCache->pathFor(imageUrls.at(k)));
            // 图片原始尺寸，QML 想按比例留白可以用
            if (k < images.size()) {
                const QVariantMap src = images.at(k).toMap();
                imgItem.insert(QLatin1String("imageWidth"),
                               src.value(QLatin1String("width")));
                imgItem.insert(QLatin1String("imageHeight"),
                               src.value(QLatin1String("height")));
            }
            out.append(imgItem);
        }

        // Replies belong after every image in this floor, never inside its
        // text/header row (which precedes the separate image rows).
        if (map.value(QLatin1String("subPostCount")).toInt() > 0
                || !map.value(QLatin1String("subPostPreview")).toList().isEmpty()) {
            QVariantMap footer = map;
            footer.insert(QLatin1String("rowType"), QLatin1String("replies"));
            footer.insert(QLatin1String("isReplyFooter"), true);
            footer.insert(QLatin1String("postHead"), false);
            footer.insert(QLatin1String("plainText"), QString());
            footer.insert(QLatin1String("contentSegments"), QVariantList());
            footer.insert(QLatin1String("displaySegments"), QVariantList());
            footer.insert(QLatin1String("remoteImageUrl"), QString());
            footer.insert(QLatin1String("localImagePath"), QString());
            out.append(footer);
        }

        /*
         * ★ floorEnd：标记"这一层的最后一项"。
         *
         * 为什么需要：列表项改用 Cascades 的 CustomListItem 之后，
         * 分隔线（dividerVisible）由每一项自己决定。一个楼层被拆成了
         * 「正文项 + 图片项 N 个」，如果每条都画分隔线，正文和它自己的图
         * 之间也会被切开 —— 看着像两个楼层。
         * 所以只有**这层的最后一项**画分隔线，视觉上才是"楼层之间一条线"。
         */
        const int lastIndex = out.size() - 1;
        if (lastIndex >= 0) {
            QVariantMap lastMap = out.at(lastIndex).toMap();
            lastMap.insert(QLatin1String("floorEnd"), true);
            out[lastIndex] = lastMap;
        }
    }
}

void TiebaController::onImageReady()
{
    /*
     * 有图片下载完成。
     *
     * ★★ 这里曾经导致闪退（堆损坏），原因和修法都记下来：
     *
     *   错的做法：把整个模型 clear() 再 append() 重建一遍。
     *   崩因：图片是**同时**下完好几张的，于是这个函数被连续调用多次，
     *         而每次都在同步地整表替换；此时 Cascades 的 ListView 正在
     *         遍历模型做布局，拿到的项被换掉 → use-after-free。
     *         真机日志最后是：
     *             free malloc object that is not allocated:
     *               /var/build/workspace/src/lib/c/alloc/dlist.c:1096
     *
     *   正确做法（现在这样）：
     *     1. 只 **replace()** 本地路径真的变了的那几项，不动其他项；
     *     2. 用定时器把刷新推迟一小段，避开"加载器正在遍历模型"这个时机，
     *        同时把连续完成的下载**合并成一次刷新**。
     *
     *   ★ 为什么用 80ms 的定时器而不是 singleShot(0)：
     *     一页 30 个楼层都有图时，图片会接连下完。如果每次完成都立刻
     *     全表扫一遍，就是 30 次 O(N) 遍历 + 30 次 QVariantMap 拷贝，
     *     这才是"多图时卡"的主因之一。攒到一个短窗口里刷新一次，
     *     体感顺很多，图片出现也几乎无延迟。
     */
    ++m_imageCacheVersion;
    emit imageCacheChanged();

    if (m_imageRefreshPending)
        return;                     // 已经排了，多次完成合并成一次
    m_imageRefreshPending = true;
    m_imageRefreshTimer->start();
}

void TiebaController::refreshImagePaths()
{
    m_imageRefreshPending = false;
    if (!m_imageCache)
        return;

    /*
     * 三类列表都要刷新：
     *   - 吧列表：localAvatarPath（吧头像）
     *   - 帖子列表 / 楼层：localImagePath（正文图片）
     * 用 localKey 指定这一类该填哪个字段。
     */
    struct Target { ArrayDataModel *model; const char *localKey; };
    const Target targets[9] = {
        { m_forums, "localAvatarPath" },
        { m_threads, "localImagePath" },
        { m_posts, "localImagePath" },
        { m_subPosts, "localImagePath" },
        { streamModel("follow"), "localImagePath" },
        { streamModel("recommend"), "localImagePath" },
        { streamModel("hot"), "localImagePath" },
        { streamModel("reply"), "localImagePath" },
        { streamModel("mention"), "localImagePath" }
    };

    for (int t = 0; t < 9; ++t) {
        ArrayDataModel *model = targets[t].model;
        if (!model)
            continue;
        const QString localKey = QLatin1String(targets[t].localKey);

        /*
         * 每次都重新读 size()，并且**在循环里逐次校验 i < size**。
         *
         * 因为这是延迟执行的：从排队到现在，可能刚好赶上一轮翻页 append，
         * 模型变长了。如果按排队时的 size 循环，replace() 会越界 —— 那是
         * 直接的内存破坏（和之前那次闪退是同一类问题，不能再犯）。
         */
        for (int i = 0; i < model->size(); ++i) {
            const QVariantMap map = model->value(i).toMap();
            QVariantMap updated = map.contains(QLatin1String("displaySegments"))
                ? cachedPostText(map, m_imageCache) : map;
            const QString remote = map.value(QLatin1String("remoteImageUrl")).toString();
            if (!remote.isEmpty()) {
                const QString fresh = m_imageCache->pathFor(remote);
                if (!fresh.isEmpty()) updated.insert(localKey, fresh);
            }
            const QString avatar = map.value(QLatin1String("remoteAvatarUrl")).toString();
            if (!avatar.isEmpty()) {
                const QString fresh = m_imageCache->pathFor(avatar);
                if (!fresh.isEmpty()) updated.insert(QLatin1String("localAvatarPath"), fresh);
            }
            if (updated == map) continue;
            // 再确认一次下标仍然有效，然后只替换这一项
            if (i < model->size())
                model->replace(i, updated);
        }
    }

    /*
     * 账号头像也可能刚下完。
     * ★ 只在真的有账号头像在等下载时才重建 —— 否则每次图片刷新都白扫一遍。
     */
    for (int i = 0; i < m_otherAccounts->size(); ++i) {
        const QVariantMap row = m_otherAccounts->value(i).toMap();
        if (! row.value(QLatin1String("avatarUrl")).toString().isEmpty()
                && row.value(QLatin1String("localAvatarPath")).toString().isEmpty()) {
            rebuildAccounts();
            break;
        }
    }

    // 缓存占用变了（设置页的"已用 X%"要跟着动）。这里已经是节流过的，
    // 不会每张图都发一次信号。
    emit cacheUsageChanged();
}

/*!
 * 打开图片查看器（把本地已缓存的原图交给系统看图应用）。
 *
 * @param remoteImageUrl 图片的远程地址。本地缓存路径由 imagePath() 内部解析。
 * @param target         看图应用的 invocation target
 *                       （默认 sys.pictures.card.previewer，见下方说明）。
 *
 * ── target 名字（已按官方文档修正）──────────────────────────────────────
 *
 * 正确 target 是 **"sys.pictures.card.previewer"**，
 * 来源：docs.waitberry.com 的 device_platform/invocation/pictures.html
 *       （"Pictures" 一节给了 Target ID / Action / MIME / URI 四项表格 + 调用示例）。
 *
 * 之前用的 "sys.photos" / "sys.pictures" 都是我**按命名习惯猜的** ——
 * target 名不对时 BB10 的 invoke() **不报错**、finished() 也不报错，
 * 只是系统毫无反应。这就是"意图框架点不动"的真正原因。
 *
 * 图片仅交给系统看图 card，调用失败时提示用户重试。
 *
 * ── 为什么用 file:// 而不是 https:// ──────────────────────────────────────
 *
 * 看图应用需要**本地文件**（它按文件路径读图）。远程地址交给它没有意义，
 * 而且本机已经为了显示把图下载缓存好了（TbImageCache）。
 */

/*!
 * 上一次放到共享目录的副本路径（空表示没有）。
 *
 * ★ 为什么需要清理：共享目录 /accounts/1000/shared 是**用户可见**的，
 *   放进去的文件会出现在相册里。如果每次点图都写一份新的，
 *   用户的相册很快就被 bbtieba-* 临时文件塞满。
 *   看图应用在 invoke 时已经把内容读进去了，删掉源文件不影响它显示。
 */
static QString g_lastSharedCopy;

/*!
 * 把图片复制到**共享目录**，返回 file:// 形式的路径；失败返回空串。
 *
 * ★★ 这是"看图意图点不动"的真正修复。
 *
 * 背景：图片缓存在应用私有目录（/accounts/1000/appdata/<appid>/tmp/...）。
 * 看图应用（sys.pictures.card.previewer）是**独立进程**，
 * 受 BB10 沙箱限制**读不到**别人的私有目录 —— 于是 Invocation 请求
 * 被静默丢弃：invoke() 返回成功、finished() 也不报错、系统毫无反应。
 *
 * 共享目录 /accounts/1000/shared 是所有应用都能读的
 * （官方示例见 bb/cascades/Image/image.h：
 *   Image image = Image(QUrl("file:///accounts/1000/shared/camera/camera0001.jpg")); ）
 *
 * 需要 bar-descriptor.xml 里声明 <permission>access_shared</permission>，
 * 否则这里创建目录会失败，并提示用户检查共享目录权限。
 */
static QString copyToSharedForViewer(const QString &localPath)
{
    /*
     * 先清掉上一次的副本，避免共享目录（用户相册里）越堆越多。
     * 见 g_lastSharedCopy 的说明。
     */
    if (! g_lastSharedCopy.isEmpty()) {
        if (QFile::exists(g_lastSharedCopy)) {
            if (QFile::remove(g_lastSharedCopy))
                qWarning("TiebaController: 已清理上一次的共享副本 %s",
                         qPrintable(g_lastSharedCopy));
            else
                qWarning("TiebaController: 清理共享副本失败 %s",
                         qPrintable(g_lastSharedCopy));
        }
        g_lastSharedCopy.clear();
    }

    if (localPath.trimmed().isEmpty())
        return QString();

    QString plain = localPath;
    if (plain.startsWith(QLatin1String("file://")))
        plain = plain.mid(7);

    QFile src(plain);
    if (! src.exists() || src.size() <= 0) {
        qWarning("TiebaController: 共享复制失败 —— 源文件不存在或为空: %s",
                 qPrintable(plain));
        return QString();
    }

    /*
     * 候选共享子目录，按"最像图片的"顺序试。
     * 用子目录而不是 shared 根目录：根目录写一堆图会污染用户的文件列表。
     */
    const QStringList dirs = QStringList()
        << QLatin1String("/accounts/1000/shared/photos")
        << QLatin1String("/accounts/1000/shared/camera")
        << QLatin1String("/accounts/1000/shared/downloads");

    for (int i = 0; i < dirs.size(); ++i) {
        const QString dir = dirs.at(i);
        if (! QDir().mkpath(dir)) {
            qWarning("TiebaController: 共享目录建不出来（缺 access_shared 权限？）: %s",
                     qPrintable(dir));
            continue;
        }

        // 目标文件名：保留原扩展名，加 bbtieba- 前缀避免和用户文件重名
        QFileInfo info(plain);
        QString ext = info.suffix();
        if (ext.isEmpty())
            ext = QLatin1String("jpg");
        const QString dst = dir + QLatin1String("/bbtieba-")
                            + info.completeBaseName() + QLatin1Char('.') + ext;

        QFile::remove(dst);   // 覆盖旧副本
        if (QFile::copy(plain, dst)) {
            qWarning("TiebaController: 已复制到共享目录 -> %s", qPrintable(dst));
            g_lastSharedCopy = dst;
            return QLatin1String("file://") + dst;
        }
        qWarning("TiebaController: 复制到 %s 失败", qPrintable(dst));
    }

    return QString();
}

bool TiebaController::openImage(const QString &remoteImageUrl,
                                const QString &target)
{
    if (remoteImageUrl.trimmed().isEmpty())
        return false;

    // 系统看图 card 只能读取共享目录中的本地文件。
    const QString localImagePath = imagePath(remoteImageUrl);
    if (localImagePath.trimmed().isEmpty()) {
        showToast(TbTr("E59BBEE78987E6ADA3E59CA8E4B88BE8BDBDEFBC8CE8AFB7E7A88DE5908EE5868DE8AF95"));
        return false;
    }
    const QString sharedPath = copyToSharedForViewer(localImagePath);
    if (sharedPath.isEmpty()) {
        showToast(TbTr("E697A0E6B395E58786E5A487E59BBEE78987EFBC8CE8AFB7E6A380E69FA5E585B1E4BAABE79BAEE5BD95E69D83E99990"));
        return false;
    }
    if (!m_invokeManager) {
        showToast(TbTr("E697A0E6B395E68993E5BC80E7B3BBE7BB9FE59BBEE78987E69FA5E79C8BE599A8"));
        return false;
    }

    bb::system::InvokeRequest request;
    request.setTarget(target.trimmed().isEmpty()
                      ? QLatin1String("sys.pictures.card.previewer")
                      : target.trimmed());
    request.setAction(QLatin1String("bb.action.VIEW"));
    // 保留文件扩展名，由平台推断 MIME 类型。
    request.setUri(sharedPath);
    bb::system::InvokeTargetReply *reply = m_invokeManager->invoke(request);
    if (!reply) {
        showToast(TbTr("E697A0E6B395E68993E5BC80E7B3BBE7BB9FE59BBEE78987E69FA5E79C8BE599A8"));
        return false;
    }
    connect(reply, SIGNAL(finished()), this, SLOT(onImageInvokeFinished()));
    // 等待异步回复，card 冷启动期间不再用定时器打开浏览器。
    return true;
}

void TiebaController::onImageInvokeFinished()
{
    QObject *from = sender();
    if (!from)
        return;
    // 此槽只连接 InvokeTargetReply；桌面测试桩没有 Q_OBJECT。
    bb::system::InvokeTargetReply *reply =
        static_cast<bb::system::InvokeTargetReply *>(from);
    const bb::system::InvokeReplyError::Type err = reply->error();
    const int code = reply->errorCode();
    reply->deleteLater();
    if (err != bb::system::InvokeReplyError::None) {
        qWarning("TiebaController: openImage 失败 error=%d errorCode=%d", (int)err, code);
        showToast(TbTr("E7B3BBE7BB9FE59BBEE78987E69FA5E79C8BE599A8E68993E5BC80E5A4B1E8B4A5EFBC8CE8AFB7E9878DE8AF95"));
    }
}

void TiebaController::showToast(const QString &body, int position)
{
    if (body.isEmpty())
        return;

    /*
     * ★ 用常驻的 m_toast，**不能**在这里定义局部 SystemToast ——
     *   局部对象在函数返回时就析构，提示根本来不及显示（这是"Toast 不显示"的原因）。
     */
    if (! m_toast)
        return;

    m_toast->setBody(body);

    /*
     * 位置：默认底部（BottomCenter）—— 不挡正文。
     * 注意 setPosition 收的是 bb::system::SystemUiPosition::Type。
     */
    switch (position) {
    case 0:  m_toast->setPosition(bb::system::SystemUiPosition::MiddleCenter); break;
    case 1:  m_toast->setPosition(bb::system::SystemUiPosition::TopCenter);    break;
    default: m_toast->setPosition(bb::system::SystemUiPosition::BottomCenter); break;
    }

    m_toast->show();
    qWarning("TiebaController: showToast -> %s", qPrintable(body));
}

bool TiebaController::copyText(const QString &text)
{
    if (text.isEmpty())
        return false;

    /*
     * bb::system::Clipboard 的 insert(type, data) 收的是 QByteArray，
     * 所以文本要**自己转成 UTF-8** —— 传 QString 会编译不过，
     * 而且中文必须走 UTF-8，否则贴到别处是乱码。
     */
    bb::system::Clipboard clipboard;
    const bool ok = clipboard.insert(QLatin1String("text/plain"),
                                     text.toUtf8());
    qWarning("TiebaController: copyText %s (%d 字)",
             ok ? "成功" : "失败", text.length());

    /*
     * 复制成功顺手弹个提示。
     * 没有它的话"复制链接"按下去毫无反馈，用户会以为坏了
     * （这正是上一轮"复制链接不通过"的原因之一）。
     */
    if (ok) {
        showToast(text.length() > 30
                  ? TbTr("E5B7B2E5A48DE588B6")
                  : TbTr("E5B7B2E5A48DE588B63A20") + text);
    }

    return ok;
}

QString TiebaController::contextTitle(const QVariantMap &row) const
{
    QString title = row.value("title").toString();
    if (title.isEmpty()) title = row.value("name").toString();
    if (title.isEmpty() && row.value("id") == m_threadInfo.value("id")) title = m_threadInfo.value("title").toString();
    return title;
}

void TiebaController::onLibraryStorageError(const QString &message)
{
    showToast(message);
}

QString TiebaController::contextSubtitle(const QVariantMap &row) const
{
    if (!row.value("name").toString().isEmpty() && row.value("title").toString().isEmpty()) {
        const QString name = row.value("name").toString();
        QString count = row.value("memberCountText").toString();
        if (count.isEmpty()) count = m_forumMemberCounts.value(name);
        return QString::fromUtf8("关注人数 ") + (count.isEmpty() ? QString::fromUtf8("—") : count);
    }
    QString author = row.value("authorName").toString();
    if (author.isEmpty() && row.value("id") == m_threadInfo.value("id")) author = m_threadInfo.value("authorName").toString();
    return author;
}

void TiebaController::requestForumStats(const QVariantMap &row)
{
    const QString name = row.value("name").toString().trimmed();
    if (name.isEmpty() || !row.value("memberCountText").toString().isEmpty() ||
        !m_forumMemberCounts.value(name).isEmpty() || m_forumStatsRequests.values().contains(name)) return;
    const int requestId = m_api->fetchForumPagePc(name, 1, 0, false);
    m_forumStatsRequests.insert(requestId, name);
}

bool TiebaController::favoriteThread(const QString &threadId)
{
    QVariantMap info;
    if (m_threadInfo.value("id").toString() == threadId) info = m_threadInfo;
    for (int i = 0; info.isEmpty() && i < m_threads->size(); ++i) {
        const QVariantMap row = m_threads->value(i).toMap();
        if (row.value("id").toString() == threadId) info = row;
    }
    if (info.isEmpty()) return false;
    m_favoriteService->toggle(info);
    return true;
}

void TiebaController::onFavoriteOperation(const QString &threadId, bool stored, bool success, const QString &message)
{
    Q_UNUSED(threadId)
    Q_UNUSED(stored)
    Q_UNUSED(success)
    showToast(message);
}

/*!
 * 用系统浏览器打开 URL。
 *
 * 走 BB10 的 Invocation 框架（InvokeManager + InvokeRequest）。
 * 用于正文里的外链，以及帖子页的"用浏览器打开"操作。
 * 失败（没有可处理的 target）时返回 false，界面可以据此提示。
 */
bool TiebaController::openUrl(const QString &url)
{
    if (url.trimmed().isEmpty())
        return false;

    /* 同样用常驻的 m_invokeManager（见 openImage 的说明） */
    if (! m_invokeManager) {
        qWarning("TiebaController: openUrl 没有 InvokeManager");
        return false;
    }
    bb::system::InvokeManager &manager = *m_invokeManager;
    bb::system::InvokeRequest request;
    request.setTarget(QLatin1String("sys.browser"));
    request.setAction(QLatin1String("bb.action.OPEN"));
    request.setUri(url);

    // 注意：invoke() 返回的是 InvokeTargetReply*（不是 InvokeReplyError）。
    // 返回空指针表示连目标都找不到（例如设备上没有浏览器 card）。
    // 真正的执行结果要通过 reply 的 finished() 异步拿，这里只判断有没有派发出去。
    bb::system::InvokeTargetReply *reply = manager.invoke(request);
    if (!reply) {
        qWarning("TiebaController: 无法打开 URL（找不到可处理的目标）: %s", qPrintable(url));
        return false;
    }
    reply->deleteLater();
    return true;
}
