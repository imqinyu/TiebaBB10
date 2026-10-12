/*
 * TbSession - 登录态与本地设置
 *
 * 第一阶段的登录方式：用户手动粘贴 BDUSS / stoken。
 * 这样做的好处是完全不依赖验证码、RSA 加密、WebView 抓 cookie 那套东西，
 * 后续如果要加内嵌 WebView 登录，只需在上层把抓到的 BDUSS 交给 setCredentials()。
 *
 * 存储用 QSettings。真机上 BB10 会落到应用沙箱里的配置文件，
 * 注意 BDUSS 是明文保存的——这是用户自己选择的方案，README 里有说明。
 *
 * ======================================================================
 *  多账号（2026-10 加）
 *
 *  一个账号 = 一份 {BDUSS, STOKEN} + 已知的用户资料。存成一个 QVariantMap 列表：
 *
 *      QSettings 键            含义
 *      accounts/list           账号列表（每项见下）
 *      accounts/current        当前账号的 id（空 = 没在用任何账号）
 *      session 组下各键        当前账号的展开字段（老代码还在读，保留）
 *
 *  每项字段：
 *      id        QString  账号 id（**本地生成**，见 accountIdFor 的说明）
 *      bduss     QString  明文 BDUSS（★ 只在这份列表里，不进 QML）
 *      stoken    QString  明文 STOKEN
 *      uid       QString  百度 uid（还没拉到资料时是空）
 *      userName  QString  用户名（name）
 *      nickName  QString  展示用昵称（name_show，没有则回落 name）
 *      portrait  QString  头像 id（拼 URL 用 TbString::avatarUrl）
 *      tbs       QString  最近一次拿到的一次性票据
 *
 *  ★★ 两条安全约定（照搬 NeteaseMusic 那套，别破例）：
 *    1. accounts() / otherAccounts() / currentAccount() 一律**去掉明文**，
 *       只给打码后的 masked；真要明文走 credential(id)（复制凭据那一下）。
 *    2. 明文不进日志、不进模型、不进 QML 变量。
 * ======================================================================
 */

#ifndef TB_SESSION_HPP
#define TB_SESSION_HPP

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class QSettings;

namespace tieba {

class TbSession : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool loggedIn READ isLoggedIn NOTIFY changed)
    Q_PROPERTY(QString bduss READ bduss NOTIFY changed)
    Q_PROPERTY(QString stoken READ stoken NOTIFY changed)
    Q_PROPERTY(QString uid READ uid NOTIFY changed)
    Q_PROPERTY(QString userName READ userName NOTIFY changed)
    Q_PROPERTY(QString nickName READ nickName NOTIFY changed)
    Q_PROPERTY(QString portrait READ portrait NOTIFY changed)
    Q_PROPERTY(QString tbs READ tbs WRITE setTbs NOTIFY changed)

