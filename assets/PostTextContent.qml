import bb.cascades 1.4

// Native text/emoji runs. Each reply owns its own runs; no floor-wide emoji tail.
Container {
    id: textContent
    property variant segments: []
    property string fallbackText: ""
    property string prefix: ""
    property bool smallText: false
    property bool ready: false
    property real availableWidth: 0
    property real measuredHeight: 0
    property variant runHeights: []
    property int generation: 0
    horizontalAlignment: HorizontalAlignment.Fill
    verticalAlignment: VerticalAlignment.Top
    onSegmentsChanged: rebuild()
    onFallbackTextChanged: rebuild()
    onPrefixChanged: rebuild()
    onCreationCompleted: {
        ready = true
        rebuild()
    }
    function recordHeight(index, height, version) {
        if (version !== generation || height <= 0) return
        runHeights[index] = height
        var total = 0
        for (var i = 0; i < runHeights.length; ++i) {
            if (!runHeights[i]) return
            total += runHeights[i]
        }
        measuredHeight = total
    }
    function appendRun(text, segment) {
        var run = segment ? emojiRunDef.createObject() : plainRunDef.createObject()
        run.runIndex = runHeights.length
        run.version = generation
        runHeights.push(0)
        run.runText = text
        if (segment) {
            run.localPath = segment.localImagePath || ""
            run.emojiName = segment.emoticonName || segment.text || ""
        }
        textContent.add(run)
    }
    function rebuild() {
        if (!ready) return
        generation += 1
        removeAll()
        runHeights = []
        measuredHeight = 0
        var text = prefix
        var hasTextContent = false
        for (var i = 0; i < segments.length; ++i) {
            var segment = segments[i]
            if (segment.kind === "text" || segment.kind === "link" || segment.kind === "user") {
                text += segment.text || ""
                hasTextContent = true
            } else if (segment.kind === "emoticon") {
                appendRun(text, segment)
                text = ""
                hasTextContent = true
            }
        }
        if (!hasTextContent) text += fallbackText
        if (text.length > 0) appendRun(text, null)
    }
    attachedObjects: [
        LayoutUpdateHandler {
            onLayoutFrameChanged: textContent.availableWidth = layoutFrame.width
        },
        ComponentDefinition {
            id: plainRunDef
            Label {
                id: plainRun
                property string runText: ""
                property int runIndex: 0
                property int version: 0
                text: runText
                textFormat: TextFormat.Plain
                multiline: true
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Top
                topMargin: 0
                bottomMargin: 0
                textStyle.fontSize: textContent.smallText ? FontSize.Small : FontSize.Medium
                // 系统样式随明暗主题提供文字颜色，不能把正文固定成黑色。
                textStyle.base: textContent.smallText ? SystemDefaults.TextStyles.SubtitleText : SystemDefaults.TextStyles.BodyText
                onCreationCompleted: {
                    if (textContent.smallText) textStyle.color = Color.Gray
                }
                attachedObjects: [
                    LayoutUpdateHandler {
                        onLayoutFrameChanged: textContent.recordHeight(plainRun.runIndex, layoutFrame.height, plainRun.version)
                    }
                ]
            }
        },
        ComponentDefinition {
            id: emojiRunDef
            Container {
                id: emojiRun
                property string runText: ""
                property string localPath: ""
                property string emojiName: ""
                property int runIndex: 0
                property int version: 0
                property real runWidth: textContent.availableWidth
                onRunWidthChanged: {
                    if (runWidth > 48 && prefixText) prefixText.maxWidth = runWidth - 48
                }
                onCreationCompleted: {
                    if (runWidth > 48) prefixText.maxWidth = runWidth - 48
                }
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Top
                topMargin: 0
                bottomMargin: 0
                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                Label {
                    id: prefixText
                    text: emojiRun.runText + (emojiRun.localPath.length > 0 ? "" : "[" + emojiRun.emojiName + "]")
                    textFormat: TextFormat.Plain
                    multiline: true
                    topMargin: 0
                    bottomMargin: 0
                    textStyle.fontSize: textContent.smallText ? FontSize.Small : FontSize.Medium
                    textStyle.base: textContent.smallText ? SystemDefaults.TextStyles.SubtitleText : SystemDefaults.TextStyles.BodyText
                    onCreationCompleted: {
                        if (textContent.smallText) textStyle.color = Color.Gray
                    }
                }
                ImageView {
                    visible: emojiRun.localPath.length > 0
                    imageSource: emojiRun.localPath
                    scalingMethod: ScalingMethod.AspectFit
                    preferredWidth: 44
                    minWidth: 44
                    maxWidth: 44
                    preferredHeight: 44
                    minHeight: 44
                    maxHeight: 44
                    verticalAlignment: VerticalAlignment.Bottom
                }
                attachedObjects: [
                    LayoutUpdateHandler {
                        onLayoutFrameChanged: textContent.recordHeight(emojiRun.runIndex, layoutFrame.height, emojiRun.version)
                    }
                ]
            }
        }
    ]
}
