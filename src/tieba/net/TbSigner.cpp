#include "TbSigner.hpp"

#include "session/TbSession.hpp"
#include "util/TbMd5.hpp"
#include "util/TbString.hpp"

#include <QDateTime>
#include <QStringList>
#include <QtGlobal>

#include <stdlib.h>

namespace tieba {

namespace {

/*! 贴吧客户端的默认版本号 */
const char kDefaultClientVersion[] = "7.2.0.0";

} // anonymous namespace

QString TbSigner::apiBase()
{
    // 直接使用已验证的 TLS 端点，避免明文凭据及 HTTP 中间响应。
    return QLatin1String("https://c.tieba.baidu.com/");
}

QString TbSigner::webBase()
{
    return QLatin1String("https://tieba.baidu.com/");
}

QByteArray TbSigner::salt()
{
    return QByteArray("tiebaclient!!!");
}

QString TbSigner::newClientId()
{
    static bool seeded = false;
    if (!seeded) {
        srand((unsigned int)QDateTime::currentMSecsSinceEpoch());
        seeded = true;
    }
    const qint64 ms = (qint64)QDateTime::currentMSecsSinceEpoch();
    const int suffix = rand() % 1000;
    return QString(QLatin1String("wappc_%1_%2")).arg(ms).arg(suffix);
}

QString TbSigner::deviceModel()
{
    // 真机上可换成实际机型；这里给一个稳定值，避免每次请求都变
    return QLatin1String("BlackBerry");
}

QList<KeyValue> TbSigner::commonParams(const TbSession *session,
                                       const QString &clientVersion)
{
    QString version = clientVersion;
    if (version.isEmpty())
        version = QLatin1String(kDefaultClientVersion);

    QList<KeyValue> params;
    params.append(qMakePair(QString(QLatin1String("_client_id")), newClientId()));
    params.append(qMakePair(QString(QLatin1String("_client_type")), QString(QLatin1String("2"))));
    params.append(qMakePair(QString(QLatin1String("_client_version")), version));
    params.append(qMakePair(QString(QLatin1String("_phone_imei")),
                            QString(QLatin1String("0000000000000000"))));
    params.append(qMakePair(QString(QLatin1String("model")), deviceModel()));
    params.append(qMakePair(QString(QLatin1String("net_type")), QString(QLatin1String("1"))));
    params.append(qMakePair(QString(QLatin1String("timestamp")),
                            QString::fromLatin1(TbString::currentTimestampMs())));

    if (session && !session->bduss().isEmpty())
        params.append(qMakePair(QString(QLatin1String("BDUSS")), session->bduss()));

    return params;
}

QList<KeyValue> TbSigner::withCommonParams(const QList<KeyValue> &params,
                                           const TbSession *session,
                                           const QString &clientVersion)
{
    QList<KeyValue> out = params;
    const QList<KeyValue> common = commonParams(session, clientVersion);

    for (int i = 0; i < common.size(); ++i) {
        const QString &key = common.at(i).first;
        bool exists = false;
        for (int j = 0; j < out.size(); ++j) {
            if (out.at(j).first == key) {
                exists = true;
                break;
            }
        }
        if (!exists)
            out.append(common.at(i));
    }
    return out;
}

bool TbSigner::needsSign(const QList<KeyValue> &params)
{
    for (int i = 0; i < params.size(); ++i) {
        if (params.at(i).first == QLatin1String("_client_version"))
            return true;
    }
    return false;
}

QString TbSigner::sign(const QList<KeyValue> &params)
{
    if (params.isEmpty() || !needsSign(params))
        return QString();

    const QString raw = TbString::sortedRawString(params);
    const QByteArray payload = raw.toUtf8() + salt();
    return TbMd5::hex(payload);
}

QByteArray TbSigner::buildSignedForm(const QList<KeyValue> &params)
{
    QList<KeyValue> finalParams = params;

    if (needsSign(finalParams)) {
        // sign 本身不参与排序（官方实现是排序后直接追加）
        const QString signValue = sign(finalParams);
        if (!signValue.isEmpty())
            finalParams.append(qMakePair(QString(QLatin1String("sign")), signValue));
    }

    QStringList parts;
    for (int i = 0; i < finalParams.size(); ++i) {
        parts.append(TbString::percentEncode(finalParams.at(i).first)
                     + QLatin1Char('=')
                     + TbString::percentEncode(finalParams.at(i).second));
    }
    return parts.join(QLatin1String("&")).toUtf8();
}

QString TbSigner::androidSign(const QList<KeyValue> &params)
{
    QList<KeyValue> sorted = params;
    for (int i = 1; i < sorted.size(); ++i) {
        const KeyValue field = sorted.at(i);
        int j = i - 1;
        while (j >= 0 && sorted.at(j).first > field.first) {
            sorted[j + 1] = sorted.at(j);
            --j;
        }
        sorted[j + 1] = field;
    }
    QString raw;
    for (int i = 0; i < sorted.size(); ++i) {
        if (sorted.at(i).first == QLatin1String("sign") || sorted.at(i).first == QLatin1String("sig")) continue;
        raw += sorted.at(i).first + QLatin1Char('=') + sorted.at(i).second;
    }
    return TbMd5::hex(raw.toUtf8() + salt()).toUpper();
}

QByteArray TbSigner::buildAndroidForm(const QList<KeyValue> &params)
{
    QList<KeyValue> fields;
    for (int i = 0; i < params.size(); ++i)
        if (params.at(i).first != QLatin1String("sign") && params.at(i).first != QLatin1String("sig")) fields.append(params.at(i));
    fields.append(qMakePair(QString(QLatin1String("sign")), androidSign(fields)));
    return TbString::buildFormBody(fields);
}

QString TbSigner::buildSignedQuery(const QList<KeyValue> &params)
{
    QString query = TbString::sortedEncodedString(params);

    if (needsSign(params)) {
        const QString signValue = sign(params);
        if (!signValue.isEmpty()) {
            if (!query.isEmpty())
                query.append(QLatin1Char('&'));
            query.append(QLatin1String("sign=") + TbString::percentEncode(signValue));
        }
    }
    return query;
}

QString TbSigner::userAgent(const QString &clientVersion)
{
    QString version = clientVersion;
    if (version.isEmpty())
        version = QLatin1String(kDefaultClientVersion);
    return QString(QLatin1String("bdtb for Android %1")).arg(version);
}

/* ============================ PC 端接口 ============================ */

QByteArray TbSigner::pcSalt()
{
    return QByteArray("36770b1f34c9bbf2e7d1a99d2b82fa9e");
}

QList<KeyValue> TbSigner::pcCommonParams(const TbSession *session)
{
    QList<KeyValue> params;
    params.append(qMakePair(QString(QLatin1String("subapp_type")), QString(QLatin1String("pc"))));
    params.append(qMakePair(QString(QLatin1String("_client_type")), QString(QLatin1String("20"))));

    if (session && !session->bduss().isEmpty())
        params.append(qMakePair(QString(QLatin1String("BDUSS")), session->bduss()));

    return params;
}

QList<KeyValue> TbSigner::withPcCommonParams(const QList<KeyValue> &params,
                                             const TbSession *session)
{
    QList<KeyValue> out = params;
    const QList<KeyValue> common = pcCommonParams(session);

    for (int i = 0; i < common.size(); ++i) {
        const QString &key = common.at(i).first;
        bool exists = false;
        for (int j = 0; j < out.size(); ++j) {
            if (out.at(j).first == key) {
                exists = true;
                break;
            }
        }
        if (!exists)
            out.append(common.at(i));
    }
    return out;
}

QString TbSigner::pcSign(const QList<KeyValue> &params)
{
    if (params.isEmpty())
        return QString();

    // 按 key 的字典序排序（注意是 key，不是整个 "k=v"）
    QList<KeyValue> sorted = params;
    for (int i = 1; i < sorted.size(); ++i) {
        const KeyValue key = sorted.at(i);
        int j = i - 1;
        while (j >= 0 && sorted.at(j).first > key.first) {
            sorted[j + 1] = sorted.at(j);
            --j;
        }
        sorted[j + 1] = key;
    }

    // 无分隔符直接相连；sign / sig 自身不参与
    QString text;
    for (int i = 0; i < sorted.size(); ++i) {
        const KeyValue &kv = sorted.at(i);
        if (kv.first == QLatin1String("sign") || kv.first == QLatin1String("sig"))
            continue;
        text += kv.first;
        text += QLatin1Char('=');
        text += kv.second;
    }
    text += QString::fromLatin1(pcSalt());

    return TbMd5::hex(text.toUtf8());
}

QByteArray TbSigner::buildPcForm(const QList<KeyValue> &params)
{
    QList<KeyValue> finalParams = params;

    const QString signValue = pcSign(finalParams);
    if (!signValue.isEmpty())
        finalParams.append(qMakePair(QString(QLatin1String("sign")), signValue));

    QStringList parts;
    for (int i = 0; i < finalParams.size(); ++i) {
        parts.append(TbString::percentEncode(finalParams.at(i).first)
                     + QLatin1Char('=')
                     + TbString::percentEncode(finalParams.at(i).second));
    }
    return parts.join(QLatin1String("&")).toUtf8();
}

QString TbSigner::pcUserAgent()
{
    // PC 接口会看 UA，用常见桌面浏览器的即可
    return QLatin1String("Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                         "AppleWebKit/537.36 (KHTML, like Gecko) "
                         "Chrome/120.0.0.0 Safari/537.36");
}

} // namespace tieba
