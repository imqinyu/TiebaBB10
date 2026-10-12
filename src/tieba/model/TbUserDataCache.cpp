#include "TbUserDataCache.hpp"
#include "util/TbMd5.hpp"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDataStream>
#include <QTemporaryFile>
#include <QSet>
#include <QUrl>
namespace tieba {
namespace {
const quint32 kMagic = 0x55444331;
const qint64 kMaxBytes = 8 * 1024 * 1024;
QString digest(const QString &value) { return TbMd5::hex(value.toUtf8()); }
QSet<QString> fields() {
    const QString names("items profile hasMore page offset cursor requestUnix private message complete firstPageCount rowsOffsetMetadata "
        "id key resultKind threadId postId quotePostId isFloor notification title name userName authorName authorId "
        "authorLevel authorLabel author authorAvatarUrl remoteAvatarUrl avatarUrl remoteImageUrl "
        "timeText lastTimeText abstractText contentText quoteText replyCountText viewCountText hotText "
        "replyCount agreeCount agreeCountText hasAgreeCount isGood isTop isVideo isFollowedForum unread "
        "forumName forumId level levelText isSigned slogan memberCountText postCountText intro displayName "
        "followingCount fansCount privacy like group post live deleted firstImageUrl images url originUrl "
        "width height type portrait isSelf uid loaded errorCode savedAt date visitedAt markPid "
        "thread_id post_id forum_name create_time last_time contentSegments kind text emoticonName media "
        "postHead isImageItem isEmoticonItem floor floorEnd subPostCount subPostPreview subPostPreviewText "
        "isOriginalPoster minPid maxPid count totalFollowNum tipsText hasConcerned");
    const QStringList list = names.split(QLatin1Char(' '), QString::SkipEmptyParts);
    QSet<QString> out;
    for (int i = 0; i < list.size(); ++i) out.insert(list.at(i));
    return out;
}
QVariant scrub(const QVariant &value, const QSet<QString> &allowed, const QString &key, int depth) {
    if (depth > 12) return QVariant();
    if (value.type() == QVariant::Map) {
        const QVariantMap input = value.toMap(); QVariantMap output;
        QVariantMap::const_iterator it = input.constBegin();
        for (; it != input.constEnd(); ++it) {
            if (!allowed.contains(it.key())) continue;
            const QVariant cleaned = scrub(it.value(), allowed, it.key(), depth + 1);
            if (cleaned.isValid()) output.insert(it.key(), cleaned);
        }
        return output;
    }
    if (value.type() == QVariant::List) {
        const QVariantList input = value.toList(); QVariantList output;
        for (int i = 0; i < input.size() && i < 1000; ++i) {
            const QVariant cleaned = scrub(input.at(i), allowed, key, depth + 1);
            if (cleaned.isValid()) output.append(cleaned);
        }
        return output;
    }
    if (value.type() == QVariant::String) {
        QString text = value.toString();
        if (text.size() > 131072) text = text.left(131072);
        if (key.endsWith("Url") || key == "url" || key == "originUrl") {
            QUrl url(text);
            if (!url.scheme().isEmpty() && url.scheme() != "http" && url.scheme() != "https" && url.scheme() != "asset") return QVariant();
            const QList<QPair<QString, QString> > params = url.queryItems();
            for (int i = 0; i < params.size(); ++i) {
                const QString name = params.at(i).first.toLower();
                if (name == "bduss" || name == "stoken" || name == "bdusstoken" || name == "tbs" || name == "token") url.removeQueryItem(params.at(i).first);
            }
            url.setUserName(QString()); url.setPassword(QString());
            text = url.toString();
        }
        return text;
    }
    if (value.type() == QVariant::Bool || value.type() == QVariant::Int || value.type() == QVariant::UInt
        || value.type() == QVariant::LongLong || value.type() == QVariant::ULongLong || value.type() == QVariant::Double) return value;
    return QVariant();
}
bool readFile(const QString &path, const QString &account, const QString &scope, QVariantMap *payload, qint64 *savedAt) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > kMaxBytes) return false;
    QDataStream stream(&file); stream.setVersion(QDataStream::Qt_4_8);
    quint32 magic = 0; QString actualAccount, actualScope;
    stream >> magic;
    if (magic != kMagic) return false;
    stream >> actualAccount >> actualScope >> *savedAt >> *payload;
    return stream.status() == QDataStream::Ok && actualAccount == account && actualScope == scope && *savedAt > 0;
}
}
TbUserDataCache::TbUserDataCache(QObject *parent, const QString &directory)
    : QObject(parent), m_directory(directory.isEmpty() ? QDir::homePath() + "/bbtieba/data/user-cache" : directory), m_version(0) {}
