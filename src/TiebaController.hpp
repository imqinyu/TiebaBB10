/*
 * TiebaController - 逻辑层与 QML 之间的唯一接口
 *
 * 设计约定（重要，换 UI 时请遵守）：
 *   - QML 只通过这个对象干活，不直接碰网络和解析；
 *   - 所有「列表」都是 bb::cascades::ArrayDataModel，列表项是 QVariantMap，
 *     QML 里在 ListItemComponent 中直接写 ListItemData.<key>；
 *   - 所有「取数据」都是异步的：调用 loadXxx() 后监听 xxxChanged 信号，
 *     或用 loading / errorMessage 属性驱动界面。
 *
 * 暴露给 QML 的属性：
 *   forums      我关注的吧         （ArrayDataModel，项字段见 TbForum::toVariantMap）
 *   threads     吧页贴子列表       （ArrayDataModel，项字段见 TbThread::toVariantMap）
 *   posts       当前贴子的楼层     （ArrayDataModel，项字段见 TbPost::toVariantMap）
 *   subPosts    当前展开的楼中楼   （ArrayDataModel，同上）
 *   forumInfo   当前吧信息         （QVariantMap）
 *   threadInfo  当前主题贴信息     （QVariantMap）
 *   pageInfo    当前分页信息       （QVariantMap）
 *   loading / errorMessage / hasMore / currentForumName / currentThreadId
 */

#ifndef TIEBA_CONTROLLER_HPP
#define TIEBA_CONTROLLER_HPP

#include <QObject>
#include <QSet>
#include <QMap>
#include <QStringList>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class QTimer;
namespace bb { namespace system { class SystemToast; class InvokeManager; } }
namespace bb { namespace system { class InvokeTargetReply; } }

#include "api/TbApi.hpp"
#include "model/TbItems.hpp"
#include "model/TbParsers.hpp"

namespace bb {
namespace cascades {
class ArrayDataModel;
}
}

namespace tieba {
class TbHttpClient;
class TbSession;
class TbCookieStore;
class TbImageCache;
class TbThreadLibrary;
class TbFavoriteService;
class TbUserDataCache;
}

class TiebaController : public QObject
{
    Q_OBJECT

    /* ---- 列表数据 ---- */
    /* ---- 列表数据 ----
     *
     * 注意：这里必须是 bb::cascades::ArrayDataModel，不能是 Qt 的
     * QAbstractListModel —— Cascades 的 ListView 的 dataModel 属性类型是
     * bb::cascades::DataModel*，整个 Cascades 头目录里没有 QAbstractItemModel，
     * 传错类型会直接黑屏。
     *
     * 列表项是 QVariantMap，QML 里用 ListItemData.<key> 取字段。
     */
    Q_PROPERTY(bb::cascades::ArrayDataModel* forums READ forums NOTIFY forumsChanged)
    Q_PROPERTY(bb::cascades::ArrayDataModel* threads READ threads NOTIFY threadsChanged)
    Q_PROPERTY(bb::cascades::ArrayDataModel* posts READ posts NOTIFY postsChanged)
    Q_PROPERTY(bb::cascades::ArrayDataModel* subPosts READ subPosts NOTIFY subPostsChanged)
    Q_PROPERTY(int uiDataVersion READ uiDataVersion NOTIFY uiDataChanged)
    Q_PROPERTY(int streamVersion READ streamVersion NOTIFY streamsChanged)
    Q_PROPERTY(int userPageVersion READ userPageVersion NOTIFY userPagesChanged)
    Q_PROPERTY(bb::cascades::ArrayDataModel* followedFeed READ followedFeed NOTIFY streamsChanged)
    Q_PROPERTY(bb::cascades::ArrayDataModel* recommendedFeed READ recommendedFeed NOTIFY streamsChanged)
    Q_PROPERTY(bb::cascades::ArrayDataModel* hotFeed READ hotFeed NOTIFY streamsChanged)
    Q_PROPERTY(bb::cascades::ArrayDataModel* replyNotifications READ replyNotifications NOTIFY streamsChanged)
    Q_PROPERTY(bb::cascades::ArrayDataModel* mentionNotifications READ mentionNotifications NOTIFY streamsChanged)

    /* ---- 详情对象（QVariantMap，QML 里 tieba.forumInfo.name 这样取） ---- */
    Q_PROPERTY(QVariantMap forumInfo READ forumInfo NOTIFY forumInfoChanged)
    /*!
     * 吧信息版本号，每收到一次新数据 +1。
     *
     * ★ 为什么需要它：QML **监听不到 context property（tieba）的信号**
     *   （真机实测：写 onForumInfoChanged 会 "Cannot assign to non-existent
     *    property"，因为那是 tieba 的信号，不是页面的）。
     *
     *   所以用"属性 + 版本号"的办法做响应式：
     *       property variant detail: (tieba.forumInfoVersion,
     *                                 tieba.currentForumDetail())
     *   逗号表达式里引用了 forumInfoVersion，C++ 一 emit forumInfoChanged
     *   这个绑定就会重新求值 —— 效果等价于监听信号。
     *
     *   背景（吧详情空白的原因）：原来是在 onCreationCompleted 里取一次快照，
     *   但"先请求、后推页"是异步的，页面创建时数据还没到 -> 永远是空的。
     *   这是竞态条件，必须做成响应式。
     */
    Q_PROPERTY(int forumInfoVersion READ forumInfoVersion NOTIFY forumInfoChanged)
    Q_PROPERTY(QVariantMap threadInfo READ threadInfo NOTIFY threadInfoChanged)
    Q_PROPERTY(QVariantMap pageInfo READ pageInfo NOTIFY pageInfoChanged)

