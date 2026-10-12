# ============================================================================
#  BBTieba - 项目级额外配置
#
#  为什么单独放一个文件：
#    config.pri 是 Momentics IDE 自动生成的，你在 IDE 里刷新项目/加文件时
#    它会被整个重写。所以「需要长期保留」的配置放这里，再由 BBTieba.pro 引入。
#
#  注意：如果你在 IDE 里新增了源文件，IDE 可能会按下方的通配规则之外的方式
#  重新生成 config.pri。本文件用的是通配符，所以新增文件后只要能编译进来就
#  不用改这里。
# ============================================================================

# ---------------------------------------------------------------------------
# 1) 逻辑层源文件
#
#    config.pri 由 Momentics 生成，正常情况下已经包含了 src/ 下的文件。
#    但 IDE 有时不会自动扫描到新加的子目录，所以这里做兜底。
#
#    这块前后踩了三个 qmake 的坑，都有实测依据：
#
#      a) contains(SOURCES, TiebaController.cpp) 匹配不到
#         $$BASEDIR/src/TiebaController.cpp。qmake 的 contains() 是**未锚定的
#         整串正则匹配**，必须写成 .*TiebaController\.cpp.* 才可靠。
#      b) $$files() 返回**反斜杠**路径（d:\...\a.cpp），而 config.pri 里是正斜杠
#         （D:/.../a.cpp）。两者字符串不同会被当成两个文件，所以先统一成正斜杠。
#      c) $$unique() 在 Qt 4.8 这套 qmake 里是 **no-op**（实测原样返回），
#         完全不能用来去重。
#
#    结论：逐个文件判断，只追加缺失的那些。这样无论 IDE 生成的 config.pri
#    是全量、部分还是空的，最终列表都恰好包含每个文件一次。
# ---------------------------------------------------------------------------
BBTIEBA_EXISTING_SOURCES = $$replace(SOURCES, \\\\, /)
BBTIEBA_EXISTING_HEADERS = $$replace(HEADERS, \\\\, /)

BBTIEBA_TIEBA_SOURCES = \
    $$files($$BASEDIR/src/tieba/util/*.cpp) \
    $$files($$BASEDIR/src/tieba/net/*.cpp) \
    $$files($$BASEDIR/src/tieba/session/*.cpp) \
    $$files($$BASEDIR/src/tieba/model/*.cpp) \
    $$files($$BASEDIR/src/tieba/api/*.cpp) \
    $$quote($$BASEDIR/src/TiebaController.cpp)

BBTIEBA_TIEBA_HEADERS = \
    $$files($$BASEDIR/src/tieba/util/*.hpp) \
    $$files($$BASEDIR/src/tieba/net/*.hpp) \
    $$files($$BASEDIR/src/tieba/session/*.hpp) \
    $$files($$BASEDIR/src/tieba/model/*.hpp) \
    $$files($$BASEDIR/src/tieba/api/*.hpp) \
    $$quote($$BASEDIR/src/TiebaController.hpp)

# 先恢复成正斜杠形式（IDE 生成的 config.pri 就是这种写法）
SOURCES = $$BBTIEBA_EXISTING_SOURCES
HEADERS = $$BBTIEBA_EXISTING_HEADERS

BBTIEBA_ADDED_SOURCES =
for(BBTIEBA_FILE, BBTIEBA_TIEBA_SOURCES) {
    # basename() 必须内联进 contains()：qmake 的 for() 变量在函数参数里是 
    # 提前展开的，先赋给中间变量会拿到列表最后一个值，判断就全错了。
    !contains(SOURCES, .*$$basename(BBTIEBA_FILE).*) {
        SOURCES += $$BBTIEBA_FILE
        BBTIEBA_ADDED_SOURCES += X
    }
}

for(BBTIEBA_FILE, BBTIEBA_TIEBA_HEADERS) {
    !contains(HEADERS, .*$$basename(BBTIEBA_FILE).*) {
        HEADERS += $$BBTIEBA_FILE
    }
}

message(BBTieba: append.pri added $$size(BBTIEBA_ADDED_SOURCES) source(s); total sources = $$size(SOURCES))
# ---------------------------------------------------------------------------
# 2) 头文件搜索路径：代码里用 #include "net/TbSigner.hpp" 这种相对 src/tieba
#    的写法，所以要把 src/tieba 和 src 都加进来。
# ---------------------------------------------------------------------------
INCLUDEPATH += \
    $$quote($$BASEDIR/src) \
    $$quote($$BASEDIR/src/tieba)

# ---------------------------------------------------------------------------
# 3) zlib：贴吧接口会返回 gzip 压缩的响应，Qt 4.8 不会自动解压。
#    BB10 目标系统自带 libz，所以链上即可。
#    代码里用 TB_HAVE_ZLIB 宏控制，未定义时不发 Accept-Encoding，
#    服务端就不会返回压缩内容（本机桌面测试就是这么跑的）。
# ---------------------------------------------------------------------------
DEFINES += TB_HAVE_ZLIB
LIBS += -lz

# ---------------------------------------------------------------------------
# 3b) libbbsystem：bb::system::InvokeManager（用浏览器打开图片/外链）
#
#     不显式链接会在最后一步链接失败，报：
#         undefined reference to symbol 'bb::system::InvokeRequest::setUri(QString const&)'
#         libbbsystem.so.1: error adding symbols: DSO missing from command line
#
#     注意 -lbbsystem 必须放在使用它的目标文件之后；qmake 的 LIBS 会自动处理
#     顺序，所以写在这里就行。
# ---------------------------------------------------------------------------
LIBS += -lbbsystem

# ---------------------------------------------------------------------------
# 4) Qt 网络模块（QNetworkAccessManager 等）
# ---------------------------------------------------------------------------
QT += network

# ---------------------------------------------------------------------------
# 5) 中间文件位置
#
#    qmake 默认把 .obj/.moc 放在「Makefile 所在目录的上两级」（../.obj、../.moc）。
#    如果 Makefile 生成在工程根目录，那就会跑到工程外面去（D:\BBWorkspace\.obj），
#    既污染工作区又让 clean 清不干净。
#    显式指到工程内的隐藏目录，$$PWD 在 .pri 里就是工程根。
# ---------------------------------------------------------------------------
OBJECTS_DIR = $$PWD/.obj
MOC_DIR     = $$PWD/.moc
RCC_DIR     = $$PWD/.rcc
UI_DIR      = $$PWD/.ui

# ---------------------------------------------------------------------------
# 6) 编译告警：逻辑层代码在桌面 Qt 4.8.1 + gcc 4.4 上已做到零告警，
#    开高告警级别便于及早发现 BB10 工具链的差异。
# ---------------------------------------------------------------------------
QMAKE_CXXFLAGS += -Wall

# ---------------------------------------------------------------------------
# 7) 构建产物不要带进 translation 之外的东西（保持 bar 包干净）
# ---------------------------------------------------------------------------
QMAKE_CLEAN += $$PWD/tests/build/*
