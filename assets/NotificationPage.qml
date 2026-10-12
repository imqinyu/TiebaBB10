import bb.cascades 1.4

Page {
    id: notificationPage
    property variant nav: null
    property bool active: false
    property bool ready: false
    property int selectedIndex: 0
    property string kind: selectedIndex === 1 ? "mention" : "reply"
    property variant state: readState(streamVersion, kind)
    function readState(revision, selectedKind) {
        if (typeof tieba === "undefined") {
            return {
                loaded: false,
                loading: false,
                hasMore: false,
                retryAppend: false,
                error: "",
                page: 0,
                count: 0
            }
        }
        return tieba.streamState(selectedKind)
    }
    property int accountVersion: typeof tieba === "undefined" ? 0 : tieba.accountsVersion
    property int streamVersion: typeof tieba === "undefined" ? 0 : tieba.streamVersion
    onAccountVersionChanged: ensureCurrent()
    onStreamVersionChanged: ensureCurrent()
    onActiveChanged: refreshCurrent()
    onKindChanged: refreshCurrent()
    onCreationCompleted: { ready = true; ensureCurrent() }
    function ensureCurrent() {
        if (typeof tieba === "undefined") return
        if (ready && active && tieba.loggedIn) tieba.ensureStream(kind)
    }
    function refreshCurrent() {
        if (ready && active && typeof tieba !== "undefined" && tieba.loggedIn)
            tieba.loadStream(kind, false)
    }
    function openUser(row) {
        if (! nav || ! row.authorId) return
        var page = userDef.createObject()
        if (! page) return
        page.nav = nav
        page.userData = tieba.uiUserProfile(row)
        page.refreshItems()
        nav.push(page)
    }
    function openThread(row) {
        if (! nav || ! row.threadId) return
        var page = threadDef.createObject()
        if (! page) return
        page.nav = nav
        page.threadId = row.threadId
        page.threadTitle = row.title || ""
        var postId = row.isFloor ? (row.quotePostId || "") : (row.postId || "")
        var subPostId = row.isFloor ? (row.postId || "") : ""
        if (postId.length > 0 || subPostId.length > 0)
            tieba.openThreadPost(row.threadId, postId, subPostId, false, false)
        else tieba.loadThread(row.threadId, 1, false, false, false)
        nav.push(page)
    }
    titleBar: TitleBar {
        id: notificationSegments
        kind: TitleBarKind.Segmented
        options: [
            Option { text: qsTr("回复"); selected: true },
            Option { text: qsTr("提及") }
        ]
        onSelectedIndexChanged: notificationPage.selectedIndex = selectedIndex < 0 ? 0 : selectedIndex
    }
    actions: [
        ActionItem {
            title: qsTr("刷新")
            imageSource: "asset:///icons/ic_sync.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            enabled: tieba.loggedIn && ! notificationPage.state.loading
            onTriggered: {
                tieba.loadStream(notificationPage.kind, false)
            }
        },
        ActionItem {
            title: qsTr("加载更多")
            imageSource: "asset:///icons/ic_select_more.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            enabled: tieba.loggedIn && notificationPage.state.hasMore && ! notificationPage.state.loading
            onTriggered: {
                tieba.loadStream(notificationPage.kind, true)
            }
        }
    ]
    Container {
        topPadding: ui.du(1.0)

//        SegmentedControl {
//
//            
//        }
        Container {
            visible: notificationPage.state.loading
            horizontalAlignment: HorizontalAlignment.Center
            layout: StackLayout {
                orientation: LayoutOrientation.LeftToRight
            }
            ActivityIndicator {
                running: notificationPage.state.loading
                preferredWidth: ui.du(4)
                preferredHeight: ui.du(4)
            }
            Label {
                text: qsTr("正在加载…")
                textStyle.fontSize: FontSize.Small
            }
        }
        Container {
            visible: (notificationPage.state.error || "").length > 0
            leftPadding: ui.du(1.5)
            rightPadding: ui.du(1.5)
            Label {
                text: notificationPage.state.error || ""
                multiline: true
                textStyle.fontSize: FontSize.Small
            }
            Button {
                text: qsTr("重试")
                enabled: tieba.loggedIn && ! notificationPage.state.loading
                onClicked: tieba.loadStream(notificationPage.kind, notificationPage.state.retryAppend)
            }
        }
        Container {
            layoutProperties: StackLayoutProperties {
                spaceQuota: 1
            }
            layout: DockLayout {
            }
            ListView {
                id: streamList
                property real itemWidth: notificationWidth.layoutFrame.width > 0 ? notificationWidth.layoutFrame.width : -1
                visible: notificationPage.state.count > 0
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Fill
                dataModel: notificationPage.kind === "reply" ? tieba.replyNotifications : tieba.mentionNotifications
                layout: StackListLayout {
                }
                function openAuthor(row) {
                    notificationPage.openUser(row)
                }
                function contextTitle(row) { return tieba.contextTitle(row) }
                function contextSubtitle(row) { return tieba.contextSubtitle(row) }
                function copyLink(row) {
                    var address = "https://tieba.baidu.com/p/" + row.threadId
                    tieba.copyText(address)
                }
                listItemComponents: [
                    ListItemComponent {
                        type: ""
                        Container {
                            id: streamRow
                            preferredWidth: streamRow.ListItem.view.itemWidth
                            horizontalAlignment: HorizontalAlignment.Fill
                            verticalAlignment: VerticalAlignment.Top
                            NotificationCard {
                                itemData: ListItemData
                                onUserTapped: streamRow.ListItem.view.openAuthor(ListItemData)
                            }
                            Container {
                                preferredHeight: 1
                                maxHeight: 1
                                horizontalAlignment: HorizontalAlignment.Fill
                                background: Color.create("#e6e6e6")
                            }
                            contextActions: [
                                ActionSet {
                                    title: streamRow.ListItem.view.contextTitle(ListItemData)
                                    subtitle: streamRow.ListItem.view.contextSubtitle(ListItemData)
                                    ActionItem {
                                        title: qsTr("复制链接")
                                        onTriggered: streamRow.ListItem.view.copyLink(ListItemData)
                                    }
                                }
                            ]
                        }
                    }
                ]
                onTriggered: notificationPage.openThread(dataModel.data(indexPath))
                attachedObjects: [
                    LayoutUpdateHandler { id: notificationWidth },
                    ListScrollStateHandler {
                        onAtEndChanged: {
                            if (atEnd && notificationPage.state.count > 0 && notificationPage.state.hasMore
                                    && !notificationPage.state.loading && !(notificationPage.state.error || "").length)
                                tieba.loadStream(notificationPage.kind, true)
                        }
                    }
                ]
            }
//            Container {
//                visible: ! notificationPage.state.loading && notificationPage.state.count === 0 && ! (notificationPage.state.error || "").length
//                horizontalAlignment: HorizontalAlignment.Center
//                verticalAlignment: VerticalAlignment.Center
//                Label {
//                    text: ! tieba.loggedIn ? qsTr("登录后查看通知") : qsTr("暂无通知")
//                    textStyle.color: Color.Gray
//                }
//                Button {
//                    text: qsTr("登录")
//                    visible: ! tieba.loggedIn
//                    onClicked: tieba.requestOpenLogin()
//                }
//            }
        }
        Label {
            visible: notificationPage.state.loaded && ! notificationPage.state.loading && ! notificationPage.state.hasMore && notificationPage.state.count > 0
            text: qsTr("已显示全部内容")
            horizontalAlignment: HorizontalAlignment.Center
            textStyle.fontSize: FontSize.XSmall
            textStyle.color: Color.Gray
        }
    }
    attachedObjects: [
        ComponentDefinition {
            id: threadDef
            source: "ThreadPage.qml"
        },
        ComponentDefinition {
            id: userDef
            source: "UserDetailPage.qml"
        }
    ]
    actionBarVisibility: ChromeVisibility.Compact
}
