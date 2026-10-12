/*
 * 本机验证用测试（不属于 BB10 工程）
 *
 * 用「接近真实响应形状」的 fixture 验证：
 *   - TbParsers 的字段映射（重点是类型不稳、大小写、缺字段）
 *   - TbSigner 的公共参数与签名
 *   - TbSession 的 cookie 解析
 *   - TbItems 的 toVariantMap()（交给 Cascades ArrayDataModel 的形式）
 *
 * fixture 里的 JSON 是按 TiebaLite 的模型定义和贴吧实际返回形状手写的，
 * 目的是把「字段名/类型」这类最容易写错的地方钉死。
 */
#include "../src/tieba/api/TbApi.hpp"
#include "../src/tieba/model/TbItems.hpp"
#include "../src/tieba/model/TbParsers.hpp"
#include "../src/tieba/model/TbPcParsers.hpp"
#include "../src/tieba/net/TbCookieStore.hpp"
#include "../src/tieba/net/TbSigner.hpp"
#include "../src/tieba/session/TbSession.hpp"
#include "../src/tieba/util/TbJson.hpp"
#include "../src/tieba/util/TbMd5.hpp"
#include "../src/tieba/util/TbString.hpp"

#include <QByteArray>
#include <QFile>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtGlobal>
#include <stdio.h>
#include <string.h>

#include <windows.h>

static int g_failures = 0;
static int g_checks = 0;

/*! 把字符串转成可读的十六进制，便于在编码混乱的控制台里看清真实字节 */
static QByteArray hexOf(const QString &text)
{
    const QByteArray utf8 = text.toUtf8();
    QByteArray out;
    for (int i = 0; i < utf8.size(); ++i) {
        if (i > 0)
            out.append(' ');
        out.append(QByteArray::number((unsigned char)utf8.at(i), 16).rightJustified(2, '0'));
    }
    return out;
}

static void checkStr(const QString &label, const QString &actual, const QString &expected)
{
    ++g_checks;
    if (actual == expected) {
        printf("  OK   %s\n", qPrintable(label));
    } else {
        ++g_failures;
        printf("  FAIL %s\n", qPrintable(label));
        printf("       expected: [%s]\n", qPrintable(expected));
        printf("       actual:   [%s]\n", qPrintable(actual));
        printf("       expected hex: %s\n", hexOf(expected).constData());
        printf("       actual   hex: %s\n", hexOf(actual).constData());
    }
}

/*!
 * 期望值为窄字面量时的重载。
 *
 * 走 const QString& 版本的话，窄字面量会被 Qt 按 Latin-1 隐式转换，
 * 中文的 3 个 UTF-8 字节会变成 3 个 Latin-1 字符。这里直接比较 UTF-8 字节。
 */
static void checkStr(const QString &label, const QString &actual,
                     const char *expected)
{
    ++g_checks;
    const QByteArray a = actual.toUtf8();
    if (expected && strcmp(a.constData(), expected) == 0) {
        printf("  OK   %s\n", qPrintable(label));
    } else {
        ++g_failures;
        printf("  FAIL %s\n", qPrintable(label));
        printf("       expected: [%s]\n", expected ? expected : "(null)");
        printf("       actual:   [%s]\n", a.constData());
        printf("       expected hex: %s\n", expected ? hexOf(QByteArray(expected)).constData() : "");
        printf("       actual   hex: %s\n", hexOf(a).constData());
    }
}

static void checkInt(const QString &label, qint64 actual, qint64 expected)
{
    ++g_checks;
    if (actual == expected) {
        printf("  OK   %s\n", qPrintable(label));
    } else {
        ++g_failures;
        printf("  FAIL %s  expected %lld actual %lld\n",
               qPrintable(label), (long long)expected, (long long)actual);
    }
}

static void checkBool(const QString &label, bool actual, bool expected)
{
    ++g_checks;
    if (actual == expected) {
        printf("  OK   %s\n", qPrintable(label));
    } else {
        ++g_failures;
        printf("  FAIL %s  expected %d actual %d\n",
               qPrintable(label), (int)expected, (int)actual);
    }
}

static QByteArray utf8(const char *text)
{
    return QByteArray(text);
}

/* ==================== fixture 1: 我关注的吧 ==================== */
static const char *kForumRecommendJson =
    "{"
    "  \"error_code\":\"0\","
    "  \"error_msg\":\"\","
    "  \"like_forum\":["
    "    {\"forum_id\":\"23080\",\"forum_name\":\"\\u9ed1\\u8309\\u8389\","
    "     \"level_id\":\"7\",\"is_sign\":\"1\",\"avatar\":\"abc123\"},"
    "    {\"forum_id\":28195,\"forum_name\":\"BlackBerry\","
    "     \"level_id\":12,\"is_sign\":0,\"avatar\":"
    "     \"http://tb.himg.baidu.com/sys/forum/image?x=1\"},"
    "    {\"forum_id\":\"3\",\"forum_name\":\"\\u7f3a\\u5b57\\u6bb5\\u7684\\u5427\","
    "     \"level_id\":\"1\",\"is_sign\":\"0\"}"
    "  ]"
    "}";

static void testForumRecommend()
{
    printf("\n== \u89E3\u6790\uFF1A/c/f/forum/forumrecommend ==\n");

    const tieba::TbJson root = tieba::TbJson::parse(QByteArray(kForumRecommendJson));
    checkBool("fixture \u662F\u5408\u6CD5 JSON", root.isValid(), true);
    if (!root.isValid()) {
        printf("       %s\n", qPrintable(root.errorMessage()));
        return;
    }

    const tieba::TbForumListResult result = tieba::TbParsers::parseForumRecommend(root);
    checkStr("\u65E0\u9519\u8BEF\u7801", result.errorCode, "0");
    checkInt("\u5427\u6570\u91CF", result.forums.size(), 3);

    if (result.forums.size() >= 1) {
        const tieba::TbForum *f0 = result.forums.at(0);
        checkStr("id \u53D6\u5B57\u7B26\u4E32", f0->id(), "23080");
        checkStr("\u4E2D\u6587\u5427\u540D", f0->name(), QString::fromUtf8("\u9ED1\u8309\u8389"));
        checkInt("\u7B49\u7EA7", f0->level(), 7);
        checkBool("is_sign=\"1\" \u5DF2\u7B7E\u5230", f0->isSigned(), true);
        checkStr("\u5934\u50CF\u8865\u5168 URL", f0->avatarUrl(),
                 "https://himg.bdimg.com/sys/portrait/item/abc123");
    }

    if (result.forums.size() >= 2) {
        const tieba::TbForum *f1 = result.forums.at(1);
        checkStr("\u6570\u5B57\u578B id \u4E5F\u80FD\u53D6", f1->id(), "28195");
        checkInt("\u6570\u5B57\u578B\u7B49\u7EA7", f1->level(), 12);
        checkBool("is_sign=0 \u672A\u7B7E\u5230", f1->isSigned(), false);
        checkStr("\u5DF2\u662F\u5B8C\u6574 URL \u7684\u5934\u50CF\u4E0D\u518D\u62FC\u63A5", f1->avatarUrl(),
                 "https://tb.himg.baidu.com/sys/forum/image?x=1");
    }

    if (result.forums.size() >= 3) {
        checkStr("\u7F3A avatar \u5B57\u6BB5\u65F6\u4E3A\u7A7A", result.forums.at(2)->avatarUrl(), "");
    }

    qDeleteAll(result.forums);
}

/* ==================== fixture 2: 吧页贴子列表 ==================== */
static const char *kForumPageJson =
    "{"
    "  \"error_code\":\"0\",\"error_msg\":\"\","
    "  \"forum\":{\"id\":23080,\"name\":\"\\u9ed1\\u8309\\u8389\","
    "    \"is_like\":\"1\",\"user_level\":\"7\",\"level_id\":\"7\","
    "    \"member_num\":\"1234567\",\"thread_num\":\"89012\","
    "    \"slogan\":\"<b>\\u4f60\\u597d</b>\","
    "    \"avatar\":\"http://x/a.png\"},"
    "  \"anti\":{\"tbs\":\"TBSVALUE123\",\"ifpost\":\"1\"},"
    "  \"thread_list\":["
    "    {\"id\":9876543210123456,\"title\":\"\\u6d4b\\u8bd5\\u5e16\\u5b50\","
    "     \"reply_num\":\"12\",\"view_num\":\"345\",\"agree_num\":\"6\","
    "     \"is_top\":\"1\",\"is_good\":\"0\",\"author_id\":\"111\","
    "     \"last_time_int\":\"1700000000\","
    "     \"abstract\":[{\"type\":\"0\",\"text\":\"\\u6b63\\u6587\\u6458\\u8981\"}]},"
    "    {\"id\":\"222\",\"title\":\"\\u5e26\\u56fe\\u7684\\u5e16\","
    "     \"reply_num\":45678,\"view_num\":120000,\"is_top\":0,\"is_good\":1,"
    "     \"author_id\":\"222\",\"last_time\":\"1700000000\","
    "     \"media\":[{\"type\":\"pic\",\"src_pic\":\"http://img/1.jpg\","
    "                 \"big_pic\":\"http://img/1b.jpg\"}]},"
    "    {\"id\":\"333\",\"title\":\"  \\u5e26\\u96f6\\u5bbd\\u5b57\\u7b26  \","
    "     \"reply_num\":\"0\",\"view_num\":\"0\",\"is_top\":\"0\",\"is_good\":\"0\","
    "     \"author_id\":\"333\"}"
    "  ],"
    "  \"page\":{\"current_page\":\"2\",\"total_page\":\"50\",\"has_more\":\"1\","
    "    \"has_prev\":\"0\",\"page_size\":\"30\",\"total_count\":\"1500\"}"
    "}";

