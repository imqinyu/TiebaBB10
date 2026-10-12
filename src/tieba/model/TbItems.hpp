/*
 * TbItems - 展示层数据对象
 *
 * 设计目标：UI 会重写，所以这里把「接口字段」翻译成「界面要用的字段」，
 * 并且每个属性都用 Q_PROPERTY 暴露，QML 里可以直接 xxx.authorName。
 *
 * 所有对象都是纯数据 + 只读属性；由 TbParsers 负责填充。
 * 时间等字段在解析时就格式化成字符串，避免 QML 里做时间运算。
 */

#ifndef TB_ITEMS_HPP
#define TB_ITEMS_HPP

#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include "util/TbString.hpp"

namespace tieba {
class TbJson;

/*!
 * 正文段落。
 *
 * 贴吧正文不是纯文本，而是「类型化段落数组」，类型定义（来自官方客户端）：
 *   0, 9, 27, 35, 40  纯文本
 *   1                 超链接（link 里是 URL）
 *   2                 表情（c 是表情名，text 是 [xx] 形式）
 *   3                 图片（src / bigCdnSrc / originSrc）
 *   4                 @某人（uid 是被 @ 的用户）
 *   5                 视频（link 是视频地址，src 是封面）
 *   10                语音（voiceMD5 / duringTime）
 *   20                图片（另一种来源）
 */
class TbContentSeg : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int type READ type CONSTANT)
    Q_PROPERTY(QString kind READ kind CONSTANT)
    Q_PROPERTY(QString text READ text CONSTANT)
    Q_PROPERTY(QString url READ url CONSTANT)
    Q_PROPERTY(QString thumbUrl READ thumbUrl CONSTANT)
    Q_PROPERTY(QString originUrl READ originUrl CONSTANT)
    Q_PROPERTY(QString uid READ uid CONSTANT)
    Q_PROPERTY(QString emoticonName READ emoticonName CONSTANT)
    Q_PROPERTY(int width READ width CONSTANT)
    Q_PROPERTY(int height READ height CONSTANT)
    Q_PROPERTY(int duration READ duration CONSTANT)

public:
    explicit TbContentSeg(QObject *parent = 0);

    int type() const { return m_type; }

    /*! 归一化类型：text / link / emoticon / image / user / video / voice / unknown */
    QString kind() const;

    QString text() const { return m_text; }
    QString url() const { return m_url; }
    QString thumbUrl() const { return m_thumbUrl; }
    QString originUrl() const { return m_originUrl; }
    QString uid() const { return m_uid; }
    QString emoticonName() const { return m_emoticonName; }
    int width() const { return m_width; }
    int height() const { return m_height; }
    int duration() const { return m_duration; }

    void setType(int value) { m_type = value; }
    void setText(const QString &value) { m_text = value; }
    void setUrl(const QString &value) { m_url = value; }
    void setThumbUrl(const QString &value) { m_thumbUrl = value; }
    void setOriginUrl(const QString &value) { m_originUrl = value; }
    void setUid(const QString &value) { m_uid = value; }
    void setEmoticonName(const QString &value) { m_emoticonName = value; }
    void setSize(int width, int height)
    {
        m_width = width;
        m_height = height;
    }
    void setDuration(int value) { m_duration = value; }

    /*! 转成 QVariantMap（给 Cascades 的 ArrayDataModel / ListItemData 用） */
    QVariantMap toVariantMap() const;

private:
    int m_type;
    QString m_text;
    QString m_url;
    QString m_thumbUrl;
    QString m_originUrl;
    QString m_uid;
    QString m_emoticonName;
    int m_width;
    int m_height;
    int m_duration;
};

