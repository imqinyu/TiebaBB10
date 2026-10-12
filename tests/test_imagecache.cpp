/*
 * TbImageCache 的本机测试
 *
 * 重点验证**分流逻辑**（什么该下载、什么该原样返回），因为这部分写错
 * 会导致图片永远不显示：
 *   - http/https  -> 触发下载，先返回空串
 *   - asset/file/data -> 原样返回（ImageView 自己认）
 *   - 其它 scheme  -> 返回空串
 *
 * 真正的下载需要网络，本机测试不依赖网络：只验证"未缓存时返回空串并
 * 且不重复触发"，以及本地路径直通。
 */

#include "util/TbImageCache.hpp"
#include "util/TbMd5.hpp"
#include "util/TbString.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QTimer>
#include <QUrl>
#include <QFile>
#include <QString>
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
        printf("  FAIL %s\n     expected=%d actual=%d\n",
               qPrintable(label), expected, actual);
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
    // Exercise migration in isolated directories without touching desktop cache.
    const QString sandboxRoot = QDir::currentPath() + QLatin1String("/tests/tmp/image-cache-")
            + QString::number(QDateTime::currentMSecsSinceEpoch());
    const QString sandboxHome = sandboxRoot + QLatin1String("/home");
    const QString sandboxTemp = sandboxRoot + QLatin1String("/tmp");
    QDir().mkpath(sandboxHome);
    QDir().mkpath(sandboxTemp);
    QCoreApplication app(argc, argv);

    printf("== TbImageCache ==\n");

    {
        const QString url = QLatin1String("https://offline.invalid/migration.png");
        const QString name = tieba::TbMd5::hex(url.toUtf8()) + QLatin1String(".png");
        const QString legacyDir = sandboxTemp + QLatin1String("/bbtieba-img");
        QDir().mkpath(legacyDir);
        const QString legacyFile = legacyDir + QLatin1Char('/') + name;
        QFile old(legacyFile);
        check("write legacy cache fixture", old.open(QIODevice::WriteOnly));
        old.write("legacy-image-fixture");
        old.close();
        tieba::TbImageCache upgraded;
        upgraded.setCacheDir(sandboxHome + QLatin1String("/bbtieba-img"), legacyDir);
        const QString migrated = upgraded.pathFor(url);
        checkStr("legacy image moves to home cache", QUrl(migrated).toLocalFile(),
                 sandboxHome + QLatin1String("/bbtieba-img/") + name);
        check("migration preserves image bytes", QFileInfo(QUrl(migrated).toLocalFile()).size() == 20);
        check("migration removes temporary copy", !QFile::exists(legacyFile));
        checkInt("migration avoids download", upgraded.activeCount(), 0);
        tieba::TbImageCache reopened;
        reopened.setCacheDir(sandboxHome + QLatin1String("/bbtieba-img"), legacyDir);
        checkStr("persistent directory reused after restart", reopened.pathFor(url), migrated);
        reopened.clearCache();
        tieba::TbImageCache afterClear;
        afterClear.setCacheDir(sandboxHome + QLatin1String("/bbtieba-img"), legacyDir);
        checkStr("cleared legacy image does not resurrect", afterClear.pathFor(url), "");
        checkInt("cleared image can download again", afterClear.activeCount(), 1);
    }

    // Qt 4.8 没有 QTemporaryDir，手动建一个唯一目录
    const QString tmpPath = sandboxTemp + QLatin1String("/suite");
    QDir tmpDir(tmpPath);
    if (!tmpDir.mkpath(QLatin1String("."))) {
        printf("  FAIL 无法创建临时目录 %s\n", qPrintable(tmpPath));
        return 1;
    }

    tieba::TbImageCache cache;
    cache.setCacheDir(tmpPath);

    checkStr("default cache is persistent private home directory",
             tieba::TbImageCache::defaultCacheDir(),
             QDir::homePath() + QLatin1String("/bbtieba-img"));

    // Avatar endpoint aliases and response timestamps must share a disk key.
    // Preserve meaningful parameters and the portrait/portraith size distinction.
    {
        const QString avatarDir = sandboxTemp + QLatin1String("/avatars");
        QDir().mkpath(avatarDir);
        const QStringList paths = QStringList()
                << QLatin1String("/sys/portrait/item/fake-user")
                << QLatin1String("/sys/portraith/item/fake-user")
                << QLatin1String("/sys/portrait/item/fake-user?size=large");
        tieba::TbImageCache avatars;
        avatars.setCacheDir(avatarDir);
        for (int i = 0; i < paths.size(); ++i) {
            const QString canonical = QLatin1String("https://himg.bdimg.com") + paths.at(i);
            const QString filename = tieba::TbMd5::hex(canonical.toUtf8()) + QLatin1String(".img");
            const QString local = avatarDir + QLatin1Char('/') + filename;
            QFile fixture(local);
            check("write canonical avatar fixture", fixture.open(QIODevice::WriteOnly));
            fixture.write("avatar-fixture");
            fixture.close();
            const QString delimiter = paths.at(i).contains(QLatin1Char('?'))
                    ? QLatin1String("&t=") : QLatin1String("?t=");
            const QString legacy = QLatin1String("http://tb.himg.baidu.com")
                    + paths.at(i) + delimiter + QLatin1String("1");
            const QString current = canonical + delimiter + QLatin1String("2");
            checkStr("avatar normalization matches canonical URL",
                     tieba::TbString::httpsImageUrl(legacy), canonical);
            const QString first = avatars.pathFor(legacy);
            checkStr("legacy avatar alias hits canonical disk file", QUrl(first).toLocalFile(), local);
            checkStr("different avatar timestamp shares same cache", avatars.pathFor(current), first);
            tieba::TbImageCache reopened;
            reopened.setCacheDir(avatarDir);
            checkStr("avatar canonical cache survives fresh instance", reopened.pathFor(current), first);
            checkInt("fresh avatar instance avoids downloads", reopened.activeCount(), 0);
            checkInt("fresh avatar instance avoids queue", reopened.queuedCount(), 0);
        }
        checkInt("avatar aliases never download", avatars.activeCount(), 0);
        checkInt("avatar aliases never queue", avatars.queuedCount(), 0);
        checkInt("portrait size and meaningful query remain distinct", avatars.cachedCount(), 3);
        const QString imageUrl = QLatin1String("http://images.invalid/photo.png?t=7");
        const QString imageLocal = avatarDir + QLatin1Char('/')
                + tieba::TbMd5::hex(imageUrl.toUtf8()) + QLatin1String(".png");
        QFile photo(imageLocal);
        check("write original non-avatar cache fixture", photo.open(QIODevice::WriteOnly));
        photo.write("photo-fixture");
        photo.close();
        tieba::TbImageCache photos;
        photos.setCacheDir(avatarDir);
        checkStr("non-avatar scheme and timestamp retain original key",
                 QUrl(photos.pathFor(imageUrl)).toLocalFile(), imageLocal);
        checkInt("non-avatar cache hit does not download", photos.activeCount(), 0);
    }

    // No network: reconstructing the object models a fresh application launch.
    {
        const QString url = QLatin1String("https://offline.invalid/restart.png");
        const QString filename = tieba::TbMd5::hex(url.toUtf8()) + QLatin1String(".png");
        const QString fixturePath = tmpPath + QLatin1Char('/') + filename;
        QFile fixture(fixturePath);
        check("write disk cache fixture", fixture.open(QIODevice::WriteOnly));
        fixture.write("persistent-image-fixture");
        fixture.close();
        QString firstPath;
        {
            tieba::TbImageCache firstLaunch;
            firstLaunch.setCacheDir(tmpPath);
            firstPath = firstLaunch.pathFor(url);
            check("first launch reuses disk file", QUrl(firstPath).toLocalFile() == fixturePath);
            checkInt("disk hit does not download", firstLaunch.activeCount(), 0);
        }
        tieba::TbImageCache secondLaunch;
        secondLaunch.setCacheDir(tmpPath);
        checkStr("fresh instance reuses same disk file", secondLaunch.pathFor(url), firstPath);
        checkInt("fresh instance does not queue", secondLaunch.queuedCount(), 0);
        checkInt("fresh instance does not download", secondLaunch.activeCount(), 0);
        check("cache bytes survives restart", secondLaunch.cacheBytes() > 0);
        secondLaunch.setMaxCacheBytes(1);
        check("limit removes cached file", !QFile::exists(fixturePath));
        checkStr("evicted URL becomes download eligible", secondLaunch.pathFor(url), "");
        checkInt("evicted URL starts a new download", secondLaunch.activeCount(), 1);
    }

    /* ---- 本地路径直通（不该触发下载）---- */
    checkStr("asset:// 原样返回",
             cache.pathFor("asset:///images/x.png"), "asset:///images/x.png");
    checkStr("file:// 原样返回",
             cache.pathFor("file:///accounts/x.png"), "file:///accounts/x.png");
    checkStr("data: 原样返回",
             cache.pathFor("data:image/png;base64,AAAA"),
             "data:image/png;base64,AAAA");

    /* ---- 非法输入 ---- */
    checkStr("空串返回空", cache.pathFor(""), "");
    checkStr("空白返回空", cache.pathFor("   "), "");
    checkStr("其它 scheme 返回空", cache.pathFor("ftp://x/y.png"), "");

    /* ---- http/https：未缓存时返回空串并开始下载 ---- */
    const QString u1 = QString("http://example.com/a.jpg");
    checkStr("http 未缓存返回空", cache.pathFor(u1), "");
    checkStr("https 未缓存返回空", cache.pathFor("https://example.com/b.png"), "");

    // 已缓存的判定看内存表；同一次进程内重复请求不应再返回路径（仍在下载中）
    checkStr("下载中的 URL 再问仍返回空", cache.pathFor(u1), "");
    check("已缓存计数为 0", cache.cachedCount() == 0);

    /* ---- 预置一个"上次运行留下的文件"，应被直接复用 ---- */
    // 文件名算法与 TbImageCache 内部一致：md5(url) + 后缀
    // 这里换个思路：先把缓存目录指到一个新建目录，放一个同名文件进去。
    // 为不重复实现 md5 命名，直接用同一个 cache 实例让它算一遍：
    // 触发下载后不会立刻有文件，所以改为验证"目录能被创建"。
    check("缓存目录可创建", QDir(tmpPath).exists());

    /*
     * ---- 信号连接必须成功 ----
     *
     * 这条断言是为一个真实的 bug 加的：
     *   manager 的信号是 finished(QNetworkReply*)，reply 由**参数**给出。
     *   我一开始把槽写成 onFinished()（无参数）并用 sender() 取 reply，
     *   而 sender() 返回的是 manager，qobject_cast 失败后**静默 return**。
     *
     *   字符串版 connect 在**运行时**才解析签名，失败时只打一行
     *   "Object::connect: No such slot ..."，功能完全失效但不崩溃 ——
     *   表现就是"图片永远不加载，日志还一片空白"。
     *
     *   用带返回值的 connect 断言一句，这类错误就能在开发机上被拦住。
     */
    QObject probe;
    const bool connected = QObject::connect(
                &cache, SIGNAL(ready()), &probe, SLOT(deleteLater()));
    check("ready() 信号可连接（签名正确）", connected);

    // 这条直指那个真实 bug：内部 onFinished 的签名必须能被解析到。
    // 写错的话 isWired() 会是 false，下载永远不完成。
    check("内部 finished -> onFinished 连接有效", cache.isWired());

    /* ---- 本地缓存路径必须带 file://（真机踩过的坑）----
     *
     * 真机实测：把裸绝对路径 "/accounts/1000/.../x.jpg" 交给
     * ImageView.imageSource 时，Cascades 会当成 **asset 相对路径**，
     * 拼到 /apps/<appid>/native/assets/ 下面找，然后报：
     *     AssetPathResolver::resolveAssetPath: Did not find any asset ...
     *     Image not found: "/accounts/1000/..."
     * 必须加 file:// 才会按本地文件处理。
     *
     * 这里用真实下载验证（唯一能确认这条规则的可靠办法）。
     * 网络不可用时跳过，不让它变成假绿。
     */
    if (!app.arguments().contains(QLatin1String("--offline"))) {
        const QString url = QString("https://tiebapic.baidu.com/forum/w%3D580/"
                                    "sign=4ebe11eed48fa0ec7fc764051696594a/"
                                    "2834349b033b5bb5e584f32373d3d539b700bc98.jpg");
        const QString first = cache.pathFor(url);
        check("首次请求返回空（开始下载）", first.isEmpty());

        QEventLoop loop;
        QObject::connect(&cache, SIGNAL(ready()), &loop, SLOT(quit()));
        QTimer guard;
        guard.setSingleShot(true);
        QObject::connect(&guard, SIGNAL(timeout()), &loop, SLOT(quit()));
        guard.start(25000);
        loop.exec();

        const QString got = cache.pathFor(url);
        if (got.isEmpty()) {
            printf("  SKIP 网络不可用，跳过 file:// 校验\n");
        } else {
            check("缓存路径以 file:// 开头", got.startsWith(QLatin1String("file://")));
            checkStr("file:// 后是绝对路径",
                     got.mid(7, 1), QLatin1String("/"));
            /*
             * 还原成本地路径并确认文件真的在。
             *
             * 注意两种平台的形态：
             *   Unix/QNX : file:///accounts/x.jpg -> mid(7) = /accounts/x.jpg
             *   Windows  : file:///C:/Users/x.jpg -> mid(8) = C:/Users/x.jpg
             * 直接用 QUrl::toLocalFile() 最省事，也能顺带验证 URL 本身合法。
             */
            const QUrl gotUrl(got);
            const QString local = gotUrl.toLocalFile();
            check("file:// URL 合法", gotUrl.isValid());
            check("能还原出本地路径（toLocalFile 非空）", !local.isEmpty());
            check("对应文件确实存在", QFile::exists(local));
            check("文件非空", QFileInfo(local).size() > 0);
        }
    }

    /* ---- 并发限流（性能优化）----
     *
     * 一个贴的第一页有 30 个楼层，如果每层都有图，不限流就会同时发出
     * 30 个 HTTP 请求，把设备网络和 UI 都拖住 → "多图时卡"。
     * 这里验证：一次丢进去很多 URL，同时进行的下载数不超过上限。
     */
    {
        tieba::TbImageCache c3;
        c3.setCacheDir(tmpPath);
        c3.setMaxConcurrent(2);
        checkInt("并发上限可设置", c3.maxConcurrent(), 2);

        // 丢 10 个不同 URL（用不存在的域名，保证停在"下载中/排队"而不是秒完成）
        for (int i = 0; i < 10; ++i) {
            c3.pathFor(QString("https://img-not-exist-%1.invalid/x%1.jpg").arg(i));
        }
        check("并发数不超过上限", c3.activeCount() <= 2);
        check("有任务在排队", c3.queuedCount() > 0);
        checkInt("入队总数正确", c3.activeCount() + c3.queuedCount(), 10);

        // 同一个 URL 反复问不应该重复入队（列表滚动会反复问同一个 URL）
        const QString dup = QString("https://img-not-exist-dup.invalid/y.jpg");
        c3.pathFor(dup);
        const int before = c3.activeCount() + c3.queuedCount();
        c3.pathFor(dup);
        c3.pathFor(dup);
        checkInt("同一 URL 不重复入队", c3.activeCount() + c3.queuedCount(), before);

        // 调大上限应立即派发更多任务
        const int activeBefore = c3.activeCount();
        c3.setMaxConcurrent(6);
        check("调大上限后立即派发", c3.activeCount() > activeBefore);
        check("仍不超过新上限", c3.activeCount() <= 6);
    }

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
