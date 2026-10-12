/*
 * 楼中楼页（新文件）
 *
 * 显示 tieba.subPosts 模型（某个楼层下的所有回复）。
 *
 * 注意：这里显示的是**点"查看全部楼中楼(N)"进来的那一页**。
 * 楼层正文里内嵌的前几条预览（subPostPreviewText 字段）是另一回事，
 * 由 C++ 拼成多行字符串直接显示在贴子页里，不走这个模型。
 *
 * 结构：根是 **Page**（不是 NavigationPane）。
 *   NavigationPane::push() 只接受 Page*，所以被 push 的页面根都必须是 Page；
 *   它们共用关注 Tab 里那一个导航栈（栈引用由调用方通过 nav 属性传入，
 *   因为 Page 上没有任何找回导航栈的接口）。这是最深的一层，不再 push。
 */

import bb.cascades 1.4

Page {
    id: subPostsPage

    /*! 所属导航栈。由 ThreadPage.qml 的 pushSubPosts() 设置（本页不再 push，留作扩展）。 */
    property variant nav: null

    /*! 所在楼层的 post id。由 ThreadPage.qml 的 pushSubPosts() 设置。 */
    property string postId: ""
    /*! 楼层号，仅用于标题显示 */
    property string floorLabel: ""
    /*! 所在贴子 id。由调用方设置，翻页请求要用。 */
    property string threadId: ""

    titleBar: TitleBar {
        title: subPostsPage.floorLabel.length > 0
               ? (qsTr("楼中楼 · 第") + subPostsPage.floorLabel + qsTr("楼"))
               : qsTr("楼中楼")
    }

    actionBarVisibility: ChromeVisibility.Visible

    actions: [
        ActionItem {
            title: qsTr("加载更多")
            imageSource: "asset:///icons/ic_select_more.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            enabled: !tieba.loading && tieba.hasMore
            onTriggered: tieba.loadSubPosts(subPostsPage.threadId, subPostsPage.postId, subPostsPage.pageCurrent() + 1)
        },
        ActionItem {
            title: qsTr("刷新")
            imageSource: "asset:///icons/ic_sync.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            onTriggered: {
                tieba.loadSubPosts(subPostsPage.threadId,
                                   subPostsPage.postId, 1)
            }
        }
    ]

    /*
     * 安全读取当前页码。
     * ★ 不能写 tieba.pageInfo.currentPage：空 map 上取属性会 SIGSEGV
     *   （真机实测，见 InBaPage.qml 里的详细说明）。
     */
    function pageCurrent() {
        return (tieba.pageInfo && tieba.pageInfo.currentPage)
               ? tieba.pageInfo.currentPage : 1
    }

    /*
     * 空状态 + 列表。
     *
     * Cascades 的 ListView **没有 emptyText 属性**（真机头文件里没有 empty*），
     * 空提示要用 "Container(DockLayout) + 覆盖 Label" 来搭。
     * visible 用 tieba.subPosts.isEmpty()（ArrayDataModel::isEmpty() 是 Q_INVOKABLE）。
     */
    Container {
        layout: DockLayout {
        }

        ListView {
            id: subList

            dataModel: tieba.subPosts

            /*!
             * 长按菜单的处理函数（列表项通过 ListItem.view.xxx() 调到这里，
             * 因为 ListItemComponent 是独立上下文，访问不到 tieba）。
             */
            function copyText(text) {
                if (! text || text.length == 0)
                    return
                var ok = tieba.copyText(text)
                console.log("SubPostsPage: 复制 " + (ok ? "成功" : "失败"))
            }

            /*!
             * 打开层主个人页。
             *
             * ★ 个人页还没做（需要用户主页接口），所以这里先只把作者信息打进日志 ——
             *   至少能确认菜单点到了、数据是对的。
             *   等个人页做好，把这里换成 pushUserPage(...) 即可。
             */
            function openUserPage(row) {
                subPostsPage.pushUser(row)
            }

            listItemComponents: [
                ListItemComponent {
                    type: ""

                    /*
                     * ★ 这一层用 **CustomListItem**（Cascades 自带的列表项组件），
                     *   取代原来"外面套一层 Container 只为挂 contextActions"的写法。
                     *   按压高亮与分隔线交给框架，布局一行不动
                     *   （CustomListItem 的默认属性是 content）。
                     *   ListItem.* 只在根元素上有值，所以这层必须有 id。
                     */
                    CustomListItem {
                        id: subItemRoot

                        contextActions: [
                            ActionSet {
                            ActionItem {
                                title: qsTr("复制")
                                onTriggered: {
                                    subItemRoot.ListItem.view.copyText(
                                        ListItemData.plainText)
                                }
                                    imageSource: "asset:///icons/ic_copy.png"
                                }
                            ActionItem {
                                title: qsTr("打开层主个人页")
                                onTriggered: {
                                    subItemRoot.ListItem.view.openUserPage(
                                        ListItemData)
                                }
                                    imageSource: "asset:///icons/ic_open_contacts.png"
                                }
                        } ]

                    Container {
                        leftPadding: 20
                        rightPadding: 20
                        topPadding: 14
                        bottomPadding: 14
                        layout: StackLayout {
                        }

                        AuthorHeader {
                            itemData: ListItemData
                            onUserTapped: subItemRoot.ListItem.view.openUserPage(ListItemData)
                        }

                        PostTextContent {
                            segments: ListItemData.displaySegments || []
                            fallbackText: ListItemData.plainText || ""
                            topMargin: 4
                        }
                    }
                    }
                }
            ]

            attachedObjects: [
                // 翻页：楼中楼多了也要能滚着加载
                ListScrollStateHandler {
                    onAtEndChanged: {
                        if (atEnd && !tieba.subPosts.isEmpty() && !tieba.loading && tieba.hasMore && !tieba.errorMessage.length) {
                            tieba.loadSubPosts(subPostsPage.threadId,
                                               subPostsPage.postId,
                                               subPostsPage.pageCurrent() + 1)
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
            visible: tieba.subPosts.isEmpty() && ! tieba.loading
            horizontalAlignment: HorizontalAlignment.Center
            verticalAlignment: VerticalAlignment.Center
            text: (tieba.errorMessage && tieba.errorMessage.length > 0)
                  ? tieba.errorMessage
                  : "没有楼中楼"
            multiline: true
            textStyle.textAlign: TextAlign.Center
            textStyle.color: Color.Gray
        }
    }
    function pushUser(row) {
        if (!subPostsPage.nav) return
        var page = userDef.createObject()
        if (!page) return
        page.nav = subPostsPage.nav
        page.userData = tieba.uiUserProfile(row)
        page.refreshItems()
        subPostsPage.nav.push(page)
    }
    attachedObjects: [ ComponentDefinition { id: userDef; source: "UserDetailPage.qml" } ]
}
