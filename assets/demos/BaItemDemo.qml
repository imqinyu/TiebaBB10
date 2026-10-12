import bb.cascades 1.4

Container {
    //background: Color.Yellow

    preferredHeight: 100.0
    maxHeight: 100.0

    layout: StackLayout {
        orientation: LayoutOrientation.LeftToRight

    }
    bottomPadding: 10.0
    ImageView {
        maxWidth: ui.du(10)
        preferredWidth: ui.du(10)
        preferredHeight: ui.du(10)
        maxHeight: ui.du(10)
        imageSource: "asset:///placeholder%20image.png"//吧头图
        minWidth: ui.du(10.0)
        minHeight: ui.du(10.0)
    }
    Container {
        layout: StackLayout {

        }
        Container {
            layout: StackLayout {
                orientation: LayoutOrientation.TopToBottom

            }
            leftPadding: 5.0
            Label {
                text: "吧名"//吧名

            }

        }

        Container {

            leftPadding: 8.0

            topPadding: 10.0
            ImageView {//吧等级
                imageSource: "asset:///icon_grade_lv1.png"
                scalingMethod: ScalingMethod.AspectFit
                visible: true

            }
        }

        //Container {
        //
        //
        //            leftPadding: 5.0
        //            Label {
        //        text: "xxx关注"
        //
        //    }
        //}
    }

}
