#include "TbItems.hpp"
#include "util/TbJson.hpp"

#include "util/TbString.hpp"
#include "util/TbTr.hpp"

namespace tieba {

/* ============================ TbContentSeg ============================ */

TbContentSeg::TbContentSeg(QObject *parent)
    : QObject(parent)
    , m_type(-1)
    , m_width(0)
    , m_height(0)
    , m_duration(0)
{
}

QString TbContentSeg::kind() const
{
    switch (m_type) {
    case 0:
    case 9:
    case 27:
    case 35:
    case 40:
        return QLatin1String("text");
    case 1:
        return QLatin1String("link");
    case 2:
        return QLatin1String("emoticon");
    case 3:
    case 20:
        return QLatin1String("image");
    case 4:
        return QLatin1String("user");
    case 5:
        return QLatin1String("video");
    case 10:
        return QLatin1String("voice");
    default:
        return QLatin1String("unknown");
    }
}

QVariantMap TbContentSeg::toVariantMap() const
{
    QVariantMap map;
    map.insert(QLatin1String("type"), m_type);
    map.insert(QLatin1String("kind"), kind());
    map.insert(QLatin1String("text"), m_text);
    map.insert(QLatin1String("url"), m_url);
    map.insert(QLatin1String("thumbUrl"), m_thumbUrl);
    map.insert(QLatin1String("originUrl"), m_originUrl);
    map.insert(QLatin1String("uid"), m_uid);
    map.insert(QLatin1String("emoticonName"), m_emoticonName);
    map.insert(QLatin1String("width"), m_width);
    map.insert(QLatin1String("height"), m_height);
    map.insert(QLatin1String("duration"), m_duration);
    return map;
}

/* ============================ TbUser ============================ */

TbUser::TbUser(QObject *parent)
    : QObject(parent)
    , m_level(0)
    , m_isBawu(false)
{
}

QString TbUser::displayName() const
{
    if (!m_nameShow.isEmpty())
        return m_nameShow;
    if (!m_name.isEmpty())
        return m_name;
    return m_id;
}

QString TbUser::bawuLabel() const
{
    if (!m_isBawu && m_bawuType.isEmpty())
        return QString();
    if (m_bawuType == QLatin1String("manager"))
        return TbTr("E590A7E4B8BB");
    if (m_bawuType == QLatin1String("assistant")
            || m_bawuType == QLatin1String("bawu")) {
        return TbTr("E5B08FE590A7E4B8BB");
    }
    if (m_bawuType.isEmpty())
        return TbTr("E590A7E58AA1");
    return TbTr("E590A7E58AA1");
}

QVariantMap TbUser::toVariantMap() const
{
    QVariantMap map;
    map.insert(QLatin1String("id"), m_id);
    map.insert(QLatin1String("name"), m_name);
    map.insert(QLatin1String("nameShow"), m_nameShow);
    map.insert(QLatin1String("displayName"), displayName());
    map.insert(QLatin1String("portrait"), m_portrait);
    map.insert(QLatin1String("avatarUrl"), m_avatarUrl);
    map.insert(QLatin1String("level"), m_level);
    map.insert(QLatin1String("isBawu"), m_isBawu);
    map.insert(QLatin1String("bawuType"), m_bawuType);
    map.insert(QLatin1String("bawuLabel"), bawuLabel());
    map.insert(QLatin1String("ipAddress"), m_ipAddress);
    return map;
}

/* ============================ TbPost ============================ */

TbPost::TbPost(QObject *parent)
    : QObject(parent)
    , m_author(0)
    , m_agreeCount(0)
    , m_disagreeCount(0)
    , m_subPostCount(0)
    , m_isOriginalPoster(false)
    , m_authorLevel(0)
    , m_authorIsBawu(false)
{
}

void TbPost::setAuthor(TbUser *value)
{
    m_author = value;
    // 作者信息变了就立刻刷新扁平化副本，避免 UI 看到过期数据
    if (value) {
        m_authorName = value->displayName();
        m_authorAvatarUrl = value->avatarUrl();
        m_authorLevel = value->level();
        m_authorIsBawu = value->isBawu();
        m_authorLabel = value->bawuLabel();
        m_authorIpAddress = value->ipAddress();
    } else {
        m_authorName.clear();
        m_authorAvatarUrl.clear();
        m_authorLevel = 0;
        m_authorIsBawu = false;
        m_authorLabel.clear();
        m_authorIpAddress.clear();
    }
}

QString TbPost::authorName() const { return m_authorName; }
QString TbPost::authorAvatarUrl() const { return m_authorAvatarUrl; }
int TbPost::authorLevel() const { return m_authorLevel; }
bool TbPost::authorIsBawu() const { return m_authorIsBawu; }
QString TbPost::authorLabel() const { return m_authorLabel; }
QString TbPost::authorIpAddress() const { return m_authorIpAddress; }

void TbPost::setContentSegments(const QVariantList &value)
{
    m_contentSegments = value;

    /*
     * 顺手把纯文本拼好：UI 里直接显示、复制、搜索都用它。
     *
     * 关于图片：**不往正文里塞 "[图片]"**。
     *   QML 会用单独的 ImageView 把图渲染出来，文字里再出现占位符就重复了。
     *   但纯图贴（一个文字段都没有）需要给点提示，否则正文整段空白。
     */
    QString text;
    bool hasImage = false;
    bool hasVideo = false;
    bool hasVoice = false;

    for (int i = 0; i < m_contentSegments.size(); ++i) {
        const QVariantMap seg = m_contentSegments.at(i).toMap();
        const QString kind = seg.value(QLatin1String("kind")).toString();
        if (kind == QLatin1String("text")
                || kind == QLatin1String("link")
                || kind == QLatin1String("user")) {
            text += seg.value(QLatin1String("text")).toString();
        } else if (kind == QLatin1String("emoticon")) {
            const QString name = seg.value(QLatin1String("emoticonName")).toString();
            text += QLatin1Char('[') + (name.isEmpty()
                       ? seg.value(QLatin1String("text")).toString() : name)
                    + QLatin1Char(']');
        } else if (kind == QLatin1String("image")) {
            hasImage = true;
        } else if (kind == QLatin1String("video")) {
            hasVideo = true;
        } else if (kind == QLatin1String("voice")) {
            hasVoice = true;
        }
    }

    // 纯图贴 / 纯视频贴：文字部分空着不好看，补个提示
    if (text.trimmed().isEmpty()) {
        if (hasImage)
            text += TbTr("5BE59BBEE789875D");
        if (hasVideo)
            text += TbTr("5BE8A786E9A2915D");
        if (hasVoice)
            text += TbTr("5BE8AFADE99FB35D");
    }

    m_plainText = text;
}

QVariantList TbPost::subPosts() const
{
    QVariantList out;
    for (int i = 0; i < m_subPosts.size(); ++i) {
        TbPost *post = m_subPosts.at(i).value<TbPost *>();
        if (post)
            out.append(post->toVariantMap());
    }
    return out;
}

QList<TbPost *> TbPost::children() const
{
    QList<TbPost *> out;
    for (int i = 0; i < m_subPosts.size(); ++i) {
        TbPost *post = m_subPosts.at(i).value<TbPost *>();
        if (post)
            out.append(post);
    }
    return out;
}

QVariantList TbPost::subPostPreview() const
{
    // 占位界面里直接展示前几条楼中楼；需要完整列表时走
    // TiebaController::loadSubPosts() 拿 tieba.subPosts
    QVariantList out;
    const int limit = qMin(m_subPosts.size(), 3);
    for (int i = 0; i < limit; ++i) {
        TbPost *post = m_subPosts.at(i).value<TbPost *>();
        if (post)
            out.append(post->toVariantMap());
    }
    return out;
}

QVariantMap TbPost::toVariantMap() const
{
    QVariantMap map;
    map.insert(QLatin1String("id"), m_id);
    map.insert(QLatin1String("floor"), m_floor);
    map.insert(QLatin1String("floorNumber"), floorNumber());
    map.insert(QLatin1String("timeText"), m_timeText);
    map.insert(QLatin1String("plainText"), m_plainText);
    map.insert(QLatin1String("agreeCount"), m_agreeCount);
    map.insert(QLatin1String("disagreeCount"), m_disagreeCount);
    map.insert(QLatin1String("subPostCount"), m_subPostCount);
    map.insert(QLatin1String("isOriginalPoster"), m_isOriginalPoster);
    map.insert(QLatin1String("contentSegments"), m_contentSegments);

    /*
     * 正文里的图片地址列表。
     *
     * QML 的 ListItemComponent 里没法方便地对 contentSegments 做过滤/映射，
     * 所以在这里提前挑好，界面直接用 `ListItemData.imageUrls` 配 Repeater 渲染。
     * 每项是 { url, width, height }。
     */
    QVariantList images;
    for (int i = 0; i < m_contentSegments.size(); ++i) {
        const QVariantMap seg = m_contentSegments.at(i).toMap();
        if (seg.value(QLatin1String("kind")).toString() != QLatin1String("image"))
            continue;
        // 显示用地址优先（服务端压过的小图），没有再退回原图
        QString url = seg.value(QLatin1String("url")).toString();
        if (url.isEmpty())
            url = seg.value(QLatin1String("originUrl")).toString();
        if (url.isEmpty())
            continue;
        QVariantMap img;
        img.insert(QLatin1String("url"), url);
        img.insert(QLatin1String("originUrl"),
                   seg.value(QLatin1String("originUrl")));
        img.insert(QLatin1String("width"), seg.value(QLatin1String("width")));
        img.insert(QLatin1String("height"), seg.value(QLatin1String("height")));
        images.append(img);
    }
    map.insert(QLatin1String("images"), images);
    // 便捷字段：界面只渲染第一张图（Cascades 里嵌套列表不可靠），
    // 其余数量用 imageCount / extraImageText 提示。
    map.insert(QLatin1String("firstImageUrl"),
               images.isEmpty()
                   ? QString()
                   : images.at(0).toMap().value(QLatin1String("url")).toString());
    map.insert(QLatin1String("hasImage"), !images.isEmpty());
    map.insert(QLatin1String("imageCount"), images.size());
    if (images.size() > 1) {
        // 例： "还有 2 张图片"
        map.insert(QLatin1String("extraImageText"),
                   TbTr("E8BF98E69C89") + QLatin1Char(' ')
                       + QString::number(images.size() - 1) + QLatin1Char(' ')
                       + TbTr("E5BCA0E59BBEE78987"));
    } else {
        map.insert(QLatin1String("extraImageText"), QString());
    }

    /*
     * 楼中楼预览拼成纯文本。
     *
     * 为什么在 C++ 拼而不是在 QML 里循环：
     *   Cascades **没有 Repeater**（那是 Qt Quick 的类型），
     *   在 ListItemComponent 里没法遍历数组。拼成一个多行 Label 最省事。
     */
    QString previewText;
    const QVariantList preview = subPostPreview();
    for (int i = 0; i < preview.size(); ++i) {
        const QVariantMap m = preview.at(i).toMap();
        if (i > 0)
            previewText += QLatin1Char('\n');
        QString name = m.value(QLatin1String("authorName")).toString().trimmed();
        if (name.isEmpty())
            name = TbTr("E69CAAE79FA5E794A8E688B7"); // Unknown user, never a bare colon.
        previewText += name
                       + QLatin1String(": ")
                       + m.value(QLatin1String("plainText")).toString();
    }
    map.insert(QLatin1String("subPostPreviewText"), previewText);
    // 原始结构也保留，将来 UI 想自己排版可以用
    map.insert(QLatin1String("subPostPreview"), preview);

    // 作者：既有扁平字段（QML 列表里最可靠），也有完整 map
    map.insert(QLatin1String("authorName"), m_authorName);
    map.insert(QLatin1String("authorAvatarUrl"), m_authorAvatarUrl);
    map.insert(QLatin1String("authorId"), m_author ? m_author->id() : QString());
    map.insert(QLatin1String("authorLevel"), m_authorLevel);
    map.insert(QLatin1String("authorIsBawu"), m_authorIsBawu);
    map.insert(QLatin1String("authorLabel"), m_authorLabel);
    map.insert(QLatin1String("authorIpAddress"), m_authorIpAddress);
    map.insert(QLatin1String("author"), m_author ? m_author->toVariantMap() : QVariantMap());

    return map;
}

/* ============================ TbThread ============================ */

TbThread::TbThread(QObject *parent)
    : QObject(parent)
    , m_replyCount(0)
    , m_viewCount(0)
    , m_threadAgreeCount(-1)
    , m_isTop(false)
    , m_isGood(false)
    , m_isVideo(false)
{
}

QVariantMap TbThread::toVariantMap() const
{
    QVariantMap map;
    map.insert(QLatin1String("id"), m_id);
    map.insert(QLatin1String("title"), m_title);
    map.insert(QLatin1String("authorId"), m_authorId);
    map.insert(QLatin1String("authorName"), m_authorName);
    map.insert(QLatin1String("authorLabel"), m_authorLabel);
    map.insert(QLatin1String("avatarUrl"), m_avatarUrl);
    map.insert(QLatin1String("replyCount"), m_replyCount);
    map.insert(QLatin1String("replyCountText"), m_replyCountText);
    map.insert(QLatin1String("viewCount"), m_viewCount);
    map.insert(QLatin1String("viewCountText"), m_viewCountText);
    map.insert(QLatin1String("agreeCount"), m_threadAgreeCount);
    map.insert(QLatin1String("hasAgreeCount"), hasAgreeCount());
    map.insert(QLatin1String("agreeCountText"), agreeCountText());
    map.insert(QLatin1String("abstractText"), m_abstractText);
    map.insert(QLatin1String("lastTimeText"), m_lastTimeText);
    map.insert(QLatin1String("isTop"), m_isTop);
    map.insert(QLatin1String("isGood"), m_isGood);
    map.insert(QLatin1String("isVideo"), m_isVideo);
    map.insert(QLatin1String("firstImageUrl"), m_firstImageUrl);
    map.insert(QLatin1String("hasImage"), hasImage());
    return map;
}

/* ============================ TbForum ============================ */
qint64 TbThread::parseAgreeCount(const TbJson &node)
{
    // Nested agree is the client / PC feed count; search uses like_num.
    // Missing, null, malformed and negative values remain unknown, not zero.
    const TbJson candidates[] = {
        node.member(QLatin1String("agree")).member(QLatin1String("agree_num")),
        node.member(QLatin1String("agree_num")),
        node.member(QLatin1String("like_num"))
    };
    for (int i = 0; i < 3; ++i) {
        const TbJson &value = candidates[i];
        if (!value.isNumber() && !value.isString()) continue;
        bool ok = false;
        const qint64 count = value.toString().trimmed().toLongLong(&ok);
        if (ok && count >= 0) return count;
    }
    return -1;
}

bool TbThread::parseIsVideo(const TbJson &node)
{
    if (!node.isObject()) return false;
    // TiebaLite uses non-null video_info. media.type=3 represents pictures,
    // and must never turn a regular image thread into a video thread.
    if (TbJson::boolean(node, QLatin1String("is_video"))
            || node.member(QLatin1String("video_info")).isObject()
            || node.member(QLatin1String("feed_video")).isObject()
            || !TbJson::str(node, QLatin1String("video_url")).isEmpty()) return true;
    const TbJson business = node.member(QLatin1String("business_info_map"));
    if (TbJson::boolean(business, QLatin1String("is_video"))) return true;
    const TbJson components = node.member(QLatin1String("components"));
    for (int i = 0; i < components.size(); ++i) {
        const TbJson component = components.at(i);
        if (TbJson::str(component, QLatin1String("component")) == QLatin1String("feed_video")) return true;
    }
    const TbJson feed = node.member(QLatin1String("feed"));
    return feed.isObject() && parseIsVideo(feed);
}

TbForum::TbForum(QObject *parent)
    : QObject(parent)
    , m_level(0)
    , m_signed(false)
{
}

QVariantMap TbForum::toVariantMap() const
{
    QVariantMap map;
    map.insert(QLatin1String("id"), m_id);
    map.insert(QLatin1String("name"), m_name);
    map.insert(QLatin1String("level"), m_level);
    map.insert(QLatin1String("levelText"), levelText());
    map.insert(QLatin1String("avatarUrl"), m_avatarUrl);
    map.insert(QLatin1String("isSigned"), m_signed);
    return map;
}

/* ============================ TbForumInfo ============================ */

TbForumInfo::TbForumInfo(QObject *parent)
    : QObject(parent)
    , m_userLevel(0)
    , m_liked(false)
    , m_score(0)
    , m_levelupScore(0)
{
}

QVariantMap TbForumInfo::toVariantMap() const
{
    QVariantMap map;
    map.insert(QLatin1String("id"), m_id);
    map.insert(QLatin1String("name"), m_name);
    map.insert(QLatin1String("userLevel"), m_userLevel);
    map.insert(QLatin1String("liked"), m_liked);
    map.insert(QLatin1String("memberCountText"), m_memberCountText);
    map.insert(QLatin1String("threadCountText"), m_threadCountText);
    // 吧详情页要的字段
    map.insert(QLatin1String("postCountText"), m_postCountText);
    map.insert(QLatin1String("score"), m_score);
    map.insert(QLatin1String("levelupScore"), m_levelupScore);
    map.insert(QLatin1String("levelName"), m_levelName);
    map.insert(QLatin1String("slogan"), m_slogan);
    map.insert(QLatin1String("avatarUrl"), m_avatarUrl);
    return map;
}

/* ============================ TbPageInfo ============================ */

TbPageInfo::TbPageInfo(QObject *parent)
    : QObject(parent)
    , m_currentPage(0)
    , m_totalPage(0)
    , m_pageSize(0)
    , m_totalCount(0)
    , m_hasMore(false)
    , m_hasPrev(false)
{
}

QVariantMap TbPageInfo::toVariantMap() const
{
    QVariantMap map;
    map.insert(QLatin1String("currentPage"), m_currentPage);
    map.insert(QLatin1String("totalPage"), m_totalPage);
    map.insert(QLatin1String("pageSize"), m_pageSize);
    map.insert(QLatin1String("totalCount"), m_totalCount);
    map.insert(QLatin1String("hasMore"), m_hasMore);
    map.insert(QLatin1String("hasPrev"), m_hasPrev);
    return map;
}

/* ============================ TbThreadInfo ============================ */

TbThreadInfo::TbThreadInfo(QObject *parent)
    : QObject(parent)
    , m_replyCount(0)
    , m_author(0)
    , m_collected(false)
{
}

QVariantMap TbThreadInfo::toVariantMap() const
{
    QVariantMap map;
    map.insert(QLatin1String("id"), m_id);
    map.insert(QLatin1String("title"), m_title);
    map.insert(QLatin1String("replyCount"), m_replyCount);
    map.insert(QLatin1String("replyCountText"), replyCountText());
    map.insert(QLatin1String("collected"), m_collected);
    map.insert(QLatin1String("author"), m_author ? m_author->toVariantMap() : QVariantMap());
    map.insert(QLatin1String("authorName"),
               m_author ? m_author->displayName() : QString());
    map.insert(QLatin1String("authorAvatarUrl"),
               m_author ? m_author->avatarUrl() : QString());
    return map;
}

} // namespace tieba
