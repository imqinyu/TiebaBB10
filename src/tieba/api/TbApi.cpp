#include "TbApi.hpp"

#include "net/TbSigner.hpp"
#include "util/TbTr.hpp"
#include "session/TbSession.hpp"
#include "util/TbString.hpp"

#include <QDebug>
#include <QStringList>
#include <QSet>

namespace tieba {

namespace {

void insertAgreeCount(QVariantMap &row, const TbJson &node)
{
    const qint64 count = TbThread::parseAgreeCount(node);
    row.insert("hasAgreeCount", count >= 0);
    row.insert("agreeCount", count);
    row.insert("agreeCountText", count >= 0 ? TbString::shortNumber(count) : QString());
}

/* ---------- 业务 tag：必须与请求一一对应，才能把响应路由回正确的解析器 ---------- */
const char kTagForumList[] = "forum.list";
const char kTagForumListLike[] = "forum.list.like";
const char kTagForumPage[] = "forum.page";
const char kTagThreadContent[] = "thread.content";
const char kTagSubFloors[] = "thread.subfloors";
/* PC 端列表接口 */
const char kTagForumPagePc[] = "forum.page.pc";
const char kTagThreadPc[] = "thread.content.pc";
const char kTagChannelProbe[] = "channel.probe";

QString forumMemberCount(const TbJson &node)
{
    const char *fields[] = { "member_count", "member_num", "follow_num", "concern_num" };
    for (unsigned int i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i)
        if (node.contains(QLatin1String(fields[i])))
            return TbString::shortNumber(TbJson::integer(node, QLatin1String(fields[i])));
    return QString();
}

/*! 某个 tag 是否必须以登录态发出 */
bool tagRequiresLogin(const QString &tag)
{
    return tag == QLatin1String(kTagForumList);
}

/*! 是否属于 PC 端 tag（决定走哪个解析器） */
bool isPcTag(const QString &tag)
{
    return tag == QLatin1String(kTagForumPagePc)
           || tag == QLatin1String(kTagThreadPc)
           || tag == QLatin1String(kTagChannelProbe);
}

} // anonymous namespace

TbApiService::TbApiService(TbHttpClient *client, TbSession *session, QObject *parent)
    : QObject(parent)
    , m_client(client)
    , m_session(session)
    , m_clientVersion(QLatin1String("7.2.0.0"))
    , m_pageSize(30)
    , m_screenDip(240)
    , m_screenHeight(1280)
    , m_screenWidth(720)
    , m_pcProbed(false)
    , m_pcUsable(true)
{
    Q_ASSERT(m_client);

    // 直连（同线程）即可：解析在槽里同步完成，避免跨线程拷贝自定义结构体
    connect(m_client, SIGNAL(finished(int,QString,tieba::TbJson,tieba::TbHttpResponse)),
            this, SLOT(onHttpFinished(int,QString,tieba::TbJson,tieba::TbHttpResponse)));
}

TbApiService::~TbApiService()
{
}

void TbApiService::setClientVersion(const QString &version)
{
    if (!version.isEmpty())
        m_clientVersion = version;
}

void TbApiService::setPageSize(int value)
{
    if (value > 0 && value <= 100)
        m_pageSize = value;
}

void TbApiService::setScreenInfo(int dip, int height, int width)
{
    if (dip > 0)
        m_screenDip = dip;
    if (height > 0)
        m_screenHeight = height;
    if (width > 0)
        m_screenWidth = width;
}

void TbApiService::setDebugEnabled(bool enabled)
{
    m_client->setDebugEnabled(enabled);
}

bool TbApiService::isDebugEnabled() const
{
    return m_client->isDebugEnabled();
}

QStringList TbApiService::debugLog() const
{
    return m_client->debugLog();
}

void TbApiService::cancel(int requestId)
{
    Q_UNUSED(requestId)
    // 当前实现里取消整批在途请求（UI 切换页面时够用）
    m_client->abortAll();
}

/* ============================ 请求组装 ============================ */

TbHttpRequest TbApiService::buildLoginRequest(const QString &bduss, const QString &stoken)
{
    const QString version = QLatin1String("11.10.8.6");
    QList<KeyValue> params = TbSigner::commonParams(0, version);
    params.append(qMakePair(QString(QLatin1String("bdusstoken")), bduss + QLatin1Char('|')));
    params.append(qMakePair(QString(QLatin1String("stoken")), stoken));
    params.append(qMakePair(QString(QLatin1String("authsid")), QString(QLatin1String("null"))));
    params.append(qMakePair(QString(QLatin1String("channel_id")), QString()));
    params.append(qMakePair(QString(QLatin1String("channel_uid")), QString()));
    TbHttpRequest request;
    request.url = TbSigner::apiBase() + QLatin1String("c/s/login");
    request.tag = QLatin1String("account.login");
    request.params = params;
    request.body = TbSigner::buildAndroidForm(params);
    request.headers.append(qMakePair(QString(QLatin1String("User-Agent")), TbSigner::userAgent(version)));
    request.headers.append(qMakePair(QString(QLatin1String("Cookie")), QString(QLatin1String("ka=open"))));
    return request;
}

int TbApiService::verifyLogin(const QString &bduss, const QString &stoken)
{
    qWarning("TbApi: account.login scheme=https credentialLength=%d stokenPresent=%d",
             bduss.size(), !stoken.isEmpty());
    return m_client->send(buildLoginRequest(bduss, stoken));
}

QList<KeyValue> TbApiService::streamParams(const QString &kind, int page,
                                           const QString &cursor, const QString &requestUnix)
{
    QList<KeyValue> params;
    const bool notice = kind == "reply" || kind == "mention";
    params.append(qMakePair(QString("pn"), QString::number(notice && page == 1 ? 0 : page)));
    if (kind == "follow" || kind == "recommend") {
        params.append(qMakePair(QString("load_type"), QString::number(page == 1 ? 1 : 2)));
    }
    if (kind == "follow") {
        params.append(qMakePair(QString("follow_type"), QString("1")));
        params.append(qMakePair(QString("page_tag"), cursor));
        params.append(qMakePair(QString("last_request_unix"), requestUnix));
    } else if (kind == "recommend") {
        params.append(qMakePair(QString("page_thread_count"), QString("15")));
        params.append(qMakePair(QString("need_tags"), QString("0")));
        params.append(qMakePair(QString("need_forumlist"), QString("0")));
        params.append(qMakePair(QString("q_type"), QString("1")));
    } else if (kind == "hot") {
        params.append(qMakePair(QString("tab_id"), QString("1")));
        params.append(qMakePair(QString("tab_code"), QString("all")));
    }
    return params;
}

int TbApiService::fetchStream(const QString &kind, int page, const QString &cursor, const QString &requestUnix)
{
    QString path;
    if (kind == "follow") path = "/c/f/concern/userlike";
    else if (kind == "recommend") path = "/c/f/excellent/personalized";
    else if (kind == "hot") path = "/c/f/forum/hotThreadList";
    else if (kind == "reply") path = "/c/u/feed/replyme";
    else if (kind == "mention") path = "/c/u/feed/atme";
    else return -1;
    return postPc(path, streamParams(kind, page, cursor, requestUnix), "stream." + kind);
}

QVariantMap TbApiService::parseStream(const QString &kind, const TbJson &json)
{
    QVariantMap result;
    const bool notice = kind == "reply" || kind == "mention";
    const QString key = kind == "reply" ? "reply_list" : kind == "mention" ? "at_list"
                        : kind == "recommend" ? "thread_list" : "thread_info";
    const QString code = TbJson::str(json, "error_code");
    if (code != "0") {
        result.insert("error", code.isEmpty() ? QString::fromUtf8("接口响应缺少错误码")
            : QString::fromUtf8("服务端错误码：") + code);
        return result;
    }
    const TbJson list = json.member(key);
    // 通知接口会用 0 或空字符串表示空列表（与 TiebaLite MessageListAdapter 对齐）。
    const bool emptyNotice = notice && json.contains(key) && !list.isObject() && !list.isArray()
        && (list.isNull() || list.toString().isEmpty() || list.toString() == "0");
    if (!list.isArray() && !emptyNotice) {
        result.insert("error", QString::fromUtf8("接口响应缺少有效的列表字段：") + key);
        return result;
    }
    QMap<QString, TbJson> users;
    const TbJson userList = json.member("user_list");
    for (int i = 0; i < userList.size(); ++i) users.insert(TbJson::str(userList.at(i), "id"), userList.at(i));
    QVariantList rows;
    for (int i = 0; i < list.size(); ++i) {
        TbJson node = list.at(i);
        if (kind == "follow") node = node.member("thread_list");
        if (!node.isObject()) continue;
        const QString id = notice ? TbJson::str(node, "thread_id") : TbJson::str2(node, "tid", "id");
        if (id.isEmpty() || id == "0") continue; // 跳过广告、系统提示和非帖子条目。
        TbJson author = node.member(notice ? "replyer" : "author");
        if (!author.isObject()) author = users.value(author.toString());
        const TbJson forum = node.member("forum_info");
        QVariantMap row;
        row.insert("notification", notice);
        row.insert("id", id);
        row.insert("threadId", id);
        row.insert("postId", TbJson::str(node, "post_id"));
        row.insert("quotePostId", TbJson::str(node, "quote_pid"));
        row.insert("isFloor", TbJson::boolean(node, "is_floor"));
        const QString identity = notice ? id + ":" + TbJson::str(node, "post_id") + ":" + TbJson::str(node, "time") + ":" + TbJson::str(author, "id") : id;
        row.insert("key", identity);
        row.insert("title", TbString::stripHtml(TbJson::str(node, "title")));
        row.insert("forumName", TbJson::str(node, "fname", TbJson::str(forum, "name")));
        row.insert("authorId", TbJson::str(author, "id", TbJson::str(node, "user_id")));
        row.insert("userName", TbJson::str(author, "name"));
        row.insert("authorName", TbJson::str(author, "name_show", TbJson::str(author, "name")));
        row.insert("authorLevel", TbJson::intValue(author, "level_id"));
        row.insert("remoteAvatarUrl", TbString::avatarUrl(TbJson::str(author, "portrait")));
        QVariantMap profileAuthor;
        profileAuthor.insert("id", row.value("authorId"));
        profileAuthor.insert("name", row.value("userName"));
        profileAuthor.insert("displayName", row.value("authorName"));
        profileAuthor.insert("avatarUrl", row.value("remoteAvatarUrl"));
        row.insert("author", profileAuthor);
        row.insert("localAvatarPath", QString());
        row.insert("timeText", TbString::formatUnixTime(TbJson::integer(node, notice ? "time" : "create_time")));
        row.insert("unread", TbJson::boolean(node, "unread"));
        row.insert("contentText", TbString::stripHtml(TbJson::str(node, "content")));
        row.insert("quoteText", TbString::stripHtml(TbJson::str(node, "quote_content")));
        QString abstract;
        const TbJson segments = node.member("abstract");
        if (segments.isArray()) {
            for (int n = 0; n < segments.size(); ++n) abstract += TbJson::str(segments.at(n), "text");
        } else abstract = segments.toString();
        row.insert("abstractText", TbString::stripHtml(abstract));
        row.insert("replyCountText", TbString::shortNumber(TbJson::integer(node, "reply_num")));
        insertAgreeCount(row, node);
        row.insert("viewCountText", node.contains("view_num") ? TbString::shortNumber(TbJson::integer(node, "view_num")) : QString());
        row.insert("hotText", node.contains("hot_num") ? TbString::shortNumber(TbJson::integer(node, "hot_num")) : QString());
        row.insert("isGood", TbJson::boolean(node, "is_good"));
        row.insert("isTop", TbJson::boolean(node, "is_top"));
        row.insert("isFollowedForum", TbJson::boolean(forum, "is_liked"));
        row.insert("isVideo", TbThread::parseIsVideo(node));
        rows.append(row);
    }
    bool more = notice ? TbJson::boolean(json.member("page"), "has_more") : TbJson::boolean(json, "has_more");
    if (kind == "recommend") {
        const TbJson pageData = json.member("page_data");
        more = json.contains("has_more") ? more : pageData.contains("has_more")
            ? TbJson::boolean(pageData, "has_more") : list.size() > 0;
    }
    result.insert("items", rows);
    result.insert("hasMore", more);
    result.insert("cursor", TbJson::str(json, "page_tag"));
    result.insert("requestUnix", TbJson::str(json, "req_unix"));
    return result;
}

QList<KeyValue> TbApiService::userPageParams(const QString &kind, const QString &uid, int page)
{
    QList<KeyValue> params;
    params.append(qMakePair(QString("uid"), uid));
    params.append(qMakePair(QString(kind == "forums" ? "page_no" : "pn"), QString::number(qMax(1, page))));
    if (kind == "profile") {
        params.append(qMakePair(QString("need_post_count"), QString("1")));
        params.append(qMakePair(QString("is_from_usercenter"), QString("1")));
        params.append(qMakePair(QString("page"), QString::number(qMax(1, page))));
    } else if (kind == "threads") {
        params.append(qMakePair(QString("is_thread"), QString("1")));
        params.append(qMakePair(QString("rn"), QString("20")));
        params.append(qMakePair(QString("need_content"), QString("1")));
    } else if (kind == "forums") {
        params.append(qMakePair(QString("page_size"), QString("50")));
    }
    return params;
}

int TbApiService::fetchUserPage(const QString &uid, const QString &kind, int page)
{
    const QString path = kind == "profile" ? "/c/u/user/profile" : kind == "threads" ? "/c/u/feed/userpost"
                       : kind == "forums" ? "/c/f/forum/like" : kind == "follows" ? "/c/u/follow/followList" : QString();
    if (path.isEmpty() || uid.isEmpty()) return -1;
    QList<KeyValue> params = userPageParams(kind, uid, page);
    if (kind == "forums" && m_session) {
        const QString selfUid = m_session->currentAccount().value("uid").toString();
        if (selfUid != uid) {
            params[0].second = selfUid;
            params.append(qMakePair(QString("friend_uid"), uid));
            params.append(qMakePair(QString("is_guest"), QString("1")));
        }
    }
    if (kind == "profile" && m_session && !m_session->stoken().isEmpty())
        params.append(qMakePair(QString("stoken"), m_session->stoken()));
    if (kind == "profile" && m_session && !m_session->tbs().isEmpty())
        params.append(qMakePair(QString("tbs"), m_session->tbs()));
    // Authenticated Android requests return 110001 on BB10; use the same
    // client identity/signature as the verified forum and thread PC channel.
    return postPc(path, params, "userpage." + kind + "." + uid);
}

QVariantMap TbApiService::parseUserPage(const QString &kind, const TbJson &json)
{
    QVariantMap result;
    if (kind != "profile" && kind != "threads" && kind != "forums" && kind != "follows") {
        result.insert("error", QString::fromUtf8("不支持的用户页面类型"));
        return result;
    }
    const QString code = TbJson::str(json, "error_code");
    if (code != "0") {
        const QString message = TbJson::str(json, "error_msg");
        result.insert("error", code.isEmpty() ? QString::fromUtf8("接口响应缺少错误码")
            : code == "110001" ? QString::fromUtf8("用户主页接口拒绝此客户端请求（110001），请刷新重试")
            : (message.isEmpty() ? QString::fromUtf8("用户主页请求失败") : message) + " (" + code + ")");
        return result;
    }
    const TbJson user = json.member("user");
    if (kind == "profile") {
        if (!user.isObject() || TbJson::str(user, "id").isEmpty() || TbJson::str(user, "id") == "0") {
            result.insert("error", QString::fromUtf8("用户资料响应缺少有效用户"));
            return result;
        }
        QVariantMap profile;
        profile.insert("id", TbJson::str(user, "id"));
        const QString name = TbJson::str(user, "name");
        const QString displayName = TbJson::str(user, "name_show");
        profile.insert("name", displayName.isEmpty() ? name : displayName);
        profile.insert("userName", name);
        profile.insert("avatarUrl", TbString::avatarUrl(TbJson::str(user, "portrait")));
        if (user.contains("concern_num")) profile.insert("followingCount", TbJson::integer(user, "concern_num"));
        if (user.contains("fans_num")) profile.insert("fansCount", TbJson::integer(user, "fans_num"));
        const TbJson agree = json.member("user_agree_info");
        if (user.contains("total_agree_num")) profile.insert("agreeCount", TbJson::integer(user, "total_agree_num"));
        else if (agree.contains("total_agree_num")) profile.insert("agreeCount", TbJson::integer(agree, "total_agree_num"));
        profile.insert("intro", TbJson::str(user, "intro"));
        if (user.member("priv_sets").isObject()) {
            QVariantMap privacy;
            const TbJson settings = user.member("priv_sets");
            const QStringList keys = settings.keys();
            for (int n = 0; n < keys.size(); ++n) privacy.insert(keys.at(n), TbJson::intValue(settings, keys.at(n)));
            profile.insert("privacy", privacy);
        }
        result.insert("profile", profile);
        return result;
    }
    TbJson list = json.member(kind == "threads" ? "post_list" : kind == "forums" ? "forum_list" : "follow_list");
    TbJson extraForums;
    if (kind == "forums" && list.isObject()) {
        extraForums = list.member("gconforum");
        list = list.member("non-gconforum");
        if (!list.isArray() && extraForums.isArray()) {
            list = extraForums;
            extraForums = TbJson();
        }
    }
    const bool hidden = kind == "threads" && TbJson::intValue(json, "hide_post") == 1;
    const QString tips = TbJson::str(json, "tips_text");
    if (!list.isArray() && !hidden && !(kind == "follows" && !tips.isEmpty() && json.contains("follow_list"))) {
        result.insert("error", QString::fromUtf8("用户页面响应缺少有效列表"));
        return result;
    }
    QVariantList rows;
    for (int i = 0; i < list.size() + extraForums.size(); ++i) {
        const TbJson node = i < list.size() ? list.at(i) : extraForums.at(i - list.size());
        const QString id = TbJson::str(node, kind == "threads" ? "thread_id" : "id");
        if (id.isEmpty() || id == "0") continue;
        QVariantMap row;
        row.insert("id", id);
        row.insert("key", kind == "threads" ? id + ":" + TbJson::str(node, "post_id") : id);
        row.insert("resultKind", kind == "threads" ? "thread" : kind == "forums" ? "forum" : "user");
        row.insert("authorLevel", 0);
        row.insert("localAvatarPath", QString());
        if (kind == "threads") {
            insertAgreeCount(row, node);
            row.insert("isVideo", TbThread::parseIsVideo(node));
            row.insert("threadId", id);
            row.insert("postId", TbJson::str(node, "post_id"));
            row.insert("title", TbString::stripHtml(TbJson::str(node, "title")));
            row.insert("forumName", TbJson::str(node, "forum_name"));
            row.insert("authorId", TbJson::str(node, "user_id"));
            row.insert("userName", TbJson::str(node, "user_name"));
            const QString display = TbJson::str(node, "name_show");
            row.insert("authorName", display.isEmpty() ? TbJson::str(node, "user_name") : display);
            row.insert("avatarUrl", TbString::avatarUrl(TbJson::str(node, "user_portrait")));
            row.insert("authorAvatarUrl", row.value("avatarUrl"));
            row.insert("timeText", TbString::formatUnixTime(TbJson::integer(node, "create_time")));
            row.insert("replyCountText", TbString::shortNumber(TbJson::integer(node, "reply_num")));
            QString text;
            const TbJson content = node.member("content");
            if (content.isArray()) {
                for (int n = 0; n < content.size(); ++n) {
                    const TbJson parts = content.at(n).member("post_content");
                    if (parts.isArray()) for (int p = 0; p < parts.size(); ++p) text += TbJson::str(parts.at(p), "text");
                    else text += TbJson::str(content.at(n), "text");
                }
            } else text = content.toString();
            if (text.isEmpty()) {
                const TbJson abstract = node.member("abstract");
                if (abstract.isArray()) for (int n = 0; n < abstract.size(); ++n) text += TbJson::str(abstract.at(n), "text");
                else text = abstract.toString();
            }
            row.insert("abstractText", TbString::stripHtml(text));
        } else if (kind == "forums") {
            row.insert("name", TbJson::str(node, "name"));
            row.insert("forumName", TbJson::str(node, "name"));
            row.insert("avatarUrl", TbString::httpsImageUrl(TbJson::str(node, "avatar")));
            row.insert("level", TbJson::intValue(node, "level_id"));
            row.insert("slogan", TbJson::str(node, "slogan"));
            row.insert("memberCountText", forumMemberCount(node));
        } else {
            const QString name = TbJson::str(node, "name");
            const QString display = TbJson::str(node, "name_show");
            row.insert("name", display.isEmpty() ? name : display);
            row.insert("authorName", row.value("name"));
            row.insert("userName", name);
            row.insert("authorId", id);
            row.insert("avatarUrl", TbString::avatarUrl(TbJson::str(node, "portrait")));
            row.insert("authorAvatarUrl", row.value("avatarUrl"));
            row.insert("authorLabel", TbJson::str(node, "intro"));
        }
        row.insert("remoteAvatarUrl", row.value("avatarUrl"));
        rows.append(row);
    }
    result.insert("items", rows);
    result.insert("hasMore", !hidden && (json.contains("has_more") ? TbJson::boolean(json, "has_more") : list.size() + extraForums.size() > 0));
    if (hidden) result.insert("private", true);
    if (!tips.isEmpty()) result.insert("message", tips);
    return result;
}

TbHttpRequest TbApiService::buildSearchRequest(const QString &query, const QString &kind,
                                              const QString &forum, int page)
{
    // TiebaLite AppHybridTiebaApi: public search is a WebView GET endpoint.
    // An explicit anonymous Cookie also prevents TbHttpClient adding session cookies.
    TbHttpRequest request;
    request.method = "GET";
    request.url = "https://tieba.baidu.com/mo/q/search/" + kind;
    request.tag = "search." + kind;
    request.params.append(qMakePair(QString("word"), query.trimmed()));
    request.params.append(qMakePair(QString("pn"), QString::number(qMax(1, page))));
    if (kind == "thread") {
        request.params.append(qMakePair(QString("rn"), QString("20")));
        request.params.append(qMakePair(QString("st"), QString("0")));
        request.params.append(qMakePair(QString("tt"), QString("1")));
        request.params.append(qMakePair(QString("ct"), forum.isEmpty() ? QString("1") : QString("2")));
        request.params.append(qMakePair(QString("cv"), forum.isEmpty() ? QString("99.9.101") : QString("12.52.1.0")));
        if (forum.isEmpty()) request.params.append(qMakePair(QString("is_use_zonghe"), QString("1")));
        else request.params.append(qMakePair(QString("fname"), forum));
    }
    request.headers.append(qMakePair(QString("Cookie"), QString("ka=open")));
    request.headers.append(qMakePair(QString("User-Agent"), QString("Mozilla/5.0 (BlackBerry; Touch) tieba/12.52.1.0 skin/default")));
    request.headers.append(qMakePair(QString("Accept"), QString("application/json, text/plain, */*")));
    request.headers.append(qMakePair(QString("X-Requested-With"), QString("com.baidu.tieba")));
    request.headers.append(qMakePair(QString("Referer"), QString("https://tieba.baidu.com/mo/q/hybrid/search?keyword=") + TbString::percentEncode(query)));
    return request;
}

int TbApiService::fetchSearch(const QString &query, const QString &kind, const QString &forum, int page)
{
    if (query.trimmed().isEmpty() || (kind != "thread" && kind != "forum" && kind != "user")) return -1;
    return m_client->send(buildSearchRequest(query, kind, forum, page));
}

QVariantMap TbApiService::parseSearch(const QString &kind, const TbJson &json)
{
    QVariantMap result;
    const QString code = TbJson::str(json, "no");
    if (code != "0") {
        const QString message = TbJson::str(json, "error");
        result.insert("error", code.isEmpty() ? QString::fromUtf8("搜索响应缺少状态码")
            : (message.isEmpty() ? QString::fromUtf8("搜索请求失败") : message) + " (" + code + ")");
        return result;
    }
    const TbJson data = json.member("data");
    if (!data.isObject() || (kind != "thread" && kind != "forum" && kind != "user")) {
        result.insert("error", QString::fromUtf8("搜索响应缺少有效数据"));
        return result;
    }
    QList<TbJson> nodes;
    if (kind == "thread") {
        const TbJson list = data.member("post_list");
        if (!list.isArray()) {
            result.insert("error", QString::fromUtf8("帖子搜索响应缺少列表"));
            return result;
        }
        for (int i = 0; i < list.size(); ++i) nodes.append(list.at(i));
    } else {
        if (!data.contains("exactMatch") && !data.contains("fuzzyMatch")) {
            result.insert("error", QString::fromUtf8("搜索响应缺少匹配列表"));
            return result;
        }
        const TbJson exact = data.member("exactMatch");
        if (exact.isObject()) nodes.append(exact);
        const TbJson fuzzy = data.member("fuzzyMatch");
        if (fuzzy.isArray()) for (int i = 0; i < fuzzy.size(); ++i) nodes.append(fuzzy.at(i));
        else if (fuzzy.isObject()) {
            const QStringList keys = fuzzy.keys();
            for (int i = 0; i < keys.size(); ++i) nodes.append(fuzzy.member(keys.at(i)));
        } else if (!fuzzy.isNull() && !fuzzy.toString().isEmpty() && fuzzy.toString() != "0") {
            result.insert("error", QString::fromUtf8("搜索匹配列表格式错误"));
            return result;
        }
    }
    QVariantList rows;
    QSet<QString> seen;
    for (int i = 0; i < nodes.size(); ++i) {
        const TbJson node = nodes.at(i);
        if (!node.isObject()) continue;
        const QString id = TbJson::str(node, kind == "thread" ? "tid" : kind == "forum" ? "forum_id" : "id");
        if (id.isEmpty() || id == "0") continue;
        const QString key = kind == "thread" ? id + ":" + TbJson::str(node, "pid") : id;
        if (seen.contains(key)) continue;
        seen.insert(key);
        QVariantMap row;
        row.insert("id", id);
        row.insert("key", key);
        row.insert("resultKind", kind);
        row.insert("authorLevel", 0);
        row.insert("localAvatarPath", QString());
        row.insert("avatarUrl", QString());
        if (kind == "thread") {
            insertAgreeCount(row, node);
            const TbJson user = node.member("user");
            row.insert("threadId", id);
            row.insert("postId", TbJson::str(node, "pid"));
            row.insert("title", TbString::stripHtml(TbJson::str(node, "title")));
            row.insert("abstractText", TbString::stripHtml(TbJson::str(node, "content")));
            row.insert("forumName", TbJson::str(node, "forum_name"));
            row.insert("forumId", TbJson::str(node, "forum_id"));
            row.insert("authorId", TbJson::str2(user, "user_id", "id"));
            row.insert("userName", TbJson::str(user, "name"));
            const QString nick = TbJson::str2(user, "show_nickname", "user_nickname");
            row.insert("authorName", nick.isEmpty() ? TbJson::str(user, "name") : TbString::stripHtml(nick));
            row.insert("avatarUrl", TbString::avatarUrl(TbJson::str(user, "portrait")));
            row.insert("authorAvatarUrl", row.value("avatarUrl"));
            row.insert("replyCountText", TbString::shortNumber(TbJson::integer(node, "post_num")));
            row.insert("timeText", TbJson::str(node, "time"));
            row.insert("isVideo", TbThread::parseIsVideo(node));
            row.insert("isGood", false);
            row.insert("isTop", false);
        } else if (kind == "forum") {
            row.insert("name", TbJson::str(node, "forum_name"));
            row.insert("forumName", row.value("name"));
            row.insert("avatarUrl", TbString::httpsImageUrl(TbJson::str(node, "avatar")));
            row.insert("level", 0);
            row.insert("slogan", TbString::stripHtml(TbJson::str2(node, "slogan", "intro")));
            row.insert("memberCountText", forumMemberCount(node));
        } else {
            const QString nick = TbJson::str2(node, "show_nickname", "user_nickname");
            const QString name = TbJson::str(node, "name");
            row.insert("name", TbString::stripHtml(nick.isEmpty() ? name : nick));
            row.insert("authorName", row.value("name"));
            row.insert("userName", name);
            row.insert("authorId", id);
            row.insert("avatarUrl", TbString::avatarUrl(TbJson::str(node, "portrait")));
            row.insert("authorAvatarUrl", row.value("avatarUrl"));
            row.insert("authorLabel", TbString::stripHtml(TbJson::str(node, "intro")));
        }
        row.insert("remoteAvatarUrl", row.value("avatarUrl"));
        QVariantMap author;
        author.insert("id", row.value("authorId"));
        author.insert("name", row.value("userName"));
        author.insert("displayName", row.value("authorName"));
        author.insert("avatarUrl", row.value("avatarUrl"));
        row.insert("author", author);
        rows.append(row);
    }
    result.insert("items", rows);
    result.insert("hasMore", TbJson::boolean(data, "has_more"));
    result.insert("page", TbJson::intValue(data, kind == "thread" ? "current_page" : "pn"));
    return result;
}

QList<KeyValue> TbApiService::baseParams() const
{
    return TbSigner::commonParams(m_session, m_clientVersion);
}

QList<KeyValue> TbApiService::baseHeaders(bool forceLogin) const
{
    QList<KeyValue> headers;
    headers.append(qMakePair(QString(QLatin1String("User-Agent")),
                             TbSigner::userAgent(m_clientVersion)));
    headers.append(qMakePair(QString(QLatin1String("client_type")),
                             QString(QLatin1String("2"))));
    headers.append(qMakePair(QString(QLatin1String("Charset")),
                             QString(QLatin1String("UTF-8"))));

    if (forceLogin)
        headers.append(qMakePair(QString(QLatin1String("force_login")),
                                 QString(QLatin1String("true"))));

    // Cookie 里也带一份 BDUSS：部分接口只认 cookie
    QString cookie = QLatin1String("ka=open");
    if (m_session && !m_session->bduss().isEmpty()) {
        cookie += QLatin1String("; BDUSS=") + m_session->bduss();
        if (!m_session->stoken().isEmpty())
            cookie += QLatin1String("; STOKEN=") + m_session->stoken();
    }
    headers.append(qMakePair(QString(QLatin1String("Cookie")), cookie));

    return headers;
}

int TbApiService::post(const QString &path, const QList<KeyValue> &params,
                       const QString &tag, const QList<KeyValue> &extraHeaders)
{
    // 拼完整 URL：path 以 '/' 开头
    QString url = TbSigner::apiBase();
    if (url.endsWith(QLatin1Char('/')) && path.startsWith(QLatin1Char('/')))
        url.chop(1);
    url += path;

    // 合并业务参数 + 公共参数，然后签名
    QList<KeyValue> all = params;
    const QList<KeyValue> common = baseParams();
    for (int i = 0; i < common.size(); ++i) {
        bool exists = false;
        for (int j = 0; j < all.size(); ++j) {
            if (all.at(j).first == common.at(i).first) {
                exists = true;
                break;
            }
        }
        if (!exists)
            all.append(common.at(i));
    }

    TbHttpRequest request;
    request.method = "POST";
    request.url = url;
    request.tag = tag;
    request.params = all;
    request.timeoutMs = 30000;

    // 签名结果直接作为参数发送（TbSigner 会处理排序与 sign 追加）
    request.body = TbSigner::buildSignedForm(all);

    QList<KeyValue> headers = baseHeaders(tagRequiresLogin(tag));
    for (int i = 0; i < extraHeaders.size(); ++i)
        headers.append(extraHeaders.at(i));
    request.headers = headers;

    return m_client->send(request);
}

/*!
 * PC 端接口请求。
 *
 * 与 post() 的差别：换盐、换公共参数（subapp_type=pc / _client_type=20）、
 * 换 UA。端点也必须是 *_pc 形式。
 */
int TbApiService::postPc(const QString &path, const QList<KeyValue> &params,
                         const QString &tag)
{
    QString url = TbSigner::apiBase();
    if (url.endsWith(QLatin1Char('/')) && path.startsWith(QLatin1Char('/')))
        url.chop(1);
    url += path;

    QList<KeyValue> all = TbSigner::withPcCommonParams(params, m_session);

    TbHttpRequest request;
    request.method = "POST";
    request.url = url;
    request.tag = tag;
    request.params = all;
    request.timeoutMs = 30000;
    request.body = TbSigner::buildPcForm(all);

    QList<KeyValue> headers;
    headers.append(qMakePair(QString(QLatin1String("User-Agent")), TbSigner::pcUserAgent()));
    headers.append(qMakePair(QString(QLatin1String("Accept")),
                             QString(QLatin1String("application/json, text/plain, */*"))));
    headers.append(qMakePair(QString(QLatin1String("Accept-Language")),
                             QString(QLatin1String("zh-CN,zh;q=0.9"))));
    headers.append(qMakePair(QString(QLatin1String("Referer")),
                             QString(QLatin1String("https://tieba.baidu.com/"))));

    QString cookie = QLatin1String("ka=open");
    if (m_session && !m_session->bduss().isEmpty()) {
        cookie += QLatin1String("; BDUSS=") + m_session->bduss();
        if (!m_session->stoken().isEmpty())
            cookie += QLatin1String("; STOKEN=") + m_session->stoken();
    }
    headers.append(qMakePair(QString(QLatin1String("Cookie")), cookie));

    request.headers = headers;

    // 只记录请求形状，不记录凭据或表单正文。
    if (tag == QLatin1String(kTagForumListLike))
        qWarning("TbApi: forum.list.like scheme=https credentialPresent=%d credentialLength=%d",
                 m_session && !m_session->bduss().isEmpty(),
                 m_session ? m_session->bduss().size() : 0);
    return m_client->send(request);
}

/* ============================ PC 端列表路径 ============================
 *
 * 常规吧列表和帖内分页使用 PC 接口。
 * 早期移动端 110001 的观察使用了旧签名，不能据此判断接口失效；
 * 登录及 PID 定位请求现已使用校验通过的 Android 客户端签名。
 * ============================================================================ */

int TbApiService::fetchForumPagePc(const QString &forumName, int page, int sortType,
                                   bool goodOnly)
{
    QList<KeyValue> params;
    params.append(qMakePair(QString(QLatin1String("kw")), forumName));
    params.append(qMakePair(QString(QLatin1String("pn")),
                            QString::number(page < 1 ? 1 : page)));

    /*
     * ★★ "最新 / 精华" 到底怎么传给服务端（这里踩过两次坑）
     *
     * 坑 1（最初）：以为"精华"就是 sort_type=1 —— 错。
     *   查 Android 版 TiebaLite 的 Enums.kt：
     *       enum class ForumSortType(val value: Int) {
     *           REPLY_TIME(0), SEND_TIME(1), ONLY_FOLLOWED(2)
     *       }
     *   也就是说 sort_type=1 的含义是**按发帖时间排序**，和"精华"没关系。
     *   而 MixedTiebaApiImpl.kt 的 frsPage 里：
     *       is_good = if (goodClassifyId != null) 1 else 0     ← 精华是独立参数
     *       sort_type = sortType                               ← 只管排序
     *
     * 坑 2（上一版）：既用了 sort_type=-1 又想表达"精华"，
     *   -1 在移动端语义里根本不是 REPLY_TIME（是 0），
     *   PC 接口对 -1 的行为也没有依据 —— 结果就是"列表里的贴子不是最新的"。
     *
     * 现在严格按安卓的定义来：
     *     sortType == 0 -> 按回复时间（最新）   ← 默认
     *     sortType == 1 -> 按发帖时间
     *     sortType == 2 -> 只看关注
     *     goodOnly      -> 额外加 is_good=1（只看精品），与排序无关
     */
    int sort = sortType;
    if (sort < 0 || sort > 2)
        sort = 0;                       // 兜底：非法值一律按"最新"
    params.append(qMakePair(QString(QLatin1String("sort_type")),
                            QString::number(sort)));

    if (goodOnly)
        params.append(qMakePair(QString(QLatin1String("is_good")),
                                QString::fromUtf8("1")));

    params.append(qMakePair(QString(QLatin1String("is_newfrs")), QString::fromUtf8("1")));
    params.append(qMakePair(QString(QLatin1String("is_newfeed")), QString::fromUtf8("1")));
    params.append(qMakePair(QString(QLatin1String("rn")), QString::number(m_pageSize)));
    params.append(qMakePair(QString(QLatin1String("rn_need")), QString::fromUtf8("10")));

    return postPc(QLatin1String("/c/f/frs/page_pc"), params,
                  QLatin1String(kTagForumPagePc));
}

int TbApiService::fetchThreadContentPc(const QString &threadId, int page,
                                       bool onlyOriginalPoster, bool reverse,
                                       const QString &postId, const QString &subPostId)
{
    if (!postId.isEmpty()) {
        const int requestId = m_client->send(buildThreadPostRequest(m_session, threadId, postId, onlyOriginalPoster, reverse));
        if (requestId >= 0) m_postResolveRequests.insert(requestId);
        return requestId;
    }
    return postPc(QLatin1String("/c/f/pb/page_pc"),
                  buildThreadContentPcParams(threadId, page, onlyOriginalPoster, reverse, postId, subPostId),
                  QLatin1String(kTagThreadPc));
}

TbHttpRequest TbApiService::buildThreadPostRequest(const TbSession *session, const QString &threadId,
        const QString &postId, bool onlyOriginalPoster, bool reverse)
{
    QList<KeyValue> params;
    params.append(qMakePair(QString("kz"), threadId));
    params.append(qMakePair(QString("pid"), postId));
    params.append(qMakePair(QString("lz"), QString(onlyOriginalPoster ? "1" : "0")));
    if (reverse) {
        params.append(qMakePair(QString("r"), QString("1")));
        params.append(qMakePair(QString("last"), QString("1")));
    }
    params.append(qMakePair(QString("st_type"), QString("tb_frslist")));
    params.append(qMakePair(QString("back"), QString("0")));
    params.append(qMakePair(QString("floor_rn"), QString("3")));
    params.append(qMakePair(QString("mark"), QString("0")));
    params.append(qMakePair(QString("rn"), QString("30")));
    params.append(qMakePair(QString("with_floor"), QString("1")));
    const QString version = QLatin1String("11.10.8.6");
    TbHttpRequest request;
    request.url = TbSigner::apiBase() + QLatin1String("c/f/pb/page");
    request.tag = QLatin1String(kTagThreadPc);
    request.params = TbSigner::withCommonParams(params, session, version);
    request.body = TbSigner::buildAndroidForm(request.params);
    request.headers.append(qMakePair(QString("User-Agent"), TbSigner::userAgent(version)));
    request.headers.append(qMakePair(QString("Cookie"), QString("ka=open")));
    request.headers.append(qMakePair(QString("thread_id"), threadId));
    return request;
}

QList<KeyValue> TbApiService::buildThreadContentPcParams(const QString &threadId, int page,
        bool onlyOriginalPoster, bool reverse, const QString &postId, const QString &subPostId)
{
    // Mobile TiebaLite pb/page has a pid overload. Anonymous PC page_pc probes
    // require pn (pid-only => 210009) and ignore pid for page selection; the
    // controller must locate the response row and search another page if needed.
    // spid belongs to the floor API, and remains a controller-local target here.
    Q_UNUSED(subPostId);
    QList<KeyValue> params;
    params.append(qMakePair(QString(QLatin1String("kz")), threadId));
    params.append(qMakePair(QString(QLatin1String("pn")), QString::number(page < 1 ? 1 : page)));
    if (!postId.isEmpty())
        params.append(qMakePair(QString(QLatin1String("pid")), postId));
    params.append(qMakePair(QString(QLatin1String("lz")),
                            onlyOriginalPoster ? QString::fromUtf8("1")
                                               : QString::fromUtf8("0")));
    params.append(qMakePair(QString(QLatin1String("r")), QString::fromUtf8(reverse ? "1" : "2")));
    if (reverse)
        params.append(qMakePair(QString(QLatin1String("last")), QString::fromUtf8("1")));
    params.append(qMakePair(QString(QLatin1String("mark_type")), QString::fromUtf8("0")));
    params.append(qMakePair(QString(QLatin1String("back")), QString::fromUtf8("0")));
    params.append(qMakePair(QString(QLatin1String("fr")), QString()));
    params.append(qMakePair(QString(QLatin1String("session_request_times")),
                            QString::fromUtf8("1")));

    return params;
}

int TbApiService::fetchSubFloorsPc(const QString &threadId, const QString &postId,
                                   int page)
{
    QList<KeyValue> params;
    params.append(qMakePair(QString(QLatin1String("kz")), threadId));
    params.append(qMakePair(QString(QLatin1String("pid")), postId));
    params.append(qMakePair(QString(QLatin1String("pn")),
                            QString::number(page < 1 ? 1 : page)));
    params.append(qMakePair(QString(QLatin1String("rn")), QString::fromUtf8("20")));

    // ★ 端点与移动端相同（/c/f/pb/floor），但**必须用 PC 签名**：
    //     签名方式        结果
    //     tiebaclient!!!  error_code=110001
    //     PC salt         error_code=0，subpost_list[] 正常返回
    return postPc(QLatin1String("/c/f/pb/floor"), params,
                  QLatin1String(kTagSubFloors));
}

/*!
 * 探测 PC 通道是否可用。
 *
 * PC 接口的可用性依赖服务端，随时可能变。与其把端点写死、出问题只能重新
 * 构建，不如第一次请求时探测一次：失败就自动回退到移动端端点。
 */
int TbApiService::probeChannel()
{
    QList<KeyValue> params;
    params.append(qMakePair(QString(QLatin1String("kw")), QString::fromUtf8("baidu")));
    params.append(qMakePair(QString(QLatin1String("pn")), QString::fromUtf8("1")));
    params.append(qMakePair(QString(QLatin1String("sort_type")), QString::fromUtf8("-1")));
    params.append(qMakePair(QString(QLatin1String("is_newfrs")), QString::fromUtf8("1")));
    params.append(qMakePair(QString(QLatin1String("is_newfeed")), QString::fromUtf8("1")));
    params.append(qMakePair(QString(QLatin1String("rn")), QString::fromUtf8("1")));
    params.append(qMakePair(QString(QLatin1String("rn_need")), QString::fromUtf8("1")));
    return postPc(QLatin1String("/c/f/frs/page_pc"), params,
                  QLatin1String(kTagChannelProbe));
}


int TbApiService::fetchForumList()
{
    QList<KeyValue> params;
    params.append(qMakePair(QString(QLatin1String("like_forum")), QString::fromUtf8("1")));
    params.append(qMakePair(QString(QLatin1String("recommend")), QString::fromUtf8("0")));
    params.append(qMakePair(QString(QLatin1String("topic")), QString::fromUtf8("0")));

    // ★ 同一个端点，换成 PC 签名就能用（实测）：
    //     签名方式        结果
    //     tiebaclient!!!  error_code=110001
    //     PC salt         error_code=0，且返回更丰富（avatar / is_sign / level_id / is_top）
    //   响应结构与移动端一致（like_forum[]），所以 TbParsers::parseForumRecommend 可直接复用。
    return postPc(QLatin1String("/c/f/forum/forumrecommend"), params,
                  QLatin1String(kTagForumList));
}

int TbApiService::fetchForumListPaged(int page, int pageSize)
{
    /*
     * ★★ 为什么需要这个接口（踩过的坑）
     *
     * 上面那个 /c/f/forum/forumrecommend 有个**约 99 条的上限**：
     * 实测返回 99 条，且最后一条是 `freebsd`。
     * 关注数超过 99 的用户，后面的吧就**永远加载不出来**。
     *
     * /c/f/forum/like 是带分页的完整版：
     *     参数：page_no（从 1 开始）、page_size
     *     响应：forum_list.non-gconforum[] + forum_list.gconforum[]
     *           （实测 121 个吧 = 118 普通 + 3 个 "gcon" 类吧，两边都要收）
     *     分页：has_more 字段。（page_no=2, page_size=50 时 has_more=1，说明有效）
     *
     * 字段名和 forumrecommend **不一样**，别混：
     *     forumrecommend : forum_id / forum_name / level_id / is_sign
     *     forum/like     : id       / name       / level_id / favo_type（没有 is_sign）
     * 所以解析要用单独的 parseForumLike()。
     *
     * 不需要 uid：实测不带 uid 也能拿到本人的完整关注列表
     * （Android 版传 uid 是为了看**别人**的关注吧）。
     */
    QList<KeyValue> params;
    params.append(qMakePair(QString(QLatin1String("page_no")),
                            QString::number(page < 1 ? 1 : page)));
    params.append(qMakePair(QString(QLatin1String("page_size")),
                            QString::number(pageSize < 1 ? 200 : pageSize)));

    return postPc(QLatin1String("/c/f/forum/like"), params,
                  QLatin1String(kTagForumListLike));
}

int TbApiService::fetchForumPage(const QString &forumName, int page,
                                 int sortType, const QString &goodClassifyId)
{
    QList<KeyValue> params;
    params.append(qMakePair(QString(QLatin1String("kw")), forumName));
    params.append(qMakePair(QString(QLatin1String("pn")), QString::number(page < 1 ? 1 : page)));
    params.append(qMakePair(QString(QLatin1String("rn")), QString::number(m_pageSize)));
    params.append(qMakePair(QString(QLatin1String("sort_type")), QString::number(sortType)));
    params.append(qMakePair(QString(QLatin1String("q_type")), QString::fromUtf8("2")));
    params.append(qMakePair(QString(QLatin1String("st_type")), QString::fromUtf8("tb_forumlist")));
    params.append(qMakePair(QString(QLatin1String("with_group")), QString::fromUtf8("0")));
    params.append(qMakePair(QString(QLatin1String("scr_dip")), QString::number(m_screenDip)));
    params.append(qMakePair(QString(QLatin1String("scr_h")), QString::number(m_screenHeight)));
    params.append(qMakePair(QString(QLatin1String("scr_w")), QString::number(m_screenWidth)));

    if (!goodClassifyId.isEmpty()) {
        params.append(qMakePair(QString(QLatin1String("cid")), goodClassifyId));
        params.append(qMakePair(QString(QLatin1String("is_good")), QString::fromUtf8("1")));
    }

    // 吧页也需要 BDUSS 才能显示自己的等级/关注状态
    return post(QLatin1String("/c/f/frs/page"), params,
                QLatin1String(kTagForumPage));
}

int TbApiService::fetchThreadContent(const QString &threadId, int page,
                                     const QString &postId,
                                     bool onlyOriginalPoster, bool reverse)
{
    QList<KeyValue> params;
    params.append(qMakePair(QString(QLatin1String("kz")), threadId));

    // 给 pid 时表示定位到该楼层，此时 pn 传 0
    if (!postId.isEmpty()) {
        params.append(qMakePair(QString(QLatin1String("pid")), postId));
        params.append(qMakePair(QString(QLatin1String("pn")), QString::fromUtf8("0")));
    } else {
        params.append(qMakePair(QString(QLatin1String("pn")), QString::number(page < 1 ? 1 : page)));
    }

    params.append(qMakePair(QString(QLatin1String("lz")),
                            QString::number(onlyOriginalPoster ? 1 : 0)));
    params.append(qMakePair(QString(QLatin1String("r")),
                            QString::number(reverse ? 1 : 0)));
    params.append(qMakePair(QString(QLatin1String("rn")), QString::number(m_pageSize)));
    params.append(qMakePair(QString(QLatin1String("with_floor")), QString::fromUtf8("1")));
    // floor_rn 控制每层带几条楼中楼预览
    params.append(qMakePair(QString(QLatin1String("floor_rn")), QString::fromUtf8("3")));
    params.append(qMakePair(QString(QLatin1String("back")), QString::fromUtf8("0")));
    params.append(qMakePair(QString(QLatin1String("mark")), QString::fromUtf8("0")));
    params.append(qMakePair(QString(QLatin1String("st_type")), QString::fromUtf8("tb_frslist")));
    params.append(qMakePair(QString(QLatin1String("scr_dip")), QString::number(m_screenDip)));
    params.append(qMakePair(QString(QLatin1String("scr_h")), QString::number(m_screenHeight)));
    params.append(qMakePair(QString(QLatin1String("scr_w")), QString::number(m_screenWidth)));

    // 这个接口要求把 tid 放进请求头，缺失时会返回错误码
    QList<KeyValue> headers;
    headers.append(qMakePair(QString(QLatin1String("thread_id")), threadId));

    return post(QLatin1String("/c/f/pb/page"), params,
                QLatin1String(kTagThreadContent), headers);
}

int TbApiService::fetchSubFloors(const QString &threadId, const QString &postId,
                                 int page, const QString &subPostId)
{
    QList<KeyValue> params;
    params.append(qMakePair(QString(QLatin1String("kz")), threadId));
    params.append(qMakePair(QString(QLatin1String("pid")), postId));
    params.append(qMakePair(QString(QLatin1String("pn")),
                            QString::number(page < 1 ? 1 : page)));
    params.append(qMakePair(QString(QLatin1String("rn")), QString::fromUtf8("20")));
    if (!subPostId.isEmpty())
        params.append(qMakePair(QString(QLatin1String("spid")), subPostId));

    return post(QLatin1String("/c/f/pb/floor"), params,
                QLatin1String(kTagSubFloors));
}

/* ============================ 响应分发 ============================ */

void TbApiService::emitRequestFailure(int requestId, const QString &tag,
                                      const QString &message,
                                      const QVariantMap &detail)
{
    emit requestFailed(requestId, tag, message, detail);
}

void TbApiService::onHttpFinished(int requestId, const QString &tag,
                                  const tieba::TbJson &json,
                                  const tieba::TbHttpResponse &response)
{
    // 签到服务独立处理响应，包含仅供该服务使用的登录票据。
    if (tag.startsWith(QLatin1String("sign."))) return;
    const bool anchorResolved = m_postResolveRequests.remove(requestId) > 0;
    const bool login = tag == QLatin1String("account.login");
    const bool stream = tag.startsWith(QLatin1String("stream."));
    const bool userPage = tag.startsWith(QLatin1String("userpage."));
    const bool search = tag.startsWith(QLatin1String("search."));
    if (tag == QLatin1String(kTagForumListLike) || login || stream || userPage || search)
        qWarning("TbApi: %s request=%d http=%d networkError=%d code=%s keys=%s",
                 qPrintable(tag), requestId, response.httpStatus, response.networkError,
                 qPrintable(TbJson::str2(json, QLatin1String("error_code"), QLatin1String("no"))),
                 qPrintable(json.keys().join(QLatin1String(","))));
    // HTTP / 网络层失败
    if (!response.ok) {
        QVariantMap detail;
        detail.insert(QLatin1String("httpStatus"), response.httpStatus);
        detail.insert(QLatin1String("networkError"), response.networkError);
        detail.insert(QLatin1String("raw"),
                      login || stream || userPage || search ? QString() : QString::fromUtf8(response.body.left(1200).constData()));
        detail.insert(QLatin1String("url"), response.finalUrl);
        emitRequestFailure(requestId, tag, response.errorMessage, detail);
        return;
    }

    // 响应不是合法 JSON（多半是被拦截返回了 HTML）
    if (!json.isValid()) {
        QVariantMap detail;
        detail.insert(QLatin1String("httpStatus"), response.httpStatus);
        detail.insert(QLatin1String("raw"),
                      login || stream || userPage || search ? QString() : QString::fromUtf8(response.body.left(1200).constData()));
        detail.insert(QLatin1String("parseError"), json.errorMessage());
        detail.insert(QLatin1String("url"), response.finalUrl);
        emitRequestFailure(requestId, tag,
                           TbTr("E5938DE5BA94E4B88DE698AFE59088E6B395204A534F4E3A2531").arg(json.errorMessage()),
                           detail);
        return;
    }

    if (search) {
        const QVariantMap result = parseSearch(tag.mid(7), json);
        if (result.contains("error")) emitRequestFailure(requestId, tag, result.value("error").toString());
        else emit searchLoaded(requestId, result);
        return;
    }
    if (userPage) {
        const int separator = tag.indexOf(QLatin1Char('.'), 9);
        const QString kind = tag.mid(9, separator - 9);
        const QString uid = tag.mid(separator + 1);
        const QVariantMap result = parseUserPage(kind, json);
        if (result.contains("error")) emitRequestFailure(requestId, tag, result.value("error").toString());
        else emit userPageLoaded(requestId, uid, kind, result);
        return;
    }
    if (stream) {
        const QString kind = tag.mid(7);
        const QVariantMap result = parseStream(kind, json);
        if (result.contains("error")) emitRequestFailure(requestId, tag, result.value("error").toString());
        else emit streamLoaded(requestId, kind, result);
        return;
    }

    // 登录响应可能含令牌，只传递所需资料，不记录响应正文。
    if (login) {
        const QString code = TbJson::str(json, QLatin1String("error_code"));
        const TbJson user = json.member(QLatin1String("user"));
        const QString uid = TbJson::str(user, QLatin1String("id"));
        const bool verified = code == QLatin1String("0") && !uid.isEmpty() && uid != QLatin1String("0");
        qWarning("TbApi: account.login verified=%d userPresent=%d", verified, !uid.isEmpty());
        if (!verified) {
            QVariantMap detail;
            detail.insert(QLatin1String("code"), code);
            detail.insert(QLatin1String("httpStatus"), response.httpStatus);
            const QString message = code.isEmpty() || code == QLatin1String("0")
                ? QString::fromUtf8("登录接口未确认用户身份，请检查粘贴的是 BDUSS 值。")
                : QString::fromUtf8("登录校验失败，服务端错误码：") + code;
            emitRequestFailure(requestId, tag, message, detail);
            return;
        }
        QVariantMap profile;
        profile.insert(QLatin1String("uid"), uid);
        profile.insert(QLatin1String("userName"), TbJson::str(user, "name"));
        profile.insert(QLatin1String("nickName"), TbJson::str(user, "name_show"));
        profile.insert(QLatin1String("portrait"), TbJson::str(user, "portrait"));
        profile.insert(QLatin1String("tbs"), TbJson::str(json.member("anti"), "tbs"));
        emit loginVerified(requestId, profile);
        return;
    }

    // 调试模式下把服务端原文写进日志，便于真机排查
    if (m_client->isDebugEnabled()) {
        qWarning("TbApi [%s] http=%d body=%s", qPrintable(tag), response.httpStatus,
                 response.body.left(600).constData());
    }

    if (tag == QLatin1String(kTagForumList)) {
        TbForumListResult result = TbParsers::parseForumRecommend(json);
        result.raw = QString::fromUtf8(response.body.left(1500).constData());
        result.httpStatus = response.httpStatus;
        emit forumListLoaded(requestId, result);
    } else if (tag == QLatin1String(kTagForumListLike)) {
        // 完整关注列表：字段名与 forumrecommend 不同，必须用 parseForumLike
        TbForumListResult result = TbParsers::parseForumLike(json);
        result.raw = QString::fromUtf8(response.body.left(1500).constData());
        result.httpStatus = response.httpStatus;
        emit forumListLoaded(requestId, result);
    } else if (tag == QLatin1String(kTagSubFloors)) {
        TbPcSubFloorResult result = TbPcParsers::parseSubFloors(json);
        result.raw = QString::fromUtf8(response.body.left(1500).constData());
        result.httpStatus = response.httpStatus;
        emit subFloorsLoaded(requestId, result);

    /* ---------- PC 端 ---------- */
    } else if (tag == QLatin1String(kTagForumPagePc)) {
        TbPcForumPageResult result = TbPcParsers::parseForumPage(json);
        result.raw = QString::fromUtf8(response.body.left(1500).constData());
        result.httpStatus = response.httpStatus;
        // PC 通道探测：只要拿到合法响应（哪怕业务错误码非 0）就说明通道可用
        if (!m_pcProbed) {
            m_pcProbed = true;
            m_pcUsable = result.errorCode.isEmpty()
                         || result.errorCode == QLatin1String("0");
            emit channelProbed(m_pcUsable);
        }
        emit forumPageLoaded(requestId, result);
    } else if (tag == QLatin1String(kTagThreadPc)) {
        TbPcThreadResult result;
        if (anchorResolved) {
            const TbThreadContentResult mobile = TbParsers::parseThreadContent(json);
            result.posts = mobile.posts; result.threadInfo = mobile.threadInfo;
            result.pageInfo = mobile.pageInfo; result.errorCode = mobile.errorCode;
            result.errorMessage = mobile.errorMessage; result.anchorResolved = true;
        } else result = TbPcParsers::parseThread(json);
        result.raw = QString::fromUtf8(response.body.left(1500).constData());
        result.httpStatus = response.httpStatus;
        emit threadContentLoaded(requestId, result);
    } else if (tag == QLatin1String(kTagChannelProbe)) {
        const TbPcForumPageResult result = TbPcParsers::parseForumPage(json);
        m_pcProbed = true;
        m_pcUsable = result.errorCode.isEmpty()
                     || result.errorCode == QLatin1String("0");
        emit channelProbed(m_pcUsable);
    } else {
        emitRequestFailure(requestId, tag,
                           TbTr("E69CAAE79FA5E79A84E8AFB7E6B182E6A087E7ADBE3A2531").arg(tag));
    }
}

} // namespace tieba