    /* ---- 状态 ---- */
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    /*!
     * 上一次错误的详细信息（QVariantMap），界面可以直接显示来排查接口问题：
     *   code / message / raw / httpStatus / networkError / url / tag
     * raw 是服务端原始响应片段，接口返回 error_code 时这里就是它的原话。
     */
Q_PROPERTY(QVariantMap errorDetail READ errorDetail NOTIFY errorDetailChanged)
    /*!
     * 最近一次成功响应的原始 JSON（前 2000 字符）。
     * 成功但显示不对时，看这个就知道服务端到底返回了什么。
     */
    Q_PROPERTY(QString lastRaw READ lastRaw NOTIFY lastRawChanged)
    Q_PROPERTY(QString currentForumName READ currentForumName NOTIFY currentForumNameChanged)
    Q_PROPERTY(QString currentThreadId READ currentThreadId NOTIFY currentThreadIdChanged)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY hasMoreChanged)

    /* ---- 登录态（转发自 TbSession） ---- */
    Q_PROPERTY(bool loggedIn READ loggedIn NOTIFY loginChanged)
    Q_PROPERTY(QString nickName READ nickName NOTIFY loginChanged)
    Q_PROPERTY(QString lastLoginError READ lastLoginError NOTIFY lastLoginErrorChanged)

    /* ==================================================================
     *  多账号（账号管理页 AccountManagePage.qml 用）
     *
     *  仿 NeteaseMusic 那一套：
     *    - 账号列表与当前账号都存在 TbSession（QSettings，应用沙箱）
     *    - 给 QML 的**只有打码后的 masked**，明文 BDUSS 只走
     *      accountCredential(id)（"复制凭据"那一下），不进模型、不进日志
     *    - 切换账号 = 换掉 TbSession 里的凭证，之后的请求自动用新的
     *      （TbApiService 每次发请求时才去 session 取 BDUSS）
     * ================================================================== */
    Q_PROPERTY(QVariantMap currentAccount READ currentAccount NOTIFY accountsChanged)
    /*! 除当前账号以外的已保存账号（字段见 rebuildAccounts 的说明） */
    Q_PROPERTY(bb::cascades::ArrayDataModel *otherAccounts READ otherAccounts
               NOTIFY accountsChanged)
    /*! 已保存的其它账号条数（空状态提示用；ArrayDataModel.size() 在 QML 里不保险） */
    Q_PROPERTY(int otherAccountCount READ otherAccountCount NOTIFY accountsChanged)
    Q_PROPERTY(int accountsVersion READ accountsVersion NOTIFY accountsChanged)

    /*!
     * 子页面请求主界面推/弹页（"新增账号"按钮用）。
     *
     * ★ 为什么绕一圈：账号管理页是独立页，拿不到 main.qml 的 NavigationPane
     *   （NeteaseMusic 踩过：子页里存 nav 对象会报 "Unable to set property"）。
     *   所以子页调 requestOpenLogin()，C++ 攒一下再通知主界面，由主界面自己 push。
     *   kind 取值："login"（推登录页）/ "loginDone"（弹掉登录页）。
     */
    Q_PROPERTY(QString openRequestKind READ openRequestKind NOTIFY openRequestChanged)
    Q_PROPERTY(int openRequestVersion READ openRequestVersion NOTIFY openRequestChanged)

    /* ==================================================================
     *  设置（设置页 SettingsPage.qml 用）
     *
     *  ★ 存哪里：和吧列表缓存同一个 config 目录（settings.ini，QSettings）。
     *    QSettings 属于 QtCore，Cascades 应用（QT -= gui）也能用；
     *    QDesktopServices（QtGui）则用不了 —— 那条路在设备上编不过
     *    （见 forumCacheFile() 里的说明）。
     *
     *  ★ 主题的值就是 Cascades 的 VisualStyle::Type：
     *      VisualStyle.Bright = 1（亮色，默认）
     *      VisualStyle.Dark   = 2（暗色）
     *    QML 那边直接 `Application.themeSupport.setVisualStyle(tieba.theme)`，
     *    所以这里存枚举值而不是自己另编一套数字，避免两处对不上。
     * ================================================================== */
    Q_PROPERTY(int theme READ theme WRITE setTheme NOTIFY settingsChanged)
    /*! 图片缓存上限（MB）。设置页给的是 512 / 1024 / 2048。 */
    Q_PROPERTY(int cacheLimitMb READ cacheLimitMb WRITE setCacheLimitMb NOTIFY settingsChanged)
    /*! 隐藏视频贴（吧页面的视频贴不显示）—— 对齐安卓版 blockVideo */
    Q_PROPERTY(bool hideVideoThreads READ hideVideoThreads WRITE setHideVideoThreads
               NOTIFY settingsChanged)
    /*! 推荐页只显示已关注的吧 —— 对齐安卓版 showFollowedOnly */
    Q_PROPERTY(bool showFollowedOnly READ showFollowedOnly WRITE setShowFollowedOnly
               NOTIFY settingsChanged)

    /* ---- 图片缓存占用（设置页"缓存已使用限额的 X%"用） ---- */
    /*! 已用占上限的百分比（0~100；上限不限时为 0） */
    Q_PROPERTY(int cacheUsagePercent READ cacheUsagePercent NOTIFY cacheUsageChanged)
    /*! 一句话说明，例如 "已用 12.3 MB / 512 MB（2%）" */
    Q_PROPERTY(QString cacheUsageText READ cacheUsageText NOTIFY cacheUsageChanged)

