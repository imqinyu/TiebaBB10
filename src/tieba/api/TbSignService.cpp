#include "TbSignService.hpp"
#include "TbApi.hpp"
#include "session/TbSession.hpp"
#include "net/TbSigner.hpp"
#include "util/TbMd5.hpp"
#include <QDateTime>
#include <QMap>

namespace tieba {
namespace {
QString text(const char *utf8) { return QString::fromUtf8(utf8); }
QString errorText(const TbJson &json) {
    return TbJson::str2(json, QLatin1String("error_msg"), QLatin1String("error")).left(140);
}
bool okJson(const TbJson &json) {
    return json.isObject() && json.contains(QLatin1String("error_code"))
            && TbJson::integer(json, QLatin1String("error_code"), -1) == 0;
}
}

TbSignService::TbSignService(TbHttpClient *client, TbSession *session, QObject *parent)
    : QObject(parent), m_client(client), m_session(session), m_loading(false),
      m_version(0), m_intervalSeconds(4), m_requestId(-1), m_stage(0), m_page(1), m_currentIndex(-1), m_failureStage(0)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, SIGNAL(timeout()), this, SLOT(sendNext()));
    if (m_client) {
        const bool connected = connect(m_client, SIGNAL(finished(int,QString,tieba::TbJson,tieba::TbHttpResponse)),
                         this, SLOT(onFinished(int,QString,tieba::TbJson,tieba::TbHttpResponse)));
        qWarning("TbSignService: httpFinishedConnected=%d", connected);
    }
    if (m_session) connect(m_session, SIGNAL(changed()), this, SLOT(onSessionChanged()));
}

void TbSignService::notify() { ++m_version; emit changed(); }

QVariantMap TbSignService::state() const
{
    int signedCount = 0, completed = 0, failed = 0;
    for (int i = 0; i < m_rows.size(); ++i) {
        const QString status = m_rows.at(i).toMap().value("status").toString();
        if (status == "success" || status == "already") { ++signedCount; ++completed; }
        if (status == "failed") { ++failed; ++completed; }
    }
    QVariantMap out;
    out.insert("total", m_rows.size()); out.insert("signedCount", signedCount);
    out.insert("completed", completed); out.insert("failedCount", failed);
    out.insert("progress", m_rows.isEmpty() ? 0 : completed * 100 / m_rows.size());
    out.insert("loading", m_loading); out.insert("currentName", m_currentName);
    out.insert("error", m_error); out.insert("logs", m_logs);
    out.insert("remainingSeconds", qMax(0, m_rows.size() - completed - 1) * m_intervalSeconds);
    out.insert("canRetry", failed > 0 || (!m_error.isEmpty() && !m_rows.isEmpty()));
    return out;
}

void TbSignService::setIntervalSeconds(int seconds)
{
    m_intervalSeconds = qBound(4, seconds, 60);
    notify();
}

bool TbSignService::sameAccount() const
{
    return m_session && m_session->isLoggedIn() && m_session->currentAccountId() == m_accountId
            && m_session->bduss() == m_bduss;
}

void TbSignService::prepareRun()
{
    m_accountId = m_session->currentAccountId(); m_uid = m_session->uid();
    m_bduss = m_session->bduss(); m_tbs.clear(); m_error.clear(); m_currentName.clear();
    m_loading = true; m_requestId = -1; m_currentIndex = -1; m_timer.stop();
    m_failureStage = 0;
}

void TbSignService::start()
{
    if (m_loading) return;
    if (!m_session || !m_session->isLoggedIn()) { fail(text("请先登录账号")); return; }
    if (!m_client) { fail(text("网络服务不可用")); return; }
    prepareRun(); m_rows.clear(); m_queue.clear(); m_seen.clear(); m_logs.clear(); m_page = 1;
    fetchForums();
}

void TbSignService::cancel()
{
    m_timer.stop(); m_requestId = -1; m_stage = 0; m_loading = false;
    // Do not abort the shared HTTP client: other pages may have requests in flight.
    for (int i = 0; i < m_rows.size(); ++i) {
        const QString status = m_rows.at(i).toMap().value("status").toString();
        if (status == "pending" || status == "signing") updateRow(i, "cancelled", text("已取消"));
    }
    m_queue.clear(); m_currentName.clear(); notify();
}

void TbSignService::onSessionChanged()
{
    if (m_bduss.isEmpty() || sameAccount()) return;
    cancel(); m_rows.clear(); m_logs.clear(); m_bduss.clear();
    m_error = text("账号已变化，请重新开始签到"); notify();
}

void TbSignService::retryFailed()
{
    if (m_loading) return;
    if (!sameAccount() || !m_client) { start(); return; }
    if (m_failureStage == 1) { start(); return; }
    m_queue.clear();
    for (int i = 0; i < m_rows.size(); ++i) {
        const QString status = m_rows.at(i).toMap().value("status").toString();
        if (status == "failed" || status == "cancelled" || status == "pending") {
            updateRow(i, "pending", text("等待签到")); m_queue.append(i);
        }
    }
    if (m_queue.isEmpty()) return;
    prepareRun(); fetchTbs();
}

