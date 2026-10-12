#include "TbImageCache.hpp"

#include "util/TbMd5.hpp"
#include "util/TbString.hpp"
#include "util/TbTr.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegExp>
#include <QTimer>
#include <QUrl>

#include <stdlib.h>

namespace tieba {

namespace {

/*!
 * 把本地绝对路径转成 file:// URL。
 *
 * ★ 为什么必须这样转（真机实测）：
 *   如果直接把 "/accounts/1000/appdata/.../x.jpg" 交给 ImageView.imageSource，
 *   Cascades 会把它当成**asset 相对路径**，拼到
 *       /apps/<appid>/native/assets/
 *   下面去找，然后报：
 *       AssetPathResolver::resolveAssetPath: Did not find any asset for
 *         basePath=/apps/<appid>/native/assets/ ,
 *         filePath=accounts/1000/appdata/.../x.jpg
 *       Image not found: "/accounts/1000/..."
 *   加上 file:// 前缀后，加载器才会按本地文件处理。
 */
QString toFileUrl(const QString &absPath)
{
    if (absPath.isEmpty())
        return absPath;
    // 已经带 scheme（file:// / asset:// / http:// ...）就原样返回
    if (absPath.contains(QLatin1String("://")))
        return absPath;

    // 情况一：Unix/QNX 绝对路径 —— /accounts/1000/.../x.jpg
    //         file:// + 绝对路径 = file:///accounts/...（三斜杠是对的）
    if (absPath.startsWith(QLatin1Char('/')))
        return QLatin1String("file://") + absPath;

    // 情况二：Windows 盘符路径 —— C:/Users/.../x.jpg
    //         这种**不以 / 开头**，需要 file:/// 前缀。
    //         （本机测试会走到这里；真机不会，但不能因此漏掉 ——
    //          我第一版只判断了 startsWith('/')，本机测出来就没加前缀。）
    if (absPath.length() >= 2 && absPath.at(1) == QLatin1Char(':'))
        return QLatin1String("file:///") + absPath;

    // 既不是绝对路径也不是盘符路径：原样返回，交给调用方判断
    return absPath;
}

/*! 从 URL 猜一个后缀，ImageView 主要看内容，后缀只是方便排查 */
QString suffixFor(const QString &url)
{
    const int dot = url.lastIndexOf(QLatin1Char('.'));
    if (dot < 0)
        return QLatin1String(".img");
    QString ext = url.mid(dot);
    const int q = ext.indexOf(QLatin1Char('?'));
    if (q >= 0)
        ext = ext.left(q);
    // 只接受常见图片后缀，其余一律 .img（避免怪字符进文件名）
    if (ext.compare(QLatin1String(".jpg"), Qt::CaseInsensitive) == 0
            || ext.compare(QLatin1String(".jpeg"), Qt::CaseInsensitive) == 0
            || ext.compare(QLatin1String(".png"), Qt::CaseInsensitive) == 0
            || ext.compare(QLatin1String(".gif"), Qt::CaseInsensitive) == 0
            || ext.compare(QLatin1String(".webp"), Qt::CaseInsensitive) == 0
            || ext.compare(QLatin1String(".bmp"), Qt::CaseInsensitive) == 0)
        return ext.toLower();
    return QLatin1String(".img");
}

} // anonymous namespace

TbImageCache::TbImageCache(QObject *parent)
    : QObject(parent)
    , m_manager(new QNetworkAccessManager(this))
    , m_timeoutMs(30000)
    , m_maxConcurrent(4)
    , m_maxCacheBytes(0)
    , m_cacheBytes(-1)
    , m_verbose(false)
    , m_wired(false)
{
    // 图片下载不需要 cookie jar，贴吧图床也不看
    m_manager->setCookieJar(0);

    /*
     * reply 由信号参数带过来。
     *
     * ★ 槽必须带 QNetworkReply* 参数，且这里要接住 connect 的返回值：
     *   字符串版 connect 只在**运行时**解析签名，写错时只打一行
     *       "Object::connect: No such slot ..."
     *   然后功能完全失效 —— 不崩溃、不报错，表现是"图片永远不加载"。
     *   我踩过一次（槽写成无参数的 onFinished()，想用 sender() 取 reply，
     *   而 sender() 返回的是 manager）。所以把结果存下来供测试断言。
     */
    m_wired = connect(m_manager, SIGNAL(finished(QNetworkReply *)),
                      this, SLOT(onFinished(QNetworkReply *)));
}

void TbImageCache::setMaxConcurrent(int n)
{
    m_maxConcurrent = (n < 1) ? 1 : n;
    pumpQueue();
}

TbImageCache::~TbImageCache()
{
}

/*!
 * 自检：manager 的 finished(QNetworkReply*) 是否成功连到了我们的槽。
 *
 * 结果在构造时就算好了（见构造函数里的说明）。
 * 字符串版 connect 到运行时才解析签名，写错只会打一行
 *   "Object::connect: No such slot ..."
 * 然后功能静默失效 —— 不崩溃、不报错，表现是"图片永远不加载，日志还空白"。
 * 暴露出来让测试断言一句，就能在开发机上拦住这类错误。
 */
bool TbImageCache::isWired() const
{
    return m_wired;
}

QString TbImageCache::ensureCacheDir() const
{
    if (!m_effectiveDir.isEmpty())
        return m_effectiveDir;
    QString base = m_cacheDir;
    if (base.isEmpty())
        base = defaultCacheDir();
    QDir dir(base);
    if (!dir.exists() && !dir.mkpath(QLatin1String(".")))
        return QString();

    // 与网易云音乐相同：临时目录会被 BB10 清理，私有 home 目录才能跨启动复用。
    // 升级时迁移尚未被清理的旧图片；只处理本类生成的 MD5 文件名。
    if (m_cacheDir.isEmpty() || !m_legacyCacheDir.isEmpty()) {
        const QString legacy = m_legacyCacheDir.isEmpty()
                ? QDir::tempPath() + QLatin1String("/bbtieba-img") : m_legacyCacheDir;
        if (QDir(legacy).absolutePath() != dir.absolutePath()) {
            const QFileInfoList files = QDir(legacy).entryInfoList(QDir::Files);
            const QRegExp cacheName(QLatin1String("[0-9a-f]{32}\\.(jpg|jpeg|png|gif|webp|bmp|img)"));
            int migrated = 0;
            for (int i = 0; i < files.size(); ++i) {
                const QFileInfo &old = files.at(i);
                if (!cacheName.exactMatch(old.fileName()) || old.size() <= 0)
                    continue;
                const QString target = dir.absoluteFilePath(old.fileName());
                // 持久目录已有的有效文件优先；移除旧副本避免清缓存后又迁回来。
                if (QFileInfo(target).size() > 0) {
                    QFile::remove(old.absoluteFilePath());
                } else {
                    QFile::remove(target);
                    if (QFile::copy(old.absoluteFilePath(), target)) {
                        QFile::remove(old.absoluteFilePath());
                        ++migrated;
                    }
                }
            }
            if (migrated > 0)
                qWarning("TbImageCache: migrated %d images to persistent cache", migrated);
        }
    }
    // 记下实际生效的目录，imageDiag() 才能打印出真实路径
    // （之前只读 m_cacheDir，没显式设置时打印出来是空的，排查时很误导）
    m_effectiveDir = base;
    return base;
}

QString TbImageCache::defaultCacheDir()
{
    return QDir::homePath() + QLatin1String("/bbtieba-img");
}

/* ==================== 缓存占用 / 上限 ==================== */

qint64 TbImageCache::cacheBytes() const
{
    if (m_cacheBytes < 0)
        recomputeCacheBytes();
    return m_cacheBytes;
}

void TbImageCache::recomputeCacheBytes() const
{
    qint64 total = 0;
    const QString dir = ensureCacheDir();
    if (!dir.isEmpty()) {
        const QFileInfoList list = QDir(dir).entryInfoList(QDir::Files);
        for (int i = 0; i < list.size(); ++i)
            total += list.at(i).size();
    }
    m_cacheBytes = total;
}

void TbImageCache::setMaxCacheBytes(qint64 bytes)
{
    m_maxCacheBytes = bytes;
    if (m_verbose)
        qWarning("TbImageCache: 缓存上限设为 %s 字节",
                 qPrintable(QString::number(bytes)));
    enforceCacheLimit();
}

/*! 删掉某个缓存文件：同步占用计数，并把它从 m_done 里摘掉 */
void TbImageCache::removeCachedFile(const QString &path)
{
    QFileInfo info(path);
    const qint64 size = info.size();

    if (!QFile::remove(path))
        return;

    if (m_cacheBytes >= 0)
        m_cacheBytes = (m_cacheBytes > size) ? (m_cacheBytes - size) : 0;

    /*
     * ★ 必须同步 m_done：它记的是 url -> 路径。
     *   文件删了但表还在，pathFor() 就会返回一个**已经不存在**的路径，
     *   界面上是"图片永远空白"（下一次运行也不会重新下载，因为路径在表里）。
     */
    QStringList deadUrls;
    QHash<QString, QString>::iterator it = m_done.begin();
    for (; it != m_done.end(); ++it) {
        if (it.value() == path)
            deadUrls.append(it.key());
    }
    for (int i = 0; i < deadUrls.size(); ++i)
    {
        m_done.remove(deadUrls.at(i));
        m_requested.remove(deadUrls.at(i));
    }
}

void TbImageCache::enforceCacheLimit()
{
    if (m_maxCacheBytes <= 0)
        return;

    const QString dir = ensureCacheDir();
    if (dir.isEmpty())
        return;

    if (m_cacheBytes > m_maxCacheBytes || m_cacheBytes < 0)
        recomputeCacheBytes();
    if (m_cacheBytes <= m_maxCacheBytes)
        return;

    /*
     * 按修改时间**升序**（最久没动过的在最前面）回收。
     * QDir::Time 是按 mtime 排序，Reversed 之后就是 oldest-first。
     */
    QFileInfoList list =
        QDir(dir).entryInfoList(QDir::Files, QDir::Time | QDir::Reversed);

    int removed = 0;
    for (int i = 0; i < list.size() && m_cacheBytes > m_maxCacheBytes; ++i) {
        const QString path = list.at(i).absoluteFilePath();
        removeCachedFile(path);
        ++removed;
    }

    qWarning("TbImageCache: 缓存超过上限(%s 字节)，已回收 %d 个文件，剩余 %s 字节",
             qPrintable(QString::number(m_maxCacheBytes)), removed,
             qPrintable(QString::number(m_cacheBytes)));
}

void TbImageCache::clearCache()
{
    const QString dir = ensureCacheDir();
    if (!dir.isEmpty()) {
        const QFileInfoList list = QDir(dir).entryInfoList(QDir::Files);
        for (int i = 0; i < list.size(); ++i)
            QFile::remove(list.at(i).absoluteFilePath());
    }

    /*
     * 内存表一起清。在途的下载不动：它们写完会重新进 m_done
     * （m_requested 也保留，避免同一个 URL 在下载中被重复入队）。
     */
    m_done.clear();
    m_cacheBytes = 0;
}

QString TbImageCache::fileNameFor(const QString &url)
{
    // MD5 当文件名：URL 里有 ? 和 % 之类，直接用不安全
    const QByteArray hash = TbMd5::hex(url.toUtf8()).toLatin1();
    return QString::fromLatin1(hash) + suffixFor(url);
}

QString TbImageCache::pathFor(const QString &sourceUrl)
{
    // Normalize here as well so old saved account/forum snapshots do not keep
    // requesting the retired host, or use different keys from freshly parsed data.
    const QString original = sourceUrl.trimmed();
    const QString normalized = TbString::httpsImageUrl(original);
    const QUrl normalizedUrl(normalized);
    const bool isAvatar = normalizedUrl.host() == QLatin1String("himg.bdimg.com")
        && (normalizedUrl.path().startsWith(QLatin1String("/sys/portrait/item/"))
            || normalizedUrl.path().startsWith(QLatin1String("/sys/portraitn/item/"))
            || normalizedUrl.path().startsWith(QLatin1String("/sys/portraith/item/")));
    const QString url = isAvatar ? normalized : original;
    if (url.trimmed().isEmpty())
        return QString();

    // 本来就是本地路径（asset:// / file:// / 相对路径）直接交给 ImageView
    if (url.startsWith(QLatin1String("asset://"))
            || url.startsWith(QLatin1String("file://"))
            || url.startsWith(QLatin1String("data:")))
        return url;

    // 只处理 http/https，其他 scheme 不碰
    if (!url.startsWith(QLatin1String("http://"))
            && !url.startsWith(QLatin1String("https://")))
        return QString();

    /*
     * 已缓存 —— 这条通常是**最热的路径**：
     * 列表滚动、模型刷新都会反复问同一个 URL。
     * 命中 m_done 就直接返回，不重复算 MD5、不重复 stat 文件。
     */
    if (m_done.contains(url))
        return toFileUrl(m_done.value(url));

    // 已经在下载 / 已排队：不要重复入队，直接返回空
    if (m_requested.contains(url))
        return QString();

    const QString dir = ensureCacheDir();
    if (dir.isEmpty()) {
        qWarning("TbImageCache: 缓存目录不可用，无法下载");
        return QString();
    }

    const QString path = dir + QLatin1Char('/') + fileNameFor(url);

    // 上次运行留下的文件：直接复用，不用重新下（并进内存缓存，下次不再 stat）
    if (QFileInfo(path).exists() && QFileInfo(path).size() > 0) {
        m_done.insert(url, path);
        if (m_verbose)
            qWarning("TbImageCache: 复用已有文件 %s", qPrintable(path));
        return toFileUrl(path);
    }

    // 记下来，避免同一个 URL 被反复入队（列表滚动会反复问）
    m_requested.insert(url);
    m_queue.append(url);
    if (m_verbose)
        qWarning("TbImageCache: 入队 url=%s", qPrintable(url.left(120)));

    pumpQueue();
    return QString();
}

/*!
 * 从队列里取任务补足并发额度。
 *
 * ★ 这是性能优化的核心之一。
 *   一页 30 个楼层每层都有图时，不限流就会**同时**发 30 个请求，
 *   把设备网络和 UI 都拖住，表现为"加载时卡"。
 *   限制成几个并行、其余排队，体感明显更顺。
 */
void TbImageCache::pumpQueue()
{
    while (!m_queue.isEmpty() && m_active.size() < m_maxConcurrent)
        startDownload(m_queue.takeFirst());
}

/*! 真正发起一个下载（由 pumpQueue 按并发额度派发） */
void TbImageCache::startDownload(const QString &url)
{
    const QString dir = ensureCacheDir();
    if (dir.isEmpty()) {
        m_requested.remove(url);
        return;
    }

    const QString path = dir + QLatin1Char('/') + fileNameFor(url);

    // 排队期间可能已经被别的路径写好了（例如上一次失败重试），再确认一次
    if (QFileInfo(path).exists() && QFileInfo(path).size() > 0) {
        m_done.insert(url, path);
        m_requested.remove(url);
        emit ready();
        return;
    }

    QNetworkRequest request((QUrl(url)));
    request.setRawHeader("User-Agent",
                         "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                         "AppleWebKit/537.36 (KHTML, like Gecko) "
                         "Chrome/120.0.0.0 Safari/537.36");
    request.setRawHeader("Accept", "image/*,*/*;q=0.8");
    request.setRawHeader("Referer", "https://tieba.baidu.com/");
    // 图片不压缩，省得自己解压（服务端一般也不压图片）
    request.setRawHeader("Accept-Encoding", "identity");

    QNetworkReply *reply = m_manager->get(request);
    if (!reply) {
        qWarning("TbImageCache: m_manager->get() 返回空，下载无法开始");
        m_requested.remove(url);
        return;
    }

    // 把 url / 目标路径挂在 reply 上，onFinished 里取回来。
    // （用动态属性最简单，属性 key 用字符串前缀避免和 Qt 内部冲突）
    reply->setProperty("tbUrl", url);
    reply->setProperty("tbPath", path);

    m_active.insert(url, reply);

    if (m_verbose)
        qWarning("TbImageCache: 开始下载(并行 %d/%d) -> %s",
                 m_active.size(), m_maxConcurrent, qPrintable(path));

    // 网络层错误单独监听：有些失败不会触发 finished 的"成功"分支，
    // 但一定会触发 error 信号，这行日志能直接看出 DNS/TLS/连接问题
    connect(reply, SIGNAL(error(QNetworkReply::NetworkError)),
            this, SLOT(onReplyError(QNetworkReply::NetworkError)));

    /*
     * ★★ SSL 错误处理（真机实测踩到的坑）
     *
     * 现象：表情图 image_emoticon66.png 下载失败
     *         TbImageCache: 网络层错误 code=6 url=.../image_emoticon66.png
     *       code=6 是 QNetworkReply::SslHandshakeFailedError。
     *       同一个域名在开发机上用 Python 请求是 200，说明是**设备侧的证书校验**问题
     *       （BB10 的根证书库较旧 / 缺中间证书），不是域名或路径错。
     *
     * 处理：连 sslErrors，把**错误内容打进日志**，然后 ignoreSslErrors()。
     *
     * ⚠ 安全性说明：忽略 SSL 错误会降低中间人攻击的防护。
     *   这里的取舍是：拉的是**公开的贴吧表情图和帖子配图**，不含账号凭据，
     *   而 BB10 的证书库无法更新 —— 不忽略的话这类图就永远显示不出来。
     *   **登录/发帖那类带凭据的请求走的是 TbHttpClient（那边没有忽略）**，
     *   所以账号安全不受影响。
     */
    connect(reply, SIGNAL(sslErrors(QList<QSslError>)),
            this, SLOT(onReplySslErrors(QList<QSslError>)));

    /*
     * 超时保护。
     *
     * 注意不能用 connect(timer, SIGNAL(timeout()), reply, SLOT(abort()))：
     * 这个 Qt 版本里 QNetworkReply::abort() 不是槽，连接会在运行时失败并打
     *   "Object::connect: No such slot QNetworkReplyImpl::abort()"
     * 所以这里让超时信号回到自己身上，显式处理。
     */
    QTimer *timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setProperty("tbUrl", url);
    connect(timer, SIGNAL(timeout()), this, SLOT(onTimeout()));

    // reply 结束时把它的定时器停掉并销毁，避免堆积
    connect(reply, SIGNAL(finished()), timer, SLOT(stop()));
    connect(reply, SIGNAL(finished()), timer, SLOT(deleteLater()));
    timer->start(m_timeoutMs);
}

void TbImageCache::onReplyError(QNetworkReply::NetworkError code)
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    const QString url = reply ? reply->property("tbUrl").toString() : QString();
    qWarning("TbImageCache: 网络层错误 code=%d url=%s",
             (int)code, qPrintable(url.left(120)));
}

