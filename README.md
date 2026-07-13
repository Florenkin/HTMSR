# HTMSR

HTMSR 是一个基于 `Qt 5.15.2 + Visual Studio + CMake + OpenCV + Eigen + PCL` 的双目标定与离线激光三维重建桌面项目。当前版本定位为离线处理工具，核心能力包括：

- 双目标定
- 激光中心线提取
- 左右图像三维重建
- 点云显示与导出
- 参数持久化与日志管理

本 README 用于统一说明项目的结构、运行方式、核心流程和后续开发约束，便于继续演进本项目。

## 1. 项目定位

项目当前的设计目标是：

- 将 `references` 中的旧业务逻辑迁移为可维护的 C++/Qt 工程
- 保持核心算法层与 Qt UI 解耦
- 先完成离线重建，再为在线采集预留扩展点

当前工程遵循以下原则：

- `references` 目录只作为参考，不参与编译，不修改其中代码
- `src/core` 保持纯 C++ 核心，不依赖 Qt UI
- `src/app` 负责界面、配置、日志桥接和采集预留
- 后续在线采集通过接口扩展，不破坏现有离线重建核心

## 2. 顶层目录

```text
C:\PROJECT\HTMSR
├── CMakeLists.txt
├── CMakePresets.json
├── docs
├── out
├── references
├── scripts
└── src
    ├── app
    │   ├── acquisition
    │   ├── services
    │   ├── ui
    │   └── main.cpp
    └── core
```

各目录职责如下：

- `src/core`
  纯业务核心，包括标定、激光中心线提取、重建、点云导出、日志和文件工具。
- `src/app/ui`
  Qt Widgets 界面层，包括主窗口、参数面板、日志面板、图像显示和点云显示控件。
- `src/app/services`
  Qt 应用服务层，包括 `QSettings` 配置持久化和日志桥接。
- `src/app/acquisition`
  采集预留层，定义相机设备和帧来源接口，当前只提供离线图片序列 Provider。
- `references`
  原始参考实现与 OpenCorr 资料，不参与编译。
- `docs`
  项目分析文档、脑图和开发说明。
- `scripts`
  辅助脚本。
- `out`
  构建输出目录。

## 3. 构建方式

项目采用 `CMake` 构建，推荐直接在 Visual Studio 2022 中打开项目目录。

可用预设：

- `vs2022-x64-debug`
- `vs2022-x64-release`

主要第三方依赖：

- `Qt5 Widgets / Concurrent`
- `OpenCV`
- `Eigen3`
- `PCL`
- `VTK` Qt 组件

说明：

- 如果未找到 VTK Qt 组件，程序仍可构建，但点云视图会退化为占位显示。
- `CMakePresets.json` 中已经给出了当前机器的典型依赖路径写法，可按本机环境调整。

## 4. 工程分层

### 4.1 `htmsr_core`

核心静态库，主要由以下模块组成：

- `Types.h`
  定义核心数据契约，包括：
  - `CalibrationInput`
  - `CalibrationResult`
  - `LaserExtractionConfig`
  - `ReconstructionInput`
  - `FrameReconstructionResult`
  - `ReconstructionResult`
  - `AppProjectConfig`
- `CalibrationService`
  双目标定、YAML 标定文件读写。
- `LaserExtractionService`
  激光中心线提取，支持灰度重心法和 Steger 法。
- `ReconstructionService`
  离线批量重建核心。
- `PointCloudService`
  点云合并、TXT/PCD 导出。
- `FileSystemUtils`
  图像扫描、排序、范围裁剪、目录创建。
- `Logger`
  统一日志入口。

### 4.2 `htmsr_app`

Qt 桌面程序，包含：

- `MainWindow`
  组织菜单、工具栏、Dock 区、任务入口和结果显示。
- `ParameterPanel`
  收集项目路径、标定参数、重建参数和采集占位参数。
- `LogPanel`
  显示日志表。
- `ImageViewWidget`
  显示左图、右图、调试图。
- `PointCloudViewWidget`
  显示点云或占位信息。

### 4.3 `app/services`

- `AppConfigService`
  使用 `QSettings` 做参数持久化。
- `QtLogSink`
  将核心层 `Logger` 输出桥接到 Qt signal。

### 4.4 `app/acquisition`

- `ICameraDevice`
  在线相机设备接口。
- `IAcquisitionProvider`
  帧来源接口。
- `FramePair`
  左右帧数据对象。
- `OfflineImageSequenceProvider`
  离线图片序列实现。

当前该层已预留，但离线重建还没有真正通过 `IAcquisitionProvider` 统一接入。

## 5. 核心业务流程

### 5.1 程序启动

```text
main.cpp
  -> QApplication
  -> qRegisterMetaType<LogMessage>
  -> MainWindow
  -> buildCentralView / buildDocks / buildMenus / buildToolBar
  -> AppConfigService::load
  -> Logger::info("HTMSR started")
```

### 5.2 双目标定流程

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

### 5.3 离线重建流程

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

