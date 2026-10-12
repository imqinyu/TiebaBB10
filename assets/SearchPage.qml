import bb.cascades 1.4

Page {
    id: searchPage
    property variant nav: null
    property string forumName: ""
    property int resultCount: 0
    property bool hideVideoThreads: typeof tieba !== "undefined" && tieba.hideVideoThreads
    onHideVideoThreadsChanged: { if (ready) searchPage.updateItems() }
    property bool submitted: false
    property bool ready: false
    property string activeQuery: ""
    property string selectedKind: searchTitle.selectedIndex == 0 ? "thread" : (searchTitle.selectedIndex == 1 ? "forum" : "user")
    property int searchVersion: typeof searchService === "undefined" ? 0 : searchService.version
    property variant state: readState(activeQuery, selectedKind, forumName, searchVersion)
    property string renderedScope: ""
    property int accountVersion: typeof tieba === "undefined" ? 0 : tieba.accountsVersion
    function readState(query, kind, forum, revision) {
        if (!query || typeof searchService === "undefined")
            return { items: [], loading: false, loaded: false, error: "", hasMore: false, page: 0 }
        return searchService.state(query, kind, forum)
    }
    onStateChanged: { if (ready) searchPage.updateItems() }
    onAccountVersionChanged: { if (ready) searchPage.clearResults() }
    titleBar: TitleBar {
        id: searchTitle
        kind: TitleBarKind.Segmented
        options: [
            Option { text: qsTr("帖子") },
            Option { text: qsTr("吧") },
            Option { text: qsTr("用户") }
        ]
//        selectedIndex: searchPage.forumName.length > 0 ? 0 : 1
        onSelectedIndexChanged: { if (searchPage.ready) searchPage.clearResults() }
    }
    actions: [
        ActionItem {
            title: qsTr("搜索")
            imageSource: "asset:///icons/ic_search.png"
            ActionBar.placement: ActionBarPlacement.Signature
            onTriggered: searchPage.submitSearch()
        },
        ActionItem {
            title: qsTr("刷新")
            imageSource: "asset:///icons/ic_sync.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            enabled: searchPage.submitted && !searchPage.state.loading
            onTriggered: searchPage.refreshResults()
        },
        ActionItem {
            title: qsTr("加载更多")
            imageSource: "asset:///icons/ic_select_more.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            enabled: searchPage.state.hasMore && !searchPage.state.loading
            onTriggered: searchPage.loadMore()
        }
    ]
    function clearResults() {
        resultModel.clear()
        searchPage.resultCount = 0
        searchPage.submitted = false
        searchPage.activeQuery = ""
        searchPage.renderedScope = ""
    }
    function submitSearch() {
        var query = queryField.text.trim()
        if (query.length == 0) {
            tieba.showToast(qsTr("请输入搜索内容"))
            return
        }
        searchPage.activeQuery = query
        searchPage.submitted = true
        searchPage.refreshResults()
    }
    function refreshResults() {
        if (!activeQuery || typeof searchService === "undefined") return
        searchService.load(activeQuery, selectedKind, forumName, false)
    }
    function loadMore() {
        if (!activeQuery || typeof searchService === "undefined" || state.loading || !state.hasMore) return
        searchService.load(activeQuery, selectedKind, forumName, true)
    }
    function updateItems() {
        var rows = state.items || []
        if (searchPage.hideVideoThreads) rows = rows.filter(function(row) { return !row.isVideo || row.isTop })
        var scope = activeQuery + ":" + selectedKind + ":" + forumName
        var previousCount = resultModel.size()
        if (renderedScope != scope || rows.length < previousCount) {
            resultModel.clear()
            previousCount = 0
            renderedScope = scope
        }
        for (var i = 0; i < previousCount; ++i) resultModel.replace(i, rows[i])
        if (rows.length > previousCount) resultModel.append(rows.slice(previousCount))
        searchPage.resultCount = rows.length
    }
    function pushUser(row) {
        if (!searchPage.nav) return
        var page = userDef.createObject()
        if (!page) return
        page.nav = searchPage.nav
        page.userData = row.resultKind == "user" ? row : tieba.uiUserProfile(row)
        page.refreshItems()
        searchPage.nav.push(page)
    }
    function openResult(row) {
        if (!searchPage.nav || !row) return
        if (row.resultKind == "user") {
            searchPage.pushUser(row)
            return
        }
        var page
        if (row.resultKind == "forum") {
            page = forumDef.createObject()
            if (!page) return
            page.forumName = row.name
            tieba.loadForumPage(row.name, 1, 0, false)
        } else {
            page = threadDef.createObject()
            if (!page) return
            page.threadId = row.id
            page.threadTitle = row.title || ""
            tieba.loadThread(row.id, 1)
        }
        page.nav = searchPage.nav
        searchPage.nav.push(page)
    }
    onCreationCompleted: searchPage.ready = true
    Container {
        leftPadding: ui.du(1)
        rightPadding: ui.du(1)
        topPadding: ui.du(1)
        TextField {
            id: queryField
            hintText: searchTitle.selectedIndex == 0 ? (searchPage.forumName ? qsTr("搜索 ") + searchPage.forumName + qsTr(" 吧内帖子") : qsTr("搜索帖子")) : (searchTitle.selectedIndex == 1 ? qsTr("搜索吧") : qsTr("搜索用户"))
            // 编辑只清空旧结果；提交才搜索。
            onTextChanging: searchPage.clearResults()
        }
//        Label {
//            text: searchPage.forumName.length > 0 ? qsTr("仅搜索已加载的 ") + searchPage.forumName + qsTr(" 吧数据") : qsTr("仅搜索已加载的吧、帖子和作者；点击搜索提交")
//            multiline: true
//            textStyle.fontSize: FontSize.XSmall
//            textStyle.color: Color.Gray
//        }
//        Divider {}
        Container {
            layout: DockLayout {}
            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
            ListView {
                id: results
                property int imageVersion: typeof tieba === "undefined" ? 0 : tieba.imageCacheVersion
                function contextTitle(row) { return tieba.contextTitle(row) }
                property int contextVersion: typeof tieba === "undefined" ? 0 : tieba.contextVersion
                function contextSubtitle(row, revision) { return tieba.contextSubtitle(row) }
                function requestForumStats(row) { tieba.requestForumStats(row) }
                function copyResultLink(row) {
                    var link = row.resultKind == "forum" ? "https://tieba.baidu.com/f?kw=" + encodeURIComponent(row.name || "") : "https://tieba.baidu.com/p/" + (row.threadId || row.id || "")
                    tieba.copyText(link)
                }
                function avatarPath(row) {
                    return tieba.imagePath(row.avatarUrl || row.authorAvatarUrl || "") || "asset:///images/default_avatar.png"
                }
                dataModel: resultModel
                listItemComponents: [ ListItemComponent { type: ""; ResultItem {} } ]
                function openUser(row) { searchPage.pushUser(row) }
                onTriggered: searchPage.openResult(dataModel.data(indexPath))
                attachedObjects: [
                    ListScrollStateHandler {
                        onAtEndChanged: { if (atEnd && searchPage.resultCount > 0 && searchPage.state.loaded && !searchPage.state.error) searchPage.loadMore() }
                    }
                ]
            }
            ActivityIndicator {
                running: searchPage.state.loading
                horizontalAlignment: HorizontalAlignment.Center
                verticalAlignment: VerticalAlignment.Center
            }
            Label {
                visible: searchPage.resultCount == 0 && !searchPage.state.loading && !searchPage.state.error
                text: searchPage.submitted ? qsTr("没有匹配的搜索结果") : qsTr("输入关键词后点击底部搜索")
                horizontalAlignment: HorizontalAlignment.Center
                verticalAlignment: VerticalAlignment.Center
                multiline: true
                textStyle.textAlign: TextAlign.Center
                textStyle.color: Color.Gray
            }
            Container {
                visible: !!searchPage.state.error && !searchPage.state.loading
                horizontalAlignment: HorizontalAlignment.Center
                verticalAlignment: VerticalAlignment.Center
                Label {
                    text: searchPage.state.error || ""
                    multiline: true
                    horizontalAlignment: HorizontalAlignment.Fill
                    textStyle.textAlign: TextAlign.Center
                    textStyle.color: Color.Gray
                }
                Button {
                    text: qsTr("重试")
                    horizontalAlignment: HorizontalAlignment.Center
                    onClicked: { if (searchPage.state.page > 0) searchPage.loadMore(); else searchPage.refreshResults() }
                }
            }
        }
    }
    attachedObjects: [
        ArrayDataModel { id: resultModel },
        ComponentDefinition { id: forumDef; source: "InBaPage.qml" },
        ComponentDefinition { id: threadDef; source: "ThreadPage.qml" },
        ComponentDefinition { id: userDef; source: "UserDetailPage.qml" }
    ]
}
