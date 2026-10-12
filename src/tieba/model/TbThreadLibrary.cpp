#include "TbThreadLibrary.hpp"

#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryFile>
#include <QtAlgorithms>

namespace tieba {
namespace {
const quint32 kMagic = 0x54424c31;
bool newestFirst(const QVariant &a, const QVariant &b)
{
    return a.toMap().value("visitedAt").toLongLong() > b.toMap().value("visitedAt").toLongLong();
}
bool readFile(const QString &path, QVariantList *history, QVariantList *favorites)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 16 * 1024 * 1024) return false;
    QDataStream stream(&file);
    stream.setVersion(QDataStream::Qt_4_8);
    quint32 magic = 0;
    stream >> magic;
    if (magic != kMagic) return false;
    stream >> *history >> *favorites;
    return stream.status() == QDataStream::Ok;
}
}

TbThreadLibrary::TbThreadLibrary(QObject *parent, const QString &storagePath)
    : QObject(parent), m_path(storagePath.isEmpty()
        ? QDir::homePath() + "/bbtieba/config/thread-library.dat" : storagePath), m_version(0)
{
    load();
    if (pruneHistory()) save();
}

QVariantMap TbThreadLibrary::metadata(const QVariantMap &thread) const
{
    QVariantMap result;
    QString id = thread.value("id").toString();
    if (id.isEmpty()) id = thread.value("threadId").toString();
    result.insert("id", id);
    result.insert("threadId", id);
    result.insert("title", thread.value("title").toString());
    result.insert("authorName", thread.value("authorName").toString());
    result.insert("forumName", thread.value("forumName").toString());
    return result;
}

void TbThreadLibrary::recordVisit(const QVariantMap &thread)
{
    QVariantMap row = metadata(thread);
    const QString id = row.value("id").toString();
    if (id.isEmpty()) return;
    for (int i = m_history.size() - 1; i >= 0; --i) {
        if (m_history.at(i).toMap().value("id").toString() == id) m_history.removeAt(i);
    }
    const QDateTime now = QDateTime::currentDateTime();
    row.insert("visitedAt", now.toMSecsSinceEpoch());
    row.insert("date", now.date().toString(Qt::ISODate));
    m_history.prepend(row);
    pruneHistory();
    save();
    notifyChange();
}

void TbThreadLibrary::toggleFavorite(const QVariantMap &thread)
{
    const QVariantMap row = metadata(thread);
    const QString id = row.value("id").toString();
    if (id.isEmpty()) return;
    for (int i = 0; i < m_favorites.size(); ++i) {
        if (m_favorites.at(i).toMap().value("id").toString() == id) {
            m_favorites.removeAt(i);
            save();
            notifyChange();
            return;
        }
    }
    QVariantMap saved = row;
    saved.insert("visitedAt", QDateTime::currentMSecsSinceEpoch());
    saved.insert("date", QDate::currentDate().toString(Qt::ISODate));
    m_favorites.prepend(saved);
    save();
    notifyChange();
}

bool TbThreadLibrary::isFavorite(const QString &id) const
{
    for (int i = 0; i < m_favorites.size(); ++i)
        if (m_favorites.at(i).toMap().value("id").toString() == id) return true;
    return false;
}

QVariantList TbThreadLibrary::items(const QString &kind)
{
    if (pruneHistory()) { save(); notifyChange(); }
    QVariantList rows = kind == "favorites" ? m_favorites : m_history;
    qStableSort(rows.begin(), rows.end(), newestFirst);
    return rows;
}

bool TbThreadLibrary::pruneHistory()
{
    const QDate cutoff = QDate::currentDate().addMonths(-2);
    bool changed = false;
    for (int i = m_history.size() - 1; i >= 0; --i) {
        const QDate date = QDate::fromString(m_history.at(i).toMap().value("date").toString(), Qt::ISODate);
        if (!date.isValid() || date < cutoff) { m_history.removeAt(i); changed = true; }
    }
    return changed;
}

void TbThreadLibrary::load()
{
    QVariantList history, favorites;
    if (!readFile(m_path, &history, &favorites) && !readFile(m_path + ".bak", &history, &favorites)) return;
    // Whitelist fields on read as well as write, and ignore duplicate / malformed IDs.
    const QVariantList lists[] = { history, favorites };
    QVariantList *outputs[] = { &m_history, &m_favorites };
    for (int list = 0; list < 2; ++list) {
        QStringList seen;
        for (int i = 0; i < lists[list].size(); ++i) {
            const QVariantMap source = lists[list].at(i).toMap();
            QVariantMap row = metadata(source);
            const QString id = row.value("id").toString();
            if (id.isEmpty() || seen.contains(id)) continue;
            seen.append(id);
            row.insert("visitedAt", source.value("visitedAt").toLongLong());
            row.insert("date", source.value("date").toString());
            outputs[list]->append(row);
        }
    }
}

bool TbThreadLibrary::save()
{
    const QString dir = QFileInfo(m_path).absolutePath();
    if (!QDir().mkpath(dir)) { emit storageError(tr("无法创建帖子记录目录")); return false; }
    QTemporaryFile temporary(dir + "/thread-library-XXXXXX.tmp");
    if (!temporary.open()) { emit storageError(tr("无法保存帖子记录")); return false; }
    QDataStream stream(&temporary);
    stream.setVersion(QDataStream::Qt_4_8);
    stream << kMagic << m_history << m_favorites;
    if (stream.status() != QDataStream::Ok || !temporary.flush()) return false;
    temporary.close();
    const QString backup = m_path + ".bak";
    if (QFile::exists(m_path)) {
        if (QFile::exists(backup) && !QFile::remove(backup)) return false;
        if (!QFile::rename(m_path, backup)) return false;
    }
    if (!temporary.rename(m_path)) {
        if (QFile::exists(backup)) QFile::rename(backup, m_path);
        emit storageError(tr("无法保存帖子记录"));
        return false;
    }
    temporary.setAutoRemove(false);
    QFile::remove(backup);
    return true;
}

void TbThreadLibrary::notifyChange()
{
    ++m_version;
    emit changed();
}
}
