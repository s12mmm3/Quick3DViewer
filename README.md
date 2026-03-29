# Quick3DViewer

基于 Qt Quick 3D 的跨平台模型浏览器
支持跨平台 (Windows, macOS, Linux, Android, iOS) 和多种编译器编译

Quick3DViewer is a cross-platform model viewer built with Qt Quick 3D.
Support cross platform (Windows, macOS, Linux, Android, iOS) and compilation with multiple compilers.

<p align=center>
    <img alt="Screenshot" src="./static/screenshot.png">
</p>

## 功能 / Features

- 拖拽、文件对话框或直接选择文件夹批量导入 PLY / STL / OBJ / glTF / glb 模型，支持多模型同时显示与列表管理。

- Drag-and-drop, file dialogs, or full-folder import for PLY / STL / OBJ / glTF / glb files with simultaneous multi-model display and list management.

## 需求和依赖 / Requirements and dependencies

- [Qt 6.9+](https://www.qt.io/download-qt-installer)

## 构建 / Build

将“/path/to/install”更改为软件应该安装的位置。

Change '/path/to/install' to the location where the software should be installed.

```bash
git clone --recursive https://github.com/s12mmm3/Quick3DViewer.git
cd Quick3DViewer
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/path/to/install
cmake --build build
```