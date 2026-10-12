#include <QCoreApplication>
#include <cstdio>
#include "api/TbApi.hpp"
using namespace tieba;
static int checks = 0, failures = 0;
static void check(bool value) { ++checks; if (!value) ++failures; }
static QVariantMap parse(const char *kind, const char *fixture) {
    return TbApiService::parseSearch(QString::fromLatin1(kind), TbJson::parse(QByteArray(fixture)));
}
static QString parameter(const TbHttpRequest &r, const QString &name) {
    for (int i = 0; i < r.params.size(); ++i) if (r.params.at(i).first == name) return r.params.at(i).second;
    return QString();
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const TbHttpRequest global = TbApiService::buildSearchRequest("two words & x", "thread", "", 2);
    check(global.method == "GET" && global.body.isEmpty());
    check(parameter(global, "word") == "two words & x");
    check(parameter(global, "pn") == "2");
    check(parameter(global, "ct") == "1" && parameter(global, "is_use_zonghe") == "1");
    check(parameter(global, "BDUSS").isEmpty() && parameter(global, "STOKEN").isEmpty());
    const TbHttpRequest scoped = TbApiService::buildSearchRequest("word", "thread", "TestForum", 1);
    check(parameter(scoped, "fname") == "TestForum" && parameter(scoped, "ct") == "2");
    check(parameter(scoped, "is_use_zonghe").isEmpty());
    const QVariantMap thread = parse("thread", "{\"no\":0,\"data\":{\"has_more\":1,\"current_page\":2,\"post_list\":[{\"tid\":\"11\",\"pid\":\"12\",\"title\":\"<em>match</em>\",\"content\":\"<b>body</b>\",\"user\":{\"user_id\":\"20\",\"name\":\"author\",\"show_nickname\":\"Nick\"}}]}}");
    const QVariantMap row = thread.value("items").toList().first().toMap();
    check(row.value("id") == "11" && row.value("postId") == "12");
    check(row.value("title") == "match" && row.value("abstractText") == "body");
    check(row.value("authorId") == "20" && row.value("authorName") == "Nick");
    check(row.value("authorLevel").toInt() == 0);
    check(thread.value("hasMore").toBool() && thread.value("page").toInt() == 2);
    const QVariantMap forum = parse("forum", "{\"no\":0,\"data\":{\"exactMatch\":{\"forum_id\":\"33\",\"forum_name\":\"forum\",\"concern_num\":1200},\"fuzzyMatch\":{\"0\":{\"forum_id\":\"33\",\"forum_name\":\"forum\"},\"1\":{\"forum_id\":\"34\",\"forum_name\":\"other\"}},\"has_more\":0,\"pn\":1}}");
    check(forum.value("items").toList().size() == 2);
    check(!forum.value("items").toList().first().toMap().value("memberCountText").toString().isEmpty());
    check(forum.value("items").toList().last().toMap().value("memberCountText").toString().isEmpty());
    const QVariantMap user = parse("user", "{\"no\":0,\"data\":{\"exactMatch\":null,\"fuzzyMatch\":[{\"id\":\"9007199254740993\",\"name\":\"account\",\"user_nickname\":\"Nick\"}],\"has_more\":0}}");
    check(user.value("items").toList().first().toMap().value("id").toString() == "9007199254740993");
    check(user.value("items").toList().first().toMap().value("authorName") == "Nick");
    const QVariantMap empty = parse("thread", "{\"no\":0,\"data\":{\"post_list\":[],\"has_more\":0}}");
    check(!empty.contains("error") && empty.value("items").toList().isEmpty());
    check(parse("thread", "{\"no\":0,\"data\":{}}").contains("error"));
    check(parse("user", "{\"no\":0,\"data\":{}}").contains("error"));
    check(parse("forum", "{\"no\":0,\"data\":{\"fuzzyMatch\":\"malformed\"}}").contains("error"));
    check(parse("thread", "{\"no\":110001,\"error\":\"UnknownError\"}").value("error").toString().contains("110001"));
    std::printf("Search API: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
