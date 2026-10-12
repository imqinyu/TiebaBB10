import bb.cascades 1.4
//帖内回复demo
Container {
    layout: StackLayout {
        orientation: LayoutOrientation.TopToBottom

    }

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
            imageSource: "asset:///default_avatar.png" //默认头像 用户头像没加载出来的时候就显示这个
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
                        imageSource: "asset:///icon_grade_lv1.png"

                    }

                }

            }
            
            Container {

                Label {
                    text: "xxxx年xx月xx日 发自xxx客户端"
                    verticalAlignment: VerticalAlignment.Top
                    topMargin: 5.0

                }
            }

        }
        Container {
            leftPadding: ui.du(12)
            horizontalAlignment: HorizontalAlignment.Left
            verticalAlignment: VerticalAlignment.Center
            Label {
                text: "#xx楼"

            }
        }
    }
    ExpandableView {
        expandMode: ExpandMode.UserExpand
        Container {
            leftPadding: ui.du(1.0)

            rightPadding: ui.du(1.0)
            //正文内容
            Label {
                text: "Nostrud id eu anim anim veniam eu non veniam exercitation duis do. Fugiat officia ad nostrud enim elit qui ea mollit nostrud reprehenderit. Aute cupidatat magna ad eiusmod eu ex sint occaecat adipisicing labore. Ad ex ea do incididunt quis ipsum non anim duis minim."
                multiline: true

            }
        }

    }
    //    Container {
    //        leftPadding: ui.du(1.0)
    //
    //        rightPadding: ui.du(1.0)
    //        Label {
    //            //正文内容
    //            text: "Nostrud id eu anim anim veniam eu non veniam exercitation duis do. Fugiat officia ad nostrud enim elit qui ea mollit nostrud reprehenderit. Aute cupidatat magna ad eiusmod eu ex sint occaecat adipisicing labore. Ad ex ea do incididunt quis ipsum non anim duis minim."
    //            multiline: true
//
//        }
//    }

    Container {
        layout: StackLayout {

        }
        leftPadding: ui.du(1.0)
        rightPadding: ui.du(1.0)
        Label {
            text: "用户名：高赞回复1\r\n用户名：高赞回复2"//如果都没有赞 就以时间倒序显示回复
            multiline: true
        }

    }
    Container {
        leftPadding: ui.du(1.0)
        rightPadding: ui.du(1.0)
        horizontalAlignment: HorizontalAlignment.Right
        Label {
            text: "查看全部xx条回复"
            textStyle.color: Color.Blue

        }

    }

}
