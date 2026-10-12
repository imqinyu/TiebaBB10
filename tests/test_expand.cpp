/*
 * TiebaController::expandImageItems 的行为测试
 *
 * 这是"一个楼层多张图"能全部显示的关键：Cascades 的 ListItemComponent
 * 里没法遍历数组（没有 Repeater，套 ListView 又会崩），所以多图必须在
 * C++ 侧把楼层拆成"正文一项 + 每张图各一项"。
 *
 * 这里验证拆分规则和字段传递是否正确。
 */

#include "TiebaController.hpp"
#include <bb/cascades/ArrayDataModel>

#include <QCoreApplication>
#include <QString>
#include <QStringList>
#include <QVariantList>
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
        printf("  FAIL %s\n     expected=%d actual=%d\n",
               qPrintable(label), expected, actual);
        ++g_failures;
    }
}

/*! 造一个楼层的 map */
static QVariantMap makePost(const QString &id, const QString &floor,
                            const QStringList &imageUrls, bool withText)
{
    QVariantMap m;
    m.insert(QLatin1String("id"), id);
    m.insert(QLatin1String("floor"), floor);
    m.insert(QLatin1String("authorName"), QLatin1String("tester"));
    m.insert(QLatin1String("plainText"),
             withText ? QLatin1String("some text") : QString());
    m.insert(QLatin1String("subPostCount"), 0);
    m.insert(QLatin1String("subPostPreviewText"), QString());

    QVariantList images;
    for (int i = 0; i < imageUrls.size(); ++i) {
        QVariantMap img;
        img.insert(QLatin1String("url"), imageUrls.at(i));
        img.insert(QLatin1String("originUrl"), imageUrls.at(i));
        img.insert(QLatin1String("width"), 560);
        img.insert(QLatin1String("height"), 713);
        images.append(img);
    }
    m.insert(QLatin1String("images"), images);
    m.insert(QLatin1String("firstImageUrl"),
             imageUrls.isEmpty() ? QString() : imageUrls.at(0));
    m.insert(QLatin1String("hasImage"), !imageUrls.isEmpty());
    m.insert(QLatin1String("imageCount"), imageUrls.size());
    return m;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    printf("== expandImageItems ==\n");

    TiebaController c;

    /* ---- 用例 1：无图楼层 -> 1 项 ---- */
    {
        QVariantList posts;
        posts.append(makePost("1", "2", QStringList(), true));
        QVariantList out;
        c.expandImageItems(posts, out);

        checkInt("无图：1 个楼层 -> 1 项", out.size(), 1);
        const QVariantMap it = out.at(0).toMap();
        check("无图项 isImageItem=false", it.value("isImageItem").toBool() == false);
        check("无图项 postHead=true", it.value("postHead").toBool() == true);
        check("无图项 localImagePath 为空",
              it.value("localImagePath").toString().isEmpty());
    }

    /* ---- 用例 2：单图楼层 -> 2 项 ---- */
    {
        QVariantList posts;
        posts.append(makePost("2", "3", QStringList() << "https://a.invalid/1.jpg", true));
        QVariantList out;
        c.expandImageItems(posts, out);

        checkInt("单图：1 个楼层 -> 2 项", out.size(), 2);
        check("第 1 项是正文", out.at(0).toMap().value("isImageItem").toBool() == false);
        check("第 1 项 postHead=true",
              out.at(0).toMap().value("postHead").toBool() == true);
        check("第 2 项是图片", out.at(1).toMap().value("isImageItem").toBool() == true);
        check("第 2 项 postHead=false",
              out.at(1).toMap().value("postHead").toBool() == false);
        check("图片项带上了远程地址",
              out.at(1).toMap().value("remoteImageUrl").toString()
                  == QLatin1String("https://a.invalid/1.jpg"));
        checkInt("图片项带上了原始宽度",
                 out.at(1).toMap().value("imageWidth").toInt(), 560);
        // id / floor 要跟着图片项，列表 onTriggered 才能取到
        check("图片项带上了 id", out.at(1).toMap().value("id").toString() == "2");
        check("图片项带上了 floor",
              out.at(1).toMap().value("floor").toString() == "3");
    }

    /* ---- 用例 3：多图楼层 -> 正文 + N 项（关键用例）---- */
    {
        QStringList urls;
        urls << "https://a.invalid/1.jpg"
             << "https://a.invalid/2.jpg"
             << "https://a.invalid/3.jpg"
             << "https://a.invalid/4.jpg"
             << "https://a.invalid/5.jpg";
        QVariantList posts;
        posts.append(makePost("3", "1", urls, true));
        QVariantList out;
        c.expandImageItems(posts, out);

        checkInt("5 张图：1 个楼层 -> 6 项", out.size(), 6);

        // 正文项在第一
        check("第 1 项是正文", out.at(0).toMap().value("isImageItem").toBool() == false);

        // 之后每项一张图，顺序与 contentSegments 一致
        bool orderOk = true;
        int imageItems = 0;
        for (int i = 1; i < out.size(); ++i) {
            const QVariantMap m = out.at(i).toMap();
            if (m.value("isImageItem").toBool() != true) { orderOk = false; break; }
            if (m.value("postHead").toBool() != false) { orderOk = false; break; }
            ++imageItems;
            if (m.value("remoteImageUrl").toString() != urls.at(i - 1)) {
                orderOk = false;
                break;
            }
        }
        check("图片项顺序与原始顺序一致", orderOk);
        checkInt("图片项数量 = 图片数", imageItems, 5);
    }

    /* ---- 用例 4：多个楼层混合 ---- */
    {
        QVariantList posts;
        posts.append(makePost("a", "2", QStringList(), true));                          // 0 图
        posts.append(makePost("b", "3", QStringList() << "https://a.invalid/b1.jpg", true)); // 1 图
        posts.append(makePost("c", "4",
                              QStringList() << "https://a.invalid/c1.jpg"
                                            << "https://a.invalid/c2.jpg", true));       // 2 图
        QVariantList out;
        c.expandImageItems(posts, out);

        // 1 + 2 + 3 = 6
        checkInt("混合：3 个楼层 -> 6 项", out.size(), 6);

        // 每个楼层恰好一个 postHead=true
        int heads = 0;
        for (int i = 0; i < out.size(); ++i) {
            if (out.at(i).toMap().value("postHead").toBool())
                ++heads;
        }
        checkInt("postHead 恰好 3 个（每楼层一个）", heads, 3);
    }

    /* ---- 用例 5：远程地址必须保留（刷新本地路径要用）---- */
    {
        QVariantList posts;
        posts.append(makePost("x", "5",
                              QStringList() << "https://a.invalid/x.jpg", false));
        QVariantList out;
        c.expandImageItems(posts, out);

        check("正文项也带 remoteImageUrl",
              !out.at(0).toMap().value("remoteImageUrl").toString().isEmpty());
        check("图片项有 remoteImageUrl",
              !out.at(1).toMap().value("remoteImageUrl").toString().isEmpty());
    }

    /*!
     * ---- 用例 6：表情留在所属正文 / 楼中楼预览内 ----
     * 独立表情行会把楼中楼里的表情挪到父楼层末尾。验证两个来源
     * 都保留段落及远程地址，且没有 isEmoticonItem 独立行。
     */
    {
        QVariantList posts;

        // 本层正文里一个表情
        QVariantMap p = makePost("e1", "1", QStringList(), true);
        QVariantList segs;
        {
            QVariantMap s;
            s.insert("kind", "emoticon");
            s.insert("emoticonName", QString::fromUtf8("\u6CEA"));   // 泪
            s.insert("url", "https://emo/1.png");
            segs.append(s);
        }
        p.insert("contentSegments", segs);
        p.insert("subPostPreview", QVariantList());
        posts.append(p);

        QVariantList out;
        c.expandImageItems(posts, out);

        checkInt("本层表情：1 个楼层仍是 1 正文项", out.size(), 1);
        const QVariantMap body = out.at(0).toMap();
        check("表情留在正文项且保留作者头", body.value("postHead").toBool()
              && body.value("rowType").toString() == QLatin1String("post"));
        const QVariantList bodySegments = body.value("contentSegments").toList();
        checkInt("本层表情段落保留", bodySegments.size(), 1);
        check("本层表情类型和名称保留",
              bodySegments.at(0).toMap().value("kind").toString() == QLatin1String("emoticon")
              && bodySegments.at(0).toMap().value("emoticonName").toString() == QString::fromUtf8("\u6CEA"));
        check("本层表情远程地址保留",
              bodySegments.at(0).toMap().value("url").toString() == QLatin1String("https://emo/1.png"));
        check("本层表情不生成独立行", !body.value("isEmoticonItem").toBool());

        // ---- 楼中楼预览里的表情（这是用户实际遇到的那个场景）----
        QVariantList posts2;
        QVariantMap p2 = makePost("e2", "2", QStringList() << "https://a.invalid/y.jpg", true);
        p2.insert("contentSegments", QVariantList());
        QVariantList preview;
        {
            QVariantMap sub;
            sub.insert("authorName", QLatin1String("someone"));
            sub.insert("plainText", QString::fromUtf8("\u54ED [\u6CEA]"));  // 哭 [泪]
            QVariantList ssegs;
            {
                QVariantMap s;
                s.insert("kind", "emoticon");
                s.insert("emoticonName", QString::fromUtf8("\u6CEA"));
                s.insert("url", "https://emo/2.png");
                ssegs.append(s);
            }
            sub.insert("contentSegments", ssegs);
            preview.append(sub);
        }
        p2.insert("subPostPreview", preview);
        posts2.append(p2);

        QVariantList out2;
        c.expandImageItems(posts2, out2);

        // 正文 + 图片 + 回复尾部；楼中楼表情只属于回复预览。
        checkInt("楼中楼表情：正文、图片、回复尾部共 3 项", out2.size(), 3);
        check("正文在前", out2.at(0).toMap().value("rowType").toString() == QLatin1String("post"));
        check("图片在回复前", out2.at(1).toMap().value("isImageItem").toBool()
              && out2.at(1).toMap().value("remoteImageUrl").toString() == QLatin1String("https://a.invalid/y.jpg"));
        const QVariantMap footer = out2.at(2).toMap();
        check("回复尾部在图片后", footer.value("isReplyFooter").toBool()
              && footer.value("rowType").toString() == QLatin1String("replies"));
        const QVariantList savedPreview = footer.value("subPostPreview").toList();
        checkInt("楼中楼预览保留", savedPreview.size(), 1);
        const QVariantMap savedReply = savedPreview.at(0).toMap();
        check("楼中楼作者和正文保留", savedReply.value("authorName").toString() == QLatin1String("someone")
              && savedReply.value("plainText").toString() == QString::fromUtf8("\u54ED [\u6CEA]"));
        const QVariantList replySegments = savedReply.value("contentSegments").toList();
        checkInt("楼中楼表情段落保留", replySegments.size(), 1);
        check("楼中楼表情类型、名称和地址保留",
              replySegments.at(0).toMap().value("kind").toString() == QLatin1String("emoticon")
              && replySegments.at(0).toMap().value("emoticonName").toString() == QString::fromUtf8("\u6CEA")
              && replySegments.at(0).toMap().value("url").toString() == QLatin1String("https://emo/2.png"));
        bool foundIndependentEmoticon = false;
        for (int i = 0; i < out2.size(); ++i)
            if (out2.at(i).toMap().value("isEmoticonItem").toBool()) foundIndependentEmoticon = true;
        check("楼中楼表情不生成独立行", !foundIndependentEmoticon);
        check("楼层分隔线在回复尾部", footer.value("floorEnd").toBool()
              && !out2.at(0).toMap().value("floorEnd").toBool()
              && !out2.at(1).toMap().value("floorEnd").toBool());
    }

    // 楼中楼复用同一个模型：第一页要立即清空，翻页不能清空已有回复。
    {
        QVariantMap oldReply;
        oldReply.insert("plainText", "previous floor reply");
        c.subPosts()->append(QVariant(oldReply));
        c.loadSubPosts("123", "456", 1);
        check("楼中楼第一页：响应前已清空上一层回复", c.subPosts()->isEmpty());
        check("楼中楼第一页：保持加载状态", c.loading());
        check("楼中楼第一页：不沿用上一页的翻页状态", !c.hasMore());

        c.subPosts()->append(QVariant(oldReply));
        c.loadSubPosts("123", "456", 2);
        checkInt("楼中楼翻页：响应前保留现有回复", c.subPosts()->size(), 1);
        c.loadSubPosts("123", "789", 1);
        check("切换到另一层：再次立即清空回复", c.subPosts()->isEmpty());
    }

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
