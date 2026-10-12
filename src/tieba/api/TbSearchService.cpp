#include "TbSearchService.hpp"
#include "TbApi.hpp"
#include "session/TbSession.hpp"
#include <QSet>
namespace tieba {
TbSearchService::TbSearchService(TbApiService *api, TbSession *session, QObject *parent)
    : QObject(parent), m_api(api), m_version(0)
{
    connect(api, SIGNAL(searchLoaded(int,QVariantMap)), this, SLOT(loaded(int,QVariantMap)));
    connect(api, SIGNAL(requestFailed(int,QString,QString,QVariantMap)), this, SLOT(failed(int,QString,QString,QVariantMap)));
    if (session) connect(session, SIGNAL(accountsChanged()), this, SLOT(clear()));
}
QString TbSearchService::scope(const QString &query, const QString &kind, const QString &forum)
{
    return QString::number(query.trimmed().size()) + ":" + query.trimmed() + ":" + kind + ":" + forum;
}
QVariantMap TbSearchService::emptyState()
{
    QVariantMap result;
    result.insert("items", QVariantList());
    result.insert("loading", false);
    result.insert("loaded", false);
    result.insert("error", QString());
    result.insert("hasMore", false);
    result.insert("page", 0);
    return result;
}
QVariantMap TbSearchService::state(const QString &query, const QString &kind, const QString &forum) const
{
    return m_states.value(scope(query, kind, forum), emptyState());
}
void TbSearchService::notify() { ++m_version; emit versionChanged(); }
void TbSearchService::clear() { m_requests.clear(); m_states.clear(); notify(); }
void TbSearchService::load(const QString &query, const QString &kind, const QString &forum, bool append)
{
    const QString key = scope(query, kind, forum);
    QVariantMap current = m_states.value(key, emptyState());
    if (query.trimmed().isEmpty() || current.value("loading").toBool()) return;
    if (append && !current.value("hasMore").toBool()) return;
    const int page = append ? current.value("page").toInt() + 1 : 1;
    if (!append) current = emptyState();
    current.insert("loading", true);
    current.insert("error", QString());
    m_states.insert(key, current);
    QVariantMap request;
    request.insert("scope", key);
    request.insert("page", page);
    request.insert("append", append);
    const int id = m_api->fetchSearch(query, kind, forum, page);
    if (id < 0) {
        current.insert("loading", false);
        current.insert("error", QString::fromUtf8("不支持的搜索请求"));
        m_states.insert(key, current);
    } else m_requests.insert(id, request);
    notify();
}
void TbSearchService::loaded(int requestId, const QVariantMap &result)
{
    if (!m_requests.contains(requestId)) return;
    const QVariantMap request = m_requests.take(requestId);
    const QString key = request.value("scope").toString();
    QVariantMap current = m_states.value(key, emptyState());
    QVariantList rows = current.value("items").toList();
    QSet<QString> seen;
    for (int i = 0; i < rows.size(); ++i) seen.insert(rows.at(i).toMap().value("key").toString());
    const QVariantList incoming = result.value("items").toList();
    int added = 0;
    for (int i = 0; i < incoming.size(); ++i) {
        const QVariantMap row = incoming.at(i).toMap();
        const QString identity = row.value("key").toString();
        if (identity.isEmpty() || seen.contains(identity)) continue;
        seen.insert(identity);
        rows.append(row);
        ++added;
    }
    current.insert("items", rows);
    current.insert("loading", false);
    current.insert("loaded", true);
    current.insert("error", QString());
    current.insert("page", request.value("page"));
    // A repeated/empty page must not leave automatic paging in an endless loop.
    current.insert("hasMore", added > 0 && result.value("hasMore").toBool());
    m_states.insert(key, current);
    notify();
}
void TbSearchService::failed(int requestId, const QString &tag, const QString &message, const QVariantMap &detail)
{
    Q_UNUSED(tag)
    if (!m_requests.contains(requestId)) return;
    const QString key = m_requests.take(requestId).value("scope").toString();
    QVariantMap current = m_states.value(key, emptyState());
    QString error = message;
    if (detail.value("httpStatus").toInt() > 0 && detail.value("httpStatus").toInt() != 200)
        error += QString(" (HTTP %1)").arg(detail.value("httpStatus").toInt());
    if (detail.value("networkError").toInt() != 0)
        error += QString::fromUtf8("（网络错误 %1）").arg(detail.value("networkError").toInt());
    current.insert("loading", false);
    current.insert("error", error.isEmpty() ? QString::fromUtf8("搜索请求失败，请重试") : error);
    m_states.insert(key, current);
    notify();
}
}
