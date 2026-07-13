# UWScan2 项目借鉴分析与 HTMSR 落地计划

## 1. 分析范围

本次阅读对象主要为 `C:\PROJECT\UWScan2\UWScan2` 下的工程源码与文档，重点关注其自有业务代码，而不是外层 `Program Files` 或打包进去的第三方库目录。

重点阅读内容包括：

- `C:\PROJECT\UWScan2\UWScan2\CMakeLists.txt`
- `C:\PROJECT\UWScan2\UWScan2\src\CMakeLists.txt`
- `C:\PROJECT\UWScan2\UWScan2\src\mainwindow`
- `C:\PROJECT\UWScan2\UWScan2\src\scanner`
- `C:\PROJECT\UWScan2\UWScan2\src\pointcloud`
- `C:\PROJECT\UWScan2\UWScan2\src\setting`
- `C:\PROJECT\UWScan2\UWScan2\src\server`
- `C:\PROJECT\UWScan2\UWScan2\include`
- `C:\PROJECT\UWScan2\UWScan2\docs\api.md`

整体判断：`UWScan2` 是一个比当前 HTMSR 更完整的扫描仪桌面软件，已经覆盖在线设备控制、扫描流程、点云显示、点云交互、远程控制、配置保存和运行时部署。它有很多工程经验值得吸收，但不能直接照搬，因为它存在主窗口过重、硬编码路径较多、全局依赖传播较强、算法和 Qt/硬件耦合偏深等问题。

## 2. UWScan2 总体结构观察

### 2.1 工程结构

`UWScan2` 的主要结构如下：

```text
C:\PROJECT\UWScan2\UWScan2
|-- CMakeLists.txt
|-- cmake
|-- docs
|-- include
|   |-- mainwindow
|   |-- pointcloud
|   |-- scanner
|   |-- server
|   `-- setting
|-- resources
|   `-- ui
|-- src
|   |-- mainwindow
|   |-- pointcloud
|   |-- scanner
|   |-- server
|   `-- setting
`-- python
    `-- uwscan3d
```

它的模块边界大致是：

- `mainwindow`：主窗口、Ribbon 菜单、状态栏、主交互入口。
- `scanner`：相机、电机、扫描控制、重建队列、中心线提取和扫描结果保存。
- `pointcloud`：点云模型、点云树、点云窗口、选择工具、过滤和测量入口。
- `setting`：基于 `QSettings` 的配置保存。
- `server`：TCP/UDP 远程控制协议。
- `resources/ui`：Qt Designer `.ui` 页面。
- `python/uwscan3d`：算法 Python 封装与验证脚本。

### 2.2 和 HTMSR 当前结构的关系

HTMSR 当前已经建立了更清晰的三层结构：

```text
C:\PROJECT\HTMSR\src
|-- core
|-- app
|   |-- acquisition
|   |-- services
|   `-- ui
```

因此，UWScan2 的内容不应该整体迁移，而应该按职责拆解后吸收：

- UI 交互经验进入 `src/app/ui`。
- 配置、任务编排和日志桥接经验进入 `src/app/services`。
- 在线采集经验进入 `src/app/acquisition`。
- 点云数据处理和导出能力进入 `src/core` 或新的结果服务。
- 远程控制能力作为中后期扩展，不进入首轮离线闭环。

## 3. 构建与部署方面可借鉴内容

### 3.1 可借鉴点

- 使用 `CMAKE_AUTOMOC`、`CMAKE_AUTORCC`、`CMAKE_AUTOUIC` 简化 Qt 工程构建。
- 在构建后调用 `windeployqt`，自动复制 Qt 运行时 DLL 和插件。
- 将项目运行时 DLL 通过 `POST_BUILD` 复制到输出目录。
- 针对 PCL/Boost 的异常宏做 CMake 层清理。
- 对 MSVC 增加 `/utf-8`、`/EHsc`、`/bigobj` 等适配选项。

### 3.2 不建议照搬点

