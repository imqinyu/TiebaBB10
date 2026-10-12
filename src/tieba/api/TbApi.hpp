/*
 * TbApiService - 贴吧客户端接口封装（第一阶段：只做浏览链路）
 *
 * 覆盖的接口（全部是 JSON，走 https://c.tieba.baidu.com/）：
 * 登录独立使用 /c/s/login，确认 user.id 后再保存账号。
 *
 *   1. POST /c/f/forum/forumrecommend   我关注的吧列表
 *        like_forum=1&recommend=0&topic=0        （需登录）
 *
 *   2. POST /c/f/frs/page               吧页贴子列表
 *        kw=<吧名>&pn=<页码>&rn=30&sort_type=<0回复时间|1发帖时间|2精品>
 *        q_type=2&st_type=tb_forumlist&with_group=0&cid=&is_good=
 *        可选 scr_dip/scr_h/scr_w
 *
 *   3. POST /c/f/pb/page                 贴子楼层
 *        kz=<tid>&pn=<页码 或 0>&pid=<定位楼层 id>&lz=<1 只看楼主>
 *        r=<1 倒序>&rn=30&with_floor=1&floor_rn=3&back=0&mark=0
 *        st_type=tb_frslist&scr_dip/scr_h/scr_w
 *        另需请求头 thread_id: <tid>
 *
 *   4. POST /c/f/pb/floor                楼中楼
 *        kz=<tid>&pid=<楼层 id>&spid=<楼中楼 id>&pn=<页码>&rn=20
 *
 * 关于登录：BDUSS 既作为公共参数参与签名，也会额外放进 Cookie 头
 * （有些接口只在 cookie 里认 BDUSS）。未登录时也能浏览，只是
 * 「我关注的吧」等接口会返回错误码。
 */

#ifndef TB_API_HPP
#define TB_API_HPP

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QSet>

#include "model/TbParsers.hpp"
#include "model/TbPcParsers.hpp"
#include "net/TbHttpClient.hpp"

namespace tieba {

class TbSession;

class TbApiService : public QObject
{
    Q_OBJECT
public:
    explicit TbApiService(TbHttpClient *client, TbSession *session, QObject *parent = 0);
    virtual ~TbApiService();

    /*! 接口版本：不同版本走不同一代接口，出问题时可切换 */
    void setClientVersion(const QString &version);
    QString clientVersion() const { return m_clientVersion; }

    /*! 每页条数 */
    void setPageSize(int value);
    int pageSize() const { return m_pageSize; }

    /*! 屏幕信息（部分接口要求，缺失时用默认值） */
    void setScreenInfo(int dip, int height, int width);

    /*! 是否记录原始请求/响应（真机排查用） */
    void setDebugEnabled(bool enabled);
    bool isDebugEnabled() const;
    QStringList debugLog() const;

    /* ---- 业务接口 ---- */

    /*! TiebaLite 登录协议：bdusstoken=BDUSS|，无公共 BDUSS 参数。 */
    static TbHttpRequest buildLoginRequest(const QString &bduss, const QString &stoken);
    int verifyLogin(const QString &bduss, const QString &stoken);
    static QList<KeyValue> streamParams(const QString &kind, int page,
                                       const QString &cursor = QString(), const QString &requestUnix = QString());
    static QVariantMap parseStream(const QString &kind, const TbJson &json);
    int fetchStream(const QString &kind, int page, const QString &cursor, const QString &requestUnix);
    static QList<KeyValue> userPageParams(const QString &kind, const QString &uid, int page);
    static QVariantMap parseUserPage(const QString &kind, const TbJson &json);
    int fetchUserPage(const QString &uid, const QString &kind, int page = 1);
    static TbHttpRequest buildSearchRequest(const QString &query, const QString &kind,
                                           const QString &forum, int page);
    static QVariantMap parseSearch(const QString &kind, const TbJson &json);
    int fetchSearch(const QString &query, const QString &kind, const QString &forum, int page);

    /*! 我关注的吧（需登录） */
    int fetchForumList();

    /*!
     * "我关注的吧"完整列表（带分页）。
     *
     * 为什么要它：fetchForumList() 用的 forumrecommend 接口实测有 **99 条上限**
     * （返回 99 条就停，最后一条是 freebsd），关注更多的用户后面的吧加载不出来。
     * 这个走 /c/f/forum/like，支持 page_no/page_size，字段名也不同
     * （id/name 而不是 forum_id/forum_name），要用 parseForumLike() 解析。
     */
    int fetchForumListPaged(int page = 1, int pageSize = 200);

    /*!
     * 吧页贴子列表。
     * @param forumName 吧名
     * @param page 页码，从 1 开始
     * @param sortType 0=按回复时间 1=按发帖时间 2=只看精品
     * @param goodClassifyId 精品分类 id（sortType=2 时可选）
     */
    int fetchForumPage(const QString &forumName, int page = 1,
                       int sortType = 0, const QString &goodClassifyId = QString());

    /*!
     * 贴子楼层。
     * @param threadId 主题贴 id
     * @param page 页码，从 1 开始；与 postId 二选一
     * @param postId 定位到某个楼层（收藏/跳楼用），给定时 page 忽略
     * @param onlyOriginalPoster 只看楼主
     * @param reverse 倒序（从最后一页往前看）
     */
    int fetchThreadContent(const QString &threadId, int page = 1,
                           const QString &postId = QString(),
                           bool onlyOriginalPoster = false,
                           bool reverse = false);

