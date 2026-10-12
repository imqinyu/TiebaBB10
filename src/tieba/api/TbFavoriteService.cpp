#include "TbFavoriteService.hpp"
#include "session/TbSession.hpp"
#include "net/TbSigner.hpp"
#include "util/TbMd5.hpp"
#include "model/TbItems.hpp"
#include "model/TbUserDataCache.hpp"
#include <QSet>
namespace tieba {
namespace {
QString accountKey(const TbSession *session) {
    if (!session) return QString();
    return session->currentAccountId() + ":" + TbMd5::hex((session->bduss() + ":" + session->stoken()).toUtf8());
}
}
TbFavoriteService::TbFavoriteService(TbHttpClient *client, TbSession *session, QObject *parent)
    : QObject(parent), m_client(client), m_session(session), m_cache(new TbUserDataCache(this)), m_loading(false), m_loaded(false),
      m_hasMore(false), m_complete(false), m_writing(false), m_mobile(false), m_cacheHydrated(false), m_cacheShown(false),
      m_backgroundStarted(false), m_version(0), m_offset(0), m_failedOffset(0), m_scanOffset(0)
{
    if (client) connect(client, SIGNAL(finished(int,QString,tieba::TbJson,tieba::TbHttpResponse)),
                        this, SLOT(onFinished(int,QString,tieba::TbJson,tieba::TbHttpResponse)));
    m_accountKey = accountKey(session);
    if (session) connect(session, SIGNAL(accountsChanged()), this, SLOT(onSessionChanged()));
}
void TbFavoriteService::setCacheDirectoryForTest(const QString &directory) { m_cache->setDirectory(directory); }
void TbFavoriteService::restoreCache() {
    if (m_cacheHydrated || !signedIn()) return;
    m_cacheHydrated = true;
    const QVariantMap entry = m_cache->read(m_session->currentAccountId(), "favorites");
    if (!entry.value("hit").toBool()) return;
    const QVariantMap cached = entry.value("payload").toMap();
    m_rows = cached.value("items").toList(); m_loaded = true; m_cacheShown = true;
    m_offset = cached.value("offset").toInt(); m_hasMore = cached.value("hasMore").toBool(); m_complete = cached.value("complete").toBool();
    for (int i = 0; i < m_rows.size(); ++i) m_known.insert(m_rows.at(i).toMap().value("id").toString(), true);
    notify();
}
void TbFavoriteService::saveCache() {
    if (!signedIn() || !m_loaded) return;
    QVariantMap payload; payload.insert("items", m_rows); payload.insert("offset", m_offset);
    payload.insert("hasMore", m_hasMore); payload.insert("complete", m_complete);
    m_cache->save(m_session->currentAccountId(), "favorites", payload);
}
void TbFavoriteService::onSessionChanged() {
    const QString key = accountKey(m_session);
    if (key == m_accountKey) return;
    m_accountKey = key; clear();
}
bool TbFavoriteService::signedIn() const { return m_client && m_session && m_session->isLoggedIn(); }
bool TbFavoriteService::validId(const QString &id) {
    if (id.isEmpty() || id == "0") return false;
    for (int i = 0; i < id.size(); ++i) if (id.at(i) < QLatin1Char('0') || id.at(i) > QLatin1Char('9')) return false;
    return true;
}
void TbFavoriteService::notify() { ++m_version; emit changed(); }
QVariantMap TbFavoriteService::state() const {
    QVariantMap out;
    out.insert("items", m_rows); out.insert("loading", m_loading); out.insert("loaded", m_loaded);
    out.insert("hasMore", m_hasMore); out.insert("offset", m_offset); out.insert("error", m_error);
    out.insert("mutationError", m_mutationError); out.insert("busy", busy());
    out.insert("cached", m_cacheShown); out.insert("refreshing", m_loading && !m_rows.isEmpty());
    return out;
}
int TbFavoriteService::status(const QString &threadId) const {
    if (m_known.contains(threadId)) return m_known.value(threadId) ? 1 : 0;
    return m_complete ? 0 : -1;
}
void TbFavoriteService::clear() {
    m_requests.clear(); m_rows.clear(); m_known.clear(); m_toggleRow.clear(); m_statusTarget.clear();
    m_error.clear(); m_mutationError.clear(); m_offset = m_failedOffset = m_scanOffset = 0;
    m_loading = m_loaded = m_hasMore = m_complete = m_writing = m_mobile = false;
    m_cacheHydrated = m_cacheShown = m_backgroundStarted = false;
    notify();
}
TbHttpRequest TbFavoriteService::request(const TbSession *session, const QString &path,
            const QList<KeyValue> &params, const QString &tag, bool mobile)
{
    TbHttpRequest out;
    out.url = TbSigner::apiBase() + path.mid(1); out.tag = tag;
    QList<KeyValue> fields = params;
    if (mobile) {
        fields.append(qMakePair(QString("_phone_imei"), QString("000000000000000")));
        fields.append(qMakePair(QString("_os_version"), QString("23")));
        fields.append(qMakePair(QString("cuid"), TbMd5::hex((session ? session->currentAccountId() : QString("BlackBerry")).toUtf8()) + "|0"));
        fields.append(qMakePair(QString("from"), QString("tieba")));
        fields.append(qMakePair(QString("stErrorNums"), QString("0")));
    }
    out.params = mobile ? TbSigner::withCommonParams(fields, session, "11.10.8.6")
                        : TbSigner::withPcCommonParams(fields, session);
    if (mobile) {
        // TiebaLite FormBody.sortedRaw(false): sorted raw k=v pairs without '&'.
        QMap<QString, QString> sorted;
        for (int i = 0; i < out.params.size(); ++i) sorted.insert(out.params.at(i).first, out.params.at(i).second);
        QString raw;
        QMap<QString, QString>::const_iterator it = sorted.constBegin();
        for (; it != sorted.constEnd(); ++it) if (it.key() != "sign") raw += it.key() + "=" + it.value();
        QList<KeyValue> encoded = out.params;
        encoded.append(qMakePair(QString("sign"), TbMd5::hex((raw + "tiebaclient!!!").toUtf8()).toUpper()));
        out.body = TbString::buildFormBody(encoded);
    } else out.body = TbSigner::buildPcForm(out.params);
    out.headers.append(qMakePair(QString("User-Agent"), mobile ? TbSigner::userAgent("11.10.8.6") : TbSigner::pcUserAgent()));
    QString cookie("ka=open");
    if (!mobile && session && !session->bduss().isEmpty()) cookie += "; BDUSS=" + session->bduss();
    out.headers.append(qMakePair(QString("Cookie"), cookie));
    if (session && !session->uid().isEmpty()) out.headers.append(qMakePair(QString("client_user_token"), session->uid()));
    return out;
}
TbHttpRequest TbFavoriteService::listRequest(const TbSession *session, int offset, bool mobile) {
    QList<KeyValue> params;
    params.append(qMakePair(QString("rn"), QString("20")));
    params.append(qMakePair(QString("offset"), QString::number(qMax(0, offset))));
    if (session && !session->uid().isEmpty()) params.append(qMakePair(QString("user_id"), session->uid()));
    if (session && !session->stoken().isEmpty()) params.append(qMakePair(QString("stoken"), session->stoken()));
    return request(session, "/c/f/post/threadstore", params, "favorites.list", mobile);
}
TbHttpRequest TbFavoriteService::mutationRequest(const TbSession *session, const QString &threadId,
            const QString &postId, bool stored, const QString &tbs, bool mobile)
{
    QList<KeyValue> params;
    if (!validId(threadId)) return TbHttpRequest();
    if (stored) {
        const QString pid = validId(postId) ? postId : QString("0");
        // NewCollectDataBean fields are tid,pid,status (the legacy helper swaps arguments).
        params.append(qMakePair(QString("data"), QString("[{\"tid\":\"") + threadId + "\",\"pid\":\"" + pid + "\",\"status\":1}]"));
    } else {
        params.append(qMakePair(QString("tid"), threadId));
        params.append(qMakePair(QString("fid"), QString("null")));
    }
    params.append(qMakePair(QString("tbs"), tbs));
    if (session && !session->uid().isEmpty()) params.append(qMakePair(QString("user_id"), session->uid()));
    if (session && !session->stoken().isEmpty()) params.append(qMakePair(QString("stoken"), session->stoken()));
    return request(session, stored ? "/c/c/post/addstore" : "/c/c/post/rmstore", params,
                   stored ? "favorites.add" : "favorites.remove", mobile);
}
QVariantMap TbFavoriteService::parseList(const TbJson &json) {
    QVariantMap result;
    QString code = TbJson::str(json, "error_code");
    const TbJson nested = json.member("error");
    if (nested.isObject() && nested.contains("errno") && TbJson::str(nested, "errno") != "0") code = TbJson::str(nested, "errno");
    if (code != "0") {
        const QString message = nested.isObject() ? TbJson::str(nested, "errmsg") : TbJson::str(json, "error_msg");
        result.insert("error", code.isEmpty() ? QString::fromUtf8("收藏响应缺少状态码")
           : code == "110001" ? QString::fromUtf8("云端收藏接口拒绝此客户端（110001），请刷新重试")
           : (message.isEmpty() ? QString::fromUtf8("收藏请求失败") : message) + " (" + code + ")");
        return result;
    }
    const TbJson list = json.member("store_thread");
    // Tieba uses numeric 0 for a valid empty bookmark list on some clients.
    const bool empty = json.contains("store_thread") && !list.isObject() && !list.isArray()
            && (list.isNull() || list.toString().isEmpty() || list.toString() == "0");
    if (!list.isArray() && !empty) { result.insert("error", QString::fromUtf8("收藏响应缺少有效列表")); return result; }
    QVariantList rows;
    QSet<QString> seen;
    for (int i = 0; i < list.size(); ++i) {
        const TbJson node = list.at(i), author = node.member("author");
        const QString tid = TbJson::str(node, "thread_id");
        if (!validId(tid) || seen.contains(tid)) continue;
        seen.insert(tid);
        QVariantMap row;
        row.insert("id", tid); row.insert("key", tid); row.insert("threadId", tid); row.insert("resultKind", "thread");
        row.insert("postId", TbJson::str(node, "mark_pid"));
        row.insert("title", TbString::stripHtml(TbJson::str(node, "title")));
        row.insert("forumName", TbJson::str(node, "forum_name"));
        row.insert("authorId", TbJson::str(author, "lz_uid")); row.insert("userName", TbJson::str(author, "name"));
        const QString nick = TbJson::str(author, "name_show");
        row.insert("authorName", nick.isEmpty() ? TbJson::str(author, "name") : nick);
        row.insert("authorLevel", 0); row.insert("avatarUrl", TbString::avatarUrl(TbJson::str(author, "user_portrait")));
        row.insert("authorAvatarUrl", row.value("avatarUrl")); row.insert("remoteAvatarUrl", row.value("avatarUrl"));
        row.insert("localAvatarPath", QString()); row.insert("abstractText", TbJson::str(node, "post_no_msg"));
        row.insert("timeText", TbString::formatUnixTime(TbJson::integer(node, "last_time")));
        row.insert("replyCountText", node.contains("count") ? TbString::shortNumber(TbJson::integer(node, "count")) : QString());
        const qint64 agreeCount = TbThread::parseAgreeCount(node);
        row.insert("hasAgreeCount", agreeCount >= 0);
        row.insert("agreeCount", agreeCount);
        row.insert("agreeCountText", agreeCount >= 0 ? TbString::shortNumber(agreeCount) : QString());
    row.insert("deleted", TbJson::boolean(node, "is_deleted")); row.insert("isGood", false); row.insert("isTop", false); row.insert("isVideo", TbThread::parseIsVideo(node));
        QVariantMap profileAuthor;
        profileAuthor.insert("id", row.value("authorId")); profileAuthor.insert("name", row.value("userName"));
        profileAuthor.insert("displayName", row.value("authorName")); profileAuthor.insert("avatarUrl", row.value("avatarUrl"));
        row.insert("author", profileAuthor);
        rows.append(row);
    }
    result.insert("items", rows); result.insert("count", list.size());
    result.insert("hasMore", json.contains("has_more") ? TbJson::boolean(json, "has_more") : list.size() >= 20);
    return result;
}
void TbFavoriteService::ensureLoaded() { restoreCache(); if (!m_backgroundStarted && !m_loading) refresh(); }
void TbFavoriteService::refresh() {
    if (!signedIn()) { m_error = QString::fromUtf8("请先登录后查看云端收藏"); notify(); return; }
    if (m_loading || m_writing) return;
    restoreCache(); m_backgroundStarted = true; m_complete = false; fetch(0);
}
void TbFavoriteService::loadMore() { if (!m_loading && m_hasMore && !m_writing) fetch(m_offset); }
void TbFavoriteService::retry() { if (!m_loading && !m_writing && signedIn()) fetch(m_failedOffset); }
void TbFavoriteService::fetch(int offset) {
    if (!signedIn()) return;
    m_loading = true; m_error.clear();
    QVariantMap context; context.insert("type", "list"); context.insert("offset", offset); context.insert("mobile", m_mobile);
    m_requests.insert(m_client->send(listRequest(m_session, offset, m_mobile)), context);
    notify();
}
void TbFavoriteService::ensureStatus(const QString &threadId) {
    if (!validId(threadId) || !signedIn()) return;
    restoreCache();
    if (m_writing || (!m_toggleRow.isEmpty() && m_statusTarget != threadId)) return;
    if (!m_cacheShown && status(threadId) >= 0) return;
    m_statusTarget = threadId;
    if (!m_loading) { if (!m_backgroundStarted || !m_loaded || m_scanOffset == 0) refresh(); else fetch(m_scanOffset); }
}
void TbFavoriteService::resolveStatus() {
    if (m_statusTarget.isEmpty()) return;
    if (status(m_statusTarget) < 0) {
        if ((m_hasMore || m_scanOffset < m_offset) && !m_loading) fetch(m_scanOffset);
        else if (!m_loading && !m_toggleRow.isEmpty()) operationError(m_statusTarget, false,
                   m_error.isEmpty() ? QString::fromUtf8("无法确认收藏状态，请刷新重试") : m_error);
        return;
    }
    const QString target = m_statusTarget; m_statusTarget.clear();
    if (!m_toggleRow.isEmpty()) {
        const QVariantMap row = m_toggleRow; m_toggleRow.clear();
        m_toggleRow = row;
        write(target, row.value("postId", "0").toString(), status(target) == 0);
    }
}
void TbFavoriteService::toggle(const QVariantMap &row) {
    const QString tid = row.value("threadId", row.value("id")).toString();
    if (!signedIn()) { operationError(tid, false, QString::fromUtf8("请先登录")); return; }
    if (!validId(tid) || m_writing || !m_toggleRow.isEmpty()) return;
    restoreCache();
    if (m_cacheShown || status(tid) < 0) { m_toggleRow = row; m_statusTarget = tid; ensureStatus(tid); notify(); return; }
    m_toggleRow = row;
    write(tid, row.value("postId", "0").toString(), status(tid) == 0);
}
void TbFavoriteService::remove(const QString &tid) {
    if (!signedIn()) { operationError(tid, false, QString::fromUtf8("请先登录")); return; }
    if (validId(tid) && !m_writing && m_toggleRow.isEmpty()) write(tid, "0", false);
}
void TbFavoriteService::write(const QString &tid, const QString &pid, bool stored, const QString &ticket) {
    m_writing = true; m_mutationError.clear();
    const QString tbs = ticket.isEmpty() ? m_session->tbs() : ticket;
    QVariantMap context; context.insert("threadId", tid); context.insert("postId", pid); context.insert("stored", stored);
    context.insert("row", m_toggleRow);
    TbHttpRequest req;
    if (tbs.isEmpty()) {
        context.insert("type", "tbs"); req.method = "GET"; req.url = "https://tieba.baidu.com/dc/common/tbs"; req.tag = "favorites.tbs";
        req.headers.append(qMakePair(QString("Cookie"), QString("BDUSS=") + m_session->bduss()));
    } else { context.insert("type", "write"); req = mutationRequest(m_session, tid, pid, stored, tbs, m_mobile); }
    m_requests.insert(m_client->send(req), context); notify();
}
void TbFavoriteService::operationError(const QString &tid, bool stored, const QString &message) {
    m_writing = false; m_toggleRow.clear(); m_statusTarget.clear(); m_mutationError = message;
    notify(); emit operationFinished(tid, stored, false, message);
}
void TbFavoriteService::onFinished(int requestId, const QString &tag, const TbJson &json, const TbHttpResponse &http) {
    Q_UNUSED(tag)
    if (!m_requests.contains(requestId) || !signedIn()) return;
    const QVariantMap context = m_requests.take(requestId);
    const QString type = context.value("type").toString();
    QString error;
    if (!http.ok) error = QString::fromUtf8("收藏请求网络错误（HTTP %1，网络 %2）").arg(http.httpStatus).arg(http.networkError);
    else if (!json.isValid()) error = QString::fromUtf8("收藏响应不是有效 JSON");
    if (type == "list") {
        const int offset = context.value("offset").toInt();
        if (error.isEmpty() && TbJson::str(json, "error_code") == "110001" && !context.value("mobile").toBool()) {
            m_mobile = true; m_loading = false; fetch(offset); return; // One bounded TiebaLite client fallback, never a write retry.
        }
        const QVariantMap parsed = error.isEmpty() ? parseList(json) : QVariantMap();
        if (error.isEmpty()) error = parsed.value("error").toString();
        m_loading = false;
        if (!error.isEmpty()) {
            m_failedOffset = offset;
            if (TbJson::str(json, "error_code") == "110001" && m_session->stoken().isEmpty())
                error = QString::fromUtf8("云端收藏返回 110001。当前账号未保存 STOKEN，可在账号管理补填后重试；尚不能确认具体原因。");
            m_error = error;
            if (!m_toggleRow.isEmpty()) operationError(m_statusTarget, false, QString::fromUtf8("无法确认云端收藏状态：") + error);
            else notify();
            return;
        }
        const QVariantList incoming = parsed.value("items").toList();
        const int oldOffset = m_offset;
        const bool oldHasMore = m_hasMore;
        bool samePrefix = offset == 0 && incoming.size() > 0 && m_rows.size() >= incoming.size();
        for (int i = 0; samePrefix && i < incoming.size(); ++i)
            samePrefix = TbUserDataCache::equivalent(m_rows.at(i).toMap(), incoming.at(i).toMap());
        if (offset == 0) { if (!samePrefix) m_rows.clear(); m_known.clear(); m_scanOffset = 0; }
        int added = 0;
        for (int i = 0; i < incoming.size(); ++i) {
            const QString tid = incoming.at(i).toMap().value("id").toString();
            if (m_known.contains(tid) && m_known.value(tid)) continue;
            m_known.insert(tid, true); ++added;
            int existing = -1;
            for (int n = 0; n < m_rows.size(); ++n) if (m_rows.at(n).toMap().value("id") == tid) { existing = n; break; }
            if (existing < 0) m_rows.append(incoming.at(i));
            else if (!TbUserDataCache::equivalent(m_rows.at(existing).toMap(), incoming.at(i).toMap())) m_rows[existing] = incoming.at(i);
        }
        const int nextOffset = offset + parsed.value("count").toInt();
        if (offset == m_scanOffset) m_scanOffset = nextOffset;
        m_offset = samePrefix || offset > 0 ? qMax(oldOffset, nextOffset) : nextOffset;
        m_loaded = true;
        m_hasMore = samePrefix && m_rows.size() > incoming.size() ? oldHasMore : added > 0 && parsed.value("hasMore").toBool();
        // A repeated nonempty page is a server paging failure, not proof that a thread is absent.
        m_complete = !parsed.value("hasMore").toBool() && m_scanOffset == nextOffset;
        if (m_complete) {
            for (int i = m_rows.size() - 1; i >= 0; --i)
                if (!m_known.contains(m_rows.at(i).toMap().value("id").toString())) m_rows.removeAt(i);
        }
        m_cacheShown = false;
        if (parsed.value("hasMore").toBool() && added == 0) m_error = QString::fromUtf8("收藏接口返回重复或空页，请刷新重试");
        saveCache(); notify(); resolveStatus(); return;
    }
    const QString tid = context.value("threadId").toString(); const bool stored = context.value("stored").toBool();
    if (type == "tbs") {
        const QString ticket = TbJson::str(json, "tbs");
        if (error.isEmpty() && (!TbJson::boolean(json, "is_login") || ticket.isEmpty())) error = QString::fromUtf8("无法取得收藏校验票据，请重新登录");
        if (!error.isEmpty()) operationError(tid, stored, error);
        else write(tid, context.value("postId").toString(), stored, ticket);
        return;
    }
    const QString code = TbJson::str(json, "error_code");
    if (error.isEmpty() && code != "0") error = QString::fromUtf8("云端收藏操作失败（%1）").arg(code.isEmpty() ? QString("missing code") : code);
    if (!error.isEmpty()) { operationError(tid, stored, error); return; }
    m_writing = false; m_known.insert(tid, stored); m_mutationError.clear(); m_toggleRow.clear();
    bool exists = false;
    for (int i = m_rows.size() - 1; i >= 0; --i) {
        if (m_rows.at(i).toMap().value("id").toString() != tid) continue;
        exists = true;
        if (!stored) m_rows.removeAt(i);
    }
    if (stored && !exists && !context.value("row").toMap().isEmpty()) {
        QVariantMap row = context.value("row").toMap();
        row.insert("id", tid); row.insert("threadId", tid); row.insert("key", tid); row.insert("resultKind", "thread");
        m_rows.prepend(row);
    }
    saveCache();
    // Invalidate old list reads so an older snapshot cannot undo this successful mutation.
    QList<int> ids = m_requests.keys();
    for (int i = 0; i < ids.size(); ++i) if (m_requests.value(ids.at(i)).value("type") == "list") m_requests.remove(ids.at(i));
    m_loading = false;
    notify(); emit operationFinished(tid, stored, true, stored ? QString::fromUtf8("已收藏到云端") : QString::fromUtf8("已取消云端收藏"));
    refresh();
}
}
