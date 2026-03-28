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
    property bool useFreeCamera: true
    title: currentFile === "" ? qsTr("Q3D Viewer")
                               : qsTr("Q3D Viewer - %1").arg(fileNameFromUrl(currentFile))

    function fileNameFromUrl(url) {
        const parts = url.toString().split("/");
        return parts.length > 0 ? parts[parts.length - 1] : url.toString();
    }

    property real lastFitDistance: 400

    Component {
        id: meshLoaderComponent
        MeshLoader { }
    }

    QtObject {
        id: sceneController
        property var meshes: []
        property int nextId: 1
        property bool hasVisibleData: false
        property vector3d boundsMin: Qt.vector3d(0, 0, 0)
        property vector3d boundsMax: Qt.vector3d(0, 0, 0)
        property vector3d boundsCenter: Qt.vector3d(0, 0, 0)
        property real boundingRadius: 1.0
        property string lastError: ""
        property var pivotNode: null
        property var pendingSources: []

        function copyMeshes() {
            meshes = meshes.slice();
        }

        function addSources(list) {
            if (!list)
                return;
            let urls = list
            for (let i = 0; i < urls.length; ++i)
                addSingle(urls[i]);
        }

        function addSingle(source) {
            if (!source || source === "")
                return;
            if (!pivotNode) {
                pendingSources = pendingSources.concat([source]);
                console.warn("场景尚未就绪，已排队等待加载", source);
                return;
            }
            const loader = meshLoaderComponent.createObject(pivotNode);
            if (!loader) {
                console.error("无法创建 MeshLoader");
                return;
            }
            const entry = {
                id: nextId++,
                loader: loader,
                source: source,
                name: window.fileNameFromUrl(source),
                visible: true,
                opacity: 1.0,
                autoFitPending: meshes.length === 0
            };

            const boundsChanged = function() {
                sceneController.recalculateBounds();
                if (entry.autoFitPending && loader.hasData) {
                    entry.autoFitPending = false;
                    window.fitSceneToVisible();
                }
            };

            loader.boundsChanged.connect(boundsChanged);
            loader.hasDataChanged.connect(boundsChanged);
            loader.errorChanged.connect(sceneController.refreshErrorMessage);
            loader.source = source;

            meshes = meshes.concat([entry]);
            window.currentFile = source;
            recalculateBounds();
            refreshErrorMessage();
        }

        function removeMesh(id) {
            const remaining = [];
            for (let i = 0; i < meshes.length; ++i) {
                const entry = meshes[i];
                if (entry.id === id) {
                    if (entry.loader)
                        entry.loader.destroy();
                } else {
                    remaining.push(entry);
                }
            }
            meshes = remaining;
            recalculateBounds();
            refreshErrorMessage();
        }

        function setVisible(id, visible) {
            let changed = false;
            for (let i = 0; i < meshes.length; ++i) {
                const entry = meshes[i];
                if (entry.id === id) {
                    if (entry.visible !== visible) {
                        entry.visible = visible;
                        changed = true;
                    }
                    break;
                }
            }
            if (changed) {
                copyMeshes();
                recalculateBounds();
            }
        }

        function registerPivot(node) {
            pivotNode = node;
            reparentExistingLoaders();
            if (pendingSources.length > 0) {
                const queue = pendingSources;
                pendingSources = [];
                for (let i = 0; i < queue.length; ++i)
                    addSingle(queue[i]);
            }
        }

        function reparentExistingLoaders() {
            if (!pivotNode)
                return;
            for (let i = 0; i < meshes.length; ++i) {
                const entry = meshes[i];
                if (entry.loader && entry.loader.parent !== pivotNode)
                    entry.loader.parent = pivotNode;
            }
        }

        function setOpacity(id, opacity) {
            const value = Math.min(1.0, Math.max(0.05, opacity));
            let changed = false;
            for (let i = 0; i < meshes.length; ++i) {
                const entry = meshes[i];
                if (entry.id === id) {
                    if (Math.abs(entry.opacity - value) > 0.0001) {
                        entry.opacity = value;
                        changed = true;
                    }
                    break;
                }
            }
            if (changed)
                copyMeshes();
        }

        function recalculateBounds() {
            let valid = false;
            let minPoint = null;
            let maxPoint = null;
            for (let i = 0; i < meshes.length; ++i) {
                const entry = meshes[i];
                const loader = entry.loader;
                if (!entry.visible || !loader || !loader.hasData)
                    continue;

                const min = loader.boundsMin;
                const max = loader.boundsMax;
                if (!minPoint) {
                    minPoint = Qt.vector3d(min.x, min.y, min.z);
                    maxPoint = Qt.vector3d(max.x, max.y, max.z);
                } else {
                    minPoint = Qt.vector3d(Math.min(minPoint.x, min.x),
                                           Math.min(minPoint.y, min.y),
                                           Math.min(minPoint.z, min.z));
                    maxPoint = Qt.vector3d(Math.max(maxPoint.x, max.x),
                                           Math.max(maxPoint.y, max.y),
                                           Math.max(maxPoint.z, max.z));
                }
                valid = true;
            }

            if (valid) {
                boundsMin = minPoint;
                boundsMax = maxPoint;
                boundsCenter = Qt.vector3d((minPoint.x + maxPoint.x) * 0.5,
                                           (minPoint.y + maxPoint.y) * 0.5,
                                           (minPoint.z + maxPoint.z) * 0.5);
                const size = Qt.vector3d(maxPoint.x - minPoint.x,
                                         maxPoint.y - minPoint.y,
                                         maxPoint.z - minPoint.z);
                const diag = Math.sqrt(size.x * size.x + size.y * size.y + size.z * size.z);
                boundingRadius = Math.max(diag * 0.5, 1);
            } else {
                boundsMin = Qt.vector3d(0, 0, 0);
                boundsMax = Qt.vector3d(0, 0, 0);
                boundsCenter = Qt.vector3d(0, 0, 0);
                boundingRadius = 1;
                if (typeof pivot !== "undefined")
                    pivot.position = Qt.vector3d(0, 0, 0);
            }
            hasVisibleData = valid;
        }

        function refreshErrorMessage() {
            let message = "";
            for (let i = 0; i < meshes.length; ++i) {
                const loader = meshes[i].loader;
                if (loader && loader.errorString.length > 0) {
                    message = loader.errorString;
                    break;
                }
            }
            lastError = message;
        }
    }

    function updateFreeCameraSettings(distance) {
        lastFitDistance = distance
        const base = Math.max(distance * 0.02, 2)
        freeController.speed = base
        freeController.shiftSpeed = base * 3
        freeController.forwardSpeed = base
        freeController.backSpeed = base
        freeController.rightSpeed = base
        freeController.leftSpeed = base
        freeController.upSpeed = base
        freeController.downSpeed = base
        freeController.xSpeed = 0.35
        freeController.ySpeed = 0.35
    }

    function updateCameraClip(distance) {
        const nearValue = Math.max(distance / 600.0, 0.15)
        const farValue = Math.max(distance * 25.0, 5000)
        camera.clipNear = nearValue
        camera.clipFar = farValue
    }

    function fitView(boundsMin, boundsMax, radiusHint) {
        if (!sceneController.hasVisibleData)
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
        camera.position = Qt.vector3d(0, 0, targetDistance)
        cameraRig.position = Qt.vector3d(0, 0, 0)
        cameraRig.eulerRotation = Qt.vector3d(0, 0, 0)
        updateCameraClip(targetDistance)
        updateFreeCameraSettings(targetDistance)
        console.info("fitView", min, max, "radius", radius,
                     "distance", targetDistance,
                     "clip", camera.clipNear, camera.clipFar)
    }

    function fitSceneToVisible() {
        if (!sceneController.hasVisibleData)
            return
        fitView(sceneController.boundsMin, sceneController.boundsMax, sceneController.boundingRadius)
    }

    function setCameraPreset(preset) {
        if (!sceneController.hasVisibleData)
            return
        var rotation = Qt.vector3d(0, 0, 0)
        switch (preset) {
        case "front":
            rotation = Qt.vector3d(0, 0, 0)
            break
        case "back":
            rotation = Qt.vector3d(0, 180, 0)
            break
        case "left":
            rotation = Qt.vector3d(0, 90, 0)
            break
        case "right":
            rotation = Qt.vector3d(0, -90, 0)
            break
        case "top":
            rotation = Qt.vector3d(-90, 0, 0)
            break
        case "bottom":
            rotation = Qt.vector3d(90, 0, 0)
            break
        }
        cameraRig.position = Qt.vector3d(0, 0, 0)
        cameraRig.eulerRotation = rotation
        camera.position = Qt.vector3d(0, 0, lastFitDistance)
        updateCameraClip(lastFitDistance)
    }
    Connections {
        target: cameraRig
        function onPositionChanged() {
            if (window.useFreeCamera) {
                const distance = Math.max(camera.position.length(), 1)
                updateCameraClip(distance)
            }
        }
    }

    menuBar: MenuBar {
        Menu {
            title: qsTr("&File")
            Action {
                text: qsTr("&Open...")
                icon.name: "document-open"
                onTriggered: openDialog.open()
            }
            Action { text: qsTr("&Save") }
            Action { text: qsTr("Save &As...") }
            MenuSeparator { }
            Action {
                text: qsTr("&Quit")
                onTriggered: Qt.quit()
            }
        }
        // Menu {
        //     title: qsTr("&Edit")
        //     Action { text: qsTr("Cu&t") }
        //     Action { text: qsTr("&Copy") }
        //     Action { text: qsTr("&Paste") }
        // }

        Menu {
            title: qsTr("&View")
            Action {
                text: qsTr("&Fit")
                enabled: sceneController.hasVisibleData
                onTriggered: fitSceneToVisible()
            }
            MenuSeparator {}
            Repeater {
                model: [
                    { text: qsTr("Front"), value: "front" },
                    { text: qsTr("Back"), value: "back" },
                    { text: qsTr("Left"), value: "left" },
                    { text: qsTr("Right"), value: "right" },
                    { text: qsTr("Top"), value: "top" },
                    { text: qsTr("Bottom"), value: "bottom" },
                ]
                MenuItem {
                    text: modelData.text
                    onTriggered: setCameraPreset(modelData.value)
                }
            }
        }
        Menu {
            title: qsTr("&Help")
            Action { text: qsTr("&About") }
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
            const list = selectedFiles.length > 0 ? selectedFiles : [selectedFile]
            const first = list.length > 0 ? list[0] : ""
            window.currentFile = first ? first : ""
            sceneController.addSources(list)
        }
    }

    Shortcut {
        sequences: [ StandardKey.Open ]
        onActivated: openDialog.open()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            View3D {
                id: view3d
                anchors.fill: parent
                focus: true
                camera: camera
                environment: SceneEnvironment {
                    clearColor: "#3a3a3f"
                    backgroundMode: SceneEnvironment.Color
                    tonemapMode: SceneEnvironment.TonemapModeFilmic
                    antialiasingMode: SceneEnvironment.MSAA
                    antialiasingQuality: SceneEnvironment.High
                    aoStrength: 0.65
                    aoDistance: 220
                    aoSoftness: 0.35
                    probeExposure: 0.75
                }

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

                Node {
                    id: pivot
                    Component.onCompleted: sceneController.registerPivot(pivot)
                }

                DirectionalLight {
                    eulerRotation: Qt.vector3d(-35, 45, 0)
                    brightness: 24
                }

                DirectionalLight {
                    eulerRotation: Qt.vector3d(60, -110, 0)
                    brightness: 12
                }

                Repeater3D {
                    model: sceneController.meshes
                    delegate: Model {
                        id: meshModel
                        required property var modelData
                        property bool isTransparent: modelData.opacity < 0.999
                        opacity: modelData.opacity
                        parent: pivot
                        visible: modelData.visible && modelData.loader && modelData.loader.hasData
                        geometry: modelData.loader
                        materials: PrincipledMaterial {
                            baseColor: "#cfcfcf"
                            metalness: 0.0
                            roughness: 0.6
                            specularAmount: 0.25
                            cullMode: Material.NoCulling
                            opacity: modelData.opacity
                            alphaMode: meshModel.isTransparent ?
                                           PrincipledMaterial.Blend :
                                           PrincipledMaterial.Opaque
                            depthDrawMode: meshModel.isTransparent ?
                                               Material.NeverDepthDraw :
                                               Material.OpaqueOnlyDepthDraw
                        }
                    }
                }

                WasdController {
                    id: freeController
                    anchors.fill: parent
                    controlledObject: cameraRig
                    mouseEnabled: true
                    keysEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
                }

                DropArea {
                    anchors.fill: parent
                    onDropped: (event) => {
                        if (event.hasUrls) {
                            window.currentFile = event.urls[0]
                            sceneController.addSources(event.urls)
                            event.acceptProposedAction()
                        }
                    }
                }

                Text {
                    anchors.centerIn: parent
                    color: "#cfd8dc"
                    text: qsTr("拖放文件到此处或使用 Ctrl/Cmd+O 打开")
                    visible: !sceneController.hasVisibleData
                }
            }

            Rectangle {
                id: modelListPanel
                z: 10
                visible: sceneController.meshes.length > 0
                color: "#2b2b30cc"
                radius: 6
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.margins: 12
                width: 320
                height: Math.min(parent.height - 24, 320)
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 6
                    Label {
                        text: qsTr("模型列表")
                        color: "#f4f6f8"
                        font.bold: true
                    }
                    ListView {
                        id: modelListView
                        model: sceneController.meshes
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(contentHeight, 260)
                        clip: true
                        ScrollBar.vertical: ScrollBar { }
                        delegate: Rectangle {
                            required property int index
                            required property var modelData
                            property var entry: modelData
                            width: ListView.view.width
                            color: "transparent"
                            implicitHeight: entryLayout.implicitHeight + 4
                            ColumnLayout {
                                id: entryLayout
                                anchors.fill: parent
                                spacing: 4
                                RowLayout {
                                    spacing: 6
                                    CheckBox {
                                        checked: entry.visible
                                        onToggled: sceneController.setVisible(entry.id, checked)
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        elide: Label.ElideRight
                                        color: "#f5f5f5"
                                        text: entry.name
                                    }
                                    ToolButton {
                                        text: qsTr("删")
                                        onClicked: sceneController.removeMesh(entry.id)
                                    }
                                }
                                RowLayout {
                                    spacing: 6
                                    Label {
                                        text: qsTr("透明度")
                                        color: "#c0c3ca"
                                    }
                                    Slider {
                                        Layout.fillWidth: true
                                        from: 0.05
                                        to: 1.0
                                        value: entry.opacity
                                        onValueChanged: sceneController.setOpacity(entry.id, value)
                                    }
                                    Label {
                                        text: qsTr("%1%").arg(Math.round(entry.opacity * 100))
                                        color: "#c0c3ca"
                                    }
                                }
                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 1
                                    color: "#3f4048"
                                    visible: index < sceneController.meshes.length - 1
                                }
                            }
                        }
                    }
                }
            }
        }

        Pane {
            Layout.fillWidth: true
            visible: sceneController.lastError.length > 0
            RowLayout {
                Label {
                    text: sceneController.lastError
                    color: "tomato"
                    Layout.fillWidth: true
                }
                ToolButton {
                    text: qsTr("关闭")
                    onClicked: sceneController.lastError = ""
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
                text: sceneController.hasVisibleData ?
                          qsTr("中心 (%1, %2, %3)  半径 %4")
                              .arg(Number(sceneController.boundsCenter.x).toFixed(2))
                              .arg(Number(sceneController.boundsCenter.y).toFixed(2))
                              .arg(Number(sceneController.boundsCenter.z).toFixed(2))
                              .arg(Number(sceneController.boundingRadius).toFixed(2)) :
                          qsTr("准备就绪")
            }

            Label {
                Layout.fillWidth: true
                elide: Label.ElideRight
                text: sceneController.hasVisibleData ?
                          qsTr("已加载 %1 个模型").arg(sceneController.meshes.length) :
                          qsTr("拖拽或打开 PLY/STL/OBJ 文件")
            }
        }
    }
}