public:
    explicit TiebaController(QObject *parent = 0, const QString &dataDirectory = QString());
    virtual ~TiebaController();
    tieba::TbApiService *apiClient() const { return m_api; }
    tieba::TbHttpClient *httpClient() const { return m_httpClient; }
    tieba::TbSession *session() const { return m_session; }
    tieba::TbThreadLibrary *threadLibrary() const { return m_threadLibrary; }
    tieba::TbFavoriteService *favoriteService() const { return m_favoriteService; }
    Q_PROPERTY(int contextVersion READ contextVersion NOTIFY contextChanged)
    int contextVersion() const { return m_contextVersion; }
    Q_INVOKABLE QString contextTitle(const QVariantMap &row) const;
    Q_INVOKABLE QString contextSubtitle(const QVariantMap &row) const;
    Q_INVOKABLE void requestForumStats(const QVariantMap &row);
    Q_PROPERTY(int threadNavigationVersion READ threadNavigationVersion NOTIFY threadNavigationChanged)
    int threadNavigationVersion() const { return m_threadNavigationVersion; }
    Q_INVOKABLE QVariantMap threadNavigationState(const QString &threadId) const;
    Q_INVOKABLE void jumpThreadFloor(const QString &threadId, const QString &floor, bool onlyOp = false, bool reverse = false);
    Q_INVOKABLE void openThreadPost(const QString &threadId, const QString &postId, const QString &subPostId = QString(), bool onlyOp = false, bool reverse = false);

    bb::cascades::ArrayDataModel *forums() const;
    bb::cascades::ArrayDataModel *threads() const;
    bb::cascades::ArrayDataModel *posts() const;
    bb::cascades::ArrayDataModel *subPosts() const;
    int uiDataVersion() const { return m_uiDataVersion; }
    int streamVersion() const { return m_streamVersion; }
    bb::cascades::ArrayDataModel *followedFeed() const { return streamModel("follow"); }
    bb::cascades::ArrayDataModel *recommendedFeed() const { return streamModel("recommend"); }
    bb::cascades::ArrayDataModel *hotFeed() const { return streamModel("hot"); }
    bb::cascades::ArrayDataModel *replyNotifications() const { return streamModel("reply"); }
    bb::cascades::ArrayDataModel *mentionNotifications() const { return streamModel("mention"); }
    Q_INVOKABLE bb::cascades::ArrayDataModel *streamModel(const QString &kind) const;
    Q_INVOKABLE QVariantMap streamState(const QString &kind) const;
    Q_INVOKABLE void ensureStream(const QString &kind);
    Q_INVOKABLE void loadStream(const QString &kind, bool append = false);
    int userPageVersion() const { return m_userPageVersion; }
    Q_INVOKABLE QVariantMap userPageState(const QString &uid, const QString &kind) const;
    Q_INVOKABLE void loadUserPage(const QString &uid, const QString &kind, bool append = false);

    // UI 第一阶段只展示已加载数据；返回快照，子页面不复用主列表模型。
    Q_INVOKABLE QVariantList loadedUiItems(const QString &kind,
                                           const QString &query = QString(),
                                           const QString &userId = QString(),
                                           const QString &forumName = QString());
    Q_INVOKABLE QVariantMap uiUserProfile(const QVariantMap &row, bool own = false);

    QVariantMap forumInfo() const { return m_forumInfo; }
    int forumInfoVersion() const { return m_forumInfoVersion; }
    QVariantMap threadInfo() const { return m_threadInfo; }
    QVariantMap pageInfo() const { return m_pageInfo; }

    bool loading() const { return m_loading; }
