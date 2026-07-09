# HTMSR Qt/VS 实施计划

## Summary
新建一套独立 Qt6 + Visual Studio + CMake 工程，不修改 `C:\PROJECT\HTMSR\references`。`references/src` 只作为算法行为参考，新代码重新组织为低耦合业务模块、Qt 桌面界面和可扩展采集接口。

首版实现离线双目标定、激光中心线提取、点云重建、点云内嵌显示、日志与参数管理；后续在线采集通过预留的设备抽象层接入。

## Implementation
- 工程采用 `CMake`，Visual Studio 直接打开文件夹构建，默认 `MSVC x64 + Qt6 + OpenCV + Eigen3 + PCL + VTK`。
- 新增核心目标 `htmsr_core`，只依赖 OpenCV/Eigen/PCL，不依赖 Qt UI。
- 新增桌面目标 `htmsr_app`，使用 Qt Widgets 代码构建界面，依赖 `htmsr_core`。
- 不把 `references` 加入编译目标，不 include `references` 头文件，不改动 `references` 文件。
- 根目录新增 VS 友好的 `CMakePresets.json`，配置 `Debug/Release x64`。

## Architecture
- `core`：纯业务逻辑，包含标定、线提取、重建、点云读写、参数模型、错误模型。
- `app/services`：Qt 应用服务层，负责任务调度、进度回调、日志转发、配置持久化。
- `app/ui`：Qt Widgets 界面层，负责主窗口、工具栏、Dock 区域、参数面板、点云视图。
- `app/acquisition`：在线采集预留层，先提供接口和占位页，不接真实相机 SDK。
- 日志统一走 `Logger`，同时输出到 UI 日志表和本地日志文件，算法层不直接 `cout`。

## Core Modules
- `CalibrationService`
  - 输入 `CalibrationInput`，输出 `CalibrationResult`。
  - 支持左右单目标定、双目标定、角点检测统计、RMS、OpenCV YAML 标定文件读写。
  - 保持旧 `stereo_calibration.yml` 格式兼容。
- `LaserExtractionService`
  - 输入 `LaserExtractionConfig`。
  - 实现灰度重心法和 Steger 法。
  - 返回中心线点集和调试预览图，不使用 `imshow/waitKey`。
- `ReconstructionService`
  - 输入 `ReconstructionInput`，输出 `ReconstructionResult`。
  - 批量读取左右图像、去畸变、匹配中心线点、恢复三维点。
  - 左右图像数量不一致时按最小数量处理并写日志。
- `PointCloudService`
  - 合并逐帧点云。
  - 导出 `txt` 和 `pcd`。
  - 为 UI 提供 PCL/VTK 点云数据适配。
- `Acquisition`
  - 定义 `ICameraDevice`、`IAcquisitionProvider`、`FramePair`。
  - 首版提供 `OfflineImageSequenceProvider` 和空的在线设备占位实现。

## UI
- 主窗口使用 `QMainWindow`，风格参考给定图片：顶部菜单和工具栏、左侧数据树、中间点云/图像主视图、右侧参数面板、底部日志区。
- 顶部菜单固定为 `文件`、`扫描`、`显示`、`设置`、`帮助`。
- 左侧 Dock 显示项目树：标定数据、重建数据、结果点云。
- 中央区域使用多标签页：点云视图、左图预览、右图预览、中心线调试图。
- 点云视图使用 `PCLVisualizer + VTK Qt 控件` 内嵌到主界面。
- 右侧参数面板按功能切换：项目配置、标定参数、重建参数、采集占位。
- 底部日志表包含时间、级别、模块、消息，并显示任务进度条。
- 所有长任务使用 `QThread` 或 `QtConcurrent + QFutureWatcher`，界面不阻塞。

## Data Contracts
- `CalibrationInput`：左右标定目录、棋盘格内角点尺寸、方格物理尺寸、图像范围、输出 YAML 路径。
- `CalibrationResult`：相机内参、畸变、外参、`E/F/P1/P2`、RMS、成功帧、失败帧、每帧误差。
- `LaserExtractionConfig`：算法模式、左右 ROI、阈值、最小灰度、激光颜色、线宽、筛选阈值、端点剔除。
- `ReconstructionInput`：左右重建目录、图像范围、标定结果、线提取参数、匹配距离阈值。
- `ReconstructionResult`：逐帧点云、合并点云、逐帧统计、调试图、导出文件路径。
- `AppProjectConfig`：最近路径、默认输出目录、最近标定文件、UI 状态、默认算法参数。

## Test Plan
- 构建验证：Visual Studio 打开 CMake 工程，`Debug/Release x64` 均可配置和编译。
- 标定验证：正确数据可生成 YAML，错误棋盘参数或无角点时不崩溃并输出清晰日志。
- 兼容验证：旧 `stereo_calibration.yml` 可读取并用于重建。
- 重建验证：左右图像数量不一致、空图、ROI 越界、无中心线时均有日志和可恢复状态。
- 点云验证：可导出 `txt/pcd`，内嵌点云视图可加载、旋转、缩放。
- UI 验证：长任务执行时主界面可操作，进度和日志持续刷新。
- 架构验证：新增在线采集实现时只需实现采集接口，不改动标定和重建核心服务。

## Assumptions
- Qt 基线锁定为 `Qt6 + CMake`。
- Visual Studio 使用 CMake 打开文件夹，不手写 `.sln/.vcxproj`。
- UI 使用 C++ 代码构建，不使用 Qt Designer `.ui`。
- 点云首版采用 `PCLVisualizer + VTK Qt 控件` 内嵌。
- 在线采集首版只做接口、状态模型和占位页面。
- 第三方库路径由用户本机环境、`CMAKE_PREFIX_PATH` 或 VS CMake 配置提供。
