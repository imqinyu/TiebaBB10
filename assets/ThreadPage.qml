/*
 * 贴子页（新文件）
 *
 * 显示 tieba.posts 模型。这个模型的特殊之处：
 *
 *   ★ 一个楼层可能对应**多个**列表项。
 *     因为 Cascades 的 ListItemComponent 里没法遍历数组（没有 Repeater，
 *     套 ListView 又会崩），所以"一楼多图"是在 C++ 侧展开的：
 *         正文项（isImageItem = false, postHead = true）
 *         图片项 × N（isImageItem = true,  postHead = false）
 *     这里只需要按标记分流显示：
 *         isImageItem === true  → 图片项
 *         postHead    === true  → 正文项（作者/楼层/时间/正文/楼中楼预览）
 *
 *   ★ 图片只能绑 localImagePath（file:// 本地路径）。
 *     BB10 的图片加载器不支持任何网络方案，直接给远程 URL 会
 *     "Image loading aborted"。下载与缓存由 C++ 负责。
 *
 * 结构：根是 **Page**（不是 NavigationPane）。
 *
 *   ★ 关于页面跳转（这里踩过一次，别改回去）：
 *     NavigationPane::push() 只接受 Page*，而且 **Page 上没有任何
 *     push()/navigationPane 接口**（真机头文件零匹配）——
 *     页面无法自己找到所属导航栈，所以由创建方通过下面的 `nav` 属性传进来。
 *
 * ⚠️ 列表项里不要访问 tieba（ListItemComponent 是独立上下文，会 ReferenceError）。
 */

import bb.cascades 1.4
import bb.system 1.0

