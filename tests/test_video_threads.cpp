#include "tieba/model/TbItems.hpp"
#include "tieba/model/TbParsers.hpp"
#include "tieba/model/TbPcParsers.hpp"
#include "tieba/api/TbApi.hpp"
#include <QCoreApplication>
#include <stdio.h>
using namespace tieba;
static int checks = 0, failures = 0;
static void check(const char *label, bool ok) {
    ++checks; if (!ok) ++failures; printf("%s %s\n", ok ? "OK" : "FAIL", label);
}
static bool video(const char *fixture) { return TbThread::parseIsVideo(TbJson::parse(fixture)); }
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    check("plain thread is not video", !video("{}"));
    check("false flag is not video", !video("{\"is_video\":\"0\"}"));
    check("missing video metadata is not video", !video("{\"video_info\":null}"));
    check("image media type 3 is not video", !video("{\"media\":[{\"type\":3,\"big_pic\":\"https://image.invalid/p.jpg\"}]}"));
    check("image feed component is not video", !video("{\"components\":[{\"component\":\"feed_pic\",\"feed_pic\":{}}]}"));
    check("explicit flag video", video("{\"is_video\":1}"));
    check("string flag video", video("{\"is_video\":\"1\"}"));
    check("boolean flag video", video("{\"is_video\":true}"));
    check("client video metadata", video("{\"video_info\":{\"video_url\":\"https://video.invalid/v.mp4\"}}"));
    check("non-null client metadata matches TiebaLite", video("{\"video_info\":{}}"));
    check("PC business flag", video("{\"business_info_map\":{\"is_video\":1}}"));
    check("PC video component without business flag", video("{\"components\":[{\"component\":\"feed_video\",\"feed_video\":{\"video_url\":\"https://video.invalid/v.mp4\"}}]}"));
    check("PC component overrides absent false flag", video("{\"business_info_map\":{\"is_video\":0},\"components\":[{\"component\":\"feed_video\"}]}"));
    check("feed wrapper video", video("{\"feed\":{\"components\":[{\"component\":\"feed_video\"}]}}"));

    TbForumPageResult mobile = TbParsers::parseForumPage(TbJson::parse("{\"error_code\":0,\"thread_list\":[{\"id\":\"1\",\"video_info\":{}},{\"id\":\"2\",\"video_info\":null,\"media\":[{\"type\":3}]}]}"));
    check("mobile parser identifies video", mobile.threads.size() == 2 && mobile.threads.at(0)->isVideo());
    check("mobile parser preserves image thread", mobile.threads.size() == 2 && !mobile.threads.at(1)->isVideo());
    if (mobile.threads.size() == 2) check("variant preserves video flag for filtering", mobile.threads.at(0)->toVariantMap().value("isVideo").toBool()
        && !mobile.threads.at(1)->toVariantMap().value("isVideo").toBool());
    qDeleteAll(mobile.threads); delete mobile.forumInfo; delete mobile.pageInfo;
    TbPcForumPageResult pc = TbPcParsers::parseForumPage(TbJson::parse("{\"error_code\":0,\"page_data\":{\"feed_list\":[{\"layout\":\"feed\",\"feed\":{\"business_info_map\":{\"thread_id\":\"1\"},\"components\":[{\"component\":\"feed_video\",\"feed_video\":{}}]}},{\"layout\":\"feed\",\"feed\":{\"business_info_map\":{\"thread_id\":\"2\",\"is_video\":0},\"components\":[{\"component\":\"feed_pic\",\"feed_pic\":{}}]}}]}}"));
    check("PC parser identifies component-only video", pc.threads.size() == 2 && pc.threads.at(0)->isVideo());
    check("PC parser preserves image thread", pc.threads.size() == 2 && !pc.threads.at(1)->isVideo());
    qDeleteAll(pc.threads); delete pc.forumInfo; delete pc.pageInfo; delete pc.user;
    const QVariantList stream = TbApiService::parseStream("recommend", TbJson::parse("{\"error_code\":0,\"thread_list\":[{\"tid\":\"1\",\"video_info\":{}},{\"tid\":\"2\",\"media\":[{\"type\":3}]}]}")).value("items").toList();
    check("recommendation metadata detects video", stream.size() == 2 && stream.at(0).toMap().value("isVideo").toBool());
    check("recommendation image media stays visible", stream.size() == 2 && !stream.at(1).toMap().value("isVideo").toBool());
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
