# HTMSR

HTMSR 是一个基于 `Qt 5.15.2 + Visual Studio 2022 + CMake + OpenCV + Eigen + PCL` 的双目线激光三维重建桌面软件。项目当前已经覆盖离线双目标定、离线激光三维重建、点云导出、海康相机在线采集预留与振镜串口联动流程。

当前软件的核心目标是：先保证双目线激光扫描仪的软件框架、标定链路、重建链路和在线采集链路可以串起来，再逐步用真实硬件数据优化标定精度、中心线提取、匹配策略和点云质量。

## 当前能力

- 离线双目标定：读取左右棋盘格图像，计算 `K1/D1/K2/D2/R/t/E/F`，保存 `stereo_calibration.yml`。
- 离线重建：读取左右线激光图像，提取激光中心线，按双目几何恢复三维点云。
- 激光中心线提取：支持灰度重心法和 Steger 方法。
- 点云输出：支持 TXT / PCD 导出。
- 点云显示：Debug / Release 均可使用 VTK Qt 三维视图；如果 VTK Qt 组件不可用，会自动退回占位视图。
- 在线采集：支持海康 MVS 相机枚举、连接、曝光/增益/触发参数配置、左右图像采集保存。
- 振镜联动：支持按 `振镜通信协议.docx` 通过 Windows 串口 COM 下发振镜、电机、激光和采集节拍相关参数。
- 一键流程：提供“一键自动标定”和“一键扫描重建”入口，复用现有标定与重建核心服务。

## 目录结构

```text
C:\PROJECT\HTMSR
|-- CMakeLists.txt
|-- CMakePresets.json
|-- README.md
|-- docs
|-- references
|-- scripts
|-- src
|   |-- core
|   |-- app
|       |-- acquisition
|       |-- services
|       |-- ui
|-- test
|-- out
```

主要目录职责：

| 目录 | 作用 |
|---|---|
| `src/core` | 纯业务核心层，包含标定、中心线提取、三维重建、点云导出、日志和文件工具。 |
| `src/app/acquisition` | 采集与设备抽象层，包含海康相机、模拟采集、离线序列、振镜串口控制。 |
| `src/app/services` | 应用服务层，负责配置持久化、日志桥接、在线采集、自动标定、扫描重建工作流。 |
| `src/app/ui` | Qt Widgets 界面层，包含主窗口、参数面板、在线采集面板、图像视图、点云视图和日志表。 |
| `docs` | 开发环境、准备流程、编码规范等项目文档。 |
| `references` | 旧实现和 OpenCorr 参考资料，只作分析参考，不参与编译。 |
| `test` | 测试素材、参考点云和实际输出目录。 |
| `out` | CMake 构建、打包和运行输出目录。 |

## 环境依赖

当前项目默认 Windows x64 + MSVC x64。推荐另一台电脑尽量保持同样目录结构，这样可以少改 CMake 配置。

| 依赖 | 推荐版本/路径 | 是否必需 | 用途 |
|---|---|---:|---|
| Visual Studio 2022 | Community 或更高版本 | 是 | MSVC x64 编译、CMake 集成、调试。 |
| CMake | VS 自带即可，3.24+ | 是 | 工程配置和生成。 |
| Ninja | VS 自带即可 | 是 | CMake 构建后端。 |
| Qt | `C:\ENVIORNMENT\qt\5.15.2\msvc2019_64` | 是 | Qt Widgets 桌面界面。 |
| OpenCV | `C:\ENVIORNMENT\opencv_450_vs2019` | 是 | 图像读取、标定、畸变处理、图像预处理。 |
| Eigen | `C:\ENVIORNMENT\ceresLib\Eigen` | 是 | 矩阵、向量、三维几何计算。 |
| PCL | `C:\ENVIORNMENT\PCL\PCL 1.12.1` | 是 | 点云结构、PCD 保存。 |
| Boost / Qhull / FLANN | PCL 第三方目录内 | 间接必需 | PCL 依赖。 |
| VTK Qt | PCL 自带或单独安装 | 可选 | 内嵌三维点云视图。 |
| Hikrobot MVS SDK | `C:\ENVIORNMENT\MVS` | 接真实相机时必需 | 海康相机枚举、连接、取流和参数配置。 |
| MVS Runtime | `C:\Program Files (x86)\Common Files\MVS\Runtime\Win64_x64` | 接真实相机时必需 | 海康运行时 DLL、GenICam DLL、CTI 文件。 |
| 串口驱动 | 由振镜控制器/USB 转串口决定 | 接振镜时必需 | 提供 `COMx` 串口。 |

