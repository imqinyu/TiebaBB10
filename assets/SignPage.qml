import bb.cascades 1.4

Page {
    id: signPage
    property variant nav: null
    property bool showLogs: false
    property bool ready: false
    property int version: typeof signService === "undefined" ? 0 : signService.version
    property variant state: readState(version)
    function readState(revision) {
        if (typeof signService === "undefined")
            return { total: 0, signedCount: 0, completed: 0, progress: 0, loading: false, currentName: "", error: "", logs: [], remainingSeconds: 0, canRetry: false }
        return signService.state
    }
    function updateRows() {
        if (typeof signService === "undefined") return
        var rows = signService.rows
        var count = signModel.size()
        if (rows.length < count) { signModel.clear(); count = 0 }
        for (var i = 0; i < count; ++i) signModel.replace(i, rows[i])
        if (rows.length > count) signModel.append(rows.slice(count))
    }
    onVersionChanged: { if (ready) updateRows() }
    onCreationCompleted: { ready = true; updateRows() }
    titleBar: TitleBar { title: qsTr("签到") }
    actions: [
        ActionItem {
            title: signPage.state.loading ? qsTr("取消签到") : qsTr("开始签到")
            ActionBar.placement: ActionBarPlacement.Signature
            imageSource: "asset:///icons/ic_select_more.png"
            enabled: typeof signService !== "undefined"
            onTriggered: {
                if (signPage.state.loading) signService.cancel()
                else signService.start()
            }
        },
        ActionItem {
            title: qsTr("日志")
            imageSource: "asset:///icons/ic_entry.png"
            ActionBar.placement: ActionBarPlacement.OnBar
            onTriggered: signPage.showLogs = !signPage.showLogs
        },
        ActionItem {
            title: qsTr("重试失败")
            imageSource: "asset:///icons/ic_sync.png"
            enabled: !signPage.state.loading && signPage.state.canRetry
            onTriggered: signService.retryFailed()
        }
    ]
    Container {
        Header { title: qsTr("概览") }
        Container {
            leftPadding: ui.du(1)
            rightPadding: ui.du(1)
            topPadding: ui.du(1)
            bottomPadding: ui.du(1)
            Label { text: qsTr("已关注 ") + signPage.state.total + qsTr(" 个吧，已签到 ") + signPage.state.signedCount + qsTr(" 个") }
            Label {
                text: qsTr("进度 ") + signPage.state.progress + "%"
                      + (signPage.state.loading ? qsTr("，预计间隔等待 ") + Math.ceil(signPage.state.remainingSeconds / 60) + qsTr(" 分钟") : "")
                textStyle.fontSize: FontSize.Small
            }
            ProgressIndicator { fromValue: 0; toValue: 100; value: signPage.state.progress }
            Label {
                text: signPage.state.currentName ? qsTr("当前正在签到：") + signPage.state.currentName
                      : (signPage.state.loading ? qsTr("正在准备签到…") : qsTr("点击开始签到后运行"))
                textStyle.fontSize: FontSize.Small
            }
            Label {
                visible: signPage.state.error.length > 0
                text: signPage.state.error
                multiline: true
                textStyle.fontSize: FontSize.Small
                textStyle.color: Color.DarkRed
            }
        }
        Header { title: qsTr("设置") }
        Container {
            leftPadding: ui.du(1)
            rightPadding: ui.du(1)
            topPadding: ui.du(1)
            bottomPadding: ui.du(1)
            DropDown {
                title: qsTr("间隔时长")
                enabled: !signPage.state.loading
                options: [
                    Option { text: qsTr("4秒"); value: 4; selected: true },
                    Option { text: qsTr("5秒"); value: 5 },
                    Option { text: qsTr("8秒"); value: 8 },
                    Option { text: qsTr("10秒"); value: 10 }
                ]
                onSelectedValueChanged: {
                    if (typeof signService !== "undefined" && selectedValue)
                        signService.setIntervalSeconds(Number(selectedValue))
                }
            }
        }
        Header { title: signPage.showLogs ? qsTr("日志") : qsTr("签到结果") }
        Container {
            layout: DockLayout {}
            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
            ListView {
                visible: !signPage.showLogs
                dataModel: signModel
                listItemComponents: [
                    ListItemComponent {
                        type: ""
                        StandardListItem {
                            title: ListItemData.name
                            description: ListItemData.message
                            status: ListItemData.status == "success" ? qsTr("成功")
                                    : (ListItemData.status == "already" ? qsTr("已签到")
                                    : (ListItemData.status == "failed" ? qsTr("失败") : ""))
                        }
                    }
                ]
            }
            ScrollView {
                visible: signPage.showLogs
                Container {
                    leftPadding: ui.du(1)
                    rightPadding: ui.du(1)
                    Label {
                        text: signPage.state.logs.length > 0 ? signPage.state.logs.join("\n") : qsTr("暂无签到日志")
                        multiline: true
                        textStyle.fontSize: FontSize.Small
                    }
                }
            }
        }
    }
    attachedObjects: [ ArrayDataModel { id: signModel } ]
}