static void testForumPage()
{
    printf("\n== \u89E3\u6790\uFF1A/c/f/frs/page ==\n");

    const tieba::TbJson root = tieba::TbJson::parse(QByteArray(kForumPageJson));
    checkBool("fixture \u662F\u5408\u6CD5 JSON", root.isValid(), true);
    if (!root.isValid()) {
        printf("       %s\n", qPrintable(root.errorMessage()));
        return;
    }

    const tieba::TbForumPageResult result = tieba::TbParsers::parseForumPage(root);

    checkBool("\u6709\u5427\u4FE1\u606F", result.forumInfo != 0, true);
    if (result.forumInfo) {
        checkStr("\u5427\u540D", result.forumInfo->name(), QString::fromUtf8("\u9ED1\u8309\u8389"));
        checkInt("\u6211\u7684\u7B49\u7EA7", result.forumInfo->userLevel(), 7);
        checkBool("\u5DF2\u5173\u6CE8", result.forumInfo->liked(), true);
        checkStr("\u4F1A\u5458\u6570\u53CB\u597D\u663E\u793A", result.forumInfo->memberCountText(), "123.5W");
        checkStr("\u4E3B\u9898\u6570\u53CB\u597D\u663E\u793A", result.forumInfo->threadCountText(), "8.9W");
        checkStr("\u7B80\u4ECB\u5265\u6389 HTML", result.forumInfo->slogan(), QString::fromUtf8("\u4F60\u597D"));
    }

    checkStr("anti.tbs \u88AB\u63D0\u53D6\uFF08\u53D1\u5E16\u8981\u7528\uFF09", result.tbs, "TBSVALUE123");
    checkInt("\u8D34\u5B50\u6570", result.threads.size(), 3);

    if (result.threads.size() >= 1) {
        const tieba::TbThread *t0 = result.threads.at(0);
        checkStr("\u8D85\u957F\u6570\u5B57 id \u4E0D\u4E22\u7CBE\u5EA6", t0->id(), "9876543210123456");
        checkStr("\u6807\u9898", t0->title(), QString::fromUtf8("\u6D4B\u8BD5\u5E16\u5B50"));
        checkInt("\u56DE\u590D\u6570", t0->replyCount(), 12);
        checkStr("\u56DE\u590D\u6570\u663E\u793A", t0->replyCountText(), "12");
        checkInt("\u6D4F\u89C8\u6570", t0->viewCount(), 345);
        checkBool("\u7F6E\u9876", t0->isTop(), true);
        checkBool("\u975E\u7CBE\u54C1", t0->isGood(), false);
        checkStr("\u6458\u8981", t0->abstractText(), QString::fromUtf8("\u6B63\u6587\u6458\u8981"));
        checkBool("\u65E0\u56FE", t0->hasImage(), false);
        checkStr("\u4F5C\u8005 id", t0->authorId(), "111");
        checkBool("\u65F6\u95F4\u5DF2\u683C\u5F0F\u5316", !t0->lastTimeText().isEmpty(), true);
    }

    if (result.threads.size() >= 2) {
        const tieba::TbThread *t1 = result.threads.at(1);
        checkStr("\u5B57\u7B26\u4E32 id", t1->id(), "222");
        checkInt("\u6570\u5B57\u578B reply_num", t1->replyCount(), 45678);
        checkStr("\u4E07\u7EA7\u663E\u793A", t1->replyCountText(), "4.6W");
        checkStr("\u5341\u4E07\u7EA7\u6D4F\u89C8", t1->viewCountText(), "12.0W");
        checkBool("is_good=1 \u7CBE\u54C1", t1->isGood(), true);
        checkBool("is_top=0 \u975E\u7F6E\u9876", t1->isTop(), false);
        checkStr("\u4ECE media \u91CC\u53D6\u9996\u56FE", t1->firstImageUrl(), "http://img/1.jpg");
        checkBool("\u6709\u56FE", t1->hasImage(), true);
    }

    if (result.threads.size() >= 3) {
        checkStr("\u6807\u9898\u96F6\u5BBD\u5B57\u7B26\u88AB\u6E05\u6389", result.threads.at(2)->title(),
                 QString::fromUtf8("\u5E26\u96F6\u5BBD\u5B57\u7B26"));
    }

    checkBool("\u6709\u5206\u9875\u4FE1\u606F", result.pageInfo != 0, true);
    if (result.pageInfo) {
        checkInt("\u5F53\u524D\u9875", result.pageInfo->currentPage(), 2);
        checkInt("\u603B\u9875\u6570", result.pageInfo->totalPage(), 50);
        checkInt("\u6BCF\u9875\u6761\u6570", result.pageInfo->pageSize(), 30);
        checkInt("\u603B\u6570", result.pageInfo->totalCount(), 1500);
        checkBool("\u6709\u4E0B\u4E00\u9875", result.pageInfo->hasMore(), true);
        checkBool("\u65E0\u4E0A\u4E00\u9875", result.pageInfo->hasPrev(), false);
    }

    qDeleteAll(result.threads);
    delete result.forumInfo;
    delete result.pageInfo;
}

/* ==================== fixture 3: 贴子楼层 ==================== */
static const char *kThreadContentJson =
    "{"
    "  \"error_code\":\"0\",\"error_msg\":\"\","
    "  \"thread\":{\"id\":\"9876543210123456\",\"title\":\"\\u4e3b\\u9898\\u5e16\\u6807\\u9898\","
    "    \"reply_num\":\"12\",\"collect_status\":\"1\","
    "    \"author\":{\"id\":\"111\",\"name\":\"opuser\",\"name_show\":\"\\u697c\\u4e3b\\u6635\\u79f0\","
    "                \"portrait\":\"op123\",\"level_id\":\"9\",\"is_bawu\":\"1\","
    "                \"bawu_type\":\"manager\"}},"
    "  \"anti\":{\"tbs\":\"TBS_ABC\"},"
    "  \"user_list\":["
    "    {\"id\":\"111\",\"name\":\"opuser\",\"name_show\":\"\\u697c\\u4e3b\\u6635\\u79f0\","
    "     \"portrait\":\"op123\",\"level_id\":\"9\"},"
    "    {\"id\":\"222\",\"name\":\"replier\",\"name_show\":\"\\u56de\\u5e16\\u8005\","
    "     \"portrait\":\"rp456\",\"level_id\":\"4\"}"
    "  ],"
    "  \"post_list\":["
    "    {\"id\":\"9001\",\"floor\":\"1\",\"time\":\"1700000000\","
    "     \"author_id\":\"111\","
    "     \"content\":["
    "        {\"type\":\"0\",\"text\":\"\\u7b2c\\u4e00\\u6bb5\"},"
    "        {\"type\":\"3\",\"bsize\":\"800,600\",\"src\":\"http://img/a.jpg\","
    "         \"big_cdn_src\":\"http://img/a_big.jpg\",\"origin_src\":\"http://img/a_org.jpg\"},"
    "        {\"type\":\"2\",\"c\":\"smile\",\"text\":\"[smile]\"},"
    "        {\"type\":\"0\",\"text\":\"\\u6700\\u540e\\u4e00\\u6bb5\"}"
    "     ],"
    "     \"agree\":{\"agree_num\":\"10\",\"disagree_num\":\"1\"},"
    "     \"sub_post_number\":\"5\","
    "     \"sub_post_list\":{\"pid\":\"9001\",\"sub_post_list\":["
    "        {\"id\":\"9002\",\"floor\":\"2\",\"time\":\"1700000100\","
    "         \"author\":{\"id\":\"222\",\"name\":\"replier\",\"name_show\":\"\\u56de\\u5e16\\u8005\","
    "                     \"portrait\":\"rp456\",\"level_id\":\"4\"},"
    "         \"content\":[{\"type\":\"0\",\"text\":\"\\u697c\\u4e2d\\u697c\\u56de\\u590d\"}]}"
    "     ]}},"
    "    {\"id\":\"9003\",\"floor\":\"2\",\"time\":\"1700000200\","
    "     \"author\":{\"id\":\"222\",\"name\":\"replier\",\"name_show\":\"\\u56de\\u5e16\\u8005\","
    "                 \"portrait\":\"rp456\",\"level_id\":\"4\",\"ip_address\":\"1.2.3.4\"},"
    "     \"content\":[{\"type\":\"4\",\"uid\":\"111\",\"text\":\"@\\u697c\\u4e3b\"},"
    "                  {\"type\":\"0\",\"text\":\" \\u4f60\\u597d\"}],"
    "     \"agree\":{\"agree_num\":\"0\",\"disagree_num\":\"0\"},"
    "     \"sub_post_number\":\"0\","
    "     \"sub_post_list\":[]},"
    "    {\"id\":\"9004\",\"floor\":\"3\",\"time\":\"1700000300\","
    "     \"author_id\":\"222\","
    "     \"content\":[{\"type\":\"10\",\"voice_md5\":\"vmd5\",\"during_time\":\"3\"},"
    "                  {\"type\":\"5\",\"text\":\"http://v/1\",\"src\":\"http://v/cover.jpg\","
    "                   \"link\":\"http://v/real.mp4\",\"bsize\":\"640,360\"}]}"
    "  ],"
    "  \"page\":{\"current_page\":\"1\",\"total_page\":\"3\",\"has_more\":\"1\","
    "    \"has_prev\":\"0\"}"
    "}";

