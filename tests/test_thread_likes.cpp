#include "tieba/model/TbItems.hpp"
#include "tieba/model/TbParsers.hpp"
#include "tieba/model/TbPcParsers.hpp"
#include "tieba/util/TbJson.hpp"
#include "tieba/api/TbApi.hpp"
#include <QCoreApplication>
#include <stdio.h>

using namespace tieba;
static int checks = 0, failures = 0;
static void check(const char *label, bool ok)
{
    ++checks;
    if (!ok) ++failures;
    printf("%s %s\n", ok ? "OK" : "FAIL", label);
}
static qint64 likes(const char *json) { return TbThread::parseAgreeCount(TbJson::parse(json)); }
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    check("missing is unknown", likes("{}") == -1);
    check("null is unknown", likes("{\"agree_num\":null}") == -1);
    check("malformed is unknown", likes("{\"agree_num\":\"unknown\"}") == -1);
    check("negative is unknown", likes("{\"agree_num\":-1}") == -1);
    check("boolean is unknown", likes("{\"agree_num\":false}") == -1);
    check("fraction is invalid", likes("{\"agree_num\":1.5}") == -1);
    check("explicit numeric zero", likes("{\"agree\":{\"agree_num\":0}}") == 0);
    check("explicit string zero", likes("{\"agree_num\":\"0\"}") == 0);
    check("client nested agree count", likes("{\"agree\":{\"agree_num\":365}}") == 365);
    check("search like_num", likes("{\"like_num\":\"12345\"}") == 12345);
    check("nested wins over flat", likes("{\"agree\":{\"agree_num\":0},\"agree_num\":99}") == 0);
    check("invalid nested permits flat", likes("{\"agree\":{\"agree_num\":null},\"agree_num\":7}") == 7);
    check("large count stays 64 bit", likes("{\"agree_num\":4294967296}") == Q_INT64_C(4294967296));
    TbThread model;
    check("new model has unknown likes", !model.hasAgreeCount() && model.agreeCountText().isEmpty());
    model.setAgreeCount(0);
    check("model zero shown", model.hasAgreeCount() && model.agreeCountText() == QLatin1String("0"));
    model.setAgreeCount(12345);
    check("model formatted count", model.agreeCountText() == TbString::shortNumber(12345));
    check("variant exposes count and presence", model.toVariantMap().value("hasAgreeCount").toBool()
          && model.toVariantMap().value("agreeCount").toLongLong() == 12345
          && !model.toVariantMap().value("agreeCountText").toString().isEmpty());
    model.setAgreeCount(-1);
    check("unknown never becomes fake zero", !model.toVariantMap().value("hasAgreeCount").toBool()
          && model.toVariantMap().value("agreeCountText").toString().isEmpty());

    const TbJson mobile = TbJson::parse("{\"error_code\":0,\"thread_list\":[{\"id\":\"1\",\"agree\":{\"agree_num\":42}},{\"id\":\"2\",\"agree_num\":\"0\"},{\"id\":\"3\"}]}");
    TbForumPageResult parsed = TbParsers::parseForumPage(mobile);
    check("mobile parser count", parsed.threads.size() == 3 && parsed.threads.at(0)->agreeCount() == 42);
    check("mobile parser known zero", parsed.threads.at(1)->hasAgreeCount() && parsed.threads.at(1)->agreeCount() == 0);
    check("mobile parser missing unknown", !parsed.threads.at(2)->hasAgreeCount());
    qDeleteAll(parsed.threads); delete parsed.forumInfo; delete parsed.pageInfo;
    const TbJson pc = TbJson::parse("{\"error_code\":0,\"page_data\":{\"feed_list\":[{\"layout\":\"feed\",\"feed\":{\"business_info_map\":{\"thread_id\":\"1\"},\"components\":[{\"component\":\"feed_social\",\"feed_social\":{\"agree\":{\"agree_num\":\"365\"}}}]}},{\"layout\":\"feed\",\"feed\":{\"business_info_map\":{\"thread_id\":\"2\"},\"components\":[{\"component\":\"feed_social\",\"feed_social\":{\"agree\":{\"agree_num\":0}}}]}},{\"layout\":\"feed\",\"feed\":{\"business_info_map\":{\"thread_id\":\"3\"},\"components\":[]}}]}}");
    TbPcForumPageResult pcParsed = TbPcParsers::parseForumPage(pc);
    check("PC feed social count", pcParsed.threads.size() == 3 && pcParsed.threads.at(0)->agreeCount() == 365);
    check("PC feed known zero", pcParsed.threads.at(1)->hasAgreeCount() && pcParsed.threads.at(1)->agreeCount() == 0);
    check("PC feed missing unknown", !pcParsed.threads.at(2)->hasAgreeCount());
    qDeleteAll(pcParsed.threads); delete pcParsed.forumInfo; delete pcParsed.pageInfo; delete pcParsed.user;
    // All three directly-built API row shapes must preserve the same semantics.
    const char *countFields[] = { ",\"agree_num\":0", ",\"like_num\":\"17\"",
        ",\"agree\":{\"agree_num\":\"23\"}", "" };
    const qint64 expected[] = { 0, 17, 23, -1 };
    for (int api = 0; api < 3; ++api) {
        for (int sample = 0; sample < 4; ++sample) {
            const QByteArray node = QByteArray(api == 1 ? "{\"thread_id\":\"7\"" : "{\"tid\":\"7\"")
                + countFields[sample] + "}";
            QVariantMap result;
            if (api == 0) result = TbApiService::parseStream("recommend", TbJson::parse(
                QByteArray("{\"error_code\":0,\"thread_list\":[") + node + "]}"));
            else if (api == 1) result = TbApiService::parseUserPage("threads", TbJson::parse(
                QByteArray("{\"error_code\":0,\"post_list\":[") + node + "]}"));
            else result = TbApiService::parseSearch("thread", TbJson::parse(
                QByteArray("{\"no\":0,\"data\":{\"post_list\":[") + node + "]}}"));
            const QVariantList apiRows = result.value("items").toList();
            const QByteArray label = QByteArray(api == 0 ? "stream" : api == 1 ? "user page" : "search")
                + " sample " + QByteArray::number(sample);
            check((label + " produces a row").constData(), !result.contains("error") && apiRows.size() == 1);
            if (apiRows.isEmpty()) continue;
            const QVariantMap apiRow = apiRows.first().toMap();
            check((label + " agrees presence").constData(), apiRow.value("hasAgreeCount").toBool() == (expected[sample] >= 0));
            check((label + " agrees numeric count").constData(), apiRow.value("agreeCount").toLongLong() == expected[sample]);
            check((label + " agrees display text").constData(), apiRow.value("agreeCountText").toString()
                == (expected[sample] >= 0 ? TbString::shortNumber(expected[sample]) : QString()));
        }
    }
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
