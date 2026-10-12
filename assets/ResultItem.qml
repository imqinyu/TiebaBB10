import bb.cascades 1.4

CustomListItem {
    id: resultRoot
    contextMenuHandler: ContextMenuHandler {
        onPopulating: {
            if (ListItemData.resultKind == "user") event.abort()
            else if (ListItemData.resultKind == "forum") resultRoot.ListItem.view.requestForumStats(ListItemData)
        }
    }
    contextActions: [
        ActionSet {
            title: resultRoot.ListItem.view.contextTitle(ListItemData)
            subtitle: resultRoot.ListItem.view.contextSubtitle(ListItemData, resultRoot.ListItem.view.contextVersion)
            ActionItem {
                title: qsTr("复制链接")
                imageSource: "asset:///icons/ic_link.png"
                onTriggered: resultRoot.ListItem.view.copyResultLink(ListItemData)
            }
        }
    ]
    function readAvatar(row, revision) {
        var view = resultRoot.ListItem.view
        if (!view) return "asset:///images/default_avatar.png"
        return view.avatarPath(row)
    }
    Container {
        ForumRow {
            visible: ListItemData.resultKind == "forum"
            itemData: ListItemData
            avatarPath: resultRoot.readAvatar(ListItemData, resultRoot.ListItem.view ? resultRoot.ListItem.view.imageVersion : 0)
        }
        ThreadCard {
            visible: ListItemData.resultKind == "thread"
            itemData: ListItemData
            avatarPath: resultRoot.readAvatar(ListItemData, resultRoot.ListItem.view ? resultRoot.ListItem.view.imageVersion : 0)
            onUserTapped: resultRoot.ListItem.view.openUser(ListItemData)
        }
        Container {
            visible: ListItemData.resultKind == "user"
            leftPadding: ui.du(1.5)
            rightPadding: ui.du(1.5)
            topPadding: ui.du(1)
            bottomPadding: ui.du(1)
            AuthorHeader {
                itemData: ListItemData
                avatarPath: resultRoot.readAvatar(ListItemData, resultRoot.ListItem.view ? resultRoot.ListItem.view.imageVersion : 0)
                onUserTapped: resultRoot.ListItem.view.openUser(ListItemData)
            }
        }
    }
}