Page {
    id: threadPage

    /*! 所属导航栈。由 InBaPage.qml 的 pushThread() 在 push 之前设置。 */
    property variant nav: null

    /*! 贴子 id。由 InBaPage.qml 的 pushThread() 在 push 之前设置。 */
    property string threadId: ""
    /*! 贴子标题，显示在 TitleBar 上 */
    property string threadTitle: ""
    /*! 是否只看楼主 */
    property bool onlyOp: false
    property bool ready: false
    property int lastTargetVersion: -1
    property int navigationVersion: typeof tieba === "undefined" ? 0 : tieba.threadNavigationVersion
    property variant navigationState: readNavigation(navigationVersion, threadId)
    property bool reverse: navigationState.reverse === true
    function readNavigation(revision, tid) {
        if (typeof tieba === "undefined" || !tid)
            return { page: 1, hasMore: false, loading: false, reverse: false, targetIndex: -1, targetVersion: 0, error: "" }
        return tieba.threadNavigationState(tid)
    }
    function positionTarget() {
        if (!ready || navigationState.loading || navigationState.targetIndex < 0
                || navigationState.targetVersion == lastTargetVersion) return
        if (navigationState.targetIndex >= postList.dataModel.size()) return
        lastTargetVersion = navigationState.targetVersion
        postList.scrollToItem([navigationState.targetIndex], ScrollAnimation.None)
    }
    function loadNextPage() {
        if (navigationState.loading || !navigationState.hasMore || typeof tieba === "undefined") return
        tieba.loadThread(threadId, pageCurrent() + 1, onlyOp, reverse, true)
    }
    onNavigationStateChanged: positionTarget()
    onCreationCompleted: { ready = true; positionTarget() }
    property int libraryVersion: typeof favoriteService !== "undefined" ? favoriteService.version : 0
    onThreadIdChanged: {
        if (threadId.length > 0 && typeof favoriteService !== "undefined") favoriteService.ensureStatus(threadId)
    }
    function favoriteTitle(revision) {
        return typeof favoriteService !== "undefined" && favoriteService.status(threadPage.threadId) === 1 ? qsTr("取消收藏") : qsTr("收藏")
    }

    titleBar: TitleBar {
        title: threadPage.threadTitle.length > 0
               ? threadPage.threadTitle
               : qsTr("贴子")
    }

    actionBarVisibility: ChromeVisibility.Visible

    actions: [
        ActionItem {
            title: qsTr("加载更多")
            imageSource: "asset:///icons/ic_select_more.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            enabled: !threadPage.navigationState.loading && threadPage.navigationState.hasMore
            onTriggered: threadPage.loadNextPage()
        },
        ActionItem {
            title: qsTr("只看楼主")
            imageSource: "asset:///icons/ic_contact.png"
            ActionBar.placement: ActionBarPlacement.Signature
            onTriggered: {
                // 切换"只看楼主"并重新加载第一页
                threadPage.onlyOp = ! threadPage.onlyOp
                tieba.loadThread(threadPage.threadId, 1,
                                 threadPage.onlyOp, threadPage.reverse, false)
            }
        },
        ActionItem {
            title: qsTr("刷新")
            imageSource: "asset:///icons/ic_sync.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            onTriggered: {
                tieba.loadThread(threadPage.threadId, 1,
                                 threadPage.onlyOp, threadPage.reverse, false)
            }
        },
        ActionItem {
            title: threadPage.favoriteTitle(threadPage.libraryVersion)
            enabled: typeof favoriteService === "undefined" ? false : !favoriteService.busy
            imageSource: "asset:///icons/ic_sb_favorite.png"
            ActionBar.placement: ActionBarPlacement.OnBar
            onTriggered: {
                tieba.favoriteThread(threadPage.threadId)
            }
        },
        ActionItem {
            title: "浏览器打开"
            imageSource: "asset:///icons/ic_browser.png"
            onTriggered: {
                // ★ 意图框架：tieba.openUrl() 内部用 bb::system::InvokeManager，
                //   target = sys.browser，action = bb.action.OPEN。
                //   走系统浏览器还有个好处：那边能直接看大图，
                //   而本机的 Cascades 图片加载器不支持任何网络图片。
                threadPage.openInBrowser()
            }
            ActionBar.placement: ActionBarPlacement.InOverflow
        },
        ActionItem {
            //是倒序的时候就显示正序浏览 反之亦然
            title: threadPage.reverse ? qsTr("正序浏览") : qsTr("倒序浏览")
            imageSource: "asset:///icons/ic_sort.png"
            ActionBar.placement: ActionBarPlacement.OnBar
            enabled: !threadPage.navigationState.loading
            onTriggered: tieba.loadThread(threadPage.threadId, 1, threadPage.onlyOp, !threadPage.reverse, false)
        },
        ActionItem {
            title: "跳转到楼层"
            imageSource: "asset:///icons/ic_mark_prior_unopened.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            enabled: !threadPage.navigationState.loading
            onTriggered: floorPrompt.show()
        }
    ]

    /*!
     * 用系统浏览器打开当前贴子。
     *
     * 地址就是标准的 https://tieba.baidu.com/p/<tid>；
     * 当前页码作为 ?pn= 带上，浏览器里翻到同一页。
     */
    function openInBrowser() {
        if (! threadPage.threadId || threadPage.threadId.length == 0) {
            console.log("ThreadPage: threadId 为空，无法打开浏览器")
            return
        }
        var url = "https://tieba.baidu.com/p/" + threadPage.threadId
        var page = threadPage.pageCurrent()
        if (page > 1)
            url = url + "?pn=" + page
        var ok = tieba.openUrl(url)
        if (! ok)
            console.log("ThreadPage: 打开浏览器失败（设备上可能没有浏览器 card）")
    }

    /*!
     * 安全读取当前页码。
     * ★ 不能写 tieba.pageInfo.currentPage：空 map 上取属性会 SIGSEGV
     *   （真机实测，见 InBaPage.qml 里的详细说明）。
     */
    function pageCurrent() {
        return threadPage.navigationState.page > 0 ? threadPage.navigationState.page : 1
    }

    /*
     * 空状态 + 列表。
     *
     * Cascades 的 ListView **没有 emptyText 属性**（真机头文件里没有 empty*），
     * 空提示要用 "Container(DockLayout) + 覆盖 Label" 来搭。
     * visible 用 tieba.posts.isEmpty()（ArrayDataModel::isEmpty() 是 Q_INVOKABLE）。
     */
    Container {
        layout: DockLayout {
        }

        ListView {
            
            id: postList
            property real itemWidth: postWidth.layoutFrame.width > 0 ? postWidth.layoutFrame.width : -1
            property int contextVersion: tieba.contextVersion
            property int favoriteVersion: threadPage.libraryVersion
            property bool favoriteBusy: typeof favoriteService === "undefined" ? true : favoriteService.busy
            function menuFavoriteTitle(revision) { return threadPage.favoriteTitle(revision) }
            function contextTitle() {
                var row = {}
                row.id = threadPage.threadId
                return threadPage.threadTitle || tieba.contextTitle(row)
            }
            function contextSubtitle(revision) {
                var row = {}
                row.id = threadPage.threadId
                return tieba.contextSubtitle(row)
            }
            function openUser(row) { threadPage.pushUser(row) }

            dataModel: tieba.posts

            /*
             * ★ 图片点击的处理函数（不是信号）。
             *
             * ListItemComponent 是**独立上下文**，访问不到 tieba。官方文档
             * （bb/cascades/controls/listview.h 的 ListItem.view 说明）给的桥是：
             *
             *     "If you create a list in QML and need to access variables placed
             *      in the ListView in a ListItemComponent, you can access the
             *      variables using the ListItem.view property."
             *
             * 所以 item 里用 `itemRoot.ListItem.view.openImage(url)` 调到这个函数，
             * 这里已经是页面作用域，能直接访问 tieba。
             *
             * ★ 为什么不用自定义信号（我上一版就是这么写的，**没生效**）：
             *   我在 ListView 上声明了 `signal imageTapped`，又在 ListItemComponent
             *   根节点上也声明了一个**同名**信号，然后在 item 里发 `itemRoot.imageTapped()` ——
             *   发出去的是 **itemRoot 自己那个信号**，永远传不到 ListView 上，
             *   所以 onImageTapped 从来不会触发。
             *   现在改用函数直调，没有这层绕弯。
             */
            function openImage(url) {
                console.log("ThreadPage: 图片被点击 url=" + url)
                if (! url || url.length == 0) {
                    /*
                     * 这个动作对没有图的项（比如纯文字楼层）不适用。
                     * 菜单是所有行共用的，所以这里**必须校验** ——
                     * 否则会静默失败，用户不知道发生了什么。
                     */
                    tieba.showToast(qsTr("这一项没有图片"))
                    return
                }
                tieba.openImage(url)
            }

            /*
             * ============ 长按菜单的处理函数 ============
             * 列表项里通过 itemRoot.ListItem.view.xxx() 调到这里
             * （ListItemComponent 是独立上下文，访问不到 tieba）。
             */
            function copyText(text) {
                if (! text || text.length == 0) {
                    tieba.showToast(qsTr("无可复制文字"))
                    return
                }
                var ok = tieba.copyText(text)
                console.log("ThreadPage: 复制 " + (ok ? "成功" : "失败"))
            }

            function copyThreadLink() {
                if (! threadPage.threadId || threadPage.threadId.length == 0) {
                    tieba.showToast(qsTr("无法获取帖子ID"))
                    return
                }
                var url = "https://tieba.baidu.com/p/" + threadPage.threadId
                var ok = tieba.copyText(url)
                console.log("ThreadPage: 复制贴子链接 " + (ok ? "成功" : "失败") + " " + url)
            }

            function favoriteThread() {
                console.log("ThreadPage: 长按收藏 " + threadPage.threadId)
                if (! tieba.favoriteThread(threadPage.threadId))
                    tieba.showToast(qsTr("收藏功能还没做"))
            }

            /*
             * 文案里的链接：当前贴子 + 具体楼层。
             * 楼层锚点用 pid（post id），贴吧网页支持 ?pid= 定位。
             */
            function copyFloorLink(postId) {
                if (! postId || postId.length == 0) {
                    tieba.showToast(qsTr("无楼层id"))
                    return
                }
                var url = "https://tieba.baidu.com/p/" + threadPage.threadId
                if (postId && postId.length > 0)
                    url = url + "?pid=" + postId
                var ok = tieba.copyText(url)
                console.log("ThreadPage: 复制楼层链接 " + (ok ? "成功" : "失败") + " " + url)
            }

            // A single Cascades component keeps ListItem.view and the context menu.
            // Native Container rows size to their content, including expansion.
            // Only the last piece of each floor draws a divider.
            listItemComponents: [
                ListItemComponent {
                    type: ""
                    Container {
                        id: itemRoot
                        preferredWidth: itemRoot.ListItem.view.itemWidth
                        // Only instantiate the body for this row. Hidden fixed-size media
                        // must not contribute to a text row's intrinsic height.
                        property variant rowData: ListItemData
                        property string bodyKind: rowData.isImageItem === true ? "image"
                                                  : (rowData.isReplyFooter === true ? "replies"
                                                  : (rowData.isEmoticonItem === true ? "emoticon" : "post"))
                        property string mountedKind: ""
                        property bool bodyReady: false
                        horizontalAlignment: HorizontalAlignment.Fill
                        verticalAlignment: VerticalAlignment.Top
                        onBodyKindChanged: updateBody()
                        onCreationCompleted: {
                            bodyReady = true
                            updateBody()
                        }
                        function updateBody() {
                            if (!bodyReady || mountedKind === bodyKind)
                                return
                            bodyHost.removeAll()
                            var definition = bodyKind === "image" ? imageBodyDef
                                             : (bodyKind === "replies" ? replyBodyDef
                                             : (bodyKind === "emoticon" ? emoticonBodyDef : postBodyDef))
                            var body = definition.createObject()
                            bodyHost.add(body)
                            mountedKind = bodyKind
                        }
                        /*
                         * 长按菜单。
                         *
                         * ★ 注意这里用 **enabled** 而不是 visible ——
                         *   ActionItem 没有 visible 属性，误用会让**整页加载失败**
                         *   （真机：Cannot assign to non-existent property "visible"）。
                         *   enabled 是真实存在的（abstractactionitem.h）。
                         *   不适用的项会显示成灰色、点不动，用户能一眼看出哪个可用。
                         */
                        contextActions: [
                            ActionSet {
                                title: itemRoot.ListItem.view.contextTitle()
                                subtitle: itemRoot.ListItem.view.contextSubtitle(itemRoot.ListItem.view.contextVersion)
//                                ActionItem {
//                                    title: qsTr("打开图片")
//                                    enabled: ListItemData.remoteImageUrl
//                                             && ListItemData.remoteImageUrl.length > 0
//                                    onTriggered: {
//                                        itemRoot.ListItem.view.openImage(
//                                            ListItemData.remoteImageUrl)
//                                    }
//                                    imageSource: "asset:///icons/ic_open.png"
//                                }
//                                ActionItem {
//                                    title: qsTr("复制图片地址")
//                                    enabled: ListItemData.remoteImageUrl
//                                             && ListItemData.remoteImageUrl.length > 0
//                                    onTriggered: {
//                                        itemRoot.ListItem.view.copyText(
//                                            ListItemData.remoteImageUrl)
//                                    }
//                                    imageSource: "asset:///icons/ic_link.png"
//                                }
                                ActionItem {
                                    title: itemRoot.ListItem.view.menuFavoriteTitle(itemRoot.ListItem.view.favoriteVersion)
                                    imageSource: "asset:///icons/ic_sb_favorite.png"
                                    enabled: ListItemData.postHead === true && !itemRoot.ListItem.view.favoriteBusy
                                    onTriggered: {
                                        itemRoot.ListItem.view.favoriteThread()
                                    }
                                }
                                ActionItem {
                                    title: qsTr("复制链接")
                                    enabled: ListItemData.postHead === true
                                    onTriggered: {
                                        itemRoot.ListItem.view.copyThreadLink()
                                    }
                                    imageSource: "asset:///icons/ic_link.png"
                                }
//                                ActionItem {
//                                    title: qsTr("复制楼层链接")
//                                    enabled: ListItemData.postHead === true
//                                    onTriggered: {
//                                        itemRoot.ListItem.view.copyFloorLink(
//                                            ListItemData.id)
//                                    }
//                                    imageSource: "asset:///icons/ic_link.png"
//                                }
                                ActionItem {
                                    title: qsTr("复制正文")
                                    enabled: ListItemData.plainText
                                             && ListItemData.plainText.length > 0
                                    onTriggered: {
                                        itemRoot.ListItem.view.copyText(
                                            ListItemData.plainText)
                                    }
                                    imageSource: "asset:///icons/ic_copy.png"
                                }
                            }
                        ]


                        

                        

                        Container {
                            id: bodyHost
                            horizontalAlignment: HorizontalAlignment.Fill
                            verticalAlignment: VerticalAlignment.Top
                        }
                        Container {
                            visible: itemRoot.rowData.floorEnd === true
                            preferredHeight: 1
                            maxHeight: 1
                            horizontalAlignment: HorizontalAlignment.Fill
                            background: Color.create("#e6e6e6")
                        }
                        attachedObjects: [
                            ComponentDefinition {
                                id: postBodyDef
                                Container {
                                    horizontalAlignment: HorizontalAlignment.Fill
                                    verticalAlignment: VerticalAlignment.Top
                                    leftPadding: 20
                                    rightPadding: 20
                                    topPadding: 15
                                    bottomPadding: 15
                                    AuthorHeader {
                                        itemData: itemRoot.rowData
                                        trailingText: "#" + (itemRoot.rowData.floor || "") + qsTr("楼")
                                        onUserTapped: itemRoot.ListItem.view.openUser(itemRoot.rowData)
                                    }
                                    ExpandablePostText {
                                        postKey: itemRoot.rowData.id || ""
                                        segments: itemRoot.rowData.displaySegments || []
                                        fallbackText: itemRoot.rowData.displayText !== undefined
                                                      ? itemRoot.rowData.displayText : (itemRoot.rowData.plainText || "")
                                    }
                                }
                            },
                            ComponentDefinition {
                                id: replyBodyDef
                                Container {
                                    id: replyBody
                                    property bool showReplyLink: itemRoot.rowData.subPostCount > 1
                                                                || (itemRoot.rowData.subPostCount === 1
                                                                    && !(itemRoot.rowData.subPostPreview || []).length)
                                    property bool linkReady: false
                                    onShowReplyLinkChanged: updateReplyLink()
                                    onCreationCompleted: {
                                        linkReady = true
                                        updateReplyLink()
                                    }
                                    function updateReplyLink() {
                                        if (!linkReady) return
                                        replyLinkHost.removeAll()
                                        if (showReplyLink) replyLinkHost.add(replyLinkDef.createObject())
                                    }
                                    horizontalAlignment: HorizontalAlignment.Fill
                                    verticalAlignment: VerticalAlignment.Top
                                    leftPadding: 20
                                    rightPadding: 20
                                    bottomPadding: 15
                                    PostReplyPreview {
                                        replies: itemRoot.rowData.subPostPreview || []
                                        topMargin: 8
                                    }
                                    // A single visible preview already receives ListView.onTriggered.
                                    // Do not instantiate another label that could reserve a line.
                                    Container {
                                        id: replyLinkHost
                                        horizontalAlignment: HorizontalAlignment.Fill
                                    }
                                    attachedObjects: [
                                        ComponentDefinition {
                                            id: replyLinkDef
                                            Label {
                                                text: qsTr("查看全部回复（") + (itemRoot.rowData.subPostCount || 0) + qsTr("）")
                                                textStyle.fontSize: FontSize.Small
                                                textStyle.color: Color.Blue
                                            }
                                        }
                                    ]
                                }
                            },
                            ComponentDefinition {
                                id: imageBodyDef
                                Container {
                                    horizontalAlignment: HorizontalAlignment.Fill
                                    preferredHeight: ui.du(40)
                                    leftPadding: 20
                                    rightPadding: 20
                                    topMargin: 10
                                    bottomMargin: 10
                                    layout: DockLayout {}
                                    gestureHandlers: [
                                        TapHandler {
                                            onTapped: {
                                                itemRoot.ListItem.view.openImage(itemRoot.rowData.remoteImageUrl)
                                            }
                                        }
                                    ]
                                    ImageView {
                                        horizontalAlignment: HorizontalAlignment.Fill
                                        verticalAlignment: VerticalAlignment.Fill
                                        scalingMethod: ScalingMethod.AspectFit
                                        imageSource: itemRoot.rowData.localImagePath || ""
                                    }
                                }
                            },
                            ComponentDefinition {
                                id: emoticonBodyDef
                                Container {
                                    leftPadding: 20
                                    rightPadding: 20
                                    topPadding: 6
                                    bottomPadding: 6
                                    ImageView {
                                        imageSource: itemRoot.rowData.localImagePath || ""
                                        scalingMethod: ScalingMethod.AspectFit
                                        minHeight: 44
                                        maxHeight: 44
                                        minWidth: 44
                                        maxWidth: 44
                                        horizontalAlignment: HorizontalAlignment.Left
                                    }
                                }
                            }
                        ]
                    }
                }
            ]
            // 点击楼层：有楼中楼就进楼中楼页
            onTriggered: {
                var item = dataModel.data(indexPath)
                if (! item)
                    return
                if (item.subPostCount > 0) {
                    tieba.loadSubPosts(threadPage.threadId, item.id, 1)
                    threadPage.pushSubPosts(item.id, item.floor ? item.floor : "")
                }
            }

            attachedObjects: [
                LayoutUpdateHandler { id: postWidth },
                // 翻页：滚到底自动加载下一页楼层
                ListScrollStateHandler {
                    onAtEndChanged: {
                        if (atEnd && !tieba.posts.isEmpty() && !threadPage.navigationState.error.length)
                            threadPage.loadNextPage()
                    }
                }
            ]
        }

        // 空列表提示
        /*
         * 加载中：用 ActivityIndicator。
         *
         * ★ ActivityIndicator **只有 running 一个属性**
         *   （见 bb/cascades/controls/activityindicator.h）。
         *   visible / opacity 由 Cascades **自己接管**：
         *     running=true  -> visible=true,  opacity=1
         *     running=false -> visible=false, opacity=0
         *   所以这里**绝不能**手写 visible/opacity，设了也会被覆盖。
         */
        ActivityIndicator {
            running: threadPage.navigationState.loading
            horizontalAlignment: HorizontalAlignment.Center
            verticalAlignment: VerticalAlignment.Center
        }

        // 空状态提示：只在**加载结束后**才画，免得和转圈同时出现
        Label {
            visible: tieba.posts.isEmpty() && !threadPage.navigationState.loading
            horizontalAlignment: HorizontalAlignment.Center
            verticalAlignment: VerticalAlignment.Center
            text: (threadPage.navigationState.error && threadPage.navigationState.error.length > 0)
                  ? threadPage.navigationState.error
                  : "没有内容"
            multiline: true
            textStyle.textAlign: TextAlign.Center
            textStyle.color: Color.Gray
        }
    }

    /*!
     * 打开楼中楼页。
     * 楼中楼数据由上面的 onTriggered 先请求好再进来。
     * 导航栈从 nav 属性传给下一页（Page 自己找不到导航栈，见文件头说明）。
     */
    function pushSubPosts(postId, floorLabel) {
        if (! threadPage.nav) {
            console.log("ThreadPage: nav 未设置，无法打开楼中楼")
            return
        }
        var page = subPostsPageDef.createObject()
        if (! page)
            return
        page.nav = threadPage.nav
        page.postId = postId
        page.floorLabel = floorLabel
        // 楼中楼翻页要用到贴子 id
        page.threadId = threadPage.threadId
        threadPage.nav.push(page)
    }

    function pushUser(row) {
        if (!threadPage.nav) return
        var page = userDef.createObject()
        if (!page) return
        page.nav = threadPage.nav
        page.userData = tieba.uiUserProfile(row)
        page.refreshItems()
        threadPage.nav.push(page)
    }

    attachedObjects: [
        SystemPrompt {
            id: floorPrompt
            title: qsTr("跳转到楼层")
            body: qsTr("请输入要查看的楼层号")
            inputField.inputMode: SystemUiInputMode.NumericKeypad
            confirmButton.label: qsTr("跳转")
            cancelButton.label: qsTr("取消")
            onFinished: {
                if (result == SystemUiResult.ConfirmButtonSelection)
                    tieba.jumpThreadFloor(threadPage.threadId, floorPrompt.inputFieldTextEntry(), threadPage.onlyOp, threadPage.reverse)
            }
        },
        ComponentDefinition { id: userDef; source: "UserDetailPage.qml" },
        ComponentDefinition {
            id: subPostsPageDef
            source: "SubPostsPage.qml"
        }
    ]
}
