# HTMSR 开发环境表

## 1. 文档用途

本文档记录 HTMSR 项目当前开发、编译、运行和后续在线采集需要使用的本机环境路径。

适用场景：

- Visual Studio 打开 CMake 工程前检查环境。
- 新电脑迁移项目时对照安装依赖。
- 程序启动缺 DLL、CMake 找不到库、海康相机无法接入时排查路径。
- 后续整理发布包或安装包时确认运行时依赖。

## 2. 基础开发环境

| 环境项 | 当前版本/路径 | 是否必需 | 用途 | 备注 |
|---|---|---:|---|---|
| Windows | Windows x64 | 是 | 软件运行平台 | 当前项目默认 Windows x64 |
| Visual Studio | Visual Studio 2022 Community | 是 | MSVC 编译器、CMake 集成、调试 | 当前使用 VS 2022 |
| MSVC | `C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.43.34808` | 是 | C++ 编译工具链 | 需使用 x64 环境 |
| CMake | `C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe` | 是 | 工程配置与生成 | 当前检测版本为 `3.30.5-msvc23` |
| Ninja | `C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe` | 是 | CMake 构建后端 | 当前检测版本为 `1.12.1` |

## 3. 第三方库环境

| 库 | 当前路径 | 是否必需 | CMake 变量/配置 | 用途 |
|---|---|---:|---|---|
| Qt | `C:\ENVIORNMENT\qt\5.15.2\msvc2019_64` | 是 | `Qt5_DIR=C:/ENVIORNMENT/qt/5.15.2/msvc2019_64/lib/cmake/Qt5` | Qt Widgets 桌面界面 |
| OpenCV | `C:\ENVIORNMENT\opencv_450_vs2019` | 是 | `OpenCV_DIR=C:/ENVIORNMENT/opencv_450_vs2019/x64/vc16/lib` | 图像读取、标定、线提取、保存图像 |
| Eigen | `C:\ENVIORNMENT\ceresLib\Eigen` | 是 | `HTMSR_EIGEN_INCLUDE_DIR=C:/ENVIORNMENT/ceresLib/Eigen` | 矩阵、向量、三维点计算 |
| PCL | `C:\ENVIORNMENT\PCL\PCL 1.12.1` | 是 | `PCL_DIR=C:/ENVIORNMENT/PCL/PCL 1.12.1/cmake` | 点云数据结构、PCD 导出、点云查看适配 |
| VTK | `C:\ENVIORNMENT\PCL\PCL 1.12.1\3rdParty\VTK` | 可选 | `VTK_DIR=C:/ENVIORNMENT/PCL/PCL 1.12.1/3rdParty/VTK/lib/cmake/vtk-9.1` | 内嵌点云视图支持 |
| Boost | `C:\ENVIORNMENT\PCL\PCL 1.12.1\3rdParty\Boost` | 间接必需 | 由 PCL 间接引入 | PCL 依赖 |
| Qhull | `C:\ENVIORNMENT\PCL\PCL 1.12.1\3rdParty\Qhull` | 间接必需 | 由 PCL 间接引入 | PCL 依赖 |

注意：

- 当前工程实际使用的是 `Qt5.15.2 + MSVC2019_64`，不是 Qt6。
- 后续如果切换 Qt6，需要同步调整 `CMakeLists.txt`、`CMakePresets.json` 和运行时 DLL。
- VTK Qt 组件如果缺失，点云视图可能退回占位显示，不影响标定、重建和点云导出。

## 4. 海康 MVS 在线采集环境

| 环境项 | 当前路径 | 是否必需 | 用途 |
|---|---|---:|---|
| MVS SDK 根目录 | `C:\ENVIORNMENT\MVS` | 接海康相机时必需 | 海康 SDK 头文件、库、示例和文档 |
| SDK 头文件 | `C:\ENVIORNMENT\MVS\Development\Includes` | 接海康相机时必需 | 包含 `MvCameraControl.h` |
| SDK x64 链接库 | `C:\ENVIORNMENT\MVS\Development\Libraries\win64\MvCameraControl.lib` | 接海康相机时必需 | 编译链接海康 SDK |
| SDK 示例 | `C:\ENVIORNMENT\MVS\Development\Samples\C++` | 推荐 | 参考枚举、连接、取流、参数设置 |
| OpenCV 示例 | `C:\ENVIORNMENT\MVS\Development\Samples\OpenCV\C++` | 推荐 | 参考 SDK 图像转换为 OpenCV |
| SDK 文档 | `C:\ENVIORNMENT\MVS\Development\Documentations` | 推荐 | 查询接口、错误码、参数节点 |
| Runtime x64 | `C:\Program Files (x86)\Common Files\MVS\Runtime\Win64_x64` | 运行时必需 | 程序运行需要的 MVS DLL、GenICam DLL、CTI 文件 |

关键文件检查：

| 文件 | 当前状态 | 用途 |
|---|---|---|
| `C:\ENVIORNMENT\MVS\Development\Includes\MvCameraControl.h` | 已确认存在 | 海康 SDK 主头文件 |
| `C:\ENVIORNMENT\MVS\Development\Libraries\win64\MvCameraControl.lib` | 已确认存在 | x64 编译链接库 |
| `C:\Program Files (x86)\Common Files\MVS\Runtime\Win64_x64\MvCameraControl.dll` | 已确认存在 | 运行时主 DLL |

