/*
 * 设置页
 *
 * 布局是你写的（外观 / 缓存 / 内容 三段），这里只做"接线"：
 *   主题      -> Application.themeSupport.setVisualStyle() + 存进 tieba.theme
 *   缓存上限  -> tieba.cacheLimitMb（C++ 侧按上限回收最久没用的图）
 *   缓存占用  -> tieba.cacheUsageText（"已用 X MB / 512 MB（Y%）"）
 *   清除缓存  -> tieba.clearImageCache()
 *   隐藏视频贴 -> tieba.hideVideoThreads（吧页面列表装配时过滤）
 *   只看关注吧 -> tieba.showFollowedOnly
 *
 * ⚠️ 三条约束（都是踩过的坑，别改回去）：
 *
 *   1) 不要写 `onThemeChanged` / `onCacheLimitMbChanged` 这类处理器。
 *      QML 的 onXxx 要求**组件自己**有 xxx 信号；tieba 是
 *      setContextProperty 注入的对象，它的信号不属于本页 —— 写了会让
 *      **整个页面加载失败**（真机：Cannot assign to non-existent property）。
 *      需要"跟着 C++ 变"的地方用绑定（如 selectedIndex: ... tieba.theme）。
 *
 *   2) 主题的值就是 Cascades 的 VisualStyle::Type（Bright=1 / Dark=2），
 *      不做二次映射 —— 免得 QML 和 C++ 两套数字对不上。
 *
 *   3) 下拉框创建时 onSelectedOptionChanged 会被动触发一次（selectedIndex
 *      从 -1 变 0）。所以写回设置前先判 uiReady，否则会把默认值
 *      （亮色 / 512MB）盖掉磁盘上真正存的值。
 */

import bb.cascades 1.4

