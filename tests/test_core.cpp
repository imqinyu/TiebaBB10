/*
 * 本机验证用测试（不属于 BB10 工程）
 * 验证 TbJson 解析器 + TbString 编码/签名排序。
 */
#include "../src/tieba/util/TbJson.hpp"
#include "../src/tieba/util/TbString.hpp"
#include "../src/tieba/util/TbMd5.hpp"

#include <QByteArray>
#include <QRegExp>
#include <QString>
#include <QStringList>
#include <stdio.h>
#include <string.h>

static int g_failures = 0;
static int g_checks = 0;

/*! 十六进制摘要，便于在编码混乱的控制台里核对真实字节 */
static QByteArray hexOf(const QByteArray &utf8)
{
    QByteArray out;
    for (int i = 0; i < utf8.size(); ++i) {
        if (i > 0)
            out.append(' ');
        out.append(QByteArray::number((unsigned char)utf8.at(i), 16).rightJustified(2, '0'));
    }
    return out;
}

/*!
 * 字符串断言。
 *
 * 期望值提供 const char* 重载是必需的：const QString& 版本会让窄字面量走
 * Qt 的隐式 Latin-1 转换，"\u4E2D" 的 3 个 UTF-8 字节会变成 3 个 Latin-1 字符。
 * const char* 版本比较的是 UTF-8 字节（strcmp），所以 test_*.cpp 里既可以写
 * 窄字面量，也可以写 QString::fromUtf8("...")，两者都正确。
 */
static void checkStr(const QString &label, const QString &actual,
                     const QString &expected)
{
    ++g_checks;
    if (actual == expected) {
        printf("  OK   %s\n", qPrintable(label));
    } else {
        ++g_failures;
        const QByteArray a = actual.toUtf8();
        const QByteArray e = expected.toUtf8();
        printf("  FAIL %s\n", qPrintable(label));
        printf("       expected: [%s]\n", e.constData());
        printf("       actual:   [%s]\n", a.constData());
        printf("       expected hex: %s\n", hexOf(e).constData());
        printf("       actual   hex: %s\n", hexOf(a).constData());
    }
}

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

/* ------------------------------------------------------------------ */