注意：项目里当前路径拼写是 `C:\ENVIORNMENT`，不是常见的 `C:\ENVIRONMENT`。如果另一台电脑路径不同，优先通过 `CMakeUserPresets.json` 或配置脚本改本机路径，不建议直接改公共 `CMakePresets.json`。

## 本机环境配置

项目提供了环境路径配置脚本，用来在不同电脑上生成本机专用 `CMakeUserPresets.json`。

首次使用可以执行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Configure-HtmsrEnvironment.ps1 -CreateDefault
```

然后按本机实际路径修改：

```text
scripts/htmsr_environment.local.json
```

修改后重新生成 CMake 本机 preset：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Configure-HtmsrEnvironment.ps1
```

如果要跳过海康相机编译，可以把本机配置里的 `enableHikCamera` 改为 `false`。这样仍可测试离线标定、离线重建和点云导出。

说明：`CMakeLists.txt` 中 `HTMSR_ENABLE_HIK_CAMERA` 的基础默认值是 `OFF`，公共 `CMakePresets.json` 和本机 `CMakeUserPresets.json` 可以把它打开。迁移到新电脑时优先改本机 JSON，不建议直接改公共 preset。

## 构建方式

推荐直接用 Visual Studio 2022 打开项目文件夹：

```text
C:\PROJECT\HTMSR
```

也可以在命令行构建。

Debug：

```powershell
cmake --preset vs2022-x64-debug
cmake --build --preset debug
```

Release：

```powershell
cmake --preset vs2022-x64-release
cmake --build --preset release
```

如果使用本机专用 preset：

```powershell
cmake --preset local-vs2022-x64-debug
cmake --build --preset local-debug

cmake --preset local-vs2022-x64-release
cmake --build --preset local-release
```

普通 PowerShell 如果找不到 MSVC 编译环境，建议使用 Visual Studio Developer PowerShell，或先执行：

```powershell
cmd /d /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 && cmake --preset vs2022-x64-release && cmake --build --preset release"
```

## 运行方式

构建后可运行：

```text
C:\PROJECT\HTMSR\out\build\vs2022-x64-debug\htmsr_app.exe
C:\PROJECT\HTMSR\out\build\vs2022-x64-release\htmsr_app.exe
```

当前构建会在 exe 同目录复制 Qt、MVS 等运行时文件，并生成 `qt.conf`，用于避免 Qt 插件路径污染。

如果只把软件发到另一台电脑上运行，不要只复制 `htmsr_app.exe`。需要复制完整发布目录或完整构建输出目录，至少包含：

- `htmsr_app.exe`
- Qt DLL
- `platforms/qwindows.dll`
- OpenCV / PCL / MVS 相关 DLL
- `qt.conf`

## UI 使用入口

主界面主要分为几个区域：

| 区域 | 功能 |
|---|---|
| 左侧资源树 | 显示标定数据、重建数据、采集结果和点云数量。 |
| 中央视图 | 显示点云、左图、右图和调试图。 |
| 右侧参数监控 | 配置项目路径、标定参数、重建参数。 |
| 右侧在线采集 | 配置海康相机、振镜串口参数，并执行采集/自动标定/扫描重建。 |
| 底部消息 | 显示 App、Calibration、Reconstruction、HikCamera、Galvo、IntegratedWorkflow 等日志。 |

需要控制相机时，请切换到右侧底部的 `在线采集` 标签，而不是 `参数监控 -> 采集` 页。

## 离线标定流程

输入素材：

- 左标定图像目录
- 右标定图像目录
- 棋盘格内角点数量
- 方格实际尺寸
- 输出标定文件路径

代码链路：

```text
MainWindow::runCalibration
-> ParameterPanel::calibrationInput
-> CalibrationService::calibrate
-> listImageFiles
-> readImages
-> calibrateSingleCamera
-> cv::findChessboardCornersSB
-> cv::cornerSubPix
-> cv::stereoCalibrate
-> saveCalibration
-> MainWindow::onCalibrationFinished
```

输出：

```text
stereo_calibration.yml
```

说明：真实重建必须使用项目左右相机在相同分辨率、相同镜头状态下拍摄的棋盘格图像。网上或 OpenCV 官方样例图只能验证流程，不能用于真实精度评估。

## 离线重建流程

输入素材：

- 左重建图像目录
- 右重建图像目录
- 有效双目标定文件
- ROI、灰度阈值、最小灰度、线宽、匹配距离等重建参数

代码链路：

```text
MainWindow::runReconstruction
-> ParameterPanel::reconstructionInput
-> ReconstructionService::reconstruct
-> listImageFiles
-> reconstructFrame
-> LaserExtractionService::extract
-> cv::undistortPoints
-> pixelToRay
-> 左右中心线匹配
-> closestPointBetweenLines
-> PointCloudService::mergeFrames
-> PointCloudViewWidget::setPoints
```

