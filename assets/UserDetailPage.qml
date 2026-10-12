import bb.cascades 1.4

Page {
    id: userPage
    property variant nav: null
    property bool ownProfile: false
    signal accountManageRequested()
    property variant userData: ({ id: "", name: "", userName: "", avatarUrl: "", localAvatarPath: "", isSelf: false })
    property variant baseProfile: readBaseProfile(accountVersion, ownProfile, userData)
    property string userId: baseProfile.id || ""
    property string selectedKind: profileTabs.selectedIndex == 1 ? "forums" : (profileTabs.selectedIndex == 2 ? "follows" : "threads")
    property int userPageVersion: typeof tieba === "undefined" ? 0 : tieba.userPageVersion
    property bool loggedIn: typeof tieba !== "undefined" && tieba.loggedIn
    property variant state: readState(userId, selectedKind, userPageVersion)
    property variant profile: readProfile(baseProfile, userPageVersion)
    function readBaseProfile(revision, self, data) {
        if (!self || typeof tieba === "undefined") return data
        return tieba.uiUserProfile({}, true)
    }
    function readState(uid, kind, revision) {
        if (!uid || typeof tieba === "undefined")
            return { items: [], profile: {}, loading: false, loaded: false, error: "", hasMore: false, page: 0 }
        return tieba.userPageState(uid, kind)
    }
    function readProfile(base, revision) {
        var merged = {}
        var key
        for (key in base) merged[key] = base[key]
        if (userId && typeof tieba !== "undefined") {
            var remote = tieba.userPageState(userId, "profile").profile || {}
            for (key in remote) {
                if (remote[key] !== undefined && remote[key] !== null && remote[key] !== "")
                    merged[key] = remote[key]
            }
        }
        return merged
    }
    function statistic(value) {
        return value === undefined || value === null || value === "" ? "—" : String(value)
    }
    property int resultCount: 0
    property string renderedScope: ""
    property variant renderedSignatures: []
    property variant ensuredScopes: ({})
    property string viewerId: readViewerId(accountVersion)
    property bool hideVideoThreads: typeof tieba !== "undefined" && tieba.hideVideoThreads
    function readViewerId(revision) {
        if (typeof tieba === "undefined") return ""
        return tieba.uiUserProfile({}, true).id || ""
    }
    onHideVideoThreadsChanged: { if (ready) userPage.updateItems() }
    property bool ready: false
    property int accountVersion: typeof tieba === "undefined" ? 0 : tieba.accountsVersion
    property int imageVersion: typeof tieba === "undefined" ? 0 : tieba.imageCacheVersion
    // 状态绑定求值期间只更新显示；请求由页面、账号和栏目变化触发。
    // 此处同步请求会再次发出 userPageVersion，造成绑定循环。
    onStateChanged: { if (ready) userPage.updateItems() }
    onSelectedKindChanged: { if (ready) { userPage.updateItems(); userPage.ensureLoaded() } }
    onUserIdChanged: { if (ready) { userPage.updateItems(); userPage.ensureLoaded() } }
    onOwnProfileChanged: { if (ready) userPage.ensureLoaded() }
    onAccountVersionChanged: { if (ready) { userPage.updateItems(); userPage.ensureLoaded() } }
    onProfileChanged: { if (ready) userPage.refreshAvatar() }
    onImageVersionChanged: { if (ready) userPage.refreshAvatar() }
    onCreationCompleted: {
        userPage.ready = true
        userPage.updateItems()
        userPage.ensureLoaded()
    }
//    titleBar: TitleBar { title: userPage.ownProfile ? qsTr("账号") : qsTr("用户详情") }
    actions: [
        ActionItem {
            title: qsTr("复制链接")
            imageSource: "asset:///icons/ic_copy.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            enabled: (userPage.profile.userName || "").length > 0
            onTriggered: {
                var url = "https://tieba.baidu.com/home/main?un="
                tieba.copyText(url + encodeURIComponent(userPage.profile.userName))
            }
        },
        ActionItem {
            title: userPage.ownProfile ? qsTr("账号管理") : qsTr("关注")
            imageSource: userPage.ownProfile ? "asset:///icons/ic_contact.png" : "asset:///icons/ic_add_contact.png"
            ActionBar.placement: ActionBarPlacement.Signature
            enabled: userPage.ownProfile
            onTriggered: userPage.accountManageRequested()
        },
        ActionItem {
            title: qsTr("刷新")
            imageSource: "asset:///icons/ic_sync.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            onTriggered: userPage.refreshItems(true)
        },
        ActionItem {
            title: qsTr("加载更多")
            imageSource: "asset:///icons/ic_select_more.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            enabled: userPage.userId.length > 0 && userPage.state.hasMore && !userPage.state.loading
            onTriggered: userPage.loadMore()
        }
    ]
    function refreshAvatar() {
        if (typeof tieba === "undefined") return
        profileAvatar.imageSource = tieba.imagePath(userPage.profile.avatarUrl) || "asset:///images/default_avatar.png"
    }
    function refreshItems(force) {
        if (!force) {
            userPage.ensureLoaded()
            return
        }
        if (typeof tieba === "undefined" || !userId) return
        tieba.loadUserPage(userId, selectedKind, false)
    }
    function ensureLoaded() {
        if (!ready || !userId || typeof tieba === "undefined") return
        // 账号通知可能先于 baseProfile/userId 的绑定更新。
        // 用实时登录账号校验自己的主页，避免退出或换号时请求旧 UID。
        var liveViewerId = tieba.uiUserProfile({}, true).id || ""
        if (ownProfile && (!liveViewerId || userId !== liveViewerId)) return
        var current = tieba.userPageState(userId, selectedKind)
        if (current.loading) return
        var scope = liveViewerId + ":" + userId + ":" + selectedKind
        if (!ensuredScopes[scope]) {
            // Qt 4 variant map 的 JS 读取可能是副本，必须重新赋值保存标记。
            var nextScopes = {}
            for (var key in ensuredScopes) nextScopes[key] = ensuredScopes[key]
            nextScopes[scope] = true
            ensuredScopes = nextScopes
            tieba.loadUserPage(userId, selectedKind, false)
        } else if (!current.loaded && !current.error)
            tieba.loadUserPage(userId, selectedKind, false)
    }
    function loadMore() {
        if (!userId || typeof tieba === "undefined") return
        var current = tieba.userPageState(userId, selectedKind)
        if (current.hasMore && !current.loading)
            tieba.loadUserPage(userId, selectedKind, true)
    }
    function retryItems() {
        if (userPage.state.retryAppend) userPage.loadMore()
        else userPage.refreshItems(true)
    }
    function updateItems() {
        var rows = readState(userId, selectedKind, userPageVersion).items || []
        var scope = viewerId + ":" + userId + ":" + selectedKind
        var previousCount = profileModel.size()
        if (scope != renderedScope) {
            profileModel.clear()
            previousCount = 0
            renderedScope = scope
            renderedSignatures = []
        }
        for (var r = previousCount - 1; r >= rows.length; --r) profileModel.removeAt(r)
        previousCount = Math.min(previousCount, rows.length)
        var signatures = []
        for (var i = 0; i < rows.length; ++i) {
            signatures.push(JSON.stringify(rows[i]))
            if (i < previousCount && renderedSignatures[i] !== signatures[i]) profileModel.replace(i, rows[i])
        }
        if (rows.length > previousCount)
            profileModel.append(rows.slice(previousCount))
        renderedSignatures = signatures
        userPage.resultCount = rows.length
        userPage.refreshAvatar()
    }
    function pushUser(row) {
        if (!userPage.nav) return
        var page = userDef.createObject()
        if (!page) return
        page.nav = userPage.nav
        page.userData = tieba.uiUserProfile(row)
        page.refreshItems()
        userPage.nav.push(page)
    }
    function openResult(row) {
        if (!userPage.nav || !row || typeof tieba === "undefined") return
        if (row.resultKind == "user") {
            userPage.pushUser(row)
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
            page.threadId = row.threadId || row.id
            page.threadTitle = row.title || ""
            tieba.loadThread(page.threadId, 1)
        }
        page.nav = userPage.nav
        userPage.nav.push(page)
    }
    Container {
        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            preferredHeight: ui.du(25)
            layout: DockLayout {}
            ImageView {
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Fill
                scalingMethod: ScalingMethod.AspectFill
                imageSource: "asset:///images/BackgroundImage.jpg"
            }
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Bottom
                leftPadding: ui.du(2)
                rightPadding: ui.du(2)
                bottomPadding: ui.du(2)
                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                ImageView {
                    id: profileAvatar
                    preferredWidth: ui.du(12)
                    preferredHeight: ui.du(12)
                    minWidth: ui.du(12)
                    maxWidth: ui.du(12)
                    minHeight: ui.du(12)
                    maxHeight: ui.du(12)
                    scalingMethod: ScalingMethod.AspectFill
                    imageSource: "asset:///images/default_avatar.png"
                }
                Container {
                    leftPadding: ui.du(1.5)
                    verticalAlignment: VerticalAlignment.Center
                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                    Label {
                        text: userPage.profile.name || (userPage.ownProfile ? (userPage.loggedIn ? qsTr("资料加载中") : qsTr("未登录")) : qsTr("未知用户"))
                        textStyle.color: Color.White
                        textStyle.fontSize: FontSize.Medium
                        autoSize.maxLineCount: 1
                    }
                    Label {
                        text: qsTr("关注 ") + userPage.statistic(userPage.profile.followingCount)
                              + qsTr("  粉丝 ") + userPage.statistic(userPage.profile.fansCount)
                              + qsTr("  获赞 ") + userPage.statistic(userPage.profile.agreeCount)
                        textStyle.color: Color.White
                        textStyle.fontSize: FontSize.XSmall
                    }
                }
            }
        }
        SegmentedControl {
            id: profileTabs
            selectedIndex: 0
            options: [ Option { text: qsTr("帖子") }, Option { text: qsTr("关注吧") }, Option { text: qsTr("关注的人") } ]
        }
