# HTMSR 项目开发标准

## 1. 目的

本文档规定 HTMSR 项目中的 CMake、宏定义、第三方依赖和构建配置标准，避免再次出现类似 `BOOST_ALL_NO_LIB-DBOOST_ALL_NO_LIB` 的非法宏定义问题。

本标准适用于：

- `CMakeLists.txt`
- `CMakePresets.json`
- `src` 下所有 C++ 源码
- 后续新增第三方库接入
- Visual Studio / Ninja / CMake 构建环境

## 2. 宏定义基本原则

### 2.1 只使用目标级宏定义

项目宏必须使用 `target_compile_definitions`，禁止使用全局 `add_definitions`。

推荐写法：

```cmake
target_compile_definitions(htmsr_core
    PUBLIC
        BOOST_ALL_NO_LIB
        NOMINMAX
)
```

不推荐写法：

```cmake
add_definitions(-DBOOST_ALL_NO_LIB)
add_definitions(-DNOMINMAX)
```

原因：

- 全局宏会影响所有目标，后续难以定位来源。
- 第三方依赖可能叠加全局宏，导致宏重复或拼接错误。
- Visual Studio IntelliSense 对全局宏污染更敏感。

### 2.2 CMake 中不要手写 `-D`

在 `target_compile_definitions` 中写宏名时，不要带 `-D`。

推荐：

```cmake
target_compile_definitions(htmsr_app PRIVATE HTMSR_WITH_VTK_VIEWER=0)
```

禁止：

```cmake
target_compile_definitions(htmsr_app PRIVATE -DHTMSR_WITH_VTK_VIEWER=0)
```

说明：

- `target_compile_definitions` 会自动为编译器生成正确的 `-D` 或 `/D` 参数。
- 手写 `-D` 容易和其他变量拼接成非法宏。

### 2.3 宏名必须是合法 C/C++ 标识符

合法宏名格式：

```text
NAME
NAME=value
```

合法示例：

```text
BOOST_ALL_NO_LIB
NOMINMAX
HTMSR_WITH_VTK_VIEWER=0
KISSFFT_DLL_IMPORT=1
```

非法示例：

```text
BOOST_ALL_NO_LIB-DBOOST_ALL_NO_LIB
HTMSR-WITH-VTK
1DEBUG
```

原因：

- C/C++ 宏名不能包含 `-`。
- 宏名不能以数字开头。
- `-Dxxx-Dyyy` 是两个命令行片段被错误粘连后的结果，不是合法宏。

## 3. Boost / PCL 宏定义标准

### 3.1 `BOOST_ALL_NO_LIB` 的作用

`BOOST_ALL_NO_LIB` 用于关闭 Boost 在 MSVC 下的自动链接机制。

Boost 默认可能通过头文件中的：

```cpp
#pragma comment(lib, "xxx.lib")
```

自动链接库。项目使用 CMake 显式链接库，因此应定义：

```cmake
BOOST_ALL_NO_LIB
```

### 3.2 禁止重复拼接 Boost 宏

禁止出现：

```text
-DBOOST_ALL_NO_LIB-DBOOST_ALL_NO_LIB
```

该写法会被 MSVC / IntelliSense 解析为非法宏定义，可能触发：

```text
E0992 命令行错误: 宏定义无效
```

### 3.3 PCL 引入宏必须先清洗

PCL 的 CMake 配置可能通过 `PCL_DEFINITIONS` 或 imported target 向项目传入宏定义。项目必须先调用宏清洗函数，再把定义传递给目标。

当前项目使用：

```cmake
htmsr_normalize_compile_definitions(HTMSR_PCL_DEFINITIONS ${PCL_DEFINITIONS})
set(PCL_DEFINITIONS "${HTMSR_PCL_DEFINITIONS}")
htmsr_sanitize_imported_compile_definitions(${PCL_LIBRARIES})
```

该逻辑会：

- 去掉第三方定义中多余的 `-D` 前缀。
- 拆分被错误粘连的 `XXX-DYYY`。
- 去重重复宏。
- 检查宏名是否合法。
- 遇到无法修复的非法宏时直接终止 CMake 配置。

### 3.4 PCL 作为实现依赖时必须隔离宏传播

如果项目公开头文件没有暴露 PCL 类型，PCL 必须作为 `PRIVATE` 实现依赖，不允许通过 `PUBLIC` 传播到上层目标。

当前项目采用 `LINK_ONLY` 方式链接 PCL：

```cmake
set(HTMSR_PCL_LINK_LIBRARIES)
foreach(pcl_library IN LISTS PCL_LIBRARIES)
    list(APPEND HTMSR_PCL_LINK_LIBRARIES "$<LINK_ONLY:${pcl_library}>")
endforeach()

target_link_libraries(htmsr_core
    PUBLIC
        Eigen3::Eigen
        ${OpenCV_LIBS}
    PRIVATE
        ${HTMSR_PCL_LINK_LIBRARIES}
)
```

这样做的目的：

- 只使用 PCL 的链接库，不继承 PCL imported target 中的脏宏。
- 避免 `BOOST_ALL_NO_LIB-DBOOST_ALL_NO_LIB` 这类第三方宏污染扩散到 `htmsr_app`。
- 保持 `htmsr_core` 的公开接口干净，符合低耦合设计。
- 后续如果 UI 层确实需要 PCL 头文件，应在 UI 目标中单独添加 `PRIVATE ${PCL_INCLUDE_DIRS}`。

判断标准：