输出：

- 批量三维点云
- 左右中心线调试图
- `point_cloud.txt`
- `point_cloud.pcd`

每帧重建日志会输出诊断信息，包括左右中心线点数、覆盖率、匹配数、匹配率、误差、点云范围和失败原因。

## 在线采集与振镜联动

在线采集面板分为两层控制。

**相机采集**

这一部分直接控制海康相机，走 MVS SDK：

- 左相机 / 右相机
- 使用模拟采集
- 采集帧数
- 保存目录
- 曝光 `us`
- 增益
- 硬触发
- 触发线
- 超时 `ms`

对应代码：

```text
src/app/acquisition/HikCameraDevice.*
src/app/acquisition/HikStereoCameraProvider.*
```

当前相机参数不是输入框变化时立即下发，而是在点击 `采集保存`、`一键自动标定` 或 `一键扫描重建` 后，连接并配置相机时下发。

**振镜控制**

这一部分按 `振镜通信协议.docx` 通过串口控制振镜控制器，不直接控制海康相机本体：

- 串口号
- 波特率
- 命令超时
- 同步模式
- 扫描方向
- 抓图间隔
- 连续模式等待
- 步进角度
- 自动旋转角度
- 正向速度
- 反向速度
- 激光占空比
- 电压范围

对应代码：

```text
src/app/acquisition/GalvoController.*
```

一键扫描重建的大致执行顺序：

```text
打开串口并连接振镜控制器
-> 下发振镜参数
-> 枚举并连接左右海康相机
-> 配置曝光、增益、触发模式
-> 开始取流
-> 振镜打开激光并触发连续采集
-> 抓取左右图像并保存 left/right
-> 加载已有 stereo_calibration.yml
-> 自动执行重建
-> 刷新资源树、图像预览和点云结果
```

自动标定流程会在线采集左右棋盘格图像，然后调用现有 `CalibrationService`。扫描图像和棋盘格标定图像不是同一种素材，不建议混在同一次任务中完成。

当前一键扫描重建要求启用海康编译、关闭模拟采集、选择真实左右相机，并且提前准备有效 `stereo_calibration.yml`。如果勾选 `扫描前强制重标`，当前实现会提示先运行一键自动标定，而不是在扫描流程里自动采棋盘并重标。

## 真实硬件联调顺序

建议按下面顺序测试，别一上来就点一键全流程。这样出问题时更容易定位。

1. 在海康 MVS 客户端确认左右相机都能预览。
2. 关闭 MVS 客户端，避免设备被占用。
3. 在设备管理器确认振镜控制器串口号，例如 `COM3`。
4. 启动 HTMSR，进入 `在线采集`。
5. 点 `刷新设备`，确认左右相机能枚举出来。
6. 取消 `使用模拟采集`。
7. 先点 `采集保存`，确认能生成 `left/right` 图像。
8. 放置棋盘格，点 `一键自动标定`，确认生成有效 `stereo_calibration.yml`。
9. 换线激光扫描场景，点 `一键扫描重建`。
10. 查看日志中的中心线、匹配率、点数和失败原因。

## 点云显示说明

当前项目支持两种点云显示模式：

| 模式 | 说明 |
|---|---|
| VTK 三维视图 | 使用 `QVTKOpenGLNativeWidget + vtkPoints/vtkActor` 原生 VTK 管线，可交互旋转缩放。 |
| 占位视图 | 只显示点数和提示文字，仍可标定、重建和导出点云。 |

当前 Release 构建优先使用 PCL 自带 VTK 的 Release DLL。如果该环境缺少 `VTK::GUISupportQt` 的 Release 导入库，但存在匹配的 `vtkGUISupportQt-9.1.dll`，CMake 会用 `dumpbin`/`lib` 在构建目录生成 Release 导入库，并启用内嵌 VTK 视图。

构建输出目录会自动拷贝 PCL/VTK/FLANN/Qhull/Boost/OpenNI2 相关运行库；Release 构建会过滤 `*d.dll`、`*-gd.dll`、`*_rd.dll` 这类 Debug 运行库，并跳过 `msvcp*`、`vcruntime*`、`concrt*` 等 VC runtime 副本，避免 Qt/VTK/PCL 运行库混用。

## 测试素材

建议测试素材按用途分开：

```text
test/
|-- 01_calibration/
|-- 02_reconstruction/
|-- 03_reference_results/
|-- 04_actual_outputs/
```

推荐含义：

