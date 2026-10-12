#include <QCoreApplication>
#include <cstdio>
#include "api/TbApi.hpp"
using namespace tieba;
static int checks = 0, failures = 0;
static void check(bool ok) { ++checks; if (!ok) ++failures; }
static QVariantMap parse(const char *kind, const char *fixture) {
    return TbApiService::parseUserPage(QString::fromLatin1(kind), TbJson::parse(QByteArray(fixture)));
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QVariantMap profile = parse("profile", "{\"error_code\":0,\"user\":{\"id\":\"42\",\"name\":\"account\",\"name_show\":\"Nick\",\"concern_num\":\"12\",\"fans_num\":0},\"user_agree_info\":{\"total_agree_num\":\"99\"}}").value("profile").toMap();
    check(profile.value("name") == "Nick");
    check(profile.value("followingCount").toInt() == 12);
    check(profile.contains("fansCount") && profile.value("fansCount").toInt() == 0);
    check(profile.value("agreeCount").toInt() == 99);
    const QVariantMap unknown = parse("profile", "{\"error_code\":0,\"user\":{\"id\":\"42\",\"name\":\"Nick\"}}").value("profile").toMap();
    check(!unknown.contains("followingCount") && !unknown.contains("fansCount") && !unknown.contains("agreeCount"));
    check(parse("profile", "{\"error_code\":0}").contains("error"));
    check(parse("threads", "{\"error_code\":0}").contains("error"));
    check(parse("forums", "{\"error_code\":0,\"forum_list\":{}}").contains("error"));
    check(parse("follows", "{\"error_code\":0}").contains("error"));
    const QVariantMap threads = parse("threads", "{\"error_code\":0,\"post_list\":[{\"thread_id\":\"10\",\"post_id\":\"11\",\"content\":[{\"post_content\":[{\"type\":0,\"text\":\"body\"}]}]}]}");
    check(threads.value("items").toList().size() == 1);
    check(threads.value("items").toList().first().toMap().value("abstractText") == "body");
    check(threads.value("hasMore").toBool());
    const QVariantMap hidden = parse("threads", "{\"error_code\":0,\"hide_post\":1}");
    check(hidden.value("private").toBool() && !hidden.value("hasMore").toBool());
    const QVariantMap forums = parse("forums", "{\"error_code\":0,\"has_more\":0,\"forum_list\":{\"non-gconforum\":[{\"id\":\"5\",\"name\":\"Forum\",\"level_id\":3}]}}");
    check(forums.value("items").toList().first().toMap().value("level").toInt() == 3);
    check(!forums.value("hasMore").toBool());
    const QVariantMap groups = parse("forums", "{\"error_code\":0,\"has_more\":0,\"forum_list\":{\"non-gconforum\":[{\"id\":\"5\",\"name\":\"Forum\"}],\"gconforum\":[{\"id\":\"6\",\"name\":\"Extra\"}]}}");
    check(groups.value("items").toList().size() == 2 && groups.value("items").toList()[1].toMap().value("id") == "6");
    const QVariantMap follows = parse("follows", "{\"error_code\":0,\"follow_list\":[],\"has_more\":0,\"tips_text\":\"privacy\"}");
    check(follows.value("message") == "privacy");
    check(!follows.contains("error"));
    check(parse("threads", "{\"error_code\":100,\"error_msg\":\"denied\"}").value("error").toString().contains("100"));
    const QList<KeyValue> params = TbApiService::userPageParams("forums", "42", 0);
    check(params.at(1).first == "page_no" && params.at(1).second == "1");
    std::printf("User page API: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