static void testJsonBasics()
{
    printf("\n== TbJson \u57FA\u7840\u89E3\u6790 ==\n");

    const QByteArray src =
        "{\n"
        "  \"error_code\":\"0\",\n"
        "  \"error_msg\":\"\",\n"
        "  \"int_field\": 42,\n"
        "  \"neg\": -7,\n"
        "  \"real\": 3.5,\n"
        "  \"exp\": 1e3,\n"
        "  \"big_id\": 12345678901234567890,\n"
        "  \"flag_true\": true,\n"
        "  \"flag_num\": 1,\n"
        "  \"flag_str\": \"1\",\n"
        "  \"flag_false_str\": \"0\",\n"
        "  \"null_field\": null,\n"
        "  \"empty_str\": \"\",\n"
        "  \"cn\": \"\u4E2D\u6587\u5185\u5BB9\",\n"
        "  \"esc\": \"tab\\there\\nnewline \\\"quote\\\" back\\\\slash\",\n"
        "  \"uni\": \"\\u4e2d\\u6587\",\n"
        "  \"emoji\": \"\\ud83d\\ude00\",\n"
        "  \"arr\": [1, \"two\", null, true, {\"k\":\"v\"}],\n"
        "  \"empty_arr\": [],\n"
        "  \"empty_obj\": {},\n"
        "  \"nested\": {\"a\":{\"b\":{\"c\":\"deep\"}}}\n"
        "}";

    tieba::TbJson j = tieba::TbJson::parse(src);
    checkBool("\u6839\u8282\u70B9\u89E3\u6790\u6210\u529F", j.isValid(), true);
    if (!j.isValid()) {
        printf("       error: %s\n", qPrintable(j.errorMessage()));
        return;
    }
    checkBool("\u6839\u662F\u5BF9\u8C61", j.isObject(), true);

    checkStr("error_code (\u5B57\u7B26\u4E32)", tieba::TbJson::str(j, "error_code"), "0");
    checkStr("int_field", tieba::TbJson::str(j, "int_field"), "42");
    checkInt("int_field \u53D6\u6574", tieba::TbJson::integer(j, "int_field"), 42);
    checkInt("neg", tieba::TbJson::integer(j, "neg"), -7);
    checkInt("real \u622A\u65AD\u53D6\u6574", tieba::TbJson::integer(j, "real"), 3);
    checkInt("exp 1e3", tieba::TbJson::integer(j, "exp"), 1000);
    checkStr("big_id \u4FDD\u7559\u5B8C\u6574\u5B57\u9762\u91CF", tieba::TbJson::str(j, "big_id"),
             "12345678901234567890");
    checkStr("\u7A7A\u5B57\u7B26\u4E32", tieba::TbJson::str(j, "empty_str"), "");
    checkStr("null -> \u9ED8\u8BA4\u503C", tieba::TbJson::str(j, "null_field", "DEF"), "DEF");
    checkStr("\u7F3A\u5931\u5B57\u6BB5 -> \u9ED8\u8BA4\u503C", tieba::TbJson::str(j, "no_such_field", "DEF"), "DEF");
    checkStr("\u53CC\u952E\u56DE\u9000", tieba::TbJson::str2(j, "no_such_field", "error_code", "DEF"), "0");
    checkStr("\u53CC\u952E\u56DE\u9000\uFF08\u7B2C\u4E00\u952E\u5B58\u5728\u65F6\u7528\u7B2C\u4E00\u952E\uFF09",
             tieba::TbJson::str2(j, "int_field", "error_code", "DEF"), "42");
    checkStr("\u4E2D\u6587", tieba::TbJson::str(j, "cn"), QString::fromUtf8("\u4E2D\u6587\u5185\u5BB9"));
    checkStr("\u4E2D\u6587 \\u \u8F6C\u4E49", tieba::TbJson::str(j, "uni"), QString::fromUtf8("\u4E2D\u6587"));
    checkStr("emoji \u4EE3\u7406\u5BF9", tieba::TbJson::str(j, "emoji"), QString::fromUtf8("\xF0\x9F\x98\x80"));
    checkStr("\u8F6C\u4E49\u5E8F\u5217", tieba::TbJson::str(j, "esc"),
             "tab\there\nnewline \"quote\" back\\slash");
    {
        const tieba::TbJson nested = tieba::TbJson::object(j, "nested");
        const tieba::TbJson levelA = tieba::TbJson::object(nested, "a");
        const tieba::TbJson levelB = tieba::TbJson::object(levelA, "b");
        checkStr("\u4E09\u5C42\u5D4C\u5957\u53D6\u503C", tieba::TbJson::str(levelB, "c"), "deep");
        checkBool("object() \u9047\u5230\u975E\u5BF9\u8C61\u8FD4\u56DE null \u503C",
                  tieba::TbJson::object(levelB, "c").isNull(), true);
        checkBool("object() \u9047\u5230\u7F3A\u5931\u952E\u8FD4\u56DE null \u503C",
                  tieba::TbJson::object(levelB, "zzz").isNull(), true);
    }

    // 布尔容错：贴吧同一语义字段可能是 true / 1 / "1" / "0"
    checkBool("flag_true", tieba::TbJson::boolean(j, "flag_true"), true);
    checkBool("flag_num=1", tieba::TbJson::boolean(j, "flag_num"), true);
    checkBool("flag_str=\"1\"", tieba::TbJson::boolean(j, "flag_str"), true);
    checkBool("flag_false_str=\"0\"", tieba::TbJson::boolean(j, "flag_false_str"), false);
    checkBool("\u7F3A\u5931\u5E03\u5C14\u7528\u9ED8\u8BA4 true", tieba::TbJson::boolean(j, "nope", true), true);

    // 数组
    tieba::TbJson arr = tieba::TbJson::array(j, "arr");
    checkInt("\u6570\u7EC4\u957F\u5EA6", arr.size(), 5);
    checkInt("\u6570\u7EC4[0] \u6570\u5B57", arr.at(0).toLongLong(), 1);
    checkStr("\u6570\u7EC4[1] \u5B57\u7B26\u4E32", arr.at(1).toString(), "two");
    checkBool("\u6570\u7EC4[2] \u662F null", arr.at(2).isNull(), true);
    checkBool("\u6570\u7EC4[3] \u5E03\u5C14", arr.at(3).toBool(), true);
    checkStr("\u6570\u7EC4[4] \u5BF9\u8C61\u6210\u5458", arr.at(4).member("k").toString(), "v");
    checkBool("\u6570\u7EC4\u8D8A\u754C\u8FD4\u56DE null", arr.at(99).isNull(), true);
    checkInt("\u7A7A\u6570\u7EC4\u957F\u5EA6", tieba::TbJson::array(j, "empty_arr").size(), 0);
    checkBool("\u7A7A\u5BF9\u8C61\u53EF\u8BC6\u522B", tieba::TbJson::object(j, "empty_obj").isObject(), true);
}

