#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QVariantList>
#include "model/TbUserDataCache.hpp"
#include "util/TbMd5.hpp"
#include <cstdio>
using namespace tieba;
static int checks = 0, failures = 0;
static void check(const char *name, bool value) {
    ++checks; if (!value) ++failures;
    std::printf("%s %s\n", value ? "OK" : "FAIL", name);
}
static QByteArray utf16(const QString &text) {
    QByteArray bytes;
    for (int i = 0; i < text.size(); ++i) { bytes.append((char)(text.at(i).unicode() >> 8)); bytes.append((char)(text.at(i).unicode() & 255)); }
    return bytes;
}
static QVariantMap payload(const QString &title) {
    QVariantMap row; row.insert("id", "90071992547409931234"); row.insert("title", title);
    row.insert("resultKind", "thread");
    row.insert("authorName", "author"); row.insert("date", "2026-10-11"); row.insert("hasAgreeCount", true); row.insert("agreeCount", 0);
    row.insert("localAvatarPath", "file:///old-install/tmp/avatar.jpg");
    row.insert("avatarUrl", "https://himg.bdimg.com/sys/portrait/item/fake?BDUSS=fake-query-secret&t=1");
    row.insert("BDUSS", "fake-row-secret");
    QVariantList rows; rows.append(row);
    QVariantMap profile; profile.insert("id", "42"); profile.insert("fansCount", 0);
    profile.insert("stoken", "fake-profile-secret");
    QVariantMap result; result.insert("items", rows); result.insert("profile", profile);
    result.insert("hasMore", true); result.insert("page", 2); result.insert("cursor", "public-cursor");
    result.insert("BDUSS", "fake-top-secret"); result.insert("tbs", "fake-ticket-secret");
    result.insert("loading", true); result.insert("error", "old failure");
    return result;
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QString dir = QDir::currentPath() + "/tests/tmp/userdata-cache-" + QString::number(QDateTime::currentMSecsSinceEpoch());
    TbUserDataCache cache(0, dir);
    check("constructor does not create files", !QDir(dir).exists());
    check("missing cache is a miss", !cache.read("account-A", "stream.reply", 1000).value("hit").toBool());
    check("empty account cannot persist", !cache.save("", "stream.reply", payload("Title"), 1000));
    check("first save succeeds", cache.save("account-A", "stream.reply", payload("Title"), 1000));
    check("content change increments version", cache.version() == 1);
    QVariantMap read = cache.read("account-A", "stream.reply", 1001);
    check("fresh snapshot hits", read.value("hit").toBool() && read.value("ageSeconds").toLongLong() == 1);
    const QVariantMap saved = read.value("payload").toMap();
    const QVariantMap savedRow = saved.value("items").toList().first().toMap();
    check("preserves string IDs and dates", savedRow.value("id") == "90071992547409931234" && savedRow.value("date") == "2026-10-11");
    check("preserves Cascades list component role", savedRow.value("resultKind") == "thread");
    check("known zero statistics are preserved", saved.value("profile").toMap().contains("fansCount") && savedRow.value("hasAgreeCount").toBool());
    check("unknown statistics are not invented", !saved.value("profile").toMap().contains("followingCount"));
    check("runtime state is excluded", !saved.contains("loading") && !saved.contains("error"));
    check("temporary install paths are excluded", !savedRow.contains("localAvatarPath"));
    check("secret keys excluded at every depth", !saved.contains("BDUSS") && !savedRow.contains("BDUSS") && !saved.value("profile").toMap().contains("stoken"));
    check("URL credential query is stripped", !savedRow.value("avatarUrl").toString().contains("fake-query-secret"));
    check("unchanged successful refresh updates time", cache.save("account-A", "stream.reply", payload("Title"), 2000));
    check("unchanged content does not notify version", cache.version() == 1);
    check("freshened save timestamp is persisted", cache.read("account-A", "stream.reply", 2001).value("savedAt").toLongLong() == 2000);
    check("different title persists", cache.save("account-A", "stream.reply", payload("Changed"), 2001) && cache.version() == 2);
    TbUserDataCache reopened(0, dir);
    check("new cache instance reads persistent data", reopened.read("account-A", "stream.reply", 2002).value("payload").toMap().value("items").toList().first().toMap().value("title") == "Changed");
    check("account isolation", !reopened.read("account-B", "stream.reply", 2002).value("hit").toBool());
    check("kind isolation", !reopened.read("account-A", "stream.mention", 2002).value("hit").toBool());
    check("user page target isolation", !reopened.read("account-A", "userpage.43.profile", 2002).value("hit").toBool());
    check("expiration boundary is inclusive", reopened.read("account-A", "stream.reply", 2001 + 604800).value("hit").toBool());
    check("expired snapshot is ignored", !reopened.read("account-A", "stream.reply", 2001 + 604801).value("hit").toBool());
    check("small clock adjustment is clamped", reopened.read("account-A", "stream.reply", 1800).value("ageSeconds").toLongLong() == 0);
    check("large clock rollback is rejected", !reopened.read("account-A", "stream.reply", 1500).value("hit").toBool());
    const QString file = dir + "/" + TbMd5::hex(QByteArray("account-A")) + "-" + TbMd5::hex(QByteArray("stream.reply")) + ".dat";
    QFile original(file); check("saved file opens", original.open(QIODevice::ReadOnly));
    const QByteArray contents = original.readAll(); original.close();
    check("file contains no top or nested credential values", !contents.contains(utf16("fake-top-secret")) && !contents.contains(utf16("fake-profile-secret"))
          && !contents.contains(utf16("fake-row-secret")) && !contents.contains(utf16("fake-ticket-secret")) && !contents.contains(utf16("fake-query-secret")));
    check("file headers do not leak raw account names", !contents.contains(utf16("account-A")));
    check("atomic backup fixture created", QFile::copy(file, file + ".bak"));
    QFile truncated(file); check("corrupted primary fixture opens", truncated.open(QIODevice::WriteOnly | QIODevice::Truncate));
    truncated.write("truncated"); truncated.close();
    check("interrupted write can recover previous backup", reopened.read("account-A", "stream.reply", 2002).value("hit").toBool());
    check("successful replacement repairs corrupted primary", reopened.save("account-A", "stream.reply", payload("Recovered"), 2003));
    check("completed atomic replacement cleans backup", !QFile::exists(file + ".bak"));
    check("repaired primary is independently readable", TbUserDataCache(0, dir).read("account-A", "stream.reply", 2004).value("hit").toBool());
    QVariantMap empty; empty.insert("items", QVariantList()); empty.insert("hasMore", false);
    check("valid empty list is cacheable", cache.save("account-A", "stream.mention", empty, 2003));
    check("empty cached list remains a hit", reopened.read("account-A", "stream.mention", 2004).value("hit").toBool()
          && reopened.read("account-A", "stream.mention", 2004).value("payload").toMap().value("items").toList().isEmpty());
    check("second account snapshot saved", cache.save("account-B", "stream.reply", payload("Other account"), 2003));
    check("path-like scope is hashed", cache.save("../outside", "../../secret", payload("Safe path"), 2003));
    const QStringList names = QDir(dir).entryList(QDir::Files);
    bool safeNames = true;
    for (int i = 0; i < names.size(); ++i) if (names.at(i).contains("account") || names.at(i).contains("secret") || names.at(i).contains("..")) safeNames = false;
    check("all cache files stay inside supplied directory", safeNames);
    check("removing one scope succeeds", cache.remove("account-A", "stream.mention"));
    check("removal does not clear neighboring account", reopened.read("account-B", "stream.reply", 2004).value("hit").toBool());
    check("clearing account succeeds", cache.clearAccount("account-A"));
    check("account cleared without clearing other account", !reopened.read("account-A", "stream.reply", 2004).value("hit").toBool()
          && reopened.read("account-B", "stream.reply", 2004).value("hit").toBool());
    std::printf("User data cache: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
