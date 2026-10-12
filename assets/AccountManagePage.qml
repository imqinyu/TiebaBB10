/*
 * 账号管理页（多账号）
 *
 * 布局照 NeteaseMusic 的 AccountManagePage.qml 搬过来：
 *   「新增账号」进登录页；分「当前账号」「已登录」两段；
 *   每项长按 -> 复制凭据 / 删除账号（其它账号还能"登录此账号"）。
 *
 * 数据（都是 C++ 给的，见 TiebaController 的多账号一节）：
 *   tieba.currentAccount    当前账号（QVariantMap，未登录时是空 map）
 *   tieba.otherAccounts     其它已保存账号（ArrayDataModel）
 *   tieba.otherAccountCount 上面那个列表的条数（空状态提示用）
 *
 * ★★ 凭证的安全约定（和 NeteaseMusic 一致，别破例）：
 *   列表里拿到的只有**打码后**的 masked（"00AC6E94…4D5A"）；
 *   要复制明文 BDUSS 走 tieba.accountCredential(id)，取完立刻进剪贴板 ——
 *   明文不进模型、不打日志。
 *
 * ⚠️ 和别的独立页一样：ListItemComponent 里看不到 context property `tieba`，
 *    所以 ListView 上挂了 `property variant controller: tieba`。
 */

import bb.cascades 1.4

