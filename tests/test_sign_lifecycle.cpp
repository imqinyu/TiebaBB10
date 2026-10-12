// Fully offline lifecycle tests. Only the transport is replaced; production
// signing orchestration and the real TbHttpClient.finished Qt signal are used.
#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QVariantMap>
#include <QUrl>
#include <QStringList>
#include <QDir>
#include <QDateTime>
#include <QEventLoop>
#include <QTimer>
#include "util/TbJson.hpp"
#include "util/TbString.hpp"
#define private public
#include "net/TbHttpClient.hpp"
#undef private
#include "api/TbSignService.hpp"
#include "session/TbSession.hpp"
#include <cstdio>

using namespace tieba;
static int checks = 0, failures = 0;
static void check(const char *name, bool pass) {
    ++checks; if (!pass) ++failures;
    std::printf("%s %s\n", pass ? "OK" : "FAIL", name);
}
class PendingSignReply : public QNetworkReply {
public:
    PendingSignReply(const QNetworkRequest &request, QNetworkAccessManager::Operation operation,
                     QObject *parent) : QNetworkReply(parent) {
        setRequest(request); setUrl(request.url()); setOperation(operation);
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    }
    void abort() {}
protected:
    qint64 readData(char *, qint64) { return -1; }
};
class OfflineSignManager : public QNetworkAccessManager {
public:
    explicit OfflineSignManager(QObject *parent) : QNetworkAccessManager(parent), count(0), last(0) {}
    int count;
    PendingSignReply *last;
    QList<QByteArray> bodies;
protected:
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request, QIODevice *outgoing) {
        ++count;
        bodies.append(outgoing ? outgoing->readAll() : QByteArray());
        last = new PendingSignReply(request, operation, this);
        return last; // Never call the real network backend.
    }
};
struct Fixture {
    TbHttpClient client;
    TbSession session;
    TbSignService service;
    OfflineSignManager *manager;
    explicit Fixture(const QString &name) : service(&client, &session) {
        delete client.m_manager;
        manager = new OfflineSignManager(&client);
        client.m_manager = manager;
        const QString dir = QDir::currentPath() + "/tests/tmp/sign-lifecycle-"
                + QString::number(QDateTime::currentMSecsSinceEpoch());
        QDir().mkpath(dir);
        session.setSettingsFileForTest(dir + "/" + name + ".ini");
    }
    void login(QChar marker = QLatin1Char('A')) {
        check("isolated fake credential is accepted", session.setCredentials(QString(192, marker)));
    }
    int id() const { return manager->last->property("tb_request_id").toInt(); }
    QString tag() const { return manager->last->property("tb_tag").toString(); }
    void deliver(int requestId, const QString &requestTag, const QByteArray &body, bool ok = true, int http = 200, int networkError = 0) {
        const TbJson json = TbJson::parse(body);
        TbHttpResponse response; response.ok = ok; response.httpStatus = ok || http != 200 ? http : 0;
        response.networkError = networkError;
        check("fixture invokes real HTTP finished signal", QMetaObject::invokeMethod(
                  &client, "finished", Qt::DirectConnection, Q_ARG(int, requestId),
                  Q_ARG(QString, requestTag), Q_ARG(tieba::TbJson, json), Q_ARG(tieba::TbHttpResponse, response)));
    }
    void deliverLast(const QByteArray &body, bool ok = true) { deliver(id(), tag(), body, ok); }
};
static const char *forums = "{\"error_code\":0,\"forum_list\":{\"non-gconforum\":["
        "{\"id\":\"1\",\"name\":\"first\"},{\"id\":\"2\",\"name\":\"second\"}],"
        "\"gconforum\":[{\"id\":\"3\",\"name\":\"already\",\"is_sign\":1}]},\"has_more\":0}";
