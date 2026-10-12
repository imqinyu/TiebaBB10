import bb.cascades 1.4

Page {
    id: favoritesPage
    property variant nav: null
    property bool ready: false
    property int favoriteVersion: typeof favoriteService === "undefined" ? 0 : favoriteService.version
    property int accountVersion: typeof tieba === "undefined" ? 0 : tieba.accountsVersion
    property variant state: readState(favoriteVersion)
    property int resultCount: 0
    property variant renderedSignatures: []
    property bool hideVideoThreads: typeof tieba !== "undefined" && tieba.hideVideoThreads
    onHideVideoThreadsChanged: { if (ready) favoritesPage.updateItems() }
    function readState(revision) {
        if (typeof favoriteService === "undefined")
            return { items: [], loading: false, loaded: false, error: "", mutationError: "", hasMore: false, busy: false }
        return favoriteService.state
    }
    titleBar: TitleBar { title: qsTr("云端收藏") }
    actions: [
        ActionItem {
            title: qsTr("刷新")
            imageSource: "asset:///icons/ic_sync.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            enabled: !favoritesPage.state.busy
            onTriggered: { if (typeof favoriteService !== "undefined") favoriteService.refresh() }
        },
        ActionItem {
            title: qsTr("加载更多")
            imageSource: "asset:///icons/ic_select_more.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            enabled: favoritesPage.state.hasMore && !favoritesPage.state.busy
            onTriggered: { if (typeof favoriteService !== "undefined") favoriteService.loadMore() }
        }
    ]
    onStateChanged: { if (ready) favoritesPage.updateItems() }
    onAccountVersionChanged: {
        if (ready) {
            favoritesPage.updateItems()
            if (typeof favoriteService !== "undefined") favoriteService.ensureLoaded()
        }
    }
    onCreationCompleted: {
        ready = true
        favoritesPage.updateItems()
        if (typeof favoriteService !== "undefined") favoriteService.ensureLoaded()
    }
    function updateItems() {
        var source = state.items || []
        var rows = []
        for (var n = 0; n < source.length; ++n)
            if (!hideVideoThreads || !source[n].isVideo || source[n].isTop) rows.push(source[n])
        var previousCount = favoritesModel.size()
        for (var r = previousCount - 1; r >= rows.length; --r) favoritesModel.removeAt(r)
        previousCount = Math.min(previousCount, rows.length)
        var signatures = []
        for (var i = 0; i < rows.length; ++i) {
            signatures.push(JSON.stringify(rows[i]))
            if (i < previousCount && renderedSignatures[i] !== signatures[i]) favoritesModel.replace(i, rows[i])
        }
        if (rows.length > previousCount) favoritesModel.append(rows.slice(previousCount))
        renderedSignatures = signatures
        favoritesPage.resultCount = rows.length
    }
    function openThread(row) {
        if (!row || typeof tieba === "undefined") return
        if (row.deleted) { tieba.showToast(qsTr("此收藏帖子已失效，可长按取消收藏")); return }
        var threadId = row.threadId || row.id || ""
        if (!threadId) return
        if (!nav) { tieba.showToast(qsTr("无法打开帖子，请返回后重新进入收藏")); return }
        var page = threadDef.createObject()
        if (!page) return
        page.nav = nav
        page.threadId = threadId
        page.threadTitle = row.title || ""
        tieba.loadThread(page.threadId, 1)
        nav.push(page)
    }
    function pushUserProfile(row) {
        if (!nav || typeof tieba === "undefined") return
        var page = userDef.createObject()
        if (!page) return
        page.nav = nav
        page.userData = tieba.uiUserProfile(row)
        page.refreshItems()
        nav.push(page)
    }
    Container {
        Container {
            visible: !!favoritesPage.state.error && !favoritesPage.state.loading
            leftPadding: ui.du(2)
            rightPadding: ui.du(2)
            Label {
                text: favoritesPage.state.error || ""
                multiline: true
                textStyle.color: Color.Gray
            }
            Button {
                text: qsTr("重试")
                horizontalAlignment: HorizontalAlignment.Center
                onClicked: { if (typeof favoriteService !== "undefined") favoriteService.retry() }
            }
        }
        Container {
            layout: DockLayout {}
            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
            ListView {
                id: favoritesList
                property real itemWidth: favoritesWidth.layoutFrame.width > 0 ? favoritesWidth.layoutFrame.width : -1
                property bool busy: favoritesPage.state.busy
                property int imageVersion: typeof tieba === "undefined" ? 0 : tieba.imageCacheVersion
                function contextTitle(row) { return tieba.contextTitle(row) }
                function contextSubtitle(row) { return tieba.contextSubtitle(row) }
                function avatarPath(row, revision) {
                    return tieba.imagePath(row.avatarUrl || "") || "asset:///images/default_avatar.png"
                }
                function openAuthor(row) { favoritesPage.pushUserProfile(row) }
                function removeFavorite(row) { if (typeof favoriteService !== "undefined") favoriteService.remove(row.threadId || row.id) }
                dataModel: favoritesModel
                listItemComponents: [
                    ListItemComponent {
                        type: ""
                        Container {
                            id: favoriteItem
                            preferredWidth: favoriteItem.ListItem.view.itemWidth
                            horizontalAlignment: HorizontalAlignment.Fill
                            contextActions: [
                                ActionSet {
                                    title: favoriteItem.ListItem.view.contextTitle(ListItemData)
                                    subtitle: favoriteItem.ListItem.view.contextSubtitle(ListItemData)
                                    ActionItem {
                                        title: qsTr("取消云端收藏")
                                        imageSource: "asset:///icons/ic_sb_favorite.png"
                                        enabled: !favoriteItem.ListItem.view.busy
                                        onTriggered: favoriteItem.ListItem.view.removeFavorite(ListItemData)
                                    }
                                }
                            ]
                            ThreadCard {
                                itemData: ListItemData
                                avatarPath: favoriteItem.ListItem.view.avatarPath(ListItemData, favoriteItem.ListItem.view.imageVersion)
                                onUserTapped: favoriteItem.ListItem.view.openAuthor(ListItemData)
                            }
                            Divider {}
                        }
                    }
                ]
                onTriggered: favoritesPage.openThread(dataModel.data(indexPath))
                attachedObjects: [
                    LayoutUpdateHandler { id: favoritesWidth },
                    ListScrollStateHandler {
                        onAtEndChanged: {
                            if (atEnd && !favoritesPage.state.error && typeof favoriteService !== "undefined") favoriteService.loadMore()
                        }
                    }
                ]
            }
            ActivityIndicator {
                running: favoritesPage.state.loading && favoritesPage.resultCount == 0
                horizontalAlignment: HorizontalAlignment.Center
                verticalAlignment: VerticalAlignment.Center
            }
            Label {
                visible: favoritesPage.resultCount == 0 && !favoritesPage.state.loading && !favoritesPage.state.error
                text: favoritesPage.state.loaded ? (favoritesPage.hideVideoThreads && (favoritesPage.state.items || []).length > 0 ? qsTr("视频收藏已隐藏") : qsTr("还没有云端收藏")) : qsTr("正在读取云端收藏")
                horizontalAlignment: HorizontalAlignment.Center
                verticalAlignment: VerticalAlignment.Center
                textStyle.color: Color.Gray
            }
        }
    }
    attachedObjects: [
        ArrayDataModel { id: favoritesModel },
        ComponentDefinition { id: threadDef; source: "ThreadPage.qml" },
        ComponentDefinition { id: userDef; source: "UserDetailPage.qml" }
    ]
    actionBarAutoHideBehavior: ActionBarAutoHideBehavior.HideOnScroll
    actionBarVisibility: ChromeVisibility.Compact
}
