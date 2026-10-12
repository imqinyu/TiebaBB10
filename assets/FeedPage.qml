import bb.cascades 1.4

Page {
    id: feedPage
    property variant nav: null
    property bool active: false
    property bool ready: false
    property int selectedIndex: 0
    property string kind: selectedIndex === 0 ? "follow" : selectedIndex === 2 ? "hot" : "recommend"
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
    onActiveChanged: ensureCurrent()
    onKindChanged: ensureCurrent()
    onCreationCompleted: { ready = true; ensureCurrent() }
    function ensureCurrent() {
        if (typeof tieba === "undefined") return
        if (ready && active && tieba.loggedIn) tieba.ensureStream(kind)
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
        tieba.loadThread(row.threadId, 1, false, false, false)
        nav.push(page)
    }
    titleBar: TitleBar {
        id: feedTitleBar
        kind: TitleBarKind.Segmented
        onSelectedIndexChanged: feedPage.selectedIndex = selectedIndex < 0 ? 0 : selectedIndex
        options: [
            Option {
                text: qsTr("关注")
                selected: true
            },
            Option {
                text: qsTr("推荐")
            },
            Option {
                text: qsTr("热门")
            }
            
        ]
        selectedIndex: 1//默认显示推荐页
    }
    actions: [
        ActionItem {
            title: qsTr("刷新")
            imageSource: "asset:///icons/ic_sync.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            enabled: tieba.loggedIn && ! feedPage.state.loading
            onTriggered: {
                tieba.loadStream(feedPage.kind, false)
            }
        },
        ActionItem {
            title: qsTr("加载更多")
            imageSource: "asset:///icons/ic_select_more.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            enabled: tieba.loggedIn && feedPage.state.hasMore && ! feedPage.state.loading
            onTriggered: {
                tieba.loadStream(feedPage.kind, true)
            }
        }
    ]
    Container {
        topPadding: ui.du(1.0)


        Container {
            visible: feedPage.state.loading && feedPage.state.count === 0
            horizontalAlignment: HorizontalAlignment.Center
            layout: StackLayout {
                orientation: LayoutOrientation.LeftToRight
            }
            ActivityIndicator {
                running: feedPage.state.loading && feedPage.state.count === 0
                preferredWidth: ui.du(4)
                preferredHeight: ui.du(4)
            }
            Label {
                text: qsTr("正在加载…")
                textStyle.fontSize: FontSize.Small
            }
        }
        Container {
            visible: (feedPage.state.error || "").length > 0
            leftPadding: ui.du(1.5)
            rightPadding: ui.du(1.5)
            Label {
                text: feedPage.state.error || ""
                multiline: true
                textStyle.fontSize: FontSize.Small
            }
            Button {
                text: qsTr("重试")
                enabled: tieba.loggedIn && ! feedPage.state.loading
                onClicked: tieba.loadStream(feedPage.kind, feedPage.state.retryAppend)
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
                property real itemWidth: feedWidth.layoutFrame.width > 0 ? feedWidth.layoutFrame.width : -1
                visible: feedPage.state.count > 0
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Fill
                dataModel: feedPage.kind === "follow" ? tieba.followedFeed : feedPage.kind === "recommend" ? tieba.recommendedFeed : tieba.hotFeed
                layout: StackListLayout {
                }
                function openAuthor(row) {
                    feedPage.openUser(row)
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
                            ThreadCard {
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
                                        imageSource: "asset:///icons/ic_link.png"
                                    }
                                }
                            ]
                        }
                    }
                ]
                onTriggered: feedPage.openThread(dataModel.data(indexPath))
                attachedObjects: [
                    LayoutUpdateHandler { id: feedWidth },
                    ListScrollStateHandler {
                        onAtEndChanged: {
                            if (atEnd && feedPage.state.count > 0 && feedPage.state.hasMore
                                    && !feedPage.state.loading && !(feedPage.state.error || "").length)
                                tieba.loadStream(feedPage.kind, true)
                        }
                    }
                ]
            }
//            Container {
//                visible: ! feedPage.state.loading && feedPage.state.count === 0 && ! (feedPage.state.error || "").length
//                horizontalAlignment: HorizontalAlignment.Center
//                verticalAlignment: VerticalAlignment.Center
//                Label {
//                    text: ! tieba.loggedIn ? qsTr("登录后查看动态") : feedPage.kind === "follow" ? qsTr("暂无关注动态") : qsTr("暂无帖子")
//                    textStyle.color: Color.Gray
//                }
//                Button {
//                    text: qsTr("登录")
//                    visible: ! tieba.loggedIn
//                    onClicked: tieba.requestOpenLogin()
//                }
//            }
        }
//        Label {
//            visible: feedPage.state.loaded && ! feedPage.state.loading && ! feedPage.state.hasMore && feedPage.state.count > 0
//            text: qsTr("已显示全部内容")
//            horizontalAlignment: HorizontalAlignment.Center
//            textStyle.fontSize: FontSize.XSmall
//            textStyle.color: Color.Gray
//        }
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
