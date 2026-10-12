// AboutPage - 关于（应用菜单的 helpAction 打开）

import bb.cascades 1.4

Page {
    id: aboutPage
    objectName: "aboutPage"

    titleBar: TitleBar {
        title: qsTr("关于")
    }

    ScrollView {
        Container {
          
            layout: StackLayout {
                orientation: LayoutOrientation.TopToBottom
            }

            leftPadding: ui.du(1.0)
            rightPadding: ui.du(1.0)
            ImageView {
                horizontalAlignment: HorizontalAlignment.Center
                preferredWidth: ui.sdu(20)
                preferredHeight: ui.sdu(20)
                imageSource: "asset:///icons/icon.png"
                scalingMethod: ScalingMethod.AspectFill
            }

            Label {
                topMargin: ui.sdu(2)
                horizontalAlignment: HorizontalAlignment.Center
                text: qsTr("百度贴吧BB10")
                textStyle {
                    base: SystemDefaults.TextStyles.TitleText
                }
            }

            Header {
                title: qsTr("说明")
            }
            Label {
                multiline: true

                text: "百度贴吧第三方版 仅供学习使用\r\n免费分享 禁止倒卖\r\n欢迎加入BlackBerryPhonix\r\nQQ交流群:892633534"
                textStyle {
                    base: SystemDefaults.TextStyles.BodyText
                }
            }

           
            Header {
                title: qsTr("开发团队")
            }
            Label {
                multiline: true

                text: "UI原型、QA：Fremantle\r\n编程：Hy4、Deepseek V4.1、ChatGPT 6"
                textStyle {
                    base: SystemDefaults.TextStyles.BodyText
                }
            }
            Header {
                title: qsTr("鸣谢")
            }
            Label {
                multiline: true

                text: "排名不分先后\r\nwesley：赞助GPT6\r\nOleksandr：提供越狱漏洞和工具\r\nPablo from WaitBerry:提供参考文档\r\nGithub NeteaseCloudMusicApiEnhanced项目：提供网易云接口\r\nAnwar Ludin、Davenson Lombard：提供免费的黑莓开发教程\r\nTiebaLite项目：提供现成参考\r\n以及所有支持我的AI朋友们"
                textStyle {
                    base: SystemDefaults.TextStyles.BodyText
                }
            }
        }
    }

//    actions: [
//        ActionItem {
//            title: qsTr("接口调试日志")
//            imageSource: "asset:///icons/ic_help.png"
//            ActionBar.placement: ActionBarPlacement.OnBar
//            onTriggered: {
//                music.setDebugEnabled(true)
//            }
//        }
//    ]
}
