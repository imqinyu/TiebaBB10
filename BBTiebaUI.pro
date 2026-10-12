APP_NAME = BBTiebaUI

CONFIG += qt warn_on cascades10

include(config.pri)

# 项目级额外配置：zlib / libbbsystem / 头文件路径 / 逻辑层源文件兜底。
# config.pri 由 Momentics IDE 自动生成且会被重写，所以长期配置放 append.pri。
include(append.pri)