/*! 用户（贴子里嵌的作者信息） */
class TbUser : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QString nameShow READ nameShow CONSTANT)
    Q_PROPERTY(QString displayName READ displayName CONSTANT)
    Q_PROPERTY(QString portrait READ portrait CONSTANT)
    Q_PROPERTY(QString avatarUrl READ avatarUrl CONSTANT)
    Q_PROPERTY(int level READ level CONSTANT)
    Q_PROPERTY(bool isBawu READ isBawu CONSTANT)
    Q_PROPERTY(QString bawuType READ bawuType CONSTANT)
    Q_PROPERTY(QString bawuLabel READ bawuLabel CONSTANT)
    Q_PROPERTY(QString ipAddress READ ipAddress CONSTANT)

public:
    explicit TbUser(QObject *parent = 0);

    QString id() const { return m_id; }
    QString name() const { return m_name; }
    QString nameShow() const { return m_nameShow; }
    /*! 优先显示昵称，没有则显示用户名 */
    QString displayName() const;
    QString portrait() const { return m_portrait; }
    /*! 已经拼成完整 URL 的头像地址 */
    QString avatarUrl() const { return m_avatarUrl; }
    int level() const { return m_level; }
    bool isBawu() const { return m_isBawu; }
    QString bawuType() const { return m_bawuType; }
    /*! 把 bawu_type 翻译成中文标签 */
    QString bawuLabel() const;
    QString ipAddress() const { return m_ipAddress; }

    void setId(const QString &value) { m_id = value; }
    void setName(const QString &value) { m_name = value; }
    void setNameShow(const QString &value) { m_nameShow = value; }
    void setPortrait(const QString &value) { m_portrait = value; }
    void setAvatarUrl(const QString &value) { m_avatarUrl = value; }
    void setLevel(int value) { m_level = value; }
    void setIsBawu(bool value) { m_isBawu = value; }
    void setBawuType(const QString &value) { m_bawuType = value; }
    void setIpAddress(const QString &value) { m_ipAddress = value; }

    QVariantMap toVariantMap() const;

private:
    QString m_id;
    QString m_name;
    QString m_nameShow;
    QString m_portrait;
    QString m_avatarUrl;
    int m_level;
    bool m_isBawu;
    QString m_bawuType;
    QString m_ipAddress;
};

/*! 贴子内的一个楼层回复 */
class TbPost : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(QString floor READ floor CONSTANT)
    Q_PROPERTY(int floorNumber READ floorNumber CONSTANT)
    Q_PROPERTY(QString timeText READ timeText CONSTANT)
    Q_PROPERTY(QString plainText READ plainText CONSTANT)
    Q_PROPERTY(tieba::TbUser* author READ author CONSTANT)
    Q_PROPERTY(int agreeCount READ agreeCount CONSTANT)
    Q_PROPERTY(int disagreeCount READ disagreeCount CONSTANT)
    Q_PROPERTY(int subPostCount READ subPostCount CONSTANT)
    Q_PROPERTY(bool isOriginalPoster READ isOriginalPoster CONSTANT)
    Q_PROPERTY(QVariantList contentSegments READ contentSegments CONSTANT)
    Q_PROPERTY(QVariantList subPosts READ subPosts CONSTANT)
    Q_PROPERTY(QVariantList subPostPreview READ subPostPreview CONSTANT)

    /* 作者的扁平化副本。
     * 为什么不全靠 author 这个对象 role：QML 列表里绑定 QObject* 类型的 role
     * 在某些情况下拿不到值（依赖元类型注册与绑定方式）。把最常用的字段直接
     * 摊到 TbPost 上，UI 就一定能取到，也省得写 author.displayName 这种链式绑定。 */
    Q_PROPERTY(QString authorName READ authorName CONSTANT)
    Q_PROPERTY(QString authorAvatarUrl READ authorAvatarUrl CONSTANT)
    Q_PROPERTY(int authorLevel READ authorLevel CONSTANT)
    Q_PROPERTY(bool authorIsBawu READ authorIsBawu CONSTANT)
    Q_PROPERTY(QString authorLabel READ authorLabel CONSTANT)
    Q_PROPERTY(QString authorIpAddress READ authorIpAddress CONSTANT)