/*!
 * SSL 握手错误 —— 把**具体错误**打进日志，然后忽略掉继续下载。
 *
 * 为什么需要：真机上表情图/部分配图会因为证书校验失败而下载不到
 * （code=6 SslHandshakeFailedError），但同一 URL 在别处是 200。
 * 打日志是为了**留下证据**（到底是证书过期、还是主机名不匹配），
 * 忽略则是为了 BB10 无法更新根证书这个现实。
 *
 * 安全性取舍见构造下载请求那里的注释。
 */
void TbImageCache::onReplySslErrors(const QList<QSslError> &errors)
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    const QString url = reply ? reply->property("tbUrl").toString() : QString();

    for (int i = 0; i < errors.size(); ++i) {
        qWarning("TbImageCache: SSL 错误(%d/%d) %s  url=%s",
                 i + 1, errors.size(),
                 qPrintable(errors.at(i).errorString()),
                 qPrintable(url.left(100)));
    }

    if (reply) {
        // 只对图片放宽（这类请求不带凭据）；带凭据的请求不走这里
        reply->ignoreSslErrors();
        qWarning("TbImageCache: 已忽略该图的 SSL 错误，继续下载");
    }
}

void TbImageCache::onTimeout()
{
    QTimer *timer = qobject_cast<QTimer *>(sender());
    if (!timer)
        return;
    const QString url = timer->property("tbUrl").toString();
    timer->deleteLater();

    // 把对应 URL 从"下载中"里摘掉，下次再请求时会重新发起
    if (!url.isEmpty()) {
        QNetworkReply *reply = m_active.take(url);
        if (reply) {
            reply->abort();
            reply->deleteLater();
        }
        m_requested.remove(url);
        qWarning("TbImageCache: 下载超时 url=%s", qPrintable(url.left(120)));
    }

    // 超时占用的并发额度要立刻释放，否则后面排队的图全被拖住
    pumpQueue();
}

