# HTMSR 项目结构功能说明书

## 1. 项目概述

HTMSR 是一个基于 Qt6、Visual Studio、CMake、OpenCV、Eigen、PCL 的双目标定与离线点云重建桌面软件工程。当前版本定位为离线处理工具，主要功能包括项目参数配置、双目标定、激光中心线提取、左右图像三维重建、点云显示占位/VTK 适配、点云导出和日志展示。

工程设计原则如下：

- `references` 目录仅作为业务逻辑参考，不参与编译，不修改其中代码。
- `src/core` 保持纯 C++ 业务核心，不依赖 Qt UI。
- `src/app` 负责 Qt 桌面界面、配置持久化、日志桥接和采集预留。
- 后续在线采集通过 `app/acquisition` 中的接口扩展，不破坏现有离线重建核心。

## 2. 顶层目录结构

```text
C:\PROJECT\HTMSR
├── CMakeLists.txt
├── CMakePresets.json
├── docs
├── references
├── scripts
├── src
│   ├── app
│   │   ├── acquisition
│   │   ├── services
│   │   ├── ui
│   │   └── main.cpp
│   └── core
└── out
```

### 2.1 `CMakeLists.txt`

项目主构建脚本，定义两个主要目标：

- `htmsr_core`：静态业务核心库。
- `htmsr_app`：Qt Widgets 桌面程序。

主要依赖：

- `OpenCV`：图像读取、图像处理、相机标定、去畸变。
- `Eigen3`：矩阵、向量、射线和三维几何计算。
- `PCL`：点云保存和点云可视化适配。
- `Qt6 Widgets / Concurrent`：桌面界面和后台任务。
- `VTK`：可选点云内嵌显示组件。

### 2.2 `docs`

项目文档目录，当前包含：

- `plan.md`：项目实施计划。
- `HTMSR_项目分析与离线重建流程.md`：函数调用、包含关系和重建流程分析。
- `HTMSR_Project_Call_Offline_Reconstruction.xmind`：XMind 流程图。
- `HTMSR_项目结构功能说明书.md`：本文档。

### 2.3 `references`

参考代码目录，来源于原始业务逻辑。当前项目不包含、不编译、不修改该目录。迁移业务逻辑时只参考其算法流程、参数意义和注释风格。

### 2.4 `scripts`

辅助脚本目录，当前用于运行程序和生成文档资产。

### 2.5 `out`

CMake/Visual Studio 构建输出目录。该目录由构建系统生成，不属于手写源码。

## 3. 构建目标说明

## 3.1 `htmsr_core`

`htmsr_core` 是项目的核心业务库，位于 `src/core`。该库不依赖 Qt UI，主要职责是完成算法和数据处理。

包含文件：

```text
src/core
├── Types.h
├── Logger.h / Logger.cpp
├── FileSystemUtils.h / FileSystemUtils.cpp
├── CalibrationService.h / CalibrationService.cpp
├── LaserExtractionService.h / LaserExtractionService.cpp
├── ReconstructionService.h / ReconstructionService.cpp
└── PointCloudService.h / PointCloudService.cpp
```

### 3.1.1 `Types.h`

核心数据结构定义文件，是核心层和 UI 层之间的数据契约中心。

主要类型：

- `LogMessage`：日志消息。
- `ImageRange`：图像索引范围。
- `CalibrationInput`：标定输入参数。
- `CalibrationResult`：标定输出结果。
- `LaserExtractionConfig`：激光中心线提取参数。
- `LaserExtractionResult`：线提取结果。
- `ReconstructionInput`：重建输入参数。
- `FrameReconstructionResult`：单帧重建结果。
- `ReconstructionResult`：批量重建结果。
- `AppProjectConfig`：软件工程配置。

### 3.1.2 `Logger`

统一日志模块。核心算法不直接使用 `cout` 或 Qt 控件输出，而是通过 `Logger` 生成日志。

主要功能：

- 支持 `Debug / Info / Warning / Error` 四级日志。
- 支持注册多个日志接收器 `Sink`。
- 当前 UI 通过 `QtLogSink` 将日志转发到日志面板。

### 3.1.3 `FileSystemUtils`

文件系统工具模块。

主要功能：

