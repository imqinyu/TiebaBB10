#ifndef TB_THREAD_LIBRARY_HPP
#define TB_THREAD_LIBRARY_HPP

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

namespace tieba {

// Local metadata only. Thread IDs remain strings; no content or account secrets are saved.
class TbThreadLibrary : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int version READ version NOTIFY changed)
public:
    explicit TbThreadLibrary(QObject *parent = 0, const QString &storagePath = QString());
    int version() const { return m_version; }
    Q_INVOKABLE void recordVisit(const QVariantMap &thread);
    Q_INVOKABLE void toggleFavorite(const QVariantMap &thread);
    Q_INVOKABLE bool isFavorite(const QString &id) const;
    Q_INVOKABLE QVariantList items(const QString &kind);
signals:
    void changed();
    void storageError(const QString &message);
private:
    QVariantMap metadata(const QVariantMap &thread) const;
    bool pruneHistory();
    bool save();
    void load();
    void notifyChange();
    QString m_path;
    QVariantList m_history;
    QVariantList m_favorites;
    int m_version;
};

}
#endif
