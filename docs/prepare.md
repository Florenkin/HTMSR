# HTMSR 准备与测试操作文档

## 1. 文档目的

本文档用于指导在当前电脑或另一台电脑上准备、构建、运行和测试 HTMSR。内容覆盖：

- 从 GitHub 获取项目
- 配置第三方依赖
- 编译 Debug / Release
- 打包可运行程序
- 准备离线标定和重建素材
- 测试海康相机在线采集
- 测试振镜/激光控制器联动流程

## 2. 两种测试方式

### 2.1 开发测试

适用于需要改代码、调试、重新编译的场景。需要准备完整开发环境：

- Visual Studio 2022
- CMake
- Ninja
- Git
- Qt 5.15.2
- OpenCV 4.5.0
- Eigen
- PCL 1.12.1
- VTK 9.1 Qt 支持库，或 PCL 自带 VTK
- 海康 MVS SDK

### 2.2 运行测试

适用于只验证软件能否运行、离线流程能否跑通、点云能否显示或导出的场景。此时可以直接使用打包目录，不一定需要安装完整开发环境。

如果要连接真实海康相机，运行测试机仍然需要安装 MVS 运行时和相机驱动。

## 3. 获取项目

推荐目录：

```text
C:/PROJECT/HTMSR
```

克隆仓库：

```powershell
git clone https://github.com/Florenkin/HTMSR.git C:\PROJECT\HTMSR
```

如果网络不稳定，可以先在浏览器确认 GitHub 能正常访问，再用 Visual Studio 或命令行执行拉取。

## 4. 准备依赖

建议依赖目录保持如下结构：

```text
C:/ENVIORNMENT/qt/5.15.2/msvc2019_64
C:/ENVIORNMENT/opencv_450_vs2019
C:/ENVIORNMENT/PCL/PCL 1.12.1
C:/ENVIORNMENT/ceresLib/Eigen
C:/ENVIORNMENT/MVS
C:/Program Files (x86)/Common Files/MVS/Runtime/Win64_x64
```

注意：项目当前使用的目录拼写是 `ENVIORNMENT`，保持一致可以减少配置修改。

## 5. 生成本机配置

首次迁移到新电脑时，建议运行：

```powershell
cd C:\PROJECT\HTMSR
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Configure-HtmsrEnvironment.ps1 -CreateDefault
```

然后打开并检查：

```text
C:/PROJECT/HTMSR/scripts/htmsr_environment.local.json
```

确认依赖路径正确后执行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Configure-HtmsrEnvironment.ps1 -Configure
```

脚本会生成：

```text
C:/PROJECT/HTMSR/CMakeUserPresets.json
```

后续本机配置优先使用 `local-*` 预设，不需要直接改仓库内的 `CMakePresets.json`。

## 6. 编译项目

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

使用本机 local 预设时：

```powershell
cmake --preset local-vs2022-x64-debug
cmake --build --preset local-debug

cmake --preset local-vs2022-x64-release
cmake --build --preset local-release
```

## 7. 运行程序

Debug 输出目录通常为：

```text
C:/PROJECT/HTMSR/out/build/vs2022-x64-debug/src/app/Debug
```

Release 输出目录通常为：

```text
C:/PROJECT/HTMSR/out/build/vs2022-x64-release/src/app/Release
```

如果通过 Visual Studio 运行，确认顶部配置选择的是当前刚构建过的配置。例如 Release 运行前要先完成 Release 构建，否则可能打开旧版本程序。

## 8. 打包发布目录

运行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Package-HtmsrRelease.ps1 -Configuration Release -Clean
```

打包目录会包含可执行文件和必要运行时依赖。复制到另一台电脑时，优先复制整个打包目录，不要只复制单个 `htmsr_app.exe`。

## 9. 离线标定测试

### 9.1 输入素材

标定图像应放在左右目录中，并且文件名顺序能够一一配对：

```text
test/01_calibration/.../left
test/01_calibration/.../right
```

棋盘格参数需要与图片真实棋盘一致：

- 棋盘宽：内角点列数
- 棋盘高：内角点行数
- 方格宽/高：实际物理尺寸，单位由项目参数决定，通常按 mm 记录