static void testThreadContent()
{
    printf("\n== \u89E3\u6790\uFF1A/c/f/pb/page ==\n");

    const tieba::TbJson root = tieba::TbJson::parse(QByteArray(kThreadContentJson));
    checkBool("fixture \u662F\u5408\u6CD5 JSON", root.isValid(), true);
    if (!root.isValid()) {
        printf("       %s\n", qPrintable(root.errorMessage()));
        return;
    }

    const tieba::TbThreadContentResult result = tieba::TbParsers::parseThreadContent(root);

    checkStr("anti.tbs", result.tbs, "TBS_ABC");
    checkBool("\u6709\u4E3B\u9898\u8D34\u4FE1\u606F", result.threadInfo != 0, true);
    if (result.threadInfo) {
        checkStr("tid", result.threadInfo->id(), "9876543210123456");
        checkStr("\u6807\u9898", result.threadInfo->title(), QString::fromUtf8("\u4E3B\u9898\u5E16\u6807\u9898"));
        checkInt("\u56DE\u590D\u6570", result.threadInfo->replyCount(), 12);
        checkBool("\u5DF2\u6536\u85CF", result.threadInfo->collected(), true);
        checkBool("\u6709\u697C\u4E3B", result.threadInfo->author() != 0, true);
        if (result.threadInfo->author()) {
            checkStr("\u697C\u4E3B\u6635\u79F0\u4F18\u5148", result.threadInfo->author()->displayName(),
                     QString::fromUtf8("\u697C\u4E3B\u6635\u79F0"));
            checkInt("\u697C\u4E3B\u7B49\u7EA7", result.threadInfo->author()->level(), 9);
            checkBool("\u697C\u4E3B\u662F\u5427\u52A1", result.threadInfo->author()->isBawu(), true);
            checkStr("\u5427\u52A1\u6807\u7B7E", result.threadInfo->author()->bawuLabel(),
                     QString::fromUtf8("\u5427\u4E3B"));
            checkStr("\u697C\u4E3B\u5934\u50CF URL", result.threadInfo->author()->avatarUrl(),
                     "https://himg.bdimg.com/sys/portrait/item/op123");
        }
    }

    checkInt("\u697C\u5C42\u6570", result.posts.size(), 3);

    if (result.posts.size() >= 1) {
        const tieba::TbPost *p0 = result.posts.at(0);
        checkStr("\u697C\u5C42\u53F7", p0->floor(), "1");
        checkInt("\u697C\u5C42\u53F7\u6570\u503C", p0->floorNumber(), 1);
        checkInt("\u70B9\u8D5E\u6570", p0->agreeCount(), 10);
        checkInt("\u70B9\u8E29\u6570", p0->disagreeCount(), 1);
        checkBool("\u662F\u4E00\u697C\uFF08\u697C\u4E3B\uFF09", p0->isOriginalPoster(), true);
        checkInt("\u697C\u4E2D\u697C\u603B\u6570", p0->subPostCount(), 5);

        // 正文段落
        const QVariantList segs = p0->contentSegments();
        {
            checkInt("\u6BB5\u843D\u6570", segs.size(), 4);
            const QVariantMap s0 = segs.value(0).toMap();
            const QVariantMap s1 = segs.value(1).toMap();
            const QVariantMap s2 = segs.value(2).toMap();
            checkStr("\u6BB5\u843D0 \u7C7B\u578B", s0.value("kind").toString(), "text");
            checkStr("\u6BB5\u843D0 \u6587\u672C", s0.value("text").toString(),
                     QString::fromUtf8("\u7B2C\u4E00\u6BB5"));
            checkStr("\u6BB5\u843D1 \u7C7B\u578B", s1.value("kind").toString(), "image");
            checkStr("\u6BB5\u843D1 \u7F29\u7565\u56FE", s1.value("thumbUrl").toString(), "http://img/a.jpg");
            checkStr("\u6BB5\u843D1 \u539F\u56FE", s1.value("originUrl").toString(), "http://img/a_org.jpg");
            checkInt("\u6BB5\u843D1 \u5BBD", s1.value("width").toInt(), 800);
            checkInt("\u6BB5\u843D1 \u9AD8", s1.value("height").toInt(), 600);
            checkStr("\u6BB5\u843D2 \u7C7B\u578B", s2.value("kind").toString(), "emoticon");
            checkStr("\u6BB5\u843D2 \u8868\u60C5\u540D", s2.value("emoticonName").toString(), "smile");
        }

        // 正文里**不插入** "[图片]" 占位符：图片由 QML 的 ImageView 单独渲染，
        // 文字里再来一个就重复了。纯图贴才会补占位符。
        checkStr("\u7EAF\u6587\u672C\uFF08\u542B\u8868\u60C5\uFF0C\u4E0D\u542B\u56FE\u7247\u5360\u4F4D\uFF09", p0->plainText(),
                 QString::fromUtf8("\u7B2C\u4E00\u6BB5[smile]\u6700\u540E\u4E00\u6BB5"));

        // 作者扁平化副本：QML 列表里靠这些字段，必须和 author 对象保持一致
        checkStr("\u6241\u5E73 authorName", p0->authorName(),
                 QString::fromUtf8("\u697C\u4E3B\u6635\u79F0"));
        checkStr("\u6241\u5E73 authorAvatarUrl", p0->authorAvatarUrl(),
                 "https://himg.bdimg.com/sys/portrait/item/op123");
        checkInt("\u6241\u5E73 authorLevel", p0->authorLevel(), 9);
        // 一楼只给了 author_id，作者是从 user_list 里取的，
        // 而 user_list 不带 is_bawu/bawu_type（那是 thread.author 才有的字段），
        // 所以这里应当没有吧务标记。
        checkBool("user_list \u6765\u6E90\u7684\u4F5C\u8005\u6CA1\u6709\u5427\u52A1\u6807\u8BB0", p0->authorIsBawu(), false);
        checkStr("user_list \u6765\u6E90\u7684\u4F5C\u8005\u6CA1\u6709\u5427\u52A1\u6807\u7B7E", p0->authorLabel(), "");

        // 楼中楼预览（现在是 QVariantList of QVariantMap）
        const QVariantList subs = p0->subPosts();
        checkInt("\u5DF2\u52A0\u8F7D\u697C\u4E2D\u697C\u6570", subs.size(), 1);
        {
            const QVariantMap sub = subs.value(0).toMap();
            checkStr("\u697C\u4E2D\u697C id", sub.value("id").toString(), "9002");
            checkStr("\u697C\u4E2D\u697C\u6B63\u6587", sub.value("plainText").toString(),
                     QString::fromUtf8("\u697C\u4E2D\u697C\u56DE\u590D"));
            checkStr("\u697C\u4E2D\u697C\u4F5C\u8005\u540D", sub.value("authorName").toString(),
                     QString::fromUtf8("\u56DE\u5E16\u8005"));
        }
        // 预览列表（给占位界面用）
        checkInt("\u697C\u4E2D\u697C\u9884\u89C8\u6761\u6570", p0->subPostPreview().size(), 1);
    }

    if (result.posts.size() >= 2) {
        const tieba::TbPost *p1 = result.posts.at(1);
        checkBool("\u4E8C\u697C\u4E0D\u662F\u697C\u4E3B", p1->isOriginalPoster(), false);
        checkBool("\u4E8C\u697C\u6709\u4F5C\u8005\u5BF9\u8C61", p1->author() != 0, true);
        if (p1->author()) {
            checkStr("\u4E8C\u697C\u6635\u79F0", p1->author()->displayName(), QString::fromUtf8("\u56DE\u5E16\u8005"));
            checkInt("\u4E8C\u697C\u7B49\u7EA7", p1->author()->level(), 4);
            checkStr("IP \u5C5E\u5730", p1->author()->ipAddress(), "1.2.3.4");
        }
        const QVariantList segs = p1->contentSegments();
        if (segs.size() >= 2) {
            const QVariantMap s0 = segs.value(0).toMap();
            checkStr("@\u4EBA\u7684\u6BB5\u843D\u7C7B\u578B", s0.value("kind").toString(), "user");
            checkStr("@\u4EBA\u7684 uid", s0.value("uid").toString(), "111");
        }
        checkStr("\u7EAF\u6587\u672C\u542B @", p1->plainText(), QString::fromUtf8("@\u697C\u4E3B \u4F60\u597D"));
    }

    if (result.posts.size() >= 3) {
        const tieba::TbPost *p2 = result.posts.at(2);
        // 这一层只有 author_id，没有内嵌 author —— 应该能从 user_list 命中
        checkBool("\u7528 author_id \u4ECE user_list \u547D\u4E2D\u4F5C\u8005", p2->author() != 0, true);
        if (p2->author()) {
            checkStr("\u547D\u4E2D\u7684\u6635\u79F0", p2->author()->displayName(),
                     QString::fromUtf8("\u56DE\u5E16\u8005"));
            checkStr("\u547D\u4E2D\u7684\u5934\u50CF", p2->author()->avatarUrl(),
                     "https://himg.bdimg.com/sys/portrait/item/rp456");
        }

        const QVariantList segs = p2->contentSegments();
        if (segs.size() >= 2) {
            const QVariantMap voice = segs.value(0).toMap();
            const QVariantMap video = segs.value(1).toMap();
            checkStr("\u8BED\u97F3\u6BB5\u843D\u7C7B\u578B", voice.value("kind").toString(), "voice");
            checkInt("\u8BED\u97F3\u65F6\u957F", voice.value("duration").toInt(), 3);
            checkStr("\u89C6\u9891\u6BB5\u843D\u7C7B\u578B", video.value("kind").toString(), "video");
            checkStr("\u89C6\u9891\u5730\u5740", video.value("url").toString(), "http://v/real.mp4");
            checkStr("\u89C6\u9891\u5C01\u9762", video.value("thumbUrl").toString(), "http://v/cover.jpg");
        }
    }

    checkBool("\u6709\u5206\u9875\u4FE1\u606F", result.pageInfo != 0, true);
    if (result.pageInfo) {
        checkInt("\u5F53\u524D\u9875", result.pageInfo->currentPage(), 1);
        checkInt("\u603B\u9875\u6570", result.pageInfo->totalPage(), 3);
        checkBool("\u6709\u4E0B\u4E00\u9875", result.pageInfo->hasMore(), true);
    }

    // 楼层里嵌套了楼中楼（TbPost*），要先删子对象
    for (int i = 0; i < result.posts.size(); ++i) {
        qDeleteAll(result.posts.at(i)->children());
        delete result.posts.at(i);
    }
    delete result.threadInfo;
    delete result.pageInfo;
}

/* ==================== fixture 4: 楼中楼 ==================== */
static const char *kSubFloorJson =
    "{"
    "  \"error_code\":\"0\",\"error_msg\":\"\","
    "  \"post\":{\"id\":\"9001\",\"floor\":\"1\",\"time\":\"1700000000\","
    "    \"author\":{\"id\":\"111\",\"name\":\"opuser\",\"name_show\":\"\\u697c\\u4e3b\"},"
    "    \"content\":[{\"type\":\"0\",\"text\":\"\\u88ab\\u56de\\u590d\\u7684\\u697c\\u5c42\"}]},"
    "  \"subpost_list\":["
    "    {\"id\":\"9101\",\"floor\":\"2\",\"time\":\"1700000100\","
    "     \"author\":{\"id\":\"222\",\"name\":\"a\",\"name_show\":\"\\u7532\"},"
    "     \"content\":[{\"type\":\"0\",\"text\":\"\\u56de\\u590d\\u4e00\"}]},"
    "    {\"id\":\"9102\",\"floor\":\"3\",\"time\":\"1700000200\","
    "     \"author\":{\"id\":\"333\",\"name\":\"b\",\"name_show\":\"\\u4e59\"},"
    "     \"content\":[{\"type\":\"0\",\"text\":\"\\u56de\\u590d\\u4e8c\"}]}"
    "  ],"
    "  \"page\":{\"current_page\":\"1\",\"total_page\":\"1\",\"total_count\":\"2\","
    "    \"page_size\":\"20\"}"
    "}";