- 递归扫描目录下的图像文件。
- 支持 `.jpg / .jpeg / .png / .bmp / .tif / .tiff`。
- 按文件名排序，保证左右图像目录稳定配对。
- 支持图像索引范围裁剪。
- 保存文件前自动创建父目录。

### 3.1.4 `CalibrationService`

双目标定服务。

主要功能：

- 读取左右标定图像目录。
- 左右相机分别执行单目标定。
- 成对提取左右棋盘格角点。
- 使用 `cv::stereoCalibrate` 求解双目外参。
- 保存和读取 OpenCV YAML 标定文件。

核心输出：

- `K1 / D1`：左相机内参和畸变。
- `K2 / D2`：右相机内参和畸变。
- `R / t`：左右相机之间的旋转和平移。
- `E / F`：本质矩阵和基础矩阵。
- `P1 / P2`：投影矩阵。
- `rms`：双目标定误差。

### 3.1.5 `LaserExtractionService`

激光中心线提取服务。

主要功能：

- 支持灰度重心法。
- 支持 Steger 法。
- 支持左/右 ROI。
- 支持按红、绿、蓝、灰度通道提取激光。
- 生成中心线调试预览图。

灰度重心法逻辑：

- 按颜色通道转灰度。
- 阈值分割激光区域。
- 连通域过滤。
- 按行进行灰度加权求中心。

Steger 法逻辑：

- 阈值提取候选区域。
- 根据线宽构造高斯导数核。
- 计算 Hessian 信息。
- 沿法线方向求亚像素中心。
- 按行筛选候选点。

### 3.1.6 `ReconstructionService`

离线三维重建服务。

主要功能：

- 扫描左右重建图像目录。
- 按排序结果组成左右图像对。
- 对每一帧执行中心线提取、去畸变、匹配和三维恢复。
- 合并所有帧的点云。

单帧重建流程：

1. 左右图像分别提取激光中心线。
2. 将中心线点从 Eigen 点转换为 OpenCV 点。
3. 使用 `cv::undistortPoints` 去畸变。
4. 将像素点转换为相机射线。
5. 使用 `R/t` 建立左右相机几何关系。
6. 根据射线误差寻找左右中心线匹配点。
7. 对匹配点构造两条空间射线。
8. 求两条射线最近点中点作为三维点。
9. 将点转换回左相机坐标系。

### 3.1.7 `PointCloudService`

点云服务。

主要功能：

- 合并逐帧点云。
- 导出 `txt` 点云。
- 导出 `pcd` 点云。

当前暂未实现点云导入，后续可增加 `txt / pcd / ply / asc` 读取能力，用于查看已有测试素材。

## 4. Qt 应用层说明

`src/app` 是 Qt 桌面程序层，包含程序入口、UI、应用服务和采集预留。

```text
src/app
├── main.cpp
├── acquisition
├── services
└── ui
```

## 4.1 `main.cpp`

程序入口。

主要职责：

- 创建 `QApplication`。
- 注册 `LogMessage` 元类型。
- 创建并显示 `MainWindow`。
- 启动 Qt 事件循环。

## 4.2 `app/ui`

Qt Widgets 界面层。

```text
src/app/ui
├── MainWindow.h / MainWindow.cpp
├── ParameterPanel.h / ParameterPanel.cpp
├── LogPanel.h / LogPanel.cpp
├── ImageViewWidget.h / ImageViewWidget.cpp
└── PointCloudViewWidget.h / PointCloudViewWidget.cpp
```

### 4.2.1 `MainWindow`

主窗口，负责组织整个桌面软件界面和用户操作入口。

界面结构：

- 顶部菜单：`文件 / 扫描 / 显示 / 设置 / 帮助`。
- 顶部工具栏：保存、加载标定、标定、重建、导出 TXT、导出 PCD。
- 左侧 Dock：项目资源树。
- 中央标签页：点云、左图、右图、调试图。
- 右侧 Dock：参数监控面板。
- 底部 Dock：日志消息面板。
- 状态栏：任务状态和进度条。

主要任务入口：

- `runCalibration`：后台执行双目标定。
- `loadCalibration`：加载已有标定文件。
- `runReconstruction`：后台执行三维重建。
- `exportTxt`：导出 TXT 点云。
- `exportPcd`：导出 PCD 点云。
- `saveProjectSettings`：保存项目参数。

后台任务使用：