static void testJsonErrors()
{
    printf("\n== TbJson \u9519\u8BEF\u5904\u7406\uFF08\u4E0D\u80FD\u5D29\u3001\u5FC5\u987B\u62A5\u9519\uFF09==\n");

    const char *bad[] = {
        "{\"a\":}",
        "{\"a\":1,}",
        "{\"a\" 1}",
        "[1,2",
        "{\"a\":\"unterminated",
        "{\"a\":\"bad\\uZZZZ\"}",
        "{\"a\":tru}",
        "",
        "   ",
        "{\"a\":1} trailing",
        "{\"a\":01x}",
    };

    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        tieba::TbJson j = tieba::TbJson::parse(QByteArray(bad[i]));
        ++g_checks;
        if (j.isValid()) {
            ++g_failures;
            printf("  FAIL \u5E94\u62A5\u9519\u5374\u6210\u529F: [%s]\n", bad[i]);
        } else {
            printf("  OK   \u6B63\u786E\u62A5\u9519: [%s] -> %s\n", bad[i],
                   qPrintable(j.errorMessage()));
        }
    }

    // 深层嵌套不能爆栈
    QByteArray deep;
    for (int i = 0; i < 500; ++i) deep.append('[');
    tieba::TbJson j = tieba::TbJson::parse(deep);
    checkBool("\u8D85\u6DF1\u5D4C\u5957\u88AB\u62D2\u7EDD\uFF08\u4E0D\u5D29\uFF09", !j.isValid(), true);
}

/* 用真实贴吧响应形状做一次解析测试 */
static void testRealisticPayload()
{
    printf("\n== \u771F\u5B9E\u5F62\u72B6\uFF1A/c/f/frs/page \u54CD\u5E94\u7247\u6BB5 ==\n");

    const QByteArray src =
        "{\"error_code\":\"0\",\"error_msg\":\"\","
        "\"forum\":{\"id\":23080,\"name\":\"\\u9ed1\\u8309\\u8389\","
        "\"is_like\":\"1\",\"user_level\":\"7\",\"level_id\":\"7\","
        "\"member_num\":\"1234567\",\"thread_num\":\"89012\","
        "\"avatar\":\"http://tb.himg.baidu.com/sys/forum/image\","
        "\"slogan\":\"<b>hello</b>\"},"
        "\"thread_list\":["
        "{\"id\":9876543210,\"tid\":\"9876543210\",\"title\":\"\\u6d4b\\u8bd5\\u5e16\","
        "\"reply_num\":\"12\",\"view_num\":\"345\",\"agree_num\":\"6\","
        "\"is_top\":\"0\",\"is_good\":\"1\",\"is_ntitle\":\"0\","
        "\"author_id\":\"111\","
        "\"create_time\":\"1700000000\",\"last_time_int\":\"1700000123\","
        "\"abstract\":[{\"type\":\"0\",\"text\":\"\\u6b63\\u6587\\u6458\\u8981\"}]"
        "}],"
        "\"page\":{\"current_page\":\"2\",\"total_page\":\"50\",\"has_more\":\"1\","
        "\"has_prev\":\"1\",\"page_size\":\"20\"},"
        "\"anti\":{\"tbs\":\"abcdef123456\"},"
        "\"user\":{\"id\":42,\"name\":\"tester\",\"name_show\":\"\\u6635\\u79f0\"}"
        "}";

    tieba::TbJson j = tieba::TbJson::parse(src);
    checkBool("\u89E3\u6790\u6210\u529F", j.isValid(), true);
    if (!j.isValid()) {
        printf("       error: %s\n", qPrintable(j.errorMessage()));
        return;
    }

    tieba::TbJson forum = tieba::TbJson::object(j, "forum");
    checkStr("forum.name\uFF08\\u \u8F6C\u4E49\u7684\u4E2D\u6587\uFF09", tieba::TbJson::str(forum, "name"),
             QString::fromUtf8("\u9ED1\u8309\u8389"));
    checkStr("forum.id \u662F\u6570\u5B57\uFF0C\u53D6\u5B57\u7B26\u4E32", tieba::TbJson::str(forum, "id"), "23080");
    checkBool("forum.is_like \u662F\u5B57\u7B26\u4E32 \"1\"", tieba::TbJson::boolean(forum, "is_like"), true);
    checkStr("forum.slogan\uFF08\u539F\u6837\u53D6\u51FA\uFF0C\u7531\u4E0A\u5C42\u5265\u6807\u7B7E\uFF09",
             tieba::TbJson::str(forum, "slogan"), "<b>hello</b>");
    checkStr("stripHtml \u80FD\u5265\u6389\u5B83", tieba::TbString::stripHtml(
                 tieba::TbJson::str(forum, "slogan")), "hello");

    tieba::TbJson threads = tieba::TbJson::array(j, "thread_list");
    checkInt("thread_list \u957F\u5EA6", threads.size(), 1);
    tieba::TbJson t0 = threads.at(0);
    checkStr("\u8D85\u957F tid \u4E0D\u88AB\u622A\u65AD", tieba::TbJson::str(t0, "tid"), "9876543210");
    checkStr("\u6807\u9898", tieba::TbJson::str(t0, "title"), QString::fromUtf8("\u6D4B\u8BD5\u5E16"));
    checkBool("is_good=\"1\" \u4E3A\u771F", tieba::TbJson::boolean(t0, "is_good"), true);
    checkBool("is_top=\"0\" \u4E3A\u5047", tieba::TbJson::boolean(t0, "is_top"), false);

    // abstract 是「类型化段落数组」，正文抽取要能拼接
    tieba::TbJson abs = tieba::TbJson::array(t0, "abstract");
    QString abstractText;
    for (int i = 0; i < abs.size(); ++i)
        abstractText += tieba::TbJson::str(abs.at(i), "text");
    checkStr("abstract \u62FC\u63A5", abstractText, QString::fromUtf8("\u6B63\u6587\u6458\u8981"));

    tieba::TbJson page = tieba::TbJson::object(j, "page");
    checkInt("page.current_page", tieba::TbJson::integer(page, "current_page"), 2);
    checkBool("page.has_more", tieba::TbJson::boolean(page, "has_more"), true);

    checkStr("anti.tbs\uFF08\u53D1\u5E16/\u56DE\u5E16\u8981\u7528\uFF09",
             tieba::TbJson::str(tieba::TbJson::object(j, "anti"), "tbs"),
             "abcdef123456");

    tieba::TbJson user = tieba::TbJson::object(j, "user");
    checkInt("user.id \u662F\u6570\u5B57 42", tieba::TbJson::integer(user, "id"), 42);
    checkStr("user.name_show", tieba::TbJson::str(user, "name_show"),
             QString::fromUtf8("\u6635\u79F0"));
}

