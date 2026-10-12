#include "TiebaController.hpp"
#include "model/TbPcParsers.hpp"
#include "net/TbSigner.hpp"
#include "api/TbApi.hpp"
#include "session/TbSession.hpp"
#include "util/TbString.hpp"
#include <bb/cascades/ArrayDataModel>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QVariantMap>
#include <stdio.h>

static int checks = 0;
static int failures = 0;
static void check(const char *name, bool ok)
{
    ++checks;
    if (!ok) ++failures;
    printf("  %s %s\n", ok ? "OK  " : "FAIL", name);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    TiebaController c;
    const QString testDir = QDir::currentPath() + QLatin1String("/tests/tmp/ui-snapshot-test");
    QDir().mkpath(testDir);
    c.setCacheDirForTest(testDir);
    c.setSessionFileForTest(testDir + QLatin1String("/session.ini"));
    {
        TiebaController pages;
        pages.setCacheDirForTest(testDir + "/userpages");
        pages.setSessionFileForTest(testDir + "/userpages/session.ini");
        pages.loadUserPage("11", "threads");
        const int first = pages.userPageState("11", "threads").value("requestId").toInt();
        const int profileRequest = pages.userPageState("11", "profile").value("requestId").toInt();
        check("user list loading also requests real profile metadata", pages.userPageState("11", "profile").value("loading").toBool());
        pages.loadUserPage("22", "threads");
        check("separate user pages can load independently", pages.userPageState("11", "threads").value("loading").toBool()
              && pages.userPageState("22", "threads").value("loading").toBool());
        QVariantMap row;
        row.insert("id", "100");
        row.insert("resultKind", "thread");
        QVariantMap response;
        response.insert("items", QVariantList() << row);
        response.insert("hasMore", true);
        QMetaObject::invokeMethod(&pages, "onUserPageLoaded", Qt::DirectConnection,
            Q_ARG(int, first), Q_ARG(QString, QString("11")), Q_ARG(QString, QString("threads")), Q_ARG(QVariantMap, response));
        check("profile list response fills only the requested user's page", pages.userPageState("11", "threads").value("items").toList().size() == 1
              && pages.userPageState("22", "threads").value("items").toList().isEmpty());
        check("profile loading is separate from forum and thread loading", !pages.loading());
        QVariantMap metadata;
        QVariantMap user;
        user.insert("id", "11");
        user.insert("fansCount", 12);
        metadata.insert("profile", user);
        QMetaObject::invokeMethod(&pages, "onUserPageLoaded", Qt::DirectConnection,
            Q_ARG(int, profileRequest), Q_ARG(QString, QString("11")), Q_ARG(QString, QString("profile")), Q_ARG(QVariantMap, metadata));
        check("remote profile publishes actual statistics without inventing absent values", pages.userPageState("11", "profile")
              .value("profile").toMap().value("fansCount").toInt() == 12 && !pages.userPageState("11", "profile").value("profile").toMap().contains("agreeCount"));
        pages.loadUserPage("11", "threads", true);
        int request = pages.userPageState("11", "threads").value("requestId").toInt();
        QMetaObject::invokeMethod(&pages, "onRequestFailed", Qt::DirectConnection,
            Q_ARG(int, request), Q_ARG(QString, QString("userpage.threads.11")), Q_ARG(QString, QString("offline")), Q_ARG(QVariantMap, QVariantMap()));
        check("pagination error preserves the list and committed page", pages.userPageState("11", "threads").value("items").toList().size() == 1
              && pages.userPageState("11", "threads").value("page").toInt() == 1 && pages.userPageState("11", "threads").value("error").toString() == "offline");
        pages.loadUserPage("11", "threads", true);
        request = pages.userPageState("11", "threads").value("requestId").toInt();
        QVariantMap second;
        second.insert("id", "101");
        response.insert("items", QVariantList() << row << second);
        QMetaObject::invokeMethod(&pages, "onUserPageLoaded", Qt::DirectConnection,
            Q_ARG(int, request), Q_ARG(QString, QString("11")), Q_ARG(QString, QString("threads")), Q_ARG(QVariantMap, response));
        check("profile pagination appends and deduplicates overlapping IDs", pages.userPageState("11", "threads").value("items").toList().size() == 2
              && pages.userPageState("11", "threads").value("page").toInt() == 2);
        pages.loadUserPage("11", "threads", true);
        request = pages.userPageState("11", "threads").value("requestId").toInt();
        QMetaObject::invokeMethod(&pages, "onUserPageLoaded", Qt::DirectConnection,
            Q_ARG(int, request), Q_ARG(QString, QString("11")), Q_ARG(QString, QString("threads")), Q_ARG(QVariantMap, response));
        check("repeated pages stop profile load-more loops", !pages.userPageState("11", "threads").value("hasMore").toBool());
        pages.loadUserPage("11", "forums");
        request = pages.userPageState("11", "forums").value("requestId").toInt();
        pages.logout();
        QMetaObject::invokeMethod(&pages, "onUserPageLoaded", Qt::DirectConnection,
            Q_ARG(int, request), Q_ARG(QString, QString("11")), Q_ARG(QString, QString("forums")), Q_ARG(QVariantMap, response));
        check("account reset rejects old profile responses and clears all users", pages.userPageState("11", "threads").value("items").toList().isEmpty()
              && pages.userPageState("22", "threads").value("items").toList().isEmpty() && !pages.userPageState("11", "forums").value("loaded").toBool());
    }
    {
        using namespace tieba;
        const QString portrait = "tb.1.test.avatar";
        const QString small = "https://himg.bdimg.com/sys/portrait/item/" + portrait;
        check("bare portrait uses the current HTTPS avatar CDN", TbString::avatarUrl(portrait) == small);
        check("large portrait uses the current HTTPS avatar CDN", TbString::bigAvatarUrl(portrait)
              == "https://himg.bdimg.com/sys/portraith/item/" + portrait);
        check("old complete avatar URLs normalize to the same image", TbString::avatarUrl(
              "http://tb.himg.baidu.com/sys/portrait/item/" + portrait + "?t=1") == small);
        check("response timestamps do not split avatar cache keys", TbString::avatarUrl(portrait + "?t=2") == small);
        check("protocol-relative avatar URL is not treated as a portrait token", TbString::avatarUrl(
              "//himg.bdimg.com/sys/portrait/item/" + portrait + "?t=3") == small);
        check("unrelated URL query parameters are preserved", TbString::httpsImageUrl(
              "https://example.invalid/picture.jpg?t=4&sign=fixture") == "https://example.invalid/picture.jpg?t=4&sign=fixture");
        check("avatar normalization retains non-timestamp parameters", TbString::httpsImageUrl(
              small + "?t=5&size=fixture") == small + "?size=fixture");
        check("timestamp at the end leaves no trailing query separator", TbString::httpsImageUrl(
              small + "?size=fixture&t=6") == small + "?size=fixture");
        check("complete portraitn URLs use the same current CDN", TbString::httpsImageUrl(
              "http://tb.himg.baidu.com/sys/portraitn/item/" + portrait + "?t=7")
              == "https://himg.bdimg.com/sys/portraitn/item/" + portrait);
        check("empty and local avatar paths remain valid", TbString::avatarUrl("  ").isEmpty()
              && TbString::avatarUrl("asset:///images/default_avatar.png") == "asset:///images/default_avatar.png");
    }

    {
        const QString root = testDir + "/migration-" + QString::number(QDateTime::currentMSecsSinceEpoch());
        const QString legacy = root + "/legacy/config";
        const QString persistent = root + "/persistent";
        QDir().mkpath(legacy);
        QSettings oldSettings(legacy + "/settings.ini", QSettings::IniFormat);
        oldSettings.setValue("theme", 2);
        oldSettings.sync();
        QFile oldForums(legacy + "/forums.json");
        oldForums.open(QIODevice::WriteOnly);
        oldForums.write("{\"migration-fixture\":true}");
        oldForums.close();
        TiebaController migrated;
        migrated.setCacheDirForTest(persistent, legacy);
        check("configuration migration preserves the old theme", migrated.theme() == 2);
        check("forum cache migrates to the same persistent config directory", QFile::exists(migrated.forumCacheFileForTest())
              && migrated.forumCacheFileForTest().startsWith(persistent));
        check("successful migration removes obsolete temporary copies", !QFile::exists(legacy + "/settings.ini")
              && !QFile::exists(legacy + "/forums.json"));
        migrated.clearForumsCache();
        TiebaController restarted;
        restarted.setCacheDirForTest(persistent, legacy);
        check("restart retains migrated settings", restarted.theme() == 2);
        check("a cleared forum cache is not resurrected from the legacy directory", !QFile::exists(restarted.forumCacheFileForTest()));
        QSettings newer(persistent + "/config/settings.ini", QSettings::IniFormat);
        newer.setValue("theme", 1);
        newer.sync();
        oldSettings.setValue("theme", 2);
        oldSettings.sync();
        TiebaController conflict;
        conflict.setCacheDirForTest(persistent, legacy);
        check("new persistent settings win over an obsolete temporary copy", conflict.theme() == 1);
    }

    QVariantMap forum;
    forum.insert("id", "9007199254740993");
    forum.insert("name", "BlackBerry");
    forum.insert("level", 13);
    QVariantList forums;
    forums.append(forum);
    c.setForumsModelForTest(forums);
    const QVariantList result = c.loadedUiItems("forum", "BLACK");
    check("forum search ignores case", result.size() == 1);
    check("large IDs retain precision", result.at(0).toMap().value("id").toString() == "9007199254740993");
    check("result identifies row kind", result.at(0).toMap().value("resultKind").toString() == "forum");
    check("foreign user cannot see own followed forums", c.loadedUiItems("forum", "", "other-user").isEmpty());
    check("no match returns an empty list", c.loadedUiItems("forum", "no-match").isEmpty());
    check("no hidden fallback to unrelated category", c.loadedUiItems("unknown").isEmpty());

    QVariantMap thread;
    thread.insert("id", "101");
    thread.insert("authorId", "42");
    thread.insert("authorName", "Same display name");
    thread.insert("title", "A Cascades question");
    thread.insert("abstractText", "BlackBerry native controls");
    c.threads()->append(QVariant(thread));
    check("post search includes abstract", c.loadedUiItems("thread", "native").size() == 1);
    check("profile posts use the exact author ID", c.loadedUiItems("thread", "", "42").size() == 1);
    check("another profile does not show these posts", c.loadedUiItems("thread", "", "43").isEmpty());
    check("forum search scope rejects a different loaded forum", c.loadedUiItems("thread", "", "", "another-forum").isEmpty());

    QVariantMap post = thread;
    c.posts()->append(QVariant(post));
    post.insert("authorId", "43");
    c.subPosts()->append(QVariant(post));
    check("users deduplicate the same ID across models", c.loadedUiItems("user", "Same").size() == 2);
    check("user search can match exact ID", c.loadedUiItems("user", "42").size() == 1);
    QVariantMap author;
    author.insert("id", "88");
    author.insert("name", "actual-user-name");
    author.insert("displayName", "Nickname");
    QVariantMap source;
    source.insert("author", author);
    source.insert("bduss", "test-secret");
    const QVariantMap profile = c.uiUserProfile(source);
    check("profile supports nested post author data", profile.value("id").toString() == "88");
    check("copy-link username differs from display name", profile.value("userName").toString() == "actual-user-name");
    check("profile cannot expose credentials", !profile.contains("bduss") && !profile.contains("stoken"));
    check("unknown statistics are not invented", !profile.contains("fans") && !profile.contains("agreeCount"));
    const QVariantMap own = c.uiUserProfile(QVariantMap(), true);
    check("own page supports an empty logged-out profile", own.value("isSelf").toBool() && own.value("id").toString().isEmpty());
    c.setForumsModelForTest(QVariantList());
    check("search result is an independent snapshot", result.size() == 1 && c.loadedUiItems("forum").isEmpty());
    check("UI version changes when the source model changes", c.uiDataVersion() > 0);

    const tieba::TbJson feed = tieba::TbJson::parse(
        "{\"layout\":\"feed\",\"feed\":{\"business_info_map\":{"
        "\"thread_id\":\"101\",\"user_id\":\"9007199254740993\"},\"components\":[]}}");
    tieba::TbThread *parsed = tieba::TbPcParsers::parseFeedThread(feed);
    check("PC thread exposes a real author ID", parsed && parsed->authorId() == "9007199254740993");
    delete parsed;
    {
        using namespace tieba;
        TbPost body;
        body.setId("emoji-floor");
        body.setContentSegments(TbPcParsers::parseContentSegments(TbJson::parse(
            "[{\"type\":0,\"text\":\"Before\"},{\"type\":2,\"text\":\"image_emoticon1\","
            "\"c\":\"Body emoji\",\"src\":\"asset:///body.png\"},{\"type\":0,\"text\":\"After\"}]")));
        // Use local fixture paths so these controller checks never download.
        QVariantList bodySegments = body.contentSegments();
        QVariantMap bodyEmoji = bodySegments[1].toMap();
        bodyEmoji.insert("url", "asset:///body.png");
        bodySegments[1] = bodyEmoji;
        body.setContentSegments(bodySegments);
        TbPost *reply = new TbPost(&body);
        reply->setId("emoji-reply");
        TbUser *user = new TbUser(reply);
        user->setName("Reply author");
        reply->setAuthor(user);
        QVariantMap replyEmoji = bodyEmoji;
        replyEmoji.insert("url", "asset:///reply.png");
        replyEmoji.insert("emoticonName", "Reply emoji");
        reply->setContentSegments(QVariantList() << replyEmoji);
        body.setSubPosts(QVariantList() << QVariant::fromValue(reply));
        QVariantList rows;
        c.expandImageItems(QVariantList() << body.toVariantMap(), rows);
        check("body and reply emojis stay in their text and reply rows", rows.size() == 2
              && rows[0].toMap().value("rowType").toString() == "post"
              && rows[1].toMap().value("rowType").toString() == "replies");
        const QVariantMap row = rows.first().toMap();
        const QVariantList segments = row.value("displaySegments").toList();
        const QVariantMap preview = row.value("subPostPreview").toList().first().toMap();
        check("text and emoji retain their segment order", segments.size() == 3
              && segments[0].toMap().value("text").toString() == "Before"
              && segments[1].toMap().value("kind").toString() == "emoticon"
              && segments[2].toMap().value("text").toString() == "After");
        check("body emoji uses its own cached path", segments[1].toMap()
              .value("localImagePath").toString() == "asset:///body.png");
        check("reply emoji remains attached to the named reply", preview.value("authorName").toString() == "Reply author"
              && preview.value("displaySegments").toList().first().toMap()
                  .value("localImagePath").toString() == "asset:///reply.png");
        QVariantMap pendingRow = row;
        QVariantList pendingSegments = segments;
        QVariantMap pendingEmoji = pendingSegments[1].toMap();
        pendingEmoji.insert("localImagePath", "");
        pendingSegments[1] = pendingEmoji;
        pendingRow.insert("displaySegments", pendingSegments);
        QVariantMap pendingPreview = preview;
        QVariantMap pendingReplyEmoji = preview.value("displaySegments").toList().first().toMap();
        pendingReplyEmoji.insert("localImagePath", "");
        pendingPreview.insert("displaySegments", QVariantList() << pendingReplyEmoji);
        pendingRow.insert("subPostPreview", QVariantList() << pendingPreview);
        c.posts()->clear();
        QVariantMap withImages = body.toVariantMap();
        QVariantMap firstImage;
        firstImage.insert("url", "asset:///first.png");
        QVariantMap secondImage;
        secondImage.insert("url", "asset:///second.png");
        withImages.insert("images", QVariantList() << firstImage << secondImage);
        withImages.insert("subPostCount", 3);
        QVariantMap nextFloor;
        nextFloor.insert("id", "next-floor");
        QVariantList ordered;
        c.expandImageItems(QVariantList() << withImages << nextFloor, ordered);
        check("floor contains text then both images then reply preview", ordered.size() == 5
              && ordered[0].toMap().value("rowType").toString() == "post"
              && ordered[1].toMap().value("remoteImageUrl").toString() == "asset:///first.png"
              && ordered[2].toMap().value("remoteImageUrl").toString() == "asset:///second.png"
              && ordered[3].toMap().value("isReplyFooter").toBool()
              && ordered[4].toMap().value("id").toString() == "next-floor");
        check("reply footer retains its parent ID and count for navigation", ordered[3].toMap()
              .value("id").toString() == "emoji-floor" && ordered[3].toMap().value("subPostCount").toInt() == 3);
        check("floor divider follows replies instead of separating images", !ordered[0].toMap().value("floorEnd").toBool()
              && !ordered[1].toMap().value("floorEnd").toBool() && !ordered[2].toMap().value("floorEnd").toBool()
              && ordered[3].toMap().value("floorEnd").toBool());
        QVariantMap imageOnly = withImages;
        imageOnly.insert("subPostPreview", QVariantList());
        imageOnly.insert("subPostCount", 0);
        QVariantList noReplies;
        c.expandImageItems(QVariantList() << imageOnly, noReplies);
        check("an image floor without replies adds no empty footer", noReplies.size() == 3
              && noReplies.last().toMap().value("floorEnd").toBool());
        TbPost picture;
        QVariantMap pictureSegment;
        pictureSegment.insert("kind", "image");
        pictureSegment.insert("url", "asset:///first.png");
        picture.setContentSegments(QVariantList() << pictureSegment);
        QVariantList pictureRows;
        c.expandImageItems(QVariantList() << picture.toVariantMap(), pictureRows);
        check("rendered image does not retain a generated text placeholder", pictureRows.size() == 2
              && pictureRows[0].toMap().value("displayText").toString().isEmpty()
              && !pictureRows[0].toMap().value("plainText").toString().isEmpty());
        QVariantMap literalText;
        literalText.insert("kind", "text");
        literalText.insert("text", picture.plainText());
        picture.setContentSegments(QVariantList() << literalText << pictureSegment);
        QVariantList literalRows;
        c.expandImageItems(QVariantList() << picture.toVariantMap(), literalRows);
        check("author-written image placeholder text is preserved", literalRows[0].toMap()
              .value("displayText").toString() == literalText.value("text").toString());
        QVariantMap videoSegment;
        videoSegment.insert("kind", "video");
        picture.setContentSegments(QVariantList() << videoSegment);
        QVariantList videoRows;
        c.expandImageItems(QVariantList() << picture.toVariantMap(), videoRows);
        check("unrendered media retains its fallback text", !videoRows[0].toMap()
              .value("displayText").toString().isEmpty());
        QVariantMap pendingFooter = rows[1].toMap();
        pendingFooter.insert("subPostPreview", QVariantList() << pendingPreview);
        c.posts()->append(QVariantList() << pendingRow << pendingFooter);
        QMetaObject::invokeMethod(&c, "refreshImagePaths", Qt::DirectConnection);
        const QVariantMap refreshed = c.posts()->value(0).toMap();
        check("download refresh updates the body emoji in place", c.posts()->size() == 2
              && refreshed.value("displaySegments").toList()[1].toMap()
                  .value("localImagePath").toString() == "asset:///body.png");
        check("download refresh updates the reply emoji in place", refreshed.value("subPostPreview").toList()
              .first().toMap().value("displaySegments").toList().first().toMap()
                  .value("localImagePath").toString() == "asset:///reply.png");
        check("reply footer emoji refresh preserves its position and parent", c.posts()->value(1).toMap()
              .value("isReplyFooter").toBool() && c.posts()->value(1).toMap().value("id").toString() == "emoji-floor"
              && c.posts()->value(1).toMap().value("subPostPreview").toList().first().toMap()
                  .value("displaySegments").toList().first().toMap().value("localImagePath").toString() == "asset:///reply.png");
        c.posts()->clear();
    }
    {
        using namespace tieba;
        const TbPcThreadResult thread = TbPcParsers::parseThread(TbJson::parse(
            "{\"error_code\":0,\"user_list\":[{\"id\":\"11\",\"name_show\":\"Main\"},"
            "{\"id\":\"22\",\"name_show\":\"Preview nickname\",\"level_id\":9},"
            "{\"id\":\"33\",\"name\":\"Preview username\"}],"
            "\"first_floor\":{\"id\":\"101\",\"floor\":1,\"author_id\":\"11\","
            "\"content\":[{\"type\":0,\"text\":\"Short body\"}],"
            "\"sub_post_list\":{\"sub_post_list\":["
            "{\"id\":\"201\",\"author_id\":\"22\",\"content\":[{\"type\":0,\"text\":\"First\"}]},"
            "{\"id\":\"202\",\"author_id\":\"33\",\"content\":[{\"type\":0,\"text\":\"Second\"}]},"
            "{\"id\":\"203\",\"author_id\":\"44\",\"content\":[{\"type\":0,\"text\":\"Third\"}]}]}}}"));
        check("thread fixture contains its first floor", thread.posts.size() == 1);
        if (!thread.posts.isEmpty()) {
            TbPost *post = thread.posts.first();
            const QVariantList preview = post->subPostPreview();
            check("preview author IDs resolve through root user list", preview.size() == 3
                  && preview[0].toMap().value("authorName").toString() == "Preview nickname");
            check("preview falls back from nickname to username", preview.size() == 3
                  && preview[1].toMap().value("authorName").toString() == "Preview username");
            check("missing indexed author retains actual user ID", preview.size() == 3
                  && preview[2].toMap().value("authorId").toString() == "44"
                  && preview[2].toMap().value("authorName").toString() == "44");
            check("preview author details survive index cleanup", preview.size() == 3
                  && preview[0].toMap().value("authorLevel").toInt() == 9);
            check("preview text names every author before the colon", post->toVariantMap()
                  .value("subPostPreviewText").toString() ==
                  "Preview nickname: First\nPreview username: Second\n44: Third");
            const QList<TbPost *> children = post->children();
            check("preview author copy belongs to its own reply", !children.isEmpty()
                  && children.first()->author()->parent() == children.first());
            delete post->author();
            delete post;
        }
        delete thread.threadInfo;
        delete thread.pageInfo;
        delete thread.forumInfo;
        TbPost anonymous;
        TbPost *reply = new TbPost(&anonymous);
        reply->setPlainText("No author data");
        anonymous.setSubPosts(QVariantList() << QVariant::fromValue(reply));
        check("absent author never leaves a bare preview colon", !anonymous.toVariantMap()
              .value("subPostPreviewText").toString().startsWith(": "));
    }
    const tieba::TbForumListResult missing = tieba::TbParsers::parseForumLike(
        tieba::TbJson::parse("{\"error_code\":0}"));
    check("missing forum data remains an error", !missing.errorCode.isEmpty());
    check("missing forum error is readable UTF-8", missing.errorMessage.startsWith(
        QString::fromUtf8("\xE5\x85\xB3\xE6\xB3\xA8")));
    const tieba::TbForumListResult empty = tieba::TbParsers::parseForumLike(
        tieba::TbJson::parse("{\"error_code\":0,\"forum_list\":{\"non-gconforum\":[],\"gconforum\":[]}}"));
    check("an empty followed list remains a successful empty state",
          (empty.errorCode.isEmpty() || empty.errorCode == "0") && empty.forums.isEmpty());

    check("API uses the verified encrypted endpoint", tieba::TbSigner::apiBase() == "https://c.tieba.baidu.com/");
    const QVariantMap accountView = c.currentAccountUi();
    check("logged-out account UI has typed empty fields", accountView.contains("localAvatarPath")
          && accountView.contains("id") && accountView.value("id").toString().isEmpty());
    {
        const QString fake = QString(192, QLatin1Char('B'));
        const tieba::TbHttpRequest request = tieba::TbApiService::buildLoginRequest(fake, "fake-stoken");
        check("login uses the dedicated endpoint", request.url.endsWith("/c/s/login") && request.tag == "account.login");
        check("login encodes the bdusstoken suffix", request.body.contains("bdusstoken=" + fake.toLatin1() + "%7C"));
        check("login drops the common BDUSS parameter", !request.body.contains("BDUSS="));
        check("login uses the Android signature", request.body.contains("sign=" + tieba::TbSigner::androidSign(request.params).toLatin1()));
        check("login uses the reference client version", request.body.contains("_client_version=11.10.8.6"));
        check("login cookie contains no credentials", request.headers.last().second == "ka=open");

        TiebaController login;
        const QString isolated = testDir + "/login-" + QString::number(QDateTime::currentMSecsSinceEpoch());
        QDir().mkpath(isolated);
        const QString sessionFile = isolated + "/session.ini";
        const QString original = QString(192, QLatin1Char('A'));
        tieba::TbSession saved;
        saved.setSettingsFileForTest(sessionFile);
        saved.setCredentials(original);
        saved.setProfile("42", "original", "Original user", "");
        login.setCacheDirForTest(isolated);
        login.setSessionFileForTest(sessionFile);
        tieba::TbHttpClient client;
        tieba::TbApiService api(&client, &saved);
        QObject::connect(&api, SIGNAL(loginVerified(int,QVariantMap)), &login, SLOT(onLoginVerified(int,QVariantMap)));
        QObject::connect(&api, SIGNAL(requestFailed(int,QString,QString,QVariantMap)), &login, SLOT(onRequestFailed(int,QString,QString,QVariantMap)));
        login.login(fake);
        check("login starts the loading state", login.loading());
        check("pending login preserves the current account", login.currentAccount().value("uid").toString() == "42");
        saved.load();
        check("pending credentials are not saved", saved.bduss() == original && saved.accountCount() == 1);
        const bool failed = QMetaObject::invokeMethod(&login, "onRequestFailed", Qt::DirectConnection,
            Q_ARG(int, 1), Q_ARG(QString, QString("account.login")),
            Q_ARG(QString, QString("offline-test")), Q_ARG(QVariantMap, QVariantMap()));
        check("login failure reaches its handler", failed);
        check("login failure stops loading", !login.loading());
        check("login failure is shown on the login page", login.lastLoginError().contains("offline-test"));
        saved.load();
        check("failed login preserves saved accounts", saved.bduss() == original && saved.accountCount() == 1);

        login.login(fake);
        tieba::TbHttpResponse response;
        response.ok = true;
        response.httpStatus = 200;
        tieba::TbJson json = tieba::TbJson::parse("{\"error_code\":0}");
        QMetaObject::invokeMethod(&api, "onHttpFinished", Qt::DirectConnection,
            Q_ARG(int, 2), Q_ARG(QString, QString("account.login")),
            Q_ARG(tieba::TbJson, json), Q_ARG(tieba::TbHttpResponse, response));
        check("code zero without identity fails login", !login.loading() && !login.lastLoginError().isEmpty());
        saved.load();
        check("missing identity never replaces saved credentials", saved.bduss() == original && saved.accountCount() == 1);

        login.login(fake);
        json = tieba::TbJson::parse("{\"error_code\":4,\"user\":{\"id\":\"99\"}}");
        QMetaObject::invokeMethod(&api, "onHttpFinished", Qt::DirectConnection,
            Q_ARG(int, 3), Q_ARG(QString, QString("account.login")),
            Q_ARG(tieba::TbJson, json), Q_ARG(tieba::TbHttpResponse, response));
        check("error code wins over an included user", !login.loading() && login.lastLoginError().contains("4"));

        login.login(fake);
        json = tieba::TbJson::parse("{\"error_code\":0,\"user\":{\"id\":\"9007199254740993\",\"name\":\"new-user\",\"name_show\":\"New user\"},\"anti\":{\"tbs\":\"fake-tbs\"}}");
        QMetaObject::invokeMethod(&api, "onHttpFinished", Qt::DirectConnection,
            Q_ARG(int, 1), Q_ARG(QString, QString("account.login")),
            Q_ARG(tieba::TbJson, json), Q_ARG(tieba::TbHttpResponse, response));
        check("stale login cannot switch accounts", login.currentAccount().value("uid").toString() == "42" && login.loading());
        QMetaObject::invokeMethod(&api, "onHttpFinished", Qt::DirectConnection,
            Q_ARG(int, 4), Q_ARG(QString, QString("account.login")),
            Q_ARG(tieba::TbJson, json), Q_ARG(tieba::TbHttpResponse, response));
        check("verified identity closes the login page", login.openRequestKind() == "loginDone");
        check("verified identity preserves large user IDs", login.currentAccount().value("uid").toString() == "9007199254740993");
        check("verified profile appears immediately", login.currentAccount().value("nickName").toString() == "New user");
        saved.load();
        check("verified credentials are saved with the previous account retained", saved.bduss() == fake && saved.accountCount() == 2 && saved.tbs() == "fake-tbs");
        tieba::TbForumListResult old;
        tieba::TbForum *oldForum = new tieba::TbForum();
        oldForum->setId("old-account-forum");
        old.forums.append(oldForum);
        QMetaObject::invokeMethod(&login, "onForumListLoaded", Qt::DirectConnection,
            Q_ARG(int, 1), Q_ARG(tieba::TbForumListResult, old));
        check("old list response cannot fill the verified account", login.forumsForTestCount() == 0 && login.loading());
        QMetaObject::invokeMethod(&login, "onRequestFailed", Qt::DirectConnection,
            Q_ARG(int, 5), Q_ARG(QString, QString("forum.list.like")),
            Q_ARG(QString, QString("list-offline-test")), Q_ARG(QVariantMap, QVariantMap()));
        check("list failure stops loading after successful login", !login.loading());
        check("list failure does not turn login into failure", login.lastLoginError().isEmpty() && login.loggedIn());
        login.login(QString(192, QLatin1Char('C')));
        login.logout();
        QMetaObject::invokeMethod(&api, "onHttpFinished", Qt::DirectConnection,
            Q_ARG(int, 6), Q_ARG(QString, QString("account.login")),
            Q_ARG(tieba::TbJson, json), Q_ARG(tieba::TbHttpResponse, response));
        check("logout invalidates a pending login", !login.loggedIn() && !login.loading());
    }
    {
        using namespace tieba;
        const QByteArray followForm = TbSigner::buildPcForm(TbApiService::streamParams("follow", 2, "next-page", "12345"));
        check("follow pagination sends the response cursor", followForm.contains("page_tag=next-page") && followForm.contains("last_request_unix=12345") && followForm.contains("load_type=2"));
        check("notification refresh uses the reference pn zero", TbSigner::buildPcForm(TbApiService::streamParams("reply", 1)).contains("pn=0"));
        check("notification next page uses pn two", TbSigner::buildPcForm(TbApiService::streamParams("reply", 2)).contains("pn=2"));
        check("hot feed requests the all category", TbSigner::buildPcForm(TbApiService::streamParams("hot", 1)).contains("tab_code=all"));
        const TbJson feedJson = TbJson::parse("{\"error_code\":0,\"thread_list\":[{\"id\":\"9007199254740993\",\"title\":\"Real thread\",\"fname\":\"BlackBerry\",\"author\":{\"id\":\"77\",\"name_show\":\"Author\"},\"abstract\":[{\"type\":0,\"text\":\"Preview\"}],\"reply_num\":25}]}");
        const QVariantMap feedResult = TbApiService::parseStream("recommend", feedJson);
        const QVariantMap feedRow = feedResult.value("items").toList().first().toMap();
        check("recommendation preserves large thread IDs", feedRow.value("threadId").toString() == "9007199254740993");
        check("recommendation provides actual author and preview", feedRow.value("authorId").toString() == "77" && feedRow.value("abstractText").toString() == "Preview");
        check("missing author level has a typed zero", feedRow.value("authorLevel").toInt() == 0 && feedRow.contains("authorLevel"));
        check("missing view count is not invented", feedRow.value("viewCountText").toString().isEmpty());
        check("missing feed list is a structural error", TbApiService::parseStream("recommend", TbJson::parse("{\"error_code\":0}")).contains("error"));
        check("business errors do not become empty success", TbApiService::parseStream("reply", TbJson::parse("{\"error_code\":4,\"reply_list\":[]}")).contains("error"));
        check("legacy scalar notification emptiness is valid", !TbApiService::parseStream("reply", TbJson::parse("{\"error_code\":0,\"reply_list\":0}")).contains("error"));
        const QVariantMap notices = TbApiService::parseStream("reply", TbJson::parse("{\"error_code\":0,\"reply_list\":[{\"thread_id\":\"101\",\"post_id\":\"201\",\"content\":\"Reply\",\"quote_content\":\"<b>Quote</b>\",\"unread\":\"1\",\"replyer\":{\"id\":\"88\"}},{\"thread_id\":\"101\",\"post_id\":\"202\",\"content\":\"Another reply\"}],\"page\":{\"has_more\":\"1\"}}"));
        const QVariantList noticeRows = notices.value("items").toList();
        check("two replies to one thread keep distinct keys", noticeRows.size() == 2 && noticeRows[0].toMap().value("key") != noticeRows[1].toMap().value("key"));
        check("notification quote HTML is stripped", noticeRows[0].toMap().value("quoteText").toString() == "Quote");
        check("notification unread status is real data", noticeRows[0].toMap().value("unread").toBool());
        check("notification pagination uses page has_more", notices.value("hasMore").toBool());
        check("hot results retain server order", TbApiService::parseStream("hot", TbJson::parse("{\"error_code\":0,\"thread_info\":[{\"id\":\"10\"},{\"id\":\"2\"}],\"has_more\":0}")).value("items").toList()[0].toMap().value("id").toString() == "10");
        const QVariantMap followed = TbApiService::parseStream("follow", TbJson::parse("{\"error_code\":0,\"thread_info\":[{\"thread_list\":{\"id\":\"31\",\"title\":\"Followed\"}}],\"page_tag\":\"cursor\",\"req_unix\":12345,\"has_more\":1}"));
        check("follow parses wrapped threads and cursor", followed.value("items").toList().size() == 1 && followed.value("cursor").toString() == "cursor");
        check("list snapshots contain no raw response or credential", !feedResult.contains("raw") && !feedRow.contains("bduss"));

        TiebaController streams;
        const QString isolated = testDir + "/streams-" + QString::number(QDateTime::currentMSecsSinceEpoch());
        QDir().mkpath(isolated);
        const QString sessionFile = isolated + "/session.ini";
        TbSession fixture;
        fixture.setSettingsFileForTest(sessionFile);
        fixture.setCredentials(QString(192, QLatin1Char('D')));
        streams.setCacheDirForTest(isolated);
        streams.setSessionFileForTest(sessionFile);
        streams.loadStream("recommend");
        streams.loadStream("reply");
        check("stream loading is separate from page loading", streams.streamState("recommend").value("loading").toBool() && !streams.loading());
        QMetaObject::invokeMethod(&streams, "onStreamLoaded", Qt::DirectConnection,
            Q_ARG(int, 1), Q_ARG(QString, QString("recommend")), Q_ARG(QVariantMap, feedResult));
        check("recommendation response fills only its own model", streams.recommendedFeed()->size() == 1 && streams.replyNotifications()->size() == 0 && streams.threads()->size() == 0);
        check("profile can use the newly loaded feed posts", streams.loadedUiItems("thread", "", "77").size() == 1);
        check("another stream remains loading", streams.streamState("reply").value("loading").toBool());
        QMetaObject::invokeMethod(&streams, "onStreamLoaded", Qt::DirectConnection,
            Q_ARG(int, 2), Q_ARG(QString, QString("reply")), Q_ARG(QVariantMap, notices));
        check("same thread notifications are not deduplicated together", streams.replyNotifications()->size() == 2);
        check("a reply does not imply authorship of the original thread", streams.loadedUiItems("thread", "", "88").isEmpty());
        streams.loadStream("recommend", true);
        QMetaObject::invokeMethod(&streams, "onRequestFailed", Qt::DirectConnection,
            Q_ARG(int, 3), Q_ARG(QString, QString("stream.recommend")), Q_ARG(QString, QString("offline")), Q_ARG(QVariantMap, QVariantMap()));
        check("append failure preserves existing items and page", streams.recommendedFeed()->size() == 1 && streams.streamState("recommend").value("page").toInt() == 1);
        check("stream errors are independent", streams.streamState("recommend").value("error").toString() == "offline" && streams.streamState("reply").value("error").toString().isEmpty());
        check("load-more retry retains its request type", streams.streamState("recommend").value("retryAppend").toBool());
        streams.loadStream("recommend", true);
        QMetaObject::invokeMethod(&streams, "onStreamLoaded", Qt::DirectConnection,
            Q_ARG(int, 4), Q_ARG(QString, QString("recommend")), Q_ARG(QVariantMap, feedResult));
        check("overlapping pages do not duplicate threads", streams.recommendedFeed()->size() == 1);
        check("a repeated page stops infinite load-more", !streams.streamState("recommend").value("hasMore").toBool());
        streams.loadStream("recommend");
        QMetaObject::invokeMethod(&streams, "onRequestFailed", Qt::DirectConnection,
            Q_ARG(int, 5), Q_ARG(QString, QString("stream.recommend")), Q_ARG(QString, QString("refresh-offline")), Q_ARG(QVariantMap, QVariantMap()));
        check("refresh retry does not accidentally request a later page", !streams.streamState("recommend").value("retryAppend").toBool() && streams.recommendedFeed()->size() == 1);
        streams.loadStream("hot");
        streams.logout();
        QMetaObject::invokeMethod(&streams, "onStreamLoaded", Qt::DirectConnection,
            Q_ARG(int, 6), Q_ARG(QString, QString("hot")), Q_ARG(QVariantMap, feedResult));
        check("account reset clears all five streams", streams.recommendedFeed()->size() == 0 && streams.replyNotifications()->size() == 0 && streams.hotFeed()->size() == 0);
        check("old account stream responses are discarded", !streams.streamState("hot").value("loaded").toBool() && !streams.streamState("hot").value("loading").toBool());
        streams.loadStream("reply");
        check("logged-out notifications require login", !streams.streamState("reply").value("error").toString().isEmpty() && streams.replyNotifications()->size() == 0);
    }
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
