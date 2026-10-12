#include <QCoreApplication>
#include <QCryptographicHash>
#include <QUrl>
#include <cstdio>
#include "api/TbApi.hpp"
#include "net/TbSigner.hpp"
using namespace tieba;
static int checks = 0, failures = 0;
static void check(const char *name, bool success) { ++checks; if (!success) ++failures; std::printf("%s %s\n", success ? "OK" : "FAIL", name); }
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QList<KeyValue> params;
    params << qMakePair(QString("z"), QString("a&b"))
           << qMakePair(QString("_client_version"), QString("11.10.8.6"))
           << qMakePair(QString("A"), QString("a+b"))
           << qMakePair(QString("sign"), QString("old"));
    const QByteArray canonical("A=a+b_client_version=11.10.8.6z=a&btiebaclient!!!");
    const QByteArray expected = QCryptographicHash::hash(canonical, QCryptographicHash::Md5).toHex().toUpper();
    check("ordinal raw signature matches independent Qt hash", TbSigner::androidSign(params).toLatin1() == expected);
    check("generic ampersand signature differs", TbSigner::sign(params).toLatin1().toUpper() != expected);
    const QByteArray form = TbSigner::buildAndroidForm(params);
    check("one fresh signature", form.count("sign=") == 1 && form.contains("sign=" + expected));
    check("raw plus and ampersand form encoded", form.contains("A=a%2Bb") && form.contains("z=a%26b"));
    const QString fake(192, QLatin1Char('A'));
    const TbHttpRequest request = TbApiService::buildLoginRequest(fake, "");
    QUrl parsed(QString("https://test.invalid/?") + QString::fromUtf8(request.body));
    check("dedicated HTTPS login endpoint", request.url == "https://c.tieba.baidu.com/c/s/login");
    check("credential only bdusstoken parameter", QUrl::fromPercentEncoding(parsed.queryItemValue("bdusstoken").toUtf8()) == fake + "|" && !parsed.hasQueryItem("BDUSS"));
    check("empty STOKEN is permitted", parsed.hasQueryItem("stoken") && parsed.queryItemValue("stoken").isEmpty());
    check("authentication cookie excludes credential", request.headers.last().second == "ka=open");
    QStringList rawFields;
    for (int i = 0; i < request.params.size(); ++i) rawFields << request.params.at(i).first + "=" + request.params.at(i).second;
    rawFields.sort();
    const QByteArray independent = QCryptographicHash::hash((rawFields.join("") + "tiebaclient!!!").toUtf8(), QCryptographicHash::Md5).toHex().toUpper();
    check("request matches independent no-separator protocol", parsed.queryItemValue("sign").toLatin1() == independent);
    check("request does not use legacy signing", parsed.queryItemValue("sign") != TbSigner::sign(request.params));
    std::printf("Login protocol: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