- 不建议硬编码 `Qt5_DIR`、`PCL_DIR`、`BASLER_PYLON_ROOT` 这类本机绝对路径。
- 不建议使用大量全局 `include_directories` 和 `link_directories`。
- 不建议把第三方依赖全部作为 `ALL_LIBS` 无差别传给最终可执行文件。
- 不建议把运行时 DLL 固定写死在源码 CMake 中，后续机器路径变化时维护成本高。

### 3.3 HTMSR 落地建议

- 保留当前 HTMSR 的目标级 CMake 风格，继续使用 `target_include_directories`、`target_link_libraries`、`target_compile_definitions`。
- 新增一个可选的部署脚本或 CMake 安装步骤，用于复制 Qt/OpenCV/PCL/VTK 运行时 DLL。
- 将运行时依赖路径配置放到 `CMakePresets.json`、本机环境变量或单独的 `deploy` 脚本中，不写死到核心 CMake。
- 保持现有 PCL/Boost 宏清理逻辑，并把 UWScan2 中“发现并清除坏宏”的经验固化到 `docs/standard.md`。

## 4. UI 与主窗口方面可借鉴内容

### 4.1 可借鉴点

- `UWScanMainWindow` 使用类似工业软件的主窗口组织方式：顶部功能区、左侧点云树、中间点云/图像视图、底部状态与日志。
- `ScannerInterface` 将扫描参数、曝光、ROI、中心线显示选项集中到一个操作面板中。
- `AdvancedStatusBar` 将参数监控、日志表、状态消息和进度条放在底部区域，和用户给出的参考图片风格接近。
- 中央区域不仅显示点云，还显示图像、中心线、深度图、合成图等多类结果。
- 点云树支持右键菜单、显示隐藏、聚焦、删除、合并、颜色设置等操作。

### 4.2 不建议照搬点

- `UWScanMainWindow` 职责过重，包含大量点云处理、图像预览、拟合、扫描控制和服务端逻辑。
- 主窗口头文件成员变量非常多，后续维护压力大。
- 部分算法预览逻辑直接写在窗口 `.cpp` 中，不利于复用和测试。
- 使用 `.ui` 文件构建界面，而 HTMSR 当前约定是用 C++ 代码构建 UI。

### 4.3 HTMSR 落地建议

- 保持 HTMSR 当前 `QMainWindow + Dock + Tab` 结构，不强制引入 SARibbon。
- 增加一个 `StatusMonitorPanel`，借鉴 `AdvancedStatusBar` 的参数监控布局。
- 将底部 `LogPanel` 升级为“日志 + 进度 + 状态消息”的复合状态区。
- 将当前左侧项目树升级为“数据树/结果树”，显示标定数据、重建数据、点云结果、调试图。
- 将 `MainWindow` 继续减负，避免新增大量业务逻辑；复杂交互放到独立 widget 或 service。

## 5. 在线采集预留方面可借鉴内容

### 5.1 可借鉴点

- `ScannerInterface` 不直接控制硬件，而是通过 Qt 信号发出操作意图，例如开始扫描、停止扫描、修改曝光、移动电机、打开激光。
- `Scanner` 统一封装相机、电机、激光、扫描状态和扫描帧生成。
- `ReconstructionFrame` 把单帧图像、扫描索引、激光位置、会话 ID、保存路径等信息打包在一起。
- `Camera` 对 Basler Pylon SDK 做了封装，包含初始化、关闭、单帧采集、连续采集、曝光和增益设置。
- 扫描过程中通过 `ScannerInfo` 持续发送角度、温度、湿度、压力、曝光等状态。

### 5.2 不建议照搬点

- `Scanner` 同时持有相机、电机、重建器和配置读取逻辑，职责偏多。
- `Camera` 直接绑定 Pylon SDK，不适合作为通用相机接口。
- 硬件状态、UI 配置和扫描保存逻辑耦合较强。
- 部分调试输出仍使用 `std::cout` 和 `qDebug`。

### 5.3 HTMSR 落地建议

