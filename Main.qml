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
    property vector3d lastFitCenter: Qt.vector3d(0, 0, 0)
    property real minCameraDistance: 1
    property real maxCameraDistance: 500000
    property real orbitYaw: 0
    property real orbitPitch: 0

    Component {
        id: meshLoaderComponent
        MeshLoader { }
    }

    ListModel {
        id: meshListModel
    }

    QtObject {
        id: sceneController
        property int nextId: 1
        property bool hasVisibleData: false
        property vector3d boundsMin: Qt.vector3d(0, 0, 0)
        property vector3d boundsMax: Qt.vector3d(0, 0, 0)
        property vector3d boundsCenter: Qt.vector3d(0, 0, 0)
        property real boundingRadius: 1.0
        property string lastError: ""
        property var pivotNode: null
        property var pendingSources: []

        function indexOfMesh(id) {
            for (let i = 0; i < meshListModel.count; ++i) {
                const entry = meshListModel.get(i);
                if (entry && entry.itemId === id)
                    return i;
            }
            return -1;
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
                itemId: nextId++,
                loader: loader,
                sourceUrl: source,
                displayName: window.fileNameFromUrl(source),
                visibleFlag: true,
                opacityValue: 1.0,
                autoFitPending: true
            };
            meshListModel.append(entry);
            const currentId = entry.itemId;
            const boundsChanged = function() {
                sceneController.recalculateBounds();
                const idx = sceneController.indexOfMesh(currentId);
                if (idx >= 0) {
                    const row = meshListModel.get(idx);
                    if (row.autoFitPending && loader.hasData) {
                        meshListModel.setProperty(idx, "autoFitPending", false);
                        window.fitSceneToVisible();
                    }
                }
            };

            loader.boundsChanged.connect(boundsChanged);
            loader.hasDataChanged.connect(boundsChanged);
            loader.errorStringChanged.connect(sceneController.refreshErrorMessage);
            loader.source = source;

            window.currentFile = source;
            recalculateBounds();
            refreshErrorMessage();
        }

        function removeMesh(id) {
            const index = indexOfMesh(id);
            if (index < 0)
                return;
            const entry = meshListModel.get(index);
            const loader = entry ? entry.loader : null;
            meshListModel.remove(index);
            Qt.callLater(() => { if (loader) loader.destroy() })
            recalculateBounds();
            refreshErrorMessage();
        }

        function setVisible(id, visible) {
            const index = indexOfMesh(id);
            if (index < 0)
                return;
            const entry = meshListModel.get(index);
            if (!entry)
                return;
            if (entry.visibleFlag === visible)
                return;
            meshListModel.setProperty(index, "visibleFlag", visible);
            recalculateBounds();
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
            for (let i = 0; i < meshListModel.count; ++i) {
                const entry = meshListModel.get(i);
                if (entry && entry.loader && entry.loader.parent !== pivotNode)
                    entry.loader.parent = pivotNode;
            }
        }

        function setOpacity(id, opacity) {
            const value = Math.min(1.0, Math.max(0.05, opacity));
            const index = indexOfMesh(id);
            if (index < 0)
                return;
            const entry = meshListModel.get(index);
            if (!entry)
                return;
            if (Math.abs(entry.opacityValue - value) <= 0.0001)
                return;
            meshListModel.setProperty(index, "opacityValue", value);
        }

        function recalculateBounds() {
            let valid = false;
            let minPoint = null;
            let maxPoint = null;
            for (let i = 0; i < meshListModel.count; ++i) {
                const entry = meshListModel.get(i);
                const loader = entry.loader;
                if (!entry.visibleFlag || !loader || !loader.hasData)
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
                window.lastFitCenter = Qt.vector3d(0, 0, 0);
            }
            hasVisibleData = valid;
        }

        function refreshErrorMessage() {
            let message = "";
            for (let i = 0; i < meshListModel.count; ++i) {
                const loader = meshListModel.get(i).loader;
                if (loader && loader.errorString.length > 0) {
                    message = loader.errorString;
                    break;
                }
            }
            lastError = message;
        }
    }

    function applyOrbitRotation() {
        cameraRig.eulerRotation = Qt.vector3d(orbitPitch, orbitYaw, 0)
    }

    function setCameraDistance(distance) {
        const clamped = Math.min(maxCameraDistance, Math.max(minCameraDistance, distance))
        lastFitDistance = clamped
        camera.position = Qt.vector3d(0, 0, clamped)
        updateCameraClip(clamped)
        updateFreeCameraSettings(clamped)
    }

    function orbitCamera(deltaX, deltaY) {
        if (!sceneController.hasVisibleData)
            return
        const sensitivity = 0.3
        orbitYaw = orbitYaw - deltaX * sensitivity
        orbitPitch = Math.max(-89.5, Math.min(89.5, orbitPitch - deltaY * sensitivity))
        applyOrbitRotation()
    }

    function zoomByFactor(factor) {
        if (!sceneController.hasVisibleData)
            return
        if (factor === 0)
            return
        const newDistance = Math.max(minCameraDistance,
                                     Math.min(maxCameraDistance,
                                              camera.position.length() * factor))
        setCameraDistance(newDistance)
    }

    function zoomCamera(deltaValue) {
        if (!sceneController.hasVisibleData)
            return
        if (deltaValue === 0)
            return
        const factor = Math.exp(-deltaValue * 0.0015)
        zoomByFactor(factor)
    }

    function panCamera(deltaX, deltaY) {
        if (!sceneController.hasVisibleData)
            return
        const height = Math.max(view3d.height, 1)
        const width = Math.max(view3d.width, 1)
        const distance = Math.max(camera.position.length(), minCameraDistance)
        const fovRad = camera.fieldOfView * Math.PI / 180
        const viewHeightAtDist = 2 * Math.tan(fovRad / 2) * distance
        const viewWidthAtDist = viewHeightAtDist * width / height
        const moveRight = -deltaX * (viewWidthAtDist / width)
        const moveUp = deltaY * (viewHeightAtDist / height)
        const rightVec = cameraRig.right
        const upVec = cameraRig.up
        const translation = Qt.vector3d(rightVec.x * moveRight + upVec.x * moveUp,
                                        rightVec.y * moveRight + upVec.y * moveUp,
                                        rightVec.z * moveRight + upVec.z * moveUp)
        const newPos = Qt.vector3d(cameraRig.position.x + translation.x,
                                   cameraRig.position.y + translation.y,
                                   cameraRig.position.z + translation.z)
        cameraRig.position = newPos
    }

    function restoreSceneFocus() {
        if (view3d)
            view3d.forceActiveFocus()
    }

    function updateFreeCameraSettings(distance) {
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
        window.lastFitCenter = center
        pivot.position = Qt.vector3d(0, 0, 0)
        const size = Qt.vector3d(max.x - min.x, max.y - min.y, max.z - min.z)
        const longest = Math.max(size.x, Math.max(size.y, size.z))
        const radius = Math.max(radiusHint, longest * 0.5, 1)
        const fovRadians = Math.max(5, camera.fieldOfView) * Math.PI / 180
        const fitDistance = radius / Math.tan(fovRadians * 0.5)
        const targetDistance = Math.max(fitDistance * 1.2, 20)
        cameraRig.position = center
        orbitYaw = 0
        orbitPitch = 0
        applyOrbitRotation()
        setCameraDistance(targetDistance)
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
        cameraRig.position = window.lastFitCenter
        orbitPitch = rotation.x
        orbitYaw = rotation.y
        applyOrbitRotation()
        setCameraDistance(lastFitDistance)
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
                    model: meshListModel
                    delegate: Node {
                        id: meshEntry
                        required property int itemId
                        required property var loader
                        required property string displayName
                        required property bool visibleFlag
                        required property real opacityValue
                        property bool isTransparent: opacityValue < 0.999
                        property bool hasTexture: loader && loader.colorTexture && loader.colorTexture.toString().length > 0
                        visible: visibleFlag && loader && loader.hasData
                        parent: pivot

                        Texture {
                            id: meshTexture
                            source: loader ? loader.colorTexture : ""
                            tilingModeHorizontal: Texture.ClampToEdge
                            tilingModeVertical: Texture.ClampToEdge
                        }

                        Model {
                            id: frontFaceModel
                            parent: meshEntry
                            geometry: loader
                            visible: meshEntry.visible
                            opacity: opacityValue
                            materials: PrincipledMaterial {
                                baseColor: meshEntry.hasTexture ? "#ffffff" : "#9a9a9f"
                                baseColorMap: meshEntry.hasTexture ? meshTexture : null
                                metalness: 0.0
                                roughness: 0.6
                                specularAmount: 0.25
                                cullMode: Material.BackFaceCulling
                                opacity: opacityValue
                                alphaMode: meshEntry.isTransparent ?
                                               PrincipledMaterial.Blend :
                                               PrincipledMaterial.Opaque
                                depthDrawMode: meshEntry.isTransparent ?
                                                   Material.NeverDepthDraw :
                                                   Material.OpaqueOnlyDepthDraw
                            }
                        }

                        Model {
                            id: backFaceModel
                            parent: meshEntry
                            geometry: loader
                            visible: meshEntry.visible
                            opacity: opacityValue
                            materials: PrincipledMaterial {
                                baseColor: "#303046"
                                metalness: 0.0
                                roughness: 0.7
                                specularAmount: 0.1
                                cullMode: Material.FrontFaceCulling
                                opacity: opacityValue
                                alphaMode: meshEntry.isTransparent ?
                                               PrincipledMaterial.Blend :
                                               PrincipledMaterial.Opaque
                                depthDrawMode: meshEntry.isTransparent ?
                                                   Material.NeverDepthDraw :
                                                   Material.OpaqueOnlyDepthDraw
                            }
                        }
                    }
                }

                WasdController {
                    id: freeController
                    anchors.fill: parent
                    controlledObject: cameraRig
                    mouseEnabled: false
                    keysEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
                }

                DragHandler {
                    id: orbitDragHandler
                    target: null
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchScreen | PointerDevice.TouchPad | PointerDevice.Stylus
                    acceptedButtons: Qt.LeftButton
                    property real lastX: 0
                    property real lastY: 0
                    onActiveChanged: {
                        lastX = translation.x
                        lastY = translation.y
                        if (active)
                            view3d.forceActiveFocus()
                    }
                    onTranslationChanged: {
                        if (!active)
                            return
                        const dx = translation.x - lastX
                        const dy = translation.y - lastY
                        lastX = translation.x
                        lastY = translation.y
                        window.orbitCamera(dx, dy)
                    }
                }

                DragHandler {
                    id: panDragHandler
                    target: null
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    acceptedButtons: Qt.MiddleButton | Qt.RightButton
                    property real lastX: 0
                    property real lastY: 0
                    onActiveChanged: {
                        lastX = translation.x
                        lastY = translation.y
                        if (active)
                            view3d.forceActiveFocus()
                    }
                    onTranslationChanged: {
                        if (!active)
                            return
                        const dx = translation.x - lastX
                        const dy = translation.y - lastY
                        lastX = translation.x
                        lastY = translation.y
                        window.panCamera(dx, dy)
                    }
                }

                PinchHandler {
                    id: pinchHandler
                    target: null
                    acceptedDevices: PointerDevice.TouchPad | PointerDevice.TouchScreen
                    minimumPointCount: 2
                    maximumPointCount: 4
                    property real lastScale: 1
                    property real lastRotation: 0
                    property real lastTx: 0
                    property real lastTy: 0
                    onActiveChanged: {
                        lastScale = 1
                        lastRotation = 0
                        lastTx = translation.x
                        lastTy = translation.y
                        if (active)
                            view3d.forceActiveFocus()
                    }
                    onScaleChanged: {
                        if (!active)
                            return
                        const delta = scale / lastScale
                        if (delta !== 0)
                            window.zoomByFactor(1 / delta)
                        lastScale = scale
                    }
                    onRotationChanged: {
                        if (!active)
                            return
                        const delta = rotation - lastRotation
                        window.orbitCamera(delta, 0)
                        lastRotation = rotation
                    }
                    onTranslationChanged: {
                        if (!active)
                            return
                        lastTx = translation.x
                        lastTy = translation.y
                    }
                }

                WheelHandler {
                    id: wheelZoomHandler
                    target: null
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    orientation: Qt.Vertical
                    onWheel: (event) => {
                        if (!sceneController.hasVisibleData)
                            return
                        const delta = event.pixelDelta.y !== 0 ? event.pixelDelta.y : event.angleDelta.y
                        window.zoomCamera(delta)
                        event.accepted = true
                    }
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
                visible: meshListModel.count > 0
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
                        model: meshListModel
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(contentHeight, 260)
                        clip: true
                        ScrollBar.vertical: ScrollBar { }
                        delegate: Rectangle {
                            required property int index
                            required property int itemId
                            required property string displayName
                            required property bool visibleFlag
                            required property real opacityValue
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
                                        checked: visibleFlag
                                        onToggled: {
                                            sceneController.setVisible(itemId, checked)
                                            window.restoreSceneFocus()
                                        }
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        elide: Label.ElideRight
                                        color: "#f5f5f5"
                                        text: displayName
                                    }
                                    ToolButton {
                                        text: qsTr("删")
                                        onClicked: {
                                            sceneController.removeMesh(itemId)
                                            window.restoreSceneFocus()
                                        }
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
                                        value: opacityValue
                                        onValueChanged: sceneController.setOpacity(itemId, value)
                                        onPressedChanged: {
                                            if (!pressed)
                                                window.restoreSceneFocus()
                                        }
                                    }
                                    Label {
                                        text: qsTr("%1%").arg(Math.round(opacityValue * 100))
                                        color: "#c0c3ca"
                                    }
                                }
                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 1
                                    color: "#3f4048"
                                    visible: index < meshListModel.count - 1
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
                          qsTr("已加载 %1 个模型").arg(meshListModel.count) :
                          qsTr("拖拽或打开 PLY/STL/OBJ 文件")
            }
        }
    }
}
