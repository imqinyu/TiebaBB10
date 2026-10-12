import bb.cascades 1.4

Container {
    id: preview
    property variant replies: []
    property bool ready: false
    horizontalAlignment: HorizontalAlignment.Fill
    verticalAlignment: VerticalAlignment.Top
    onRepliesChanged: rebuild()
    onCreationCompleted: {
        ready = true
        rebuild()
    }
    function rebuild() {
        if (!ready) return
        removeAll()
        for (var i = 0; i < replies.length; ++i) {
            var reply = replyDef.createObject()
            reply.prefix = (replies[i].authorName || qsTr("未知用户")) + ": "
            reply.fallbackText = replies[i].plainText || ""
            reply.segments = replies[i].displaySegments || []
            preview.add(reply)
        }
    }
    attachedObjects: [
        ComponentDefinition {
            id: replyDef
            PostTextContent { smallText: true }
        }
    ]
}
