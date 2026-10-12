/*
 * 吧列表本地缓存的往返测试
 *
 * 缓存这条路很容易"看起来写了但其实没生效"：
 *   - JSON 拼错 -> 下次读失败 -> 静默退回网络请求（用户察觉不到机制没工作）
 *   - 字段漏存 -> 从缓存恢复的吧列表缺数据（例如没有吧名）
 * 所以这里把「写进去再读出来」和「字段是否齐全」都断言一遍。
 *
 * 实现方式是直接测 TiebaController 的缓存接口（loadForumsFromCache /
 * saveForumsToCache），配合 setCacheDirForTest 指到临时目录。
 */

#include "TiebaController.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QTextStream>
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

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    printf("== 吧列表缓存 ==\n");

    // 临时目录（Qt 4.8 没有 QTemporaryDir）
    const QString tmp = QDir::currentPath() + QLatin1String("/tests/tmp/bbtieba-forumcache-")
                        + QString::number(QDateTime::currentMSecsSinceEpoch());
    if (!QDir().mkpath(tmp)) {
        printf("  FAIL cannot create fixture directory\n");
        return 1;
    }

    TiebaController c;
    c.setCacheDirForTest(tmp);

    // 缓存文件路径应该在临时目录下
    const QString file = c.forumCacheFileForTest();
    check("缓存路径非空", !file.isEmpty());
    check("缓存路径在指定目录内", file.startsWith(tmp));

    /* ---- 初始状态：没有缓存 ---- */
    check("初始状态没有缓存文件", !QFile::exists(file));

    /* ---- 写入：先用一个可序列化的吧列表 ---- */
    {
        QVariantList forums;

        QVariantMap a;
        a.insert(QLatin1String("id"), QLatin1String("100"));
        a.insert(QLatin1String("name"), QLatin1String("黑莓"));
        a.insert(QLatin1String("level"), 7);
        a.insert(QLatin1String("avatarUrl"),
                 QLatin1String("https://x.invalid/a.jpg"));
        a.insert(QLatin1String("isSigned"), true);
        forums.append(a);

        // 故意放一个带引号和反斜杠的吧名，验证 JSON 转义
        QVariantMap b;
        b.insert(QLatin1String("id"), QLatin1String("200"));
        b.insert(QLatin1String("name"), QString::fromUtf8("带\"引号\"和\\反斜杠"));
        b.insert(QLatin1String("level"), 0);
        b.insert(QLatin1String("avatarUrl"), QString());
        b.insert(QLatin1String("isSigned"), false);
        forums.append(b);

        c.setForumsModelForTest(forums);
        checkInt("测试用模型已填充 2 项", c.forumsForTestCount(), 2);

        c.saveForumsToCache();
        check("缓存文件已生成", QFile::exists(file));
    }

    /* ---- 读回：字段必须完整 ---- */
    {
        // 清空模型，模拟"下次启动"
        c.setForumsModelForTest(QVariantList());
        checkInt("模型已清空", c.forumsForTestCount(), 0);

        const bool hit = c.loadForumsFromCache();
        check("缓存命中", hit);
        checkInt("恢复出 2 个吧", c.forumsForTestCount(), 2);

        const QVariantList got = c.forumsModelForTest();
        if (got.size() == 2) {
            const QVariantMap a = got.at(0).toMap();
            checkStr("id 保留", a.value("id").toString(), QLatin1String("100"));
            checkStr("name 保留", a.value("name").toString(), QLatin1String("黑莓"));
            checkInt("level 保留", a.value("level").toInt(), 7);
            checkStr("avatarUrl 保留", a.value("avatarUrl").toString(),
                     QLatin1String("https://x.invalid/a.jpg"));
            check("isSigned 保留", a.value("isSigned").toBool() == true);
            // levelText 是从 level 算出来的展示字段，缓存路径也要补上
            checkStr("levelText 已补", a.value("levelText").toString(),
                     QLatin1String("7"));
            // remoteImageUrl 是 refreshImagePaths 重算本地路径的依据
            checkStr("remoteImageUrl 已补", a.value("remoteImageUrl").toString(),
                     QLatin1String("https://x.invalid/a.jpg"));

            const QVariantMap b = got.at(1).toMap();
            checkStr("带引号/反斜杠的吧名转义正确",
                     b.value("name").toString(),
                     QString::fromUtf8("带\"引号\"和\\反斜杠"));
            check("第二个吧 isSigned=false",
                  b.value("isSigned").toBool() == false);
        }
    }

    /* ---- 损坏的缓存 → 返回 false 且不崩 ---- */
    {
        QFile f(file);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.write("{ this is not json at all ");
            f.close();
        }
        TiebaController c2;
        c2.setCacheDirForTest(tmp);
        const bool hit = c2.loadForumsFromCache();
        check("损坏缓存不命中", !hit);
        checkInt("损坏缓存后模型为空", c2.forumsForTestCount(), 0);
        // 实现里会把坏文件删掉，免得每次都白读一遍
        check("损坏缓存已被删除", !QFile::exists(file));
    }

    /* ---- 清缓存 ---- */
    {
        c.saveForumsToCache();
        check("重新写入成功", QFile::exists(file));
        c.clearForumsCache();
        check("清缓存后文件不存在", !QFile::exists(file));
    }

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