Page {
    id: accountPage
    objectName: "accountManagePage"

//    titleBar: TitleBar {
//        title: qsTr("账号管理")
//    }

    // 固定字段的视图快照，避免未登录时给原生属性赋 undefined。
    property variant accountView: readAccountView(typeof tieba === "undefined" ? 0 : tieba.accountsVersion,
                                                 typeof tieba === "undefined" ? 0 : tieba.imageCacheVersion)
    function readAccountView(accountRevision, imageRevision) {
        if (typeof tieba === "undefined") {
            return { id: "", uid: "", nickName: "", userName: "", masked: "", localAvatarPath: "" }
        }
        return tieba.currentAccountUi()
    }
    property string currentId: accountView.id
    property bool hasCurrent: currentId.length > 0
    actions: [
        ActionItem {
            title: qsTr("新增账号")
            imageSource: "asset:///icons/ic_add_contact.png"
            ActionBar.placement: ActionBarPlacement.Signature
            onTriggered: {
                /*
                 * ★ 独立页拿不到 main.qml 的 NavigationPane
                 *   （NeteaseMusic 踩过：子页里存 nav 对象会报
                 *     "Unable to set property"），所以走「请求 + 版本号」
                 *   让主界面去推登录页。
                 */
                tieba.requestOpenLogin()
            }
        }
    ]

    Container {
        layout: StackLayout {
            orientation: LayoutOrientation.TopToBottom
        }

        // ===================== 当前账号 =====================
        Header {
            title: qsTr("当前账号")
        }

        Container {
            leftPadding: 20
            rightPadding: 20
            topPadding: 10
            bottomPadding: 10
            layout: StackLayout {
                orientation: LayoutOrientation.LeftToRight
            }
            contextActions: [
                ActionSet {
                    title: accountPage.hasCurrent
                           ? accountPage.accountView.nickName : qsTr("当前账号")
                    subtitle: accountPage.hasCurrent
                              ? accountPage.accountView.masked : qsTr("未登录")
                    actions: [
                        ActionItem {
                            title: qsTr("复制凭据")
                            enabled: accountPage.hasCurrent
                            onTriggered: {
                                accountPage.copyCredential(accountPage.currentId)
                            }
                            imageSource: "asset:///icons/ic_copy.png"
                        },
                        ActionItem {
                            /*
                             * ★ 这里是"退出登录"，不是"删除账号"：
                             *   logout() 只清当前登录态，账号还留在下面的
                             *   「已登录」列表里，点一下就能登回来。
                             *   真要抹掉一个账号，用列表项长按里的「删除账号」。
                             */
                            title: qsTr("退出登录")
                            enabled: accountPage.hasCurrent
                            onTriggered: {
                                tieba.logout()
                            }
                            imageSource: "asset:///icons/ic_meeting_decline.png"
                        }
                    ]
                }
            ]

            ImageView {
                preferredWidth: 110
                preferredHeight: 110
                verticalAlignment: VerticalAlignment.Center
                scalingMethod: ScalingMethod.AspectFill
                /*
                 * ★ 头像一律用**本地缓存路径**（localAvatarPath）：
                 *   BB10 的 ImageView 不认 http/https（见 imagePath 的说明），
                 *   C++ 已经把头像下载好并把路径塞进这个字段了。
                 */
                imageSource: accountPage.hasCurrent
                             && accountPage.accountView.localAvatarPath
                             && accountPage.accountView.localAvatarPath.length > 0
                             ? accountPage.accountView.localAvatarPath
                             : "asset:///images/default_avatar.png"
            }
            Container {
                leftPadding: 15
                verticalAlignment: VerticalAlignment.Center
                layoutProperties: StackLayoutProperties {
                    spaceQuota: 1
                }
                layout: StackLayout {
                    orientation: LayoutOrientation.TopToBottom
                }
                Label {
                    text: accountPage.hasCurrent
                          ? accountPage.accountView.nickName : qsTr("未登录")
                    textStyle {
                        base: SystemDefaults.TextStyles.PrimaryText
                    }
                }
                Label {
                    text: accountPage.hasCurrent
                          ? accountPage.accountView.masked
                          : qsTr("登录后可以看关注、发帖、签到")
                    textStyle {
                        base: SystemDefaults.TextStyles.SmallText
                        color: Color.Gray
                    }
                }
            }
            Label {
                visible: accountPage.hasCurrent
                text: qsTr("当前")
                verticalAlignment: VerticalAlignment.Center
                textStyle {
                    base: SystemDefaults.TextStyles.SmallText
                    color: Color.create("#2d8cf0")
                }
            }
        }

        // ===================== 已登录（其它已保存的账号） =====================
        Header {
            title: qsTr("已登录")
        }

Container {
            topPadding: ui.du(1.0)
            leftPadding: ui.du(1.0)
            rightPadding: ui.du(1.0)
            bottomPadding: ui.du(1.0)
            // 空状态提示
    Label {
        visible: tieba.otherAccountCount == 0
        text: qsTr("没有其他已登陆的账号")
        multiline: true
        leftMargin: 20
        rightMargin: 20
        topMargin: 10
        bottomMargin: 10
        textStyle {
            base: SystemDefaults.TextStyles.SmallText
            color: Color.Gray
        }
    }

}
        ListView {
            id: otherList
            objectName: "accountListView"
            layoutProperties: StackLayoutProperties {
                spaceQuota: 1
            }
            visible: tieba.otherAccountCount > 0
            dataModel: tieba.otherAccounts
            property variant controller: tieba

            listItemComponents: [
                ListItemComponent {
                    type: ""

                    /*
                     * ★ Cascades 自带的列表项组件（和吧列表/楼层一致）：
                     *   按压高亮和分隔线由框架给，观感和系统其它列表统一；
                     *   默认属性是 content，里面那层布局直接写就行。
                     */
                    CustomListItem {
                        id: accItem

                        contextActions: [
                            ActionSet {
                                title: ListItemData.nickName
                                subtitle: ListItemData.masked
                                actions: [
                                    ActionItem {
                                        title: qsTr("复制凭据")
                                        onTriggered: {
                                            /*
                                             * ★ 全部走 controller：ListItemComponent
                                             *   是独立上下文，既看不到 tieba，
                                             *   也调不到本页的函数（accountPage.xxx
                                             *   在这里会是 ReferenceError）。
                                             */
                                            var c = accItem.ListItem.view.controller
                                            var text = c.accountCredential(String(ListItemData.id))
                                            if (text && text.length > 0)
                                                c.copyText(text)
                                            else
                                                c.showToast(qsTr("取不到这个账号的凭据"))
                                        }
                                        imageSource: "asset:///icons/ic_copy.png"
                                    },
                                    ActionItem {
                                        // 不是当前账号 -> 登录（切过去）
                                        title: qsTr("登录此账号")
                                        onTriggered: {
                                            var c = accItem.ListItem.view.controller
                                            if (c.switchAccount(String(ListItemData.id)))
                                                c.showToast(qsTr("正在切换到 ") + ListItemData.nickName + "…")
                                        }
                                        imageSource: "asset:///icons/ic_contact.png"
                                    },
                                    ActionItem {
                                        title: qsTr("删除账号")
                                        onTriggered: {
                                            var c = accItem.ListItem.view.controller
                                            c.removeAccount(String(ListItemData.id))
                                            c.showToast(qsTr("已删除该账号"))
                                        }
                                        imageSource: "asset:///icons/ic_delete.png"
                                    }
                                ]
                            }
                        ]

                        Container {
                            leftPadding: 20
                            rightPadding: 20
                            topPadding: 10
                            bottomPadding: 10
                            layout: StackLayout {
                                orientation: LayoutOrientation.LeftToRight
                            }

                            ImageView {
                                preferredWidth: 110
                                preferredHeight: 110
                                verticalAlignment: VerticalAlignment.Center
                                scalingMethod: ScalingMethod.AspectFill
                                imageSource: ListItemData.localAvatarPath
                                             && ListItemData.localAvatarPath.length > 0
                                             ? ListItemData.localAvatarPath
                                             : "asset:///icons/ic_contact.png"
                            }
                            Container {
                                leftPadding: 15
                                verticalAlignment: VerticalAlignment.Center
                                layoutProperties: StackLayoutProperties {
                                    spaceQuota: 1
                                }
                                layout: StackLayout {
                                    orientation: LayoutOrientation.TopToBottom
                                }
                                Label {
                                    // 还没拉到资料的账号没有昵称，退回显示打码后的凭证
                                    text: ListItemData.nickName && ListItemData.nickName.length > 0
                                          ? ListItemData.nickName : qsTr("（未知昵称）")
                                    textStyle {
                                        base: SystemDefaults.TextStyles.PrimaryText
                                    }
                                }
                                Label {
                                    text: ListItemData.masked
                                    textStyle {
                                        base: SystemDefaults.TextStyles.SmallText
                                        color: Color.Gray
                                    }
                                }
                            }
                        }
                    }
                }
            ]

            // 点一下 = 切到那个账号（和长按「登录此账号」等价）
            onTriggered: {
                var chosen = dataModel.data(indexPath)
                if (!chosen)
                    return
                if (tieba.switchAccount(String(chosen.id)))
                    tieba.showToast(qsTr("正在切换到 ") + chosen.nickName + "…")
            }
        }
    }

    /*!
     * 复制某账号的 BDUSS 全文。
     * ★ 明文只在 C++ -> 剪贴板之间过一手，不进模型、不打印。
     */
    function copyCredential(id) {
        var text = tieba.accountCredential(id)
        if (!text || text.length == 0) {
            tieba.showToast(qsTr("取不到这个账号的凭据"))
            return
        }
        tieba.copyText(text)
    }
}
