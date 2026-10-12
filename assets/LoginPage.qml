/*
 * 登录页（由 main.qml 的 NavigationPane push 进来）
 *
 * 目前只有一种登录方式：**粘贴 BDUSS / 整段 cookie**。
 * 不依赖验证码、RSA 加密、内嵌 WebView 那套东西 —— 用户从电脑浏览器
 * 的开发者工具里复制出来粘进来就行。
 *
 * 登录成功后自动 pop 回上一页：
 *   C++ 通过 /c/s/login 确认用户身份后会发 openRequest(kind="loginDone")，
 *   main.qml 收到就把本页弹掉。失败则留在本页，错误显示在下面。
 *
 * ⚠️ 输入侧的提示（剪贴板空 / 没填就点完成）留在本页的 inputError 里；
 *    C++ 的 tieba.lastLoginError 只报**校验结果**。
 */

import bb.cascades 1.4

Page {
    id: loginPage
    objectName: "loginPage"

    property string inputError: ""

    actions: [
        ActionItem {
            title: qsTr("完成")
            imageSource: "asset:///icons/ic_add_contact.png"
            ActionBar.placement: ActionBarPlacement.Signature
            // 校验期间灰掉，免得连点反复提交（loading 由 login() 打开）
            enabled: ! tieba.loading
            onTriggered: {
                var text = cookieTextArea.text ? cookieTextArea.text.trim() : ""
                if (text.length === 0) {
                    loginPage.inputError = qsTr("请先点「粘贴」，或直接把 BDUSS 的值填进来")
                    return
                }
                loginPage.inputError = ""
                // 立即返回；结果通过 loggedIn / lastLoginError 反映。
                // 成功后由 main.qml 收 openRequest 自动把本页弹掉。
                tieba.login(text)
            }
        },
        ActionItem {
            title: qsTr("粘贴")
            imageSource: "asset:///icons/ic_copy.png"
            ActionBar.placement: ActionBarPlacement.OnBar
            onTriggered: {
                // 读系统剪贴板（text/plain）。返回空串 = 剪贴板里没有文本
                var text = tieba.clipboardText()
                if (! text || text.length === 0) {
                    loginPage.inputError = qsTr("剪贴板里没有文本：请先在电脑浏览器里复制 BDUSS 或整段 Cookie")
                    return
                }
                cookieTextArea.text = text
                loginPage.inputError = ""
            }
        }
    ]

    Container {
        topPadding: 20
        leftPadding: 20
        rightPadding: 20
        layout: StackLayout {
            orientation: LayoutOrientation.TopToBottom
        }

        Label {
            text: qsTr("在电脑浏览器登录 tieba.baidu.com 后\r\n打开开发者工具 → Application → Cookies\r\n复制 BDUSS 的值或整段 Cookie")
            multiline: true
            textStyle {
                base: SystemDefaults.TextStyles.BodyText
            }
        }

        TextArea {
            id: cookieTextArea
            preferredHeight: 200
            inputMode: TextAreaInputMode.Text
            // 用户一改输入就把上一次的红字清掉
            onTextChanging: loginPage.inputError = ""
        }

//        Label {
//            text: qsTr("识别到的 BDUSS 长度：") + tieba.credentialInputLength(cookieTextArea.text || "") + qsTr(" 字符")
//            textStyle.fontSize: FontSize.XSmall
//            textStyle.color: Color.Gray
//        }
//
//        Label {
//            text: qsTr("输入字符数：") + (cookieTextArea.text || "").length + qsTr("。请核对复制的是 BDUSS 的值。")
//            multiline: true
//            textStyle.fontSize: FontSize.XSmall
//            textStyle.color: Color.Gray
//        }

        Container {
            visible: tieba.loading
            topPadding: 8
            layout: StackLayout {
                orientation: LayoutOrientation.LeftToRight
            }
            ActivityIndicator {
                running: true
                preferredWidth: 36
                preferredHeight: 36
                verticalAlignment: VerticalAlignment.Center
            }
            Label {
                text: qsTr("正在校验登录态…")
                verticalAlignment: VerticalAlignment.Center
                textStyle {
                    base: SystemDefaults.TextStyles.SmallText
                }
            }
        }

        Label {
            // 本页的输入错误优先，否则显示 C++ 回来的校验结果
            text: loginPage.inputError.length > 0 ? loginPage.inputError : tieba.lastLoginError
            visible: text.length > 0
            multiline: true
            textStyle {
                base: SystemDefaults.TextStyles.SmallText
                color: Color.create("#ff6b6b")
            }
        }
    }
}