Page {
    id: settingsPage

    /*! 所属导航栈（本页不 push，留作扩展；由 main.qml 传进来） */
    property variant nav: null

    /*!
     * UI 是否已经建好。
     * 见文件头第 3 条：还没建好时下拉框那次"被动触发"不能写回设置。
     */
    property bool uiReady: false
    property bool cacheAvailable: typeof tieba !== "undefined"
    property int cachePercent: cacheAvailable ? Math.max(0, Math.min(100, tieba.cacheUsagePercent)) : 0
    property string cacheSummary: cacheAvailable ? tieba.cacheUsageText : qsTr("已用 — / —")

    titleBar: TitleBar {
        title: qsTr("设置")
    }

    /*!
     * 把缓存上限（MB）换算成下拉框的下标。
     * 不在 QML 里写魔法数字：设置项只有 512 / 1024 / 2048 三档。
     */
    function cacheLimitIndex(mb) {
        if (mb >= 2048)
            return 2
        if (mb >= 1024)
            return 1
        return 0
    }

    /*!
     * 应用主题。
     *
     * Application 是 Cascades 提供的全局对象（官方 ThemeSwitch 示例就是
     * 这么用的）：
     *     Application.themeSupport.setVisualStyle(VisualStyle.Dark)
     * 这里用 if/else 而不是三元表达式：枚举当函数参数没问题，
     * 但写成 `setVisualStyle(a ? VisualStyle.X : VisualStyle.Y)` 在别的
     * 枚举**属性**上会踩坑（Qt 4.8 会把枚举当整数），统一用 if/else 最稳。
     */
    function applyTheme(value) {
        if (value == VisualStyle.Dark)
            Application.themeSupport.setVisualStyle(VisualStyle.Dark)
        else
            Application.themeSupport.setVisualStyle(VisualStyle.Bright)
    }

    onCreationCompleted: {
        // 进来时重新扫一次缓存目录：占用数字要和磁盘上的实际情况一致
        if (settingsPage.cacheAvailable) tieba.refreshCacheUsage()
        settingsPage.uiReady = true
    }

    ScrollView {
        scrollRole: ScrollRole.Main

        Container {
            layout: StackLayout {
                orientation: LayoutOrientation.TopToBottom
            }

            // ==================== 外观 ====================
            Header {
                title: qsTr("外观")
            }

            Container {
                layout: StackLayout {
                    orientation: LayoutOrientation.LeftToRight
                }
                topPadding: ui.du(1.0)
                bottomPadding: ui.du(1.0)
                rightPadding: ui.du(1.0)
                leftPadding: ui.du(1.0)

                DropDown {
                    title: qsTr("主题")

                    // 跟着已保存的设置走（改完设置再进设置页，选项是对的）
                    selectedIndex: (tieba.theme == VisualStyle.Dark) ? 1 : 0

                    options: [
                        Option {
                            text: qsTr("亮色")
                            value: VisualStyle.Bright
                        },
                        Option {
                            text: qsTr("暗色")
                            value: VisualStyle.Dark
                        }
                    ]

                    onSelectedOptionChanged: {
                        if (! settingsPage.uiReady)
                            return
                        if (! selectedOption)
                            return
                        settingsPage.applyTheme(selectedOption.value)
                        tieba.theme = selectedOption.value
                        console.log("SettingsPage: 主题 -> " + selectedOption.value)
                    }
                }
            }

            // ==================== 缓存 ====================
            Header {
                title: qsTr("缓存")
            }

            Container {
                topMargin: 5.0
                bottomMargin: 5.0

                horizontalAlignment: HorizontalAlignment.Fill
                leftMargin: ui.du(1.0)
                rightMargin: ui.du(1.0)
                leftPadding: ui.du(1.0)
                rightPadding: ui.du(1.0)

                topPadding: ui.du(1.0)
                bottomPadding: ui.du(1.0)

                Label {
                    text: settingsPage.cacheSummary
                    multiline: true
                    textStyle.fontSize: FontSize.Medium
                    textStyle.fontWeight: FontWeight.Bold
                    horizontalAlignment: HorizontalAlignment.Fill
                }
                ProgressIndicator {
                    fromValue: 0
                    toValue: 100
                    value: settingsPage.cachePercent
                    horizontalAlignment: HorizontalAlignment.Fill
                    topMargin: ui.du(0.75)
                    accessibility.name: qsTr("图片缓存使用比例")
                }
//                Label {
//                    text: settingsPage.cacheAvailable ? qsTr("已使用上限的 ") + settingsPage.cachePercent + "%" : qsTr("缓存统计暂不可用")
//                    textStyle.fontSize: FontSize.Small
//                    textStyle.color: Color.Gray
//                    bottomMargin: ui.du(1)
//                }
                DropDown {
                    title: qsTr("缓存上限")
                    enabled: settingsPage.cacheAvailable
                    selectedIndex: settingsPage.cacheAvailable ? settingsPage.cacheLimitIndex(tieba.cacheLimitMb) : 0

                    options: [
                        Option {
                            text: "512MB"
                            value: 512
                        },
                        Option {
                            text: "1G"
                            // 1G 按 1024MB 算，和 C++ 里的字节换算保持一致
                            value: 1024
                        },
                        Option {
                            text: "2G"
                            value: 2048
                        }
                    ]

                    onSelectedOptionChanged: {
                        if (! settingsPage.uiReady || !settingsPage.cacheAvailable)
                            return
                        if (! selectedOption)
                            return
                        tieba.cacheLimitMb = selectedOption.value
                        tieba.refreshCacheUsage()
                        console.log("SettingsPage: 缓存上限 -> "
                                    + selectedOption.value + " MB")
                    }
                }

                Container {
                    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                    horizontalAlignment: HorizontalAlignment.Fill
                    topMargin: ui.du(1)
//                    Button {
//                        text: qsTr("刷新统计")
//                        enabled: settingsPage.cacheAvailable
//                        layoutProperties: StackLayoutProperties { spaceQuota: 1 }
//                        onClicked: tieba.refreshCacheUsage()
//                    }
                    Button {
                        text: qsTr("清除图片缓存")
                        enabled: settingsPage.cacheAvailable
                        layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                        leftMargin: ui.du(1)
                        onClicked: {
                            // C++ 那边清完会弹 Toast，并把占用数字刷新一遍
                            tieba.clearImageCache()
                            tieba.refreshCacheUsage()
                        }
                    }
                }

                Label {
                    text: qsTr("缓存超过上限时自动清理较旧的图片。")
                    multiline: true
                    topMargin: 10.0
                    horizontalAlignment: HorizontalAlignment.Center
                    textStyle.fontSize: FontSize.Small
                    textStyle.color: Color.Gray
                    textStyle.textAlign: TextAlign.Center
                }

            }

            // ==================== 内容 ====================
            Header {
                title: qsTr("内容")
            }

            Container {
                topPadding: ui.du(1.0)
                bottomPadding: ui.du(1.0)
                rightPadding: ui.du(1.0)
                leftPadding: ui.du(1.0)
                layout: StackLayout {
                    orientation: LayoutOrientation.LeftToRight
                }

                Label {
                    text: qsTr("隐藏视频贴")
                    verticalAlignment: VerticalAlignment.Center
                    layoutProperties: StackLayoutProperties {
                        spaceQuota: 1
                    }
                    textStyle {
                        base: SystemDefaults.TextStyles.BodyText
                    }
                }

                ToggleButton {
                    verticalAlignment: VerticalAlignment.Center
                    checked: settingsPage.cacheAvailable ? tieba.hideVideoThreads : false
                    enabled: settingsPage.cacheAvailable
                    onCheckedChanged: {
                        if (! settingsPage.uiReady || ! settingsPage.cacheAvailable)
                            return
                        tieba.hideVideoThreads = checked
                        console.log("SettingsPage: 隐藏视频贴 -> " + checked)
                    }
                }
            }

            Container {
                topPadding: ui.du(1.0)
                bottomPadding: ui.du(1.0)
                rightPadding: ui.du(1.0)
                leftPadding: ui.du(1.0)
                layout: StackLayout {
                    orientation: LayoutOrientation.LeftToRight
                }

                Label {
                    text: qsTr("推荐页只显示关注吧")
                    verticalAlignment: VerticalAlignment.Center
                    layoutProperties: StackLayoutProperties {
                        spaceQuota: 1
                    }
                    textStyle {
                        base: SystemDefaults.TextStyles.BodyText
                    }
                }

                ToggleButton {
                    verticalAlignment: VerticalAlignment.Center
                    checked: tieba.showFollowedOnly
                    onCheckedChanged: {
                        if (! settingsPage.uiReady)
                            return
                        tieba.showFollowedOnly = checked
                        console.log("SettingsPage: 推荐页只显示关注吧 -> " + checked)
                    }
                }
            }



        }
    }
}
