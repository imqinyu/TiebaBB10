import bb.cascades 1.4

// Short bodies never enter ExpandableView. Use native measured text height to
// enable folding, without changing the list item's height or guessing by length.
Container {
    id: bodyText
    property variant segments: []
    property string fallbackText: ""
    property string postKey: ""
    property bool ready: false
    property bool foldedControl: false
    property variant currentText: null
    property variant currentExpandable: null
    horizontalAlignment: HorizontalAlignment.Fill
    verticalAlignment: VerticalAlignment.Top
    onSegmentsChanged: updateText()
    onFallbackTextChanged: updateText()
    onPostKeyChanged: {
        if (currentExpandable) currentExpandable.expanded = false
    }
    onCreationCompleted: {
        ready = true
        currentText = textDef.createObject()
        add(currentText)
        updateText()
    }
    function updateText() {
        if (!ready || !currentText) return
        currentText.fallbackText = fallbackText
        currentText.segments = segments
    }
    function sizeChanged(height) {
        if (!ready || height <= 0 || !currentText) return
        if (height > ui.du(24) && !foldedControl) {
            remove(currentText)
            currentExpandable = expandableDef.createObject()
            currentExpandable.content = currentText
            add(currentExpandable)
            foldedControl = true
        } else if (height <= ui.du(24) && foldedControl) {
            currentExpandable.content = null
            remove(currentExpandable)
            // add() transfers the text's ownership before deleting its old owner.
            add(currentText)
            currentExpandable.destroy()
            currentExpandable = null
            foldedControl = false
        }
    }
    attachedObjects: [
        ComponentDefinition {
            id: textDef
            PostTextContent {
                onMeasuredHeightChanged: bodyText.sizeChanged(measuredHeight)
            }
        },
        ComponentDefinition {
            id: expandableDef
            ExpandableView {
                horizontalAlignment: HorizontalAlignment.Fill
                expandMode: ExpandMode.UserExpand
                collapseMode: CollapseMode.UserCollapse
                maxCollapsedHeight: ui.du(24)
            }
        }
    ]
}
