/*
 * TbImageCache - 把网络图片下载到本地并缓存
 *
 * ── 为什么必须有这个东西 ──────────────────────────────────────────────────
 *
 * 实测（真机日志）：
 *     "Unsupported scheme (http)  used in url (...). Image loading aborted."
 *     "Unsupported scheme (https) used in url (...). Image loading aborted."
 *
 * 也就是说 BB10 的 Cascades 图片加载器**不支持任何网络方案**，
 * `ImageView.imageSource` 只能吃 `asset://` / `file://` 这类本地路径。
 * 设备上也没有 Qt 的 imageformats 插件来补这个能力。
 *
 * 官方 imageloader 示例的做法正是**在 C++ 里下载**，再把图片交给 QML。
 * 这个类就承担这件事：按 URL 下载 -> 存成持久缓存文件 -> 返回本地路径。
 *
 * ── 用法（QML 侧）─────────────────────────────────────────────────────────
 *     ImageView {
 *         imageSource: tieba.imagePath(ListItemData.firstImageUrl)
 *     }
 * 未下载完时返回空串（ImageView 就空着），下载完成后
 * TbImageCache 会发 ready()，controller 转发成 imageCacheChanged()，
 * QML 重新求值绑定就显示出来了。
 *
 * 缓存目录默认用应用私有 QDir::homePath() 下的子目录；测试可以用
 * setCacheDir() 指到别处。
 */

#ifndef TB_IMAGE_CACHE_HPP
#define TB_IMAGE_CACHE_HPP

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QStringList>

#include <QNetworkReply>
#include <QSslError>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

namespace tieba {

class TbImageCache : public QObject
{
    Q_OBJECT
public:
    explicit TbImageCache(QObject *parent = 0);
    virtual ~TbImageCache();

    /*!
     * 取某个 URL 的本地缓存路径。
     * 已缓存则直接返回本地路径；否则触发一次下载并返回空串。
     * 返回的路径可以直接给 ImageView.imageSource（Qt 认绝对路径）。
     */
    QString pathFor(const QString &url);

    /*! 应用私有持久目录，跨进程启动保留。 */
    static QString defaultCacheDir();

    /*! 覆盖缓存目录（测试用，首次访问前设置）。为空时用私有持久目录。 */
    void setCacheDir(const QString &dir, const QString &legacyDir = QString())
    { m_cacheDir = dir; m_legacyCacheDir = legacyDir; }
    /*!
     * 实际生效的缓存目录。
     * 没显式 setCacheDir 时使用 QDir::homePath()/bbtieba-img。
     */
    QString cacheDir() const { return m_effectiveDir.isEmpty()
            ? (m_cacheDir.isEmpty() ? defaultCacheDir() : m_cacheDir) : m_effectiveDir; }

    /*! 已缓存的文件数（排查用） */
    int cachedCount() const { return m_done.size(); }
    /*! 正在下载的数量（排查/调优用） */
    int activeCount() const { return m_active.size(); }
    /*! 排队等待下载的数量（排查/调优用） */
    int queuedCount() const { return m_queue.size(); }

    /*!
     * 同时下载的最大数量。
     *
     * ★ 为什么要限流（性能问题的根源之一）：
     *   一个贴的第一页有 30 个楼层，如果每层都有图，
     *   不限流就会**同时**发出 30 个 HTTP 请求。
     *   在 BB10 这种设备上会把网络和 UI 线程都拖住，表现为"加载时卡"。
     *   默认 4 个并行，其余排队，体感明显更顺。
     */
    void setMaxConcurrent(int n);
    int maxConcurrent() const { return m_maxConcurrent; }

    /*! 设置下载超时（毫秒） */
    void setTimeout(int ms) { m_timeoutMs = ms; }

    /* ==================================================================
     *  缓存占用（设置页的"缓存上限 / 已用多少"用）
     * ================================================================== */

    /*!
     * 缓存目录里所有文件的总字节数。
     *
     * 第一次调用扫一遍目录，之后靠写入/删除增量维护；
     * 想强制重新扫就调 invalidateCacheBytes()。
     */
    qint64 cacheBytes() const;
    /*! 丢弃已算好的占用数，下次 cacheBytes() 重新扫目录 */
    void invalidateCacheBytes() { m_cacheBytes = -1; }

    /*!
     * 缓存上限（字节）。<= 0 表示**不限制**（也是默认值行为）。
     * 设了之后每下载完一张图都会检查，超了就按"最久没用过的先删"回收。
     */
    void setMaxCacheBytes(qint64 bytes);
    qint64 maxCacheBytes() const { return m_maxCacheBytes; }