static void testSubFloors()
{
    printf("\n== \u89E3\u6790\uFF1A/c/f/pb/floor ==\n");

    const tieba::TbJson root = tieba::TbJson::parse(QByteArray(kSubFloorJson));
    checkBool("fixture \u662F\u5408\u6CD5 JSON", root.isValid(), true);
    if (!root.isValid())
        return;

    const tieba::TbSubFloorResult result = tieba::TbParsers::parseSubFloorList(root);

    checkBool("\u6709\u88AB\u56DE\u590D\u7684\u697C\u5C42", result.parentPost != 0, true);
    if (result.parentPost) {
        checkStr("\u7236\u697C\u5C42 id", result.parentPost->id(), "9001");
        checkStr("\u7236\u697C\u5C42\u6B63\u6587", result.parentPost->plainText(),
                 QString::fromUtf8("\u88AB\u56DE\u590D\u7684\u697C\u5C42"));
    }
    checkInt("\u697C\u4E2D\u697C\u6761\u6570", result.posts.size(), 2);
    if (result.posts.size() >= 2) {
        checkStr("\u7B2C\u4E00\u6761 id", result.posts.at(0)->id(), "9101");
        checkStr("\u7B2C\u4E00\u6761\u6B63\u6587", result.posts.at(0)->plainText(),
                 QString::fromUtf8("\u56DE\u590D\u4E00"));
        checkStr("\u7B2C\u4E8C\u6761\u6635\u79F0", result.posts.at(1)->author()
                     ? result.posts.at(1)->author()->displayName() : QString(),
                 QString::fromUtf8("\u4E59"));
    }
    checkBool("\u6709\u5206\u9875\u4FE1\u606F", result.pageInfo != 0, true);
    if (result.pageInfo)
        checkInt("\u697C\u4E2D\u697C\u603B\u6570", result.pageInfo->totalCount(), 2);

    qDeleteAll(result.posts);
    delete result.parentPost;
    delete result.pageInfo;
}

/* ==================== 错误码处理 ==================== */
static void testErrorResponses()
{
    printf("\n== \u9519\u8BEF\u7801\u5904\u7406 ==\n");

    struct Case {
        const char *json;
        const char *code;
        const char *descNeedle;   // 期望中文描述里包含的关键词（UTF-8）
    };
    const Case cases[] = {
        { "{\"error_code\":\"5\",\"error_msg\":\"\"}", "5", "\u767B\u5F55" },
        { "{\"error_code\":\"110\",\"error_msg\":\"\"}", "110", "\u5220\u9664" },
        { "{\"error_code\":\"340006\",\"error_msg\":\"\"}", "340006", "\u62D2\u7EDD" },
        { "{\"error_code\":\"0\",\"error_msg\":\"\"}", "0", "" },
    };

    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        const tieba::TbJson root = tieba::TbJson::parse(QByteArray(cases[i].json));
        QString code;
        QString message;
        const bool isError = tieba::TbParsers::hasError(root, &code, &message);
        const bool expectedError = QByteArray(cases[i].code) != QByteArray("0");

        checkBool(QString("error_code=%1 \u5224\u5B9A\u4E3A\u9519\u8BEF").arg(cases[i].code),
                  isError, expectedError);
        checkStr(QString("error_code=%1 \u89E3\u6790\u51FA\u7684\u7801").arg(cases[i].code),
                 code, cases[i].code);

        if (expectedError) {
            const QString desc = tieba::TbParsers::describeErrorCode(code);
            checkBool(QString("error_code=%1 \u6709\u4E2D\u6587\u63CF\u8FF0").arg(cases[i].code),
                      desc.contains(QString::fromUtf8(cases[i].descNeedle)), true);
        }
    }

    // 解析器在错误响应上不应产生半成品对象
    const tieba::TbJson errorRoot =
        tieba::TbJson::parse(QByteArray("{\"error_code\":\"5\",\"error_msg\":\"x\"}"));
    const tieba::TbForumPageResult page =
        tieba::TbParsers::parseForumPage(errorRoot);
    checkInt("\u9519\u8BEF\u54CD\u5E94\u4E0D\u4EA7\u751F\u8D34\u5B50", page.threads.size(), 0);
    checkBool("\u9519\u8BEF\u54CD\u5E94\u4E0D\u5E26\u5427\u4FE1\u606F", page.forumInfo == 0, true);
    checkBool("\u9519\u8BEF\u54CD\u5E94\u5E26\u9519\u8BEF\u4FE1\u606F", !page.errorMessage.isEmpty(), true);

    const tieba::TbThreadContentResult thread =
        tieba::TbParsers::parseThreadContent(errorRoot);
    checkInt("\u9519\u8BEF\u54CD\u5E94\u4E0D\u4EA7\u751F\u697C\u5C42", thread.posts.size(), 0);
    checkBool("\u9519\u8BEF\u54CD\u5E94\u5E26\u9519\u8BEF\u4FE1\u606F", !thread.errorMessage.isEmpty(), true);

    const tieba::TbForumListResult forums =
        tieba::TbParsers::parseForumRecommend(errorRoot);
    checkInt("\u9519\u8BEF\u54CD\u5E94\u4E0D\u4EA7\u751F\u5427", forums.forums.size(), 0);
}

/* ==================== 签名 ==================== */
static void testSigner()
{
    printf("\n== TbSigner \u516C\u5171\u53C2\u6570\u4E0E\u7B7E\u540D ==\n");

    tieba::TbSession session;
    session.setCredentials("0123456789012345678901234567890123456789012345678901234567890123"
                           "45678901234567890123",
                           "STOKENVALUE");

    const QList<tieba::KeyValue> params =
        tieba::TbSigner::commonParams(&session, QLatin1String("7.2.0.0"));

    // 公共参数必须齐全，缺一个就会被服务端拒绝
    QStringList names;
    for (int i = 0; i < params.size(); ++i)
        names.append(params.at(i).first);

    checkBool("\u542B _client_id", names.contains("_client_id"), true);
    checkBool("\u542B _client_type", names.contains("_client_type"), true);
    checkBool("\u542B _client_version", names.contains("_client_version"), true);
    checkBool("\u542B _phone_imei", names.contains("_phone_imei"), true);
    checkBool("\u542B model", names.contains("model"), true);
    checkBool("\u542B net_type", names.contains("net_type"), true);
    checkBool("\u542B timestamp", names.contains("timestamp"), true);
    checkBool("\u767B\u5F55\u540E\u542B BDUSS", names.contains("BDUSS"), true);

    // _client_id 形如 wappc_<毫秒>_<0-999>
    QString clientId;
    for (int i = 0; i < params.size(); ++i) {
        if (params.at(i).first == QLatin1String("_client_id"))
            clientId = params.at(i).second;
    }
    checkBool("_client_id \u524D\u7F00\u6B63\u786E", clientId.startsWith("wappc_"), true);
    checkBool("_client_id \u5F62\u5982 wappc_ms_n", clientId.count(QLatin1Char('_')) == 2, true);

    // 未登录时不带 BDUSS
    tieba::TbSession anonymous;
    const QList<tieba::KeyValue> anonParams =
        tieba::TbSigner::commonParams(&anonymous, QLatin1String("7.2.0.0"));
    QStringList anonNames;
    for (int i = 0; i < anonParams.size(); ++i)
        anonNames.append(anonParams.at(i).first);
    checkBool("\u672A\u767B\u5F55\u4E0D\u542B BDUSS", anonNames.contains("BDUSS"), false);

    // 签名：同一组参数必须稳定可复现
    const QString sign1 = tieba::TbSigner::sign(params);
    const QString sign2 = tieba::TbSigner::sign(params);
    checkInt("sign \u957F\u5EA6", sign1.size(), 32);
    checkBool("sign \u53EF\u590D\u73B0", sign1 == sign2, true);
    checkBool("sign \u662F\u5341\u516D\u8FDB\u5236", sign1.contains(QRegExp("^[0-9a-f]{32}$")), true);

    // 手工验证签名算法：md5(排序后的 k=v 用 & 连接 + "tiebaclient!!!")
    {
        // 用一组固定参数，手工算一遍
        QList<tieba::KeyValue> fixed;
        fixed.append(qMakePair(QString("b"), QString("2")));
        fixed.append(qMakePair(QString("_client_version"), QString("7.2.0.0")));
        fixed.append(qMakePair(QString("a"), QString("1")));

        // 排序后：_client_version=7.2.0.0&a=1&b=2
        const QString raw = tieba::TbString::sortedRawString(fixed);
        checkStr("\u56FA\u5B9A\u53C2\u6570\u6392\u5E8F", raw, "_client_version=7.2.0.0&a=1&b=2");

        const QString expected =
            tieba::TbMd5::hex((raw + QLatin1String("tiebaclient!!!")).toUtf8());
        checkStr("sign \u4E0E\u624B\u5DE5\u8BA1\u7B97\u4E00\u81F4", tieba::TbSigner::sign(fixed), expected);
    }

    // 表单体：应包含 sign，且参数顺序与排序一致
    const QByteArray form = tieba::TbSigner::buildSignedForm(params);
    checkBool("\u8868\u5355\u4F53\u542B sign=", form.contains("sign="), true);
    checkBool("\u8868\u5355\u4F53\u5DF2\u767E\u5206\u53F7\u7F16\u7801 BDUSS",
              form.contains("BDUSS=" + session.bduss().toUtf8()), true);

    // 没有 _client_version 时不应签名（官方客户端的行为）
    QList<tieba::KeyValue> noVersion;
    noVersion.append(qMakePair(QString("k"), QString("v")));
    checkBool("\u7F3A _client_version \u65F6\u4E0D\u7B7E\u540D",
              tieba::TbSigner::sign(noVersion).isEmpty(), true);

    // 签名必须对参数顺序不敏感（因为内部会排序）
    QList<tieba::KeyValue> reordered;
    reordered.append(qMakePair(QString("a"), QString("1")));
    reordered.append(qMakePair(QString("_client_version"), QString("7.2.0.0")));
    reordered.append(qMakePair(QString("b"), QString("2")));
    checkStr("\u53C2\u6570\u987A\u5E8F\u4E0D\u5F71\u54CD\u7B7E\u540D",
             tieba::TbSigner::sign(reordered),
             tieba::TbSigner::sign(
                 QList<tieba::KeyValue>()
                 << qMakePair(QString("_client_version"), QString("7.2.0.0"))
                 << qMakePair(QString("a"), QString("1"))
                 << qMakePair(QString("b"), QString("2"))));
}