static void testUrlEncoding()
{
    printf("\n== \u767E\u5206\u53F7\u7F16\u7801\uFF08\u5FC5\u987B\u4E0E\u5B98\u65B9\u5BA2\u6237\u7AEF\u4E00\u81F4\uFF09==\n");

    checkStr("\u5B57\u6BCD\u6570\u5B57\u4E0D\u53D8", tieba::TbString::percentEncode("abcXYZ0189"), "abcXYZ0189");
    checkStr("\u7A7A\u683C -> '+'", tieba::TbString::percentEncode("a b"), "a+b");
    checkStr("\u4E2D\u6587 -> \u5927\u5199 %XX", tieba::TbString::percentEncode(QString::fromUtf8("\u4E2D\u6587")),
             "%E4%B8%AD%E6%96%87");
    checkStr("~ \u4E0D\u7F16\u7801", tieba::TbString::percentEncode("~"), "~");
    checkStr("! \u4E0D\u7F16\u7801", tieba::TbString::percentEncode("!"), "!");
    checkStr("' \u4E0D\u7F16\u7801", tieba::TbString::percentEncode("'"), "'");
    checkStr("( \u4E0D\u7F16\u7801", tieba::TbString::percentEncode("("), "(");
    checkStr(") \u4E0D\u7F16\u7801", tieba::TbString::percentEncode(")"), ")");
    checkStr("* \u4E0D\u7F16\u7801", tieba::TbString::percentEncode("*"), "*");
    checkStr("-_. \u4E0D\u7F16\u7801", tieba::TbString::percentEncode("-_."), "-_.");
    checkStr("& \u8981\u7F16\u7801", tieba::TbString::percentEncode("&"), "%26");
    checkStr("= \u8981\u7F16\u7801", tieba::TbString::percentEncode("="), "%3D");
    checkStr("+ \u8981\u7F16\u7801", tieba::TbString::percentEncode("+"), "%2B");
    checkStr("/ \u8981\u7F16\u7801", tieba::TbString::percentEncode("/"), "%2F");
    checkStr("/ \u4FDD\u7559", tieba::TbString::percentEncode("/", true), "/");
    checkStr("\u5341\u516D\u8FDB\u5236\u5927\u5199", tieba::TbString::percentEncode("\x01"), "%01");

    printf("\n== \u767E\u5206\u53F7\u89E3\u7801 ==\n");
    checkStr("\u89E3\u7801\u4E2D\u6587", tieba::TbString::percentDecode("%E4%B8%AD%E6%96%87"),
             QString::fromUtf8("\u4E2D\u6587"));
    checkStr("'+' \u89E3\u7801\u4E3A\u7A7A\u683C", tieba::TbString::percentDecode("a+b"), "a b");
    checkStr("\u5C0F\u5199\u5341\u516D\u8FDB\u5236\u4E5F\u8BA4", tieba::TbString::percentDecode("%e4%b8%ad"),
             QString::fromUtf8("\u4E2D"));
    checkStr("\u975E\u6CD5 % \u5E8F\u5217\u539F\u6837\u4FDD\u7559", tieba::TbString::percentDecode("100%"), "100%");
    checkStr("\u5F80\u8FD4\u4E00\u81F4", tieba::TbString::percentDecode(
                 tieba::TbString::percentEncode(QString::fromUtf8("\u8D34\u5427 & test/1"))),
             QString::fromUtf8("\u8D34\u5427 & test/1"));
}