QString errorMessage() const { return m_errorMessage; }
QVariantMap errorDetail() const { return m_errorDetail; }
    QString lastRaw() const { return m_lastRaw; }
    QString currentForumName() const { return m_currentForumName; }
    QString currentThreadId() const { return m_currentThreadId; }
    bool hasMore() const { return m_hasMore; }

    bool loggedIn() const;
    QString nickName() const;

    /*! 最近一次登录失败的原因 */
    QString lastLoginError() const { return m_lastLoginError; }

    /* ==================== 多账号 ==================== */

    QVariantMap currentAccount() const;
    Q_INVOKABLE QVariantMap currentAccountUi() const;
    bb::cascades::ArrayDataModel *otherAccounts() const { return m_otherAccounts; }
    int otherAccountCount() const;
    int accountsVersion() const { return m_accountsVersion; }

    QString openRequestKind() const { return m_openRequestKind; }
    int openRequestVersion() const { return m_openRequestVersion; }

    /* ==================== 设置 ==================== */

    int theme() const { return m_theme; }
    /*! 改主题并落盘。值用 Cascades 的 VisualStyle::Type（1=亮 / 2=暗）。 */
    void setTheme(int value);
    int cacheLimitMb() const { return m_cacheLimitMb; }
    void setCacheLimitMb(int value);
    bool hideVideoThreads() const { return m_hideVideoThreads; }
    void setHideVideoThreads(bool value);
    bool showFollowedOnly() const { return m_showFollowedOnly; }
    void setShowFollowedOnly(bool value);

    int cacheUsagePercent() const;
    QString cacheUsageText() const;

    /*! 重新统计一次图片缓存占用并发信号（设置页打开时调一下） */
    Q_INVOKABLE void refreshCacheUsage();
    /*! 清掉所有已下载的图片缓存（设置页"清除缓存"） */
    Q_INVOKABLE void clearImageCache();

    /* ------------------------------------------------------------------
     * 以下都是 QML 可调用的入口
     * ------------------------------------------------------------------ */

    /*! 从磁盘恢复登录态与设置（应用启动时调用一次） */
    Q_INVOKABLE void init();

    /*!
     * 手动粘贴登录。
     * bdussOrCookie 可以是：
     *   - 纯 BDUSS 值
     *   - "BDUSS=xxx; STOKEN=yyy" 整段 cookie
     * 成功返回 true，失败返回 false 并把原因写进 lastLoginError。
     */
    Q_INVOKABLE bool login(const QString &bdussOrCookie, const QString &stoken = QString());

    /*!
     * 退出登录。
     *
     * ★ 多账号之后语义变了：**只清当前登录态**，账号留在列表里 ——
     *   退出后原账号会出现在账号管理页的「已登录」里，点一下就能登回来。
     *   真要抹掉一个账号走 removeAccount()。
     */
    Q_INVOKABLE void logout();

    /* ==================== 多账号 ==================== */

    /*!
     * 切换到某个已保存账号。
     *
     * 会先把上一个账号的数据清干净（吧列表 / 缓存 / 贴子 / 楼层），
     * 再重新拉新账号的吧列表 —— 否则界面上会顶着上一个账号的内容。
     */
    Q_INVOKABLE bool switchAccount(const QString &id);

    /*! 删除一个已保存账号（删的是当前账号时会连带退出登录） */
    Q_INVOKABLE bool removeAccount(const QString &id);

    /*!
     * 取某个账号的**明文** BDUSS（"复制凭据"用）。
     * ★ 只在 C++ → 剪贴板之间过一手，不进模型、不打印。
     */
    Q_INVOKABLE QString accountCredential(const QString &id) const;

    /*! 读系统剪贴板（text/plain）；没有文本时返回空串 */
    Q_INVOKABLE QString clipboardText() const;

    /*! 账号管理页的「新增账号」：请求主界面推登录页 */
    Q_INVOKABLE void requestOpenLogin();
    /*! 主界面处理完请求后调用，避免同一个请求被重复处理 */
    Q_INVOKABLE void clearOpenRequest();

    /*! 我关注的吧 */
    /*! 加载"我关注的吧"。
     *
     *  会先用本地缓存立刻显示（如果有且不过期），再在后台刷新一次；
     *  刷新成功后覆盖缓存。这样 App 一启动就能看到吧列表，不用干等网络。
     */
    Q_INVOKABLE void loadForums();

    /*!
     * 强制从网络刷新"我关注的吧"（忽略缓存新鲜度）。
     * 用于下拉刷新 / 刷新按钮。
     */
    Q_INVOKABLE void refreshForums();

    /*!
     * 当前吧的信息（吧详情页用）。
     *
     * 数据来自吧页接口的 forum 对象 —— 所以**必须先 loadForumPage 过**，
     * 否则是空 map。字段：
     *     name / avatarUrl / localAvatarPath / memberCountText /
     *     postCountText / threadCountText / slogan /
     *     userLevel / score / levelupScore / levelName / liked
     *
     * ★ 这里顺带把头像的本地路径也补上（localAvatarPath），
     *   因为 BB10 的 ImageView 不能加载网络图片 —— 见 imagePath 的说明。
     *   所以 QML 直接用 localAvatarPath，不要去拼远程地址。
     */
    Q_INVOKABLE QVariantMap currentForumDetail();

    /*!
     * 加载吧页。
     *
     * @param forumName 吧名
     * @param page      页码（从 1 开始）
     * @param sortType 排序方式，和安卓版 ForumSortType 对齐：
     *                     0 = 按回复时间（最新）★ 默认
     *                     1 = 按发帖时间
     *                     2 = 只看关注
     *                 ★「精华」**不是** sort_type，而是独立的 goodOnly
     *                   （安卓的定义：REPLY_TIME(0)/SEND_TIME(1)/ONLY_FOLLOWED(2)，
     *                    精华走 is_good 参数 —— 见 TbApi::fetchForumPagePc 的注释）
     * @param append    true 表示翻页追加，false 表示替换（下拉刷新/切筛选）
     * @param goodOnly  true 时只看精品（is_good=1），与排序无关
     */
    Q_INVOKABLE void loadForumPage(const QString &forumName, int page = 1,
                                   int sortType = 0, bool append = false,
                                   bool goodOnly = false);

    /*!
     * 加载贴子楼层。
     * @param threadId 主题贴 id
     * @param page 页码（从 1 开始）
     * @param onlyOriginalPoster 只看楼主
     * @param reverse 倒序
     * @param append 追加还是替换
     */
    Q_INVOKABLE void loadThread(const QString &threadId, int page = 1,
                                bool onlyOriginalPoster = false,
                                bool reverse = false, bool append = false);

    /*! 加载某层的楼中楼 */
    Q_INVOKABLE void loadSubPosts(const QString &threadId, const QString &postId,
                                  int page = 1);

    /*! 清空错误信息（UI 关闭提示条时调用） */
    Q_INVOKABLE void clearError();

    /*! 打开/关闭接口调试模式（会记录请求与响应原文） */
    Q_INVOKABLE void setDebugEnabled(bool enabled);
    Q_INVOKABLE bool debugEnabled() const;
    /*! 取调试日志（最新在前），用于在界面上直接排查接口问题 */
    Q_INVOKABLE QString debugLog() const;
    Q_INVOKABLE void clearDebugLog();

    /*! 当前客户端版本号（出问题时可切换接口版本，见 README） */
    Q_INVOKABLE QString clientVersion() const;
    Q_INVOKABLE void setClientVersion(const QString &version);

    /*!
     * 用系统浏览器打开外链或帖子网页。
     * 走 BB10 的 Invocation 框架，失败时返回 false。
     */
    Q_INVOKABLE bool openUrl(const QString &url);

    /*!
     * 把文本放进系统剪贴板（长按菜单的"复制"用）。
     *
     * 用 bb::system::Clipboard，MIME 类型 "text/plain"。
     * 这是**跨应用**剪贴板，所以粘到别的地方也能用。
     */
    Q_INVOKABLE bool copyText(const QString &text);

    /*!
     * 弹一个系统 Toast 提示（复制成功之类的轻提示）。
     *
     * 用 bb::system::SystemToast。为什么需要它：像"复制链接"这种操作，
     * 成功与否界面上完全看不出来 —— 用户按了没反应会以为坏了。
     *
     * @param body     提示文字
     * @param position 0=中间 1=顶部 2=底部（默认底部，不挡内容）
     */
    Q_INVOKABLE void showToast(const QString &body, int position = 2);

    /*!
     * 收藏贴子。
     *
     * ★ 目前是**占位实现**：贴吧收藏要 tbs（一次性签名）和专门的接口，
     *   还没做。这里只把调用记进日志并返回 false，让界面能给出提示 ——
     *   比"按了没反应"清楚。
     *   等 tbs 那条链路做好，把这里换成真实请求即可。
     */
    Q_INVOKABLE bool favoriteThread(const QString &threadId);

    /*!
     * 打开系统图片查看 card；失败时提示用户，不打开浏览器。
     * @param remoteImageUrl 远程图片地址，本地文件由 imagePath() 解析。
     * @param target 默认 sys.pictures.card.previewer。
     */
    Q_INVOKABLE bool openImage(const QString &remoteImageUrl,
                               const QString &target = QString());
    Q_INVOKABLE int credentialInputLength(const QString &input) const;

    /* ======================================================================
     *  图片本地缓存
     *
     *  ★ 为什么需要：
     *    BB10 的 Cascades 图片加载器**不支持任何网络方案**。真机日志实测：
     *        "Unsupported scheme (http)  used in url (...). Image loading aborted."
     *        "Unsupported scheme (https) used in url (...). Image loading aborted."
     *    所以 ImageView.imageSource 只能吃本地路径。图片必须在 C++ 里下载。
     *    （官方 imageloader 示例也是这个做法。）
     * ====================================================================== */

    /*!
     * 取图片的本地缓存路径；未下载完返回空串并触发下载。
     * 下载完成后会发 imageCacheChanged()，QML 重新求值绑定即可显示。
     */
    Q_INVOKABLE QString imagePath(const QString &url);

    /*!
     * 缓存版本号。QML 的绑定里只要引用它，缓存有更新时绑定就会被重新求值。
     * （Cascades 没法通知"某个函数返回值变了"，所以用一个属性当触发器。）
     */
    Q_PROPERTY(int imageCacheVersion READ imageCacheVersion NOTIFY imageCacheChanged)
    int imageCacheVersion() const { return m_imageCacheVersion; }

    /*! 图片缓存诊断信息（缓存目录 / 已缓存数 / 版本号） */
    Q_INVOKABLE QString imageDiag() const;

    /*!
     * 打开图片下载的详细日志。
     * 排查"图片不显示"时打开，控制台会打出每一步进展与失败原因。
     */
    Q_INVOKABLE void setImageDebug(bool on);

    /*!
     * 把楼层展开成列表项：正文一项 + 每张图各一项。
     *
     * ★ 为什么需要它（多图显示的关键）：
     *   Cascades 的 ListItemComponent 里**没法遍历数组** —— 没有 Repeater，
     *   在 item 里再套 ListView 又会崩。所以"一个楼层多张图"没法在 QML 里循环，
     *   必须在这里把楼层拆成多个模型项，QML 那边一项永远只画一张图。
     *
     * 顺带会把图片的本地缓存路径写进 localImagePath，并触发未缓存图片的下载。
     *
     * 公开出来有两个原因：一是测试要断言拆分规则，二是你改 UI 时如果想自己
     * 控制展开方式，可以直接在 QML/C++ 里调用它。
     */
    Q_INVOKABLE void expandImageItems(const QVariantList &posts, QVariantList &out);

