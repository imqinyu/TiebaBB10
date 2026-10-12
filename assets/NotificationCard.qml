import bb.cascades 1.4

Container {
    id: card
    property variant itemData: ({})
    signal userTapped()
    horizontalAlignment: HorizontalAlignment.Fill
    leftPadding: ui.du(1.5)
    rightPadding: ui.du(1.5)
    topPadding: ui.du(1.5)
    bottomPadding: ui.du(1.5)
    AuthorHeader {
        itemData: card.itemData
        trailingText: card.itemData.unread ? qsTr("未读") : ""
        onUserTapped: card.userTapped()
    }
    Label {
        text: card.itemData.contentText || qsTr("查看这条通知")
        multiline: true
        autoSize.maxLineCount: 3
        textStyle.fontSize: FontSize.Small
        topMargin: ui.du(1)
    }
    Container {
        visible: (card.itemData.quoteText || "").length > 0
        leftPadding: ui.du(1)
        rightPadding: ui.du(1)
        topPadding: ui.du(0.5)
        bottomPadding: ui.du(0.5)
        Label {
            text: card.itemData.quoteText || ""
            multiline: true
            autoSize.maxLineCount: 2
            textStyle.fontSize: FontSize.XSmall
            textStyle.color: Color.Gray
        }
    }
    Label {
        text: (card.itemData.forumName ? card.itemData.forumName + qsTr("吧 · ") : "") + (card.itemData.title || "")
        autoSize.maxLineCount: 1
        textStyle.fontSize: FontSize.XSmall
        textStyle.color: Color.Gray
        topMargin: ui.du(0.5)
    }
}
