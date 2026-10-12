/*
 * TbPcParsers - PC 端接口（*_pc）的解析器
 *
 * 为什么要单独一套：
 *   2026-10 实测确认移动端 /c/f/frs/page 恒定返回 110001，
 *   只有 PC 端接口可用。PC 接口返回的是**组件化**结构，
 *   与旧的扁平结构完全不同，所以解析逻辑另起一份，
 *   旧解析器保留（/c/f/forum/getforumlist 仍在用旧的移动端格式）。
 *
 * PC 吧页响应骨架：
 *   {
 *     "error_code": 0,
 *     "forum": { name, first_class, second_class, slogan, avatar,
 *                member_num, thread_num, user_level, level_id,
 *                good_classify[], managers[] },
 *     "page":  { current_page, total_page, total_count, has_more, page_size },
 *     "page_data": { "feed_list": [ ... ] }
 *   }
 *
 * feed_list 是多态数组，用 layout 区分：
 *   "frs_top_thread_card"  置顶卡片（本解析器跳过）
 *   "feed"                 普通贴子，内容在 feed.components[]
 *
 * components[] 用 component 字段区分：
 *   feed_head      作者、吧头衔、时间、头像
 *   feed_title     标题
 *   feed_abstract  摘要（含表情）
 *   feed_pic       图片
 *   feed_social    tid、回复数
 *
 * 帖子详情 /c/f/pb/page_pc 骨架：
 *   { "error_code":0, "thread":{...}, "post_list":[...], "user_list":[...], "page":{...} }
 */

#ifndef TB_PC_PARSERS_HPP
#define TB_PC_PARSERS_HPP

#include <QMetaType>
#include <QList>
#include <QString>
#include <QVariantList>

#include "model/TbItems.hpp"
#include "util/TbJson.hpp"

namespace tieba {

/*! 吧页列表结果（PC 版） */
struct TbPcForumPageResult
{
    QList<TbThread *> threads;
    TbForumInfo *forumInfo;     // 可能为 0
    TbPageInfo *pageInfo;       // 可能为 0
    /*
     * 当前登录用户（多账号用）。
     *
     * ★ 实测：/c/f/frs/page_pc 的响应里带 `user` 对象，
     *   里面有 id / name / name_show / portrait —— 正好是"我是谁"。
     *   可用于补充当前账号资料。登录另由 /c/s/login 的 Android
     *   客户端签名请求校验身份。
     *   可能为 0（未登录 / 字段缺失）。
     */
    TbUser *user;
    /*! anti.tbs：发帖/签到要用的一次性票据 */
    QString tbs;
    QString errorCode;
    QString errorMessage;
    QString raw;
    int httpStatus;

    TbPcForumPageResult()
        : forumInfo(0)
        , pageInfo(0)
        , user(0)
        , httpStatus(0)
    {
    }
};

/*! 帖子楼层结果（PC 版） */
struct TbPcThreadResult
{
    bool anchorResolved; // Mobile pid locator response; controller resolves floor before displaying PC page.
    QList<TbPost *> posts;
    TbThreadInfo *threadInfo;   // 可能为 0
    TbPageInfo *pageInfo;       // 可能为 0
    TbForumInfo *forumInfo;     // 可能为 0
    QString errorCode;
    QString errorMessage;
    QString raw;
    int httpStatus;

    TbPcThreadResult()
        : anchorResolved(false)
        , threadInfo(0)
        , pageInfo(0)
        , forumInfo(0)
        , httpStatus(0)
    {
    }
};

/*! 楼中楼结果（PC 版） */
struct TbPcSubFloorResult
{
    QList<TbPost *> posts;
    TbPageInfo *pageInfo;       // 可能为 0
    int subPostNum;             // 服务端报的总数
    QString errorCode;
    QString errorMessage;
    QString raw;
    int httpStatus;

    TbPcSubFloorResult()
        : pageInfo(0)
        , subPostNum(0)
        , httpStatus(0)
    {
    }
};

class TbPcParsers
{
public:
    /*! 吧页贴子列表：page_data.feed_list[] -> 扁平的 TbThread 列表 */
    static TbPcForumPageResult parseForumPage(const TbJson &root);