- 在 `src/app/acquisition` 中继续完善现有 `ICameraDevice`、`IAcquisitionProvider`、`FramePair`。
- 新增 `IOnlineAcquisitionProvider`，用于未来在线左右相机同步采集。
- 新增 `AcquisitionSession`，记录一次采集或重建任务的 `sessionId`、起止时间、保存目录、帧数和状态。
- 新增 `DeviceStatus`，包含相机连接状态、采集状态、曝光、增益、温度等信息。
- 未来接 Basler、海康或其他相机 SDK 时，只新增具体实现类，不改 `core` 标定和重建服务。

## 6. 离线重建流程方面可借鉴内容

### 6.1 可借鉴点

- `Reconstructor` 内部维护重建配置、标定数据缓存和待处理帧队列。
- 每一帧处理时先检查初始化状态、图像有效性、扫描索引范围、ROI 边界。
- 中心线提取支持灰度重心、Steger、多阈值等模式。
- 中心线结果不足时会尝试 fallback 阈值，提升弱激光或曝光不稳定时的容错性。
- 重建输出不仅有点云，还保留中心线点、关联点、灰度值、调试图所需信息。
- 对单帧失败会记录原因，但批量任务可以继续处理。

### 6.2 不建议照搬点

- UWScan2 的重建模型是单相机加扫描运动/曲面标定，不等同于 HTMSR 的双目重建。
- `Reconstructor` 依赖 Qt、PCL、OpenCV、Eigen 和内部扫描数据结构，耦合偏重。
- 工作线程使用裸 `std::thread + QMutex + QWaitCondition`，后续取消和生命周期管理复杂。
- 标定 YAML 字段和 HTMSR 双目标定 YAML 不一致，不能直接复用。

### 6.3 HTMSR 落地建议

- 在 `src/core` 中新增或扩展 `ReconstructionSession` 和 `FrameReconstructionResult`，明确每帧输入、输出、失败原因和统计信息。
- 让 `ReconstructionService` 从“直接目录读取”逐步升级为“接收 `IFrameSource` 或 `IAcquisitionProvider`”，使离线和在线共用同一条重建链。
- 在 `LaserExtractionService` 中增加 fallback 策略开关，例如弱光时自动降低阈值重试。
- 在 `ReconstructionResult` 中保留更多调试信息：每帧中心线点数、匹配点数、失败原因、预览图路径。
- 对 ROI 越界、图像为空、中心线为空、匹配为空等情况统一返回结构化失败信息，不让任务直接崩溃。

## 7. 点云显示与结果管理方面可借鉴内容

### 7.1 可借鉴点

- `CloudWindow` 使用 `QVTKOpenGLNativeWidget + PCLVisualizer` 实现内嵌点云视图。
- 点云视图支持渐变背景、坐标系、正交投影、点数显示、坐标显示。
- `SelectionOverlay` 在 VTK 视图上叠加 Qt 透明层，实现矩形选择、套索选择、多边形选择。
- `CloudTreeWidget` 负责点云列表、右键菜单、显示隐藏、删除、合并、聚焦、修改颜色。
- 点云结果可绑定额外信息，例如误差图例、拟合结果、分组关系。
- `AssociatedPointsManager` 支持保存深度图、强度图、合成图、纹理 PLY。

### 7.2 不建议照搬点

- 点云窗口功能非常多，直接迁移会显著增加 HTMSR 首版复杂度。
- 点云选择、拟合、缺陷检测等功能超出当前离线双目重建首版范围。
- `AssociatedPointsManager` 使用单例和后台裸线程，不适合直接作为 HTMSR 的结果管理实现。

### 7.3 HTMSR 落地建议

- 第一阶段：让 `PointCloudViewWidget` 真正接入 VTK Qt 组件，完成基本旋转、缩放、重置视角。
- 第二阶段：新增 `ResultTreeWidget`，管理多个点云结果、调试图和导出文件。
- 第三阶段：增加点云右键菜单，支持显示隐藏、删除、重命名、打开输出目录、导出。
- 第四阶段：增加坐标提示、点数显示、颜色设置和误差/深度图例。
- 第五阶段：再考虑框选、套索、滤波、拟合和缺陷检测。

## 8. 配置与参数管理方面可借鉴内容

### 8.1 可借鉴点

