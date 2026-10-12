/*
 * 多账号的往返测试
 *
 * 为什么专门测：
 *   - 账号"看起来存了其实没存" -> 每次重启都要重新粘贴 BDUSS（很烦）；
 *   - 明文凭证漏进 QML 模型 -> 凭据会被打到日志/界面上（安全问题）；
 *   - 切换账号没清旧数据 -> 界面顶着上一个账号的内容（看起来像串号）。
 * 所以这里把「存 / 切 / 删 / 打码 / 迁移」都断言一遍。
 *
 * ★ 存储必须改道（setSettingsFileForTest）：默认位置存着**用户真实的
 *   BDUSS**，测试往那儿写会把开发机上已登录的账号冲掉。
 */

#include "TiebaController.hpp"
#include "session/TbSession.hpp"
#include <bb/cascades/ArrayDataModel>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <stdio.h>

static int g_checks = 0;
static int g_failures = 0;

static void check(const QString &label, bool ok)
{
    ++g_checks;
    if (ok) {
        printf("  OK   %s\n", qPrintable(label));
    } else {
        printf("  FAIL %s\n", qPrintable(label));
        ++g_failures;
    }
}

static void checkInt(const QString &label, int actual, int expected)
{
    ++g_checks;
    if (actual == expected) {
        printf("  OK   %s\n", qPrintable(label));
    } else {
        printf("  FAIL %s (expected=%d actual=%d)\n", qPrintable(label),
               expected, actual);
        ++g_failures;
    }
}

static void checkStr(const QString &label, const QString &actual,
                     const QString &expected)
{
    ++g_checks;
    if (actual == expected) {
        printf("  OK   %s\n", qPrintable(label));
    } else {
        printf("  FAIL %s\n     expected=[%s]\n     actual  =[%s]\n",
               qPrintable(label), qPrintable(expected), qPrintable(actual));
        ++g_failures;
    }
}

