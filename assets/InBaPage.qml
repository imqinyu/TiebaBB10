/*
 * 进吧页（你的 InBaPage.qml 的接线版）
 *
 * 保留你原有的：
 *   - TitleBarKind.Segmented（最新 / 精华）  ← 已接到排序参数上，真的能用
 *   - actionBarVisibility / actionBarAutoHideBehavior
 *   - "吧内搜索" ActionItem
 *
 * 结构说明：根是 **Page**（你的原文件也是 Page，这点没变）。
 *
 *   ★ 关于页面跳转（这里踩过一次，别改回去）：
 *     NavigationPane::push() 的签名是 push(bb::cascades::Page*) —— 只接受 Page，
 *     而且 **Page 上没有任何 push()/navigationPane 接口**（真机头文件里零匹配），
 *     也就是说页面**无法自己找到所属的导航栈**。
 *     所以导航栈引用必须由外部创建方显式传进来，就是下面的 `nav` 属性。
 *     main.qml 创建本页时会做 `page.nav = followNav`。
 *
 * ⚠️ 列表项里不要访问 tieba（ListItemComponent 是独立上下文，会 ReferenceError）。
 */

import bb.cascades 1.4

Page {
    id: inBaPage

    /*!
     * 所属导航栈。由创建方在 push 之前设置：
     *     page.nav = followNav
     * 用 variant 而不是具体类型，方便空引用判断。
     */
    property variant nav: null

    /*! 当前吧名。由 main.qml 的 pushInBa() 在 push 之前设置。 */
    property string forumName: ""

    /*! 当前排序方式：0 = 最近回复，1 = 新发布。 */
    property int sortType: 0
    /*! 是否只看精品（精华）。★ 和排序无关，服务端 is_good=1。 */
    property bool goodOnly: false

    /*
     * 分段标题栏：最新 / 精华
     *
     * ★★ 这里踩过一个大坑，别再改回去：
     *    Option **没有 onSelected 信号**！真机报错原文：
     *        asset:///InBaPage.qml:45:17:
     *        Cannot assign to non-existent property "onSelected"
     *        QmlDocument:createRootObject ... can't create root
     *    → 整个 InBaPage 加载失败，所以"点吧点不进去"。
     *
     *    查 bb/cascades/option.h 确认：Option 的属性只有
     *        image / imageSource / text / description / value / selected / enabled / focused
     *    其中 selected 是**普通布尔属性**，不是信号，不能写 onSelected。
     *
     *    正确的监听办法：TitleBar 自己有
     *        Q_PROPERTY(int selectedIndex ... NOTIFY selectedIndexChanged)
     *    所以用 **onSelectedIndexChanged** 监听分段切换。
     *    （SegmentedControl 也有 selectedIndexChanged；
     *      但 TitleBar 没有办法直接监听某个 Option 被点。）
     */
    titleBar: TitleBar {
        id: inBaTitleBar
        kind: TitleBarKind.Segmented
        options: [
            Option {
                text: "最新"
                selected: true
            },
            Option {
                text: "精华"
            }
        ]

        /*
         * 0 = 最新列表，1 = 精华列表；保留当前帖子排序方式。
         *
         * ★ 注意「精华」**不是** sort_type=1，而是服务端独立的 is_good=1 参数。
         *   这是查 Android 版 TiebaLite 的 frsPage 实现确认的
         *   （MixedTiebaApiImpl.kt: is_good = if (goodClassifyId != null) 1 else 0，
         *     而 sort_type 是另一个字段）。映射逻辑在
         *   TbApi::fetchForumPagePc 里，这里只管把 0/1 传对。
         */
        onSelectedIndexChanged: {
            // 刚创建时 selectedIndex 会从 -1 变成 0，那时 forumName 还没设，
            // 不该发请求（而且进吧时已经请求过一次第一页了）。
            if (! inBaPage.forumName || inBaPage.forumName.length == 0)
                return

            /*
             * 0 = 最新：不筛精品
             * 1 = 精华：额外 is_good=1
             * 两者保留当前选择的新发布 / 最近回复排序。
             *
             * ★ 注意"精华"用的是 goodOnly，**不是** sort_type=1 ——
             *   安卓里 sort_type=1 的含义是"按发帖时间"。
             */
            var wantSort = inBaPage.sortType
            var wantGood = (selectedIndex == 1)
            if (wantSort != inBaPage.sortType || wantGood != inBaPage.goodOnly) {
                inBaPage.goodOnly = wantGood
                inBaPage.reload(wantSort, wantGood)
            }
        }
    }

    actionBarVisibility: ChromeVisibility.Visible
    actionBarAutoHideBehavior: ActionBarAutoHideBehavior.HideOnScroll

    actions: [
        ActionItem {
            title: qsTr("加载更多")
            imageSource: "asset:///icons/ic_select_more.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            enabled: !tieba.loading && tieba.hasMore
            onTriggered: tieba.loadForumPage(inBaPage.forumName, inBaPage.pageCurrent() + 1, inBaPage.sortType, true, inBaPage.goodOnly)
        },
        ActionItem {
            title: "吧内搜索"
            imageSource: "asset:///icons/ic_search.png"
            ActionBar.placement: ActionBarPlacement.Signature
            onTriggered: {
                inBaPage.pushSearch()
            }
        },
        ActionItem {
            title: "刷新"
            imageSource: "asset:///icons/ic_sync.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            onTriggered: {
                inBaPage.reload(inBaPage.sortType)
            }
        },
        ActionItem {
            title: "详情"
            onTriggered: {
                // 打开吧详情页。
                // 数据来自 tieba.currentForumDetail()，即吧页接口返回的 forum
                // 对象 —— 进这个吧时已经请求过，所以这里不用再发网络请求。
                inBaPage.pushForumDetail()
            }
            imageSource: "asset:///icons/ic_info.png"
            ActionBar.placement: ActionBarPlacement.OnBar
        },
        ActionItem {
            imageSource: "asset:///icons/ic_sort.png"
            ActionBar.placement: ActionBarPlacement.OnBar
            title: inBaPage.sortType === 1 ? qsTr("最近回复排序") : qsTr("新发布排序")
            enabled: inBaPage.forumName.length > 0 && !tieba.loading
            onTriggered: {
                inBaPage.reload(inBaPage.sortType === 1 ? 0 : 1, inBaPage.goodOnly)
            }
        }
    ]

    /*!
     * 打开吧详情页。
     *
     * 导航栈引用（nav）由 main.qml 在 push 本页时传进来；
     * 这里再原样传给详情页 —— Page 上没有找回导航栈的接口，只能这么传。
     */
    function pushForumDetail() {
        if (! inBaPage.nav) {
            console.log("InBaPage: nav 未设置，无法打开吧详情")
            return
        }
        var page = forumDetailDef.createObject()
        if (! page)
            return
        page.nav = inBaPage.nav
        page.forumName = inBaPage.forumName
        inBaPage.nav.push(page)
    }

    function pushSearch() {
        if (!inBaPage.nav) return
        var page = searchDef.createObject()
        if (!page) return
        page.nav = inBaPage.nav
        page.forumName = inBaPage.forumName
        inBaPage.nav.push(page)
    }

    function pushUser(row) {
        if (!inBaPage.nav) return
        var page = userDef.createObject()
        if (!page) return
        page.nav = inBaPage.nav
        page.userData = tieba.uiUserProfile(row)
        page.refreshItems()
        inBaPage.nav.push(page)
    }

    /*!
     * 安全读取当前页码。
     *
     * ★ 不能直接写 tieba.pageInfo.currentPage：
     *   pageInfo 是 QVariantMap，C++ 还没灌数据时是**空 map**，
     *   在空 map 上取属性会让 QML 引擎把进程打挂
     *   （真机实测 SIGSEGV in QString::operator=）。
     */
    function pageCurrent() {
        return (tieba.pageInfo && tieba.pageInfo.currentPage)
               ? tieba.pageInfo.currentPage : 1
    }

    /*!
     * 重新加载第一页。
     *
     * @param sort     **排序方式**（和安卓 ForumSortType 对齐）：
     *                     0 = 按回复时间（最新）
     *                     1 = 按发帖时间
     * @param goodOnly 是否只看精品（服务端 is_good=1），**与排序无关**
     *
     * ★ 这里以前把"精华"和排序混成一个参数，导致传了 sort_type=-1
     *   （-1 不是合法的 REPLY_TIME，安卓那边是 0），
     *   结果列表里的贴子不是最新的。现在两个概念分开了。
     */
    function reload(sort, goodOnly) {
        inBaPage.sortType = sort
        var wantGood = typeof goodOnly === "undefined" ? inBaPage.goodOnly : goodOnly === true
        inBaPage.goodOnly = wantGood
        tieba.loadForumPage(inBaPage.forumName, 1, sort, false, wantGood)
    }

    /*
     * 空状态 + 列表。
     *
     * Cascades 的 ListView **没有 emptyText 属性**（真机头文件里没有 empty*），
     * 所以空提示要用 "Container(DockLayout) + 覆盖 Label" 来搭。
     * visible 用 tieba.threads.isEmpty() —— ArrayDataModel::isEmpty() 是
     * Q_INVOKABLE，QML 可调，模型变化时绑定自动重算。
     */
    Container {
        //minWidth: 5000.0
        layout: DockLayout {
        }

        ListView {
            id: threadList
            property string displayForumName: inBaPage.forumName
            function openUser(row) { inBaPage.pushUser(row) }

            dataModel: tieba.threads

            /*
             * ==================== 长按菜单要用到的处理函数 ====================
             *
             * ★ 为什么定义在 ListView 上（而不是直接写进 ActionItem）：
             *   ListItemComponent 是**独立上下文**，访问不到 tieba。
             *   官方文档给的桥是 ListItem.view：
             *     "If you need to access variables placed in the ListView in a
             *      ListItemComponent, you can access the variables using the
             *      ListItem.view property."（bb/cascades/controls/listview.h）
             *   所以 item 里写 itemRoot.ListItem.view.xxx(...)，由这里落到 tieba。
             */
            function copyThreadLink(threadId) {
                if (! threadId || threadId.length == 0)
                    return
                var url = "https://tieba.baidu.com/p/" + threadId
                var ok = tieba.copyText(url)
                console.log("InBaPage: 复制贴子链接 " + (ok ? "成功" : "失败") + " " + url)
            }

            function favoriteThread(threadId, title) {
                console.log("InBaPage: 收藏 " + threadId + " " + title)
                if (! tieba.favoriteThread(threadId)) {
                    console.log("InBaPage: 收藏功能还没做（缺 tbs），这里只是占位")
                }
            }
            property int favoriteVersion: typeof favoriteService === "undefined" ? 0 : favoriteService.version
            property bool favoriteBusy: typeof favoriteService === "undefined" ? true : favoriteService.busy
            function favoriteTitle(threadId, revision) {
                return typeof favoriteService !== "undefined" && favoriteService.status(threadId) === 1 ? qsTr("取消收藏") : qsTr("收藏")
            }

            listItemComponents: [
                ListItemComponent {
                    type: ""

                    /*
                     * ★ 这一层用 **CustomListItem**（Cascades 自带的列表项组件），
                     *   取代了原来"外面套一层 Container 只为挂 contextActions"的写法。
                     *
                     *   好处：按压高亮、分隔线（dividerVisible）都由框架给，
                     *   和系统的其它列表观感一致（第6章：Header / StandardListItem /
                     *   CustomListItem 是框架自带的三种列表项）。
                     *   CustomListItem 的默认属性是 content，里面那层 Container
                     *   直接写就行，布局一行都不用动。
                     *
                     *   注意 ListItem.* 只在**根元素**上有值，所以这层必须有 id。
                     */
                    CustomListItem {
                        id: threadItemRoot

                        contextActions: [
                            ActionSet {
                            title: ListItemData.title || ""
                            subtitle: ListItemData.authorName || ""
                            ActionItem {
                                title: threadItemRoot.ListItem.view.favoriteTitle(ListItemData.id, threadItemRoot.ListItem.view.favoriteVersion)
                                enabled: !threadItemRoot.ListItem.view.favoriteBusy
                                imageSource: "asset:///icons/ic_sb_favorite.png"
                                onTriggered: {
                                    threadItemRoot.ListItem.view.favoriteThread(
                                        ListItemData.id, ListItemData.title)
                                }
                            }
                            ActionItem {
                                title: qsTr("复制链接")
                                imageSource: "asset:///icons/ic_browser.png"
                                onTriggered: {
                                    threadItemRoot.ListItem.view.copyThreadLink(
                                        ListItemData.id)
                                }
                            }
                        } ]

                        ThreadCard {
                            itemData: ListItemData
                            showForumName: false
                            forumName: threadItemRoot.ListItem.view.displayForumName
                            onUserTapped: threadItemRoot.ListItem.view.openUser(ListItemData)
                        }
                    }
                }
            ]
            // 点击进贴（页面作用域，可以访问 tieba）
            onTriggered: {
                var item = dataModel.data(indexPath)
                if (! item || ! item.id)
                    return

                // 先请求正文再推页面 —— 页面一进来就有内容
                tieba.loadThread(item.id, 1, false, false, false)
                inBaPage.pushThread(item.id, item.title ? item.title : "")
            }

            attachedObjects: [
                // 翻页：滚到底自动加载下一页。
                // 必须判 tieba.loading，否则会连发好几个请求。
                ListScrollStateHandler {
                    onAtEndChanged: {
                        if (atEnd && !tieba.threads.isEmpty() && !tieba.loading && tieba.hasMore && !tieba.errorMessage.length) {
                            tieba.loadForumPage(inBaPage.forumName,
                                                inBaPage.pageCurrent() + 1,
                                                inBaPage.sortType, true,
                                                inBaPage.goodOnly)
                        }
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
            running: tieba.loading
            horizontalAlignment: HorizontalAlignment.Center
            verticalAlignment: VerticalAlignment.Center
        }

        // 空状态提示：只在**加载结束后**才画，免得和转圈同时出现
        Label {
            visible: tieba.threads.isEmpty() && ! tieba.loading
            horizontalAlignment: HorizontalAlignment.Center
            verticalAlignment: VerticalAlignment.Center
            text: (tieba.errorMessage && tieba.errorMessage.length > 0)
                  ? tieba.errorMessage
                  : "这个吧还没有贴子"
            multiline: true
            textStyle.textAlign: TextAlign.Center
            textStyle.color: Color.Gray
        }
    }

    /*!
     * 打开贴子。
     *
     * 用创建方传进来的导航栈（nav）push。
     * 不能写 inBaPage.push(...) —— Page 上没有 push()，见文件头说明。
     */
    function pushThread(threadId, title) {
        if (! inBaPage.nav) {
            console.log("InBaPage: nav 未设置，无法打开贴子")
            return
        }
        var page = threadPageDef.createObject()
        if (! page)
            return
        page.nav = inBaPage.nav
        page.threadId = threadId
        page.threadTitle = title
        inBaPage.nav.push(page)
    }

    attachedObjects: [
        ComponentDefinition { id: searchDef; source: "SearchPage.qml" },
        ComponentDefinition { id: userDef; source: "UserDetailPage.qml" },
        ComponentDefinition {
            id: threadPageDef
            source: "ThreadPage.qml"
        },
        ComponentDefinition {
            id: forumDetailDef
            source: "ForumDetailPage.qml"
        }
    ]
}