### 9.2 操作流程

1. 在项目参数页选择左标定目录和右标定目录。
2. 设置正确的棋盘格参数。
3. 点击标定按钮。
4. 查看日志中的有效图像数量、失败图像数量和 RMS。
5. 标定成功后确认生成 `stereo_calibration.yml`。

### 9.3 判断结果

RMS 越小越好，但不能只看 RMS。还要看：

- 左右角点是否大多数成功检测
- 双目有效配对数量是否足够
- 左右图像是否确实是一一对应的同一姿态
- 棋盘格物理尺寸是否填写正确

OpenCV 官方样例棋盘图只能用于流程测试，不能代表项目真实相机系统的标定结果。

## 10. 离线重建测试

### 10.1 输入素材

重建图像应放在：

```text
test/02_reconstruction/.../left
test/02_reconstruction/.../right
```

左右文件名排序后必须一一配对，否则匹配和三维恢复会明显异常。

### 10.2 操作流程

1. 选择左右重建图像目录。
2. 选择或填写已有标定文件 `stereo_calibration.yml`。
3. 设置 ROI、灰度阈值、最小灰度、匹配距离等重建参数。
4. 点击重建。
5. 查看日志中的每帧诊断摘要。
6. 对照 `test/03_reference_results` 判断点云形态是否合理。

### 10.3 重点日志

当前重建日志会输出每帧诊断信息，包括：

- 左右中心线点数
- 左右中心线覆盖率
- 匹配点数和匹配率
- 平均/最大匹配误差
- 三维点包围盒
- 失败原因

如果点云为 0，优先看失败原因是 `left_empty`、`right_empty`、`both_empty`、`match_empty` 还是 `points_empty`。

## 11. 在线采集测试

在线采集页目前分成两部分：

- 相机采集：控制海康双相机
- 振镜控制：通过串口控制振镜、激光器和扫描参数

### 11.1 相机采集参数

界面中的相机参数会在执行采集任务时下发给真实海康相机，主要包括：

- 左相机设备
- 右相机设备
- 是否使用模拟采集
- 采集帧数
- 保存目录
- 曝光时间 us
- 增益
- 是否硬触发
- 触发线
- 超时时间 ms

注意：修改输入框本身通常不会立即写入相机，参数会在点击 `采集保存`、`一键自动标定` 或 `一键扫描重建` 时统一读取并下发。

### 11.2 振镜控制参数

振镜控制对应 `振镜通信协议.docx` 中的串口协议参数，主要包括：

- 串口号
- 波特率
- 命令超时
- 同步/异步模式
- 扫描方向
- 抓图间隔
- 连续模式等待间隔
- 步进角度
- 自动旋转角度
- 正向速度
- 反向速度
- 激光占空比
- 电压范围
- 扫描前强制重标

这些参数控制的是振镜/电机/激光控制器，不是海康相机本身。

## 12. 一键自动标定

用途：在线采集左右棋盘格图片，并调用现有 `CalibrationService` 生成标定文件。

推荐流程：

1. 连接左右相机。
2. 在相机前放置棋盘格标定板。
3. 设置曝光、增益、采集帧数和保存目录。
4. 点击 `刷新设备`。
5. 选择左相机和右相机。
6. 点击 `一键自动标定`。
7. 查看日志和弹窗中的 RMS。

该流程默认不依赖振镜扫描动作，因为棋盘格标定素材和线激光扫描素材不是同一类图像。

## 13. 一键扫描重建

用途：复用已有有效标定文件，执行振镜参数下发、双相机采集、图像落盘、三维重建和点云刷新。

推荐流程：

1. 确认已有有效 `stereo_calibration.yml`。
2. 连接左右相机和振镜控制器串口。
3. 设置相机曝光、增益、触发模式、采集帧数。
4. 设置振镜同步模式、步进角度、旋转角度、速度、激光占空比等参数。
5. 点击 `一键扫描重建`。
6. 查看日志中的采集帧数、是否复用标定、每帧重建诊断和总点数。