## 6. 激光中心线提取逻辑

`LaserExtractionService` 是当前重建链路中的关键模块。

### 6.1 公共入口

- 检查空图
- 将用户输入 ROI 裁剪到图像有效范围
- 根据模式选择：
  - `GrayCentroid`
  - `Steger`

### 6.2 灰度重心法

主要步骤：

- 按颜色通道取灰度
- 阈值分割
- 形态学开运算去噪
- 连通区域筛选
- 在每一行内按灰度加权求中心
- 生成中心线调试预览图

### 6.3 Steger 法

主要步骤：

- 提取候选亮区域
- 依据条纹宽度构造高斯导数核
- 计算一阶/二阶导数
- 用 Hessian 信息计算亚像素中心
- 生成中心线调试预览图

## 7. 当前输入输出约定

### 7.1 标定输入

- 左标定图像目录
- 右标定图像目录
- 棋盘格内角点数 `boardSize`
- 方格物理尺寸 `squareSize`
- 图像索引范围 `ImageRange`
- 输出标定文件路径

### 7.2 重建输入

- 左重建图像目录
- 右重建图像目录
- 图像索引范围
- 标定结果文件
- ROI、阈值、颜色通道、算法模式等重建参数

### 7.3 输出

- `stereo_calibration.yml`
- 批量重建结果点云
- `txt` 点云
- `pcd` 点云
- 左右中心线预览图

## 8. 当前功能边界

当前已实现：

- Qt Widgets 主界面
- 双目标定
- OpenCV YAML 标定文件读写
- 灰度重心法中心线提取
- Steger 法中心线提取
- 左右图像离线批量重建
- 点云合并
- TXT / PCD 导出
- 参数持久化
- 日志展示
- 在线采集接口预留

当前限制：

- 点云内嵌三维显示依赖 VTK Qt 组件，缺失时只显示占位视图
- 重建流程当前仍直接扫描目录，不是通过 `IAcquisitionProvider`
- 未实现已有点云文件导入查看
- 异步任务目前只有完成回调，没有逐帧进度回调
- 左右点匹配目前是基于射线误差的近似匹配，仍需真实数据持续调参

## 9. 与 `references` 的关系

`references` 目录中包含两类资料：

- `references/src`
  旧版激光三维重建实现，是当前项目算法迁移的直接参考。
- `references/OpenCorr-GUI_3.0`
  OpenCorr 资料，主要可借鉴其架构分层、双目匹配分层和 ROI/POI 组织思路。

当前项目与 OpenCorr 的关系是：

- 可以借鉴其“分层”和“流程设计”
- 不直接照搬其面向 DIC 的相关匹配算法

## 10. 后续开发建议

### 10.1 在线采集接入

建议下一步扩展：

- 新增具体相机 SDK 适配类，如 `CameraDeviceBasler`
- 实现 `OnlineStereoAcquisitionProvider`
- 将 `ReconstructionService` 改造成支持统一帧源接口

目标结构：

```text
ReconstructionService
  -> IFrameSource / IAcquisitionProvider
  -> 离线图片
  -> 在线相机
```

### 10.2 点云导入

建议新增 `PointCloudImportService`，支持：

- `txt`
- `pcd`
- `ply`
- `asc`

便于直接查看已有历史点云或测试素材。

### 10.3 测试与回归

建议逐步增加：

- 标定服务测试
- 图像配对测试
- ROI 越界测试
- 空图像测试
- 无中心线测试
- 点云导出测试
- 固定样例回归测试

### 10.4 工程维护规范

建议保持：

- `core` 不直接 include Qt UI
- UI 只负责参数收集和结果展示，不承载复杂算法
- 所有长任务继续放后台线程执行
- 所有异常统一转为日志和用户可读提示
- 新增算法优先通过数据结构显式传参，避免全局变量

## 11. 推荐阅读顺序

如果后续开发者需要快速熟悉工程，建议按以下顺序阅读：

1. `src/core/Types.h`
2. `src/core/CalibrationService.*`
3. `src/core/LaserExtractionService.*`
4. `src/core/ReconstructionService.*`
5. `src/core/PointCloudService.*`
6. `src/app/ui/MainWindow.*`
7. `src/app/ui/ParameterPanel.*`
8. `src/app/services/AppConfigService.*`
9. `docs/HTMSR_项目分析与离线重建流程.md`
10. `docs/双目激光三维重建.xmind`

## 12. 文档来源

本 README 由以下文档整理合并而成：

- [plan.md](/C:/PROJECT/HTMSR/docs/plan.md)
- [HTMSR_项目结构功能说明书.md](/C:/PROJECT/HTMSR/docs/HTMSR_项目结构功能说明书.md)
- [HTMSR_项目分析与离线重建流程.md](/C:/PROJECT/HTMSR/docs/HTMSR_项目分析与离线重建流程.md)

后续如项目结构、流程或输入输出约定发生变化，应优先同步更新本 README，再决定是否拆分回专题文档。