void TbImageCache::onFinished(QNetworkReply *reply)
{
    // reply 由 manager 的 finished(QNetworkReply*) 信号直接给出。
    // 不要用 sender()：那是 manager，不是 reply。
    if (!reply) {
        qWarning("TbImageCache: finished 信号的 reply 为空");
        return;
    }

    const QString url = reply->property("tbUrl").toString();
    const QString path = reply->property("tbPath").toString();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError netError = reply->error();
    const QByteArray body = reply->readAll();
    const bool ok = (netError == QNetworkReply::NoError)
                    && (status == 200 || status == 0)
                    && !body.isEmpty();
    reply->deleteLater();

    // 释放这个 URL 占用的并发额度
    m_active.remove(url);

    if (m_verbose) {
        qWarning("TbImageCache: 完成 status=%d err=%d bytes=%d (还在下 %d, 排队 %d)",
                 status, (int)netError, body.size(), m_active.size(), m_queue.size());
    }

    if (!ok || path.isEmpty()) {
        qWarning("TbImageCache: 下载失败 url=%s status=%d err=%d",
                 qPrintable(url.left(120)), status, (int)netError);
        // 失败的也把标记清掉，避免这个 URL 永远不再重试
        m_requested.remove(url);
        pumpQueue();
        return;
    }

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning("TbImageCache: 无法写入 %s", qPrintable(path));
        m_requested.remove(url);
        pumpQueue();
        return;
    }
    f.write(body);
    f.close();

    m_done.insert(url, path);
    // 已完成的只留在 m_done；回收/清理之后必须能够重新下载。
    m_requested.remove(url);
    emit ready();

    // 占用计数（只有已经算过总数时才增量维护，不然下次会自己扫一遍）
    if (m_cacheBytes >= 0)
        m_cacheBytes += body.size();
    // 超上限就回收最久没用过的（见 enforceCacheLimit 的说明）
    enforceCacheLimit();

    // ★ 腾出的额度马上派给排队的任务
    pumpQueue();
}

} // namespace tieba