    /*!
     * 帖子楼层：first_floor + post_list[] + user_list
     *
     * ★ 1 楼**不在** post_list 里，而是单独的 first_floor 对象，
     *   必须单独解析并放在最前面，否则正文（1 楼）永远不显示。
     */
    static TbPcThreadResult parseThread(const TbJson &root);

    /*! 楼中楼：subpost_list[]（每条自带 author 对象） */
    static TbPcSubFloorResult parseSubFloors(const TbJson &root);

    /* ---- 组件级解析（可单独测试） ---- */

    /*! 单个 layout="feed" 的条目 -> TbThread；不是贴子则返回 0 */
    static TbThread *parseFeedThread(const TbJson &feedItem);

    /*! 从 components 数组里取指定 component 名的节点 */
    static TbJson componentOf(const TbJson &feed, const QString &name);

    /*!
     * 解析一层楼的正文 content[] -> QVariantList（喂给 TbPost::setContentSegments）。
     * 楼层、楼中楼、first_floor 三处字段名一致，所以抽成一个函数。
     */
    static QVariantList parseContentSegments(const TbJson &content);

    /*!
     * 从 content[] 里挑出第一张图的地址。
     * 图片字段实测是 origin_src / cdn_src（不是 origin_pic_url）。
     */
    static QString firstImageInContent(const TbJson &content);

    /*!
     * 把贴吧表情名（"滑稽"、"呵呵"…）映射成表情图地址。
     *
     * 贴吧正文里的表情是 `type: 2` 的段落，带一个 **c 字段 = 表情名字**，
     * 而不是一个现成的图片 URL。所以必须自己拼：
     *
     *     http://static.tieba.baidu.com/tb/editor/images/client/<id>.png
     *
     * 其中 id 是 image_emoticon<N>。名字->N 的对应表来自 Android 版
     * TiebaLite 的 EmoticonManager.DEFAULT_EMOTICON_MAPPING（那是权威表）。
     *
     * @return 拼好的 URL（已升成 https）；名字查不到时返回空串。
     */
    static QString emoticonUrl(const QString &name);

    /*!
     * 直接由表情 ID 拼地址（ID 形如 image_emoticon66）。
     *
     * 为什么要这个：实测贴吧的 type 2 段**自带 text = "image_emoticon66"**，
     * 比按名字查表可靠 —— 有些表情（"小乖"）根本不在名字表里。
     * 所以 parser 优先走这条，查表只作为回退。
     */
    static QString emoticonUrlById(const QString &id);

    /*!
     * 取文本组件里的纯文本。
     * 用于 feed_title.data / feed_abstract.data —— 每项可能是
     *   {"type":1,"text_info":{"text":"..."}}
     *   {"type":3,"emoji_info":{"c":"#(笑眼)"}}   表情
     */
    static QString textOfDataArray(const TbJson &dataArray);

    /*!
     * JSON 字符串二次解析。
     * feed_head.main_data[] 里吧头衔藏在 custom.data —— 它是一个
     * **被转义过的 JSON 字符串**，必须再 parse 一次才能拿到 level_name。
     */
    static QString nestedJsonValue(const QString &jsonText, const QString &key);
};

} // namespace tieba

/*
 * 这些结构体会通过信号传递（TbApiService -> TiebaController）。
 * 同线程直连本不需要元类型，但声明上更稳妥：一旦将来改成队列连接，
 * 没有 Q_DECLARE_METATYPE 会在运行时报
 *   "Cannot queue arguments of type ..."
 * 而且真机上一次意外崩溃排查成本很高，这种保险值得加。
 */
Q_DECLARE_METATYPE(tieba::TbPcForumPageResult)
Q_DECLARE_METATYPE(tieba::TbPcThreadResult)
Q_DECLARE_METATYPE(tieba::TbPcSubFloorResult)

#endif // TB_PC_PARSERS_HPP