- `QtConcurrent::run`。
- `QFutureWatcher<CalibrationResult>`。
- `QFutureWatcher<ReconstructionResult>`。

### 4.2.2 `ParameterPanel`

右侧参数面板。

页面分区：

- `项目`：左右标定目录、左右重建目录、标定文件、输出目录。
- `标定`：棋盘宽高、方格宽高、图像起止索引。
- `重建`：算法模式、颜色通道、左右 ROI、灰度阈值、Steger 阈值、线宽、匹配距离、端点剔除。
- `采集`：在线采集预留状态。

主要职责：

- 将 UI 控件值转换为 `CalibrationInput`。
- 将 UI 控件值转换为 `ReconstructionInput`。
- 将 UI 控件值转换为 `AppProjectConfig`。
- 将已保存配置恢复到 UI。

### 4.2.3 `LogPanel`

日志显示面板。

表格列：

- 时间。
- 级别。
- 模块。
- 消息。

日志来源：

- 核心层 `Logger`。
- 应用层操作日志。

### 4.2.4 `ImageViewWidget`

图像显示控件。

主要功能：

- 接收 `cv::Mat`。
- 将 OpenCV BGR/Gray 图像转换为 Qt `QImage`。
- 在滚动区域中按比例显示图像。

用途：

- 显示左图中心线预览。
- 显示右图中心线预览。
- 显示调试图。

### 4.2.5 `PointCloudViewWidget`

点云显示控件。

工作模式：

- 如果构建环境存在 VTK Qt 组件，则内嵌 `PCLVisualizer`。
- 如果缺少 VTK Qt 组件，则显示占位视图和点云数量。

当前环境中由于未找到 VTK Qt 组件，程序会使用占位视图，不影响标定、重建和导出。

## 4.3 `app/services`

应用服务层。

```text
src/app/services
├── AppConfigService.h / AppConfigService.cpp
└── QtLogSink.h / QtLogSink.cpp
```

### 4.3.1 `AppConfigService`

配置持久化服务。

主要功能：

- 使用 `QSettings` 保存项目路径。
- 保存标定参数。
- 保存重建参数。
- 下次启动时恢复上次工作现场。

### 4.3.2 `QtLogSink`

日志桥接服务。

主要功能：

- 注册到核心层 `Logger`。
- 将核心日志转为 Qt 信号。
- 让 UI 日志面板可以在线程安全的方式显示日志。

## 4.4 `app/acquisition`

采集预留层。

```text
src/app/acquisition
├── AcquisitionTypes.h / AcquisitionTypes.cpp
└── OfflineImageSequenceProvider.h / OfflineImageSequenceProvider.cpp
```

### 4.4.1 `AcquisitionTypes`

定义后续在线采集扩展所需的基础接口和数据对象。

主要内容：

- `CameraState`：相机状态。
- `FramePair`：一组左右图像帧。
- `ICameraDevice`：相机设备接口。
- `IAcquisitionProvider`：图像帧来源接口。

### 4.4.2 `OfflineImageSequenceProvider`

离线图像序列采集源。

主要功能：

- 从左右目录读取图像。
- 按最小数量组成左右帧对。
- 通过 `hasNext / next / reset` 形式输出 `FramePair`。

当前说明：

- 该 Provider 已预留，但当前 `ReconstructionService` 仍直接从目录读取图像。
- 后续如需统一离线和在线流程，可让 `ReconstructionService` 接收 `IAcquisitionProvider` 或新增 `IFrameSource`。

## 5. 主要业务流程

## 5.1 程序启动流程

```text
main.cpp
  -> QApplication
  -> qRegisterMetaType<LogMessage>
  -> MainWindow
  -> buildCentralView / buildDocks / buildMenus / buildToolBar
  -> AppConfigService::load
  -> Logger::info("HTMSR started")
```

## 5.2 双目标定流程

```text
用户点击“标定”
  -> MainWindow::runCalibration
  -> ParameterPanel::calibrationInput
  -> QtConcurrent::run
  -> CalibrationService::calibrate
  -> listImageFiles
  -> calibrateSingleCamera 左相机
  -> calibrateSingleCamera 右相机
  -> findChessboardCornersSB
  -> cornerSubPix
  -> cv::stereoCalibrate
  -> saveCalibration
  -> MainWindow::onCalibrationFinished
```

## 5.3 离线重建流程

