import bb.cascades 1.4

// 帖子卡片和楼层共用的头像、作者、时间行。
Container {
    id: header
    property variant itemData: ({})
    property string trailingText: ""
    property string avatarPath: itemData.localAvatarPath || "asset:///images/default_avatar.png"
    property int authorLevel: itemData && itemData.authorLevel ? Number(itemData.authorLevel) : 0
    signal userTapped()
    horizontalAlignment: HorizontalAlignment.Fill
    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
    ImageView {
        imageSource: header.avatarPath
        scalingMethod: ScalingMethod.AspectFill
        preferredWidth: ui.du(8)
        preferredHeight: ui.du(8)
        minWidth: ui.du(8)
        maxWidth: ui.du(8)
        minHeight: ui.du(8)
        maxHeight: ui.du(8)
        gestureHandlers: [ TapHandler { onTapped: header.userTapped() } ]
    }
    Container {
        leftPadding: ui.du(1)
        layoutProperties: StackLayoutProperties { spaceQuota: 1 }
        verticalAlignment: VerticalAlignment.Center
        Container {
            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
            Label {
                text: header.itemData.authorName || header.itemData.name || qsTr("未知用户")
                autoSize.maxLineCount: 1
                textStyle.fontSize: FontSize.Small
                textStyle.fontWeight: FontWeight.Bold
                horizontalAlignment: HorizontalAlignment.Left
                maxWidth: ui.du(28)
                gestureHandlers: [ TapHandler { onTapped: header.userTapped() } ]
            }
            ImageView {
                visible: header.authorLevel >= 1 && header.authorLevel <= 18
                imageSource: header.authorLevel >= 1 && header.authorLevel <= 18 ? "asset:///icons/grades/icon_grade_lv" + header.authorLevel + ".png" : ""
                preferredWidth: ui.du(5)
                preferredHeight: ui.du(3)
                leftMargin: ui.du(0.75)
                scalingMethod: ScalingMethod.AspectFit
                verticalAlignment: VerticalAlignment.Center
            }
            ImageView {
                visible: header.itemData.isOriginalPoster === true
                imageSource: "asset:///icons/icon_floorhost.png"
                preferredWidth: ui.du(6)
                preferredHeight: ui.du(3.2)
                leftMargin: ui.du(0.75)
                scalingMethod: ScalingMethod.AspectFit
                verticalAlignment: VerticalAlignment.Center
            }
        }
        Label {
            text: header.itemData.timeText || header.itemData.lastTimeText || header.itemData.authorLabel || ""
            autoSize.maxLineCount: 1
            textStyle.fontSize: FontSize.XSmall
            textStyle.color: Color.Gray
            topMargin: ui.du(0.5)
        }
    }
    Label {
        text: header.trailingText
        visible: text.length > 0
        verticalAlignment: VerticalAlignment.Center
        textStyle.fontSize: FontSize.XSmall
        textStyle.color: Color.Gray
    }
}
