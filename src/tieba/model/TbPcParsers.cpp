#include "TbPcParsers.hpp"

#include "model/TbParsers.hpp"
#include "util/TbString.hpp"
#include "util/TbTr.hpp"

#include <QMap>
#include <QSet>

namespace tieba {

namespace {

/*! feed_head.main_data[] 里靠 icon_type 区分用途 */
const char kIconTypeNameShow[] = "name_show";
const char kIconTypeForumLevel[] = "forum_level";

/*
 * ============================ 贴吧表情 ============================
 *
 * 贴吧正文里的表情是 `type: 2` 的段落，字段是 **c = 表情名字**（"滑稽"、"呵呵"…），
 * **不是**现成的图片 URL。所以显示表情必须自己拼地址：
 *
 *     http://static.tieba.baidu.com/tb/editor/images/client/image_emoticon<N>.png
 *
 * 名字 -> N 的对应表取自 Android 版 TiebaLite
 * （utils/EmoticonManager.kt 的 DEFAULT_EMOTICON_MAPPING），那是权威表。
 *
 * 注意表里有同名的项（"生气" 同时映射到 31 和 61），
 * QMap 的 insert 会覆盖，最终值是后插的那个 —— 和 Kotlin 的 mapOf 行为一致
 * （后面的 key 覆盖前面的），所以两种实现结果相同。
 */
const char kEmoticonBase[] =
    "http://static.tieba.baidu.com/tb/editor/images/client/";

struct EmoticonEntry { const char *name; int number; };

const EmoticonEntry kEmoticons[] = {
    { "\xE5\x91\xB5\xE5\x91\xB5", 1 },   // 呵呵
    { "\xE5\x93\x88\xE5\x93\x88", 2 },   // 哈哈
    { "\xE5\x90\x90\xE8\x88\x8C", 3 },   // 吐舌
    { "\xE5\x95\x8A", 4 },               // 啊
    { "\xE9\x85\xB7", 5 },               // 酷
    { "\xE6\x80\x92", 6 },               // 怒
    { "\xE5\xBC\x80\xE5\xBF\x83", 7 },   // 开心
    { "\xE6\xB1\x97", 8 },               // 汗
    { "\xE6\xB3\xAA", 9 },               // 泪
    { "\xE9\xBB\x91\xE7\xBA\xBF", 10 },  // 黑线
    { "\xE9\x84\x99\xE8\xA7\x86", 11 },  // 鄙视
    { "\xE4\xB8\x8D\xE9\xAB\x98\xE5\x85\xB4", 12 },  // 不高兴
    { "\xE7\x9C\x9F\xE6\xA3\x92", 13 },  // 真棒
    { "\xE9\x92\xB1", 14 },              // 钱
    { "\xE7\x96\x91\xE9\x97\xAE", 15 },  // 疑问
    { "\xE9\x98\xB4\xE9\x99\xA9", 16 },  // 阴险
    { "\xE5\x90\x90", 17 },              // 吐
    { "\xE5\x92\xA6", 18 },              // 咦
    { "\xE5\xA7\x94\xE5\xB1\x88", 19 },  // 委屈
    { "\xE8\x8A\xB1\xE5\xBF\x83", 20 },  // 花心
    { "\xE5\x91\xBC~", 21 },             // 呼~
    { "\xE7\xAC\x91\xE7\x9C\xBC", 22 },  // 笑眼
    { "\xE5\x86\xB7", 23 },              // 冷
    { "\xE5\xA4\xAA\xE5\xBC\x80\xE5\xBF\x83", 24 },  // 太开心
    { "\xE6\xBB\x91\xE7\xA8\xBD", 25 },  // 滑稽
    { "\xE5\x8B\x89\xE5\xBC\xBA", 26 },  // 勉强
    { "\xE7\x8B\x82\xE6\xB1\x97", 27 },  // 狂汗
    { "\xE4\xB9\x96", 28 },              // 乖
    { "\xE7\x9D\xA1\xE8\xA7\x89", 29 },  // 睡觉
    { "\xE6\x83\x8A\xE5\x93\xAD", 30 },  // 惊哭
    { "\xE7\x94\x9F\xE6\xB0\x94", 31 },  // 生气
    { "\xE6\x83\x8A\xE8\xAE\xB6", 32 },  // 惊讶
    { "\xE5\x96\xB7", 33 },              // 喷
    { "\xE7\x88\xB1\xE5\xBF\x83", 34 },  // 爱心
    { "\xE5\xBF\x83\xE7\xA2\x8E", 35 },  // 心碎
    { "\xE7\x8E\xAB\xE7\x91\xB0", 36 },  // 玫瑰
    { "\xE7\xA4\xBC\xE7\x89\xA9", 37 },  // 礼物
    { "\xE5\xBD\xA9\xE8\x99\xB9", 38 },  // 彩虹
    { "\xE6\x98\x9F\xE6\x98\x9F\xE6\x9C\x88\xE4\xBA\xAE", 39 },  // 星星月亮
    { "\xE5\xA4\xAA\xE9\x98\xB3", 40 },  // 太阳
    { "\xE9\x92\xB1\xE5\xB8\x81", 41 },  // 钱币
    { "\xE7\x81\xAF\xE6\xB3\xA1", 42 },  // 灯泡
    { "\xE8\x8C\xB6\xE6\x9D\xAF", 43 },  // 茶杯
    { "\xE8\x9B\x8B\xE7\xB3\x95", 44 },  // 蛋糕
    { "\xE9\x9F\xB3\xE4\xB9\x90", 45 },  // 音乐
    { "haha", 46 },
    { "\xE8\x83\x9C\xE5\x88\xA9", 47 },  // 胜利
    { "\xE5\xA4\xA7\xE6\x8B\x87\xE6\x8C\x87", 48 },  // 大拇指
    { "\xE5\xBC\xB1", 49 },              // 弱
    { "OK", 50 },
    { "\xE7\x94\x9F\xE6\xB0\x94", 61 },  // 生气（覆盖上面的 31，和安卓表一致）
    { "\xE6\xB2\x99\xE5\x8F\x91", 77 },  // 沙发
    { "\xE6\x89\x8B\xE7\xBA\xB8", 78 },  // 手纸
    { "\xE9\xA6\x99\xE8\x95\x89", 79 },  // 香蕉
    { "\xE4\xBE\xBF\xE4\xBE\xBF", 80 },  // 便便
    { "\xE8\x8D\xAF\xE4\xB8\xB8", 81 },  // 药丸
    { "\xE7\xBA\xA2\xE9\xA2\x86\xE5\xB7\xBE", 82 },  // 红领巾
    { "\xE8\x9C\xA1\xE7\x83\x9B", 83 },  // 蜡烛
    { "\xE4\xB8\x89\xE9\x81\x93\xE6\x9D\xA0", 84 },  // 三道杠
    { 0, 0 }
};

/*! 名字 -> image_emoticon<N> 的编号；查不到返回 0 */
int emoticonNumber(const QString &name)
{
    if (name.isEmpty())
        return 0;
    /*
     * ★ 必须遍历**整张表**并取最后一次匹配，不能找到第一个就 return。
     *
     * 表里有同名项（"生气" 同时映射 31 和 61）。Kotlin 那边用的是
     * mapOf(...)，**后面的 key 会覆盖前面的**，所以"生气" 最终是 61。
     * 如果这里找到 31 就返回，两边行为就不一致了。
     * （这条是被断言抓出来的：期望 61，实际给了 31。）
     */
    int found = 0;
    for (int i = 0; kEmoticons[i].name; ++i) {
        if (name == QString::fromUtf8(kEmoticons[i].name))
            found = kEmoticons[i].number;
    }
    return found;
}

/*!
 * 从 main_data[] 里按 icon_type 找一项。
 * main_data 的每项形如：
 *   { "type":1, "text":{"text":"作者名"},
 *     "business_info_map":{"icon_type":"name_show"} }
 *   { "type":3, "custom":{"data":"{\"level_name\":\"Syncnode\"}"},
 *     "business_info_map":{"icon_type":"forum_level"} }
 */
TbJson findMainDataByIconType(const TbJson &mainData, const QString &iconType)
{
    for (int i = 0; i < mainData.size(); ++i) {
        const TbJson item = mainData.at(i);
        const TbJson map = TbJson::object(item, QLatin1String("business_info_map"));
        if (TbJson::str(map, QLatin1String("icon_type")) == iconType)
            return item;
    }
    return TbJson();
}

/*! 统一处理错误码；有错时返回 true 并填好 result 的错误字段 */
bool fillError(const TbJson &root, QString *code, QString *message)
{
    return TbParsers::hasError(root, code, message);
}

} // anonymous namespace

/* ============================ 片段解析 ============================ */

TbJson TbPcParsers::componentOf(const TbJson &feed, const QString &name)
{
    const TbJson components = TbJson::array(feed, QLatin1String("components"));
    for (int i = 0; i < components.size(); ++i) {
        const TbJson item = components.at(i);
        if (TbJson::str(item, QLatin1String("component")) == name)
            return item;
    }
    return TbJson();
}

QString TbPcParsers::textOfDataArray(const TbJson &dataArray)
{
    QString out;
    if (!dataArray.isArray())
        return out;

    for (int i = 0; i < dataArray.size(); ++i) {
        const TbJson item = dataArray.at(i);

        // 文本项
        const TbJson textInfo = TbJson::object(item, QLatin1String("text_info"));
        if (textInfo.isObject()) {
            out += TbJson::str(textInfo, QLatin1String("text"));
            continue;
        }

        // 表情项：用微博式占位符表示，UI 想换成图片再按需处理
        const TbJson emoji = TbJson::object(item, QLatin1String("emoji_info"));
        if (emoji.isObject()) {
            const QString c = TbJson::str(emoji, QLatin1String("c"));
            if (!c.isEmpty())
                out += c;
            continue;
        }

        // 少数情况直接给 text
        out += TbJson::str(item, QLatin1String("text"));
    }
    return out;
}

QString TbPcParsers::nestedJsonValue(const QString &jsonText, const QString &key)
{
    if (jsonText.isEmpty() || key.isEmpty())
        return QString();

    // custom.data 是被转义过的 JSON 字符串，必须二次解析
    const TbJson nested = TbJson::parse(jsonText.toUtf8());
    if (!nested.isValid())
        return QString();
    return TbJson::str(nested, key);
}

/* ============================ 正文 content[] ============================ */

QVariantList TbPcParsers::parseContentSegments(const TbJson &content)
{
    QVariantList segs;
    if (!content.isArray())
        return segs;

    for (int k = 0; k < content.size(); ++k) {
        const TbJson seg = content.at(k);
        const int type = (int)TbJson::integer(seg, QLatin1String("type"), 0);

        QString kind;
        QString text;
        QString emoticonName;
        QString url;
        QString originUrl;

        switch (type) {
        case 0:   // 纯文本
            kind = QLatin1String("text");
            text = TbJson::str(seg, QLatin1String("text"));
            break;
        case 1:   // 链接
            kind = QLatin1String("link");
            text = TbJson::str(seg, QLatin1String("text"));
            url = TbJson::str(seg, QLatin1String("url"));
            if (text.isEmpty())
                text = url;
            break;
        case 2:   // 表情
            /*
             * ★★ 贴吧给的信息比想象的多 —— 实测一个真实的 type 2 段是：
             *
             *     { "type": "2", "text": "image_emoticon66", "c": "小乖" }
             *
             * 也就是说：
             *   text  就是**表情 ID**（image_emoticon<N>），可以直接拼地址
             *   c     是显示用的名字
             *
             * 所以**优先用 text 拼地址**，只有它为空时才回退到按名字查表。
             * 为什么这很重要："小乖" 根本不在名字表里（表里只有"乖"），
             * 光靠名字映射会漏掉表情 —— 这就是表情不显示的原因之一。
             *
             * 之前这里只取了 name/c，**url 一直是空的**。
             */
            kind = QLatin1String("emoticon");
            emoticonName = TbJson::str(seg, QLatin1String("c"));
            if (emoticonName.isEmpty())
                emoticonName = TbJson::str(seg, QLatin1String("name"));
            text = TbJson::str(seg, QLatin1String("text"));
            {
                // 1) 服务端直接给了 image_emoticon<N>，直接拼
                QString id = text;
                if (id.isEmpty())
                    id = TbJson::str(seg, QLatin1String("name"));
                // 2) 都没给（或给的不是 id 形式）就按名字查表
                if (id.indexOf(QLatin1String("image_emoticon")) < 0) {
                    url = TbPcParsers::emoticonUrl(emoticonName);
                    if (url.isEmpty())
                        url = TbPcParsers::emoticonUrl(text);
                } else {
                    url = TbPcParsers::emoticonUrlById(id);
                }
                // 3) 服务端给了完整 url 就用它的
                const QString given = TbJson::str(seg, QLatin1String("url"));
                if (! given.isEmpty())
                    url = given;
            }
            break;
        case 3:   // 图片
            // ★ 实测字段名是 origin_src / cdn_src，**不是** origin_pic_url。
            //   之前只找 origin_pic_url/big_pic_url/url/src，图片地址全部为空，
            //   所以界面上图片显示不出来。
            //
            //   三者用途不同，这里都取出来：
            //     displayUrl  优先用 cdn_src（服务端已压到 720 宽，几十 KB）
            //                 —— BB10 的 ImageView 对大图有内存限制，小图更稳、更快
            //     originUrl   原图（可能几百 KB 到几 MB）
            //     bigUrl      big_cdn_src（960 宽）
            kind = QLatin1String("image");
            {
                const QString origin = TbJson::str(seg, QLatin1String("origin_src"));
                const QString cdn = TbJson::str(seg, QLatin1String("cdn_src"));
                const QString big = TbJson::str(seg, QLatin1String("big_cdn_src"));

                originUrl = origin;
                if (originUrl.isEmpty())
                    originUrl = cdn;
                if (originUrl.isEmpty())
                    originUrl = TbJson::str(seg, QLatin1String("origin_pic_url"));

                // 显示用：小的优先
                QString display = cdn;
                if (display.isEmpty())
                    display = big;
                if (display.isEmpty())
                    display = originUrl;
                if (display.isEmpty())
                    display = TbJson::str(seg, QLatin1String("url"));
                if (display.isEmpty())
                    display = TbJson::str(seg, QLatin1String("src"));
                url = display;

                // ★ 必须升级成 https：BB10 的图片加载器只认
                //   https / asset / file，遇到 http:// 会直接拒绝
                //   （"Unsupported scheme (http) used in url ... Image loading aborted"）。
                //   贴吧接口返回的图片地址可能是 http，服务端按接入点变化。
                originUrl = TbString::httpsImageUrl(originUrl);
                url = TbString::httpsImageUrl(url);
            }
            break;
        case 4:   // @某人
            kind = QLatin1String("user");
            text = TbJson::str(seg, QLatin1String("text"));
            break;
        case 5:   // 视频
            kind = QLatin1String("video");
            url = TbJson::str(seg, QLatin1String("url"));
            break;
        case 10:  // 语音
            kind = QLatin1String("voice");
            break;
        default:
            // 未知类型：能取到 text 就当文本，否则忽略
            text = TbJson::str(seg, QLatin1String("text"));
            if (text.isEmpty())
                continue;
            kind = QLatin1String("text");
            break;
        }

        QVariantMap m;
        m.insert(QLatin1String("kind"), kind);
        m.insert(QLatin1String("type"), kind);
        m.insert(QLatin1String("text"), text);
        m.insert(QLatin1String("url"), url);
        m.insert(QLatin1String("originUrl"), originUrl);
        m.insert(QLatin1String("emoticonName"), emoticonName);

        // 图片带尺寸信息（bsize 形如 "560,713"），UI 排版用得上
        if (kind == QLatin1String("image")) {
            const QString bsize = TbJson::str(seg, QLatin1String("bsize"));
            if (!bsize.isEmpty()) {
                const QStringList wh = bsize.split(QLatin1Char(','));
                if (wh.size() == 2) {
                    m.insert(QLatin1String("width"), wh.at(0).toInt());
                    m.insert(QLatin1String("height"), wh.at(1).toInt());
                }
            }
            m.insert(QLatin1String("showOriginalBtn"),
                     (int)TbJson::integer(seg, QLatin1String("show_original_btn"), 0));
        }

        segs.append(m);
    }
    return segs;
}

QString TbPcParsers::emoticonUrl(const QString &name)
{
    const int n = emoticonNumber(name);
    if (n <= 0)
        return QString();

    const QString url = QString(QLatin1String(kEmoticonBase))
                        + QLatin1String("image_emoticon") + QString::number(n)
                        + QLatin1String(".png");
    // 统一升成 https（BB10 图片加载器只认 https/asset/file）
    return TbString::httpsImageUrl(url);
}

QString TbPcParsers::emoticonUrlById(const QString &id)
{
    const QString trimmed = id.trimmed();
    if (trimmed.isEmpty())
        return QString();

    /*
     * 正常情况下传进来的就是 "image_emoticon66" 这种。
     * 但也可能只给了数字（"66"），两种都容忍。
     */
    QString fileName = trimmed;
    if (fileName.indexOf(QLatin1String("image_emoticon")) < 0) {
        bool ok = false;
        const int n = fileName.toInt(&ok);
        if (! ok || n <= 0)
            return QString();
        fileName = QLatin1String("image_emoticon") + QString::number(n);
    }

    const QString url = QString(QLatin1String(kEmoticonBase)) + fileName
                        + QLatin1String(".png");
    return TbString::httpsImageUrl(url);
}

QString TbPcParsers::firstImageInContent(const TbJson &content)
{
    if (!content.isArray())
        return QString();

    for (int k = 0; k < content.size(); ++k) {
        const TbJson seg = content.at(k);
        if ((int)TbJson::integer(seg, QLatin1String("type"), 0) != 3)
            continue;
        QString src = TbJson::str(seg, QLatin1String("origin_src"));
        if (src.isEmpty())
            src = TbJson::str(seg, QLatin1String("cdn_src"));
        if (src.isEmpty())
            src = TbJson::str(seg, QLatin1String("origin_pic_url"));
        if (!src.isEmpty())
            return TbString::httpsImageUrl(src);
    }
    return QString();
}

/* ============================ 单个贴子（吧页 feed） ============================ */

TbThread *TbPcParsers::parseFeedThread(const TbJson &feedItem)
{
    if (TbJson::str(feedItem, QLatin1String("layout")) != QLatin1String("feed"))
        return 0;

    const TbJson feed = TbJson::object(feedItem, QLatin1String("feed"));
    if (!feed.isObject())
        return 0;

    TbThread *thread = new TbThread();
    thread->setIsVideo(TbThread::parseIsVideo(feed));

    /*
     * ---- feed 的元信息都在 business_info_map 里 ----
     *
     * ★ 实测：PC 版的 is_good / thread_id / view_num / create_time 都在
     *   feed.business_info_map，而不是像名字暗示的那样散在 feed_social 里。
     *   （用真实响应 pc_frs.json 逐层翻出来的。）
     *
     * ★ 这里以前**完全没读 is_good** —— 后果是：即使服务端按 is_good=1 筛出了
     *   精品贴，前端也标记不出来，界面上看起来"精华筛选不生效"。
     *   这就是那个 bug 的真正原因（不是参数传错，是结果没解析）。
     */
    const TbJson biz = TbJson::object(feed, QLatin1String("business_info_map"));
    if (biz.isObject()) {
        // PC feed 的作者 ID 用于点击作者资料和筛选已加载的用户帖子。
        thread->setAuthorId(TbJson::str(biz, QLatin1String("user_id")));
        thread->setIsGood(TbJson::integer(biz, QLatin1String("is_good"), 0) != 0);

        /*
         * 视频贴标记（设置页"隐藏视频贴"要用）。
         *
         * parseIsVideo 同时读取 business_info_map.is_video 和 feed_video
         * 组件，避免服务端省略 is_video 时漏掉视频。
         */
        // parseIsVideo also accepts feed_video components when is_video is absent.

        // thread_id 在 business_info_map 里一定有；feed_social.tid 作为兜底
        const QString tid = TbJson::str(biz, QLatin1String("thread_id"));
        if (! tid.isEmpty())
            thread->setId(tid);

        // 浏览数：之前一直留空（以为 PC 接口没给），其实这里就有
        thread->setViewCount(
            (int)TbJson::integer(biz, QLatin1String("view_num"), 0));

        // 创建时间（Unix 秒）。feed_head 里的时间文本有时解析不出来，
        // 这个数字更可靠，所以优先用它。
        const qint64 created = TbJson::integer(biz, QLatin1String("create_time"), 0);
        if (created > 0)
            thread->setLastTimeText(TbString::formatUnixTime(created));
    }

    // ---- 标题 ----
    // 注意 componentOf 返回的是整个 component 项，形如
    //   {"component":"feed_title","feed_title":{"data":[...]}}
    // 所以还要再取一层同名内层对象。
    const TbJson titleComp = componentOf(feed, QLatin1String("feed_title"));
    if (titleComp.isObject()) {
        const TbJson inner = TbJson::object(titleComp, QLatin1String("feed_title"));
        thread->setTitle(textOfDataArray(TbJson::array(inner, QLatin1String("data"))));
    }

    // ---- 摘要 ----
    const TbJson abstractComp = componentOf(feed, QLatin1String("feed_abstract"));
    if (abstractComp.isObject()) {
        const TbJson inner = TbJson::object(abstractComp, QLatin1String("feed_abstract"));
        thread->setAbstractText(
            textOfDataArray(TbJson::array(inner, QLatin1String("data"))));
    }

    // ---- 作者 / 吧头衔 / 时间 / 头像 ----
    const TbJson headComp = componentOf(feed, QLatin1String("feed_head"));
    if (headComp.isObject()) {
        const TbJson head = TbJson::object(headComp, QLatin1String("feed_head"));
        const TbJson mainData = TbJson::array(head, QLatin1String("main_data"));

        // 作者名（icon_type == name_show）
        const TbJson nameItem =
            findMainDataByIconType(mainData, QLatin1String(kIconTypeNameShow));
        if (nameItem.isObject()) {
            thread->setAuthorName(
                TbJson::str(TbJson::object(nameItem, QLatin1String("text")),
                            QLatin1String("text")));
        }

        // 吧头衔（icon_type == forum_level）：藏在 custom.data 这个
        // 二次转义的 JSON 字符串里
        const TbJson levelItem =
            findMainDataByIconType(mainData, QLatin1String(kIconTypeForumLevel));
        if (levelItem.isObject()) {
            const QString customData =
                TbJson::str(TbJson::object(levelItem, QLatin1String("custom")),
                            QLatin1String("data"));
            thread->setAuthorLabel(
                nestedJsonValue(customData, QLatin1String("level_name")));
        }

        // 头像
        const TbJson imageData = TbJson::object(head, QLatin1String("image_data"));
        if (imageData.isObject()) {
            thread->setAvatarUrl(
                TbString::httpsImageUrl(TbJson::str(imageData, QLatin1String("img_url"))));
        }

        // 时间：extra_data 里能解析成 Unix 秒的那个 text
        const TbJson extraData = TbJson::array(head, QLatin1String("extra_data"));
        for (int i = 0; i < extraData.size(); ++i) {
            const QString t =
                TbJson::str(TbJson::object(extraData.at(i), QLatin1String("text")),
                            QLatin1String("text"));
            bool ok = false;
            const qint64 secs = t.toLongLong(&ok);
            if (ok && secs > 0) {
                thread->setLastTimeText(TbString::formatUnixTime(secs));
                break;
            }
        }
    }

    // ---- 图片（只取第一张，列表用） ----
    const TbJson picComp = componentOf(feed, QLatin1String("feed_pic"));
    if (picComp.isObject()) {
        const TbJson inner = TbJson::object(picComp, QLatin1String("feed_pic"));
        const TbJson pics = TbJson::array(inner, QLatin1String("pics"));
        if (pics.size() > 0) {
            const TbJson pic0 = pics.at(0);
            QString url = TbJson::str(pic0, QLatin1String("origin_pic_url"));
            if (url.isEmpty())
                url = TbJson::str(pic0, QLatin1String("small_pic_url"));
            if (url.isEmpty())
                url = TbJson::str(pic0, QLatin1String("big_pic_url"));
            thread->setFirstImageUrl(TbString::httpsImageUrl(url));
        }
    }

    // ---- tid / 回复数 ----
    const TbJson socialComp = componentOf(feed, QLatin1String("feed_social"));
    if (socialComp.isObject()) {
        const TbJson social = TbJson::object(socialComp, QLatin1String("feed_social"));
        // tid 只作兜底：上面 business_info_map.thread_id 更可靠
        if (thread->id().isEmpty())
            thread->setId(TbJson::str(social, QLatin1String("tid")));
        thread->setReplyCount(
            (int)TbJson::integer(social, QLatin1String("comment_num"), 0));
        thread->setAgreeCount(TbThread::parseAgreeCount(social));
    }
    if (!thread->hasAgreeCount()) thread->setAgreeCount(TbThread::parseAgreeCount(biz));
    if (!thread->hasAgreeCount()) thread->setAgreeCount(TbThread::parseAgreeCount(feed));

    /*
     * 浏览数的展示文本：上面已经从 business_info_map 取到数字了
     * （以前这里写死留空，因为以为 PC 接口不给 —— 那是错的）。
     */
    if (thread->viewCount() > 0)
        thread->setViewCountText(TbString::shortNumber(thread->viewCount()));

    return thread;
}

/* ============================ 吧页列表 ============================ */

TbPcForumPageResult TbPcParsers::parseForumPage(const TbJson &root)
{
    TbPcForumPageResult result;

    QString code;
    QString message;
    if (fillError(root, &code, &message)) {
        result.errorCode = code;
        result.errorMessage = message;
        return result;
    }

    // ---- 吧信息 ----
    const TbJson forum = TbJson::object(root, QLatin1String("forum"));
    if (forum.isObject()) {
        TbForumInfo *info = new TbForumInfo();
        info->setId(TbJson::str(forum, QLatin1String("id")));
        info->setName(TbJson::str(forum, QLatin1String("name")));
        info->setUserLevel((int)TbJson::integer(forum, QLatin1String("user_level"), 0));
        info->setLiked(TbJson::integer(forum, QLatin1String("is_like"), 0) != 0);
        info->setAvatarUrl(
            TbString::httpsImageUrl(TbJson::str(forum, QLatin1String("avatar"))));
        info->setSlogan(TbJson::str(forum, QLatin1String("slogan")));
        info->setMemberCountText(TbString::shortNumber(
            TbJson::integer(forum, QLatin1String("member_num"), 0)));
        info->setThreadCountText(TbString::shortNumber(
            TbJson::integer(forum, QLatin1String("thread_num"), 0)));
        /*
         * 吧详情页要的字段（实测都在 pc_frs.json 的 forum 对象里）：
         *   post_num      贴子总数（含回复）
         *   cur_score     当前账号在本吧的经验值
         *   levelup_score 升到下一级所需经验
         *   level_name    等级名（这个吧是 "FlowMind" 这种自定义名）
         */
        info->setPostCountText(TbString::shortNumber(
            TbJson::integer(forum, QLatin1String("post_num"), 0)));
        info->setScore((int)TbJson::integer(forum, QLatin1String("cur_score"), 0));
        info->setLevelupScore((int)TbJson::integer(forum, QLatin1String("levelup_score"), 0));
        info->setLevelName(TbJson::str(forum, QLatin1String("level_name")));
        result.forumInfo = info;
    }

    // ---- 分页 ----
    const TbJson page = TbJson::object(root, QLatin1String("page"));
    if (page.isObject()) {
        TbPageInfo *pi = new TbPageInfo();
        pi->setCurrentPage((int)TbJson::integer(page, QLatin1String("current_page"), 1));
        pi->setTotalPage((int)TbJson::integer(page, QLatin1String("total_page"), 0));
        pi->setPageSize((int)TbJson::integer(page, QLatin1String("page_size"), 0));
        pi->setTotalCount((int)TbJson::integer(page, QLatin1String("total_count"), 0));
        pi->setHasMore(TbJson::integer(page, QLatin1String("has_more"), 0) != 0);
        pi->setHasPrev(TbJson::integer(page, QLatin1String("has_prev"), 0) != 0);
        result.pageInfo = pi;
    }

    /*
     * ---- 当前登录用户（多账号用）----
     *
     * 实测响应里 `user` 是**当前登录者**（不是吧务、不是楼主），字段：
     *     user.id / user.name / user.name_show / user.portrait
     * 顺带把 anti.tbs 取出来（发帖/签到要用的票据）。
     *
     * ★ 只要这两个字段存在就填，没有就留空 —— 未登录时响应里没有 user，
     *   调用方据此判断"这份凭证是不是还有效"。
     */
    const TbJson userNode = TbJson::object(root, QLatin1String("user"));
    if (userNode.isObject()) {
        const QString id = TbJson::str(userNode, QLatin1String("id"));
        if (! id.isEmpty()) {
            TbUser *user = new TbUser();
            user->setId(id);
            user->setName(TbJson::str(userNode, QLatin1String("name")));
            user->setNameShow(TbJson::str(userNode, QLatin1String("name_show")));
            user->setPortrait(TbJson::str(userNode, QLatin1String("portrait")));
            user->setAvatarUrl(TbString::avatarUrl(user->portrait()));
            result.user = user;
        }
    }
    result.tbs = TbJson::str(TbJson::object(root, QLatin1String("anti")),
                             QLatin1String("tbs"));

    // ---- feed_list ----
    const TbJson pageData = TbJson::object(root, QLatin1String("page_data"));
    const TbJson feedList = TbJson::array(pageData, QLatin1String("feed_list"));
    for (int i = 0; i < feedList.size(); ++i) {
        TbThread *t = parseFeedThread(feedList.at(i));
        if (t)
            result.threads.append(t);
    }

    return result;
}

/* ============================ 楼层公用逻辑 ============================ */

namespace {

/*! 用节点里的用户信息填充一个 TbUser */
TbUser *userFromNode(const TbJson &u)
{
    TbUser *user = new TbUser();
    user->setId(TbJson::str(u, QLatin1String("id")));
    user->setName(TbJson::str(u, QLatin1String("name")));
    user->setNameShow(TbJson::str(u, QLatin1String("name_show")));
    user->setPortrait(TbJson::str(u, QLatin1String("portrait")));
    user->setAvatarUrl(TbString::avatarUrl(user->portrait()));
    user->setLevel((int)TbJson::integer(u, QLatin1String("level_id"), 0));
    user->setIsBawu(TbJson::integer(u, QLatin1String("is_bawu"), 0) != 0);
    user->setBawuType(TbJson::str(u, QLatin1String("bawu_type")));
    user->setIpAddress(TbJson::str(u, QLatin1String("ip_address")));
    return user;
}

/*!
 * 把一层楼/一条楼中楼的 json 节点转成 TbPost。
 *
 * post_list[]、first_floor、subpost_list[] 三处节点形状基本一致，
 * 差别只在作者信息来源：
 *   - post_list[]   只有 author_id，要去 user_list 查表
 *   - first_floor   同上
 *   - subpost_list[] 自带完整 author 对象
 *
 * @param threadAuthorId 楼主（1 楼作者）的 author_id。
 *        ★ 服务端**不提供**"这层是不是楼主"的字段 —— 实测 first_floor 和
 *          post_list[] 的键完全一样，只有 author_id / floor / content …… 没有
 *          is_lz 之类。所以只能拿每层的 author_id 和 1 楼的比对。
 *          这就是楼主图标之前不显示的原因：**这里从来没设过 isOriginalPoster**。
 *        传空串表示不判定（例如解析楼中楼时不需要）。
 */
TbPost *postFromNode(const TbJson &p, const QMap<QString, TbUser *> *userIndex,
                     QSet<QString> *usedAuthorIds,
                     const QString &threadAuthorId = QString())
{
    TbPost *post = new TbPost();

    post->setId(TbJson::str(p, QLatin1String("id")));
    post->setFloor(QString::number(TbJson::integer(p, QLatin1String("floor"), 0)));
    post->setAgreeCount((int)TbJson::integer(p, QLatin1String("agree"), 0));
    post->setDisagreeCount((int)TbJson::integer(p, QLatin1String("disagree"), 0));
    post->setSubPostCount((int)TbJson::integer(p, QLatin1String("sub_post_number"), 0));

    const qint64 t = TbJson::integer(p, QLatin1String("time"), 0);
    if (t > 0)
        post->setTimeText(TbString::formatUnixTime(t));

    // 作者
    const TbJson author = TbJson::object(p, QLatin1String("author"));
    if (author.isObject()) {
        post->setAuthor(userFromNode(author));
    } else if (userIndex) {
        const QString authorId = TbJson::str(p, QLatin1String("author_id"));
        if (userIndex->contains(authorId)) {
            TbUser *indexed = userIndex->value(authorId);
            if (usedAuthorIds) {
                post->setAuthor(indexed);
                usedAuthorIds->insert(authorId);
            } else {
                // Previews own their author copy; the root index is cleaned up
                // after parsing and may contain users absent from main floors.
                TbUser *copy = new TbUser(post);
                copy->setId(indexed->id());
                copy->setName(indexed->name());
                copy->setNameShow(indexed->nameShow());
                copy->setPortrait(indexed->portrait());
                copy->setAvatarUrl(indexed->avatarUrl());
                copy->setLevel(indexed->level());
                copy->setIsBawu(indexed->isBawu());
                copy->setBawuType(indexed->bawuType());
                copy->setIpAddress(indexed->ipAddress());
                post->setAuthor(copy);
            }
        }
    }
    if (!post->author()) {
        const QString authorId = TbJson::str(p, QLatin1String("author_id"));
        if (!authorId.isEmpty()) {
            TbUser *fallback = new TbUser(post);
            fallback->setId(authorId);
            post->setAuthor(fallback);
        }
    }

    /*
     * ★ 楼主判定：拿本层的 author_id 和 1 楼的比对。
     *
     * 注意这里**读的是 json 里的 author_id**，不是 post->author()->id() ——
     * 因为 user_list 里可能查不到作者（比如被屏蔽的用户），
     * 那时 post->author() 是空，但 author_id 仍然在，判定照样成立。
     */
    if (! threadAuthorId.isEmpty()) {
        const QString authorId = TbJson::str(p, QLatin1String("author_id"));
        if (! authorId.isEmpty() && authorId == threadAuthorId)
            post->setIsOriginalPoster(true);
    }

    // 正文
    const TbJson content = TbJson::array(p, QLatin1String("content"));
    post->setContentSegments(TbPcParsers::parseContentSegments(content));

    // 楼中楼预览：post_list 里已经内嵌了 sub_post_list.sub_post_list[]，
    // 列表页可以直接显示前几条，不必再发一次请求。
    // 注意 TbPost::setSubPosts 存的是**原始指针**（children() 与内存释放都依赖
    // 它），不是 map。
    const TbJson subWrap = TbJson::object(p, QLatin1String("sub_post_list"));
    const TbJson subList = TbJson::array(subWrap, QLatin1String("sub_post_list"));
    if (subList.size() > 0) {
        QVariantList subPointers;
        for (int i = 0; i < subList.size(); ++i) {
            // 楼中楼里楼主自己说的话也要能标出来，所以 threadAuthorId 继续往下传
            TbPost *sp = postFromNode(subList.at(i), userIndex, 0, threadAuthorId);
            if (!sp)
                continue;
            sp->setParent(post);   // 由父楼层负责释放
            subPointers.append(QVariant::fromValue(sp));
        }
        post->setSubPosts(subPointers);
    }

    // 没有正文时用标题兜底，避免界面空白
    if (post->plainText().isEmpty()) {
        const QString title = TbJson::str(p, QLatin1String("title"));
        if (!title.isEmpty())
            post->setPlainText(title);
    }

    return post;
}

} // anonymous namespace

/* ============================ 帖子楼层 ============================ */

TbPcThreadResult TbPcParsers::parseThread(const TbJson &root)
{
    TbPcThreadResult result;

    QString code;
    QString message;
    if (fillError(root, &code, &message)) {
        result.errorCode = code;
        result.errorMessage = message;
        return result;
    }

    // ---- 作者索引 ----
    // post_list[] / first_floor 里**没有**内嵌 author，只有 author_id；
    // 作者详情（昵称/头像/等级/头衔/吧务/ip）都在 user_list 里，必须查表。
    QMap<QString, TbUser *> users;
    const TbJson userList = TbJson::array(root, QLatin1String("user_list"));
    for (int i = 0; i < userList.size(); ++i) {
        TbUser *user = userFromNode(userList.at(i));
        users.insert(user->id(), user);
    }

    // ---- 主题信息 ----
    const TbJson thread = TbJson::object(root, QLatin1String("thread"));
    if (thread.isObject()) {
        TbThreadInfo *info = new TbThreadInfo();
        info->setId(TbJson::str(thread, QLatin1String("id")));
        info->setTitle(TbJson::str(thread, QLatin1String("title")));
        info->setReplyCount((int)TbJson::integer(thread, QLatin1String("reply_num"), 0));
        result.threadInfo = info;
    }

    // ---- 分页 ----
    const TbJson page = TbJson::object(root, QLatin1String("page"));
    if (page.isObject()) {
        TbPageInfo *pi = new TbPageInfo();
        pi->setCurrentPage((int)TbJson::integer(page, QLatin1String("current_page"), 1));
        pi->setTotalPage((int)TbJson::integer(page, QLatin1String("total_page"), 0));
        pi->setPageSize((int)TbJson::integer(page, QLatin1String("page_size"), 0));
        pi->setTotalCount((int)TbJson::integer(page, QLatin1String("total_count"), 0));
        pi->setHasMore(TbJson::integer(page, QLatin1String("has_more"), 0) != 0);
        result.pageInfo = pi;
    }

    QSet<QString> usedAuthorIds;

    /*
     * 楼主 = 1 楼的作者。先把 author_id 取出来，后面每层都用它比对。
     * （服务端没有现成的"是否楼主"字段，见 postFromNode 的说明。）
     */
    const TbJson firstFloor = TbJson::object(root, QLatin1String("first_floor"));
    const QString threadAuthorId =
        firstFloor.isObject() ? TbJson::str(firstFloor, QLatin1String("author_id"))
                              : QString();

    // ---- 1 楼 ----
    // ★ 1 楼**不在** post_list 里（post_list 的 floor 从 2 开始），
    //   它是单独的 first_floor 对象。不解析它，正文（楼主那层）就永远不显示。
    if (firstFloor.isObject())
        result.posts.append(postFromNode(firstFloor, &users, &usedAuthorIds,
                                         threadAuthorId));

    // ---- 其余楼层 ----
    // ★ 也传 threadAuthorId：post_list[] 里楼主自己回复的楼层同样要打楼主标。
    /*
     * ★★ 楼层号兜底（治"除 1 楼外全显示 0 楼"）。
     *
     * 背景：post_list[] 里一般**带** `floor`（pc_pb.json 里是 2,4,5,6...），
     *      但实测有些贴子的响应里这个字段缺失/为 0 —— 那时 postFromNode 会
     *      兜底成 0，界面就显示"第0楼"。
     *
     * 处理：服务端值有效就用它；无效则**按顺序推算**（1 楼之后是 2,3,4...）。
     *      推算只是退路，不覆盖任何有效值。
     *
     * 同时打一行日志（缺字段时才有），下次一看日志就知道是不是这个原因。
     */
    const TbJson postList = TbJson::array(root, QLatin1String("post_list"));
    int missingFloor = 0;
    for (int i = 0; i < postList.size(); ++i) {
        TbPost *post = postFromNode(postList.at(i), &users, &usedAuthorIds,
                                    threadAuthorId);
        if (post && post->floorNumber() <= 0) {
            ++missingFloor;
            // 1 楼占掉第 1 位，所以 post_list[i] 的推算楼层是 i + 2
            post->setFloor(QString::number(i + 2));
        }
        result.posts.append(post);
    }
    if (missingFloor > 0) {
        qWarning("TbPcParsers: post_list 有 %d/%d 层**没有有效 floor 字段**，"
                 "已按顺序推算楼层号（这就是界面显示 0 楼的原因）",
                 missingFloor, postList.size());
    }

    // ---- 释放没被引用的用户对象 ----
    // TbPost 只保存扁平化的作者字段，没有反向引用，所以这里自己记录用过的 key。
    for (QMap<QString, TbUser *>::iterator it = users.begin(); it != users.end(); ++it) {
        if (!usedAuthorIds.contains(it.key()))
            delete it.value();
    }

    return result;
}

/* ============================ 楼中楼 ============================ */

TbPcSubFloorResult TbPcParsers::parseSubFloors(const TbJson &root)
{
    TbPcSubFloorResult result;

    QString code;
    QString message;
    if (fillError(root, &code, &message)) {
        result.errorCode = code;
        result.errorMessage = message;
        return result;
    }

    result.subPostNum = (int)TbJson::integer(root, QLatin1String("subpost_num"), 0);

    const TbJson page = TbJson::object(root, QLatin1String("page"));
    if (page.isObject()) {
        TbPageInfo *pi = new TbPageInfo();
        pi->setCurrentPage((int)TbJson::integer(page, QLatin1String("current_page"), 1));
        pi->setTotalPage((int)TbJson::integer(page, QLatin1String("total_page"), 0));
        pi->setPageSize((int)TbJson::integer(page, QLatin1String("page_size"), 0));
        pi->setTotalCount((int)TbJson::integer(page, QLatin1String("total_count"), 0));
        pi->setHasMore(TbJson::integer(page, QLatin1String("has_more"), 0) != 0);
        result.pageInfo = pi;
    }

    // 注意字段名：楼中楼是 subpost_list（连写），与楼层的 post_list 不同。
    // 每条自带完整 author 对象，所以不需要 user_list 查表。
    const TbJson subList = TbJson::array(root, QLatin1String("subpost_list"));
    for (int i = 0; i < subList.size(); ++i)
        result.posts.append(postFromNode(subList.at(i), 0, 0));

    return result;
}

} // namespace tieba
