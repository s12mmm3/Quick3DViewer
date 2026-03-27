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
    property bool useFreeCamera: false
    title: currentFile === "" ? qsTr("Q3D Viewer")
                               : qsTr("Q3D Viewer - %1").arg(fileNameFromUrl(currentFile))

    function fileNameFromUrl(url) {
        const parts = url.toString().split("/");
        return parts.length > 0 ? parts[parts.length - 1] : url.toString();
    }

    function fitView(boundsMin, boundsMax, radiusHint) {
        if (!meshLoader.hasData)
            return
        const min = boundsMin
        const max = boundsMax
        const center = Qt.vector3d((min.x + max.x) * 0.5,
                                   (min.y + max.y) * 0.5,
                                   (min.z + max.z) * 0.5)
        pivot.position = Qt.vector3d(-center.x, -center.y, -center.z)
        const size = Qt.vector3d(max.x - min.x, max.y - min.y, max.z - min.z)
        const longest = Math.max(size.x, Math.max(size.y, size.z))
        const radius = Math.max(radiusHint, longest * 0.5, 1)
        const fovRadians = Math.max(5, camera.fieldOfView) * Math.PI / 180
        const fitDistance = radius / Math.tan(fovRadians * 0.5)
        const targetDistance = Math.max(fitDistance * 1.2, 20)
        orbitController.applyDistance(targetDistance)
        cameraRig.position = Qt.vector3d(0, 0, 0)
        cameraRig.eulerRotation = Qt.vector3d(0, 0, 0)
        if (window.useFreeCamera)
            window.useFreeCamera = false
        orbitController.resetView()
        camera.clipNear = Math.max(targetDistance / 500.0, 0.05)
        camera.clipFar = Math.max(targetDistance * 10.0, 5000)
        console.info("fitView", min, max, "radius", radius,
                     "distance", targetDistance,
                     "clip", camera.clipNear, camera.clipFar)
    }

    MeshLoader {
        id: meshLoader
        onBoundsChanged: {
            if (!meshLoader.hasData)
                return
            fitView(boundsMin, boundsMax, boundingRadius)
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
            ToolButton {
                text: qsTr("适配")
                enabled: meshLoader.hasData
                onClicked: fitView(meshLoader.boundsMin, meshLoader.boundsMax, meshLoader.boundingRadius)
            }
            ToolButton {
                checkable: true
                checked: window.useFreeCamera
                text: window.useFreeCamera ? qsTr("自由视角") : qsTr("轨道视角")
                onToggled: window.useFreeCamera = checked
                ToolTip.visible: hovered
                ToolTip.text: qsTr("切换鼠标自由移动（按住右键拖动，滚轮调整速度）")
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
            camera: camera
            environment: SceneEnvironment {
                clearColor: "#2b2b30"
                backgroundMode: SceneEnvironment.Color
                tonemapMode: SceneEnvironment.TonemapModeFilmic
                antialiasingMode: SceneEnvironment.MSAA
                antialiasingQuality: SceneEnvironment.High
            }

            // Model {
            //     source: "#Sphere"
            //     scale: Qt.vector3d(10, 10, 10)
            //     materials: PrincipledMaterial { baseColor: "#ff7777" }
            // }



            Node {
                id: cameraRig
                position: Qt.vector3d(0, 0, 0)

                PerspectiveCamera {
                    id: camera
                    position: Qt.vector3d(0, 0, 400)
                    fieldOfView: 45
                    clipNear: 0.1
                    clipFar: 20000
                }
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
                    cullMode: Material.NoCulling
                }
            }

            OrbitCameraController {
                id: orbitController
                anchors.fill: parent
                origin: cameraRig
                camera: camera
                enabled: !window.useFreeCamera
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
            WasdController {
                id: freeController
                anchors.fill: parent
                controlledObject: cameraRig
                visible: window.useFreeCamera
                mouseEnabled: window.useFreeCamera
                keysEnabled: window.useFreeCamera
                acceptedButtons: Qt.RightButton
                speed: 0.0025
                shiftSpeed: 0.01
                forwardSpeed: 0.0025
                backSpeed: 0.0025
                rightSpeed: 0.0025
                leftSpeed: 0.0025
                upSpeed: 0.0025
                downSpeed: 0.0025
                xSpeed: 0.003
                ySpeed: 0.003
            }
            Connections {
                target: meshLoader
                function onHasDataChanged() {
                    model.geometry = null
                    model.geometry = meshLoader
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