static void testSignOrdering()
{
    printf("\n== sign \u53C2\u6570\u6392\u5E8F ==\n");

    QList<tieba::KeyValue> params;
    params.append(qMakePair(QString("kw"), QString::fromUtf8("\u9ED1\u8309\u8389")));
    params.append(qMakePair(QString("pn"), QString("1")));
    params.append(qMakePair(QString("_client_version"), QString("7.2.0.0")));
    params.append(qMakePair(QString("net_type"), QString("1")));
    params.append(qMakePair(QString("timestamp"), QString("1700000000")));

    const QString raw = tieba::TbString::sortedRawString(params);
    // 字节序：'_'(0x5F) < 'k'(0x6B) < 'n'(0x6E) < 'p'(0x70) < 't'(0x74)
    checkStr("\u539F\u59CB\u4E32\u6309\u5B57\u8282\u5E8F\u6392\u5E8F",
             raw,
             "_client_version=7.2.0.0&kw=" + QString::fromUtf8("\u9ED1\u8309\u8389")
             + "&net_type=1&pn=1&timestamp=1700000000");

    const QString sign = tieba::TbMd5::hex((raw + "tiebaclient!!!").toUtf8());
    checkInt("sign \u957F\u5EA6 32", sign.size(), 32);
    checkBool("sign \u5168\u4E3A\u5C0F\u5199\u5341\u516D\u8FDB\u5236",
              QRegExp("^[0-9a-f]{32}$").exactMatch(sign), true);
    printf("       sign = %s\n", qPrintable(sign));

    // 编码串应与原始串同序
    const QString encoded = tieba::TbString::sortedEncodedString(params);
    checkStr("\u7F16\u7801\u4E32\u540C\u5E8F\u4E14\u4E2D\u6587\u88AB\u7F16\u7801",
             encoded,
             "_client_version=7.2.0.0&kw=%E9%BB%91%E8%8C%89%E8%8E%89"
             "&net_type=1&pn=1&timestamp=1700000000");

    // 排序不能受平台 locale 影响：大写字母在小写之前
    QList<tieba::KeyValue> caseParams;
    caseParams.append(qMakePair(QString("ZZ"), QString("1")));
    caseParams.append(qMakePair(QString("aa"), QString("1")));
    checkStr("ASCII \u5B57\u8282\u5E8F Z < a", tieba::TbString::sortedRawString(caseParams),
             "ZZ=1&aa=1");
}

