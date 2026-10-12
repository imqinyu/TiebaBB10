#ifndef TB_USER_DATA_CACHE_HPP
#define TB_USER_DATA_CACHE_HPP
#include <QObject>
#include <QVariantMap>
#include <QString>
namespace tieba {
// Successful, sanitized UI snapshots only. No session tokens or runtime file URLs.
class TbUserDataCache : public QObject {
    Q_OBJECT
    Q_PROPERTY(int version READ version NOTIFY changed)
public:
    explicit TbUserDataCache(QObject *parent = 0, const QString &directory = QString());
    int version() const { return m_version; }
    QString directory() const { return m_directory; }
    void setDirectory(const QString &directory);
    QVariantMap read(const QString &accountId, const QString &scope, qint64 nowSeconds = 0,
                     qint64 maxAgeSeconds = 604800) const;
    bool save(const QString &accountId, const QString &scope, const QVariantMap &payload, qint64 nowSeconds = 0);
    bool remove(const QString &accountId, const QString &scope);
    bool clearAccount(const QString &accountId);
    static QVariantMap sanitize(const QVariantMap &payload);
    static bool equivalent(const QVariantMap &first, const QVariantMap &second);
signals:
    void changed();
    void storageError(const QString &message);
private:
    QString pathFor(const QString &accountId, const QString &scope) const;
    QString m_directory;
    int m_version;
};
}
#endif