如果缺少有效标定文件，流程会中止并提示先执行自动标定，而不是用扫描图像直接标定。

## 14. 常见问题

### 14.1 点云数量为 0

优先检查：

- ROI 是否覆盖激光线
- 灰度阈值是否过高
- 左右重建图像是否配对
- 标定文件是否与当前相机和镜头一致
- 日志失败原因是否为 `left_empty`、`right_empty`、`match_empty`

### 14.2 点云形状奇怪

优先检查：

- 双目标定 RMS 和有效配对数量
- 棋盘格参数是否正确
- 左右相机是否拿反
- 左右图像是否时间同步
- 激光线提取是否包含大量噪声
- 匹配误差是否过大
- 与 `test/03_reference_results` 的坐标范围和整体形态是否接近

### 14.3 Release 启动报 Qt 错误

如果出现：

```text
QWidget: Must construct a QApplication before a QWidget
```

通常是 Release 程序加载了 Debug 版 Qt/VTK DLL。需要确认：

- Release 目录不要出现 `Qt5Cored.dll`
- Release 不要加载 `vtk...d.dll`
- CMake 已重新配置，不要继续运行旧 exe

### 14.4 Qt platform plugin 初始化失败

如果出现：

```text
no Qt platform plugin could be initialized
```

检查程序目录下是否存在：

```text
platforms/qwindows.dll
```

打包脚本或 Qt 的 `windeployqt` 应负责复制该插件。

### 14.5 找不到相机

检查：

- 相机网线/USB 连接
- 海康 MVS 驱动是否安装
- MVS 客户端是否占用相机
- 防火墙和网卡 IP 是否正确
- 界面是否勾选了模拟采集

## 15. 推荐测试顺序

1. 只启动程序，确认界面正常。
2. 跑离线标定，确认能生成 `stereo_calibration.yml`。
3. 跑离线重建，确认能生成点云。
4. 打包 Release，复制到独立目录运行。
5. 连接真实相机，测试 `刷新设备` 和 `采集保存`。
6. 采集棋盘格，测试 `一键自动标定`。
7. 连接振镜串口，测试参数下发。
8. 最后测试 `一键扫描重建`。

## 16. 新版在线标定采集流程

新版在线标定不再要求用户一边移动棋盘格一边连续采集，而是使用会话式单帧采集：

1. 在 `在线采集` 面板选择左相机、右相机、曝光、增益、保存目录等参数。
2. 点击 `开始标定采集`，软件连接相机并创建 `calibration_capture_yyyyMMdd_HHmmss/left|right` 会话目录。
3. 用户手动移动棋盘格到合适姿态。
4. 点击 `采集当前帧`，软件抓取当前左右相机画面并保存一对 `frame_000001.bmp`。
5. 重复“移动棋盘格 -> 采集当前帧”，直到采集到足够多的姿态。
6. 点击 `标定当前采集帧`，软件使用当前会话目录调用现有双目标定流程，并保存 `stereo_calibration.yml`。
7. 如需释放相机，点击 `结束标定采集`。

建议至少采集 6 组以上有效棋盘格姿态；帧数过少时软件会提示精度风险。

## 17. 新版在线重建采集流程

新版在线重建将“采集激光图像序列”和“对当前采集帧重建”拆成两个按钮：

1. 在 `在线采集` 面板设置相机参数、振镜总旋转角度、每帧步进角度和采集帧数。
2. 点击 `开始重建采集`，软件创建 `reconstruction_capture_yyyyMMdd_HHmmss/left|right` 会话目录。
3. 软件按采集帧数逐帧抓取左右激光图像，并按步进角度驱动振镜进入下一帧位置。
4. 采集完成后，确认已有有效 `stereo_calibration.yml`。
5. 点击 `重建当前采集帧`，软件使用当前会话目录调用现有重建流程。
6. 重建完成后刷新点云视图，并保存 `point_cloud.txt` 和 `point_cloud.pcd`。

当“总旋转角度”和“采集帧数 × 步进角度”不完全一致时，软件以采集帧数为主继续执行，并在日志中提示理论旋转角度差异。
