import bb.cascades 1.4

// 按 InBaPostItem：作者行、标题、一行预览、右对齐统计。
Container {
    id: card
    property variant itemData: ({})
    property string forumName: ""
    property bool showForumName: true
    property string avatarPath: itemData.localAvatarPath || "asset:///images/default_avatar.png"
    signal userTapped()
    horizontalAlignment: HorizontalAlignment.Fill
    leftPadding: ui.du(1.5)
    rightPadding: ui.du(1.5)
    topPadding: ui.du(1.5)
    bottomPadding: ui.du(1.5)
    AuthorHeader {
        itemData: card.itemData
        avatarPath: card.avatarPath
        trailingText: card.showForumName ? (card.itemData.forumName || card.forumName) : ""
        onUserTapped: card.userTapped()
    }
    Container {
        layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
        topMargin: ui.du(1)
        ImageView {
            visible: card.itemData.isGood === true
            imageSource: "asset:///icons/icon_elite.png"
            preferredWidth: ui.du(3)
            preferredHeight: ui.du(3)
            scalingMethod: ScalingMethod.AspectFit
        }
        Label {
            text: (card.itemData.isTop ? qsTr("[置顶] ") : "") + (card.itemData.title || "")
            multiline: true
            autoSize.maxLineCount: 2
            textStyle.fontSize: FontSize.Medium
            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
        }
    }
    Label {
        text: card.itemData.abstractText || ""
        visible: text.length > 0
        autoSize.maxLineCount: 1
        textStyle.fontSize: FontSize.Small
        textStyle.color: Color.Gray
        topMargin: ui.du(0.5)
    }
    Label {
        // 缺失的点赞数留空；明确的 "0" 仍显示 0 赞。
        text: (card.itemData.agreeCountText ? card.itemData.agreeCountText + qsTr(" 赞") + "  " : "")
              + (card.itemData.replyCountText || "0") + qsTr(" 回复")
              + (card.itemData.viewCountText ? "  " + card.itemData.viewCountText + qsTr(" 浏览") : "")
              + (card.itemData.hotText ? "  " + qsTr("热度 ") + card.itemData.hotText : "")
        horizontalAlignment: HorizontalAlignment.Right
        textStyle.fontSize: FontSize.XSmall
        textStyle.color: Color.Gray
        topMargin: ui.du(1)
    }
}
