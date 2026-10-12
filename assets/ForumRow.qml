import bb.cascades 1.4

// 按 BaItemDemo：左侧吧头像，右侧吧名和等级。
Container {
    id: row
    property variant itemData: ({})
    property string avatarPath: itemData.localAvatarPath || "asset:///images/default_avatar.png"
    horizontalAlignment: HorizontalAlignment.Fill
    leftPadding: ui.du(1.5)
    rightPadding: ui.du(1.5)
    topPadding: ui.du(1)
    bottomPadding: ui.du(1)
    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
    ImageView {
        imageSource: row.avatarPath
        scalingMethod: ScalingMethod.AspectFill
        preferredWidth: ui.du(10)
        preferredHeight: ui.du(10)
        minWidth: ui.du(10)
        maxWidth: ui.du(10)
        minHeight: ui.du(10)
        maxHeight: ui.du(10)
    }
    Container {
        leftPadding: ui.du(1)
        verticalAlignment: VerticalAlignment.Center
        layoutProperties: StackLayoutProperties { spaceQuota: 1 }
        Label {
            text: row.itemData.name || ""
            textStyle.fontSize: FontSize.Medium
            autoSize.maxLineCount: 1
        }
        ImageView {
            visible: row.itemData.level >= 1 && row.itemData.level <= 18
            imageSource: visible ? "asset:///icons/grades/icon_grade_lv" + row.itemData.level + ".png" : ""
            scalingMethod: ScalingMethod.AspectFit
            preferredHeight: ui.du(3)
            preferredWidth: ui.du(6)
            topMargin: ui.du(1)
            horizontalAlignment: HorizontalAlignment.Left
        }
    }
}
