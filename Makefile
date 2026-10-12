# ============================================================================
#  BBTiebaUI - 包装 Makefile
#
#  ── 这个文件解决什么问题 ──────────────────────────────────────────────────
#
#  .cproject 里每个构建配置都指定了目标名：
#      <builder autoBuildTarget="Device-Debug"
#               incrementalBuildTarget="Device-Debug" .../>
#  于是 IDE 在工程根执行 `make -j12 Device-Debug`。但 qmake 生成的 Makefile
#  **没有**这些目标（实测 5 种组合都不生成：设备 spec、模拟器 spec
#  blackberry-x86-qcc、debug_and_release、debug_and_release build_all、
#  recursive），也没有 sub- 递归目标。报错就是：
#      make: *** No rule to make target 'Device-Debug'.  Stop.
#  100~400ms 就退出，**代码一行都不编译**，界面永远停在旧版本。
#
#  这个包装 Makefile 把配置名映射到真实构建流程。
#
#  ── 两个必须显式指定的 CONFIG（缺一个就出难查的故障）─────────────────────
#
#  规则来自 NDK 的 cascades10.prf，原文：
#
#      TEMPLATE = app
#      device    { QNX_VARIANT = o.le-v7 }
#      simulator { QNX_VARIANT = o }
#      CONFIG(release, debug|release) { TEMPLATE = lib; DESTDIR = $$QNX_VARIANT }
#      CONFIG(debug,   debug|release) { DESTDIR = $$QNX_VARIANT-g }
#      OBJECTS_DIR = $${DESTDIR}/.obj
#
#  1) CONFIG+=debug（并 CONFIG-=release）
#     debug 和 release 都不指定时 Qt 默认命中 **release** 分支，TEMPLATE 变成
#     lib，编出来是共享库。部署时报：
#         Error: Failed to create application process: Attempting to exec a shared lib
#
#  2) CONFIG+=device（或 simulator）
#     两套 spec（blackberry-*-qcc、qnx-*-qcc）都**没有**定义 device/simulator
#     CONFIG，所以那两个块从来没命中，QNX_VARIANT 是空的：
#         DESTDIR = -g/        TARGET = -g/BBTiebaUI
#     产物跑进一个叫 "-g" 的怪目录。
#
#  ── 为什么生成在子目录、make 也在子目录执行 ────────────────────────────────
#     Makefile 里的 .pro 依赖是相对「生成它的目录」写的，所以生成和执行必须在
#     同一目录，否则报 "No rule to make target '../../BBTiebaUI.pro'"。
#
#  ── 为什么还要跑 fix-destdir.ps1 ──────────────────────────────────────────
#     DESTDIR / OBJECTS_DIR 是相对**工程根**写的（o.le-v7-g/），而 make 在
#     arm/o.le-v7-g/ 里执行，路径会再解析一次，多套一层同名目录：
#         arm/o.le-v7-g/o.le-v7-g/BBTieba
#     生成后把这两个变量改成当前目录，产物就正好落在
#         arm/o.le-v7-g/BBTiebaUI
#     与 bar-descriptor.xml 的要求一致，且不需要任何 post-build 复制。
#
#  ── 产物路径约定（来自 bar-descriptor.xml）─────────────────────────────────
#     Device-Debug    -> arm/o.le-v7-g/BBTiebaUI      （无 .so 后缀）
#     Device-Release  -> arm/o.le-v7/BBTiebaUI.so
#     Device-Profile  -> arm-p/o.le-v7-g/BBTiebaUI
#     Simulator-Debug -> x86/o-g/BBTiebaUI
#
#  ── 手工构建（不依赖 IDE）──────────────────────────────────────────────────
#     $ndk = 'C:\NDKFiles'
#     $env:QNX_HOST   = "$ndk\host_10_3_1_12\win32\x86"
#     $env:QNX_TARGET = "$ndk\target_10_3_1_995\qnx6"
#     $env:PATH = "$env:QNX_HOST\usr\bin;$env:PATH"
#     cd D:\BBWorkspace\BBTiebaUI
#     make -j4 Device-Debug
#
#  如果哪天 IDE 能自己生成这些目标，删掉本文件即可。
# ============================================================================

QNX_HOST   ?=
QNX_TARGET ?=
QMAKE      ?= $(QNX_HOST)/usr/bin/qmake.exe
PROFILE    ?= ../../BBTiebaUI.pro

SPEC_DEVICE    ?= $(QNX_TARGET)/usr/share/qt4/mkspecs/blackberry-armv7le-qcc
SPEC_SIMULATOR ?= $(QNX_TARGET)/usr/share/qt4/mkspecs/blackberry-x86-qcc

# 生成 Makefile 后修正 DESTDIR / OBJECTS_DIR（原因见文件头）
FIXDIRS = powershell -NoProfile -ExecutionPolicy Bypass -File tools/fix-destdir.ps1

first: Device-Debug

# --------------------------------------------------------------------------
#  在 $(1) 目录里生成 Makefile（$(2)=mkspec, $(3)=debug|release,
#  $(4)=device|simulator），修正路径后编译。产物由 Makefile 自己放到 $(1)/。
# --------------------------------------------------------------------------
define BUILD_IN
	@mkdir -p $(1)
	@echo "=== [$(1)] qmake ($(3)/$(4)) ==="
	@cd $(1) && "$(QMAKE)" -spec "$(2)" "$(PROFILE)" -o Makefile \
		"CONFIG+=$(3)" "CONFIG-=$(if $(filter $(3),debug),release,debug)" \
		"CONFIG+=$(4)" "CONFIG-=$(if $(filter $(4),device),simulator,device)"
	@$(FIXDIRS) $(1)/Makefile
	@echo "=== [$(1)] compile ==="
	@cd $(1) && $(MAKE) -f Makefile
endef

# ---------------------------------------------------------------- 配置目标
Device-Debug:
	$(call BUILD_IN,arm/o.le-v7-g,$(SPEC_DEVICE),debug,device)
	@echo "=== [Device-Debug] artifact: arm/o.le-v7-g/BBTiebaUI ==="

Device-Release:
	$(call BUILD_IN,arm/o.le-v7,$(SPEC_DEVICE),release,device)
	@echo "=== [Device-Release] artifact: arm/o.le-v7/BBTiebaUI.so ==="

Device-Profile:
	$(call BUILD_IN,arm-p/o.le-v7-g,$(SPEC_DEVICE),debug,device)
	@echo "=== [Device-Profile] artifact: arm-p/o.le-v7-g/BBTiebaUI ==="

Simulator-Debug:
	$(call BUILD_IN,x86/o-g,$(SPEC_SIMULATOR),debug,simulator)
	@echo "=== [Simulator-Debug] artifact: x86/o-g/BBTiebaUI ==="

# ---------------------------------------------------------------- 其它
all: Device-Debug

# 注意：recipe 行里的 '#' 不是 make 注释 —— make 会把整行交给 shell 执行，
# 中文注释会导致 "cannot execute binary file"。所以说明只能写在配方块外面。
clean:
	@powershell -NoProfile -ExecutionPolicy Bypass -File tools/clean-build.ps1

distclean: clean
	@powershell -NoProfile -ExecutionPolicy Bypass -File tools/clean-build.ps1 distclean

install dist: Device-Debug

.PHONY: first all Device-Debug Device-Release Device-Profile Simulator-Debug \
        clean distclean install dist
