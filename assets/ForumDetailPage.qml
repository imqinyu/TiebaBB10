/*
 * 吧详情页（新文件）
 *
 * 显示：吧头像 / 吧名 / 关注人数 / 贴子数 / 吧简介 / 我的经验值
 *
 * 数据来源：tieba.currentForumDetail()（Q_INVOKABLE）
 *   它返回的是吧页接口里 forum 对象的 map，字段：
 *       name / avatarUrl / localAvatarPath / memberCountText /
 *       postCountText / threadCountText / slogan /
 *       userLevel / score / levelupScore / levelName / liked
 *
 * ⚠️ 两条硬性约束（都是踩过的坑）：
 *   1. 头像只能用 **localAvatarPath**（C++ 已下载好的 file:// 路径）。
 *      BB10 的图片加载器不支持任何网络方案，直接给远程 URL 会
 *      "Image loading Unsupported scheme"。
 *   2. **这个页面里读数据要小心 map 为空**：如果没先 loadForumPage 过，
 *      currentForumDetail() 返回的是空 map。所以下面每个字段都用
 *      `detail.xxx ? detail.xxx : ""` 这种安全写法，不要让空值直接进 Label。
 *
 * UI 是先用最朴素的形式把数据摆出来 —— 用户说后面会单独改 UI，
 * 所以这里重点是"数据接对了"，不是好看。
 */

import bb.cascades 1.4