```text
用户点击“重建”
  -> MainWindow::runReconstruction
  -> 检查或加载 CalibrationResult
  -> ParameterPanel::reconstructionInput
  -> QtConcurrent::run
  -> ReconstructionService::reconstruct
  -> listImageFiles 左右重建图
  -> 逐帧 reconstructFrame
  -> LaserExtractionService::extract
  -> cv::undistortPoints
  -> pixelToRay
  -> 左右射线匹配
  -> closestPointBetweenLines
  -> PointCloudService::mergeFrames
  -> MainWindow::onReconstructionFinished
  -> PointCloudViewWidget::setPoints
  -> ImageViewWidget::setImage
```

## 5.4 点云导出流程

```text
用户点击 TXT / PCD
  -> MainWindow::exportTxt / exportPcd
  -> 检查 reconstruction_.mergedPoints
  -> QFileDialog 选择保存路径
  -> PointCloudService::saveTxt / savePcd
  -> Logger 记录保存结果
```

## 6. 模块依赖关系

核心层依赖关系：

```text
Types.h
  <- Logger
  <- FileSystemUtils
  <- CalibrationService
  <- LaserExtractionService
  <- PointCloudService
  <- ReconstructionService

ReconstructionService
  -> LaserExtractionService
  -> PointCloudService
  -> FileSystemUtils
  -> Logger
```

应用层依赖关系：

```text
MainWindow
  -> ParameterPanel
  -> LogPanel
  -> ImageViewWidget
  -> PointCloudViewWidget
  -> AppConfigService
  -> QtLogSink
  -> CalibrationService
  -> ReconstructionService
  -> PointCloudService

QtLogSink
  -> Logger

OfflineImageSequenceProvider
  -> FileSystemUtils
```

## 7. 当前已实现功能

- Qt Widgets 主界面。
- 顶部菜单和工具栏。
- 左侧项目资源树。
- 右侧参数面板。
- 底部日志面板。
- 参数持久化。
- 双目标定。
- OpenCV YAML 标定文件保存和读取。
- 灰度重心法中心线提取。
- Steger 法中心线提取。
- 离线左右图像批量重建。
- 中心线预览图显示。
- 合并点云。
- TXT 点云导出。
- PCD 点云导出。
- 点云视图 VTK 占位兼容。
- 在线采集接口预留。

## 8. 当前限制

- 当前点云内嵌三维显示依赖 VTK Qt 组件，未找到该组件时只显示占位视图。
- 当前重建流程直接读取左右目录，尚未统一接入 `IAcquisitionProvider`。
- 当前未实现点云导入功能，无法直接打开已有 `pcd / ply / asc / txt` 点云文件查看。
- 当前没有完整测试数据回归流程。
- 当前异步任务只有完成回调，尚未实现逐帧进度百分比。
- 当前匹配算法为基于射线误差的近似匹配，需要真实数据进一步调参验证。

## 9. 后续扩展建议

### 9.1 在线采集

建议新增：

- `CameraDeviceBasler` 或其他相机 SDK 适配类。
- `OnlineStereoAcquisitionProvider`。
- 采集参数配置页面。
- 采集状态、曝光、触发模式、保存图像等功能。

重建核心建议演进为：

```text
ReconstructionService
  -> IFrameSource / IAcquisitionProvider
  -> 离线图片
  -> 在线相机
```

这样离线重建和在线采集可以复用同一套中心线提取和三维恢复逻辑。

### 9.2 点云导入

建议新增 `PointCloudImportService`：

- 支持 `txt`。
- 支持 `pcd`。
- 支持 `ply`。
- 支持 `asc`。

这样可以直接使用 `C:\PROJECT\Cases` 中的已有点云素材测试 UI 和显示模块。

### 9.3 测试与稳定性

建议增加：

- 标定服务单元测试。
- 图像目录配对测试。
- ROI 越界测试。
- 空图像测试。
- 无中心线测试。
- 点云导出测试。
- 使用固定样例数据进行回归测试。

### 9.4 工程维护

建议保持以下规范：

- `core` 不直接 include Qt UI。
- UI 不直接写复杂算法，只负责参数收集和结果展示。
- 所有长任务继续放后台线程执行。
- 所有异常统一转为日志和用户可读提示。
- 新增算法优先通过数据结构显式传参，不使用全局变量。