signals:
    void threadNavigationChanged();
    void forumsChanged();
    void threadsChanged();
    void postsChanged();
    void subPostsChanged();
    void uiDataChanged();
    void forumInfoChanged();
    void threadInfoChanged();
    void pageInfoChanged();
    void loadingChanged();
    void errorMessageChanged();
    void errorDetailChanged();
    void lastRawChanged();
    void currentForumNameChanged();
    void currentThreadIdChanged();
    void hasMoreChanged();
    void loginChanged();
    void lastLoginErrorChanged();

    /*! 任一设置项被改写（QML 里不用监听它，设置页自己就是写入方） */
    void settingsChanged();
    /*! 图片缓存占用变了（下载完成 / 清理之后） */
    void cacheUsageChanged();
    /*! 账号列表 / 当前账号变了（账号管理页据此刷新） */
    void accountsChanged();
    /*! 子页请求主界面推/弹页（见 openRequestKind 的说明） */
    void openRequestChanged();

    /*! 一次业务请求结束（成功或失败都会发），QML 可用来收尾动画 */
    void requestFinished(const QString &what, bool success);
    void streamsChanged();
    void userPagesChanged();
    void contextChanged();

    /*! 有图片下载完成，QML 应重新求值 imagePath 绑定 */
    void imageCacheChanged();