- `SettingsManager` 统一封装 `QSettings`。
- 配置 key 使用常量集中定义，避免字符串到处散落。
- 支持默认曝光、增益、扫描角度、阈值、标定文件、保存路径等持久化。
- 修改配置时发出 `settingChanged` 信号。

### 8.2 不建议照搬点

- 所有 key 都放在一个头文件中，后续规模变大后会比较拥挤。
- 单例模式让依赖关系变隐式，不利于测试和替换。
- UI 和扫描逻辑直接读取配置，容易绕过参数对象。

### 8.3 HTMSR 落地建议

- 保留当前 `AppConfigService`，不直接引入全局单例。
- 新增 `AppSettingKeys.h`，集中管理配置 key。
- 每类参数按命名空间分组，例如 `Project`、`Calibration`、`Reconstruction`、`Acquisition`、`View`。
- UI 只通过 `AppConfigService` 加载/保存配置，不直接到处调用 `QSettings`。
- `Types.h` 中的默认参数仍作为算法默认值来源，配置服务只负责持久化。

## 9. 日志、状态与用户反馈方面可借鉴内容

### 9.1 可借鉴点

- `MessageStation` 使用表格显示消息，包含类型、消息、时间。
- `AdvancedStatusBar` 将参数监控和日志整合在底部区域。
- 扫描状态、进度、错误、警告都通过信号传递到 UI。
- 日志表自动滚动到底部，方便用户看到最新状态。

### 9.2 不建议照搬点

- UWScan2 中仍有不少 `std::cout` 和 `qDebug`。
- 日志类型和模块名没有形成完全统一的规范。
- 消息表和状态栏有两套相似实现，存在重复。

### 9.3 HTMSR 落地建议

- 继续以 `Logger` 作为唯一日志入口。
- 扩展 `LogPanel`，支持清空、复制、保存日志、按级别过滤。
- 新增状态监控区，显示当前点数、当前任务、当前标定文件、输出目录、耗时。
- 长任务统一输出开始、进度、失败帧、完成摘要。
- 禁止在新代码中直接使用 `cout`、`printf`、`qDebug` 作为业务日志。

## 10. 远程控制接口方面可借鉴内容

### 10.1 可借鉴点

- `RemoteCommandServer` 使用 TCP 接收控制命令。
- 使用 UDP 推送状态、图像和点云。
- 协议中有 magic、version、req_id、data_length，能够处理粘包和请求响应。
- 通过订阅掩码控制是否推送状态、图像、点云，避免无意义的大流量发送。
- `docs\api.md` 记录了命令结构和数据格式。

### 10.2 不建议立即实现点

- HTMSR 首版定位仍是离线工具，不应该过早引入网络控制复杂度。
- UDP 图像和点云推送涉及分片、丢包、带宽和线程安全，需要单独设计。
- 当前远程协议与 HTMSR 双目离线重建不完全匹配。

### 10.3 HTMSR 落地建议

- 先在 `docs` 中预留 `remote_api_plan.md` 或后续协议章节。
- 等在线采集模块稳定后，再实现 `RemoteControlService`。
- 命令层只调用应用服务，不直接操作 UI 控件或 core 算法。
- 首批远程命令可考虑：加载项目、开始离线重建、查询任务状态、导出点云、打开/关闭在线采集设备。

## 11. 文档与测试资料方面可借鉴内容

### 11.1 可借鉴点

- `README.md` 说明了 Qt 主程序、DLL、Python 包和构建入口。
- `docs\api.md` 说明了通信协议。
- `git规范.md` 可作为团队协作习惯参考。
- Python 包中有算法验证逻辑，可作为未来跨语言测试思路参考。

### 11.2 HTMSR 落地建议

- 为 HTMSR 补充 `docs\用户操作说明.md`，说明从导入数据到导出点云的完整流程。
- 为 HTMSR 补充 `docs\参数说明.md`，解释棋盘格、阈值、ROI、匹配距离等参数。
- 为 HTMSR 补充 `docs\在线采集扩展计划.md`，明确后续接相机 SDK 的接口和边界。
- 当前 `plan.md` 可作为“UWScan2 借鉴计划”的入口文档。

## 12. 建议优先落地清单

### 12.1 第一阶段：短期可做，收益明显