public:
    explicit TbSession(QObject *parent = 0);
    virtual ~TbSession();

    /*! 是否已具备可用登录态（有 BDUSS 即视为已登录） */
    bool isLoggedIn() const;

    QString bduss() const { return m_bduss; }
    QString stoken() const { return m_stoken; }
    QString uid() const { return m_uid; }
    QString userName() const { return m_userName; }
    QString nickName() const { return m_nickName; }
    QString portrait() const { return m_portrait; }
    /*! tbs 是发帖/签到/关注的校验票据，从吧页或贴子页响应里的 anti.tbs 取 */
    QString tbs() const { return m_tbs; }

    /*! 本次运行内是否需要重新拉取用户资料 */
    bool needsProfileRefresh() const { return m_needsProfileRefresh; }
    void setNeedsProfileRefresh(bool value);

    /* ---- 修改（都会持久化并 emit changed） ---- */

    /*!
     * 设置登录凭证。bduss 可以是用户直接粘贴的一整段 cookie，
     * 这里会尝试从中解析出 BDUSS= 与 STOKEN= 字段。
     */
    Q_INVOKABLE bool setCredentials(const QString &bduss, const QString &stoken = QString());

    /*! 更新用户资料（登录后由 /c/u/user/profile 或 initNickname 填充） */
    void setProfile(const QString &uid, const QString &userName,
                    const QString &nickName, const QString &portrait);

    void setTbs(const QString &tbs);

    /*! 清空登录态 */
    Q_INVOKABLE void logout();

    /*! 从磁盘读取 */
    void load();

    /*!
     * 启动时调用：读取用户保存的本地登录态。没有保存账号时保持未登录。
     */
    void init();

    /*! 写入磁盘 */
    void save() const;

    /* ==================== 测试钩子 ==================== */

    /*!
     * 把 QSettings 指到指定的 ini 文件（空串 = 回到应用默认位置）。
     *
     * ★ 为什么需要它：默认位置存着**用户真实的 BDUSS**，测试要是往那儿
     *   写，会把开发机上已登录的账号冲掉。所以测试必须能改道。
     *   生产代码不会调它。
     */
    void setSettingsFileForTest(const QString &path);

    /*!
     * 从一段 cookie 文本里抽出某个字段。
     * 支持 "BDUSS=xxx; STOKEN=yyy" 以及 "BDUSS=xxx" 单值两种输入。
     */
    static QString extractCookieValue(const QString &text, const QString &name);

    /*! 判断一段文本看起来是否像 BDUSS（长度足够且字符集合法） */
    static bool looksLikeBduss(const QString &value);
    /*! 从裸值、Cookie 或表单中提取并校验 BDUSS。 */
    static QString normalizeBdussInput(const QString &text);

    /* ==================== 多账号 ==================== */

    /*!
     * 账号 id：**由 BDUSS 本地算出来**（"b" + md5(bduss) 前 12 位）。
     *
     * ★ 为什么不用 uid 当 id：
     *   uid 要联网拉到资料才有。而"粘贴完立刻要能存、能切、能去重"，
     *   这一步不能依赖网络 —— 断网时粘贴的账号也得进列表。
     *   用 BDUSS 算 id 还顺带解决去重：同一份 BDUSS 贴两次只会有一条记录
     *   （md5 相同 → id 相同）。
     */
    static QString accountIdFor(const QString &bduss);

    /*! 打码后的凭证（"00AC6E94…4D5A"），界面上显示用 */
    static QString maskCredential(const QString &bduss);

    /*!
     * 全部账号（**不含明文**），字段：
     *   id / uid / userName / nickName / portrait / masked / isCurrent
     */
    QVariantList accounts() const;
    /*! 除当前账号以外的已保存账号（账号管理页的「已登录」一段） */
    QVariantList otherAccounts() const;
    /*! 当前账号（未登录时是空 map） */
    QVariantMap currentAccount() const;
    /*! 当前账号的 id */
    QString currentAccountId() const { return m_currentId; }
    int accountCount() const { return m_accounts.size(); }

    /*!
     * 取某个账号的**明文** BDUSS。
     * ★ 只有"复制凭据"这一个用途，取完立刻给剪贴板，不进模型不进日志。
     */
    QString credential(const QString &id) const;

    /*! 切换到某个已保存账号（换掉当前凭证与资料）；成功返回 true */
    bool switchTo(const QString &id);

    /*!
     * 删除账号。
     * ★ 删的正是当前在用的那个时，会**连带清掉登录态** ——
     *   否则手里还攥着一份已经删了的凭证继续发请求。
     */
    bool removeAccount(const QString &id);

    /*!
     * 把当前凭证 + 已知资料写进账号列表（多账号的"记住这个账号"）。
     *
     * 什么时候调：粘贴登录成功、以及每次拉到用户资料时（补昵称/头像）。
     * 去重靠 id（= md5(BDUSS)），所以重复调用只会更新，不会多出一条。
     */
    void rememberCurrentAccount();

signals:
    void changed();
    /*! 账号列表 / 当前账号变了（账号管理页据此刷新） */
    void accountsChanged();

private:
    /*! 测试用：QSettings 的文件路径（空 = 用应用默认位置） */
    QString m_settingsFilePath;

    QString m_bduss;
    QString m_stoken;
    QString m_uid;
    QString m_userName;
    QString m_nickName;
    QString m_portrait;
    QString m_tbs;
    bool m_needsProfileRefresh;
    bool m_loading;

    /* ---- 多账号（见文件头的说明） ---- */
    /*! 已保存账号，每项：{id, bduss, stoken, uid, userName, nickName, portrait, tbs} */
    QVariantList m_accounts;
    /*! 当前账号的 id（空 = 没在用任何账号） */
    QString m_currentId;

    /*! 按 id 找下标；id 为空时按当前 BDUSS 找；找不到返回 -1 */
    int indexOfId(const QString &id) const;
    /*!
     * 造一个 QSettings：测试改过道就用指定 ini，否则用应用默认位置。
     * （每次现造，QSettings 很轻，省得管生命周期。）
     */
    QSettings *makeSettings() const;
    /*! 去掉明文、补 masked / isCurrent，转成给外面看的形状 */
    QVariantMap publicAccount(const QVariantMap &raw) const;
};

} // namespace tieba

#endif // TB_SESSION_HPP
