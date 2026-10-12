#ifndef TB_FAVORITE_SERVICE_HPP
#define TB_FAVORITE_SERVICE_HPP
#include <QObject>
#include <QVariantMap>
#include <QVariantList>
#include <QMap>
#include <QPointer>
#include "net/TbHttpClient.hpp"
namespace tieba {
class TbSession;
class TbUserDataCache;
class TbFavoriteService : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap state READ state NOTIFY changed)
    Q_PROPERTY(QVariantList rows READ rows NOTIFY changed)
    Q_PROPERTY(int version READ version NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
public:
    TbFavoriteService(TbHttpClient *client, TbSession *session, QObject *parent = 0);
    QVariantMap state() const;
    QVariantList rows() const { return m_rows; }
    int version() const { return m_version; }
    void setCacheDirectoryForTest(const QString &directory);
    bool loading() const { return m_loading; }
    bool busy() const { return m_loading || !m_toggleRow.isEmpty() || m_writing; }
    Q_INVOKABLE int status(const QString &threadId) const;
    static TbHttpRequest listRequest(const TbSession *session, int offset, bool mobile = false);
    static TbHttpRequest mutationRequest(const TbSession *session, const QString &threadId,
                const QString &postId, bool stored, const QString &tbs, bool mobile = false);
    static QVariantMap parseList(const TbJson &json);
public slots:
    void ensureLoaded();
    void ensureStatus(const QString &threadId);
    void refresh();
    void loadMore();
    void retry();
    void toggle(const QVariantMap &row);
    void remove(const QString &threadId);
    void clear();
signals:
    void changed();
    void operationFinished(const QString &threadId, bool stored, bool success, const QString &message);
private slots:
    void onFinished(int requestId, const QString &tag, const tieba::TbJson &json, const tieba::TbHttpResponse &response);
    void onSessionChanged();
private:
    static TbHttpRequest request(const TbSession *session, const QString &path,
                const QList<KeyValue> &params, const QString &tag, bool mobile);
    static bool validId(const QString &id);
    bool signedIn() const;
    void notify();
    void fetch(int offset);
    void resolveStatus();
    void write(const QString &threadId, const QString &postId, bool stored, const QString &tbs = QString());
    void operationError(const QString &threadId, bool stored, const QString &message);
    void restoreCache();
    void saveCache();
    QPointer<TbHttpClient> m_client;
    QPointer<TbSession> m_session;
    TbUserDataCache *m_cache;
    QVariantList m_rows;
    QMap<int, QVariantMap> m_requests;
    QMap<QString, bool> m_known;
    QVariantMap m_toggleRow;
    QString m_statusTarget, m_error, m_mutationError, m_accountKey;
    bool m_loading, m_loaded, m_hasMore, m_complete, m_writing, m_mobile;
    bool m_cacheHydrated, m_cacheShown, m_backgroundStarted;
    int m_version, m_offset, m_failedOffset, m_scanOffset;
};
}
#endif
