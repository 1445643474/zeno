import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Controls 2.15
import QtQuick.Dialogs 1.2

Item {
    id: thisCollection

    property var descs: []

    ZenoScene {
        id: currScene
        anchors.fill: parent
        focus: true
    }

    ToolBar {
        Flow {
            anchors.fill: parent

            ToolButton {
                text: qsTr("Open")
                icon.name: 'document-open'
                onClicked: {
                    fileOpenDialog.open()
                }

                FileDialog {
                    id: fileOpenDialog
                    title: qsTr("Select a file to open")
                    folder: shortcuts.documents
                    nameFilters: [
                        qsTr("Zeno Graph files (*.zsg)"),
                    ]
                    onAccepted: {
                        print('opening file', fileOpenDialog.fileUrl)
                    }
                    onRejected: {
                        print('no file opened')
                    }
                }
            }

            ToolButton {
                text: qsTr("Apply")
                icon.name: 'edit-cut'
                onClicked: {
                    var r_scene = currScene.dumpScene()
                    var str_scene = JSON.stringify(r_scene)
                    rootWindow.appData.load_scene(str_scene)
                }
            }
        }
    }

    Component {
        id: compZenoAddNodeMenu

        Rectangle {
            id: thisMenu
            x: 50
            y: 50
            width: 100
            height: 100

            property var scene: null
            property var descs: []
        }
    }

    function onAddNode(scene) {
        compZenoAddNodeMenu.createObject(scene.sceneRect, {
            scene: scene,
            descs: descs,
        })
    }

    Component.onCompleted: {
        var str_descs = rootWindow.appData.get_descriptors()
        thisCollection.descs = JSON.parse(str_descs)
    }
}