public:
    explicit TbPost(QObject *parent = 0);

    QString id() const { return m_id; }
    QString floor() const { return m_floor; }
    int floorNumber() const { return m_floor.isEmpty() ? 0 : m_floor.toInt(); }
    QString timeText() const { return m_timeText; }
    /*! 所有文本段落拼起来的纯文本（复制、搜索用） */
    QString plainText() const { return m_plainText; }
    TbUser *author() const { return m_author; }
    int agreeCount() const { return m_agreeCount; }
    int disagreeCount() const { return m_disagreeCount; }
    int subPostCount() const { return m_subPostCount; }
    bool isOriginalPoster() const { return m_isOriginalPoster; }
    QVariantList contentSegments() const { return m_contentSegments; }
    /*!
     * 楼中楼。
     *
     * 对 QML 暴露的是 QVariantList<QVariantMap>（和 contentSegments 一致），
     * 因为 QVariant 里装 TbPost* 时 toMap() 拿不到东西。
     * 内部原始指针走 subPostPointers()，用于 children() / 内存释放。
     */
    QVariantList subPosts() const;
    /*! 原始指针（内部使用：释放子对象、遍历） */
    QVariantList subPostPointers() const { return m_subPosts; }
    /*! 前几条楼中楼（已转成 map），给占位界面直接展示用 */
    QVariantList subPostPreview() const;

    /* 作者字段的扁平化副本（见上面的 Q_PROPERTY 注释） */
    QString authorName() const;
    QString authorAvatarUrl() const;
    int authorLevel() const;
    bool authorIsBawu() const;
    QString authorLabel() const;
    QString authorIpAddress() const;

    /*! 内部使用：拿具体类型的列表 */
    QList<TbPost *> children() const;

    void setId(const QString &value) { m_id = value; }
    void setFloor(const QString &value) { m_floor = value; }
    void setTimeText(const QString &value) { m_timeText = value; }
    void setPlainText(const QString &value) { m_plainText = value; }
    /*! 设置作者时会同步刷新作者的扁平化副本 */
    void setAuthor(TbUser *value);
    void setAgreeCount(int value) { m_agreeCount = value; }
    void setDisagreeCount(int value) { m_disagreeCount = value; }
    void setSubPostCount(int value) { m_subPostCount = value; }
    void setIsOriginalPoster(bool value) { m_isOriginalPoster = value; }
    void setContentSegments(const QVariantList &value);    void setSubPosts(const QVariantList &value) { m_subPosts = value; }
    QVariantMap toVariantMap() const;

private:
    QString m_id;
    QString m_floor;
    QString m_timeText;
    QString m_plainText;
    TbUser *m_author;
    int m_agreeCount;
    int m_disagreeCount;
    int m_subPostCount;
    bool m_isOriginalPoster;
    QVariantList m_contentSegments;
    QVariantList m_subPosts;

    /* 作者字段的扁平化副本，由 setAuthor() 维护 */
    QString m_authorName;
    QString m_authorAvatarUrl;
    int m_authorLevel;
    bool m_authorIsBawu;
    QString m_authorLabel;
    QString m_authorIpAddress;
};