- 公开头文件使用了第三方类型：依赖可以考虑 `PUBLIC`，但宏仍需清洗。
- 只有 `.cpp` 使用第三方类型：依赖必须是 `PRIVATE`。
- 第三方包会传播异常宏或编译选项：优先使用 `LINK_ONLY` 隔离，再显式添加必要 include 和干净宏。

## 4. CMake 编写标准

### 4.1 每个模块只管理自己的宏

示例：

```cmake
target_compile_definitions(htmsr_core
    PUBLIC
        BOOST_ALL_NO_LIB
        NOMINMAX
)

target_compile_definitions(htmsr_app
    PRIVATE
        HTMSR_WITH_VTK_VIEWER=0
)
```

含义：

- `htmsr_core` 的公共宏可以传递给依赖它的目标。
- `htmsr_app` 的 UI 宏只在应用目标内部有效。

### 4.2 第三方库不要修改安装目录

禁止直接修改：

```text
C:\ENVIORNMENT\PCL
C:\ENVIORNMENT\opencv_450_vs2019
C:\ENVIORNMENT\qt
```

如果第三方 CMake 配置存在问题，应在本项目 CMake 中做隔离和修正。

原因：

- 修改安装目录会影响其他项目。
- 重新安装第三方库后改动会丢失。
- 无法通过本项目版本管理追踪。

### 4.3 依赖路径放在 Preset 或缓存变量中

第三方库路径应写入：

```text
CMakePresets.json
```

或由用户通过 CMake cache 变量指定。

不要在源码中硬编码运行时路径，也不要在业务代码中读取开发环境路径。

## 5. 新增第三方库流程

新增第三方库时按以下顺序处理：

1. 使用 `find_package` 或明确的 imported target 接入。
2. 不使用 `add_definitions`。
3. 检查第三方是否暴露 `INTERFACE_COMPILE_DEFINITIONS`。
4. 如第三方会传递宏，调用 `htmsr_sanitize_imported_compile_definitions`。
5. 重新生成 `compile_commands.json`。
6. 搜索是否有非法宏或重复拼接宏。

检查命令：

```powershell
Select-String -Path C:\PROJECT\HTMSR\out\build\vs2022-x64-debug\compile_commands.json `
  -Pattern "-D[A-Za-z_][A-Za-z0-9_]*-D"
```

如果有输出，说明可能存在宏拼接问题。

## 6. 构建后检查标准

每次调整 CMake 或第三方依赖后，至少执行以下检查。

### 6.1 重新配置

```powershell
cmake --preset vs2022-x64-debug
```

### 6.2 重新构建

```powershell
cmake --build C:\PROJECT\HTMSR\out\build\vs2022-x64-debug --config Debug
```

### 6.3 检查非法宏

```powershell
Select-String -Path C:\PROJECT\HTMSR\out\build\vs2022-x64-debug\compile_commands.json `
  -Pattern "BOOST_ALL_NO_LIB-DBOOST_ALL_NO_LIB"
```

预期结果：

```text
无输出
```

### 6.4 检查 CMake 中禁止写法

```powershell
Select-String -Path C:\PROJECT\HTMSR\CMakeLists.txt -Pattern "add_definitions|remove_definitions"
```

预期结果：

```text
无输出
```

## 7. 常见错误示例

### 7.1 两个宏被粘成一个

错误：

```text
-DDEBUG-DNOMINMAX
```

正确：

```text
-DDEBUG
-DNOMINMAX
```

在 CMake 中正确写法：

```cmake
target_compile_definitions(target PRIVATE DEBUG NOMINMAX)
```

### 7.2 带值宏被错误拼接

错误：

```text
-DHTMSR_WITH_VTK_VIEWER=0-DNOMINMAX
```

正确：

```text
-DHTMSR_WITH_VTK_VIEWER=0
-DNOMINMAX
```

在 CMake 中正确写法：

```cmake
target_compile_definitions(target PRIVATE HTMSR_WITH_VTK_VIEWER=0 NOMINMAX)
```

### 7.3 将编译选项当宏定义

错误：

```cmake
target_compile_definitions(target PRIVATE /utf-8)
```

正确：

```cmake
target_compile_options(target PRIVATE /utf-8)
```

## 8. 当前项目宏定义约定

当前允许的项目级宏：

```text
BOOST_ALL_NO_LIB
NOMINMAX
HTMSR_WITH_VTK_VIEWER=0
HTMSR_WITH_VTK_VIEWER=1
```

当前允许的第三方宏示例：

```text
BOOST_SYSTEM_NO_LIB
BOOST_FILESYSTEM_NO_LIB
BOOST_DATE_TIME_NO_LIB
BOOST_IOSTREAMS_NO_LIB
BOOST_SERIALIZATION_NO_LIB
KISSFFT_DLL_IMPORT=1
kiss_fft_scalar=double
__SSE__
__SSE2__
__SSE3__
__SSSE3__
__SSE4_1__
__SSE4_2__
```

如需新增宏，必须满足：

- 宏名合法。
- 有明确用途。
- 定义在最小必要目标上。
- 不污染全局。
- 通过 `compile_commands.json` 检查。

## 9. 代码审查要求

涉及 CMake 或构建配置的提交必须检查：

- 是否新增了 `add_definitions`。
- 是否新增了 `remove_definitions`。
- 是否在 `target_compile_definitions` 中手写了 `-D`。
- 是否新增了不合法宏名。
- 是否更新了本文档中对应的宏约定。

如果发现非法宏，应优先在 CMake 配置阶段失败，而不是等到编译阶段或 Visual Studio IntelliSense 报错。
