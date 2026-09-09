# HTMSR 开发环境表

## 1. 文档目的

本文档用于记录 HTMSR 当前可用的开发、构建、运行环境。后续把项目复制到另一台电脑时，优先按本文档准备依赖，再参考根目录 `README.md` 和 `docs/prepare.md` 进行配置。

项目目前以 Windows 桌面端为主，核心功能包括离线双目标定、离线线激光三维重建、点云显示、海康双相机在线采集，以及振镜/激光控制器串口联动流程。

## 2. 基础开发环境

| 类型 | 当前建议 | 说明 |
| --- | --- | --- |
| 操作系统 | Windows 10 / Windows 11 x64 | 当前项目主要在 Windows 下开发和测试 |
| IDE | Visual Studio 2022 | 使用 VS 的 CMake 工程打开项目 |
| 编译器 | MSVC x64 | 与 Qt、PCL、VTK、OpenCV 的预编译版本保持一致 |
| 构建系统 | CMake + Ninja | 通过 `CMakePresets.json` 或 `CMakeUserPresets.json` 管理配置 |
| C++ 标准 | C++17 | 当前 CMake 已按 C++17 配置 |
| Git | Git for Windows | 用于版本管理和 GitHub 同步 |

建议检查命令：

```powershell
git --version
cmake --version
ninja --version
```

## 3. 第三方依赖

| 依赖 | 推荐版本/路径 | 用途 |
| --- | --- | --- |
| Qt | `C:/ENVIORNMENT/qt/5.15.2/msvc2019_64` | Qt Widgets 桌面界面、Dock、信号槽、串口模块 |
| OpenCV | `C:/ENVIORNMENT/opencv_450_vs2019` | 图像读取、棋盘格角点检测、标定、畸变处理、图像预处理 |
| Eigen | `C:/ENVIORNMENT/ceresLib/Eigen` | 三维点、矩阵、向量等数学类型 |
| PCL | `C:/ENVIORNMENT/PCL/PCL 1.12.1` | 点云数据结构、点云导出、可选可视化依赖 |
| VTK | 随 PCL 或独立 VTK 9.1 | 点云 Qt 嵌入显示；不完整时 Release 会退回占位视图 |
| Hik MVS SDK | `C:/ENVIORNMENT/MVS` | 海康工业相机枚举、连接、参数设置、取流 |
| MVS Runtime | `C:/Program Files (x86)/Common Files/MVS/Runtime/Win64_x64` | 海康运行时 DLL |

注意：当前路径中 `ENVIORNMENT` 是项目已有约定拼写，不要误改成 `ENVIRONMENT`，否则 CMake 预设和脚本会找不到依赖。

## 4. CMake 预设

项目根目录已有 `CMakePresets.json`，常用预设如下：

| 配置预设 | 构建预设 | 输出目录 | 用途 |
| --- | --- | --- | --- |
| `vs2022-x64-debug` | `debug` | `out/build/vs2022-x64-debug` | 调试开发，保留更多符号信息 |
| `vs2022-x64-release` | `release` | `out/build/vs2022-x64-release` | 发布构建，用于运行和打包验证 |

如果另一台电脑的依赖路径不同，建议不要直接修改仓库内 `CMakePresets.json`，而是运行配置脚本生成本机专用的 `CMakeUserPresets.json`。

## 5. 一键环境配置脚本

项目提供脚本：

```text
C:/PROJECT/HTMSR/scripts/Configure-HtmsrEnvironment.ps1
```

首次使用建议执行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Configure-HtmsrEnvironment.ps1 -CreateDefault
```

然后编辑：

```text
C:/PROJECT/HTMSR/scripts/htmsr_environment.local.json
```

确认 Qt、OpenCV、PCL、Eigen、MVS、VTK 路径后，再执行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Configure-HtmsrEnvironment.ps1 -Configure
```

脚本会生成本机专用的 `CMakeUserPresets.json`，并把依赖路径写入 CMake 配置。

## 6. 构建命令

Debug 构建：