/*! 吧页里的一条贴子 */
class TbThread : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(QString title READ title CONSTANT)
    Q_PROPERTY(QString authorId READ authorId CONSTANT)
    Q_PROPERTY(QString authorName READ authorName CONSTANT)
    Q_PROPERTY(QString authorLabel READ authorLabel CONSTANT)
    Q_PROPERTY(QString avatarUrl READ avatarUrl CONSTANT)
    Q_PROPERTY(QString replyCountText READ replyCountText CONSTANT)
    Q_PROPERTY(QString viewCountText READ viewCountText CONSTANT)
    Q_PROPERTY(int replyCount READ replyCount CONSTANT)
    Q_PROPERTY(int viewCount READ viewCount CONSTANT)
    Q_PROPERTY(qint64 agreeCount READ agreeCount CONSTANT)
    Q_PROPERTY(bool hasAgreeCount READ hasAgreeCount CONSTANT)
    Q_PROPERTY(QString agreeCountText READ agreeCountText CONSTANT)
    Q_PROPERTY(QString abstractText READ abstractText CONSTANT)
    Q_PROPERTY(QString lastTimeText READ lastTimeText CONSTANT)
    Q_PROPERTY(bool isTop READ isTop CONSTANT)
    Q_PROPERTY(bool isGood READ isGood CONSTANT)
    Q_PROPERTY(QString firstImageUrl READ firstImageUrl CONSTANT)
    Q_PROPERTY(bool hasImage READ hasImage CONSTANT)
    /*!
     * 是不是**视频贴**（设置页"隐藏视频贴"用）。
     *
     * 数据来源：PC feed.business_info_map.is_video 或 feed_video 组件；
     * 客户端 video_info 非 null。media.type=3 是图片，不据此判断视频。
     */
    Q_PROPERTY(bool isVideo READ isVideo CONSTANT)

public:
    explicit TbThread(QObject *parent = 0);

    QString id() const { return m_id; }
    QString title() const { return m_title; }
    QString authorId() const { return m_authorId; }
    QString authorName() const { return m_authorName; }
    /*! 吧头衔 / 等级名（PC 接口的 forum_level，可能为空） */
    QString authorLabel() const { return m_authorLabel; }
    QString avatarUrl() const { return m_avatarUrl; }
    QString replyCountText() const { return m_replyCountText; }
    QString viewCountText() const { return m_viewCountText; }
    int replyCount() const { return m_replyCount; }
    int viewCount() const { return m_viewCount; }
    qint64 agreeCount() const { return m_threadAgreeCount; }
    bool hasAgreeCount() const { return m_threadAgreeCount >= 0; }
    QString agreeCountText() const { return hasAgreeCount() ? TbString::shortNumber(m_threadAgreeCount) : QString(); }
    static qint64 parseAgreeCount(const TbJson &node);
    static bool parseIsVideo(const TbJson &node);
    QString abstractText() const { return m_abstractText; }
    QString lastTimeText() const { return m_lastTimeText; }
    bool isTop() const { return m_isTop; }
    bool isGood() const { return m_isGood; }
    QString firstImageUrl() const { return m_firstImageUrl; }
    bool hasImage() const { return !m_firstImageUrl.isEmpty(); }
    bool isVideo() const { return m_isVideo; }

    void setId(const QString &value) { m_id = value; }
    void setTitle(const QString &value) { m_title = value; }
    void setAuthorId(const QString &value) { m_authorId = value; }
    void setAuthorName(const QString &value) { m_authorName = value; }
    void setAuthorLabel(const QString &value) { m_authorLabel = value; }
    void setAvatarUrl(const QString &value) { m_avatarUrl = value; }
    void setReplyCount(int value)
    {
        m_replyCount = value;
        // 自动做万/亿友好显示，和 TbThreadInfo 保持一致
        m_replyCountText = TbString::shortNumber(value);
    }
    void setReplyCountText(const QString &value) { m_replyCountText = value; }
    void setViewCount(int value) { m_viewCount = value; }
    void setViewCountText(const QString &value) { m_viewCountText = value; }
    void setAgreeCount(qint64 value) { m_threadAgreeCount = value >= 0 ? value : -1; }
    void setAbstractText(const QString &value) { m_abstractText = value; }
    void setLastTimeText(const QString &value) { m_lastTimeText = value; }
    void setIsTop(bool value) { m_isTop = value; }
    void setIsGood(bool value) { m_isGood = value; }
    void setFirstImageUrl(const QString &value) { m_firstImageUrl = value; }
    void setIsVideo(bool value) { m_isVideo = value; }

    QVariantMap toVariantMap() const;