/* ==================== 会话 ==================== */
static void testSession()
{
    printf("\n== TbSession cookie \u89E3\u6790 ==\n");

    // 完整 cookie 串
    const QString full =
        "BAIDUID=ABC123:FG=1; BDUSS=0123456789012345678901234567890123456789"
        "012345678901234567890123456789012345678901234567890123; STOKEN=deadbeef; "
        "PSTM=1700000000";

    checkStr("\u4ECE cookie \u4E32\u53D6 BDUSS",
             tieba::TbSession::extractCookieValue(full, QLatin1String("BDUSS")),
             QString("0123456789012345678901234567890123456789"
                     "012345678901234567890123456789012345678901234567890123"));
    checkStr("\u4ECE cookie \u4E32\u53D6 STOKEN",
             tieba::TbSession::extractCookieValue(full, QLatin1String("STOKEN")),
             "deadbeef");
    checkStr("\u5B57\u6BB5\u540D\u4E0D\u533A\u5206\u5927\u5C0F\u5199",
             tieba::TbSession::extractCookieValue(full, QLatin1String("bduss")),
             QString("0123456789012345678901234567890123456789"
                     "012345678901234567890123456789012345678901234567890123"));
    checkStr("\u4E0D\u5B58\u5728\u7684\u5B57\u6BB5\u8FD4\u56DE\u7A7A",
             tieba::TbSession::extractCookieValue(full, QLatin1String("NOPE")),
             "");

    // 请求参数形式
    checkStr("\u4ECE k=v&k2=v2 \u5F62\u5F0F\u53D6",
             tieba::TbSession::extractCookieValue(
                 QLatin1String("foo=1&BDUSS=bbb&bar=2"), QLatin1String("BDUSS")),
             "bbb");

    // BDUSS 合法性判断
    checkBool("\u6B63\u5E38\u957F\u5EA6\u7684 BDUSS \u901A\u8FC7",
              tieba::TbSession::looksLikeBduss(
                  QByteArray(120, 'A').constData() ? QString(120, QLatin1Char('A')) : QString()),
              true);
    checkBool("\u592A\u77ED\u7684\u4E0D\u901A\u8FC7",
              tieba::TbSession::looksLikeBduss("abc"), false);
    checkBool("\u542B\u975E\u6CD5\u5B57\u7B26\u7684\u4E0D\u901A\u8FC7",
              tieba::TbSession::looksLikeBduss(QString(120, QLatin1Char('A'))
                                               + QLatin1String(" \u4E2D\u6587")),
              false);

    // 完整登录流程
    tieba::TbSession session;
    checkBool("\u521D\u59CB\u672A\u767B\u5F55", session.isLoggedIn(), false);

    const bool ok = session.setCredentials(full);
    checkBool("\u8BBE\u7F6E\u51ED\u8BC1\u6210\u529F", ok, true);
    checkBool("\u5DF2\u767B\u5F55", session.isLoggedIn(), true);
    checkBool("\u62BD\u53D6\u5230 stoken", session.stoken() == QLatin1String("deadbeef"), true);
    checkBool("\u9700\u8981\u62C9\u53D6\u8D44\u6599", session.needsProfileRefresh(), true);

    session.setProfile(QLatin1String("42"), QLatin1String("opuser"),
                       QString::fromUtf8("\u6635\u79F0"), QLatin1String("p1"));
    checkBool("\u586B\u5B8C\u8D44\u6599\u540E\u4E0D\u518D\u9700\u8981\u5237\u65B0", session.needsProfileRefresh(), false);
    checkStr("uid", session.uid(), "42");

    // 只给裸值也能用
    tieba::TbSession session2;
    checkBool("\u88F8 BDUSS \u503C\u53EF\u63A5\u53D7",
              session2.setCredentials(QString(120, QLatin1Char('B'))), true);

    // 明显不是 BDUSS 的输入应被拒绝
    tieba::TbSession session3;
    checkBool("\u4E71\u8F93\u5165\u88AB\u62D2\u7EDD", session3.setCredentials(QLatin1String("hello")), false);
    checkBool("\u88AB\u62D2\u7EDD\u540E\u4ECD\u672A\u767B\u5F55", session3.isLoggedIn(), false);

    session.logout();
    checkBool("\u767B\u51FA\u540E\u672A\u767B\u5F55", session.isLoggedIn(), false);
}

/* ==================== toVariantMap（交给 QML 的形式） ==================== */
static void testVariantMaps()
{
    printf("\n== toVariantMap\uFF1AQML ListItemData \u4F9D\u8D56\u7684\u952E\u540D ==\n");

    // 贴子：字段最多，重点核对
    tieba::TbPost *post = new tieba::TbPost();
    post->setId(QLatin1String("9001"));
    post->setFloor(QLatin1String("3"));
    post->setTimeText(QLatin1String("10-01 12:00"));
    post->setAgreeCount(7);
    post->setSubPostCount(2);
    post->setIsOriginalPoster(true);

    tieba::TbUser *author = new tieba::TbUser();
    author->setId(QLatin1String("42"));
    author->setName(QLatin1String("op"));
    author->setNameShow(QString::fromUtf8("\u697C\u4E3B"));
    author->setAvatarUrl(QLatin1String("http://a/b.png"));
    author->setLevel(6);
    author->setIsBawu(true);
    author->setBawuType(QLatin1String("manager"));
    post->setAuthor(author);

    tieba::TbContentSeg *seg = new tieba::TbContentSeg();
    seg->setType(0);
    seg->setText(QString::fromUtf8("\u6B63\u6587"));
    QVariantList segments;
    segments.append(seg->toVariantMap());
    delete seg;
    post->setContentSegments(segments);

    const QVariantMap map = post->toVariantMap();

    // 这些键名是 main.qml / 你以后写的 UI 会直接写的，必须存在
    checkStr("id", map.value("id").toString(), "9001");
    checkStr("floor", map.value("floor").toString(), "3");
    checkInt("floorNumber", map.value("floorNumber").toInt(), 3);
    checkStr("timeText", map.value("timeText").toString(), "10-01 12:00");
    checkInt("agreeCount", map.value("agreeCount").toInt(), 7);
    checkInt("subPostCount", map.value("subPostCount").toInt(), 2);
    checkBool("isOriginalPoster", map.value("isOriginalPoster").toBool(), true);
    // plainText 由 setContentSegments 自动拼好
    checkStr("plainText \u81EA\u52A8\u751F\u6210", map.value("plainText").toString(),
             QString::fromUtf8("\u6B63\u6587"));
    checkStr("authorName\uFF08\u6241\u5E73\uFF09", map.value("authorName").toString(),
             QString::fromUtf8("\u697C\u4E3B"));
    checkStr("authorAvatarUrl\uFF08\u6241\u5E73\uFF09", map.value("authorAvatarUrl").toString(),
             "http://a/b.png");
    checkInt("authorLevel\uFF08\u6241\u5E73\uFF09", map.value("authorLevel").toInt(), 6);
    checkBool("authorIsBawu\uFF08\u6241\u5E73\uFF09", map.value("authorIsBawu").toBool(), true);
    checkStr("authorLabel\uFF08\u6241\u5E73\uFF09", map.value("authorLabel").toString(),
             QString::fromUtf8("\u5427\u4E3B"));

    // 嵌套：author 与 contentSegments 都是可继续取值的 map
    const QVariantMap authorMap = map.value("author").toMap();
    checkStr("author.displayName", authorMap.value("displayName").toString(),
             QString::fromUtf8("\u697C\u4E3B"));
    checkInt("author.level", authorMap.value("level").toInt(), 6);

    const QVariantList segs = map.value("contentSegments").toList();
    checkInt("contentSegments \u6BB5\u6570", segs.size(), 1);
    checkStr("\u6BB5\u843D kind", segs.value(0).toMap().value("kind").toString(), "text");
    checkStr("\u6BB5\u843D text", segs.value(0).toMap().value("text").toString(),
             QString::fromUtf8("\u6B63\u6587"));

    // 关键是这个：subPosts() 也要能给 QML 取到值（曾经这里是 TbPost*，toMap 是空）
    tieba::TbPost *sub = new tieba::TbPost();
    sub->setId(QLatin1String("9002"));
    QVariantList subPointers;
    subPointers.append(QVariant::fromValue(sub));
    post->setSubPosts(subPointers);

    const QVariantList subMaps = post->subPosts();
    checkInt("subPosts \u8FD4\u56DE map \u5217\u8868", subMaps.size(), 1);
    checkStr("subPosts[0].id", subMaps.value(0).toMap().value("id").toString(), "9002");
    checkInt("subPostPreview \u6761\u6570", post->subPostPreview().size(), 1);
    checkInt("children() \u4ECD\u80FD\u62FF\u539F\u59CB\u6307\u9488", post->children().size(), 1);

    delete sub;
    delete author;
    delete post;

    // 贴子列表项
    tieba::TbThread *thread = new tieba::TbThread();
    thread->setId(QLatin1String("123"));
    thread->setTitle(QString::fromUtf8("\u6807\u9898"));
    thread->setReplyCount(45678);
    thread->setIsGood(true);
    const QVariantMap tmap = thread->toVariantMap();
    checkStr("thread.id", tmap.value("id").toString(), "123");
    checkStr("thread.title", tmap.value("title").toString(), QString::fromUtf8("\u6807\u9898"));
    checkStr("thread.replyCountText\uFF08\u4E07\u7EA7\uFF09", tmap.value("replyCountText").toString(), "4.6W");
    checkBool("thread.isGood", tmap.value("isGood").toBool(), true);
    checkBool("thread.hasImage", tmap.value("hasImage").toBool(), false);
    delete thread;

    // 吧列表项
    tieba::TbForum *forum = new tieba::TbForum();
    forum->setId(QLatin1String("23080"));
    forum->setName(QString::fromUtf8("\u9ED1\u8309\u8389"));
    forum->setLevel(7);
    forum->setSigned(true);
    const QVariantMap fmap = forum->toVariantMap();
    checkStr("forum.name", fmap.value("name").toString(), QString::fromUtf8("\u9ED1\u8309\u8389"));
    checkInt("forum.level", fmap.value("level").toInt(), 7);
    checkBool("forum.isSigned", fmap.value("isSigned").toBool(), true);
    delete forum;

    // 吧信息 / 分页 / 主题贴信息
    tieba::TbForumInfo *fi = new tieba::TbForumInfo();
    fi->setName(QString::fromUtf8("\u9ED1\u8309\u8389"));
    fi->setUserLevel(3);
    fi->setMemberCountText(QLatin1String("1.2W"));
    const QVariantMap fimap = fi->toVariantMap();
    checkStr("forumInfo.name", fimap.value("name").toString(), QString::fromUtf8("\u9ED1\u8309\u8389"));
    checkInt("forumInfo.userLevel", fimap.value("userLevel").toInt(), 3);
    delete fi;

    tieba::TbPageInfo *pi = new tieba::TbPageInfo();
    pi->setCurrentPage(2);
    pi->setHasMore(true);
    const QVariantMap pimap = pi->toVariantMap();
    checkInt("pageInfo.currentPage", pimap.value("currentPage").toInt(), 2);
    checkBool("pageInfo.hasMore", pimap.value("hasMore").toBool(), true);
    delete pi;

    tieba::TbThreadInfo *ti = new tieba::TbThreadInfo();
    ti->setId(QLatin1String("555"));
    ti->setTitle(QString::fromUtf8("\u4E3B\u9898"));
    tieba::TbUser *ta = new tieba::TbUser();
    ta->setNameShow(QString::fromUtf8("\u697C\u4E3B"));
    ti->setAuthor(ta);
    const QVariantMap timap = ti->toVariantMap();
    checkStr("threadInfo.id", timap.value("id").toString(), "555");
    checkStr("threadInfo.authorName", timap.value("authorName").toString(),
             QString::fromUtf8("\u697C\u4E3B"));
    delete ta;
    delete ti;
}