Page {
    id: forumDetailPage

    /*! 所属导航栈。由 InBaPage.qml 在 push 之前设置（本页不再 push，留作扩展）。 */
    property variant nav: null

    /*! 吧名。由 InBaPage.qml 在 push 之前设置（也用于标题）。 */
    property string forumName: ""

    /*!
     * 吧信息（响应式）。
     *
     * ★★ 这里踩过一个坑，改法别退回：
     *
     * 原来是在 onCreationCompleted 里取**一次快照**：
     *     onCreationCompleted: { detail = tieba.currentForumDetail() }
     * 结果从长按菜单进来时**永远是空白** —— 因为流程是
     *     tieba.loadForumPage(...)   // 异步发请求
     *     pushForumDetailFor(...)    // 立刻推页
     * 页面创建时 C++ 的 forumInfo 还是空的，快照就取到了空 map。
     * 这是**竞态条件**，取一次快照的写法注定不可靠。
     *
     * 在绑定块中读取 tieba.forumInfoVersion，让资料更新触发重新求值。
     * C++ 每次收到新的吧信息都会 ++forumInfoVersion 并 emit forumInfoChanged，
     * 绑定随之重新求值 —— 效果等价于监听信号。
     * （不用 onForumInfoChanged 的原因：QML 监听不到 context property 的信号，
     *   那会让整个页面加载失败。）
     */
    property variant detail: readDetail(typeof tieba === "undefined" ? 0 : tieba.forumInfoVersion)
    function readDetail(revision) {
        if (typeof tieba === "undefined") return {}
        return tieba.currentForumDetail()
    }



    ScrollView {
        scrollRole: ScrollRole.Main

        Container {
            layout: StackLayout {
            }
            topPadding: 24
            leftPadding: 24
            rightPadding: 24
            bottomPadding: 24

            // ---------------- 头像 + 吧名 ----------------
            Container {
                layout: StackLayout {
                    orientation: LayoutOrientation.LeftToRight
                }

                ImageView {
                    // ★ 只能绑本地路径（见文件头说明 1）
                    imageSource: forumDetailPage.detail.localAvatarPath
                                 ? forumDetailPage.detail.localAvatarPath : ""
                    scalingMethod: ScalingMethod.AspectFill
                    minWidth: 120.0
                    maxWidth: 120.0
                    minHeight: 120.0
                    maxHeight: 120.0
                }

                Container {
                    layout: StackLayout {
                    }
                    leftMargin: 20
                    verticalAlignment: VerticalAlignment.Center

                   // background: Color.Yellow
                    minWidth: 800.0
                    Label {
                        text: forumDetailPage.detail.name
                              ? forumDetailPage.detail.name : ""
                        textStyle.fontSize: FontSize.XLarge
                        textStyle.fontWeight: FontWeight.Bold
                    }

                    // 我的经验值 / 等级
                    Label {
                        text: qsTr("我的经验：")
                              + (forumDetailPage.detail.score != undefined
                                 ? forumDetailPage.detail.score : 0)
                        textStyle.fontSize: FontSize.Small
                        textStyle.color: Color.Gray
                        topMargin: 6
                    }

                    Label {
                        text: qsTr("贴吧等级：")
                              + (forumDetailPage.detail.userLevel != undefined
                                 ? forumDetailPage.detail.userLevel : 0)
                              + qsTr(" 级")
                              + (forumDetailPage.detail.levelName
                                 && forumDetailPage.detail.levelName.length > 0
                                 ? ("  (" + forumDetailPage.detail.levelName + ")")
                                 : "")
                        textStyle.fontSize: FontSize.Small
                        textStyle.color: Color.Gray
                    }
                }
            }

            // ---------------- 分隔 ----------------
            Container {
                topMargin: 20
                bottomMargin: 20
                preferredHeight: 1
              //  background: Color.create("#33000000")
                horizontalAlignment: HorizontalAlignment.Fill
            }

            // ---------------- 关注人数 / 贴子数 ----------------
            Container {
                layout: StackLayout {
                    orientation: LayoutOrientation.LeftToRight
                }

               // background: Color.Magenta
                verticalAlignment: VerticalAlignment.Top
                horizontalAlignment: HorizontalAlignment.Fill
                Container {
                    layout: StackLayout {
                    }
                    leftMargin: 0
                    rightMargin: 40

                 //   background: Color.Green
                    Label {
                        text: forumDetailPage.detail.memberCountText
                              ? forumDetailPage.detail.memberCountText : "-"
                        textStyle.fontSize: FontSize.Large
                        textStyle.fontWeight: FontWeight.Bold
                    }
                    Label {
                        text: qsTr("关注")
                        textStyle.fontSize: FontSize.Small
                        textStyle.color: Color.Gray
                    }
                }

                Container {
                    layout: StackLayout {
                    }
                    rightMargin: 40

                    Label {
                        text: forumDetailPage.detail.postCountText
                              ? forumDetailPage.detail.postCountText : "-"
                        textStyle.fontSize: FontSize.Large
                        textStyle.fontWeight: FontWeight.Bold
                    }
                    Label {
                        text: qsTr("贴子")
                        textStyle.fontSize: FontSize.Small
                        textStyle.color: Color.Gray
                    }
                }

                Container {
                    layout: StackLayout {
                    }

                    Label {
                        text: forumDetailPage.detail.threadCountText
                              ? forumDetailPage.detail.threadCountText : "-"
                        textStyle.fontSize: FontSize.Large
                        textStyle.fontWeight: FontWeight.Bold
                    }
                    Label {
                        text: qsTr("主题")
                        textStyle.fontSize: FontSize.Small
                        textStyle.color: Color.Gray
                    }
                }
            }

            // ---------------- 吧简介 ----------------
            Label {
                text: qsTr("简介")
                textStyle.fontSize: FontSize.Medium
                textStyle.fontWeight: FontWeight.Bold
                topMargin: 30
            }

            Label {
                text: (forumDetailPage.detail.slogan
                       && forumDetailPage.detail.slogan.length > 0)
                      ? forumDetailPage.detail.slogan
                      : qsTr("（这个吧没有简介）")
                multiline: true
                topMargin: 8
            }

            // ---------------- 数据没拿到时的提示 ----------------
//            Label {
//                visible: ! forumDetailPage.detail.name
//                         || forumDetailPage.detail.name.length == 0
//                text: qsTr("还没拿到吧信息 —— 请先进入这个吧的贴子列表再打开详情。")
//                multiline: true
//                topMargin: 24
//                textStyle.fontSize: FontSize.Small
//                textStyle.color: Color.Red
//            }
        }
    }
}