//        Label {
//            text: qsTr("目前仅显示已加载的数据，完整主页资料暂不可用")
//            multiline: true
//            textStyle.fontSize: FontSize.XSmall
//            textStyle.color: Color.Gray
//            horizontalAlignment: HorizontalAlignment.Center
//            leftMargin: ui.du(1)
//            rightMargin: ui.du(1)
//        }
        Container {
            layout: DockLayout {}
            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
            ListView {
                id: profileList
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
                    if (typeof tieba === "undefined") return "asset:///images/default_avatar.png"
                    return tieba.imagePath(row.avatarUrl || row.authorAvatarUrl || "") || "asset:///images/default_avatar.png"
                }
                dataModel: profileModel
                listItemComponents: [ ListItemComponent { type: ""; ResultItem {} } ]
                function openUser(row) { userPage.pushUser(row) }
                onTriggered: userPage.openResult(dataModel.data(indexPath))
                attachedObjects: [
                    ListScrollStateHandler {
                        onAtEndChanged: {
                            if (atEnd && firstVisibleItem.length > 0 && userPage.resultCount > 0 && !userPage.state.error)
                                userPage.loadMore()
                        }
                    }
                ]
            }
            Label {
                visible: userPage.resultCount == 0
                text: userPage.ownProfile && !userPage.loggedIn ? qsTr("点击账号管理添加或切换账号")
                      : (userPage.state.loading ? qsTr("正在加载…")
                      : (userPage.state.error ? userPage.state.error + qsTr("\n点击重试")
                      : (userPage.state.message ? userPage.state.message
                      : (userPage.state.private ? qsTr("该用户已隐藏帖子")
                      : (profileTabs.selectedIndex == 0 ? qsTr("暂无帖子")
                      : (profileTabs.selectedIndex == 1 ? qsTr("暂无关注吧") : qsTr("暂无关注的人")))))))
                horizontalAlignment: HorizontalAlignment.Center
                verticalAlignment: VerticalAlignment.Center
                textStyle.color: Color.Gray
                textStyle.textAlign: TextAlign.Center
                multiline: true
                gestureHandlers: [
                    TapHandler {
                        onTapped: { if (userPage.state.error && !userPage.state.loading) userPage.retryItems() }
                    }
                ]
            }
        }
        Label {
            visible: userPage.resultCount > 0 && (userPage.state.loading || userPage.state.error.length > 0)
            text: userPage.state.loading ? qsTr("正在加载…") : userPage.state.error + qsTr(" · 点击重试")
            textStyle.fontSize: FontSize.XSmall
            textStyle.color: Color.Gray
            horizontalAlignment: HorizontalAlignment.Center
            multiline: true
            gestureHandlers: [
                TapHandler {
                    onTapped: { if (userPage.state.error && !userPage.state.loading) userPage.retryItems() }
                }
            ]
        }
    }
    attachedObjects: [
        ArrayDataModel { id: profileModel },
        ComponentDefinition { id: forumDef; source: "InBaPage.qml" },
        ComponentDefinition { id: threadDef; source: "ThreadPage.qml" },
        ComponentDefinition { id: userDef; source: "UserDetailPage.qml" }
    ]
    actionBarVisibility: ChromeVisibility.Compact
}