static void testMiscString()
{
    printf("\n== \u5176\u4ED6\u5B57\u7B26\u4E32\u5DE5\u5177 ==\n");

    checkStr("\u5934\u50CF URL \u62FC\u63A5",
             tieba::TbString::avatarUrl("abc123"),
             "https://himg.bdimg.com/sys/portrait/item/abc123");
    // 已是完整 URL 时不重复拼接；但 http 会被升级成 https
    // （BB10 的图片加载器只认 https/asset/file，见 httpsImageUrl 的说明）
    checkStr("\u5DF2\u662F\u5B8C\u6574 URL \u4E0D\u91CD\u590D\u62FC\u63A5\uFF08\u5347\u7EA7 https\uFF09",
             tieba::TbString::avatarUrl("http://x/y.png"), "https://x/y.png");
    checkStr("\u5B8C\u6574 https URL \u4E0D\u53D8",
             tieba::TbString::avatarUrl("https://x/y.png"), "https://x/y.png");
    checkStr("\u7A7A portrait", tieba::TbString::avatarUrl(""), "");

    /* ---- 图片 URL 规范化 ----
     * 设备日志实测：BB10 只接受 https/asset/file，
     * 遇到 http 会 "Image loading aborted"。
     * 贴吧接口可能返回 http，所以必须升级。 */
    checkStr("httpsImageUrl \u5347\u7EA7 http",
             tieba::TbString::httpsImageUrl("http://tiebapic.baidu.com/a.jpg"),
             "https://tiebapic.baidu.com/a.jpg");
    checkStr("httpsImageUrl \u4FDD\u7559 https",
             tieba::TbString::httpsImageUrl("https://tiebapic.baidu.com/a.jpg"),
             "https://tiebapic.baidu.com/a.jpg");
    checkStr("httpsImageUrl \u4FDD\u7559 asset",
             tieba::TbString::httpsImageUrl("asset:///images/x.png"),
             "asset:///images/x.png");
    checkStr("httpsImageUrl \u4FDD\u7559 file",
             tieba::TbString::httpsImageUrl("file:///accounts/x.png"),
             "file:///accounts/x.png");
    checkStr("httpsImageUrl \u7A7A\u4E32",
             tieba::TbString::httpsImageUrl(""), "");
    // 查询串必须原样保留（tbpicau 是签名参数，改了就 403）
    checkStr("httpsImageUrl \u4FDD\u7559\u67E5\u8BE2\u4E32",
             tieba::TbString::httpsImageUrl("http://x/a.jpg?tbpicau=1_2"),
             "https://x/a.jpg?tbpicau=1_2");

    checkStr("\u5265 HTML \u6807\u7B7E",
             tieba::TbString::stripHtml("<b>bold</b> &amp; <i>x</i>"),
             "bold & x");
    checkStr("nbsp \u5B9E\u4F53", tieba::TbString::stripHtml("a&nbsp;b"), "a b");
    checkStr("\u5341\u8FDB\u5236\u6570\u5B57\u5B9E\u4F53", tieba::TbString::stripHtml("&#65;"), "A");
    checkStr("\u5341\u516D\u8FDB\u5236\u6570\u5B57\u5B9E\u4F53", tieba::TbString::stripHtml("&#x41;&#X42;"), "AB");
    checkStr("apos \u5B9E\u4F53", tieba::TbString::stripHtml("it&apos;s"), "it's");
    checkStr("\u5E26\u5C5E\u6027\u7684\u6807\u7B7E", tieba::TbString::stripHtml("<a href=\"x\">t</a>"), "t");
    checkStr("\u6CE8\u91CA", tieba::TbString::stripHtml("a<!-- c -->b"), "ab");
    checkStr("\u672A\u95ED\u5408\u6807\u7B7E\u4E0D\u541E\u6389\u540E\u7EED\u6587\u672C",
             tieba::TbString::stripHtml("a < b"), "a < b");
    checkStr("\u88F8\u5C0F\u4E8E\u53F7\u540E\u8DDF\u6807\u7B7E",
             tieba::TbString::stripHtml("a < b <i>c</i>"), "a < b c");

    checkStr("\u6E05\u7406\u96F6\u5BBD\u5B57\u7B26",
             tieba::TbString::cleanText(QString::fromUtf8(" a\xE2\x80\x8B b ")),
             "a b");

    checkStr("\u77ED\u6570\u5B57\u4E0D\u52A0\u5355\u4F4D", tieba::TbString::shortNumber(999), "999");
    checkStr("\u4E07", tieba::TbString::shortNumber(12345), "1.2W");
    checkStr("\u4EBF", tieba::TbString::shortNumber(123456789), "1.2\u4EBF");
}

#include <windows.h>

int main()
{
    // 让 Windows 控制台按 UTF-8 显示，便于看中文断言名
    SetConsoleOutputCP(65001);

    testJsonBasics();
    testJsonErrors();
    testRealisticPayload();
    testUrlEncoding();
    testSignOrdering();
    testMiscString();

    printf("\n================================\n");
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