    /*!
     * 楼中楼。
     * @param threadId 主题贴 id
     * @param postId 楼层 id
     * @param page 页码，从 1 开始
     * @param subPostId 定位到某条楼中楼
     */
    int fetchSubFloors(const QString &threadId, const QString &postId,
                       int page = 1, const QString &subPostId = QString());

    /*! 取消某个在途请求（比如快速切换吧页时） */
    void cancel(int requestId);

    /* ======================================================================
     *  PC 端接口 —— 2026-10 实测这是**唯一可用**的路径。
     *
     *  移动端 /c/f/frs/page、/c/f/forum/forumrecommend 已恒定返回
     *  error_code=110001（加任何参数都无效），详见 docs/接口调研结论.md。
     * ====================================================================== */

    /*! 吧页贴子列表（PC）：POST /c/f/frs/page_pc */
    /*!
     * 吧页贴子列表（PC 通道）。
     *
     * @param sortType 排序方式，**和安卓版 ForumSortType 对齐**：
     *                     0 = REPLY_TIME 按回复时间（= "最新"）
     *                     1 = SEND_TIME  按发帖时间
     *                     2 = ONLY_FOLLOWED
     *                 ★ 注意"精华"**不是** sort_type —— 它是独立的 is_good 参数，
     *                   见 goodOnly。
     * @param goodOnly true 时追加 is_good=1（只看精品）。
     */
    int fetchForumPagePc(const QString &forumName, int page = 1, int sortType = 0,
                         bool goodOnly = false);

    /*! 帖子楼层（PC）：POST /c/f/pb/page_pc */
    int fetchThreadContentPc(const QString &threadId, int page = 1,
                             bool onlyOriginalPoster = false, bool reverse = false,
                             const QString &postId = QString(), const QString &subPostId = QString());
    static QList<KeyValue> buildThreadContentPcParams(const QString &threadId, int page,
            bool onlyOriginalPoster, bool reverse, const QString &postId = QString(),
            const QString &subPostId = QString());
    static TbHttpRequest buildThreadPostRequest(const TbSession *session, const QString &threadId,
            const QString &postId, bool onlyOriginalPoster = false, bool reverse = false);

    /*! 楼中楼（PC）：POST /c/f/pb/floor（端点不变，换 PC 签名才可用） */
    int fetchSubFloorsPc(const QString &threadId, const QString &postId, int page = 1);

    /*!
     * 探测 PC 通道是否可用（用最小的请求打一次 frs/page_pc）。
     * 结果通过 channelProbed(bool usable) 通知。
     */
    int probeChannel();

signals:
    void loginVerified(int requestId, const QVariantMap &profile);
    void streamLoaded(int requestId, const QString &kind, const QVariantMap &result);
    void userPageLoaded(int requestId, const QString &uid, const QString &kind, const QVariantMap &result);
    void searchLoaded(int requestId, const QVariantMap &result);
    void forumListLoaded(int requestId, const tieba::TbForumListResult &result);
    void forumPageLoaded(int requestId, const tieba::TbPcForumPageResult &result);
    void threadContentLoaded(int requestId, const tieba::TbPcThreadResult &result);
    void subFloorsLoaded(int requestId, const tieba::TbPcSubFloorResult &result);

    /*! PC 通道探测结果 */
    void channelProbed(bool usable);

/*!
     * 请求层面失败（网络/HTTP/解析）或接口返回非 0 错误码。
     *
     * detail 里带排查用信息：
     *   code        接口 error_code（HTTP 失败时为空）
     *   message     已经翻译成人话的错误说明
     *   raw         服务端原始响应片段 / HTTP 响应片段
     *   httpStatus  HTTP 状态码（0 表示网络层就失败了）
     *   networkError QNetworkReply::NetworkError 数值
     */
    void requestFailed(int requestId, const QString &tag, const QString &message,
                       const QVariantMap &detail);

private slots:
    void onHttpFinished(int requestId, const QString &tag, const tieba::TbJson &json,
                        const tieba::TbHttpResponse &response);

private:
    /*! 组装并发出请求；返回请求 id */
    int post(const QString &path, const QList<KeyValue> &params,
             const QString &tag, const QList<KeyValue> &extraHeaders = QList<KeyValue>());

    /*! PC 端请求：换盐、换公共参数与 UA */
    int postPc(const QString &path, const QList<KeyValue> &params, const QString &tag);

    /*! 公共请求头：UA / client_type / cookie(ka=open; BDUSS) */
    QList<KeyValue> baseHeaders(bool forceLogin) const;

    /*! 请求体里的公共参数（含 BDUSS 与时间戳等） */
    QList<KeyValue> baseParams() const;

void emitRequestFailure(int requestId, const QString &tag, const QString &message,
                            const QVariantMap &detail = QVariantMap());

    TbHttpClient *m_client;
    TbSession *m_session;
    QString m_clientVersion;
    int m_pageSize;
    int m_screenDip;
    int m_screenHeight;
    int m_screenWidth;
    /*! PC 通道是否已探测过 / 探测结果 */
    bool m_pcProbed;
    bool m_pcUsable;
    QSet<int> m_postResolveRequests;
};

} // namespace tieba

#endif // TB_API_HPP