HTMSR 当前 CMake 配置：

```cmake
HTMSR_ENABLE_HIK_CAMERA=ON
HIK_MVS_ROOT=C:/ENVIORNMENT/MVS
HIK_MVS_RUNTIME_DIR=C:/Program Files (x86)/Common Files/MVS/Runtime/Win64_x64
```

构建后会自动复制 `Runtime\Win64_x64` 下的以下类型文件到 `htmsr_app.exe` 同目录：

```text
*.dll
*.cti
*.ini
*.manifest
```

这样可以避免启动时报缺失：

```text
GenApi_MD_VC120_v3_0_MV.dll
GCBase_MD_VC120_v3_0_MV.dll
MvRender.dll
pthreadVC2.dll
```

## 5. CMake Preset 配置

当前项目使用 `CMakePresets.json` 作为 Visual Studio 打开文件夹时的主要配置来源。

| Preset | 构建目录 | 构建类型 | 海康相机 | 用途 |
|---|---|---|---|---|
| `vs2022-x64-debug` | `C:\PROJECT\HTMSR\out\build\vs2022-x64-debug` | Debug | 开启 | 日常开发和调试 |
| `vs2022-x64-release` | `C:\PROJECT\HTMSR\out\build\vs2022-x64-release` | Release | 继承 Debug 配置 | 发布前构建验证 |

构建命令示例：

```powershell
cmake --preset vs2022-x64-debug
cmake --build --preset debug

cmake --preset vs2022-x64-release
cmake --build --preset release
```

如果普通 PowerShell 中找不到 `cmake`，可使用 Visual Studio Developer PowerShell，或直接调用：

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --preset vs2022-x64-debug
```

## 6. 运行时 PATH 建议

开发阶段优先依赖 CMake 的构建后复制逻辑，不建议手动移动 MVS 系统运行库。

如果临时需要手动运行 exe，可检查以下目录是否能被程序找到：

| 运行库 | 路径 |
|---|---|
| Qt DLL | `C:\ENVIORNMENT\qt\5.15.2\msvc2019_64\bin` |
| OpenCV DLL | `C:\ENVIORNMENT\opencv_450_vs2019\x64\vc16\bin` |
| PCL DLL | `C:\ENVIORNMENT\PCL\PCL 1.12.1\bin` |
| VTK DLL | `C:\ENVIORNMENT\PCL\PCL 1.12.1\3rdParty\VTK\bin` |
| FLANN DLL | `C:\ENVIORNMENT\PCL\PCL 1.12.1\3rdParty\FLANN\bin` |
| Qhull DLL | `C:\ENVIORNMENT\PCL\PCL 1.12.1\3rdParty\Qhull\bin` |
| MVS Runtime | `C:\Program Files (x86)\Common Files\MVS\Runtime\Win64_x64` |

推荐做法：

- Visual Studio 内运行：优先使用 VS 的 CMake 目标启动。
- 发布运行：将所需 DLL 复制到 exe 同目录。
- 不建议把 `C:\Program Files (x86)\Common Files\MVS` 手动移动到 `C:\ENVIORNMENT\MVS`。

## 7. 快速检查命令

检查 CMake/Ninja：

```powershell
cmake --version
ninja --version
```

检查海康 SDK 关键文件：

```powershell
Test-Path "C:\ENVIORNMENT\MVS\Development\Includes\MvCameraControl.h"
Test-Path "C:\ENVIORNMENT\MVS\Development\Libraries\win64\MvCameraControl.lib"
Test-Path "C:\Program Files (x86)\Common Files\MVS\Runtime\Win64_x64\MvCameraControl.dll"
```

检查构建目录是否已复制 MVS 运行库：

```powershell
Test-Path "C:\PROJECT\HTMSR\out\build\vs2022-x64-debug\GenApi_MD_VC120_v3_0_MV.dll"
Test-Path "C:\PROJECT\HTMSR\out\build\vs2022-x64-debug\GCBase_MD_VC120_v3_0_MV.dll"
Test-Path "C:\PROJECT\HTMSR\out\build\vs2022-x64-debug\MvRender.dll"
Test-Path "C:\PROJECT\HTMSR\out\build\vs2022-x64-debug\pthreadVC2.dll"
```

检查非法 Boost 宏是否复发：

```powershell
Select-String -Path "C:\PROJECT\HTMSR\out\build\vs2022-x64-debug\compile_commands.json" `
  -Pattern "BOOST_ALL_NO_LIB-DBOOST_ALL_NO_LIB"
```

预期结果：无输出。

## 8. 当前已知注意事项

- `C:\ENVIORNMENT` 目录名拼写为 `ENVIORNMENT`，项目中应保持与本机实际路径一致，不要改成 `ENVIRONMENT`。
- 普通 PowerShell 可能没有加载 VS 编译环境，直接构建时可能找不到标准库头文件。推荐使用 Visual Studio Developer PowerShell 或 Visual Studio 内置 CMake。
- MVS 客户端能看到相机，是 HTMSR 能接入相机的前置条件。
- 测试 HTMSR 真实相机采集前，应关闭 MVS 对相机的连接，避免设备被占用。
- 当前在线采集首版目标是保存 `left/right` 图像，再复用离线重建流程，不做边采集边重建。
