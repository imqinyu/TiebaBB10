#ifndef TB_SEARCH_SERVICE_HPP
#define TB_SEARCH_SERVICE_HPP
#include <QObject>
#include <QVariantMap>
#include <QMap>
namespace tieba {
class TbApiService;
class TbSession;
class TbSearchService : public QObject {
    Q_OBJECT
    Q_PROPERTY(int version READ version NOTIFY versionChanged)
public:
    explicit TbSearchService(TbApiService *api, TbSession *session, QObject *parent = 0);
    int version() const { return m_version; }
    Q_INVOKABLE QVariantMap state(const QString &query, const QString &kind, const QString &forum = QString()) const;
    Q_INVOKABLE void load(const QString &query, const QString &kind, const QString &forum = QString(), bool append = false);
public slots:
    void clear();
signals:
    void versionChanged();
private slots:
    void loaded(int requestId, const QVariantMap &result);
    void failed(int requestId, const QString &tag, const QString &message, const QVariantMap &detail);
private:
    static QString scope(const QString &query, const QString &kind, const QString &forum);
    static QVariantMap emptyState();
    void notify();
    TbApiService *m_api;
    int m_version;
    QMap<QString, QVariantMap> m_states;
    QMap<int, QVariantMap> m_requests;
};
}
#endif
