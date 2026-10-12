/*
 * TbSigner - 贴吧客户端接口的公共参数与 sign 计算
 *
 * 背景（来自官方客户端行为逆向 + TiebaLite 参考实现）：
 *
 *  接口基址   http://c.tieba.baidu.com/
 *  盐         "tiebaclient!!!"
 *
 *  公共参数（POST 表单体，GET 则拼到查询串）：
 *      BDUSS           登录凭证（未登录时该项不参与签名也不发送）
 *      _client_id      wappc_<毫秒时间戳>_<0-999 随机>
 *      _client_type    2
 *      _client_version 7.2.0.0 / 8.2.2 / 11.x —— 决定走哪一代接口
 *      _phone_imei     搜索用 16 个 0
 *      model           机型
 *      net_type        1
 *      timestamp       毫秒时间戳
 *
 *  签名规则（仅当表单里存在 _client_version 时）：
 *      sign = md5( 按「未编码」的 k=v 以字节序升序、用 & 连接 + 盐 )
 *
 *  另外部分接口需要 cookie: ka=open，需要登录的接口会带 force_login: true 头。
 */

#ifndef TB_SIGNER_HPP
#define TB_SIGNER_HPP

#include <QByteArray>
#include <QList>
#include <QString>

#include "util/TbString.hpp"

namespace tieba {

class TbSession;

class TbSigner
{
public:
    /*! 客户端接口基址 */
    static QString apiBase();
    /*! web 接口基址（部分接口如 /mo/q/search/user 在 web 域） */
    static QString webBase();

    /*! 盐值 */
    static QByteArray salt();

    /*! 生成 _client_id，形如 wappc_1700000000000_123 */
    static QString newClientId();

    /*!
     * 构造公共参数。
     * @param session 用于取 BDUSS；为 0 或未登录时不加入 BDUSS
     * @param clientVersion 形如 "7.2.0.0"
     */
    static QList<KeyValue> commonParams(const TbSession *session,
                                        const QString &clientVersion);

    /*! 在给定参数上追加公共参数（已存在的键不覆盖） */
    static QList<KeyValue> withCommonParams(const QList<KeyValue> &params,
                                            const TbSession *session,
                                            const QString &clientVersion);

    /*!
     * 计算 sign。params 应该是「最终要发送的全量参数」（含公共参数）。
     * 返回 32 位小写十六进制；参数为空时返回空串。
     */
    static QString sign(const QList<KeyValue> &params);
    /*! Android protocol: ordinal raw k=v without separators, uppercase MD5. */
    static QString androidSign(const QList<KeyValue> &params);
    static QByteArray buildAndroidForm(const QList<KeyValue> &params);

    /*!
     * 是否需要签名：官方客户端只在表单含 _client_version 时签名。
     * 没有它却签名反而会被拒。
     */
    static bool needsSign(const QList<KeyValue> &params);

    /*!
     * 产出最终的表单体（已排序、已追加 sign）。
     * 参数顺序按签名的排序规则，与官方客户端一致。
     */
    static QByteArray buildSignedForm(const QList<KeyValue> &params);

    /*!
     * 产出最终的查询串（GET 用，已排序、已追加 sign）。
     */
    static QString buildSignedQuery(const QList<KeyValue> &params);

    /*! 默认 User-Agent：bdtb for Android <version> */
    static QString userAgent(const QString &clientVersion);

    /*! 采样一种机型字符串（真机上可用实际机型替换） */
    static QString deviceModel();

    /* ======================================================================
     *  PC 端接口（2026-10 实测：移动端 /c/f/frs/page 已恒定返回 110001，
     *  只有 PC 端 *_pc 接口可用。详见 docs/接口调研结论.md）
     *
     *  PC 与移动端的差异全在这三处：
     *      盐      36770b1f34c9bbf2e7d1a99d2b82fa9e（不是 tiebaclient!!!）
     *      拼接    排序后的 k=v **不用分隔符** 直接相连
     *      公共参数 subapp_type=pc、_client_type=20
     * ====================================================================== */

    /*! PC 端盐值 */
    static QByteArray pcSalt();

    /*! PC 端公共参数（subapp_type=pc、_client_type=20、BDUSS） */
    static QList<KeyValue> pcCommonParams(const TbSession *session);

    /*! 在给定参数上追加 PC 端公共参数（已存在的键不覆盖） */
    static QList<KeyValue> withPcCommonParams(const QList<KeyValue> &params,
                                              const TbSession *session);

    /*!
     * 计算 PC 端 sign：md5( 按 key 字典序、无分隔符拼接的 "k=v" + pcSalt )
     * 参与签名的值用「未编码」原文；sign/sig 自身排除在外。
     */
    static QString pcSign(const QList<KeyValue> &params);

    /*! 产出 PC 端表单体（已追加 sign） */
    static QByteArray buildPcForm(const QList<KeyValue> &params);

    /*! PC 端 User-Agent（桌面浏览器 UA，PC 接口会校验） */
    static QString pcUserAgent();
};

} // namespace tieba

#endif // TB_SIGNER_HPP
