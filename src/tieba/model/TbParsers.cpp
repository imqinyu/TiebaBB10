#include "TbParsers.hpp"

#include "util/TbString.hpp"
#include "util/TbTr.hpp"

#include <QHash>
#include <QSet>
#include <QStringList>

namespace tieba {

namespace {

/* ------------------------------------------------------------------ *
 * 容错取值小工具
 *
 * 贴吧接口最喜欢干的两件事：
 *   1. 把数字写成字符串："reply_num": "12"
 *   2. 同一个字段在不同接口里类型不同："is_good": "1" / 1 / true
 * 所以统一走 TbJson 的容错取值。
 * ------------------------------------------------------------------ */

/*! 取字符串（数字会转成字面量） */
QString s(const TbJson &node, const char *key, const QString &def = QString())
{
    return TbJson::str(node, QLatin1String(key), def);
}

/*!
 * 取整数。
 * 注意不要叫 toInt()：文件里大量 for 循环用 i 作下标，会遮蔽函数名。
 */
int toInt(const TbJson &node, const char *key, int def = 0)
{
    return TbJson::intValue(node, QLatin1String(key), def);
}

/*! 取布尔 */
bool b(const TbJson &node, const char *key, bool def = false)
{
    return TbJson::boolean(node, QLatin1String(key), def);
}

/*! 复制一个用户对象（user_list 里的实例不能直接挂到多个楼层下） */
TbUser *cloneUser(const TbUser *source)
{
    if (!source)
        return 0;
    TbUser *user = new TbUser();
    user->setId(source->id());
    user->setName(source->name());
    user->setNameShow(source->nameShow());
    user->setPortrait(source->portrait());
    user->setAvatarUrl(source->avatarUrl());
    user->setLevel(source->level());
    user->setIsBawu(source->isBawu());
    user->setBawuType(source->bawuType());
    user->setIpAddress(source->ipAddress());
    return user;
}

/*! 解析 "宽,高" 形式的 bsize 字段 */
void parseBsize(const QString &bsize, int *width, int *height)
{
    if (width)
        *width = 0;
    if (height)
        *height = 0;
    if (bsize.isEmpty())
        return;

    const QStringList parts = bsize.split(QLatin1Char(','));
    if (parts.size() < 2)
        return;

    bool okW = false;
    bool okH = false;
    const int w = parts.at(0).trimmed().toInt(&okW);
    const int h = parts.at(1).trimmed().toInt(&okH);
    if (width && okW)
        *width = w;
    if (height && okH)
        *height = h;
}

} // anonymous namespace

/* ============================ 错误处理 ============================ */

bool TbParsers::hasError(const TbJson &root, QString *code, QString *message)
{
    const QString errorCode = s(root, "error_code");
    const QString errorMsg = s(root, "error_msg");

    if (code)
        *code = errorCode;
    if (message)
        *message = errorMsg;

    // error_code 缺失时视为通过（有些接口不返回该字段）
    if (errorCode.isEmpty())
        return false;

    bool ok = false;
    const int numeric = errorCode.toInt(&ok);
    if (ok)
        return numeric != 0;

    // 极少数情况是字符串 "success"
    return errorCode.compare(QLatin1String("0")) != 0
           && errorCode.compare(QLatin1String("success"), Qt::CaseInsensitive) != 0;
}

QString TbParsers::describeErrorCode(const QString &code)
{
    if (code.isEmpty())
        return QString();

    bool ok = false;
    const int numeric = code.toInt(&ok);
    if (!ok)
        return code;

    // 贴吧常见的错误码（来自官方客户端与社区整理）
    switch (numeric) {
    case 1:
        return TbTr("E69CAAE79FA5E99499E8AFAF2CE8AFB7E7A88DE5908EE9878DE8AF95");
    case 2:
        return TbTr("E58F82E695B0E99499E8AFAF");
    case 4:
        return TbTr("E8AFB7E6B182E8BF87E4BA8EE9A291E7B9812CE8AFB7E7A88DE5908EE5868DE8AF95");
    case 5:
        return TbTr("E799BBE5BD95E5B7B2E5A4B1E695882CE8AFB7E9878DE696B0E799BBE5BD95");
    case 7:
        return TbTr("E6B2A1E69C89E69D83E99990");
    case 101:
        return TbTr("E8B4B4E5AD90E4B88DE5AD98E59CA8E68896E5B7B2E8A2ABE588A0E999A4");
    case 102:
        return TbTr("E8AFA5E590A7E4B88DE5AD98E59CA8");
    case 110:
        return TbTr("E8AFA5E8B4B4E5B7B2E8A2ABE588A0E999A4");
    case 130:
        return TbTr("E8AFA5E590A7E4B88DE5AD98E59CA8E68896E5B7B2E585B3E997AD");
    case 132:
        return TbTr("E8AFA5E794A8E688B7E4B88DE5AD98E59CA8");
    case 215:
        return TbTr("E7B3BBE7BB9FE7BBB4E68AA4E4B8AD2CE8AFB7E7A88DE5908EE5868DE8AF95");
    case 226:
        return TbTr("E58685E5AEB9E8A2ABE7B3BBE7BB9FE68BA6E688AA2CE8AFB7E4BFAEE694B9E5908EE9878DE8AF95");
    case 228:
        return TbTr("E8AFA5E590A7E8A2ABE7A681E6ADA2E8AEBFE997AE");
    case 302:
        return TbTr("E99C80E8A681E799BBE5BD95E6898DE883BDE69FA5E79C8B");
    case 310:
        return TbTr("E799BBE5BD95E78AB6E68081E5BC82E5B8B82CE8AFB7E9878DE696B0E799BBE5BD95");
    case 340006:
        return TbTr("E8AFB7E6B182E8A2ABE68B92E7BB9D28E58FAFE883BDE99C80E8A681E69BB4E696B0E5AEA2E688B7E7ABAFE78988E69CAC29");
    default:
        return TbTr("E69C8DE58AA1E7ABAFE8BF94E59B9EE99499E8AFAFE7A081202531").arg(code);
    }
}

/* ============================ 用户 ============================ */

TbUser *TbParsers::parseUser(const TbJson &node)
{
    if (!node.isObject())
        return 0;

    TbUser *user = new TbUser();
    user->setId(s(node, "id"));
    user->setName(s(node, "name"));
    // 老接口用 nick，新接口用 name_show
    user->setNameShow(TbJson::str2(node, QLatin1String("name_show"),
                                   QLatin1String("nick")));
    const QString portrait = s(node, "portrait");
    user->setPortrait(portrait);
    user->setAvatarUrl(TbString::avatarUrl(portrait));
    user->setLevel(toInt(node, "level_id"));
    user->setIsBawu(b(node, "is_bawu"));
    user->setBawuType(s(node, "bawu_type"));
    user->setIpAddress(s(node, "ip_address"));
    return user;
}

QHash<QString, TbUser *> TbParsers::indexUsers(const TbJson &root)
{
    QHash<QString, TbUser *> out;
    const TbJson userList = TbJson::array(root, QLatin1String("user_list"));
    for (int index = 0; index < userList.size(); ++index) {
        const TbJson node = userList.at(index);
        TbUser *user = parseUser(node);
        if (!user)
            continue;
        const QString id = user->id();
        if (id.isEmpty() || out.contains(id)) {
            delete user;
            continue;
        }
        out.insert(id, user);
    }
    return out;
}

/* ============================ 正文段落 ============================ */

QList<TbContentSeg *> TbParsers::parseContent(const TbJson &node)
{
    QList<TbContentSeg *> out;
    if (!node.isArray())
        return out;

    for (int index = 0; index < node.size(); ++index) {
        const TbJson element = node.at(index);
        if (!element.isObject())
            continue;

        TbContentSeg *seg = new TbContentSeg();
        seg->setType(toInt(element, "type", -1));
        seg->setText(s(element, "text"));
        seg->setUid(s(element, "uid"));
        seg->setEmoticonName(s(element, "c"));

        // 图片类字段在几代接口里叫法不同，按优先级挑
        const QString thumb = TbJson::str2(element, QLatin1String("cdn_src"),
                                           QLatin1String("src"));
        const QString origin = TbJson::str2(element, QLatin1String("origin_src"),
                                            QLatin1String("big_cdn_src"));
        seg->setThumbUrl(thumb);
        seg->setOriginUrl(origin.isEmpty() ? thumb : origin);

        // 链接/视频地址
        const QString link = s(element, "link");
        seg->setUrl(link);

        int width = 0;
        int height = 0;
        parseBsize(s(element, "bsize"), &width, &height);
        seg->setSize(width, height);

        const QString during = s(element, "during_time");
        if (!during.isEmpty()) {
            bool ok = false;
            const int seconds = during.toInt(&ok);
            if (ok)
                seg->setDuration(seconds);
        }

        out.append(seg);
    }
    return out;
}

QVariantList TbParsers::toVariantList(const QList<TbContentSeg *> &segments)
{
    QVariantList out;
    for (int i = 0; i < segments.size(); ++i) {
        if (segments.at(i))
            out.append(segments.at(i)->toVariantMap());
    }
    return out;
}

QString TbParsers::plainTextOf(const QList<TbContentSeg *> &segments)
{
    QString out;
    for (int i = 0; i < segments.size(); ++i) {
        const TbContentSeg *seg = segments.at(i);
        if (!seg)
            continue;

        const QString kind = seg->kind();
        if (kind == QLatin1String("text")
                || kind == QLatin1String("link")
                || kind == QLatin1String("user")) {
            out += seg->text();
        } else if (kind == QLatin1String("emoticon")) {
            // 表情用 [名字] 形式保留，方便复制后仍然可读
            const QString name = seg->emoticonName().isEmpty()
                                     ? seg->text() : seg->emoticonName();
            out += QLatin1Char('[') + name + QLatin1Char(']');
        } else if (kind == QLatin1String("image")) {
            out += TbTr("5BE59BBEE789875D");
        } else if (kind == QLatin1String("video")) {
            out += TbTr("5BE8A786E9A2915D");
        } else if (kind == QLatin1String("voice")) {
            out += TbTr("5BE8AFADE99FB35D");
        }
    }
    return out;
}

QString TbParsers::firstImageOf(const TbJson &node)
{
    if (!node.isObject())
        return QString();

    // 吧页贴子的 media 数组：只看 type == "pic"
    const TbJson media = TbJson::array(node, QLatin1String("media"));
    for (int i = 0; i < media.size(); ++i) {
        const TbJson item = media.at(i);
        if (s(item, "type") != QLatin1String("pic"))
            continue;
        const QString url = TbJson::str2(item, QLatin1String("src_pic"),
                                         QLatin1String("big_pic"));
        if (!url.isEmpty())
            return url;
    }

    // abstract 数组里的图片段落（type 3 / 20 都是图片）
    const TbJson abstract = TbJson::array(node, QLatin1String("abstract"));
    for (int i = 0; i < abstract.size(); ++i) {
        const TbJson item = abstract.at(i);
        const int type = toInt(item, "type", -1);
        if (type != 3 && type != 20)
            continue;
        const QString url = TbJson::str2(item, QLatin1String("src"),
                                         QLatin1String("big_cdn_src"));
        if (!url.isEmpty())
            return url;
    }

    return QString();
}

/* ============================ 分页 ============================ */

TbPageInfo *TbParsers::parsePageInfo(const TbJson &node)
{
    if (!node.isObject())
        return 0;

    TbPageInfo *page = new TbPageInfo();
    page->setCurrentPage(toInt(node, "current_page"));
    page->setTotalPage(toInt(node, "total_page"));
    page->setPageSize(toInt(node, "page_size"));
    page->setTotalCount(toInt(node, "total_count"));
    page->setHasMore(b(node, "has_more"));
    page->setHasPrev(b(node, "has_prev"));
    return page;
}

/* ============================ 我关注的吧 ============================ */

TbForumListResult TbParsers::parseForumRecommend(const TbJson &root)
{
    TbForumListResult result;

    if (hasError(root, &result.errorCode, &result.errorMessage)) {
        if (result.errorMessage.isEmpty())
            result.errorMessage = describeErrorCode(result.errorCode);
        return result;
    }

    const TbJson list = TbJson::array(root, QLatin1String("like_forum"));
    for (int index = 0; index < list.size(); ++index) {
        const TbJson node = list.at(index);
        if (!node.isObject())
            continue;

        TbForum *forum = new TbForum();
        forum->setId(s(node, "forum_id"));
        forum->setName(s(node, "forum_name"));
        forum->setLevel(toInt(node, "level_id"));
        forum->setSigned(b(node, "is_sign"));
        forum->setAvatarUrl(TbString::avatarUrl(s(node, "avatar")));
        result.forums.append(forum);
    }

    return result;
}

/*!
 * 解析 /c/f/forum/like（完整关注列表）。
 *
 * 实测响应结构（用 Python 在开发机上打印过）：
 *     {
 *       "error_code": 0,
 *       "has_more": 0,
 *       "forum_list": {
 *         "non-gconforum": [ {"id":"632086","name":"草莓",
 *                             "level_id":13,"level_name":"...","avatar":"https://...",
 *                             "cur_score":16140,"levelup_score":18000,
 *                             "slogan":"","favo_type":0,"is_forbidden":0}, ... ],   // 118 条
 *         "gconforum":     [ ... ]                                                  // 3 条
 *       }
 *     }
 *
 * 注意字段名是 id/name（不是 forum_id/forum_name）。
 * 没有 is_sign 字段 —— 所以 isSigned 一律 false，
 * 想要签到状态得另外调接口（签到功能本来也还没做）。
 */
TbForumListResult TbParsers::parseForumLike(const TbJson &root)
{
    TbForumListResult result;

    if (hasError(root, &result.errorCode, &result.errorMessage)) {
        if (result.errorMessage.isEmpty())
            result.errorMessage = describeErrorCode(result.errorCode);
        return result;
    }

    const TbJson forumList = TbJson::object(root, QLatin1String("forum_list"));
    if (! forumList.isObject()) {
        result.errorCode = QLatin1String("-1");
        result.errorMessage = TbTr("E585B3E6B3A8E58897E8A1A8E8BF94E59B9EE6A0BCE5BC8FE5BC82E5B8B8EFBC8CE8AFB7E588B7E696B0E68896E9878DE696B0E799BBE5BD95");
        return result;
    }

    // 关注数可能超过一页，把 has_more 带出去让控制器决定要不要继续翻
    result.hasMore = TbJson::integer(root, QLatin1String("has_more"), 0) != 0;

    // 两个数组都要收（少了 gconforum 会丢几个吧）
    const char *const kBuckets[] = { "non-gconforum", "gconforum" };
    for (int b = 0; b < 2; ++b) {
        const TbJson list = TbJson::array(forumList, QLatin1String(kBuckets[b]));
        for (int index = 0; index < list.size(); ++index) {
            const TbJson node = list.at(index);
            if (! node.isObject())
                continue;

            const QString name = TbJson::str(node, QLatin1String("name"));
            if (name.isEmpty())
                continue;

            TbForum *forum = new TbForum();
            forum->setId(TbJson::str(node, QLatin1String("id")));
            forum->setName(name);
            forum->setLevel((int)TbJson::integer(node, QLatin1String("level_id"), 0));
            // 这个接口没有 is_sign，只能给 false（界面若要"已签到"标记得另做）
            forum->setSigned(false);
            forum->setAvatarUrl(
                TbString::avatarUrl(TbJson::str(node, QLatin1String("avatar"))));
            result.forums.append(forum);
        }
    }

    return result;
}

/* ============================ 吧页贴子列表 ============================ */
TbForumPageResult TbParsers::parseForumPage(const TbJson &root)
{
    TbForumPageResult result;

    if (hasError(root, &result.errorCode, &result.errorMessage)) {
        if (result.errorMessage.isEmpty())
            result.errorMessage = describeErrorCode(result.errorCode);
        return result;
    }

    // 吧信息
    const TbJson forumNode = TbJson::object(root, QLatin1String("forum"));
    if (forumNode.isObject()) {
        TbForumInfo *info = new TbForumInfo();
        info->setId(s(forumNode, "id"));
        info->setName(s(forumNode, "name"));
        info->setUserLevel(toInt(forumNode, "user_level"));
        info->setLiked(b(forumNode, "is_like"));
        info->setMemberCountText(TbString::shortNumber(
                                     TbJson::integer(forumNode, QLatin1String("member_num"))));
        info->setThreadCountText(TbString::shortNumber(
                                     TbJson::integer(forumNode, QLatin1String("thread_num"))));
        info->setSlogan(TbString::stripHtml(s(forumNode, "slogan")));
        info->setAvatarUrl(s(forumNode, "avatar"));
        result.forumInfo = info;
    }

    // 发帖需要的 tbs
    result.tbs = TbJson::str(TbJson::object(root, QLatin1String("anti")),
                             QLatin1String("tbs"));

    // 贴子列表
    const TbJson threads = TbJson::array(root, QLatin1String("thread_list"));
    for (int index = 0; index < threads.size(); ++index) {
        const TbJson node = threads.at(index);
        if (!node.isObject())
            continue;

        TbThread *thread = new TbThread();
        // id 与 tid 在不同接口里二选一
        QString id = s(node, "id");
        if (id.isEmpty())
            id = s(node, "tid");
        thread->setId(id);
        thread->setTitle(TbString::cleanText(s(node, "title")));
        thread->setAuthorId(s(node, "author_id"));

        // setReplyCount() 内部已经做了万/亿友好显示
        thread->setReplyCount(toInt(node, "reply_num"));

        const int viewNum = toInt(node, "view_num");
        thread->setViewCount(viewNum);
        thread->setViewCountText(TbString::shortNumber(viewNum));
        thread->setAgreeCount(TbThread::parseAgreeCount(node));

        thread->setIsTop(b(node, "is_top"));
        thread->setIsGood(b(node, "is_good"));
        thread->setIsVideo(TbThread::parseIsVideo(node));

        // 最后回复时间：优先 last_time_int
        qint64 lastTime = TbJson::integer(node, QLatin1String("last_time_int"));
        if (lastTime <= 0)
            lastTime = TbJson::integer(node, QLatin1String("last_time"));
        thread->setLastTimeText(TbString::formatUnixTime(lastTime));

        // 摘要：abstract 是类型化段落数组，拼接其中的 text
        const TbJson abstract = TbJson::array(node, QLatin1String("abstract"));
        QString abstractText;
        for (int a = 0; a < abstract.size(); ++a)
            abstractText += s(abstract.at(a), "text");
        thread->setAbstractText(TbString::cleanText(abstractText));

        thread->setFirstImageUrl(firstImageOf(node));

        result.threads.append(thread);
    }

    result.pageInfo = parsePageInfo(TbJson::object(root, QLatin1String("page")));

    return result;
}

/* ============================ 贴子楼层 ============================ */

TbThreadContentResult TbParsers::parseThreadContent(const TbJson &root)
{
    TbThreadContentResult result;

    if (hasError(root, &result.errorCode, &result.errorMessage)) {
        if (result.errorMessage.isEmpty())
            result.errorMessage = describeErrorCode(result.errorCode);
        return result;
    }

    // 作者可能只在 user_list 里出现，先建索引
    const QHash<QString, TbUser *> users = indexUsers(root);

    // 主题贴信息
    const TbJson threadNode = TbJson::object(root, QLatin1String("thread"));
    if (threadNode.isObject()) {
        TbThreadInfo *info = new TbThreadInfo();
        info->setId(TbJson::str2(threadNode, QLatin1String("id"),
                                 QLatin1String("thread_id")));
        info->setTitle(TbString::cleanText(s(threadNode, "title")));
        info->setReplyCount(toInt(threadNode, "reply_num"));
        info->setCollected(b(threadNode, "collect_status"));

        // 楼主
        const TbJson authorNode = TbJson::object(threadNode, QLatin1String("author"));
        if (authorNode.isObject()) {
            TbUser *author = parseUser(authorNode);
            if (author) {
                // 如果 user_list 里有更完整的同 id 记录，用它的等级等信息补齐
                QHash<QString, TbUser *>::const_iterator it =
                    users.find(author->id());
                if (it != users.end() && it.value() != author) {
                    if (author->level() == 0)
                        author->setLevel(it.value()->level());
                    if (author->avatarUrl().isEmpty())
                        author->setAvatarUrl(it.value()->avatarUrl());
                }
                info->setAuthor(author);
            }
        }
        result.threadInfo = info;
    }

    result.tbs = TbJson::str(TbJson::object(root, QLatin1String("anti")),
                             QLatin1String("tbs"));

    const QString threadAuthorId = result.threadInfo && result.threadInfo->author()
                                       ? result.threadInfo->author()->id()
                                       : QString();

    const TbJson posts = TbJson::array(root, QLatin1String("post_list"));
    for (int index = 0; index < posts.size(); ++index) {
        const TbJson node = posts.at(index);
        if (!node.isObject())
            continue;

        TbPost *post = new TbPost();
        post->setId(s(node, "id"));
        post->setFloor(s(node, "floor"));
        post->setTimeText(TbString::formatUnixTime(
                              TbJson::integer(node, QLatin1String("time"))));

        // 作者：优先用响应里内嵌的 author 对象，
        // 否则用 author_id 到 user_list 里查（很多楼层只给 id）
        TbUser *author = 0;
        const TbJson authorNode = TbJson::object(node, QLatin1String("author"));
        if (authorNode.isObject()) {
            author = parseUser(authorNode);
        } else {
            const QString authorId = s(node, "author_id");
            QHash<QString, TbUser *>::const_iterator it = users.find(authorId);
            if (it != users.end() && it.value())
                author = cloneUser(it.value());
        }
        post->setAuthor(author);

        if (!threadAuthorId.isEmpty() && author
                && author->id() == threadAuthorId) {
            post->setIsOriginalPoster(true);
        }

        // 正文：转成 QVariantList（每项是 QVariantMap），
        // 这样 Cascades 的 ArrayDataModel / ListItemData 能直接用。
        // setContentSegments() 内部会顺手拼好 plainText。
        const QList<TbContentSeg *> segments =
            parseContent(TbJson::array(node, QLatin1String("content")));
        post->setContentSegments(toVariantList(segments));
        qDeleteAll(segments);

        // 点赞/点踩
        const TbJson agree = TbJson::object(node, QLatin1String("agree"));
        if (agree.isObject()) {
            post->setAgreeCount(toInt(agree, "agree_num"));
            post->setDisagreeCount(toInt(agree, "disagree_num"));
        }

        // 楼中楼：sub_post_list 可能是数组（旧接口）或对象（新接口）
        const TbJson subPostList = node.member(QLatin1String("sub_post_list"));

        TbJson subArray;
        if (subPostList.isArray())
            subArray = subPostList;
        else if (subPostList.isObject())
            subArray = TbJson::array(subPostList, QLatin1String("sub_post_list"));

        QVariantList subPosts;
        int loadedSubPosts = 0;
        for (int sIdx = 0; sIdx < subArray.size(); ++sIdx) {
            const TbJson subNode = subArray.at(sIdx);
            if (!subNode.isObject())
                continue;

            TbPost *sub = new TbPost();
            sub->setId(s(subNode, "id"));
            sub->setFloor(s(subNode, "floor"));
            sub->setTimeText(TbString::formatUnixTime(
                                 TbJson::integer(subNode, QLatin1String("time"))));

            const TbJson subAuthorNode =
                TbJson::object(subNode, QLatin1String("author"));
            if (subAuthorNode.isObject())
                sub->setAuthor(parseUser(subAuthorNode));

            const QList<TbContentSeg *> subSegments =
                parseContent(TbJson::array(subNode, QLatin1String("content")));
            sub->setContentSegments(toVariantList(subSegments));
            qDeleteAll(subSegments);

            subPosts.append(QVariant::fromValue(sub));
            ++loadedSubPosts;
        }
        post->setSubPosts(subPosts);

        // sub_post_number 是楼中楼总数，可能大于本次加载的数量
        int subCount = toInt(node, "sub_post_number");
        if (subCount <= 0)
            subCount = loadedSubPosts;
        post->setSubPostCount(subCount);

        result.posts.append(post);
    }

    // indexUsers 里创建的没被用到的用户要回收
    QSet<TbUser *> used;
    if (result.threadInfo && result.threadInfo->author())
        used.insert(result.threadInfo->author());
    for (int i = 0; i < result.posts.size(); ++i) {
        TbPost *post = result.posts.at(i);
        if (post->author())
            used.insert(post->author());
        const QList<TbPost *> children = post->children();
        for (int c = 0; c < children.size(); ++c) {
            if (children.at(c)->author())
                used.insert(children.at(c)->author());
        }
    }
    for (QHash<QString, TbUser *>::const_iterator it = users.begin();
            it != users.end(); ++it) {
        if (!used.contains(it.value()))
            delete it.value();
    }

    result.pageInfo = parsePageInfo(TbJson::object(root, QLatin1String("page")));

    return result;
}

/* ============================ 楼中楼 ============================ */

TbSubFloorResult TbParsers::parseSubFloorList(const TbJson &root)
{
    TbSubFloorResult result;

    if (hasError(root, &result.errorCode, &result.errorMessage)) {
        if (result.errorMessage.isEmpty())
            result.errorMessage = describeErrorCode(result.errorCode);
        return result;
    }

    // 被回复的那一层
    const TbJson parentNode = TbJson::object(root, QLatin1String("post"));
    if (parentNode.isObject()) {
        TbPost *parent = new TbPost();
        parent->setId(s(parentNode, "id"));
        parent->setFloor(s(parentNode, "floor"));
        parent->setTimeText(TbString::formatUnixTime(
                                TbJson::integer(parentNode, QLatin1String("time"))));
        const TbJson authorNode = TbJson::object(parentNode, QLatin1String("author"));
        if (authorNode.isObject())
            parent->setAuthor(parseUser(authorNode));
        const QList<TbContentSeg *> segments =
            parseContent(TbJson::array(parentNode, QLatin1String("content")));
        parent->setContentSegments(toVariantList(segments));
        qDeleteAll(segments);
        result.parentPost = parent;
    }

    const TbJson list = TbJson::array(root, QLatin1String("subpost_list"));
    for (int index = 0; index < list.size(); ++index) {
        const TbJson node = list.at(index);
        if (!node.isObject())
            continue;

        TbPost *post = new TbPost();
        post->setId(s(node, "id"));
        post->setFloor(s(node, "floor"));
        post->setTimeText(TbString::formatUnixTime(
                              TbJson::integer(node, QLatin1String("time"))));

        const TbJson authorNode = TbJson::object(node, QLatin1String("author"));
        if (authorNode.isObject())
            post->setAuthor(parseUser(authorNode));

        const QList<TbContentSeg *> segments =
            parseContent(TbJson::array(node, QLatin1String("content")));
        post->setContentSegments(toVariantList(segments));
        qDeleteAll(segments);

        result.posts.append(post);
    }

    result.pageInfo = parsePageInfo(TbJson::object(root, QLatin1String("page")));

    return result;
}

} // namespace tieba
