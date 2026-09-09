# HTMSR 准备与测试操作文档

## 1\. 文档目的

本文档用于说明以下内容：

* 如何在另一台电脑上从 Git 下载 `HTMSR` 项目到本地
* 如何准备项目依赖环境
* 如何生成本机 `CMake` 配置
* 如何编译、打包并运行项目
* 如何进行基础功能测试

本文档面向两种使用场景：

* 开发测试：需要在另一台电脑上继续编译、调试项目
* 运行测试：只需要把可执行程序和依赖复制到另一台电脑上直接运行

## 2\. 两种测试方式

### 2.1 开发测试

适用于以下情况：

* 需要在另一台电脑上修改代码
* 需要重新编译 `Debug` / `Release`
* 需要验证本机环境是否完整

这种方式需要准备完整开发环境，包括：

* Visual Studio 2022
* CMake
* Ninja
* Qt 5.15.2
* OpenCV 4.5.0
* PCL 1.12.1
* Eigen
* 海康 MVS SDK
* 可选的 VTK 9.1 Qt 版

### 2.2 运行测试

适用于以下情况：

* 只需要验证程序能否启动
* 只需要验证离线重建、导出、日志等功能
* 不在测试机上改代码

这种方式不需要完整开发环境，只需要复制打包后的发布目录。

## 3\. Git 下载项目

在另一台电脑上打开 `PowerShell` 或 `Visual Studio 2022 Developer PowerShell`，执行：

```powershell
git clone <仓库地址> C:\\\\PROJECT\\\\HTMSR
```

如果已经存在旧目录，建议先确认里面没有需要保留的本地修改，再重新拉取。

## 4\. 开发环境准备

### 4.1 必装工具

建议安装：

* `Visual Studio 2022 Community` 或更高版本
* VS 工作负载：`Desktop development with C++`
* `Git`

建议确认以下命令可用：

```powershell
cmake --version
ninja --version
git --version
```

### 4.2 依赖目录建议

建议另一台电脑也按如下路径组织依赖：

```text
C:\\\\ENVIORNMENT\\\\qt\\\\5.15.2\\\\msvc2019\\\_64
C:\\\\ENVIORNMENT\\\\opencv\\\_450\\\_vs2019
C:\\\\ENVIORNMENT\\\\PCL\\\\PCL 1.12.1
C:\\\\ENVIORNMENT\\\\ceresLib\\\\Eigen
C:\\\\ENVIORNMENT\\\\MVS
```

如果你已经准备了单独的 VTK Qt 版本，建议路径为：

```text
C:\\\\ENVIORNMENT\\\\VTK\\\\VTK-9.1.0-qt5-release
```

### 4.3 海康运行时

如果要测试海康相机在线采集，还需要确认运行时目录存在：

```text
C:\\\\Program Files (x86)\\\\Common Files\\\\MVS\\\\Runtime\\\\Win64\\\_x64
```

至少应能找到：

```text
MvCameraControl.dll
```

## 5\. 本机环境配置

项目使用本地环境配置文件来生成 `CMakeUserPresets.json`。

### 5.1 检查或编辑本地配置

编辑文件：

[htmsr\_environment.local.json](/C:/PROJECT/HTMSR/scripts/htmsr_environment.local.json)

根据另一台电脑的真实路径修改以下字段：

```json
{
  "qtRoot": "C:/ENVIORNMENT/qt/5.15.2/msvc2019\\\_64",
  "opencvRoot": "C:/ENVIORNMENT/opencv\\\_450\\\_vs2019",
  "pclRoot": "C:/ENVIORNMENT/PCL/PCL 1.12.1",
  "vtkRoot": "C:/ENVIORNMENT/VTK/VTK-9.1.0-qt5-release",
  "eigenIncludeDir": "C:/ENVIORNMENT/ceresLib/Eigen",
  "mvsRoot": "C:/ENVIORNMENT/MVS",
  "mvsRuntimeDir": "C:/Program Files (x86)/Common Files/MVS/Runtime/Win64\\\_x64",
  "enableHikCamera": true,
  "enableVtkViewer": true
}
```

说明：

* 如果没有单独的 VTK Qt 版本，可以先把 `vtkRoot` 留空
* `enableHikCamera=false` 时可跳过海康 SDK 相关测试
* `enableVtkViewer=true` 只表示尝试启用点云三维视图，是否真正可用取决于 VTK Qt 模块是否完整

### 5.2 生成本机 CMake 预设

在项目根目录执行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\\\\scripts\\\\Configure-HtmsrEnvironment.ps1 -NoPrompt
```

成功后会生成：

[CMakeUserPresets.json](/C:/PROJECT/HTMSR/CMakeUserPresets.json)

同时会做一轮依赖路径检查。

## 6\. 编译项目

### 6.1 Debug 编译

```powershell
cmake --preset local-vs2022-x64-debug
cmake --build --preset local-debug
```

### 6.2 Release 编译

```powershell
cmake --preset local-vs2022-x64-release
cmake --build --preset local-release
```

### 6.3 Visual Studio 中使用

在 Visual Studio 中打开：

```text
C:\\\\PROJECT\\\\HTMSR
```

然后选择对应预设：

* `local-vs2022-x64-debug`
* `local-vs2022-x64-release`

## 7\. 打包项目

### 7.1 Debug 包

适用于开发机调试和功能验证：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\\\\scripts\\\\Package-HtmsrRelease.ps1 -Configuration Debug -Clean
```

