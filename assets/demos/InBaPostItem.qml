import bb.cascades 1.4
//吧内帖子列表item
Container {

    maxHeight: 250

    Container {
        layout: StackLayout {
            orientation: LayoutOrientation.LeftToRight

        }
        verticalAlignment: VerticalAlignment.Bottom
        ImageView {
            maxWidth: ui.du(10)
            preferredWidth: ui.du(10)
            preferredHeight: ui.du(10)
            maxHeight: ui.du(10)
            imageSource: "asset:///images/default_avatar.png" //默认头像 用户头像没加载出来的时候就显示这个
            minWidth: ui.du(10.0)
            minHeight: ui.du(10.0)
        }
        Container {
            layout: StackLayout {

            }

            Container {
                layout: StackLayout {
                    orientation: LayoutOrientation.LeftToRight

                }

                Label {
                    text: "用户名"

                }
                Container {
                    topPadding: 6.0
                    clipContentToBounds: true
                    //用户等级
                    ImageView {
                        imageSource: "asset:///icons/grades/icon_grade_lv1.png"

                    }

                }

            }


                Label {
                    text: "xxxx年xx月xx日"
                    verticalAlignment: VerticalAlignment.Top
                topMargin: 5.0
                    bottomMargin: 0
                    
                }
            

        }
        Container {

            horizontalAlignment: HorizontalAlignment.Right
            verticalAlignment: VerticalAlignment.Center
            leftPadding: 300.0
            Label {
                text: "xx吧"

            }
        }
    }
    Container {
        leftPadding: ui.du(1.0)
        rightPadding: ui.du(1.0)
        Label {
            text: "帖子标题"
            textStyle.fontSize: FontSize.Medium

        }
        Label {
            text: "内容预览"//只显示一行 多了显示不下

        }

    }
    Container {
        horizontalAlignment: HorizontalAlignment.Right
        leftPadding: ui.du(1.0)
        rightPadding: ui.du(1.0)
        Label {
            text: "xx赞 xx回复"

        }
    }

}