    /*!
     * 清空图片缓存（设置页"清除缓存"）。
     * 磁盘文件全删，内存表也一起清 —— 否则 pathFor() 会返回已经不存在的路径。
     */
    void clearCache();

    /*!
     * 打开详细日志。默认关闭（免得每张图都刷屏）；
     * 排查图片问题时打开，能看到每一步的进展与失败原因。
     */
    void setVerbose(bool on) { m_verbose = on; }
    bool isVerbose() const { return m_verbose; }

    /*!
     * 自检：内部信号连接是否有效（签名是否解析得到）。
     *
     * 字符串版 connect 到运行时才解析签名，写错只会打一行
     * "Object::connect: No such slot ..." 然后静默失效。测试里断言一下
     * 就能在开发机上拦住这类"功能全废但不崩溃"的问题。
     */
    bool isWired() const;

signals:
    /*! 有新图片下载完成，QML 应重新求值绑定 */
    void ready();

private slots:
    /*!
     * 下载完成。
     *
     * ★ 必须带 QNetworkReply* 参数：
     *   manager 的信号是 finished(QNetworkReply*)，reply 由**参数**给出。
     *   如果槽不接参数、改用 sender() 取，拿到的是 **manager**（信号发出者），
     *   qobject_cast 会失败并静默 return —— 表现就是"图片永远不出现，
     *   而且一行日志都没有"。这一点踩过，别改回去。
     */
    void onFinished(QNetworkReply *reply);
    /*! 下载超时：把 URL 从"下载中"摘掉，下次请求会重试 */
    void onTimeout();
    /*! 网络层错误（DNS/TLS/连接被拒等），单独打日志便于定位 */
    void onReplyError(QNetworkReply::NetworkError code);
    /*! SSL 握手错误：打日志 + 忽略（见 .cpp 里的说明） */
    void onReplySslErrors(const QList<QSslError> &errors);

private:
    QString ensureCacheDir() const;
    static QString fileNameFor(const QString &url);
    /*! 真正发起一个下载（pathFor 只负责入队，由这里按并发上限派发） */
    void startDownload(const QString &url);
    /*! 从队列里取任务补足并发额度 */
    void pumpQueue();
    /*! 重新扫目录算出占用（m_cacheBytes < 0 时才真扫，见 cacheBytes） */
    void recomputeCacheBytes() const;
    /*! 超上限时按"最久没用过的先删"回收，直到回到上限以内 */
    void enforceCacheLimit();
    /*! 删掉某个缓存文件，并同步占用计数与 m_done（文件已不在了就别再返回它） */
    void removeCachedFile(const QString &path);

    QNetworkAccessManager *m_manager;
    QString m_cacheDir;
    QString m_legacyCacheDir;
    int m_timeoutMs;
    /*! 最多同时下载几个（见 setMaxConcurrent 的说明） */
    int m_maxConcurrent;
    /*!
     * url -> 本地路径（已完成）。
     *
     * 这个表同时充当**内存缓存**：pathFor() 每被调用一次都命中它就直接返回，
     * 不会重复算 MD5、也不会重复 stat 文件。
     * （之前每次调用都重算 MD5 + QFileInfo，一页几十个楼层就是几十次系统调用。）
     */
    QHash<QString, QString> m_done;
    /*! 正在下载的 url -> 它对应的 reply（用于超时/失败时清理） */
    QHash<QString, QNetworkReply *> m_active;
    /*! 排队等待下载的 url（先进先出） */
    QStringList m_queue;
    /*!
     * 查过但**还没拿到本地路径**的 url。
     * 用来避免同一个 URL 被重复入队（列表滚动会反复问同一个 URL）。
     */
    QSet<QString> m_requested;
    /*! 缓存上限（字节，<=0 表示不限制） */
    qint64 m_maxCacheBytes;
    /*!
     * 已算好的缓存占用（字节）。**-1 表示还没算过**，见 cacheBytes()。
     * mutable：cacheBytes() 是 const，但第一次要扫目录填它。
     */
    mutable qint64 m_cacheBytes;
    /*! 详细日志开关（排查图片问题时打开） */
    bool m_verbose;
    /*! 实际生效的缓存目录（mutable：ensureCacheDir 是 const） */
    mutable QString m_effectiveDir;
    /*! 内部信号连接是否有效（构造时算好，见 isWired() 的说明） */
    bool m_wired;
};

} // namespace tieba

#endif // TB_IMAGE_CACHE_HPP