static const char *ticket = "{\"error_code\":0,\"user\":{\"id\":\"42\"},\"anti\":{\"tbs\":\"fake-ticket\"}}";
static void waitMs(int milliseconds) {
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, SLOT(quit()));
    loop.exec();
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    {
        Fixture f("signed-out");
        check("construction never starts network work", f.manager->count == 0);
        f.service.start();
        check("signed-out start makes no request", f.manager->count == 0 && !f.service.loading());
        check("signed-out start reports error", !f.service.state().value("error").toString().isEmpty());
    }
    {
        Fixture f("cancelled"); f.login(); f.service.start();
        check("start first loads followed forums", f.manager->count == 1 && f.tag() == "sign.forums");
        f.deliverLast(forums);
        check("forum response schedules ticket before signing", f.manager->count == 2 && f.tag() == "sign.tbs");
        const int oldId = f.id(); const QString oldTag = f.tag();
        f.service.cancel();
        const int version = f.service.version();
        f.deliver(oldId, oldTag, ticket);
        check("cancel rejects late ticket without sending sign request", f.manager->count == 2
              && f.service.version() == version && !f.service.loading());
        check("cancel marks pending rows cancelled", f.service.rows().at(0).toMap().value("status") == "cancelled");
    }
    {
        Fixture f("account-switch"); f.login(); f.service.start();
        f.deliverLast(forums); f.deliverLast(ticket);
        check("valid ticket starts exactly one forum sign", f.manager->count == 3 && f.tag() == "sign.submit");
        const int oldId = f.id(); const QString oldTag = f.tag();
        f.login(QLatin1Char('B'));
        check("account switch cancels and clears prior account rows", !f.service.loading() && f.service.rows().isEmpty());
        const int version = f.service.version();
        f.deliver(oldId, oldTag, "{\"error_code\":0}");
        check("account switch rejects late sign success", f.manager->count == 3
              && f.service.version() == version && f.service.rows().isEmpty());
    }
    {
        Fixture f("metadata"); f.login(); f.service.start();
        f.session.setProfile("42", "account", "nickname", "fake-portrait");
        check("same-account profile metadata does not cancel", f.service.loading() && f.manager->count == 1);
        f.deliverLast(forums);
        f.session.setTbs("unrelated-profile-ticket");
        check("same-account ticket metadata does not cancel", f.service.loading() && f.manager->count == 2);
        f.deliverLast(ticket);
        check("same-account metadata still allows first sign", f.manager->count == 3 && f.tag() == "sign.submit");
        f.service.cancel();
    }
    {
        Fixture f("sequence-retry"); f.login(); f.service.start();
        f.deliverLast(forums); f.deliverLast(ticket);
        check("only first unsiged forum is initially submitted", f.manager->count == 3
              && f.manager->bodies.last().contains("fid=1&"));
        f.deliverLast("{\"error_code\":3,\"error_msg\":\"fixture failure\"}");
        check("first failure is recorded while queue remains active", f.service.loading()
              && f.service.rows().at(0).toMap().value("status") == "failed");
        check("no concurrent sign request is dispatched", f.manager->count == 3);
        waitMs(3300);
        check("next forum does not start before four second interval", f.manager->count == 3);
        waitMs(1000);
        check("timer dispatches next forum after interval", f.manager->count == 4
              && f.manager->bodies.last().contains("fid=2&"));
        f.deliverLast("{\"error_code\":0}");
        check("queue completes and retains failed row", !f.service.loading()
              && f.service.state().value("completed").toInt() == 3
              && f.service.state().value("failedCount").toInt() == 1);
        check("server already-signed row was not submitted", f.manager->count == 4);
        f.service.retryFailed();
        check("retry obtains a fresh ticket", f.manager->count == 5 && f.tag() == "sign.tbs");
        f.deliverLast(ticket);
        check("retry submits only the failed forum", f.manager->count == 6
              && f.manager->bodies.last().contains("fid=1&"));
        f.deliverLast("{\"error_code\":0}");
        check("retry completes without repeating successful forums", f.manager->count == 6
              && !f.service.loading() && f.service.state().value("signedCount").toInt() == 3);
        f.service.retryFailed();
        check("retry with no failures sends nothing", f.manager->count == 6);
    }
    {
        const char *bodies[] = {
            "{}", "<html>blocked</html>", "{\"error_code\":4}",
            "{\"error_code\":0,\"user\":{\"id\":\"42\"},\"anti\":{}}",
            "{\"error_code\":0,\"user\":{\"id\":\"99\"},\"anti\":{\"tbs\":\"fake-ticket\"}}",
            "{\"error_code\":0,\"user\":{\"id\":\"0\"},\"anti\":{\"tbs\":\"fake-ticket\"}}",
            "{\"error_code\":0,\"anti\":{\"tbs\":\"fake-ticket\"}}"
        };
        const char *messages[] = { "网络错误 6", "无法解析", "登录凭据未通过", "没有返回签到票据", "账号不一致", "登录凭据未通过", "登录凭据未通过" };
        for (int i = 0; i < 7; ++i) {
            Fixture f(QString("ticket-failure-%1").arg(i)); f.login();
            f.session.setProfile("42", "account", "nickname", "fake-portrait");
            f.service.start(); f.deliverLast(forums);
            check("ticket request is client login rather than web tbs", f.manager->last->url().path() == "/c/s/login");
            f.deliver(f.id(), f.tag(), bodies[i], i != 0, i == 0 ? 0 : 200, i == 0 ? 6 : 0);
            check("ticket failure reports the specific cause", f.service.state().value("error").toString().contains(QString::fromUtf8(messages[i])));
            check("ticket failure stops before any sign mutation", !f.service.loading() && f.manager->count == 2);
            check("ticket failure preserves selected account", f.session.isLoggedIn() && f.session.uid() == "42");
        }
    }
    std::printf("Sign lifecycle: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