| 目录 | 用途 |
|---|---|
| `01_calibration` | 左右棋盘格标定图像。 |
| `02_reconstruction` | 左右线激光重建图像。 |
| `03_reference_results` | 参考点云或标准输出结果。 |
| `04_actual_outputs` | 当前程序实际输出结果。 |

注意：标定图像目录和重建图像目录不能混用。标定图像是棋盘格，重建图像是线激光扫描图。

## 打包和迁移

只给别人测试运行时，推荐使用打包脚本：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Package-HtmsrRelease.ps1 -Configuration Release -Clean
```

输出目录：

```text
C:\PROJECT\HTMSR\out\package\HTMSR_release
```

另一台电脑直接复制整个 `HTMSR_release` 目录运行，不要只复制 exe。

如果另一台电脑需要接真实海康相机，还需要安装 MVS 客户端/运行时，并先在 MVS 中确认相机可用。如果需要接振镜控制器，还需要安装对应串口驱动。

## 常见问题

**程序启动提示缺少 Qt platform plugin**

检查：

```text
platforms/qwindows.dll
qt.conf
```

是否和 `htmsr_app.exe` 在同一个发布目录结构中。

**Release 弹出 Qt5Cored.dll 或 QWidget before QApplication**

这是 Release 混入不匹配 Qt/VTK/VC 运行库的典型表现。当前 CMake 会为缺失的 `VTK::GUISupportQt` Release 导入库生成本地 `.lib`，并在拷贝运行库时过滤 Debug DLL 和 VC runtime 副本。重新配置并构建 Release 后，确认输出目录中没有 `Qt5Cored.dll`、`vtk*9.1d.dll`、`msvcp*.dll`、`vcruntime*.dll` 这类文件。

**在线采集枚举不到相机**

先检查：

- MVS 客户端能否看到相机。
- MVS 客户端是否仍在占用相机。
- `HTMSR_ENABLE_HIK_CAMERA` 是否为 `ON`。
- `HIK_MVS_ROOT` 和 `HIK_MVS_RUNTIME_DIR` 是否正确。
- GigE 相机的 IP、网卡、Jumbo Frame、防火墙是否正常。

**重建点云形状很怪**

优先检查：

- 标定文件是否来自同一套真实左右相机。
- 标定图像分辨率是否和重建图像一致。
- ROI 是否覆盖激光条纹。
- 每帧日志中的 `leftLine/rightLine/matched/matchErrMean/matchErrMax/reason`。
- 左右图像文件是否一一配对。

## 关键代码入口

| 功能 | 代码 |
|---|---|
| 双目标定 | `src/core/CalibrationService.*` |
| 激光中心线提取 | `src/core/LaserExtractionService.*` |
| 三维重建 | `src/core/ReconstructionService.*` |
| 点云导出 | `src/core/PointCloudService.*` |
| 文件扫描与排序 | `src/core/FileSystemUtils.*` |
| 海康相机设备 | `src/app/acquisition/HikCameraDevice.*` |
| 双海康相机 Provider | `src/app/acquisition/HikStereoCameraProvider.*` |
| 振镜串口控制 | `src/app/acquisition/GalvoController.*` |
| 在线采集服务 | `src/app/services/AcquisitionService.*` |
| 自动标定采集 | `src/app/services/IntegratedCalibrationCaptureService.*` |
| 扫描重建工作流 | `src/app/services/IntegratedScanService.*` |
| 主窗口任务编排 | `src/app/ui/MainWindow.*` |
| 参数面板 | `src/app/ui/ParameterPanel.*` |
| 在线采集面板 | `src/app/ui/AcquisitionPanel.*` |

## 后续开发建议

- 优先补强左右图像配对：按文件名编号或时间戳严格配对，避免缺帧后整体错位。
- 标定阶段增加质量门槛：有效双目对数、RMS、每图误差过大时不要静默保存。
- 重建匹配阶段加入极线搜索窗口、唯一匹配和左右一致性检查。
- 真机联调振镜协议时，补充设备回包格式解析和更清晰的错误码映射。
- 在线采集后续可以增加实时预览，但当前优先保证保存图像、自动标定、自动重建这条闭环稳定。

## 相关文档

- `C:\PROJECT\docs\HTMSR_项目结构功能说明书.md`
- `C:\PROJECT\docs\HTMSR_项目分析与离线重建流程.md`
- `C:\PROJECT\docs\HTMSR_开发环境表.md`
- `C:\PROJECT\docs\环境路径配置脚本说明.md`
- `C:\PROJECT\docs\standard.md`
- `C:\PROJECT\HTMSR\docs\prepare.md`
- `C:/Users/Administrator/Desktop/振镜通信协议.docx`

后续如果项目结构、依赖版本、在线采集流程或标定/重建策略发生变化，应优先同步更新本 README。