- 完善 `PointCloudViewWidget` 的 VTK Qt 内嵌显示，优先解决当前占位视图问题。
- 增强 `LogPanel`，加入清空、保存、过滤和任务摘要。
- 新增底部参数监控区，显示当前点数、重建进度、输出目录和标定文件。
- 将图像读取和左右帧配对统一为 `FrameSource` 思路，为在线采集铺路。
- 在重建结果中增加逐帧失败原因、中心线点数、匹配点数和调试图信息。

### 12.2 第二阶段：中期增强，提升工程可用性

- 新增 `ResultTreeWidget`，用于管理点云、调试图、导出文件。
- 点云结果支持导入 `txt / pcd / ply / asc`，用于查看外部测试数据。
- 增加深度图、强度图、中心线叠加图导出。
- 增加 `ReconstructionSession`，统一管理一次离线重建的 sessionId、输出目录、帧统计和导出结果。
- 增加弱激光 fallback 策略，中心线提取失败时可自动放宽阈值重试。

### 12.3 第三阶段：后续在线采集准备

- 新增 `OnlineAcquisitionProvider` 接口，扩展现有 `IAcquisitionProvider`。
- 新增 `DeviceStatus`、`AcquisitionSession`、`CameraParameter` 等数据对象。
- 为 Basler/Pylon 或其他相机 SDK 准备独立 adapter，不让 SDK 头文件污染 core。
- 将 UI 采集页从占位页升级为设备连接、曝光、增益、预览、采集状态页面。
- 在线采集产生的 `FramePair` 进入与离线相同的重建服务。

### 12.4 第四阶段：远程控制与高级点云交互

- 新增远程控制协议文档。
- 实现 `RemoteControlService`，支持任务启动、状态查询和结果导出。
- 点云视图增加显示隐藏、颜色设置、聚焦、坐标读取。
- 视需求增加框选、滤波、拟合、误差图例等高级点云工具。

## 13. 需要避免的问题

- 避免把 `UWScanMainWindow` 那种“大而全窗口类”模式复制到 HTMSR。
- 避免在 UI 层直接写算法、点云处理或文件保存主逻辑。
- 避免把硬件 SDK、PCL/VTK 复杂头文件无节制暴露到公共头文件。
- 避免新增全局单例作为默认解法，除非确实是全局基础设施。
- 避免直接复制 UWScan2 的 CMake 硬编码路径。
- 避免继续使用 `cout/qDebug` 作为业务日志。
- 避免在首版离线工具中过早加入 TCP/UDP 远程控制，造成范围失控。

## 14. 推荐的 HTMSR 目标结构演进

建议后续 HTMSR 向以下结构演进：

```text
C:\PROJECT\HTMSR\src
|-- core
|   |-- CalibrationService
|   |-- LaserExtractionService
|   |-- ReconstructionService
|   |-- PointCloudService
|   |-- ResultExportService
|   `-- Types
|-- app
|   |-- acquisition
|   |   |-- ICameraDevice
|   |   |-- IAcquisitionProvider
|   |   |-- OfflineImageSequenceProvider
|   |   `-- OnlineAcquisitionProvider
|   |-- services
|   |   |-- AppConfigService
|   |   |-- TaskRunnerService
|   |   |-- ReconstructionSessionService
|   |   `-- QtLogSink
|   `-- ui
|       |-- MainWindow
|       |-- ParameterPanel
|       |-- LogPanel
|       |-- StatusMonitorPanel
|       |-- ResultTreeWidget
|       |-- ImageViewWidget
|       `-- PointCloudViewWidget
```

## 15. 总结

`UWScan2` 最值得 HTMSR 借鉴的不是某一段具体代码，而是完整工业扫描软件的功能组织经验：采集控制、扫描会话、重建任务、结果管理、点云交互、配置保存、状态监控和远程接口。

HTMSR 当前的优势是分层更清晰，核心算法与 UI 解耦已经打好了基础。后续应优先吸收 UWScan2 的成熟交互和结果管理思路，但保持 HTMSR 自己的低耦合架构，不让主窗口、硬件 SDK 或第三方依赖反向污染核心业务层。