void TbSignService::fail(const QString &message)
{
    m_failureStage = m_stage;
    m_timer.stop(); m_requestId = -1; m_stage = 0; m_loading = false;
    m_error = message; m_currentName.clear(); notify();
}

void TbSignService::fetchForums()
{
    m_stage = 1;
    QList<KeyValue> params;
    params.append(qMakePair(QString("page_no"), QString::number(m_page)));
    params.append(qMakePair(QString("page_size"), QString("200")));
    const QList<KeyValue> all = TbSigner::withPcCommonParams(params, m_session);
    TbHttpRequest request;
    request.url = QLatin1String("https://c.tieba.baidu.com/c/f/forum/like");
    request.tag = "sign.forums"; request.params = all; request.body = TbSigner::buildPcForm(all);
    request.headers.append(qMakePair(QString("User-Agent"), TbSigner::pcUserAgent()));
    request.headers.append(qMakePair(QString("Cookie"), QLatin1String("ka=open; BDUSS=") + m_bduss));
    request.headers.append(qMakePair(QString("Referer"), QString("https://tieba.baidu.com/")));
    m_requestId = m_client->send(request); notify();
}

void TbSignService::fetchTbs()
{
    m_stage = 2;
    // TiebaLite uses loginBean.anti.tbs. Web cookies may not establish web login
    // even when the same BDUSS passes the client login endpoint.
    const TbHttpRequest request = tbsRequest(m_bduss, m_session ? m_session->stoken() : QString());
    qWarning("TbSignService: sign.tbs source=client-login scheme=https");
    m_requestId = m_client->send(request); notify();
}

TbHttpRequest TbSignService::tbsRequest(const QString &bduss, const QString &stoken)
{
    TbHttpRequest request = TbApiService::buildLoginRequest(bduss, stoken);
    // Use an independent tag so this read-only ticket refresh cannot route to
    // the controller's interactive login handler or replace saved accounts.
    request.tag = QLatin1String("sign.tbs");
    return request;
}

QVariantList TbSignService::forumRows(const TbJson &json)
{
    QVariantList rows;
    const TbJson list = TbJson::object(json, "forum_list");
    const char *buckets[] = { "non-gconforum", "gconforum" };
    for (int b = 0; b < 2; ++b) {
        const TbJson array = TbJson::array(list, QLatin1String(buckets[b]));
        for (int i = 0; i < array.size(); ++i) {
            const TbJson forum = array.at(i);
            const QString id = TbJson::str2(forum, "id", "forum_id");
            const QString name = TbJson::str2(forum, "name", "forum_name");
            if (id.isEmpty() || name.isEmpty()) continue;
            QVariantMap row; row.insert("id", id); row.insert("name", name);
            const bool signedToday = TbJson::boolean(forum, "is_sign", false);
            row.insert("status", signedToday ? "already" : "pending");
            row.insert("message", signedToday ? text("已签到") : text("等待签到"));
            rows.append(row);
        }
    }
    return rows;
}

TbHttpRequest TbSignService::signRequest(const QString &bduss, const QString &tbs,
        const QString &fid, const QString &name, qint64 timestampSeconds)
{
    // User's 9.7.8.0 protocol: sorted raw k=v without separators, uppercase MD5.
    QMap<QString, QString> fields;
    fields.insert("BDUSS", bduss); fields.insert("_client_type", "2");
    fields.insert("_client_version", "9.7.8.0"); fields.insert("_phone_imei", "000000000000000");
    fields.insert("fid", fid); fields.insert("kw", name); fields.insert("model", "MI+5");
    fields.insert("net_type", "1"); fields.insert("tbs", tbs);
    fields.insert("timestamp", QString::number(timestampSeconds));
    QString raw;
    QMap<QString, QString>::const_iterator it;
    QList<KeyValue> params;
    for (it = fields.constBegin(); it != fields.constEnd(); ++it) {
        raw += it.key() + QLatin1Char('=') + it.value();
        params.append(qMakePair(it.key(), it.value()));
    }
    params.append(qMakePair(QString("sign"), TbMd5::hex(raw.toUtf8() + QByteArray("tiebaclient!!!")).toUpper()));
    QStringList encoded;
    for (int i = 0; i < params.size(); ++i)
        encoded.append(TbString::percentEncode(params.at(i).first) + QLatin1Char('=')
                       + TbString::percentEncode(params.at(i).second));
    TbHttpRequest request; request.url = "https://c.tieba.baidu.com/c/c/forum/sign";
    request.tag = "sign.submit"; request.params = params; request.body = encoded.join("&").toUtf8();
    request.headers.append(qMakePair(QString("User-Agent"), TbSigner::userAgent("9.7.8.0")));
    request.headers.append(qMakePair(QString("Cookie"), QLatin1String("BDUSS=") + bduss));
    return request;
}

