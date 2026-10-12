#ifndef TB_SIGN_SERVICE_HPP
#define TB_SIGN_SERVICE_HPP

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QSet>
#include <QTimer>
#include <QPointer>
#include "net/TbHttpClient.hpp"

namespace tieba {
class TbSession;

// Explicitly started, account-scoped sequential signing. Never starts in ctor.
class TbSignService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap state READ state NOTIFY changed)
    Q_PROPERTY(QVariantList rows READ rows NOTIFY changed)
    Q_PROPERTY(int version READ version NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(int intervalSeconds READ intervalSeconds WRITE setIntervalSeconds NOTIFY changed)
public:
    TbSignService(TbHttpClient *client, TbSession *session, QObject *parent = 0);
    QVariantMap state() const;
    QVariantList rows() const { return m_rows; }
    int version() const { return m_version; }
    bool loading() const { return m_loading; }
    int intervalSeconds() const { return m_intervalSeconds; }
    // Pure request/parser helpers also allow offline protocol regression tests.
    static TbHttpRequest signRequest(const QString &bduss, const QString &tbs,
            const QString &fid, const QString &name, qint64 timestampSeconds);
    static TbHttpRequest tbsRequest(const QString &bduss, const QString &stoken = QString());
    static QVariantList forumRows(const TbJson &json);
    static QString responseStatus(const TbJson &json);
public slots:
    void start();
    void cancel();
    void retryFailed();
    void setIntervalSeconds(int seconds);
signals:
    void changed();
private slots:
    void onFinished(int requestId, const QString &tag, const tieba::TbJson &json,
                    const tieba::TbHttpResponse &response);
    void onSessionChanged();
    void sendNext();
private:
    void notify();
    void fetchForums();
    void fetchTbs();
    void fail(const QString &message);
    bool sameAccount() const;
    void prepareRun();
    void updateRow(int index, const QString &status, const QString &message);
    QPointer<TbHttpClient> m_client;
    QPointer<TbSession> m_session;
    QTimer m_timer;
    QVariantList m_rows;
    QList<int> m_queue;
    QSet<QString> m_seen;
    QStringList m_logs;
    QString m_accountId, m_uid, m_bduss, m_tbs, m_error, m_currentName;
    bool m_loading;
    int m_version, m_intervalSeconds, m_requestId, m_stage, m_page, m_currentIndex, m_failureStage;
};
}
#endif