private slots:
    void onFavoriteOperation(const QString &threadId, bool stored, bool success, const QString &message);
    void onLibraryStorageError(const QString &message);
    void onUserPageLoaded(int requestId, const QString &uid, const QString &kind, const QVariantMap &result);
    void onStreamLoaded(int requestId, const QString &kind, const QVariantMap &result);
    void onLoginVerified(int requestId, const QVariantMap &profile);
    void onUiDataChanged();
    void onForumListLoaded(int requestId, const tieba::TbForumListResult &result);
    void onForumPageLoaded(int requestId, const tieba::TbPcForumPageResult &result);
    void onThreadContentLoaded(int requestId, const tieba::TbPcThreadResult &result);
    void onSubFloorsLoaded(int requestId, const tieba::TbPcSubFloorResult &result);
    /*! 系统看图 card 的异步回复，失败时显示提示。 */
    void onImageInvokeFinished();

    void onRequestFailed(int requestId, const QString &tag, const QString &message,
                           const QVariantMap &detail);
    void onSessionChanged();
    /*! 账号列表变了 -> 重建给 QML 的账号模型（见 .cpp 的说明） */
    void rebuildAccounts();
    /*! 子页推/弹页请求：等菜单动画走完再通知 QML（见 .cpp 的说明） */
    void onOpenRequestTimeout();
    void onImageReady();
    /*! 延迟刷新：只 replace() 本地图片路径变化的项（见 .cpp 里的说明） */
    void refreshImagePaths();

private:
    void setLoading(bool value);
    void setError(const QString &message, const QVariantMap &detail = QVariantMap());

    /* ==================== 多账号：内部 ==================== */
    /*!
     * 换账号（登录 / 切换 / 退出 / 删掉当前账号）时清掉上一个账号的东西。
     * 不清的话界面上会顶着上一个账号的吧列表和内容。
     */
    void resetForAccountSwitch();
    /*!
     * 把吧页响应里的"当前登录用户"写进账号资料（uid / 昵称 / 头像 / tbs）。
     * 这是目前唯一能拿到"我是谁"的地方，见 TbPcForumPageResult::user 的说明。
     */
    void applyAccountFromResult(const tieba::TbPcForumPageResult &result);
    /*!
     * 需要且可能时，打一次吧页把"当前账号是谁"补出来。
     * 吧列表响应里没有用户信息，只有吧页响应里有（见 applyAccountFromResult）。
     */
    void maybeProbeAccountProfile();
    /*! 子页推页请求：攒一下再通知（见 onOpenRequestTimeout 的说明） */
    void scheduleOpenRequest();

    /* ==================== 设置：读写磁盘 ==================== */
    /*! 设置文件（config/settings.ini）所在目录；顺便把目录建出来 */
    QString configDir() const;
    void loadSettings();
    void saveSettings() const;
    /*! 把缓存上限告诉 TbImageCache（字节；<=0 表示不限制） */
    void applyCacheLimit();
    /*! 把解析结果里的错误码 + 服务端原文整理成 detail */
    QVariantMap buildApiErrorDetail(const QString &code, const QString &message,
                                    const QString &raw, int httpStatus,
                                    const QString &tag) const;
    void finishRequest(const QString &what, bool success);
    /*! 清空某个列表模型 */
    void clearModel(bb::cascades::ArrayDataModel *model);

    /* ==================== 吧列表本地缓存 ====================
     *
     * 为什么要缓存：
     *   "我关注的吧"每次启动都要等一次网络请求，App 打开先是空白。
     *   把上次结果存本地，启动时立刻显示，再后台刷新一次覆盖。
     *
     * 存哪：QDesktopServices::DataLocation/config（应用私有、可写、持久）。
     * 过期：默认 24 小时；过期也仍然先显示出来，只是照常去刷新。
     * ====================================================== */

    /*!
     * 用给定的吧列表填充模型。
     * 缓存与网络两条路径都走它，保证 localAvatarPath 等字段处理一致。
     */
    /*!
     * 开始拉取完整关注列表（逐页累加）。
     * /c/f/forum/like 一页约 50 条，靠 has_more 翻页 —— 见 onForumListLoaded。
     */
    void startForumPaging();

    void fillForumsModel(const QVariantList &forumMaps);

