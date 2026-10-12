#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QVariantMap>
#include <QUrl>
#include <QDir>
#include <QDateTime>
#include <QStringList>
#include "util/TbJson.hpp"
#include "util/TbString.hpp"
#define private public
#include "net/TbHttpClient.hpp"
#undef private
#include "api/TbFavoriteService.hpp"
#include "net/TbSigner.hpp"
#include "util/TbMd5.hpp"
#include "session/TbSession.hpp"
#include "model/TbUserDataCache.hpp"
#include <cstdio>
using namespace tieba;
static int checks = 0, failures = 0;
static void check(const char *name, bool value) {
    ++checks; if (!value) ++failures;
    std::printf("%s %s\n", value ? "OK" : "FAIL", name);
}
class FavoriteReply : public QNetworkReply {
public:
    FavoriteReply(const QNetworkRequest &request, QNetworkAccessManager::Operation op, QObject *parent) : QNetworkReply(parent) {
        setRequest(request); setUrl(request.url()); setOperation(op);
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    }
    void abort() {}
protected:
    qint64 readData(char *, qint64) { return -1; }
};
class FavoriteManager : public QNetworkAccessManager {
public:
    explicit FavoriteManager(QObject *parent) : QNetworkAccessManager(parent), count(0), last(0) {}
    int count; FavoriteReply *last; QByteArray body;
protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &request, QIODevice *outgoing) {
        ++count; body = outgoing ? outgoing->readAll() : QByteArray();
        last = new FavoriteReply(request, op, this); return last; // No real network calls, including mutations.
    }
};
struct Fixture {
    TbHttpClient client; TbSession session; TbFavoriteService service; FavoriteManager *manager;
    explicit Fixture(const QString &name) : service(&client, &session) {
        delete client.m_manager; manager = new FavoriteManager(&client); client.m_manager = manager;
        const QString path = QDir::currentPath() + "/tests/tmp/favorite-test-" + QString::number(QDateTime::currentMSecsSinceEpoch());
        QDir().mkpath(path); session.setSettingsFileForTest(path + "/" + name + ".ini");
        service.setCacheDirectoryForTest(path + "/" + name + "-user-cache");
    }
    void login() {
        check("isolated fake credentials accepted", session.setCredentials(QString(192, QLatin1Char('A'))));
        session.setProfile("42", "fake-user", "Fake", ""); session.setTbs("fake-ticket");
    }
    int id() const { return manager->last->property("tb_request_id").toInt(); }
    QString tag() const { return manager->last->property("tb_tag").toString(); }
    void deliver(int reqId, const QString &reqTag, const QByteArray &body) {
        const TbJson json = TbJson::parse(body); TbHttpResponse response; response.ok = true; response.httpStatus = 200;
        check("fixture uses real HTTP signal", QMetaObject::invokeMethod(&client, "finished", Qt::DirectConnection,
              Q_ARG(int, reqId), Q_ARG(QString, reqTag), Q_ARG(tieba::TbJson, json), Q_ARG(tieba::TbHttpResponse, response)));
    }
    void deliverLast(const QByteArray &body) { deliver(id(), tag(), body); }
};
static QByteArray list(const QStringList &ids, bool more) {
    QString text("{\"error_code\":0,\"store_thread\":[");
    for (int i = 0; i < ids.size(); ++i) {
        if (i) text += ",";
        text += "{\"thread_id\":\"" + ids.at(i) + "\",\"title\":\"test\",\"author\":{\"name\":\"author\"}}";
    }
    return (text + "],\"has_more\":" + (more ? "1" : "0") + "}").toUtf8();
}
static QVariantMap row(const QString &tid, const QString &pid = "0") {
    QVariantMap result; result.insert("id", tid); result.insert("postId", pid); return result;
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const TbHttpRequest request = TbFavoriteService::listRequest(0, 40);
    check("list uses cloud endpoint", request.url == "https://c.tieba.baidu.com/c/f/post/threadstore");
    check("offset is item offset not page number", request.body.contains("offset=40") && request.body.contains("rn=20"));
    check("PC signature present", request.body == TbSigner::buildPcForm(request.params));
    const TbHttpRequest add = TbFavoriteService::mutationRequest(0, "90071992547409931234", "123", true, "fake-ticket");
    check("add endpoint", add.url.endsWith("/c/c/post/addstore"));
    bool correctlyOrdered = false;
    for (int i = 0; i < add.params.size(); ++i)
        if (add.params.at(i).first == "data") correctlyOrdered = add.params.at(i).second == "[{\"tid\":\"90071992547409931234\",\"pid\":\"123\",\"status\":1}]";
    check("tid and pid keep distinct string IDs", correctlyOrdered);
    check("remove endpoint", TbFavoriteService::mutationRequest(0, "10", "0", false, "tbs").url.endsWith("/c/c/post/rmstore"));
    check("invalid IDs cannot inject mutation data", TbFavoriteService::mutationRequest(0, "10\"bad", "0", true, "tbs").url.isEmpty());
    const TbHttpRequest mobile = TbFavoriteService::listRequest(0, 0, true);
    QMap<QString, QString> sorted;
    for (int i = 0; i < mobile.params.size(); ++i) sorted.insert(mobile.params.at(i).first, mobile.params.at(i).second);
    QString raw;
    QMap<QString, QString>::const_iterator it = sorted.constBegin();
    for (; it != sorted.constEnd(); ++it) raw += it.key() + "=" + it.value();
    check("mobile signature follows TiebaLite no-ampersand raw protocol", mobile.body.contains(
          QByteArray("sign=") + TbMd5::hex((raw + "tiebaclient!!!").toUtf8()).toUpper().toLatin1()));
    check("mobile form does not use old generic signature", mobile.body != TbSigner::buildSignedForm(mobile.params));
    check("mobile common fields include 15 digit IMEI and retry stats", mobile.body.contains("_phone_imei=000000000000000&") && mobile.body.contains("stErrorNums=0"));
    check("malformed success cannot appear empty", TbFavoriteService::parseList(TbJson::parse("{\"error_code\":0}")).contains("error"));
    check("API rejection retains diagnostic code", TbFavoriteService::parseList(TbJson::parse("{\"error_code\":110001}")).value("error").toString().contains("110001"));
    check("nested server error is honored", TbFavoriteService::parseList(TbJson::parse("{\"error_code\":0,\"error\":{\"errno\":3,\"errmsg\":\"denied\"}}")).contains("error"));
    check("explicit zero list is valid empty", !TbFavoriteService::parseList(TbJson::parse("{\"error_code\":0,\"store_thread\":0}")).contains("error"));
    const QVariantMap parsed = TbFavoriteService::parseList(TbJson::parse("{\"error_code\":0,\"store_thread\":[{\"thread_id\":\"90071992547409931234\",\"title\":\"<b>Title</b>\",\"agree\":{\"agree_num\":7},\"author\":{\"lz_uid\":\"30\",\"name\":\"author\",\"name_show\":\"Nick\"}}]}"));
    const QVariantMap parsedRow = parsed.value("items").toList().first().toMap();
    check("parser preserves long string IDs", parsedRow.value("id") == "90071992547409931234");
    check("parser title and author", parsedRow.value("title") == "Title" && parsedRow.value("authorName") == "Nick");
    check("parser shares real agree count contract", parsedRow.value("hasAgreeCount").toBool() && parsedRow.value("agreeCount").toInt() == 7);

    {
        Fixture f("signed-out");
        check("constructor is read and write idle", f.manager->count == 0);
        f.service.ensureLoaded(); f.service.toggle(row("10"));
        check("signed-out actions do not request network", f.manager->count == 0 && f.service.status("10") == -1);
    }
    {
        Fixture f("status-scan"); f.login();
        f.service.ensureStatus("900");
        check("checking status only reads a list", f.tag() == "favorites.list" && f.service.status("900") == -1);
        f.deliverLast(list(QStringList() << "100" << "200", true));
        check("unknown thread scans next cloud page", f.tag() == "favorites.list" && f.manager->body.contains("offset=2"));
        f.deliverLast(list(QStringList() << "900", false));
        check("read-only status scan never mutates", f.manager->count == 2 && f.service.status("900") == 1);
        f.service.toggle(row("900"));
        check("confirmed stored thread chooses remove", f.tag() == "favorites.remove");
        const int current = f.id(); const QString currentTag = f.tag();
        session_unused:
        f.session.setTbs("new-ticket"); f.session.setProfile("42", "fake-user", "Renamed", "");
        check("same-account metadata keeps mutation in flight", f.service.busy());
        f.deliver(current, currentTag, "{\"error_code\":3}");
        check("write failure preserves cloud state", f.service.status("900") == 1 && !f.service.busy());
        f.service.toggle(row("900"));
        check("explicit retry repeats remove", f.tag() == "favorites.remove");
        f.deliverLast("{\"error_code\":0}");
        check("successful write refreshes cloud list", f.tag() == "favorites.list" && f.service.status("900") == 0);
        f.deliverLast("{\"error_code\":0,\"store_thread\":[]}");
        check("empty refreshed cloud list confirms absent state", f.service.rows().isEmpty() && f.service.status("900") == 0);
    }
    {
        Fixture f("unknown-toggle"); f.login(); f.service.toggle(row("777", "123"));
        check("unknown toggle cannot immediately write", f.tag() == "favorites.list");
        f.deliverLast("{\"error_code\":0,\"store_thread\":[]}");
        check("confirmed absence chooses add", f.tag() == "favorites.add" && f.manager->body.contains("123"));
        const int oldId = f.id(); const QString oldTag = f.tag();
        check("isolated new account credential accepted", f.session.setCredentials(QString(192, QLatin1Char('B'))));
        const int previousCount = f.manager->count;
        f.deliver(oldId, oldTag, "{\"error_code\":0}");
        check("late write cannot leak state across account switch", f.service.status("777") == -1 && f.service.rows().isEmpty() && f.manager->count == previousCount);
    }
    {
        Fixture f("read-error"); f.login(); f.service.toggle(row("777"));
        f.deliverLast("{\"error_code\":110001}");
        check("PC rejection attempts one bounded read fallback", f.tag() == "favorites.list" && f.manager->body.contains("_client_version=11.10.8.6"));
        f.deliverLast("{\"error_code\":110001}");
        check("unconfirmed status never mutates", f.manager->count == 2 && f.service.status("777") == -1 && !f.service.busy());
        check("missing STOKEN diagnostic is specific", f.service.state().value("error").toString().contains("STOKEN") && f.service.state().value("error").toString().contains("110001"));
    }
    {
        Fixture f("pagination-retry"); f.login(); f.service.refresh();
        f.deliverLast(list(QStringList() << "10" << "20", true));
        f.service.loadMore(); f.deliverLast("{\"error_code\":3}");
        check("failed page keeps rows and previous offset", f.service.rows().size() == 2 && f.service.state().value("offset").toInt() == 2);
        f.service.retry(); check("retry uses failed item offset", f.manager->body.contains("offset=2"));
        f.deliverLast(list(QStringList() << "20" << "30", false));
        check("successful append deduplicates", f.service.rows().size() == 3 && f.service.status("999") == 0);
    }
    {
        const QString shared = QDir::currentPath() + "/tests/tmp/favorites-persistent-" + QString::number(QDateTime::currentMSecsSinceEpoch());
        QVariantMap snapshot;
        {
            Fixture f("cache-seed"); f.login(); f.service.setCacheDirectoryForTest(shared);
            f.service.ensureLoaded(); f.deliverLast(list(QStringList() << "100" << "200", true));
            f.service.loadMore(); f.deliverLast(list(QStringList() << "300" << "400", true));
            snapshot.insert("items", f.service.rows());
            check("multiple cloud pages are persisted", f.service.rows().size() == 4 && f.service.state().value("offset").toInt() == 4);
        }
        {
            Fixture f("cache-reopen"); f.login(); f.service.setCacheDirectoryForTest(shared);
            f.service.ensureLoaded();
            check("reopen shows cache before network response", f.service.rows().size() == 4 && f.service.state().value("loaded").toBool());
            check("cached rows refresh in background once", f.manager->count == 1 && f.service.state().value("refreshing").toBool());
            f.service.ensureLoaded();
            check("ensure during cached refresh does not duplicate reads", f.manager->count == 1);
            f.deliverLast(list(QStringList() << "100" << "200", true));
            QVariantMap current; current.insert("items", f.service.rows());
            check("identical first page preserves cached later pages", TbUserDataCache::equivalent(snapshot, current) && f.service.rows().size() == 4);
            check("identical first page preserves pagination offset", f.service.state().value("offset").toInt() == 4);
            f.service.toggle(row("400"));
            check("cached-only favorite must be verified before writing", f.tag() == "favorites.list" && f.manager->body.contains("offset=2"));
            f.deliverLast(list(QStringList() << "300" << "400", true));
            check("verified cached favorite chooses removal", f.tag() == "favorites.remove");
            f.service.clear(); // Cancel the fixture before completing the simulated write.
        }
        {
            Fixture f("cache-read-failure"); f.login(); f.service.setCacheDirectoryForTest(shared);
            f.service.ensureLoaded(); f.deliverLast("{\"error_code\":3}");
            check("failed background refresh retains cached rows", f.service.rows().size() == 4 && !f.service.state().value("error").toString().isEmpty());
        }
        {
            Fixture f("cache-other-account"); f.service.setCacheDirectoryForTest(shared);
            check("second isolated account is accepted", f.session.setCredentials(QString(192, QLatin1Char('B'))));
            f.service.ensureLoaded();
            check("favorite cache remains account-isolated", f.service.rows().isEmpty() && !f.service.state().value("loaded").toBool());
        }
    }
    std::printf("Favorites: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
