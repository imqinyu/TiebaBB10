#include "tieba/model/TbThreadLibrary.hpp"
#include <QCoreApplication>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTemporaryFile>
#include <stdio.h>

static int checks = 0, failures = 0;
static void check(const char *name, bool value)
{
    ++checks;
    if (!value) ++failures;
    printf("%s %s\n", value ? "OK" : "FAIL", name);
}
static QVariantMap row(const QString &id, const QDate &date = QDate::currentDate())
{
    QVariantMap result;
    result.insert("id", id);
    result.insert("title", QString::fromUtf8("测试帖子"));
    result.insert("authorName", "Author");
    result.insert("forumName", "BlackBerry");
    result.insert("date", date.toString(Qt::ISODate));
    result.insert("visitedAt", QDateTime(date).toMSecsSinceEpoch());
    result.insert("BDUSS", "must-not-save");
    result.insert("body", "must-not-save");
    return result;
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QDir().mkpath("tests/tmp");
    QString path;
    {
        QTemporaryFile location(QDir::currentPath() + "/tests/tmp/bbtieba-library-test-XXXXXX.dat");
        if (!location.open()) { fprintf(stderr, "Temporary fixture: %s\n", qPrintable(location.errorString())); return 2; }
        path = location.fileName();
    }
    QFile::remove(path);
    {
        tieba::TbThreadLibrary library(0, path);
        check("empty initial history", library.items("history").isEmpty());
        library.recordVisit(row("90071992547409931234"));
        library.recordVisit(row("2"));
        library.recordVisit(row("90071992547409931234"));
        const QVariantList history = library.items("history");
        check("repeat visit deduplicates", history.size() == 2);
        check("large IDs preserved", history.first().toMap().value("id").toString() == "90071992547409931234");
        check("dates grouped naturally", history.first().toMap().value("date").toString() == QDate::currentDate().toString(Qt::ISODate));
        check("whitelist excludes credentials and body", !history.first().toMap().contains("BDUSS") && !history.first().toMap().contains("body"));
        library.toggleFavorite(row("90071992547409931234"));
        check("favorite true", library.isFavorite("90071992547409931234"));
        check("favorite independently listed", library.items("favorites").size() == 1);
        const int version = library.version();
        library.recordVisit(QVariantMap());
        check("empty ID ignored", library.version() == version);
        check("store file exists", QFile::exists(path));
    }
    {
        tieba::TbThreadLibrary library(0, path);
        check("history restored on restart", library.items("history").size() == 2);
        check("favorite restored on restart", library.isFavorite("90071992547409931234"));
        library.toggleFavorite(row("90071992547409931234"));
        check("toggle removes favorite", !library.isFavorite("90071992547409931234"));
    }
    {
        tieba::TbThreadLibrary library(0, path);
        check("unfavorite persists", library.items("favorites").isEmpty());
    }
    // Seed dates on both sides of the exact two-calendar-month cutoff.
    const QDate cutoff = QDate::currentDate().addMonths(-2);
    QVariantList history, favorites;
    history << row("old", cutoff.addDays(-1)) << row("boundary", cutoff) << row("today");
    favorites << row("old-favorite", cutoff.addYears(-1));
    {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) { fprintf(stderr, "Seed fixture: %s\n", qPrintable(file.errorString())); return 2; }
        QDataStream stream(&file);
        stream.setVersion(QDataStream::Qt_4_8);
        stream << quint32(0x54424c31) << history << favorites;
    }
    {
        tieba::TbThreadLibrary library(0, path);
        const QVariantList kept = library.items("history");
        check("older than two calendar months removed", kept.size() == 2);
        check("boundary day retained", kept.last().toMap().value("id").toString() == "boundary");
        check("newest first", kept.first().toMap().value("id").toString() == "today");
        check("old favorites never expire", library.isFavorite("old-favorite"));
    }
    QFile::copy(path, path + ".bak");
    {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) { fprintf(stderr, "Corrupt fixture: %s\n", qPrintable(file.errorString())); return 2; }
        file.write("corrupt");
    }
    {
        tieba::TbThreadLibrary library(0, path);
        check("backup recovers interrupted save", library.items("history").size() == 2 && library.isFavorite("old-favorite"));
    }
    QFile::remove(path);
    QFile::remove(path + ".bak");
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