public:
    /* ================= 吧列表缓存（public：测试要驱动往返） ============
     *
     * 这几个方法本身没危险，放 public 是为了让
     * tests/test_forumcache.cpp 能完整验证"写进去再读出来"。
     * 缓存这条路很容易"看起来写了其实没生效"，值得直接测。
     * ================================================================ */

    /*! 缓存文件完整路径（必要时创建目录） */
    QString forumCacheFile() const;

    /*!
     * 从缓存加载吧列表。
     * @return 缓存存在且可用（已灌入模型）返回 true；无/损坏返回 false
     */
    bool loadForumsFromCache();

    /*! 把当前吧列表写入缓存 */
    void saveForumsToCache() const;

    /*! 丢弃缓存（退出登录时用） */
    void clearForumsCache() const;

    /* ================= 测试钩子 ================= */

    /*! 覆盖缓存目录（测试用；传空串恢复默认） */
    void setCacheDirForTest(const QString &dir, const QString &legacyConfigDir = QString());
    /*!
     * 把 TbSession 的存储指到指定 ini（测试用；传空串恢复默认）。
     *
     * ★ 为什么需要它：默认位置存着**用户真实的 BDUSS**，测试往那儿写会把
     *   开发机上已登录的账号冲掉。账号相关的测试必须先改道。
     */
    void setSessionFileForTest(const QString &path);
    int loginRequestIdForTest() const { return m_loginRequestId; }
    /*! 当前缓存文件路径（测试用） */
    QString forumCacheFileForTest() const;
    /*! 直接替换吧列表模型内容（测试用，跳过网络） */
    void setForumsModelForTest(const QVariantList &forumMaps);
    /*! 当前吧列表模型项数（测试用） */
    int forumsForTestCount() const;
    /*! 当前吧列表模型内容（测试用） */
    QVariantList forumsModelForTest() const;

private:
    /*! 测试用：覆盖缓存目录；空串表示用默认的 DataLocation */
    QString m_cacheDirOverride;
    QString m_legacyConfigDirForTest;

    /* ---- 设置项（见上面的 Q_PROPERTY） ---- */
    int m_theme;              /*!< VisualStyle::Type：1=亮色 2=暗色 */
    int m_cacheLimitMb;       /*!< 图片缓存上限（MB） */
    bool m_hideVideoThreads;  /*!< 隐藏视频贴 */
    bool m_showFollowedOnly;  /*!< 推荐页只显示已关注的吧 */

    tieba::TbCookieStore *m_cookieStore;
    tieba::TbHttpClient *m_httpClient;
    tieba::TbSession *m_session;
    tieba::TbApiService *m_api;

    bb::cascades::ArrayDataModel *m_forums;
    /*! 吧信息版本号（见 forumInfoVersion 的说明） */
    int m_forumInfoVersion;
    /*! 除当前账号外的已保存账号（账号管理页的「已登录」一段） */
    bb::cascades::ArrayDataModel *m_otherAccounts;
    int m_accountsVersion;
    /*! 子页请求推/弹的页面种类（见 openRequestKind） */
    QString m_openRequestKind;
    int m_openRequestVersion;
    QTimer *m_openRequestTimer;
    /*! 登录页还开着（校验成功后要通知主界面把它弹掉） */
    int m_loginRequestId;
    QString m_pendingLoginBduss;
    QString m_pendingLoginStoken;
    /*!
     * "只为了拿当前账号资料"发出的吧页请求 id，-1 表示没有。
     * 它的响应**不能**灌进贴子模型，见 onForumPageLoaded。
     */
    int m_profileProbeRequestId;
    bb::cascades::ArrayDataModel *m_threads;
    bb::cascades::ArrayDataModel *m_posts;
    bb::cascades::ArrayDataModel *m_subPosts;
    int m_uiDataVersion;
    struct StreamState {
        bool loaded, loading, hasMore, append;
        int page, requestedPage, requestId;
        QString error, cursor, requestUnix;
        QSet<QString> seen;
        QVariantList sourceRows;
        bool cached;
        StreamState() : loaded(false), loading(false), hasMore(false), append(false),
                        page(0), requestedPage(0), requestId(-1), cached(false) {}
    };
    QMap<QString, StreamState> m_streamStates;
    QMap<QString, bb::cascades::ArrayDataModel *> m_streamModels;
    int m_streamVersion;
    int m_userPageVersion;
    QMap<QString, QVariantMap> m_userPageStates;
    QMap<int, QString> m_userPageRequests;
    tieba::TbThreadLibrary *m_threadLibrary;
    tieba::TbFavoriteService *m_favoriteService;
    tieba::TbUserDataCache *m_userDataCache;
    QVariantList m_forumSourceRows;
    int m_threadNavigationVersion;
    QVariantMap m_threadNavigation;
    int m_jumpFloor, m_jumpLow, m_jumpHigh, m_threadTotalPages;
    QString m_jumpPostId, m_jumpSubPostId;
    QSet<int> m_jumpPages;
    bool m_threadOnlyOp;
    void requestThreadPage(const QString &threadId, int page, bool onlyOp, bool reverse, bool append, bool preserveJump);
    void updateStreamRows(const QString &kind);
    void updateForumRows();
    int m_contextVersion;
    QMap<int, QString> m_forumStatsRequests;
    QMap<QString, QString> m_forumMemberCounts;

    QVariantMap m_forumInfo;
    QVariantMap m_threadInfo;
    QVariantMap m_pageInfo;

    bool m_loading;
    QString m_errorMessage;