```powershell
cmake --preset vs2022-x64-debug
cmake --build --preset debug
```

Release 构建：

```powershell
cmake --preset vs2022-x64-release
cmake --build --preset release
```

如果使用本机脚本生成的 local 预设：

```powershell
cmake --preset local-vs2022-x64-debug
cmake --build --preset local-debug

cmake --preset local-vs2022-x64-release
cmake --build --preset local-release
```

## 7. 运行时依赖注意事项

### 7.1 Qt

Release 程序应加载：

```text
Qt5Core.dll
Qt5Gui.dll
Qt5Widgets.dll
Qt5SerialPort.dll
```

Debug 程序应加载：

```text
Qt5Cored.dll
Qt5Guid.dll
Qt5Widgetsd.dll
Qt5SerialPortd.dll
```

不要让 Release 程序加载带 `d` 后缀的 Debug Qt DLL，否则容易出现：

```text
QWidget: Must construct a QApplication before a QWidget
```

### 7.2 VTK

当前项目支持嵌入 VTK Qt 点云视图，但前提是 VTK 的 Qt 支持库与当前构建类型匹配。

如果 Release 构建只能找到 Debug 版 `vtkGUISupportQt-9.1d.dll` 或 Debug import lib，CMake 会禁用内嵌 VTK 点云视图，并让界面退回占位显示。这样可以保证主程序仍然可以启动、标定、重建和导出点云。

### 7.3 海康 MVS

在线采集前需要安装 MVS SDK，并确认 MVS 运行时 DLL 能被系统找到。若 MVS 官方客户端正在占用相机，HTMSR 可能无法连接设备，测试前建议关闭 MVS 客户端。

## 8. 功能模块与依赖关系

| 功能 | 主要依赖 | 关键代码 |
| --- | --- | --- |
| 离线标定 | OpenCV | `src/core/CalibrationService.*` |
| 激光中心线提取 | OpenCV | `src/core/LaserExtractionService.*` |
| 双目重建 | OpenCV + Eigen | `src/core/ReconstructionService.*` |
| 点云保存 | PCL | `src/core/PointCloudService.*` |
| 点云显示 | VTK + PCL + Qt | `src/app/ui/PointCloudViewWidget.*` |
| 离线图像序列 | OpenCV | `src/app/acquisition/OfflineImageSequenceProvider.*` |
| 海康在线采集 | Hik MVS SDK | `src/app/acquisition/HikCameraDevice.*` |
| 双相机采集封装 | Hik MVS SDK | `src/app/acquisition/HikStereoCameraProvider.*` |
| 振镜串口控制 | Qt SerialPort | `src/app/acquisition/GalvoController.*` |
| 一键自动标定 | Qt + OpenCV + MVS | `src/app/services/IntegratedCalibrationCaptureService.*` |
| 一键扫描重建 | Qt + MVS + 串口 + 核心重建 | `src/app/services/IntegratedScanService.*` |

## 9. 推荐测试素材

| 目录 | 用途 |
| --- | --- |
| `C:/PROJECT/HTMSR/test/01_calibration` | 离线标定图像 |
| `C:/PROJECT/HTMSR/test/02_reconstruction` | 离线重建图像序列 |
| `C:/PROJECT/HTMSR/test/03_reference_results` | 参考点云结果 |
| `C:/PROJECT/HTMSR/test/04_actual_outputs` | 当前程序实际输出 |

测试时不要把 `03_reference_results` 当作程序输出目录，它是对照参考结果。

## 10. 环境迁移建议

如果只是运行软件，优先使用打包脚本生成发布目录，然后把整个发布目录复制到另一台电脑。

如果要继续开发，需要在另一台电脑上安装完整开发环境，并用配置脚本生成本机 `CMakeUserPresets.json`。

建议后续把不可自动安装的依赖单独记录版本号和下载来源，尤其是 Qt 5.15.2、MVS SDK、PCL/VTK 的编译配置，避免不同机器之间出现 ABI 或 Debug/Release 混用问题。