// Account lifecycle fixtures must complete server verification before asserting saved state.
static bool verifiedLogin(TiebaController &controller, const QString &bduss)
{
    if (!controller.login(bduss))
        return false;
    QVariantMap profile;
    profile.insert("uid", "42");
    profile.insert("userName", "fixture-user");
    return QMetaObject::invokeMethod(&controller, "onLoginVerified", Qt::DirectConnection,
        Q_ARG(int, controller.loginRequestIdForTest()), Q_ARG(QVariantMap, profile));
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    printf("== 多账号 ==\n");

    const QString tmp = QDir::currentPath() + QLatin1String("/tests/tmp/bbtieba-accounts-")
                        + QString::number(QDateTime::currentMSecsSinceEpoch());
    if (!QDir().mkpath(tmp)) {
        printf("  FAIL unable to create isolated test directory\n");
        return 1;
    }

    const QString ini = tmp + QLatin1String("/session.ini");

    // 两份"看起来像 BDUSS"的长串（真实 BDUSS 约 190 字符，长度要够 looksLikeBduss）
    QString bdussA;
    QString bdussB;
    for (int i = 0; i < 60; ++i) {
        bdussA += QLatin1String("Ab3xY9");
        bdussB += QLatin1String("Zz7qW2");
    }
    bdussA = bdussA.left(190);
    bdussB = bdussB.left(190);

    TiebaController c;
    c.setCacheDirForTest(tmp);
    c.setSessionFileForTest(ini);

    /* ---- 初始：没有账号 ---- */
    checkInt("初始没有账号", c.otherAccountCount(), 0);
    check("初始没有当前账号", c.currentAccount().isEmpty());
    check("初始未登录", ! c.loggedIn());

    /* ---- 粘贴第一份 BDUSS ---- */
    check("login(A) 成功", verifiedLogin(c, bdussA));
    check("已登录", c.loggedIn());
    checkInt("账号列表 1 条", c.otherAccountCount() + 1, 1);

    const QVariantMap accA = c.currentAccount();
    checkStr("当前账号 id 非空", accA.value("id").toString().isEmpty()
             ? QString::fromLatin1("(空)") : QString::fromLatin1("(有)"),
             QString::fromLatin1("(有)"));

    /* ---- ★ 明文绝不能出现在给 QML 的数据里 ---- */
    {
        bool leaked = false;
        const QVariantList all = QVariantList() << accA;
        for (int i = 0; i < all.size(); ++i) {
            const QVariantMap m = all.at(i).toMap();
            if (m.contains(QLatin1String("bduss"))
                    || m.contains(QLatin1String("stoken")))
                leaked = true;
            for (QVariantMap::const_iterator it = m.constBegin(); it != m.constEnd(); ++it)
                if (it.value().toString().contains(bdussA))
                    leaked = true;
        }
        check("给 QML 的账号里没有明文 BDUSS", ! leaked);
        check("masked 是打码后的（前8+…+后4）",
              accA.value("masked").toString()
              == bdussA.left(8) + QString::fromUtf8("…") + bdussA.right(4));
        check("masked 不含完整明文",
              accA.value("masked").toString() != bdussA);
    }

    /* ---- 明文只在 credential() 里 ---- */
    checkStr("credential(id) 取到明文",
             c.accountCredential(accA.value("id").toString()), bdussA);
    checkStr("credential(不存在的 id) 返回空",
             c.accountCredential(QLatin1String("no-such-id")), QString());

    /* ---- 同一份 BDUSS 贴两次：不能变成两条 ---- */
    {
        const QString id1 = accA.value("id").toString();
        check("重复粘贴同一份 BDUSS 成功", verifiedLogin(c, bdussA));
        checkStr("还是同一个账号 id",
                 c.currentAccount().value("id").toString(), id1);
        checkInt("列表仍然只有 1 条", c.otherAccountCount() + 1, 1);
    }

    /* ---- 粘贴第二份：多账号成型 ---- */
    check("login(B) 成功", verifiedLogin(c, bdussB));
    checkInt("账号列表 2 条", c.otherAccountCount() + 1, 2);
    {
        const QString idA = c.otherAccounts()->value(0).toMap()
                                .value("id").toString();
        check("「已登录」里是 A（当前是 B）",
              c.accountCredential(idA) == bdussA);
        checkStr("当前是 B", c.accountCredential(
                     c.currentAccount().value("id").toString()), bdussB);
    }

    /* ---- 切回 A ---- */
    {
        const QString idA = c.otherAccounts()->value(0).toMap()
                                .value("id").toString();
        check("switchAccount(A) 成功", c.switchAccount(idA));
        checkStr("切换后当前是 A", c.accountCredential(
                     c.currentAccount().value("id").toString()), bdussA);
        checkInt("切换后「已登录」里是 B（1 条）", c.otherAccountCount(), 1);
        check("switchAccount(不存在的 id) 失败",
              ! c.switchAccount(QLatin1String("no-such-id")));
    }

    /* ---- 退出登录：账号要留着 ---- */
    {
        c.logout();
        check("退出后未登录", ! c.loggedIn());
        checkInt("退出后账号还在（2 条都在「已登录」里）",
                 c.otherAccountCount(), 2);
    }

    /* ---- 删除账号 ---- */
    {
        const QString idB = c.otherAccounts()->value(0).toMap()
                                .value("id").toString();
        check("删除一个非当前账号成功", c.removeAccount(idB));
        checkInt("删完剩 1 条", c.otherAccountCount(), 1);

        // 再登回 A，然后删掉**当前**账号 -> 应该连带退出登录
        const QString idA = c.otherAccounts()->value(0).toMap()
                                .value("id").toString();
        check("登录 A", c.switchAccount(idA));
        check("删除当前账号成功", c.removeAccount(idA));
        check("删掉当前账号后变成未登录", ! c.loggedIn());
        checkInt("账号列表已空", c.otherAccountCount(), 0);
    }

    /* ---- 往返：新 controller 读同一个 ini ---- */
    {
        check("重新 login(A)", verifiedLogin(c, bdussA));
        const QString idA = c.currentAccount().value("id").toString();

        TiebaController again;
        again.setCacheDirForTest(tmp);
        again.setSessionFileForTest(ini);      // 模拟"下次启动"
        check("重启后仍然登录", again.loggedIn());
        checkStr("重启后当前账号还是 A",
                 again.accountCredential(
                     again.currentAccount().value("id").toString()), bdussA);
        checkStr("重启后账号 id 不变",
                 again.currentAccount().value("id").toString(), idA);
    }

    /* ---- 老版本升级：只有一份裸 BDUSS，没有账号列表 ---- */
    {
        const QString legacyIni = tmp + QLatin1String("/legacy.ini");
        {
            QSettings s(legacyIni, QSettings::IniFormat);
            s.setValue(QLatin1String("session/bduss"), bdussA);
            s.setValue(QLatin1String("session/stoken"), QLatin1String("st-legacy"));
            s.setValue(QLatin1String("session/uid"), QLatin1String("3623476479"));
            s.setValue(QLatin1String("session/nick_name"),
                       QString::fromUtf8("老用户"));
            s.sync();
        }
        TiebaController legacy;
        legacy.setCacheDirForTest(tmp);
        legacy.setSessionFileForTest(legacyIni);
        check("升级后仍然登录（不会被挤下线）", legacy.loggedIn());
        checkInt("升级后账号列表有 1 条", legacy.otherAccountCount() + 1, 1);
        checkStr("升级后昵称保留",
                 legacy.currentAccount().value("nickName").toString(),
                 QString::fromUtf8("老用户"));
        checkStr("升级后 uid 保留",
                 legacy.currentAccount().value("uid").toString(),
                 QLatin1String("3623476479"));
    }

    // Regression: the old Cookie-first parser stored 192 + 8 + 634 = 834 bytes.
    {
        const QString value(192, QLatin1Char('A'));
        const QString token(634, QLatin1Char('S'));
        const QString form = "BDUSS=" + value + "&STOKEN=" + token;
        const QString oldStoredValue = value + "&STOKEN=" + token;
        checkInt("synthetic old stored value matches runtime length", oldStoredValue.size(), 834);
        checkStr("form extracts only BDUSS", tieba::TbSession::extractCookieValue(form, "BDUSS"), value);
        checkStr("form retains the entire separate STOKEN", tieba::TbSession::extractCookieValue(form, "STOKEN"), token);
        checkStr("bare BDUSS is unchanged", tieba::TbSession::normalizeBdussInput(value), value);
        checkStr("form normalizes to the bare value", tieba::TbSession::normalizeBdussInput(form), value);
        checkStr("old saved form tail is removable", tieba::TbSession::normalizeBdussInput(oldStoredValue), value);
        checkStr("Cookie header and mixed separators work", tieba::TbSession::normalizeBdussInput(
            "Cookie: foo=bar; bduss=" + value + "\r\nSTOKEN=" + token), value);
        checkStr("encoded form is recognized", tieba::TbSession::normalizeBdussInput(
            QString::fromLatin1(QUrl::toPercentEncoding(form))), value);
        const QString padded = QString(190, QLatin1Char('A')) + "+=";
        checkStr("percent decoding preserves literal plus and padding", tieba::TbSession::normalizeBdussInput(
            "BDUSS=" + QString::fromLatin1(QUrl::toPercentEncoding(padded)) + "&STOKEN=test"), padded);
        check("invalid extracted value is rejected", tieba::TbSession::normalizeBdussInput("BDUSS=bad value").isEmpty());
        check("an unrelated key is not a bare credential", tieba::TbSession::normalizeBdussInput("other=" + value).isEmpty());
        check("BDUSS_BFESS does not substitute for BDUSS", tieba::TbSession::normalizeBdussInput("BDUSS_BFESS=" + value).isEmpty());
        checkInt("UI previews extracted length, not full form length", c.credentialInputLength(form), 192);

        tieba::TbSession parsed;
        const QString parsedIni = tmp + "/parsed.ini";
        parsed.setSettingsFileForTest(parsedIni);
        check("form can be saved", parsed.setCredentials(form));
        checkStr("saved BDUSS has no other fields", parsed.bduss(), value);
        checkStr("saved STOKEN is separate", parsed.stoken(), token);
        check("same bare credential can be saved", parsed.setCredentials(value));
        checkInt("form and bare value represent one account", parsed.accounts().size(), 1);
        const QString previous = parsed.bduss();
        check("invalid new input is refused", !parsed.setCredentials("BDUSS=too-short"));
        checkStr("invalid new input does not overwrite current account", parsed.bduss(), previous);

        const QString repairIni = tmp + "/repair.ini";
        const QString oldId = tieba::TbSession::accountIdFor(oldStoredValue);
        const QString newId = tieba::TbSession::accountIdFor(value);
        QVariantMap corrupt;
        corrupt.insert("id", oldId);
        corrupt.insert("bduss", oldStoredValue);
        corrupt.insert("nickName", "Original current profile");
        corrupt.insert("uid", "42");
        QVariantMap clean = corrupt;
        clean.insert("id", newId);
        clean.insert("bduss", value);
        clean.insert("nickName", "Duplicate profile");
        QVariantMap other;
        other.insert("id", tieba::TbSession::accountIdFor(bdussB));
        other.insert("bduss", bdussB);
        other.insert("nickName", "Other account");
        {
            QSettings s(repairIni, QSettings::IniFormat);
            s.setValue("session/bduss", oldStoredValue);
            s.setValue("session/uid", "42");
            s.setValue("session/nick_name", "Original current profile");
            s.setValue("accounts/current", oldId);
            s.setValue("accounts/list", QVariantList() << corrupt << clean << other);
            s.sync();
        }
        tieba::TbSession repaired;
        repaired.setSettingsFileForTest(repairIni);
        repaired.load();
        checkStr("load repairs current credential", repaired.bduss(), value);
        checkStr("load recovers separate STOKEN", repaired.stoken(), token);
        checkStr("load updates the current account ID", repaired.currentAccount().value("id").toString(), newId);
        checkInt("migration merges duplicate accounts", repaired.accounts().size(), 2);
        checkStr("migration preserves the current profile", repaired.currentAccount().value("nickName").toString(), "Original current profile");
        check("migration preserves another account", repaired.switchTo(other.value("id").toString()));
        checkStr("another account retains its credential", repaired.bduss(), bdussB);
        check("corrected account can still be selected", repaired.switchTo(newId));
        repaired.load();
        checkStr("repair persists across reload", repaired.bduss(), value);
        checkInt("reload does not create duplicate accounts", repaired.accounts().size(), 2);
    }

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures > 0 ? 1 : 0;
}