/* ============================================================================
 * PC 端解析器（TbPcParsers）—— 用真实抓下来的响应做回归
 *
 * tests/data/pc_frs.json 和 pc_pb.json 是从线上接口实际抓取的完整响应：
 *     POST /c/f/frs/page_pc   （吧页，505KB）
 *     POST /c/f/pb/page_pc    （帖子楼层，100KB）
 *
 * 为什么必须用真实响应而不是手写 fixture：
 *   PC 接口返回的是**组件化**结构，字段藏在多层里，而且有若干"二次转义"的
 *   JSON 字符串（吧头衔就在 custom.data 里）。手写 fixture 很容易按自己的
 *   臆想写，测过了线上却不通 —— 这两个文件踩过的坑都在这上面。
 * ============================================================================ */

static QByteArray readFixture(const QString &name)
{
    // 测试可执行文件在 tests/build/ 下，样本在 tests/data/
    const QStringList candidates = QStringList()
        << QString("data/") + name
        << QString("../data/") + name
        << QString("../../tests/data/") + name
        << QString("tests/data/") + name;
    for (int i = 0; i < candidates.size(); ++i) {
        QFile f(candidates.at(i));
        if (f.exists() && f.open(QIODevice::ReadOnly))
            return f.readAll();
    }
    return QByteArray();
}

/*
 * /c/f/forum/like —— "我关注的吧"完整列表（用真实响应 pc_forumlike.json）。
 *
 * 为什么要这个接口：旧的 forumrecommend 实测有 **99 条上限**（返回 99 条就停，
 * 最后一条是 freebsd），关注更多的用户后面的吧加载不出来。
 *
 * 这个接口字段名不同（id/name，不是 forum_id/forum_name），结构也不同
 * （吧在 forum_list.non-gconforum[] 和 forum_list.gconforum[] 两个数组里），
 * 所以有独立的解析器和这条回归。
 */
static void testPcForumLike()
{
    printf("\n--- TbParsers: 关注列表 /c/f/forum/like（真实响应）---\n");

    const QByteArray body = readFixture(QLatin1String("pc_forumlike.json"));
    if (body.isEmpty()) {
        printf("  SKIP 找不到 tests/data/pc_forumlike.json\n");
        return;
    }

    const tieba::TbJson root = tieba::TbJson::parse(body);
    checkBool("forumlike.json 可解析", root.isValid(), true);
    if (!root.isValid())
        return;

    const tieba::TbForumListResult r = tieba::TbParsers::parseForumLike(root);

    checkBool("forumlike 无错误码",
              r.errorCode.isEmpty() || r.errorCode == QLatin1String("0"), true);

    /*
     * ★ 关键断言：**必须多于 99 条**。
     *
     * 这正是换接口的原因 —— 旧接口卡在 99 条。如果这里退回到 <=99，
     * 说明解析又丢了数组（比如只收了 non-gconforum 没收 gconforum）。
     * 样本里 non-gconforum 50 条 + gconforum 0 条 = 50，
     * 所以这条断言在**单页样本**上要写成"等于样本条数"，
     * 而"能超过 99"由下面的跨页累加保证。
     */
    const int nonGcon = tieba::TbJson::array(
        tieba::TbJson::object(root, QLatin1String("forum_list")),
        QLatin1String("non-gconforum")).size();
    const int gcon = tieba::TbJson::array(
        tieba::TbJson::object(root, QLatin1String("forum_list")),
        QLatin1String("gconforum")).size();

    checkInt("forumlike 收到 non-gconforum 全部条目", r.forums.size(), nonGcon + gcon);
    checkBool("forumlike 样本非空", r.forums.size() > 0, true);

    // has_more 要能带出来（控制器靠它翻页）
    checkBool("forumlike hasMore 字段可读",
              r.hasMore == (tieba::TbJson::integer(root, QLatin1String("has_more"), 0) != 0),
              true);

    // 每条都必须有名字和 id（字段名是 name/id，不是 forum_name/forum_id）
    int withName = 0;
    int withId = 0;
    int withAvatar = 0;
    int withLevel = 0;
    for (int i = 0; i < r.forums.size(); ++i) {
        const tieba::TbForum *f = r.forums.at(i);
        if (!f->name().isEmpty())     ++withName;
        if (!f->id().isEmpty())       ++withId;
        if (!f->avatarUrl().isEmpty()) ++withAvatar;
        if (f->level() > 0)           ++withLevel;
    }
    const int n = r.forums.size();
    checkInt("forumlike 每条都有吧名", withName, n);
    checkInt("forumlike 每条都有 id", withId, n);
    checkInt("forumlike 每条都有头像", withAvatar, n);
    checkInt("forumlike 每条都有等级", withLevel, n);

    // 第一条是"黑莓"（这个账号关注列表的第一项，抓样本时确认过）
    if (n > 0) {
        checkStr("forumlike 第一个吧名", r.forums.at(0)->name(),
                 QString::fromUtf8("\u9ED1\u8393"));
        const QVariantMap m = r.forums.at(0)->toVariantMap();
        checkBool("forumlike toVariantMap 含 name",
                  m.contains(QLatin1String("name")), true);
        checkBool("forumlike toVariantMap 含 level",
                  m.contains(QLatin1String("level")), true);
    }

    qDeleteAll(r.forums);
}

static void testPcForumPage()
{    printf("\n--- TbPcParsers: 吧页（真实响应）---\n");

    const QByteArray body = readFixture(QLatin1String("pc_frs.json"));
    if (body.isEmpty()) {
        printf("  SKIP 找不到 tests/data/pc_frs.json\n");
        return;
    }

    const tieba::TbJson root = tieba::TbJson::parse(body);
    checkBool("pc.frs.json 可解析", root.isValid(), true);
    if (!root.isValid())
        return;

    const tieba::TbPcForumPageResult r = tieba::TbPcParsers::parseForumPage(root);

    checkBool("pc.forumPage 无错误码", r.errorCode.isEmpty()
              || r.errorCode == QLatin1String("0"), true);
    checkBool("pc.forumPage 有吧信息", r.forumInfo != 0, true);
    checkBool("pc.forumPage 有分页信息", r.pageInfo != 0, true);

    if (r.forumInfo) {
        checkStr("pc.forumInfo.name", r.forumInfo->name(), QLatin1String("deepseek"));
        checkBool("pc.forumInfo.userLevel > 0", r.forumInfo->userLevel() > 0, true);
        checkBool("pc.forumInfo.memberCountText 非空",
                  !r.forumInfo->memberCountText().isEmpty(), true);
        checkBool("pc.forumInfo.threadCountText 非空",
                  !r.forumInfo->threadCountText().isEmpty(), true);
        checkBool("pc.forumInfo.slogan 非空", !r.forumInfo->slogan().isEmpty(), true);
        checkBool("pc.forumInfo.avatarUrl 非空", !r.forumInfo->avatarUrl().isEmpty(), true);

        /*
         * 吧详情页要的字段（用真实响应 pc_frs.json 验证）。
         * 这几个字段服务端确实返回了，所以要确保解析器真的取到了 ——
         * 漏一个吧详情页就会显示 "-"。
         */
        checkBool("pc.forumInfo.postCountText 非空",
                  !r.forumInfo->postCountText().isEmpty(), true);
        checkBool("pc.forumInfo.score > 0", r.forumInfo->score() > 0, true);
        checkBool("pc.forumInfo.levelupScore > 0", r.forumInfo->levelupScore() > 0, true);
        checkBool("pc.forumInfo 的经验值不超过升级线",
                  r.forumInfo->score() <= r.forumInfo->levelupScore(), true);
    }
    if (r.pageInfo) {
        checkInt("pc.pageInfo.currentPage", r.pageInfo->currentPage(), 1);
        checkBool("pc.pageInfo.totalPage > 0", r.pageInfo->totalPage() > 0, true);
        checkBool("pc.pageInfo.totalCount > 0", r.pageInfo->totalCount() > 0, true);
        checkBool("pc.pageInfo.hasMore", r.pageInfo->hasMore(), true);
    }

    // 实测一页 30 条（另有 1 条置顶卡片会被跳过）
    checkBool("pc.forumPage 解析出贴子", r.threads.size() >= 20, true);

    // 逐条检查关键字段：标题/作者/头衔/时间/头像 都必须有值。
    // 这几项都曾经因为取错嵌套层级而整片为空，是回归重点。
    int withTitle = 0;
    int withAuthor = 0;
    int withLabel = 0;
    int withTime = 0;
    int withAvatar = 0;
    int withId = 0;
    int withReplyText = 0;
    for (int i = 0; i < r.threads.size(); ++i) {
        const tieba::TbThread *t = r.threads.at(i);
        if (!t->title().isEmpty())        ++withTitle;
        if (!t->authorName().isEmpty())   ++withAuthor;
        if (!t->authorLabel().isEmpty())  ++withLabel;
        if (!t->lastTimeText().isEmpty()) ++withTime;
        if (!t->avatarUrl().isEmpty())    ++withAvatar;
        if (!t->id().isEmpty())           ++withId;
        if (!t->replyCountText().isEmpty()) ++withReplyText;
    }
    const int n = r.threads.size();
    checkInt("pc.每条都有标题", withTitle, n);
    checkInt("pc.每条都有作者名", withAuthor, n);
    checkInt("pc.每条都有 tid", withId, n);
    checkInt("pc.每条都有时间", withTime, n);
    checkInt("pc.每条都有头像", withAvatar, n);
    checkInt("pc.每条都有回复数文本", withReplyText, n);
    // 吧头衔不是每个吧都配，缺失是正常的，只要求大部分有
    checkBool("pc.多数有吧头衔", withLabel >= n / 2, true);

    /*
     * ★ 精华标记与浏览数（新增解析，之前整块漏掉了）
     *
     * 背景：PC 版把这些字段放在 feed.business_info_map 里，而解析器
     * **完全没读** —— 结果即使服务端按 is_good=1 筛出了精品贴，前端也不标记，
     * 界面上看起来就是"精华筛选不生效"。
     * 这两个计数器就是防止再次漏读的。
     */
    int withViewText = 0;
    int goodCount = 0;
    for (int i = 0; i < r.threads.size(); ++i) {
        const tieba::TbThread *t = r.threads.at(i);
        if (!t->viewCountText().isEmpty()) ++withViewText;
        if (t->isGood())                   ++goodCount;
    }
    // business_info_map.view_num 每条都有，所以浏览数文本应该每条都有
    checkInt("pc.每条都有浏览数文本", withViewText, n);
    // 这个样本（普通最新列表）里恰好有 1 条精品，能证明 is_good 真的读到了
    checkInt("pc.精品贴数量与真实响应一致", goodCount, 1);

    // toVariantMap 里也要带上，界面才能显示"精"标记
    if (n > 0) {
        const QVariantMap m0 = r.threads.at(0)->toVariantMap();
        checkBool("pc.toVariantMap 含 isGood",
                  m0.contains(QLatin1String("isGood")), true);
        checkBool("pc.toVariantMap 含 viewCountText",
                  m0.contains(QLatin1String("viewCountText")), true);
    }

    // 第一条的 id 必须是纯数字（能当 tid 用）
    if (n > 0) {
        const QString firstId = r.threads.at(0)->id();
        bool ok = false;
        firstId.toLongLong(&ok);
        checkBool("pc.首个 tid 是数字", ok && !firstId.isEmpty(), true);

        // toVariantMap 必须带上 authorLabel（新增字段，UI 要用）
        const QVariantMap m = r.threads.at(0)->toVariantMap();
        checkBool("pc.toVariantMap 含 authorLabel",
                  m.contains(QLatin1String("authorLabel")), true);
        checkBool("pc.toVariantMap 含 title",
                  m.contains(QLatin1String("title")), true);
    }

    qDeleteAll(r.threads);
    delete r.forumInfo;
    delete r.pageInfo;
}

