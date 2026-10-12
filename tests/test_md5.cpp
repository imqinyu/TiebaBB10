/*
 * 本机验证用测试（不属于 BB10 工程，不会被打进 bar 包）
 * 用 Qt 4.8.1 + mingw 编译运行，验证纯逻辑层。
 */
#include "../src/tieba/util/TbMd5.hpp"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QtGlobal>
#include <stdio.h>

static int g_failures = 0;
static int g_checks = 0;

static void check(const QString &label, const QString &actual, const QString &expected)
{
    ++g_checks;
    if (actual == expected) {
        printf("  OK   %s\n", qPrintable(label));
    } else {
        ++g_failures;
        printf("  FAIL %s\n       expected: %s\n       actual:   %s\n",
               qPrintable(label), qPrintable(expected), qPrintable(actual));
    }
}

int main()
{
    printf("== MD5 (RFC 1321 test vectors) ==\n");

    // RFC 1321 附录 A.5 的 7 个测试向量
    check("md5(\"\")", tieba::TbMd5::hex(QByteArray("")),
          "d41d8cd98f00b204e9800998ecf8427e");
    check("md5(\"a\")", tieba::TbMd5::hex(QByteArray("a")),
          "0cc175b9c0f1b6a831c399e269772661");
    check("md5(\"abc\")", tieba::TbMd5::hex(QByteArray("abc")),
          "900150983cd24fb0d6963f7d28e17f72");
    check("md5(\"message digest\")", tieba::TbMd5::hex(QByteArray("message digest")),
          "f96b697d7cb7938d525a2f31aaf161d0");
    check("md5(\"abcdefghijklmnopqrstuvwxyz\")",
          tieba::TbMd5::hex(QByteArray("abcdefghijklmnopqrstuvwxyz")),
          "c3fcd3d76192e4007dfb496cca67e13b");
    check("md5(\"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789\")",
          tieba::TbMd5::hex(QByteArray("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789")),
          "d174ab98d277d9f5a5611c2c9f419d9f");
    check("md5(8x\"1234567890\")",
          tieba::TbMd5::hex(QByteArray("1234567890123456789012345678901234567890"
                                       "1234567890123456789012345678901234567890")),
          "57edf4a22be3c955ac49da2e2107b67a");

    // 边界：55 / 56 / 63 / 64 / 65 / 119 / 120 字节，覆盖"\u586B\u5145\u8DE8\u5757"分支
    check("md5(55 x 'a')", tieba::TbMd5::hex(QByteArray(55, 'a')),
          "ef1772b6dff9a122358552954ad0df65");
    check("md5(56 x 'a')", tieba::TbMd5::hex(QByteArray(56, 'a')),
          "3b0c8ac703f828b04c6c197006d17218");
    check("md5(63 x 'a')", tieba::TbMd5::hex(QByteArray(63, 'a')),
          "b06521f39153d618550606be297466d5");
    check("md5(64 x 'a')", tieba::TbMd5::hex(QByteArray(64, 'a')),
          "014842d480b571495a4a0363793f7367");
    check("md5(65 x 'a')", tieba::TbMd5::hex(QByteArray(65, 'a')),
          "c743a45e0d2e6a95cb859adae0248435");
    check("md5(119 x 'a')", tieba::TbMd5::hex(QByteArray(119, 'a')),
          "8a7bd0732ed6a28ce75f6dabc90e1613");
    check("md5(120 x 'a')", tieba::TbMd5::hex(QByteArray(120, 'a')),
          "5f61c0ccad4cac44c75ff505e1f1e537");

    // 贴吧签名用的固定盐
    check("md5(\"tiebaclient!!!\")", tieba::TbMd5::hex(QByteArray("tiebaclient!!!")),
          "8bafcaa5e0b396536d4d7878549705e5");

    // 中文（UTF-8 多字节）不应破坏分块逻辑
    check("md5(\"\u8D34\u5427\")", tieba::TbMd5::hex(QString::fromUtf8("\u8D34\u5427").toUtf8()),
          "f0694566db2ea0eba37557ac73a73a60");

    // 与「整体一次算」的一致性检查
    {
        QByteArray big;
        for (int i = 0; i < 1000; ++i)
            big.append((char)(i % 251));
        const QString whole = tieba::TbMd5::hex(big);

        // 分块喂入应当得到同一结果
        tieba::TbMd5 chunked;
        int pos = 0;
        const int sizes[] = { 1, 7, 63, 64, 65, 100, 200, 500 };
        int si = 0;
        while (pos < big.size()) {
            const int n = qMin(sizes[si % 8], big.size() - pos);
            chunked.update(big.mid(pos, n));
            pos += n;
            ++si;
        }
        const QString chunkedHex = chunked.digest().toHex();
        check("\u5206\u5757 update \u4E0E\u6574\u4F53\u4E00\u81F4 (1000B)", chunkedHex, whole);
    }

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
