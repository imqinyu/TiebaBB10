import bb.cascades 1.4

CustomListItem {
    id: baItemRoot
    contextMenuHandler: ContextMenuHandler {
        onPopulating: baItemRoot.ListItem.view.requestForumStats(ListItemData)
    }
    contextActions: [
        ActionSet {
            title: ListItemData.name || ""
            subtitle: baItemRoot.ListItem.view.contextSubtitle(ListItemData, baItemRoot.ListItem.view.contextVersion)
            ActionItem {
                title: qsTr("复制吧名")
                imageSource: "asset:///icons/ic_copy.png"
                onTriggered: baItemRoot.ListItem.view.copyForumName(ListItemData.name)
            }
            ActionItem {
                title: qsTr("查看详情")
                imageSource: "asset:///icons/ic_info.png"
                onTriggered: baItemRoot.ListItem.view.openForumDetail(ListItemData.name)
            }
        }
    ]
    ForumRow { itemData: ListItemData }
}
