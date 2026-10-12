#include "TbMd5.hpp"

#include <string.h>

namespace tieba {

namespace {

inline unsigned int rotl(unsigned int x, int c)
{
    return (x << c) | (x >> (32 - c));
}

// 每轮使用的常量：floor(2^32 * abs(sin(i + 1)))
const unsigned int K[64] = {
    0xd76aa478u, 0xe8c7b756u, 0x242070dbu, 0xc1bdceeeu,
    0xf57c0fafu, 0x4787c62au, 0xa8304613u, 0xfd469501u,
    0x698098d8u, 0x8b44f7afu, 0xffff5bb1u, 0x895cd7beu,
    0x6b901122u, 0xfd987193u, 0xa679438eu, 0x49b40821u,
    0xf61e2562u, 0xc040b340u, 0x265e5a51u, 0xe9b6c7aau,
    0xd62f105du, 0x02441453u, 0xd8a1e681u, 0xe7d3fbc8u,
    0x21e1cde6u, 0xc33707d6u, 0xf4d50d87u, 0x455a14edu,
    0xa9e3e905u, 0xfcefa3f8u, 0x676f02d9u, 0x8d2a4c8au,
    0xfffa3942u, 0x8771f681u, 0x6d9d6122u, 0xfde5380cu,
    0xa4beea44u, 0x4bdecfa9u, 0xf6bb4b60u, 0xbebfbc70u,
    0x289b7ec6u, 0xeaa127fau, 0xd4ef3085u, 0x04881d05u,
    0xd9d4d039u, 0xe6db99e5u, 0x1fa27cf8u, 0xc4ac5665u,
    0xf4292244u, 0x432aff97u, 0xab9423a7u, 0xfc93a039u,
    0x655b59c3u, 0x8f0ccc92u, 0xffeff47du, 0x85845dd1u,
    0x6fa87e4fu, 0xfe2ce6e0u, 0xa3014314u, 0x4e0811a1u,
    0xf7537e82u, 0xbd3af235u, 0x2ad7d2bbu, 0xeb86d391u
};

const int S[64] = {
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5,  9, 14, 20, 5,  9, 14, 20, 5,  9, 14, 20, 5,  9, 14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21
};

} // anonymous namespace

TbMd5::TbMd5()
    : m_bitCount(0)
    , m_bufferLength(0)
    , m_finished(false)
{
    m_state[0] = 0x67452301u;
    m_state[1] = 0xefcdab89u;
    m_state[2] = 0x98badcfeu;
    m_state[3] = 0x10325476u;
    memset(m_buffer, 0, sizeof(m_buffer));
}

TbMd5::~TbMd5()
{
}

void TbMd5::processBlock(const unsigned char *block)
{
    unsigned int m[16];
    for (int i = 0; i < 16; ++i) {
        m[i] = (unsigned int)(block[i * 4])
             | ((unsigned int)(block[i * 4 + 1]) << 8)
             | ((unsigned int)(block[i * 4 + 2]) << 16)
             | ((unsigned int)(block[i * 4 + 3]) << 24);
    }

    unsigned int a = m_state[0];
    unsigned int b = m_state[1];
    unsigned int c = m_state[2];
    unsigned int d = m_state[3];

    for (int i = 0; i < 64; ++i) {
        unsigned int f;
        int g;

        if (i < 16) {
            f = (b & c) | ((~b) & d);
            g = i;
        } else if (i < 32) {
            f = (d & b) | ((~d) & c);
            g = (5 * i + 1) % 16;
        } else if (i < 48) {
            f = b ^ c ^ d;
            g = (3 * i + 5) % 16;
        } else {
            f = c ^ (b | (~d));
            g = (7 * i) % 16;
        }

        const unsigned int tmp = d;
        d = c;
        c = b;
        b = b + rotl(a + f + K[i] + m[g], S[i]);
        a = tmp;
    }

    m_state[0] += a;
    m_state[1] += b;
    m_state[2] += c;
    m_state[3] += d;
}

void TbMd5::append(const unsigned char *data, int length)
{
    int index = 0;

    // 先填满当前未满的块
    if (m_bufferLength > 0) {
        const int fill = 64 - m_bufferLength;
        if (length < fill) {
            memcpy(m_buffer + m_bufferLength, data, (size_t)length);
            m_bufferLength += length;
            return;
        }
        memcpy(m_buffer + m_bufferLength, data, (size_t)fill);
        processBlock(m_buffer);
        m_bufferLength = 0;
        index = fill;
    }

    // 处理完整的 64 字节块
    while (index + 64 <= length) {
        processBlock(data + index);
        index += 64;
    }

    // 余下的留到下次
    if (index < length) {
        memcpy(m_buffer, data + index, (size_t)(length - index));
        m_bufferLength = length - index;
    }
}

void TbMd5::update(const QByteArray &data)
{
    if (m_finished || data.isEmpty())
        return;

    m_bitCount += (unsigned long long)data.size() * 8ull;
    append(reinterpret_cast<const unsigned char *>(data.constData()), data.size());
}

QByteArray TbMd5::digest()
{
    if (m_finished)
        return QByteArray();
    m_finished = true;

    static const unsigned char padding[64] = { 0x80 };

    const unsigned long long bitCount = m_bitCount;

    // 填充到 56 (mod 64)，再写 8 字节小端长度
    const int padLength = (m_bufferLength < 56) ? (56 - m_bufferLength)
                                                : (120 - m_bufferLength);
    append(padding, padLength);

    unsigned char lengthBytes[8];
    for (int i = 0; i < 8; ++i)
        lengthBytes[i] = (unsigned char)((bitCount >> (8 * i)) & 0xff);
    append(lengthBytes, 8);

    QByteArray out(16, '\0');
    for (int i = 0; i < 4; ++i) {
        out[i * 4 + 0] = (char)(m_state[i] & 0xff);
        out[i * 4 + 1] = (char)((m_state[i] >> 8) & 0xff);
        out[i * 4 + 2] = (char)((m_state[i] >> 16) & 0xff);
        out[i * 4 + 3] = (char)((m_state[i] >> 24) & 0xff);
    }
    return out;
}

QString TbMd5::hex(const QByteArray &data)
{
    TbMd5 md5;
    md5.update(data);
    const QByteArray raw = md5.digest();

    static const char *digits = "0123456789abcdef";
    QString out;
    out.reserve(32);
    for (int i = 0; i < raw.size(); ++i) {
        const unsigned char byte = (unsigned char)raw.at(i);
        out.append(QChar(digits[byte >> 4]));
        out.append(QChar(digits[byte & 0x0f]));
    }
    return out;
}

QByteArray TbMd5::raw(const QByteArray &data)
{
    TbMd5 md5;
    md5.update(data);
    return md5.digest();
}

} // namespace tieba
