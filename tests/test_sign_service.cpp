#include "api/TbSignService.hpp"
#include "util/TbMd5.hpp"
#include <QCoreApplication>
#include <stdio.h>

static int checks = 0, failures = 0;
static void check(const char *name, bool pass) {
    ++checks; if (!pass) ++failures;
    printf("%s %s\n", pass ? "OK" : "FAIL", name);
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    tieba::TbSignService service(0, 0);
    check("construction never starts a request", !service.loading());
    check("default interval is four seconds", service.intervalSeconds() == 4);
    service.setIntervalSeconds(1);
    check("short intervals clamp to four seconds", service.intervalSeconds() == 4);
    service.start();
    check("signed-out start stays idle", !service.loading());
    check("signed-out start reports an error", !service.state().value("error").toString().isEmpty());
    service.cancel();
    check("cancel remains idle", !service.loading());
    tieba::TbHttpResponse lateResponse;
    lateResponse.ok = true;
    const tieba::TbJson lateJson = tieba::TbJson::parse("{\"error_code\":0}");
    const int cancelledVersion = service.version();
    check("late-response fixture reaches the private Qt slot", QMetaObject::invokeMethod(
              &service, "onFinished", Qt::DirectConnection, Q_ARG(int, 999),
              Q_ARG(QString, QString("sign.submit")), Q_ARG(tieba::TbJson, lateJson),
              Q_ARG(tieba::TbHttpResponse, lateResponse)));
    check("cancelled run ignores late success responses", service.version() == cancelledVersion
          && service.rows().isEmpty() && !service.loading());

    const QString name = QString::fromUtf8("黑莓 & 测试");
    const tieba::TbHttpRequest request = tieba::TbSignService::signRequest(
                "fake-credential", "fake-ticket", "123", name, 1700000000);
    check("HTTPS sign endpoint", request.url == "https://c.tieba.baidu.com/c/c/forum/sign");
    check("POST method", request.method == "POST");
    check("phone model plus is form-encoded", request.body.contains("model=MI%2B5"));
    check("forum UTF8 and ampersand are form-encoded", request.body.contains("kw=%E9%BB%91%E8%8E%93+%26+%E6%B5%8B%E8%AF%95"));
    check("timestamp is seconds", request.body.contains("timestamp=1700000000"));
    check("Android client version matches supplied protocol", request.body.contains("_client_version=9.7.8.0"));
    check("IMEI contains fifteen zeroes", request.body.contains("_phone_imei=000000000000000&"));
    const QByteArray raw = QByteArray("BDUSS=fake-credential_client_type=2_client_version=9.7.8.0_phone_imei=000000000000000fid=123kw=")
            + name.toUtf8() + QByteArray("model=MI+5net_type=1tbs=fake-tickettimestamp=1700000000tiebaclient!!!");
    check("signature hashes sorted raw values without separators", request.body.contains(
              QByteArray("sign=") + tieba::TbMd5::hex(raw).toUpper().toLatin1()));
    bool cookie = false;
    for (int i = 0; i < request.headers.size(); ++i)
        if (request.headers.at(i).first == "Cookie") cookie = request.headers.at(i).second == "BDUSS=fake-credential";
    check("cookie comes from the selected account", cookie);
    const tieba::TbHttpRequest tbsRequest = tieba::TbSignService::tbsRequest("fake-credential", "fake-stoken");
    check("ticket uses the verified client login endpoint", tbsRequest.url == "https://c.tieba.baidu.com/c/s/login" && tbsRequest.method == "POST");
    check("ticket refresh has its own routing tag", tbsRequest.tag == "sign.tbs");
    check("ticket credential follows shared login builder", tbsRequest.body.contains("bdusstoken=fake-credential%7C")
          && tbsRequest.body.contains("stoken=fake-stoken"));
    bool plainCookie = false;
    for (int i = 0; i < tbsRequest.headers.size(); ++i)
        if (tbsRequest.headers.at(i).first == "Cookie") plainCookie = tbsRequest.headers.at(i).second == "ka=open";
    check("ticket does not depend on authenticated web cookie", plainCookie);

    const tieba::TbJson forums = tieba::TbJson::parse(
                "{\"error_code\":0,\"forum_list\":{\"non-gconforum\":[{\"id\":\"100\",\"name\":\"one\"}],"
                "\"gconforum\":[{\"id\":\"200\",\"name\":\"two\",\"is_sign\":1}]},\"has_more\":1}");
    const QVariantList rows = tieba::TbSignService::forumRows(forums);
    check("both forum groups are collected", rows.size() == 2);
    check("normal forum starts pending", rows.at(0).toMap().value("status") == "pending");
    check("server signed flag is honored", rows.at(1).toMap().value("status") == "already");
    check("success response", tieba::TbSignService::responseStatus(tieba::TbJson::parse("{\"error_code\":0}")) == "success");
    check("already signed response", tieba::TbSignService::responseStatus(tieba::TbJson::parse("{\"error_code\":160002}")) == "already");
    check("API rejection is failed", tieba::TbSignService::responseStatus(tieba::TbJson::parse("{\"error_code\":3}")) == "failed");
    check("missing success code is failed", tieba::TbSignService::responseStatus(tieba::TbJson::parse("{}")) == "failed");
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
