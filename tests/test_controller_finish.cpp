#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QDir>
#include <QDateTime>
#include <QUrl>
#include <QVariantMap>
#include "util/TbJson.hpp"
#define private public
#include "net/TbHttpClient.hpp"
#undef private
#include "TiebaController.hpp"
#include "session/TbSession.hpp"
#include <bb/cascades/ArrayDataModel>
#include <cstdio>
using namespace tieba;
static int checks = 0, failures = 0;
static void check(const char *label, bool ok) {
    ++checks; if (!ok) ++failures; std::printf("%s %s\n", ok ? "OK" : "FAIL", label);
}
class FinishReply : public QNetworkReply {
public:
    FinishReply(const QNetworkRequest &r, QNetworkAccessManager::Operation op, QObject *p) : QNetworkReply(p) {
        setRequest(r); setUrl(r.url()); setOperation(op); open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    }
    void abort() {}
protected:
    qint64 readData(char *, qint64) { return -1; }
};
class FinishManager : public QNetworkAccessManager {
public:
    explicit FinishManager(QObject *p) : QNetworkAccessManager(p) {}
    QList<FinishReply *> replies;
    QList<QByteArray> bodies;
    FinishReply *last() const { return replies.isEmpty() ? 0 : replies.last(); }
    QByteArray body() const { return bodies.isEmpty() ? QByteArray() : bodies.last(); }
    FinishReply *tagged(const QString &tag) const {
        for (int i = replies.size() - 1; i >= 0; --i)
            if (replies.at(i)->property("tb_tag").toString().startsWith(tag)) return replies.at(i);
        return 0;
    }
protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &r, QIODevice *data) {
        FinishReply *reply = new FinishReply(r, op, this);
        replies.append(reply); bodies.append(data ? data->readAll() : QByteArray()); return reply;
    }
};
struct Fixture {
    TiebaController c; FinishManager *manager;
    explicit Fixture(const QString &dir) : c(0, dir) {
        const QString expected = QDir::cleanPath(QDir::fromNativeSeparators(dir)) + "/";
        const QString actual = QDir::cleanPath(QDir::fromNativeSeparators(c.forumCacheFileForTest()));
        if (!actual.startsWith(expected, Qt::CaseInsensitive))
            qFatal("Controller fixture directory not isolated; refusing writes");
        QDir().mkpath(dir);
        c.setCacheDirForTest(dir, dir + "/legacy"); c.setSessionFileForTest(dir + "/session.ini");
        delete c.httpClient()->m_manager;
        manager = new FinishManager(c.httpClient()); c.httpClient()->m_manager = manager;
        c.session()->setCredentials(QString(192, QLatin1Char('A')));
        c.session()->setProfile("42", "fake", "Fake", "");
    }
    void deliver(FinishReply *reply, const QByteArray &body) {
        check("fake reply exists", reply != 0); if (!reply) return;
        const TbJson json = TbJson::parse(body);
        TbHttpResponse response; response.ok = true; response.httpStatus = 200; response.body = body;
        check("real client signal delivered", QMetaObject::invokeMethod(c.httpClient(), "finished", Qt::DirectConnection,
            Q_ARG(int, reply->property("tb_request_id").toInt()), Q_ARG(QString, reply->property("tb_tag").toString()),
            Q_ARG(tieba::TbJson, json), Q_ARG(tieba::TbHttpResponse, response)));
    }
    void deliverLast(const QByteArray &body) { deliver(manager->last(), body); }
    void tagged(const QString &tag, const QByteArray &body) { deliver(manager->tagged(tag), body); }
};
static QByteArray notifications(const char *ids, bool more) {
    const QStringList values = QString::fromLatin1(ids).split(','); QByteArray text("{\"error_code\":0,\"reply_list\":[");
    for (int i = 0; i < values.size(); ++i) {
        if (i) text += ',';
        text += "{\"thread_id\":\"" + values.at(i).toLatin1() + "\",\"post_id\":\"" + values.at(i).toLatin1() + "\",\"time\":1}";
    }
    return text + "],\"page\":{\"has_more\":" + (more ? "1" : "0") + "}}";
}
static QByteArray userThreads(const char *ids, bool more) {
    const QStringList values = QString::fromLatin1(ids).split(','); QByteArray text("{\"error_code\":0,\"post_list\":[");
    for (int i = 0; i < values.size(); ++i) {
        if (i) text += ',';
        text += "{\"thread_id\":\"" + values.at(i).toLatin1() + "\",\"title\":\"stable\"}";
    }
    return text + "],\"has_more\":" + (more ? "1" : "0") + "}";
}
static QByteArray threadPage(int page, int total, const char *posts) {
    return QByteArray("{\"error_code\":0,\"thread\":{\"id\":\"900\",\"title\":\"test\"},\"page\":{\"current_page\":")
        + QByteArray::number(page) + ",\"total_page\":" + QByteArray::number(total) + ",\"has_more\":"
        + (page < total ? "1" : "0") + "},\"post_list\":[" + posts + "]}";
}
int main(int argc, char **argv) {
    const QString root = QDir::currentPath() + "/tests/tmp/controller-finish-" + QString::number(QDateTime::currentMSecsSinceEpoch());
    QDir().mkpath(root + "/temp");
    qputenv("TEMP", (root + "/temp").toLocal8Bit()); qputenv("TMP", (root + "/temp").toLocal8Bit());
    QCoreApplication app(argc, argv);
    // Qt 4 Windows ignores HOME overrides. Each fixture instead supplies its
    // private directory at construction, before library/cache/settings loading.
    {
        Fixture f(root + "/video"); f.c.setHideVideoThreads(false);
        f.c.loadForumPage("FakeForum");
        f.deliverLast("{\"error_code\":0,\"page_data\":{\"feed_list\":[{\"layout\":\"feed\",\"feed\":{\"business_info_map\":{\"thread_id\":\"1\"},\"components\":[{\"component\":\"feed_video\"}]}},{\"layout\":\"feed\",\"feed\":{\"business_info_map\":{\"thread_id\":\"2\",\"is_video\":0},\"components\":[]}}]}}");
        check("forum initially shows video and normal thread", f.c.threads()->size() == 2);
        const int requests = f.manager->replies.size();
        f.c.setHideVideoThreads(true);
        check("video switch immediately hides current forum video", f.c.threads()->size() == 1 && f.c.threads()->value(0).toMap().value("id") == "2");
        f.c.setHideVideoThreads(false);
        check("disabling immediately restores forum rows", f.c.threads()->size() == 2);
        check("forum switch does not need a new request", f.manager->replies.size() == requests);
        f.c.loadStream("recommend");
        f.deliverLast("{\"error_code\":0,\"thread_list\":[{\"tid\":\"1\",\"video_info\":{}},{\"tid\":\"2\",\"media\":[{\"type\":3}]},{\"tid\":\"3\",\"video_info\":{},\"is_top\":1}],\"has_more\":0}");
        check("recommendation initially has all rows", f.c.recommendedFeed()->size() == 3);
        f.c.setHideVideoThreads(true);
        check("picture and top video survive filtering", f.c.recommendedFeed()->size() == 2
            && f.c.recommendedFeed()->value(0).toMap().value("id") == "2" && f.c.recommendedFeed()->value(1).toMap().value("id") == "3");
        f.c.setHideVideoThreads(false); check("recommendation switch restores hidden row", f.c.recommendedFeed()->size() == 3);
    }
    const QString cacheDir = root + "/cache";
    {
        Fixture f(cacheDir);
        f.c.loadStream("reply"); f.deliverLast(notifications("10,20", true));
        f.c.loadStream("reply", true); f.deliverLast(notifications("30,40", true));
        check("notification multipage cache seeded", f.c.replyNotifications()->size() == 4 && f.c.streamState("reply").value("page").toInt() == 2);
        f.c.loadUserPage("77", "threads"); f.tagged("userpage.threads.77", userThreads("100,200", true));
        f.tagged("userpage.profile.77", "{\"error_code\":0,\"user\":{\"id\":\"77\",\"name\":\"Fake\",\"concern_num\":3,\"fans_num\":4},\"user_agree_info\":{\"total_agree_num\":5}}");
        f.c.loadUserPage("77", "threads", true); f.tagged("userpage.threads.77", userThreads("300,400", true));
        check("user multipage cache seeded", f.c.userPageState("77", "threads").value("items").toList().size() == 4);
    }
    {
        Fixture f(cacheDir); f.c.loadStream("reply");
        check("notifications visible before fresh reply", f.c.replyNotifications()->size() == 4 && f.c.streamState("reply").value("loading").toBool());
        f.deliverLast("{\"error_code\":3,\"error_msg\":\"offline\"}");
        check("notification refresh failure keeps cache", f.c.replyNotifications()->size() == 4 && !f.c.streamState("reply").value("error").toString().isEmpty());
        f.c.loadStream("reply"); f.deliverLast(notifications("10,20", true));
        check("same notification first page preserves later pages", f.c.replyNotifications()->size() == 4 && f.c.streamState("reply").value("page").toInt() == 2);
        f.c.loadUserPage("77", "threads");
        check("user rows visible before fresh reply", f.c.userPageState("77", "threads").value("items").toList().size() == 4);
        check("profile statistics visible before fresh reply", f.c.userPageState("77", "profile").value("profile").toMap().value("fansCount").toInt() == 4);
        f.tagged("userpage.profile.77", "{\"error_code\":3,\"error_msg\":\"offline\"}");
        check("profile background failure retains statistics", f.c.userPageState("77", "profile").value("profile").toMap().value("agreeCount").toInt() == 5);
        f.tagged("userpage.threads.77", "{\"error_code\":3,\"error_msg\":\"offline\"}");
        check("user refresh failure keeps cache", f.c.userPageState("77", "threads").value("items").toList().size() == 4);
        f.c.loadUserPage("77", "threads"); f.tagged("userpage.threads.77", userThreads("100,200", true));
        check("same user first page preserves later pages", f.c.userPageState("77", "threads").value("items").toList().size() == 4
            && f.c.userPageState("77", "threads").value("page").toInt() == 2);
        f.c.logout(); f.c.session()->setCredentials(QString(192, QLatin1Char('B'))); f.c.session()->setProfile("84", "other", "Other", "");
        f.c.loadStream("reply"); f.c.loadUserPage("77", "threads");
        check("other account cannot see previous notification cache", f.c.replyNotifications()->size() == 0);
        check("other account cannot see previous user cache", f.c.userPageState("77", "threads").value("items").toList().isEmpty());
        check("other account cannot see previous profile cache", f.c.userPageState("77", "profile").value("profile").toMap().isEmpty());
    }
    {
        Fixture f(root + "/navigation"); f.c.loadThread("900", 2, false, true);
        check("reverse propagated to real request", f.manager->body().contains("r=1") && f.manager->body().contains("last=1"));
        f.deliverLast(threadPage(2, 3, "{\"id\":\"501\",\"floor\":51,\"content\":[{\"type\":0,\"text\":\"body\"}]},{\"id\":\"503\",\"floor\":53,\"content\":[{\"type\":0,\"text\":\"body\"}]}"));
        const int count = f.manager->replies.size(); f.c.jumpThreadFloor("900", "53", false, true);
        check("existing floor uses actual row index", f.c.threadNavigationState("900").value("targetIndex").toInt() == 1 && f.manager->replies.size() == count);
        const QVariantMap beforeSub = f.c.threadNavigationState("900"); f.c.loadSubPosts("900", "503", 1);
        f.deliverLast("{\"error_code\":0,\"subpost_list\":[{\"id\":\"777\",\"content\":[{\"type\":0,\"text\":\"reply\"}]}],\"page\":{\"current_page\":1,\"has_more\":0}}");
        check("subposts success populated its own model", f.c.subPosts()->size() == 1);
        check("subposts do not overwrite thread navigation", f.c.threadNavigationState("900") == beforeSub);
        f.c.jumpThreadFloor("900", "52", false, false);
        for (int i = 0; i < 4 && f.c.threadNavigationState("900").value("loading").toBool(); ++i)
            f.deliverLast(threadPage(1, 1, "{\"id\":\"501\",\"floor\":51},{\"id\":\"503\",\"floor\":53}"));
        check("deleted floor reports no target", f.c.threadNavigationState("900").value("targetIndex").toInt() < 0
            && !f.c.threadNavigationState("900").value("loading").toBool() && !f.c.threadNavigationState("900").value("error").toString().isEmpty());
    }
    {
        Fixture f(root + "/anchor"); f.c.openThreadPost("900", "503");
        check("anchor starts PID request", f.manager->body().contains("pid=503"));
        f.deliverLast(threadPage(1, 4, "{\"id\":\"503\",\"floor\":53,\"content\":[{\"type\":0,\"text\":\"body\"}]}"));
        check("resolved PID switches to PC page", f.manager->last()->url().path().endsWith("page_pc") && !f.manager->body().contains("pid=503"));
        f.deliverLast(threadPage(2, 4, "{\"id\":\"501\",\"floor\":51},{\"id\":\"503\",\"floor\":53}"));
        const QVariantMap anchor = f.c.threadNavigationState("900");
        check("anchor located after PC response", anchor.value("targetIndex").toInt() == 1 && !anchor.value("loading").toBool());
        check("anchor preserves PC page and more flag", anchor.value("page").toInt() == 2 && anchor.value("hasMore").toBool());
        f.c.loadThread("900", anchor.value("page").toInt() + 1, false, false, true);
        check("load more continues next resolved page", f.manager->body().contains("pn=3"));
    }
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
