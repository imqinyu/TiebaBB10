#include <QCoreApplication>
#include <QVariantMap>
#include "util/TbThreadNavigation.hpp"
#include "api/TbApi.hpp"
#include <stdio.h>
using namespace tieba;
static int checks = 0, failures = 0;
static void check(const char *name, bool ok) {
    ++checks; if (!ok) ++failures;
    printf("%s %s\n", ok ? "OK" : "FAIL", name);
}
static QVariantMap row(const QString &id, int floor, bool head = true) {
    QVariantMap map; map.insert("id", id); map.insert("floor", floor); map.insert("postHead", head); return map;
}
static QString parameter(const QList<KeyValue> &values, const QString &name) {
    for (int i = 0; i < values.size(); ++i) if (values.at(i).first == name) return values.at(i).second;
    return QString();
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    check("positive floor accepts whitespace and leading zeroes", TbThreadNavigation::positiveFloor(" 0031 ") == 31);
    check("zero is invalid", TbThreadNavigation::positiveFloor("0") == 0);
    check("negative is invalid", TbThreadNavigation::positiveFloor("-2") == 0);
    check("decimal is invalid", TbThreadNavigation::positiveFloor("2.5") == 0);
    check("mixed text is invalid", TbThreadNavigation::positiveFloor("3abc") == 0);
    check("overflow is invalid", TbThreadNavigation::positiveFloor("2147483648") == 0);
    check("first page candidate", TbThreadNavigation::estimatedPage(30) == 1);
    check("second page candidate", TbThreadNavigation::estimatedPage(31) == 2);
    QVariantList rows;
    rows << row("a", 31, false) << row("a", 31) << row("b", 35);
    check("floor targets header rather than image row", TbThreadNavigation::locateFloor(rows, 31) == 1);
    check("post targets header rather than image row", TbThreadNavigation::locatePost(rows, "a") == 1);
    check("deleted floor is not replaced with nearby floor", TbThreadNavigation::locateFloor(rows, 33) == -1);
    QVariantMap parent = row("p", 40);
    QVariantMap sub; sub.insert("id", "child");
    QVariantList preview; preview << sub; parent.insert("subPostPreview", preview); rows << parent;
    check("subpost can locate its parent header", TbThreadNavigation::locatePost(rows, "", "child") == 3);
    int low = 1, high = 0;
    QVariantList late; late << row("x", 52) << row("y", 81);
    check("deleted earlier floors narrow binary search", TbThreadNavigation::nextFloorPage(31, 3, 3, late, false, low, high) == 1 && high == 2);
    QVariantList early; early << row("x", 2) << row("y", 23);
    check("binary search then converges on middle page", TbThreadNavigation::nextFloorPage(31, 1, 3, early, false, low, high) == 2 && low == 2);
    QVariantList middle; middle << row("x", 26) << row("y", 33);
    check("range containing a missing floor terminates", TbThreadNavigation::nextFloorPage(31, 2, 3, middle, false, low, high) == 0);
    low = 1; high = 0;
    check("reverse search moves to later logical pages", TbThreadNavigation::nextFloorPage(23, 1, 4, late, true, low, high) == 3 && low == 2);
    low = 1; high = 0;
    check("unknown total searches the next adjacent page", TbThreadNavigation::nextFloorPage(99, 1, 0, early, false, low, high) == 2);
    low = 1; high = 0;
    check("target before first ascending page is unavailable", TbThreadNavigation::nextFloorPage(1, 1, 2, early, false, low, high) == 0);
    low = 1; high = 0;
    check("empty response cannot pick a wrong floor", TbThreadNavigation::nextFloorPage(1, 1, 2, QVariantList(), false, low, high) == 0);
    const QList<KeyValue> forward = TbApiService::buildThreadContentPcParams("100", 0, false, false);
    check("forward order retains verified r2", parameter(forward, "r") == "2" && parameter(forward, "pn") == "1");
    check("forward request has no reverse last flag", parameter(forward, "last").isEmpty());
    const QList<KeyValue> reverse = TbApiService::buildThreadContentPcParams("100", 2, true, true);
    check("reverse matches verified r1 last1 protocol", parameter(reverse, "r") == "1" && parameter(reverse, "last") == "1");
    check("reverse logical paging and OP filter preserved", parameter(reverse, "pn") == "2" && parameter(reverse, "lz") == "1");
    const QList<KeyValue> anchored = TbApiService::buildThreadContentPcParams("100", 1, false, false, "parent", "child");
    check("PC pid request retains mandatory pn", parameter(anchored, "pid") == "parent" && parameter(anchored, "pn") == "1");
    check("spid is a local/floor locator rather than unsupported pb parameter", parameter(anchored, "spid").isEmpty());
    const TbHttpRequest locator = TbApiService::buildThreadPostRequest(0, "100", "parent");
    check("pid locator uses verified mobile endpoint", locator.url.endsWith("/c/f/pb/page"));
    check("pid locator omits pn for verified overload", parameter(locator.params, "pid") == "parent" && parameter(locator.params, "pn").isEmpty());
    check("pid locator uses current Android version", parameter(locator.params, "_client_version") == "11.10.8.6");
    bool threadHeader = false;
    for (int i = 0; i < locator.headers.size(); ++i)
        if (locator.headers.at(i).first == "thread_id") threadHeader = locator.headers.at(i).second == "100";
    check("pid locator carries thread_id header", threadHeader);
    check("new thread result is not an anchor resolution", !TbPcThreadResult().anchorResolved);
    printf("Thread navigation: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