private:
    QString m_id;
    QString m_title;
    QString m_authorId;
    QString m_authorName;
    /*! 吧头衔 / 等级名 */
    QString m_authorLabel;
    QString m_avatarUrl;
    QString m_replyCountText;
    QString m_viewCountText;
    int m_replyCount;
    int m_viewCount;
    qint64 m_threadAgreeCount;
    QString m_abstractText;
    QString m_lastTimeText;
    bool m_isTop;
    bool m_isGood;
    QString m_firstImageUrl;
    /*! 视频贴标记（见 isVideo 的说明） */
    bool m_isVideo;
};

/*! 我关注的吧 */
class TbForum : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(int level READ level CONSTANT)
    Q_PROPERTY(QString levelText READ levelText CONSTANT)
    Q_PROPERTY(QString avatarUrl READ avatarUrl CONSTANT)
    Q_PROPERTY(bool isSigned READ isSigned CONSTANT)

public:
    explicit TbForum(QObject *parent = 0);

    QString id() const { return m_id; }
    QString name() const { return m_name; }
    int level() const { return m_level; }
    QString levelText() const { return QString::number(m_level); }
    QString avatarUrl() const { return m_avatarUrl; }
    /*! 今日是否已签到（注意：signed 是 C++ 关键字，方法名不能叫 signed） */
    bool isSigned() const { return m_signed; }

    void setId(const QString &value) { m_id = value; }
    void setName(const QString &value) { m_name = value; }
    void setLevel(int value) { m_level = value; }
    void setAvatarUrl(const QString &value) { m_avatarUrl = value; }
    void setSigned(bool value) { m_signed = value; }

    QVariantMap toVariantMap() const;

private:
    QString m_id;
    QString m_name;
    int m_level;
    QString m_avatarUrl;
    bool m_signed;
};

/*! 吧信息（吧页表头用） */
class TbForumInfo : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(int userLevel READ userLevel CONSTANT)
    Q_PROPERTY(bool liked READ liked CONSTANT)
    Q_PROPERTY(QString memberCountText READ memberCountText CONSTANT)
    Q_PROPERTY(QString threadCountText READ threadCountText CONSTANT)
    /*!
     * 贴子数（服务端字段 post_num）。
     *
     * 和 thread_num 的区别：thread_num 是"主题数"，post_num 是"贴子总数"
     * （含所有回复）。吧详情页要显示的是后者，所以单独存一份。
     */
    Q_PROPERTY(QString postCountText READ postCountText CONSTANT)
    /*!
     * 当前账号在这个吧的经验值（服务端字段 cur_score）。
     * 配套还有 levelup_score（升级所需）和 level_name（等级名）。
     */
    Q_PROPERTY(int score READ score CONSTANT)
    Q_PROPERTY(int levelupScore READ levelupScore CONSTANT)
    Q_PROPERTY(QString levelName READ levelName CONSTANT)
    Q_PROPERTY(QString slogan READ slogan CONSTANT)
    Q_PROPERTY(QString avatarUrl READ avatarUrl CONSTANT)

public:
    explicit TbForumInfo(QObject *parent = 0);

    QString id() const { return m_id; }
    QString name() const { return m_name; }
    int userLevel() const { return m_userLevel; }
    bool liked() const { return m_liked; }
    QString memberCountText() const { return m_memberCountText; }
    QString threadCountText() const { return m_threadCountText; }
    QString postCountText() const { return m_postCountText; }
    int score() const { return m_score; }
    int levelupScore() const { return m_levelupScore; }
    QString levelName() const { return m_levelName; }
    QString slogan() const { return m_slogan; }
    QString avatarUrl() const { return m_avatarUrl; }

    void setId(const QString &value) { m_id = value; }
    void setName(const QString &value) { m_name = value; }
    void setUserLevel(int value) { m_userLevel = value; }
    void setLiked(bool value) { m_liked = value; }
    void setMemberCountText(const QString &value) { m_memberCountText = value; }
    void setThreadCountText(const QString &value) { m_threadCountText = value; }
    void setPostCountText(const QString &value) { m_postCountText = value; }
    void setScore(int value) { m_score = value; }
    void setLevelupScore(int value) { m_levelupScore = value; }
    void setLevelName(const QString &value) { m_levelName = value; }
    void setSlogan(const QString &value) { m_slogan = value; }
    void setAvatarUrl(const QString &value) { m_avatarUrl = value; }

    QVariantMap toVariantMap() const;