static void testPcThread()
{
    printf("\n--- TbPcParsers: 帖子楼层（真实响应）---\n");

    const QByteArray body = readFixture(QLatin1String("pc_pb.json"));
    if (body.isEmpty()) {
        printf("  SKIP 找不到 tests/data/pc_pb.json\n");
        return;
    }

    const tieba::TbJson root = tieba::TbJson::parse(body);
    checkBool("pc.pb.json 可解析", root.isValid(), true);
    if (!root.isValid())
        return;

    const tieba::TbPcThreadResult r = tieba::TbPcParsers::parseThread(root);

    checkBool("pc.thread 无错误码", r.errorCode.isEmpty()
              || r.errorCode == QLatin1String("0"), true);
    checkBool("pc.thread 有主题信息", r.threadInfo != 0, true);
    checkBool("pc.thread 有分页信息", r.pageInfo != 0, true);
    checkBool("pc.thread 解析出楼层", r.posts.size() > 0, true);

    if (r.threadInfo) {
        checkBool("pc.threadInfo.title 非空",
                  !r.threadInfo->title().isEmpty(), true);
        checkBool("pc.threadInfo.id 非空", !r.threadInfo->id().isEmpty(), true);
    }

    // 楼层重点：作者来自 user_list 查表（post_list 里只有 author_id），
    // 正文的 content[].type 是整数而不是字符串 —— 这两点都踩过。
    int withAuthor = 0;
    int withLevel = 0;
    int withTime = 0;
    int withText = 0;
    for (int i = 0; i < r.posts.size(); ++i) {
        const tieba::TbPost *p = r.posts.at(i);
        if (!p->authorName().isEmpty()) ++withAuthor;
        if (p->authorLevel() > 0)       ++withLevel;
        if (!p->timeText().isEmpty())   ++withTime;
        if (!p->plainText().isEmpty())  ++withText;
    }
    const int n = r.posts.size();
    checkInt("pc.每层都有作者名", withAuthor, n);
    checkInt("pc.每层都有时间", withTime, n);
    checkInt("pc.每层都有正文", withText, n);
    checkBool("pc.多数楼层有等级", withLevel >= n / 2, true);

    /*
     * ---- 楼主标记（isOriginalPoster）----
     *
     * 这是"楼主图标不显示"的修复点：PC 的解析器**从来没设过这个字段**
     * （移动版 TbParsers 里有，PC 版漏了）。
     *
     * 服务端**不提供**"这层是不是楼主"的字段 —— 实测 first_floor 和 post_list[]
     * 的键完全一样，没有 is_lz 之类。所以只能拿每层的 author_id 和 1 楼的比对。
     *
     * 真实样本里 1 楼作者 = post_list[0] 作者（都是同一个人，说明只有 1 楼是楼主，
     * 其余楼层不是），正好能验证判定是**按作者**而不是"全部标记 true"。
     */
    int opCount = 0;
    for (int i = 0; i < r.posts.size(); ++i) {
        if (r.posts.at(i)->isOriginalPoster())
            ++opCount;
    }
    checkBool("pc.有楼层被标为楼主", opCount > 0, true);
    checkBool("pc.不是所有楼层都是楼主", opCount < n, true);
    if (n > 0) {
        checkBool("pc.1 楼一定是楼主", r.posts.at(0)->isOriginalPoster(), true);
        // toVariantMap 要带出去，QML 才能显示楼主标
        checkBool("pc.toVariantMap 含 isOriginalPoster",
                  r.posts.at(0)->toVariantMap().contains(
                      QLatin1String("isOriginalPoster")), true);
    }

    // 正文段落必须用 setContentSegments 认得的 kind
    if (n > 0) {
        const QVariantList segs = r.posts.at(0)->contentSegments();
        checkBool("pc.首层有段落", !segs.isEmpty(), true);
        if (!segs.isEmpty()) {
            const QString kind = segs.at(0).toMap().value(QLatin1String("kind")).toString();
            checkBool("pc.段落 kind 非空", !kind.isEmpty(), true);
        }
    }

    /*
     * ---- 贴吧表情（emoticonUrl 映射表）----
     *
     * 贴吧正文里的表情只给**名字**（c 字段），图片地址要自己拼：
     *     http://static.tieba.baidu.com/tb/editor/images/client/image_emoticon<N>.png
     * 名字 -> N 的表有 60 项，手工抄的，最容易错，所以逐个验证几个。
     *
     * 真实样本 pc_pb.json 里的表情是：吐舌、喷、小乖、狂汗、阴险
     * （小乖 是服务端叫法，表里对应"乖"）。
     */
    checkStr("emoticonUrl 呵呵 -> 1",
             tieba::TbPcParsers::emoticonUrl(QString::fromUtf8("\u5475\u5475")),
             QLatin1String("https://static.tieba.baidu.com/tb/editor/images/client/image_emoticon1.png"));
    checkStr("emoticonUrl 滑稽 -> 25",
             tieba::TbPcParsers::emoticonUrl(QString::fromUtf8("\u6ED1\u7A3D")),
             QLatin1String("https://static.tieba.baidu.com/tb/editor/images/client/image_emoticon25.png"));
    checkStr("emoticonUrl OK -> 50",
             tieba::TbPcParsers::emoticonUrl(QLatin1String("OK")),
             QLatin1String("https://static.tieba.baidu.com/tb/editor/images/client/image_emoticon50.png"));
    checkStr("emoticonUrl 三道杠 -> 84（表尾）",
             tieba::TbPcParsers::emoticonUrl(QString::fromUtf8("\u4E09\u9053\u6760")),
             QLatin1String("https://static.tieba.baidu.com/tb/editor/images/client/image_emoticon84.png"));
    // 生气在安卓表里出现两次（31 和 61），后面的覆盖前面的 -> 61
    checkStr("emoticonUrl 生气 -> 61（后者覆盖）",
             tieba::TbPcParsers::emoticonUrl(QString::fromUtf8("\u751F\u6C14")),
             QLatin1String("https://static.tieba.baidu.com/tb/editor/images/client/image_emoticon61.png"));
    // 查不到的名字要返回空串，而不是拼出个坏地址
    checkStr("emoticonUrl 不存在的名字 -> 空",
             tieba::TbPcParsers::emoticonUrl(QString::fromUtf8("\u6CA1\u6709\u8FD9\u4E2A\u8868\u60C5")),
             QString());

    /*
     * 解析真实响应时，表情段必须拿到 url（这是"表情不显示"的修复点）。
     * 之前 case 2 只取了 name/c，**url 一直是空的**。
     *
     * 说明：这个样本里的表情大多在**楼中楼**（sub_post_list）里，
     * 顶层楼层只有一个（服务端叫"小乖"）。所以这里扫全部楼层与它们的
     * 楼中楼，要求"凡是表情段就有 url"；只要样本里至少有一个表情段，
     * 就证明这条链路是通的。
     */
    {
        int emoSegs = 0;
        int emoWithUrl = 0;

        // 把一层楼和它的楼中楼都算上
        QList<tieba::TbPost *> allPosts;
        for (int i = 0; i < r.posts.size(); ++i) {
            allPosts.append(r.posts.at(i));
            const QVariantList subs = r.posts.at(i)->subPosts();
            for (int k = 0; k < subs.size(); ++k) {
                tieba::TbPost *sp = subs.at(k).value<tieba::TbPost *>();
                if (sp)
                    allPosts.append(sp);
            }
        }

        for (int i = 0; i < allPosts.size(); ++i) {
            const QVariantList segs = allPosts.at(i)->contentSegments();
            for (int k = 0; k < segs.size(); ++k) {
                const QVariantMap sg = segs.at(k).toMap();
                if (sg.value(QLatin1String("kind")).toString() != QLatin1String("emoticon"))
                    continue;
                ++emoSegs;
                if (!sg.value(QLatin1String("url")).toString().isEmpty())
                    ++emoWithUrl;
            }
        }
        checkInt("pc.emoticon segments with url", emoWithUrl, emoSegs);
    }

    /* ---- 1 楼（first_floor）----
     * ★ 1 楼不在 post_list 里，是单独的 first_floor 对象。
     *   不解析它，楼主那层正文就永远不显示。 */
    if (n > 0) {
        checkStr("pc.首层是 1 楼", r.posts.at(0)->floor(), QLatin1String("1"));
        checkBool("pc.1 楼有正文", !r.posts.at(0)->plainText().isEmpty(), true);
        checkBool("pc.1 楼有作者", !r.posts.at(0)->authorName().isEmpty(), true);
    }

    /*
     * ★★ 每一层都要有**有效楼层号**。
     *
     * 这条是补的：以前只断言了"首层是 1 楼"，没管其余楼层 ——
     * 所以"除 1 楼外全变成 0 楼"这类 bug 测试**抓不到**。
     *
     * 真实响应里 post_list[] 的 floor 是**整数**（2,4,5,6...），
     * 解析出来必须是这些数字，不能是 0。
     */
    {
        int validFloors = 0;
        QStringList badOnes;
        QStringList allFloors;
        for (int i = 0; i < n; ++i) {
            const int f = r.posts.at(i)->floorNumber();
            if (allFloors.size() < 20)
                allFloors.append(QString::number(f));
            if (f > 0)
                ++validFloors;
            else if (badOnes.size() < 5)
                badOnes.append(QString::fromUtf8("idx%1/id=%2")
                               .arg(i).arg(r.posts.at(i)->id()));
        }
        printf("     实际楼层号: [%s]\n", qPrintable(allFloors.join(", ")));
        checkInt("pc.每层都有有效楼层号(>0)", validFloors, n);
        if (!badOnes.isEmpty())
            printf("     floor 为 0 的项: %s\n", qPrintable(badOnes.join(", ")));

        // 1 楼之后应该是 2 楼（pc_pb.json 里 post_list[0].floor == 2）
        if (n > 1)
            checkInt("pc.第 2 项是 2 楼", r.posts.at(1)->floorNumber(), 2);
    }

    /*
     * ★★ 楼层号缺失时的兜底（治"除 1 楼外全显示 0 楼"）。
     *
     * 有些贴子的响应里 post_list[] **没有 floor 字段**（或为 0），
     * 那时只能按顺序推算：1 楼之后是 2,3,4...
     *
     * 这个用例构造一份**故意不带 floor** 的响应，验证：
     *   1) 不再出现 0 楼
     *   2) 推算出的楼层号是 2,3,4
     *   3) 有 floor 时**不会被覆盖**（上面那个用例已经覆盖这点）
     */
    {
        static const char *kNoFloorJson =
            "{"
            "  \"error_code\":\"0\","
            "  \"thread\":{\"id\":\"555\",\"title\":\"no floor\"},"
            "  \"page\":{\"current_page\":1,\"has_more\":0},"
            "  \"first_floor\":{\"id\":\"555001\",\"floor\":1,\"author_id\":\"a1\","
            "                 \"time\":1700000000,"
            "                 \"content\":[{\"type\":0,\"text\":\"one\"}]},"
            "  \"post_list\":["
            "    {\"id\":\"555002\",\"author_id\":\"a2\",\"time\":1700000100,"
            "     \"content\":[{\"type\":0,\"text\":\"two\"}]},"
            "    {\"id\":\"555003\",\"author_id\":\"a3\",\"time\":1700000200,"
            "     \"content\":[{\"type\":0,\"text\":\"three\"}]},"
            "    {\"id\":\"555004\",\"author_id\":\"a4\",\"time\":1700000300,"
            "     \"content\":[{\"type\":0,\"text\":\"four\"}]}"
            "  ]"
            "}";

        const tieba::TbJson nfRoot =
            tieba::TbJson::parse(QByteArray(kNoFloorJson));
        const tieba::TbPcThreadResult nf = tieba::TbPcParsers::parseThread(nfRoot);

        checkInt("pc.no-floor.楼层数", nf.posts.size(), 4);
        if (nf.posts.size() == 4) {
            checkInt("pc.no-floor.1 楼", nf.posts.at(0)->floorNumber(), 1);
            checkInt("pc.no-floor.缺 floor 时推算第 2 层", nf.posts.at(1)->floorNumber(), 2);
            checkInt("pc.no-floor.缺 floor 时推算第 3 层", nf.posts.at(2)->floorNumber(), 3);
            checkInt("pc.no-floor.缺 floor 时推算第 4 层", nf.posts.at(3)->floorNumber(), 4);

            int zero = 0;
            for (int i = 0; i < nf.posts.size(); ++i)
                if (nf.posts.at(i)->floorNumber() <= 0)
                    ++zero;
            checkInt("pc.no-floor.没有 0 楼", zero, 0);
        }
        qDeleteAll(nf.posts);
    }

    /* ---- 正文里的图片 ----
     * ★ 图片字段是 origin_src / cdn_src，不是 origin_pic_url。
     *   找错字段会让整篇贴子的图片都拿不到地址。 */
    int imageSegs = 0;
    int imageWithUrl = 0;
    int imageWithSize = 0;
    for (int i = 0; i < n; ++i) {
        const QVariantList segs = r.posts.at(i)->contentSegments();
        for (int k = 0; k < segs.size(); ++k) {
            const QVariantMap m = segs.at(k).toMap();
            if (m.value(QLatin1String("kind")).toString() != QLatin1String("image"))
                continue;
            ++imageSegs;
            if (!m.value(QLatin1String("originUrl")).toString().isEmpty())
                ++imageWithUrl;
            if (m.value(QLatin1String("width")).toInt() > 0)
                ++imageWithSize;
        }
    }
    checkBool("pc.样本里确实有图片段", imageSegs > 0, true);
    checkInt("pc.每个图片段都有地址", imageWithUrl, imageSegs);
    checkInt("pc.每个图片段都有尺寸", imageWithSize, imageSegs);

    /* ---- 楼中楼预览（post_list 内嵌）---- */
    int floorsWithSubPosts = 0;
    int floorsWithPreview = 0;
    for (int i = 0; i < n; ++i) {
        const tieba::TbPost *p = r.posts.at(i);
        if (p->subPostCount() <= 0)
            continue;
        ++floorsWithSubPosts;
        if (!p->subPostPreview().isEmpty())
            ++floorsWithPreview;
        checkBool("pc.楼中楼预览不超过 3 条", p->subPostPreview().size() <= 3, true);
    }
    checkBool("pc.样本里有带楼中楼的楼层", floorsWithSubPosts > 0, true);
    checkInt("pc.带楼中楼的层都有预览", floorsWithPreview, floorsWithSubPosts);

    qDeleteAll(r.posts);
    delete r.threadInfo;
    delete r.pageInfo;
    delete r.forumInfo;
}

