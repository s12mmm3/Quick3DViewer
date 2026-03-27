import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick3D
import QtQuick3D.Helpers
import ModelLoader 1.0

ApplicationWindow {
    id: window
    width: 1280
    height: 720
    visible: true
    color: "#101014"
    property url currentFile: ""
    title: currentFile === "" ? qsTr("Q3D Viewer")
                               : qsTr("Q3D Viewer - %1").arg(fileNameFromUrl(currentFile))

    function fileNameFromUrl(url) {
        const parts = url.toString().split("/");
        return parts.length > 0 ? parts[parts.length - 1] : url.toString();
    }

    MeshLoader {
        id: meshLoader
        onBoundsChanged: {
            if (!meshLoader.hasData)
                return
            pivot.position = Qt.vector3d(-boundsCenter.x, -boundsCenter.y, -boundsCenter.z)
            const radius = Math.max(boundingRadius, 1)
            orbitController.applyDistance(Math.max(radius * 3, 20))
        }
    }

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            ToolButton {
                text: qsTr("打开…")
                icon.name: "document-open"
                onClicked: openDialog.open()
            }
            ToolButton {
                text: qsTr("重置视图")
                enabled: meshLoader.hasData
                onClicked: orbitController.resetView()
            }
            Label {
                Layout.fillWidth: true
                elide: Label.ElideRight
                text: meshLoader.hasData ? window.currentFile.toString() : qsTr("拖拽或打开 PLY/STL/OBJ 文件")
            }
        }
    }

    FileDialog {
        id: openDialog
        title: qsTr("选择 3D 模型")
        nameFilters: [
            qsTr("3D 模型 (*.ply *.stl *.obj)"),
            qsTr("PLY (*.ply)"),
            qsTr("STL (*.stl)"),
            qsTr("OBJ (*.obj)")
        ]
        onAccepted: {
            window.currentFile = selectedFile
            meshLoader.source = selectedFile
        }
    }

    Shortcut {
        sequences: [ StandardKey.Open ]
        onActivated: openDialog.open()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        View3D {
            id: view3d
            Layout.fillWidth: true
            Layout.fillHeight: true
            focus: true
            environment: SceneEnvironment {
                clearColor: "#101014"
                backgroundMode: SceneEnvironment.Color
                tonemapMode: SceneEnvironment.TonemapModeFilmic
                antialiasingMode: SceneEnvironment.MSAA
                antialiasingQuality: SceneEnvironment.High
            }

            PerspectiveCamera {
                id: camera
                position: Qt.vector3d(0, 0, 400)
                fieldOfView: 45
                clipNear: 0.1
                clipFar: 20000
            }

            Node { id: pivot }

            DirectionalLight {
                eulerRotation: Qt.vector3d(-35, 45, 0)
                brightness: 50
            }

            DirectionalLight {
                eulerRotation: Qt.vector3d(60, -110, 0)
                brightness: 30
            }

            Model {
                id: model
                parent: pivot
                visible: meshLoader.hasData
                geometry: meshLoader
                materials: PrincipledMaterial {
                    baseColor: "#d9d9d9"
                    metalness: 0.0
                    roughness: 0.35
                }
            }

            OrbitCameraController {
                id: orbitController
                anchors.fill: parent
                origin: pivot
                camera: camera
                xSpeed: 0.005
                ySpeed: 0.005
                property real defaultDistance: 400
                property real currentDistance: defaultDistance
                function applyDistance(value) {
                    currentDistance = value
                    camera.position = Qt.vector3d(0, 0, currentDistance)
                }
                Component.onCompleted: applyDistance(defaultDistance)
                function resetView() {
                    const targetDistance = Math.max(meshLoader.boundingRadius * 3, defaultDistance)
                    applyDistance(targetDistance)
                }
            }
            Connections {
                target: meshLoader
                function onHasDataChanged() {
                    if (!meshLoader.hasData) {
                        pivot.position = Qt.vector3d(0, 0, 0)
                        orbitController.applyDistance(orbitController.defaultDistance)
                    } else {
                        orbitController.resetView()
                    }
                }
            }

            DropArea {
                anchors.fill: parent
                onDropped: (event) => {
                    if (event.hasUrls) {
                        window.currentFile = event.urls[0]
                        meshLoader.source = event.urls[0]
                        event.acceptProposedAction()
                    }
                }
            }
            Text {
                anchors.centerIn: parent
                color: "#cfd8dc"
                text: qsTr("拖放文件到此处或使用 Ctrl/Cmd+O 打开")
                visible: !meshLoader.hasData
            }
        }

        Pane {
            Layout.fillWidth: true
            visible: meshLoader.errorString.length > 0
            RowLayout {
                Label {
                    text: meshLoader.errorString
                    color: "tomato"
                    Layout.fillWidth: true
                }
                ToolButton {
                    text: qsTr("重试")
                    onClicked: {
                        const current = window.currentFile
                        meshLoader.source = ""
                        if (current !== "")
                            meshLoader.source = current
                    }
                }
            }
        }
    }

    footer: Rectangle {
        height: 32
        color: "#1c1d20"
        anchors.left: parent.left
        anchors.right: parent.right
        RowLayout {
            anchors.fill: parent
            Label {
                color: "#cfd8dc"
                text: meshLoader.hasData ?
                          qsTr("中心 (%1, %2, %3)  半径 %4")
                              .arg(Number(meshLoader.boundsCenter.x).toFixed(2))
                              .arg(Number(meshLoader.boundsCenter.y).toFixed(2))
                              .arg(Number(meshLoader.boundsCenter.z).toFixed(2))
                              .arg(Number(meshLoader.boundingRadius).toFixed(2)) :
                          qsTr("准备就绪")
            }
        }
    }
}
