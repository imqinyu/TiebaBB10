/*
 * Copyright (c) 2011-2015 BlackBerry Limited.
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

import bb.cascades 1.4

/*
 * 主界面 —— 你的 4 Tab 结构（关注 / 动态 / 通知 / 账号）保持原样。
 * 
 * 我在里面只补了"接线"，没有改动你的布局：
 * 1. 每个 Tab 里放一个 NavigationPane
 * 真机头文件 bb/cascades/tab.h 写明：Tab 的 content 合法类型只有
 * Page 和 NavigationPane。用 NavigationPane 才能 push 子页面
 * （进吧页 / 贴子页 / 楼中楼页），系统返回键自动出栈。
 * 2. 关注 Tab 接上 tieba.forums（我关注的吧），列表项用你的 BaItem.qml
 * 3. 账号 Tab 接上登录态
 * 
 * 逻辑层入口是 `tieba`，在 src/applicationui.cpp 里通过
 * qml->setContextProperty("tieba", m_pTieba) 注入。
 * 
 * ⚠️ 在 ListView 的 ListItemComponent 里**不要**访问 tieba
 * （独立上下文，会 ReferenceError）。列表项要显示的数据一律由 C++ 解析好
 * 当字段传下来（例如 localAvatarPath）。
 */

TabbedPane {
    id: root

    Menu.definition: MenuDefinition {
        settingsAction: SettingsActionItem {
            onTriggered: {
                root.pushSettings()
            }
        }
        helpAction: HelpActionItem {
            onTriggered: {
                root.pushUtility(aboutPageDef, "")
            }
        }
        actions: [
            ActionItem {
                title: qsTr("账号管理")
                imageSource: "asset:///icons/ic_contact.png"
                onTriggered: {
                    // 账号管理页（多账号）；登录页从那里面的「新增账号」进
                    root.pushAccountManage()
                }
            },
            ActionItem {
                title: qsTr("签到")
                imageSource: "asset:///icons/ic_select_more.png"
                onTriggered: root.pushUtility(signPageDef, "")
            }
        ]
    }

    showTabsOnActionBar: false

    /*! 设置页要压上去的那个导航栈（当前 Tab 的 NavigationPane） */
    property variant settingsNav: null
    property variant utilityNav: null

    /*!
     * 子页面（账号管理页）发回来的"推/弹页"请求版本号。
     * 
     * ★ 为什么要绕这一圈：独立页拿不到本文件的 NavigationPane，
     * NeteaseMusic 踩过"子页里存 nav 对象会报 Unable to set property"。
     * 所以子页调 tieba.requestOpenLogin()，C++ 攒一下再顶版本号，
     * 由**这里**完成真正的 push / pop。
     * kind 取值见 TiebaController::requestOpenLogin 的说明。
     */
    property int openRequestVersion: tieba.openRequestVersion

    onOpenRequestVersionChanged: {
        var kind = tieba.openRequestKind
        if (kind.length == 0)
            return
        // 先清掉，免得同一个请求被处理两次
        tieba.clearOpenRequest()

        if (kind == "login")
            root.pushLogin();
        else if (kind == "loginDone")
            root.popLogin()
    }

    /*!
     * 打开设置页。
     * 
     * ★ 为什么用 root.activePane 而不是某个固定的 NavigationPane：
     * TabbedPane.activePane 是**当前这个 Tab** 的 pane（真机头文件
     * tabbedpane.h 里有这个 Q_PROPERTY），四个 Tab 里装的都是
     * NavigationPane，所以从哪一个 Tab 点开设置，页面就压在哪个栈上，
     * 返回键自然回到刚才那个 Tab。
     * 
     * 注意：activePane 的静态类型是 AbstractPane*（没有 push），
     * 但 QML 的方法调用是按**实际对象的元对象**解析的，
     * 实际对象是 NavigationPane，所以 push() 能调到。
     */
    function pushSettings() {
        root.settingsNav = root.activePane
        if (! root.settingsNav) {
            console.log("main: activePane 为空，无法打开设置")
            return
        }
        var page = settingsPageDef.createObject()
        if (! page)
            return
        root.settingsNav.push(page)
    }
    function pushUtility(definition, kind) {
        root.utilityNav = root.activePane
        if (!root.utilityNav) return
        var page = definition.createObject()
        if (!page) return
        // Qt 4 的 variant:null 读出来是 undefined；检查属性是否存在，
        // 不能用当前值的 typeof 判断，否则最近浏览/收藏页收不到导航栈。
        if ("nav" in page) page.nav = root.utilityNav
        if (kind.length > 0) {
            page.kind = kind
        }
        root.utilityNav.push(page)
    }

    /*! 打开账号管理页（多账号） */
    function pushAccountManage() {
        root.settingsNav = root.activePane
        if (! root.settingsNav) {
            console.log("main: activePane 为空，无法打开账号管理")
            return
        }
        var page = accountPageDef.createObject()
        if (! page)
            return
        root.settingsNav.push(page)
    }

    function pushSearch() {
        root.settingsNav = root.activePane
        if (!root.settingsNav) return
        var page = searchPageDef.createObject()
        if (!page) return
        page.nav = root.settingsNav
        root.settingsNav.push(page)
    }

    /*! 打开登录页（账号管理页的「新增账号」） */
    function pushLogin() {
        root.settingsNav = root.activePane
        if (! root.settingsNav) {
            console.log("main: activePane 为空，无法打开登录页")
            return
        }
        var page = loginPageDef.createObject()
        if (! page)
            return
        root.settingsNav.push(page)
    }

    /*!
     * 登录成功后把登录页弹掉。
     * ★ 只弹**栈顶是登录页**的情况：万一用户已经手动返回了，就别乱弹。
     */
    function popLogin() {
        var nav = root.activePane
        if (! nav)
            return
        if (nav.top && nav.top.objectName == "loginPage")
            nav.pop()
    }

    /*!
     * 应用上次保存的主题。
     * 
     * ★ 必须在 QML 里做（不能在 C++ 里）：切主题要用
     * Application.themeSupport.setVisualStyle()，那是 Cascades 的对象；
     * TiebaController 不依赖 Cascades（主机测试要用桩编译），
     * 把它塞进 C++ 会让本机编译检查挂掉。
     * 
     * 这里只负责"启动时恢复一次"，用户改主题由设置页自己调（那里同一套写法）。
     */
    function applySavedTheme() {
        if (tieba.theme == VisualStyle.Dark) {
            console.log("main: 恢复暗色主题")
            Application.themeSupport.setVisualStyle(VisualStyle.Dark)
        } else {
            Application.themeSupport.setVisualStyle(VisualStyle.Bright)
        }
    }

    onCreationCompleted: {
        root.applySavedTheme()
    }

    // ==================== 关注 ====================
    Tab {
        id: tab_follow
        title: "关注"
        imageSource: "asset:///icons/ic_saved_list.png"

        NavigationPane {
            id: followNav

            Page {
                id: followPage

                titleBar: TitleBar {
                    title: "关注"
                    visibility: ChromeVisibility.Compact
                }

                actions: [
                    ActionItem {
                        ActionBar.placement: ActionBarPlacement.Signature
                        title: "搜索"
                        imageSource: "asset:///icons/ic_search.png"
                        onTriggered: {
                            root.pushSearch()
                        }
                    },
//                    ActionItem {
//                        title: "签到"
//                        ActionBar.placement: ActionBarPlacement.OnBar
//                        imageSource: "asset:///icons/ic_select_more.png"
//                        onTriggered: {
//                            // 一键签到接口还没做（需要 tbs + 签到接口）。
//                        }
//                    },
                    ActionItem {
                        title: "刷新"
                        ActionBar.placement: ActionBarPlacement.InOverflow
                        imageSource: "asset:///icons/ic_sync.png"
                        onTriggered: {
                            // 用户主动刷新：跳过缓存直接请求网络。
                            // （首屏用的是 loadForums()，那是"先显示缓存再刷新"。）
                            tieba.refreshForums()
                        }
                    },
                    ActionItem {
                        imageSource: "asset:///icons/ic_sb_favorite.png"
                        title: "收藏帖子"
                        onTriggered: root.pushUtility(favoritesPageDef, "")
                        ActionBar.placement: ActionBarPlacement.OnBar

                    },
                    ActionItem {
                        title: "最近浏览"
                        onTriggered: root.pushUtility(threadLibraryPageDef, "history")
                        imageSource: "asset:///icons/ca_recent_contacts.png"
                        ActionBar.placement: ActionBarPlacement.OnBar

                    }
                ]

                /*
                 * 空状态 + 列表。
                 * 
                 * Cascades 的 ListView **没有 emptyText 属性**（真机头文件
                 * bb/cascades/listview.h 里没有 empty*），所以空提示要用
                 * "Container(DockLayout) + 覆盖 Label" 来搭。
                 * visible 用 tieba.forums.isEmpty() —— ArrayDataModel::isEmpty()
                 * 是 Q_INVOKABLE，QML 可直接调，模型变化时绑定自动重算。
                 */
                Container {
                    layout: DockLayout {
                    }

                    ListView {
                        id: forumList
                        property int contextVersion: tieba.contextVersion
                        function contextSubtitle(row, revision) { return tieba.contextSubtitle(row) }
                        function requestForumStats(row) { tieba.requestForumStats(row) }

                        // ★ 接线：用逻辑层的模型
                        dataModel: tieba.forums

                        /*
                         * ============ 长按菜单（BaItem.qml 里的 contextActions）============
                         * 
                         * BaItem 的列表项里通过 `ListItem.view.xxx()` 调到这里
                         * （ListItemComponent 是独立上下文，访问不到 tieba）。
                         */
                        function copyForumName(name) {
                            if (! name || name.length == 0)
                                return
                            var ok = tieba.copyText(name)
                            console.log("main: 复制吧名 " + (ok ? "成功" : "失败") + " " + name)
                        }

                        /*
                         * 打开吧详情。
                         * 
                         * ★ 吧详情页的数据来自吧页接口（frs/page）返回的 forum 对象，
                         * 所以**必须先 loadForumPage 过**。长按菜单能直接看到吧详情，
                         * 不需要先进吧 —— 这里先请求一次再推页面。
                         */
                        function openForumDetail(name) {
                            if (! name || name.length == 0)
                                return
                            tieba.loadForumPage(name, 1, 0, false)
                            followPage.pushForumDetailFor(name)
                        }

                        snapMode: SnapMode.Default

                        // 你选的横向流动布局
                        layout: StackListLayout {
                        }

                        // ★ 接线：列表项用你的 BaItem.qml（里面已接好数据与头像）
                        listItemComponents: [
                            ListItemComponent {
                                type: ""
                                BaItem {
                                }
                            }
                        ]

                        // ★ 接线：点击进吧
                        //   放在 onTriggered 里是因为这里在页面作用域，能访问 tieba
                        onTriggered: {
                            var item = dataModel.data(indexPath)
                            if (! item || ! item.name)
                                return

                            // 先发数据请求再推页面 —— 页面一进来就有内容
                            tieba.loadForumPage(item.name, 1, 0, false)
                            followPage.pushInBa(item.name)
                        }
                    }

                    /*
                     * 加载中：用 ActivityIndicator。
                     * 
                     * ★ ActivityIndicator **只有 running 一个属性**
                     * （见 bb/cascades/controls/activityindicator.h）。
                     * visible / opacity 由 Cascades **自己接管**：
                     * running=true  -> visible=true,  opacity=1
                     * running=false -> visible=false, opacity=0
                     * 所以这里**绝不能**手写 visible/opacity，设了也会被覆盖。
                     */
                    ActivityIndicator {
                        running: tieba.loading
                        horizontalAlignment: HorizontalAlignment.Center
                        verticalAlignment: VerticalAlignment.Center
                    }

                    // 空状态提示：只在**加载结束后**才画，免得和转圈同时出现
                    Label {
                        visible: tieba.forums.isEmpty() && ! tieba.loading
                        horizontalAlignment: HorizontalAlignment.Center
                        verticalAlignment: VerticalAlignment.Center
                        text: (tieba.errorMessage && tieba.errorMessage.length > 0) ? tieba.errorMessage : "还没有关注的吧"
                        multiline: true
                        textStyle.textAlign: TextAlign.Center
                        textStyle.color: Color.Gray
                    }
                }

                /*
                 * 打开某个吧：创建进吧页并推入导航栈。
                 * 
                 * ★★ 这个函数必须定义在**本 Page 里**，不能放到外面的
                 * NavigationPane 里 —— 放错位置真机会报：
                 * TypeError: Result of expression
                 * 'followPage.pushInBa' [undefined] is not a function.
                 * 因为 QML 的函数只对自身和子对象可见，兄弟分支看不到。
                 * 
                 * ★ 为什么要把 followNav 传进去（page.nav = followNav）：
                 * NavigationPane::push() 只接受 Page*，而 Page 上没有任何
                 * push()/navigationPane 接口（真机头文件零匹配），
                 * 页面无法自己找到所属导航栈，必须由创建方传下去。
                 */
                function pushInBa(forumName) {
                    var page = inBaPageDef.createObject()
                    if (! page)
                        return
                    page.nav = followNav
                    page.forumName = forumName
                    followNav.push(page)
                }

                /*
                 * 直接打开某个吧的详情页（长按菜单"查看吧详情"用）。
                 * 和 InBaPage 里的 pushForumDetail 是同一件事，只是这里
                 * 不是从吧内页进入，所以吧名要现传。
                 */
                function pushForumDetailFor(forumName) {
                    var page = forumDetailDef.createObject()
                    if (! page)
                        return
                    page.nav = followNav
                    page.forumName = forumName
                    followNav.push(page)
                }

                attachedObjects: [
                    ComponentDefinition {
                        id: inBaPageDef
                        source: "InBaPage.qml"
                    },
                    // 长按吧项 -> "查看吧详情" 要用
                    ComponentDefinition {
                        id: forumDetailDef
                        source: "ForumDetailPage.qml"
                    }
                ]
                actionBarAutoHideBehavior: ActionBarAutoHideBehavior.HideOnScroll
            }
        }
    }

    // ==================== 动态：关注 / 推荐 / 流行热帖 ====================
    Tab {
        id: tab_news
        title: qsTr("动态")
        imageSource: "asset:///icons/ic_sync.png"
        NavigationPane {
            id: newsNav
            FeedPage {
                nav: newsNav
                active: root.activeTab === tab_news
            }
            onPopTransitionEnded: { page.destroy() }
        }
    }

    // ==================== 通知：回复 / 提及 ====================
    Tab {
        id: tab_notification
        title: qsTr("通知")
        imageSource: "asset:///icons/ic_set_reminder.png"
        NavigationPane {
            id: notificationNav
            NotificationPage {
                nav: notificationNav
                active: root.activeTab === tab_notification
            }
            onPopTransitionEnded: { page.destroy() }
        }
    }
    // ==================== 账号：与其他用户共用详情布局 ====================
    Tab {
        id: tab_account
        title: "账号"
        imageSource: "asset:///icons/ic_contact.png"
        NavigationPane {
            id: accountNav
            UserDetailPage {
                ownProfile: true
                nav: accountNav
                onAccountManageRequested: root.pushAccountManage()
            }
        }
    }
    /*
     * 首屏加载 —— 现在由 **C++ 负责**（TiebaController::init() 末尾），
     * 这里刻意**什么都不做**。原因踩过两个坑，记下来别改回去：
     * 
     * ★★ 坑 1：onCreationCompleted 太早
     * applicationui.cpp 的顺序是
     * createRootObject()   ← 这一刻 onCreationCompleted 就触发了
     * setScene()
     * tieba->init()        ← 登录态在这之后才建立
     * 所以在这里读 tieba.loggedIn 永远是 false，loadForums() 不会执行。
     * 
     * ★★ 坑 2：QML 根部**没法**监听 context property 的信号
     * 我先试了 `onLoggedInChanged:`，真机报：
     * Cannot assign to non-existent property "onLoggedInChanged"
     * 改成信号真名 `onLoginChanged:`，**还是同样的错**：
     * Cannot assign to non-existent property "onLoginChanged"
     * 原因：QML 的 onXxx 处理器要求**该组件自己**有 xxx 信号。
     * `tieba` 是 setContextProperty 注入的对象，它的信号属于它自己，
     * 不属于 TabbedPane —— 所以无论如何都合成不出这个处理器。
     * （Cascades 也没有 Qt Quick 的 Connections 可以用。）
     * 
     ***结论：涉及 tieba 状态驱动 UI 初始化的逻辑，一律放 C++。**
     * 放在 init() 里时序自己说了算，也不依赖 QML 特性。
     * 
     * ★ 唯一的例外是**应用主题**（上面的 applySavedTheme）：那是纯 UI 动作、
     * 不依赖登录态，而且只能在 QML 里做（要调 Application.themeSupport）。
     */

    attachedObjects: [
        ComponentDefinition { id: aboutPageDef; source: "AboutPage.qml" },
        ComponentDefinition { id: signPageDef; source: "SignPage.qml" },
        ComponentDefinition { id: threadLibraryPageDef; source: "ThreadLibraryPage.qml" },
        ComponentDefinition { id: favoritesPageDef; source: "FavoritesPage.qml" },
        ComponentDefinition {
            id: searchPageDef
            source: "SearchPage.qml"
        },
        ComponentDefinition {
            id: settingsPageDef
            source: "SettingsPage.qml"
        },
        ComponentDefinition {
            id: accountPageDef
            source: "AccountManagePage.qml"
        },
        ComponentDefinition {
            id: loginPageDef
            source: "LoginPage.qml"
        }
    ]
}