QVariantMap m_errorDetail;
    /*! 最近一次成功响应的原始正文（排查用） */
    QString m_lastRaw;
    QString m_lastLoginError;
    QString m_currentForumName;
    QString m_currentThreadId;
    bool m_hasMore;

    /*! 正在跟踪的在途请求 id，-1 表示没有 */
    int m_forumListRequestId;
    int m_forumPageRequestId;
    int m_threadRequestId;

    /*!
     * 当前在途请求是否为翻页追加。
     * 不能直接看 hasMore：那是上一次响应的分页标志，会误判。
     */
    bool m_pendingForumPageAppend;
    bool m_pendingThreadAppend;
    /*
     * 楼中楼翻页（同 m_pendingThreadAppend 的道理）。
     * 第 1 页替换、第 2 页起追加 —— 不区分的话滚到底翻页会把已有内容顶掉，
     * 表现是"滑到底突然没有楼中楼"。
     */
    bool m_pendingSubPostsAppend;
    /*! 楼中楼在途请求 id，用于丢弃过期响应 */
    int m_subPostsRequestId;

    /*! 记录页码，供状态显示 */
    int m_lastPage;

    /*
     * 吧列表分页累加器。
     *
     * 为什么需要：/c/f/forum/like 一页最多给 ~50 条（page_size 传 200 也只回 49~50），
     * 而关注数可能上百。所以控制器要**逐页请求、累加，最后一次填模型** ——
     * 否则界面会先闪一堆、又变、再变，体验很差。
     * （旧的 forumrecommend 接口有 99 条硬上限，压根没有分页，这是换接口的原因。）
     */
    QVariantList m_forumAccum;
    /*! 正在请求的页码（从 1 开始） */
    int m_forumPage;
    /*!
     * 已收进的吧 id，用于**跨页去重**。
     *
     * 为什么需要：实测 page_size=50 时，page_no=1 返回 50 条、page_no=2 返回 49 条，
     * 而两页的 has_more 都是 1 —— 说明服务端的页边界 / has_more 语义不完全可靠，
     * 页之间**可能重叠**。不去重列表里会出现重复的吧。
     */
    QSet<QString> m_forumSeenIds;
    /*! 一次刷新的总页数上限，防止服务端 has_more 异常时无限翻页 */
    static const int kForumMaxPages = 20;

    /*! 图片下载缓存（见 imagePath 的说明） */
    tieba::TbImageCache *m_imageCache;
    /*! 每有一张图下载完就 +1，用于触发 QML 绑定重新求值 */
    int m_imageCacheVersion;
    /*!
     * 是否已排队一次图片路径刷新。
     * 图片常同时下完好几张，用这个把多次刷新合并成一次，
     * 避免连续整表操作（那会导致 ListView 遍历时 use-after-free）。
     */
    bool m_imageRefreshPending;
    /*
     * ★★ 常驻的 Toast 对象（不是局部变量！）
     *
     * 踩过的坑：原来 showToast() 里写的是
     *     bb::system::SystemToast toast;   // 局部变量
     *     toast.show();
     * 函数一返回，toast 就析构了 —— 界面还没把提示画出来对象就没了，
     * 所以**什么都不会显示**。SystemToast 是异步的，对象必须活到显示完。
     *
     * 现在把它做成成员，整个 controller 生命周期内只创建一次，
     * 每次 setBody() + show() 复用。
     */
    bb::system::SystemToast *m_toast;
    /*
     * ★★ 常驻的 InvokeManager。
     *
     * 同样的问题：原来 openImage / openUrl 里都是
     *     bb::system::InvokeManager manager;  // 局部变量
     * Invocation 是异步的，manager 析构会让在途的 broker 请求失去宿主。
     * 真机日志显示请求"发出去了"但系统毫无反应 —— 生命周期就是嫌疑之一。
     */
    bb::system::InvokeManager *m_invokeManager;
    /*!
     * 图片路径刷新节流定时器（80ms）。
     * 图片接连下完时攒到一个窗口里刷新一次，避免每张图都全表扫一遍。
     */
    QTimer *m_imageRefreshTimer;
};

#endif // TIEBA_CONTROLLER_HPP
