/*
 * TbParsers - 把接口 JSON 翻译成展示层对象
 *
 * 单独拆出来的原因：
 *   1. 贴吧同一字段在不同接口/版本下类型会变（"1" / 1 / true），
 *      取值规则集中在一处才好维护；
 *   2. 这部分完全不需要网络，可以拿真实响应样本做离线测试。
 *
 * 所有 parse* 方法接收 TbJson 根节点，返回的对象所有权交给调用方。
 */

#ifndef TB_PARSERS_HPP
#define TB_PARSERS_HPP

#include <QList>
#include <QString>
#include <QVariantList>

#include "model/TbItems.hpp"
#include "util/TbJson.hpp"

namespace tieba {

/*! 解析结果：可能部分项为 0（字段缺失） */
struct TbForumListResult
{
    QList<TbForum *> forums;
    QString errorCode;
    QString errorMessage;
    /*! 服务端原始响应片段（排查接口用，界面可直接显示） */
    QString raw;
    int httpStatus;
    /*!
     * 是否还有下一页（/c/f/forum/like 的 has_more）。
     * 关注数可能超过一页，控制器靠这个决定要不要继续翻页。
     * forumrecommend 那个接口没有这个字段，恒为 false。
     */
    bool hasMore;

    TbForumListResult()
        : httpStatus(0)
        , hasMore(false)
    {
    }
};

struct TbForumPageResult
{
    QList<TbThread *> threads;
    TbForumInfo *forumInfo;     // 可能为 0
    TbPageInfo *pageInfo;       // 可能为 0
    QString tbs;                // 发帖/签到用，来自 anti.tbs
    QString errorCode;
    QString errorMessage;
    QString raw;                // 服务端原始响应片段
    int httpStatus;

    TbForumPageResult()
        : forumInfo(0)
        , pageInfo(0)
        , httpStatus(0)
    {
    }
};

struct TbThreadContentResult
{
    QList<TbPost *> posts;
    TbThreadInfo *threadInfo;   // 可能为 0
    TbPageInfo *pageInfo;       // 可能为 0
    QString tbs;
    QString errorCode;
    QString errorMessage;
    QString raw;
    int httpStatus;

    TbThreadContentResult()
        : threadInfo(0)
        , pageInfo(0)
        , httpStatus(0)
    {
    }
};

struct TbSubFloorResult
{
    QList<TbPost *> posts;
    TbPost *parentPost;         // 被回复的楼层，可能为 0
    TbPageInfo *pageInfo;       // 可能为 0
    QString errorCode;
    QString errorMessage;
    QString raw;
    int httpStatus;

    TbSubFloorResult()
        : parentPost(0)
        , pageInfo(0)
        , httpStatus(0)
    {
    }
};

class TbParsers
{
public:
    /* ---- 四个第一阶段的接口 ---- */

    static TbForumListResult parseForumRecommend(const TbJson &root);

    /*!
     * 解析 /c/f/forum/like（完整关注列表，带分页）。
     *
     * 和 parseForumRecommend 的**字段名不一样**，不能混用：
     *     forumrecommend : forum_id / forum_name / level_id / is_sign
     *     forum/like     : id       / name       / level_id（没有 is_sign）
     *
     * 结构也不一样：吧在 forum_list 下面，分两个数组
     *     forum_list.non-gconforum[]   普通吧
     *     forum_list.gconforum[]       "gcon" 类吧（游戏/动漫之类）
     * 两边都要收，否则会少几个吧。
     */
    static TbForumListResult parseForumLike(const TbJson &root);
    static TbForumPageResult parseForumPage(const TbJson &root);
    static TbThreadContentResult parseThreadContent(const TbJson &root);
    static TbSubFloorResult parseSubFloorList(const TbJson &root);

    /* ---- 可复用的片段解析 ---- */

    static TbUser *parseUser(const TbJson &node);
    /*! 从 user_list 里按 id 建索引（贴子的作者常常只给 author_id） */
    static QHash<QString, TbUser *> indexUsers(const TbJson &root);
    static QList<TbContentSeg *> parseContent(const TbJson &node);
    /*!
     * 把段落列表转成 QVariantList（每项是 QVariantMap）。
     * Cascades 的 ArrayDataModel 与 ListItemData 都吃 QVariantMap，
     * 所以交给 QML 的一律是这个形式，而不是 QObject*。
     */
    static QVariantList toVariantList(const QList<TbContentSeg *> &segments);
    /*! 把内容段落数组拼成纯文本 */
    static QString plainTextOf(const QList<TbContentSeg *> &segments);
    /*! 从 abstract / media 数组里挑出第一张图 */
    static QString firstImageOf(const TbJson &node);
    static TbPageInfo *parsePageInfo(const TbJson &node);

    /*!
     * 贴吧错误码判定。
     * 返回 true 表示响应体里带非 0 的 error_code。
     */
    static bool hasError(const TbJson &root, QString *code, QString *message);

    /*! 贴吧常见的错误码翻译成人话（便于直接显示给用户） */
    static QString describeErrorCode(const QString &code);
};

} // namespace tieba

#endif // TB_PARSERS_HPP