private:
    QString m_id;
    QString m_name;
    int m_userLevel;
    bool m_liked;
    QString m_memberCountText;
    QString m_threadCountText;
    QString m_postCountText;
    int m_score;
    int m_levelupScore;
    QString m_levelName;
    QString m_slogan;
    QString m_avatarUrl;
};

/*! 分页信息 */
class TbPageInfo : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int currentPage READ currentPage CONSTANT)
    Q_PROPERTY(int totalPage READ totalPage CONSTANT)
    Q_PROPERTY(int pageSize READ pageSize CONSTANT)
    Q_PROPERTY(int totalCount READ totalCount CONSTANT)
    Q_PROPERTY(bool hasMore READ hasMore CONSTANT)
    Q_PROPERTY(bool hasPrev READ hasPrev CONSTANT)

public:
    explicit TbPageInfo(QObject *parent = 0);

    int currentPage() const { return m_currentPage; }
    int totalPage() const { return m_totalPage; }
    int pageSize() const { return m_pageSize; }
    int totalCount() const { return m_totalCount; }
    bool hasMore() const { return m_hasMore; }
    bool hasPrev() const { return m_hasPrev; }

    void setCurrentPage(int value) { m_currentPage = value; }
    void setTotalPage(int value) { m_totalPage = value; }
    void setPageSize(int value) { m_pageSize = value; }
    void setTotalCount(int value) { m_totalCount = value; }
    void setHasMore(bool value) { m_hasMore = value; }
    void setHasPrev(bool value) { m_hasPrev = value; }

    QVariantMap toVariantMap() const;

private:
    int m_currentPage;
    int m_totalPage;
    int m_pageSize;
    int m_totalCount;
    bool m_hasMore;
    bool m_hasPrev;
};

/*! 主题贴信息（标题、楼主、收藏状态） */
class TbThreadInfo : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(QString title READ title CONSTANT)
    Q_PROPERTY(int replyCount READ replyCount CONSTANT)
    Q_PROPERTY(QString replyCountText READ replyCountText CONSTANT)
    Q_PROPERTY(tieba::TbUser* author READ author CONSTANT)
    Q_PROPERTY(bool collected READ collected CONSTANT)

public:
    explicit TbThreadInfo(QObject *parent = 0);

    QString id() const { return m_id; }
    QString title() const { return m_title; }
    int replyCount() const { return m_replyCount; }
    /*! 友好显示（万/亿）。由 setReplyCount() 自动生成，与 TbThread 保持一致 */
    QString replyCountText() const { return m_replyCountText; }
    TbUser *author() const { return m_author; }
    bool collected() const { return m_collected; }

    void setId(const QString &value) { m_id = value; }
    void setTitle(const QString &value) { m_title = value; }
    void setReplyCount(int value)
    {
        m_replyCount = value;
        m_replyCountText = TbString::shortNumber(value);
    }
    void setReplyCountText(const QString &value) { m_replyCountText = value; }
    void setAuthor(TbUser *value) { m_author = value; }
    void setCollected(bool value) { m_collected = value; }

    QVariantMap toVariantMap() const;

private:
    QString m_id;
    QString m_title;
    int m_replyCount;
    QString m_replyCountText;
    TbUser *m_author;
    bool m_collected;
};

} // namespace tieba

/*!
 * 这些指针类型要能放进 QVariant，QListDataModel<TbPost*> /
 * QVariant::fromValue 之类才可用。
 */
Q_DECLARE_METATYPE(tieba::TbPost *)
Q_DECLARE_METATYPE(tieba::TbUser *)
Q_DECLARE_METATYPE(tieba::TbContentSeg *)

#endif // TB_ITEMS_HPP