void TbUserDataCache::setDirectory(const QString &directory) { if (!directory.isEmpty()) m_directory = directory; }
QString TbUserDataCache::pathFor(const QString &accountId, const QString &scope) const {
    return m_directory + "/" + digest(accountId) + "-" + digest(scope) + ".dat";
}
QVariantMap TbUserDataCache::sanitize(const QVariantMap &payload) { return scrub(payload, fields(), QString(), 0).toMap(); }
bool TbUserDataCache::equivalent(const QVariantMap &first, const QVariantMap &second) { return sanitize(first) == sanitize(second); }
QVariantMap TbUserDataCache::read(const QString &accountId, const QString &scope, qint64 nowSeconds, qint64 maxAgeSeconds) const {
    QVariantMap result; result.insert("hit", false);
    if (accountId.isEmpty() || scope.isEmpty()) return result;
    const QString path = pathFor(accountId, scope); QVariantMap payload; qint64 timestamp = 0;
    if (!readFile(path, digest(accountId), digest(scope), &payload, &timestamp)
        && !readFile(path + ".bak", digest(accountId), digest(scope), &payload, &timestamp)) return result;
    const qint64 now = nowSeconds > 0 ? nowSeconds : (qint64)QDateTime::currentDateTime().toTime_t();
    const qint64 age = now - timestamp;
    // Clock rollback beyond five minutes is invalid, rather than an immortal cache.
    if (age < -300 || (maxAgeSeconds >= 0 && age > maxAgeSeconds)) return result;
    result.insert("hit", true); result.insert("payload", sanitize(payload));
    result.insert("savedAt", timestamp); result.insert("ageSeconds", qMax((qint64)0, age));
    return result;
}
bool TbUserDataCache::save(const QString &accountId, const QString &scope, const QVariantMap &source, qint64 nowSeconds) {
    if (accountId.isEmpty() || scope.isEmpty()) return false;
    const QVariantMap payload = sanitize(source);
    if (payload.isEmpty()) return false;
    const QVariantMap previous = read(accountId, scope, nowSeconds, -1);
    const bool different = !previous.value("hit").toBool() || previous.value("payload").toMap() != payload;
    if (!QDir().mkpath(m_directory)) { emit storageError(tr("无法创建用户缓存目录")); return false; }
    QTemporaryFile temporary(m_directory + "/user-cache-XXXXXX.tmp");
    if (!temporary.open()) return false;
    QDataStream stream(&temporary); stream.setVersion(QDataStream::Qt_4_8);
    const qint64 timestamp = nowSeconds > 0 ? nowSeconds : (qint64)QDateTime::currentDateTime().toTime_t();
    stream << kMagic << digest(accountId) << digest(scope) << timestamp << payload;
    if (stream.status() != QDataStream::Ok || temporary.size() > kMaxBytes || !temporary.flush()) return false;
    temporary.close();
    const QString path = pathFor(accountId, scope), backup = path + ".bak";
    if (QFile::exists(path)) {
        if (QFile::exists(backup) && !QFile::remove(backup)) return false;
        if (!QFile::rename(path, backup)) return false;
    }
    if (!temporary.rename(path)) { if (QFile::exists(backup)) QFile::rename(backup, path); return false; }
    temporary.setAutoRemove(false); QFile::remove(backup);
    if (different) { ++m_version; emit changed(); }
    return true;
}
bool TbUserDataCache::remove(const QString &accountId, const QString &scope) {
    if (accountId.isEmpty() || scope.isEmpty()) return false;
    const QString path = pathFor(accountId, scope);
    bool ok = true, removed = false;
    if (QFile::exists(path)) { removed = true; ok = QFile::remove(path); }
    if (QFile::exists(path + ".bak")) { removed = true; ok = QFile::remove(path + ".bak") && ok; }
    if (removed && ok) { ++m_version; emit changed(); }
    return ok;
}
bool TbUserDataCache::clearAccount(const QString &accountId) {
    if (accountId.isEmpty()) return false;
    const QStringList files = QDir(m_directory).entryList(QStringList() << (digest(accountId) + "-*.dat") << (digest(accountId) + "-*.dat.bak"), QDir::Files);
    bool ok = true;
    for (int i = 0; i < files.size(); ++i) ok = QFile::remove(m_directory + "/" + files.at(i)) && ok;
    if (!files.isEmpty() && ok) { ++m_version; emit changed(); }
    return ok;
}
}
