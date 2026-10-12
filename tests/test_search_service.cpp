// Offline service tests: replace only the HTTP transport. The production API
// still builds requests and the production service still routes real Qt signals.
#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QVariantMap>
#include <QUrl>
#include <QStringList>
#include <QFile>
#include <QDir>
#include "util/TbJson.hpp"
#include "util/TbString.hpp"
// TbHttpClient predates transport injection. Expose its manager in this test
// translation unit instead of adding a production-only testing hook.
#define private public
#include "net/TbHttpClient.hpp"
#undef private
#include "api/TbApi.hpp"
#include "api/TbSearchService.hpp"
#include "session/TbSession.hpp"
#include <cstdio>

using namespace tieba;
static int checks = 0, failures = 0;
static void check(const char *name, bool value) {
    ++checks;
    if (!value) ++failures;
    std::printf("%s %s\n", value ? "OK" : "FAIL", name);
}
class PendingReply : public QNetworkReply {
public:
    PendingReply(const QNetworkRequest &request, QObject *parent) : QNetworkReply(parent) {
        setRequest(request);
        setUrl(request.url());
        setOperation(QNetworkAccessManager::GetOperation);
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    }
    void abort() {}
protected:
    qint64 readData(char *, qint64) { return -1; }
};
class OfflineManager : public QNetworkAccessManager {
public:
    explicit OfflineManager(QObject *parent) : QNetworkAccessManager(parent), count(0), last(0) {}
    int count;
    PendingReply *last;
protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) {
        ++count;
        last = new PendingReply(request, this);
        return last; // Never dispatch to QNetworkAccessManager's network backend.
    }
};
static QVariantMap response(const QStringList &keys, bool more) {
    QVariantList items;
    for (int i = 0; i < keys.size(); ++i) {
        QVariantMap row;
        row.insert("id", keys.at(i));
        row.insert("key", keys.at(i));
        items.append(row);
    }
    QVariantMap result;
    result.insert("items", items);
    result.insert("hasMore", more);
    return result;
}
static void deliver(TbApiService &api, int requestId, const QStringList &keys, bool more) {
    const QVariantMap result = response(keys, more);
    check("fixture uses registered API signal", QMetaObject::invokeMethod(&api, "searchLoaded", Qt::DirectConnection,
          Q_ARG(int, requestId), Q_ARG(QVariantMap, result)));
}
static int id(OfflineManager *manager) { return manager->last->property("tb_request_id").toInt(); }
static int page(OfflineManager *manager) { return manager->last->url().queryItemValue("pn").toInt(); }
static int rowCount(const QVariantMap &state) { return state.value("items").toList().size(); }
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    TbHttpClient client;
    delete client.m_manager;
    OfflineManager *manager = new OfflineManager(&client);
    client.m_manager = manager;
    TbSession session;
    session.setSettingsFileForTest(QDir::currentPath() + "/tests/tmp/search-service-isolated.ini");
    TbApiService api(&client, &session);
    TbSearchService service(&api, &session);

    check("construction makes no request", manager->count == 0);
    check("unknown state has typed empty defaults", !service.state("a", "thread").value("loading").toBool()
          && !service.state("a", "thread").value("loaded").toBool() && rowCount(service.state("a", "thread")) == 0);
    service.load("  a  ", "thread");
    const int first = id(manager);
    check("first page request", page(manager) == 1);
    check("query is trimmed and scope-normalized", manager->last->url().queryItemValue("word") == "a"
          && service.state("a", "thread").value("loading").toBool());
    check("search transport remains anonymous", manager->last->request().rawHeader("Cookie") == "ka=open"
          && !manager->last->url().hasQueryItem("BDUSS"));
    service.load("a", "thread");
    check("duplicate in-flight load is suppressed", manager->count == 1);
    deliver(api, first, QStringList() << "one" << "two", true);
    check("first response sets loaded state", service.state("a", "thread").value("loaded").toBool()
          && !service.state("a", "thread").value("loading").toBool());
    check("first response stores page and rows", service.state("a", "thread").value("page").toInt() == 1
          && rowCount(service.state("a", "thread")) == 2);
    service.load("a", "thread", "", true);
    const int second = id(manager);
    check("append requests next page", page(manager) == 2);
    check("append keeps old rows while loading", rowCount(service.state("a", "thread")) == 2);
    deliver(api, second, QStringList() << "two" << "three" << "three", true);
    check("pagination merges and removes duplicates", rowCount(service.state("a", "thread")) == 3);
    check("successful pagination advances page", service.state("a", "thread").value("page").toInt() == 2);
    service.load("a", "thread", "", true);
    const int failedId = id(manager);
    const QString tag("search.thread"), error("fixture failure");
    QVariantMap detail;
    detail.insert("httpStatus", 503);
    detail.insert("networkError", 99);
    check("fixture uses registered error signal", QMetaObject::invokeMethod(&api, "requestFailed", Qt::DirectConnection,
          Q_ARG(int, failedId), Q_ARG(QString, tag), Q_ARG(QString, error), Q_ARG(QVariantMap, detail)));
    QVariantMap state = service.state("a", "thread");
    check("failed append keeps old rows and page", rowCount(state) == 3 && state.value("page").toInt() == 2);
    check("failed append retains paging eligibility", state.value("hasMore").toBool() && !state.value("loading").toBool());
    check("error includes transport diagnostics", state.value("error").toString().contains("503")
          && state.value("error").toString().contains("99"));
    service.load("a", "thread", "", true);
    check("retry requests the same failed page", page(manager) == 3);
    check("retry clears error but keeps old rows", service.state("a", "thread").value("error").toString().isEmpty()
          && rowCount(service.state("a", "thread")) == 3);
    deliver(api, id(manager), QStringList() << "three" << "four", true);
    check("retry merges without duplicates", rowCount(service.state("a", "thread")) == 4);
    service.load("a", "thread", "", true);
    deliver(api, id(manager), QStringList() << "one" << "two", true);
    check("repeated page stops automatic pagination", !service.state("a", "thread").value("hasMore").toBool());
    const int beforeStopped = manager->count;
    service.load("a", "thread", "", true);
    check("stopped pagination sends no further request", manager->count == beforeStopped);

    service.load("empty-page", "thread");
    deliver(api, id(manager), QStringList() << "initial", true);
    service.load("empty-page", "thread", "", true);
    deliver(api, id(manager), QStringList(), true);
    check("empty page also stops despite server has_more", !service.state("empty-page", "thread").value("hasMore").toBool()
          && rowCount(service.state("empty-page", "thread")) == 1);

    service.load("a", "forum");
    const int forumRequest = id(manager);
    service.load("different", "thread");
    const int differentRequest = id(manager);
    service.load("a", "thread", "forum-one");
    const int scopedRequest = id(manager);
    check("forum restriction reaches request URL", manager->last->url().queryItemValue("fname") == "forum-one");
    service.load("a", "thread", "forum-two");
    const int otherForumRequest = id(manager);
    deliver(api, otherForumRequest, QStringList() << "forum-two-only", false);
    deliver(api, differentRequest, QStringList() << "different-only", false);
    deliver(api, forumRequest, QStringList() << "forum-result", false);
    deliver(api, scopedRequest, QStringList() << "forum-one-only", false);
    check("kind scope does not overwrite thread results", rowCount(service.state("a", "thread")) == 4
          && rowCount(service.state("a", "forum")) == 1);
    check("query scopes remain isolated", service.state("different", "thread").value("items").toList().first().toMap().value("id") == "different-only");
    check("forum scopes remain isolated", service.state("a", "thread", "forum-one").value("items").toList().first().toMap().value("id") == "forum-one-only"
          && service.state("a", "thread", "forum-two").value("items").toList().first().toMap().value("id") == "forum-two-only");

    service.load("late", "user");
    const int late = id(manager);
    check("account event uses registered session signal", QMetaObject::invokeMethod(&session, "accountsChanged", Qt::DirectConnection));
    check("account event clears all scopes", rowCount(service.state("a", "thread")) == 0
          && !service.state("a", "forum").value("loaded").toBool()
          && !service.state("late", "user").value("loading").toBool());
    const int clearedVersion = service.version();
    deliver(api, late, QStringList() << "old-account", true);
    check("late response cannot restore cleared rows", rowCount(service.state("late", "user")) == 0 && service.version() == clearedVersion);
    service.load("late", "user");
    const int fresh = id(manager);
    deliver(api, late, QStringList() << "old-account", true);
    check("late response cannot complete a newer request", service.state("late", "user").value("loading").toBool());
    deliver(api, fresh, QStringList() << "new-account", false);
    check("fresh account response owns new state", service.state("late", "user").value("items").toList().first().toMap().value("id") == "new-account");
    service.load("late", "user");
    check("refresh starts again at first page", page(manager) == 1 && rowCount(service.state("late", "user")) == 0);
    const int last = id(manager);
    service.clear();
    check("late failure fixture signal", QMetaObject::invokeMethod(&api, "requestFailed", Qt::DirectConnection,
          Q_ARG(int, last), Q_ARG(QString, tag), Q_ARG(QString, error), Q_ARG(QVariantMap, detail)));
    check("late failure leaves cleared state untouched", service.state("late", "user").value("error").toString().isEmpty());
    const int beforeEmpty = manager->count;
    service.load(" ", "thread");
    check("empty query starts no request", manager->count == beforeEmpty);
    std::printf("Search service: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
