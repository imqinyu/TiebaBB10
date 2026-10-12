import bb.cascades 1.4

Page {
    id: libraryPage
    property variant nav: null
    property string kind: "history"
    property string title: kind === "favorites" ? qsTr("收藏帖子") : qsTr("最近浏览")
    property bool ready: false
    property int count: 0
    property int libraryVersion: typeof threadLibrary === "undefined" ? 0 : threadLibrary.version
    onLibraryVersionChanged: refreshItems()
    onKindChanged: refreshItems()
    onCreationCompleted: {
        ready = true
        refreshItems()
    }
    function refreshItems() {
        if (!ready || typeof threadLibrary === "undefined") return
        var rows = threadLibrary.items(kind)
        libraryModel.clear()
        libraryModel.insertList(rows)
        count = rows.length
    }
    function openThread(row) {
        if (!row || typeof tieba === "undefined") return
        var threadId = row.threadId || row.id || ""
        if (!threadId) return // 日期 header 没有帖子 ID，不参与跳转。
        if (!nav) { tieba.showToast(qsTr("无法打开帖子，请返回后重新进入最近浏览")); return }
        var page = threadDef.createObject()
        if (!page) return
        page.nav = nav
        page.threadId = threadId
        page.threadTitle = row.title || ""
        tieba.loadThread(threadId, 1, false, false, false)
        nav.push(page)
    }
    titleBar: TitleBar { title: libraryPage.title }
    Container {
        layout: DockLayout {}
        ListView {
            id: libraryList
            property real itemWidth: libraryWidth.layoutFrame.width > 0 ? libraryWidth.layoutFrame.width : -1
            visible: libraryPage.count > 0
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill
            dataModel: libraryModel
            function toggleFavorite(row) {
                if (typeof favoriteService !== "undefined") favoriteService.toggle(row)
            }
            function favoriteTitle(row, revision) {
                if (typeof favoriteService === "undefined") return qsTr("收藏")
                return favoriteService.status(row.threadId) === 1 ? qsTr("取消收藏") : qsTr("收藏")
            }
            property int revision: typeof favoriteService === "undefined" ? 0 : favoriteService.version
            listItemComponents: [
                ListItemComponent {
                    type: "header"
                    Header { title: ListItemData }
                },
                ListItemComponent {
                    type: "item"
                    Container {
                        id: libraryRow
                        preferredWidth: libraryRow.ListItem.view.itemWidth
                        horizontalAlignment: HorizontalAlignment.Fill
                        verticalAlignment: VerticalAlignment.Top
                        Container {
                            horizontalAlignment: HorizontalAlignment.Fill
                            leftPadding: ui.du(1.5)
                            rightPadding: ui.du(1.5)
                            topPadding: ui.du(1.5)
                            bottomPadding: ui.du(1.5)
                            Label {
                                text: ListItemData.title || qsTr("未命名帖子")
                                horizontalAlignment: HorizontalAlignment.Fill
                                multiline: true
                                textStyle.fontSize: FontSize.Medium
                            }
                            Label {
                                text: (ListItemData.authorName || "") + (ListItemData.forumName ? " · " + ListItemData.forumName + qsTr("吧") : "")
                                visible: text.length > 0
                                textStyle.fontSize: FontSize.Small
                                textStyle.color: Color.Gray
                            }
                        }
                        Container {
                            preferredHeight: 1
                            maxHeight: 1
                            horizontalAlignment: HorizontalAlignment.Fill
                            background: Color.create("#e6e6e6")
                        }
                        contextActions: [
                            ActionSet {
                                title: ListItemData.title || ""
                                subtitle: ListItemData.authorName || ""
                                ActionItem {
                                    title: libraryRow.ListItem.view.favoriteTitle(ListItemData, libraryRow.ListItem.view.revision)
                                    imageSource: "asset:///icons/ic_sb_favorite.png"
                                    onTriggered: libraryRow.ListItem.view.toggleFavorite(ListItemData)
                                }
                            }
                        ]
                    }
                }
            ]
            onTriggered: {
                var row = dataModel.data(indexPath)
                if (row && row.threadId) libraryPage.openThread(row)
            }
            attachedObjects: [ LayoutUpdateHandler { id: libraryWidth } ]
        }
        Label {
            visible: libraryPage.count === 0
            text: libraryPage.kind === "favorites" ? qsTr("暂无收藏帖子") : qsTr("最近两个月暂无浏览记录")
            horizontalAlignment: HorizontalAlignment.Center
            verticalAlignment: VerticalAlignment.Center
            textStyle.color: Color.Gray
        }
    }
    attachedObjects: [
        GroupDataModel {
            id: libraryModel
            sortingKeys: [ "date", "visitedAt" ]
            sortedAscending: false
            grouping: libraryPage.kind === "history" ? ItemGrouping.ByFullValue : ItemGrouping.None
        },
        ComponentDefinition {
            id: threadDef
            source: "ThreadPage.qml"
        }
    ]
    actionBarVisibility: ChromeVisibility.Compact
}
