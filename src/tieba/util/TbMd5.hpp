/*
 * TbMd5 - 自包含的 MD5 实现（RFC 1321）
 *
 * 贴吧客户端接口 (c.tieba.baidu.com 的 /c/f 与 /c/c 系列) 要求所有带
 * _client_version 的请求附加 sign 参数：
 *
 *     sign = md5( 排序后的 "k=v" 用 & 连接 + "tiebaclient!!!" )
 *
 * 不能依赖 OpenSSL / QCryptographicHash，所以自带一份。
 */

#ifndef TB_MD5_HPP
#define TB_MD5_HPP

#include <QByteArray>
#include <QString>

namespace tieba {

class TbMd5
{
public:
    TbMd5();
    ~TbMd5();

    /*! 追加数据（可多次调用） */
    void update(const QByteArray &data);

    /*! 结束计算并返回 16 字节摘要；之后不可再 update / digest */
    QByteArray digest();

    /*! 一次性计算，返回 32 位小写十六进制字符串 */
    static QString hex(const QByteArray &data);

    /*! 一次性计算，返回 16 字节摘要 */
    static QByteArray raw(const QByteArray &data);

private:
    TbMd5(const TbMd5 &);
    TbMd5 &operator=(const TbMd5 &);

    void processBlock(const unsigned char *block);
    void append(const unsigned char *data, int length);

    unsigned int m_state[4];
    unsigned long long m_bitCount;
    unsigned char m_buffer[64];
    int m_bufferLength;
    bool m_finished;
};

} // namespace tieba

#endif // TB_MD5_HPP