/*! 楼中楼（独立请求）：POST /c/f/pb/floor，必须用 PC 签名 */
static void testPcSubFloors()
{
    printf("\n--- TbPcParsers: 楼中楼（真实响应）---\n");

    const QByteArray body = readFixture(QLatin1String("pc_sub.json"));
    if (body.isEmpty()) {
        printf("  SKIP 找不到 tests/data/pc_sub.json\n");
        return;
    }

    const tieba::TbJson root = tieba::TbJson::parse(body);
    checkBool("pc.sub.json 可解析", root.isValid(), true);
    if (!root.isValid())
        return;

    const tieba::TbPcSubFloorResult r = tieba::TbPcParsers::parseSubFloors(root);

    checkBool("pc.sub 无错误码",
              r.errorCode.isEmpty() || r.errorCode == QLatin1String("0"), true);
    checkBool("pc.sub 解析出楼中楼", r.posts.size() > 0, true);
    checkBool("pc.sub 有分页信息", r.pageInfo != 0, true);

    // 字段名是 subpost_list（连写）。写成 post_list 会一条都解析不出来。
    int withAuthor = 0;
    int withText = 0;
    for (int i = 0; i < r.posts.size(); ++i) {
        const tieba::TbPost *p = r.posts.at(i);
        if (!p->authorName().isEmpty()) ++withAuthor;
        if (!p->plainText().isEmpty())  ++withText;
    }
    const int n = r.posts.size();
    checkInt("pc.每条楼中楼都有作者", withAuthor, n);
    checkInt("pc.每条楼中楼都有正文", withText, n);

    qDeleteAll(r.posts);
    delete r.pageInfo;
}

static void testPcForumList()
{
    printf("\n--- 我关注的吧（PC 签名后的真实响应）---\n");

    const QByteArray body = readFixture(QLatin1String("pc_forumlist.json"));
    if (body.isEmpty()) {
        printf("  SKIP 找不到 tests/data/pc_forumlist.json\n");
        return;
    }

    const tieba::TbJson root = tieba::TbJson::parse(body);
    checkBool("pc.forumlist.json 可解析", root.isValid(), true);
    if (!root.isValid())
        return;

    const tieba::TbForumListResult r = tieba::TbParsers::parseForumRecommend(root);

    checkBool("pc.forumList 无错误码",
              r.errorCode.isEmpty() || r.errorCode == QLatin1String("0"), true);
    checkBool("pc.forumList 解析出吧", r.forums.size() > 0, true);

    // 同一个端点：移动端签名返回 110001，PC 签名才返回数据。
    // 这里钉住「响应结构仍是 like_forum[]，旧解析器可以直接复用」这个事实。
    int withName = 0;
    int withId = 0;
    int withAvatar = 0;
    int withLevel = 0;
    for (int i = 0; i < r.forums.size(); ++i) {
        const tieba::TbForum *f = r.forums.at(i);
        if (!f->name().isEmpty())      ++withName;
        if (!f->id().isEmpty())        ++withId;
        if (!f->avatarUrl().isEmpty()) ++withAvatar;
        if (f->level() > 0)            ++withLevel;
    }
    const int n = r.forums.size();
    checkInt("pc.每个吧都有名字", withName, n);
    checkInt("pc.每个吧都有 id", withId, n);
    checkInt("pc.每个吧都有头像", withAvatar, n);
    checkBool("pc.多数吧有等级", withLevel >= n / 2, true);

    if (n > 0) {
        const QVariantMap m = r.forums.at(0)->toVariantMap();
        checkBool("pc.forum toVariantMap 含 name",
                  m.contains(QLatin1String("name")), true);
    }

    qDeleteAll(r.forums);
}

int main()
{
    SetConsoleOutputCP(65001);

    testForumRecommend();
    testForumPage();
    testThreadContent();
    testSubFloors();
    testVariantMaps();
    testErrorResponses();
    testSigner();
    testSession();
    testPcForumList();
    testPcForumPage();
    testPcForumLike();
    testPcThread();
    testPcSubFloors();

    printf("\n================================\n");
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
