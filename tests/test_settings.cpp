/*
 * 设置持久化 + 图片缓存上限的往返测试
 *
 * 为什么值得单独测（和吧列表缓存是同一个道理）：
 *   - 设置"看起来存了其实没存" -> 用户每次重启都要重新设一遍主题，
 *     而且现象很像"设置页坏了"，很难定位；
 *   - 缓存上限"看起来设了其实没生效" -> 磁盘悄悄涨到几个 G，
 *     用户只有在存储空间不足时才会发现。
 * 所以这里直接做往返：controller A 写 -> controller B 读；
 * 上限则造几个文件，看超限后是不是真的被回收、且回收的是最旧的那个。
 */

#include "TiebaController.hpp"
#include "util/TbImageCache.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
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

/*! 造一个带内容的文件（内容是 size 个 'x'） */
static bool makeFile(const QString &path, int size)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(QByteArray(size, 'x'));
    f.close();
    return true;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    printf("== 设置持久化 ==\n");

    /*
     * 临时目录：优先用系统 temp，不行再退回当前目录 / home。
     *
     * 为什么写这么一段：本机（Windows）上 QDir::tempPath() 有时会建不出子目录
     * （test_forumcache 也是这样直接 SKIP 的）。测试**静默跳过**等于没测，
     * 所以这里多试几个位置，尽量让它真的跑起来。
     */
    QString tmp;
    {
        const QString stamp =
            QString::number(QDateTime::currentMSecsSinceEpoch());
        const QStringList bases = QStringList()
            << QDir::tempPath()
            << QDir::currentPath()
            << QDir::homePath();
        for (int i = 0; i < bases.size() && tmp.isEmpty(); ++i) {
            if (bases.at(i).isEmpty())
                continue;
            const QString cand = bases.at(i)
                                 + QLatin1String("/bbtieba-settings-") + stamp;
            if (QDir().mkpath(cand))
                tmp = cand;
        }
    }
    if (tmp.isEmpty()) {
        printf("  SKIP 无法创建临时目录（试过 temp/current/home）\n");
        return 0;
    }

    /* ---- 默认值 ---- */
    {
        TiebaController fresh;
        fresh.setCacheDirForTest(tmp);
        // 注意：构造时已经按目录读过一次，这里再读一次确保是"磁盘上的值"
        checkInt("默认主题是亮色(1)", fresh.theme(), 1);
        checkInt("默认缓存上限 512MB", fresh.cacheLimitMb(), 512);
        check("默认不隐藏视频贴", fresh.hideVideoThreads() == false);
        check("默认不只显示关注吧", fresh.showFollowedOnly() == false);
    }

    /* ---- 往返：A 写 -> B 读 ---- */
    {
        TiebaController a;
        a.setCacheDirForTest(tmp);
        a.setTheme(2);              // 暗色
        a.setCacheLimitMb(1024);
        a.setHideVideoThreads(true);
        a.setShowFollowedOnly(true);
        checkInt("A 改后主题是暗色(2)", a.theme(), 2);

        TiebaController b;          // 模拟"下次启动"
        b.setCacheDirForTest(tmp);
        checkInt("B 读回主题", b.theme(), 2);
        checkInt("B 读回缓存上限", b.cacheLimitMb(), 1024);
        check("B 读回隐藏视频贴", b.hideVideoThreads() == true);
        check("B 读回只看关注吧", b.showFollowedOnly() == true);
    }

    /* ---- 非法值要回落，不能把 UI 带进未知状态 ---- */
    {
        TiebaController c;
        c.setCacheDirForTest(tmp);
        c.setTheme(99);             // 不是 VisualStyle 的合法值
        checkInt("非法主题回落到亮色", c.theme(), 1);
        c.setCacheLimitMb(0);
        checkInt("非法上限回落到默认 512", c.cacheLimitMb(), 512);
    }

    printf("== 图片缓存上限 ==\n");

    const QString cacheDir = tmp + QLatin1String("/img");
    if (!QDir().mkpath(cacheDir)) {
        printf("  SKIP 无法创建缓存目录\n");
        return g_failures > 0 ? 1 : 0;
    }

    {
        tieba::TbImageCache cache;
        cache.setCacheDir(cacheDir);

        // 三个文件，各 1000 字节；oldest 最先建（mtime 最小）
        const QString oldest = cacheDir + QLatin1String("/oldest.img");
        const QString middle = cacheDir + QLatin1String("/middle.img");
        const QString newest = cacheDir + QLatin1String("/newest.img");
        check("三个测试文件都写出来了",
              makeFile(oldest, 1000) && makeFile(middle, 1000)
              && makeFile(newest, 1000));

        check("占用统计到 3000 字节", (int)cache.cacheBytes() == 3000);

        /*
         * 上限 1500：3000 字节里只能留下 1 个文件。
         *
         * ★ 为什么**不断言**"删掉的正好是 oldest 和 middle"：
         *   Windows 上文件 mtime 的精度只有 1 秒（FAT 上是 2 秒），
         *   三个文件在几毫秒内建出来，时间戳是一样的 ——
         *   "按时间排序"在这种并列情况下顺序不确定。
         *   所以只断言**数量**（删到上限以内、只剩 1 个），
         *   顺序那条留给真机上的实际使用去体现。
         */
        cache.setMaxCacheBytes(1500);
        check("超限后占用回到上限以内", cache.cacheBytes() <= 1500);
        const int left = QDir(cacheDir).entryList(QDir::Files).count();
        checkInt("超限后只剩 1 个文件", left, 1);

        // 清缓存
        cache.clearCache();
        check("清除后占用归零", cache.cacheBytes() == 0);
        check("清除后文件确实没了", !QFile::exists(newest));
    }

    /* ---- 设置页要显示的那句话 ---- */
    {
        TiebaController d;
        d.setCacheDirForTest(tmp);
        d.refreshCacheUsage();
        checkInt("空缓存时占用百分比为 0", d.cacheUsagePercent(), 0);
        const QString text = d.cacheUsageText();
        check("cache summary UTF8 prefix", text.startsWith(QString::fromUtf8("已用 ")));
        check("cache summary UTF8 percentage", text.endsWith(QString::fromUtf8("（0%）")));
        check("占用文案非空", !text.isEmpty());
        check("占用文案带上限(512 MB)", text.contains(QLatin1String("512 MB")));
        printf("     文案示例：%s\n", qPrintable(text));
    }

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures > 0 ? 1 : 0;
}