QString TbSignService::responseStatus(const TbJson &json)
{
    if (!json.isObject() || !json.contains("error_code")) return "failed";
    const qint64 code = TbJson::integer(json, "error_code", -1);
    if (code == 0) return "success";
    const QString message = errorText(json);
    if (code == 160002 || message.contains(text("已经签到")) || message == text("已签到"))
        return "already";
    return "failed";
}

void TbSignService::updateRow(int index, const QString &status, const QString &message)
{
    QVariantMap row = m_rows.at(index).toMap();
    row.insert("status", status); row.insert("message", message); m_rows[index] = row;
}

void TbSignService::sendNext()
{
    if (!m_loading || !m_client || !sameAccount()) { cancel(); return; }
    if (m_queue.isEmpty()) {
        m_loading = false; m_stage = 0; m_currentName.clear(); notify(); return;
    }
    m_currentIndex = m_queue.takeFirst();
    const QVariantMap row = m_rows.at(m_currentIndex).toMap();
    m_currentName = row.value("name").toString(); updateRow(m_currentIndex, "signing", text("正在签到"));
    m_stage = 3;
    m_requestId = m_client->send(signRequest(m_bduss, m_tbs, row.value("id").toString(),
                                          m_currentName, QDateTime::currentDateTime().toTime_t()));
    notify();
}

void TbSignService::onFinished(int requestId, const QString &tag, const TbJson &json,
                              const TbHttpResponse &response)
{
    if (!m_loading || requestId != m_requestId || !sameAccount()) return;
    m_requestId = -1;
    qWarning("TbSignService: %s request=%d http=%d networkError=%d jsonObject=%d code=%d",
             qPrintable(tag), requestId, response.httpStatus, response.networkError,
             json.isObject(), TbJson::intValue(json, "error_code", -1));
    if (m_stage == 1 && tag == "sign.forums") {
        if (!response.ok || !okJson(json) || !TbJson::object(json, "forum_list").isObject()) {
            fail(text("关注吧列表加载失败，请重新开始")); return;
        }
        const QVariantList rows = forumRows(json);
        int added = 0;
        for (int i = 0; i < rows.size(); ++i) {
            const QString id = rows.at(i).toMap().value("id").toString();
            if (m_seen.contains(id)) continue;
            m_seen.insert(id); m_rows.append(rows.at(i)); ++added;
        }
        if (TbJson::boolean(json, "has_more", false)) {
            if (!added || m_page >= 100) { fail(text("关注吧分页无进展，请重试")); return; }
            ++m_page; fetchForums(); return;
        }
        for (int i = 0; i < m_rows.size(); ++i)
            if (m_rows.at(i).toMap().value("status") == "pending") m_queue.append(i);
        if (m_queue.isEmpty()) { m_loading = false; m_stage = 0; notify(); return; }
        fetchTbs(); return;
    }
    if (m_stage == 2 && tag == "sign.tbs") {
        const QString uid = TbJson::str(TbJson::object(json, "user"), "id");
        const bool uidPresent = !uid.isEmpty() && uid != QLatin1String("0");
        const QString ticket = TbJson::str(TbJson::object(json, "anti"), "tbs").trimmed();
        const QString expectedUid = m_session ? m_session->uid() : m_uid;
        const bool uidMatches = expectedUid.isEmpty() || uid == expectedUid;
        qWarning("TbSignService: sign.tbs uidPresent=%d tbsPresent=%d uidMatches=%d",
                 uidPresent, !ticket.isEmpty(), uidMatches);
        if (!response.ok || response.httpStatus >= 400) {
            fail(text("签到票据请求失败（HTTP %1，网络错误 %2），请检查网络后重试")
                 .arg(response.httpStatus).arg(response.networkError)); return;
        }
        if (!json.isObject() || !json.contains("error_code")) {
            fail(text("签到验证接口返回内容无法解析，请重试")); return;
        }
        if (!okJson(json) || !uidPresent) {
            fail(text("登录凭据未通过签到验证（代码 %1），请重新登录")
                 .arg(TbJson::integer(json, "error_code", -1))); return;
        }
        if (!uidMatches) { fail(text("签到验证返回账号不一致，请切换账号后重试")); return; }
        if (ticket.isEmpty()) { fail(text("登录验证成功，但没有返回签到票据，请重试")); return; }
        m_tbs = ticket;
        sendNext(); return;
    }
    if (m_stage == 3 && tag == "sign.submit") {
        const QString status = response.ok ? responseStatus(json) : QString("failed");
        QString message = status == "success" ? text("签到成功")
                : status == "already" ? text("已签到") : errorText(json);
        if (message.isEmpty()) message = text("签到失败，请稍后重试");
        updateRow(m_currentIndex, status, message);
        m_logs.append(m_currentName + QLatin1String(": ") + message);
        if (m_queue.isEmpty()) { m_loading = false; m_stage = 0; m_currentName.clear(); }
        else m_timer.start(m_intervalSeconds * 1000);
        notify();
    }
}
}
