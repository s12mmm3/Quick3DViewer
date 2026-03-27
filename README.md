# Q3DViewer

基于 Qt 6 Quick 3D 的轻量模型浏览器，支持在运行时直接加载 `PLY`、`STL`（ASCII/二进制）与 `OBJ` 模型文件，自动生成几何数据并在 `View3D` 中展示。

## 功能

- 拖拽或 `Ctrl/Cmd + O` 打开 PLY/STL/OBJ 模型。
- 自定义 `MeshLoader` 解析器，支持 ASCII PLY、ASCII/Binary STL、简单 OBJ（含位置/法线/纹理坐标）。
- 自动计算包围盒/包围球，将模型居中并驱动 `OrbitCameraController`。
- Quick Controls UI 提供打开、重置视图、错误提示等控件。

## 构建

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### 运行

```bash
./build/Q3DViewer
```

确保环境中安装 Qt 6.5+，并包含 `qtquick3d` 模块。

## 已知限制

- 仅解析 ASCII PLY；OBJ 目前仅支持三角形/多边形（不含材质/组等高级特性）。
- 模型中若缺少法线会自动计算平滑法线；未实现纹理贴图。
- 大模型解析在主线程中完成，可能出现短暂阻塞，可按需拓展线程加载。