输出目录：

```text
C:\\\\PROJECT\\\\HTMSR\\\\out\\\\package\\\\HTMSR\\\_debug
```

说明：

* Debug 包会复制 Qt/OpenCV/PCL/VTK/MVS 相关 DLL
* Debug 包依赖 VS Debug Runtime，更适合开发机

### 7.2 Release 包

适用于另一台电脑直接运行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\\\\scripts\\\\Package-HtmsrRelease.ps1 -Configuration Release -Clean
```

输出目录：

```text
C:\\\\PROJECT\\\\HTMSR\\\\out\\\\package\\\\HTMSR\\\_release
```

说明：

* Release 包会复制 `exe` 同级运行所需 DLL
* 更适合普通测试机直接使用

## 8\. 另一台电脑直接运行测试

如果不需要编译，只需要复制整个目录：

```text
C:\\\\PROJECT\\\\HTMSR\\\\out\\\\package\\\\HTMSR\\\_release
```

到另一台电脑任意位置，例如：

```text
D:\\\\Test\\\\HTMSR\\\_release
```

然后直接运行：

```text
htmsr\\\_app.exe
```

注意：

* 不要只复制 `exe`
* 必须连同 `platforms`、`imageformats`、`styles`、所有 DLL 一起复制

## 9\. 在线采集说明

### 9.1 当前已实现能力

当前在线采集功能已经具备以下能力：

* 枚举海康相机
* 选择左右相机
* 配置曝光、增益、触发模式、超时
* 抓取左右图像
* 保存为标准 `left/right` 图像目录

采集保存结构如下：

```text
输出目录/
  capture\\\_YYYYMMDD\\\_HHMMSS/
    left/
      frame\\\_000001.bmp
      frame\\\_000002.bmp
    right/
      frame\\\_000001.bmp
      frame\\\_000002.bmp
```

### 9.2 当前限制

当前版本更准确的工作模式是：

* 在线采集
* 保存左右图像
* 再走离线标定 / 离线重建

尚未完成以下现场验证：

* 双海康真实硬件同步联调
* 长时间稳定性验证
* 边采集边重建的实时闭环

## 10\. 点云显示说明

### 10.1 占位视图与真实视图

项目中的点云页有两种模式：

* 占位视图：程序可运行，但只显示提示文字或点数
* 真实三维视图：通过 `VTK + QVTKOpenGLNativeWidget + PCLVisualizer` 显示可旋转缩放点云

### 10.2 当前判断标准

如果 CMake 配置时出现以下警告：

```text
PCL visualization or VTK Qt components were not found. Building with a placeholder point cloud view.
```

则说明当前构建仍然是占位视图。

如果要真正显示点云，VTK 安装目录里至少应存在：

```text
vtkGUISupportQt-9.1.dll
vtkViewsQt-9.1.dll
```

## 11\. 基础测试清单

### 11.1 启动测试

* 程序是否能正常启动
* 是否还存在缺失 DLL 报错
* 日志区是否显示 `HTMSR started.`

### 11.2 离线重建测试

* 导入左右标定图像目录
* 导入左右重建图像目录
* 加载或生成 `stereo\\\_calibration.yml`
* 执行重建
* 检查日志、左图、右图、调试图、点云统计

### 11.3 在线采集测试

* 刷新设备
* 检查是否枚举到真实海康相机
* 配置输出目录
* 采集若干帧
* 检查 `left/right` 目录及图像文件是否生成

### 11.4 导出测试

* 导出 `point\\\_cloud.txt`
* 导出 `point\\\_cloud.pcd`
* 检查文件是否存在

## 12\. 常见问题

### 12.1 程序启动时报缺 DLL

处理方式：

* 确认使用的是完整打包目录，而不是单独的 `exe`
* 确认 `platforms/qwindows.dll` 存在
* 重新执行打包脚本

### 12.2 海康设备枚举不到

处理方式：

* 先在 `MVS` 客户端中确认相机能被识别
* 检查 `MVS Runtime` 路径是否正确
* 检查网卡、IP、防火墙、触发环境

### 12.3 点云页仍是占位视图

处理方式：

* 检查 `VTK\\\_DIR` 是否指向正确安装目录
* 检查 `vtkGUISupportQt-9.1.dll` 和 `vtkViewsQt-9.1.dll` 是否存在
* 重新执行 `cmake --preset ...` 和 `cmake --build ...`

## 13\. 推荐交付方式

如果要发给其他人测试，推荐交付：

* Git 仓库地址
* 本文档 `prepare.md`
* 一个现成的 `HTMSR\\\_release` 压缩包

推荐压缩包命名：

```text
HTMSR\\\_release\\\_yyyyMMdd.zip
```

这样测试人员可以根据情况选择：

* 直接运行发布包
* 或者按文档准备开发环境后自行编译

