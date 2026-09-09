# HTMSR 项目代码规范

## 1. 文档定位

本文档是 HTMSR 项目的统一代码规范入口，用于约束当前项目中的代码编写、构建配置和工程协作方式。

当前版本先收敛两个已经明确暴露问题的主题：

- 宏定义规范
- 中文注释规范

后续随着项目推进，还会继续补充更多规范，例如：

- 命名规范
- 目录与模块边界规范
- 日志规范
- 异常与错误处理规范
- 配置文件规范
- 线程与异步任务规范
- 测试规范
- UI 交互与页面代码规范

本文档的目标不是一次写全，而是作为项目长期维护的“规范主文档”，后续所有新增规范都继续补充到这里。

## 2. 适用范围

本文档适用于以下内容：

- [CMakeLists.txt](C:/PROJECT/HTMSR/CMakeLists.txt)
- `CMakePresets.json`
- `src/core`
- `src/app/services`
- `src/app/acquisition`
- `src/app/ui`
- `docs` 中与开发流程直接相关的技术文档

不适用于：

- `references` 中的历史参考代码
- 第三方库源码
- Qt 自动生成文件

## 3. 使用原则

### 3.1 规范优先级

当实现方式与规范冲突时，优先修改实现，尽量不要绕过规范。

如果现有规范不够覆盖实际问题，应先补充文档，再推广到代码中。

### 3.2 修改要求

凡是涉及以下内容的提交，都需要同步检查本文档：

- 新增公共工程约定
- 修改构建系统行为
- 形成稳定的注释风格
- 修复某类重复出现的问题

如果某项规则已经在团队内被反复执行，就不应只停留在口头约定里，而应该写入本文档。

### 3.3 扩展方式

后续新增规范时，统一使用以下结构：

```text
## X. 规范主题
### X.1 目标
### X.2 适用范围
### X.3 强制规则
### X.4 推荐写法
### X.5 禁止写法
### X.6 示例
### X.7 检查清单
```

这样做的目的是让整份规范长期可扩展，不会因为不断追加内容而变得难读。

## 4. 宏定义规范

### 4.1 目标

宏定义规范用于避免以下问题：

- 非法宏名进入编译命令
- 多个 `-D` 片段被错误拼接
- 第三方库的宏污染扩散到整个工程
- Visual Studio IntelliSense 与真实构建结果不一致

当前项目已经真实出现过的典型问题是：

```text
BOOST_ALL_NO_LIB-DBOOST_ALL_NO_LIB
```

该问题会导致 VS 报 `E0992`，并污染 `compile_commands.json` 与 `build.ninja`。

### 4.2 适用范围

本节适用于：

- `target_compile_definitions`
- `target_compile_options`
- `find_package` 引入的第三方 usage requirements
- `PCL_DEFINITIONS`、`Boost` 相关宏
- 所有会影响最终编译命令的 CMake 逻辑

### 4.3 强制规则

#### 4.3.1 只使用目标级宏定义

项目宏必须通过 `target_compile_definitions` 添加，不允许使用全局 `add_definitions`。

正确写法：

```cmake
target_compile_definitions(htmsr_core
    PUBLIC
        BOOST_ALL_NO_LIB
        NOMINMAX
)
```

错误写法：

```cmake
add_definitions(-DBOOST_ALL_NO_LIB)
add_definitions(-DNOMINMAX)
```

#### 4.3.2 不要在 `target_compile_definitions` 中手写 `-D`

`target_compile_definitions` 只写宏名或 `NAME=value`，不写 `-D`。

正确写法：

```cmake
target_compile_definitions(htmsr_app PRIVATE HTMSR_WITH_VTK_VIEWER=0)
```

错误写法：

```cmake
target_compile_definitions(htmsr_app PRIVATE -DHTMSR_WITH_VTK_VIEWER=0)
```

#### 4.3.3 宏名必须是合法 C/C++ 标识符

宏定义只能使用以下形式：

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

#### 4.3.4 编译选项与宏定义必须分开管理

宏使用 `target_compile_definitions`，编译器选项使用 `target_compile_options`。

正确写法：

```cmake
target_compile_options(htmsr_core PRIVATE /utf-8)
```

错误写法：

```cmake
target_compile_definitions(htmsr_core PRIVATE /utf-8)
```

#### 4.3.5 第三方脏宏必须在项目侧清洗

如果第三方库通过 `PCL_DEFINITIONS`、`INTERFACE_COMPILE_DEFINITIONS` 或 `INTERFACE_COMPILE_OPTIONS` 传入宏定义，必须在项目侧清洗后再使用。

当前项目采用的清洗逻辑在 [CMakeLists.txt](C:/PROJECT/HTMSR/CMakeLists.txt) 中，包括：

- `htmsr_normalize_compile_definitions`
- `htmsr_normalize_compile_options`
- `htmsr_sanitize_imported_compile_definitions`

这些逻辑负责：

- 移除多余的 `-D`
- 拆分被错误拼接的宏片段
- 去重重复宏
- 拦截非法宏名

#### 4.3.6 第三方实现依赖不得无条件向上游传播

如果某个第三方库只在 `.cpp` 中使用，而没有出现在公开头文件里，那么它应作为实现依赖处理，不能以 `PUBLIC` 方式把自身宏和编译选项传播到上层目标。

当前项目中的 PCL 就属于这种情况。

### 4.4 PCL / Boost 特殊规则

#### 4.4.1 `BOOST_ALL_NO_LIB` 是允许且必要的

`BOOST_ALL_NO_LIB` 用于关闭 Boost 在 MSVC 下的自动链接机制。项目由 CMake 负责显式链接库，因此保留这个宏是正确的。

#### 4.4.2 禁止重复拼接 Boost 宏

禁止出现：

```text
-DBOOST_ALL_NO_LIB-DBOOST_ALL_NO_LIB
```

这不是两个宏，而是一个非法拼接结果。

#### 4.4.3 PCL 作为实现依赖时使用 `LINK_ONLY`

当前项目中，PCL 通过 `LINK_ONLY` 方式参与链接，避免 imported target 的脏 usage requirements 扩散：

```cmake
set(HTMSR_PCL_LINK_LIBRARIES)
foreach(pcl_library IN LISTS PCL_LIBRARIES)
    list(APPEND HTMSR_PCL_LINK_LIBRARIES "$<LINK_ONLY:${pcl_library}>")
endforeach()
```

使用原则：

- 只在实现层使用的第三方库，优先 `PRIVATE`
- 若第三方 imported target 会携带脏宏或脏编译选项，优先使用 `LINK_ONLY`
- 必要的 include 目录和干净宏由项目显式添加

### 4.5 推荐写法

推荐示例一：项目级宏定义。

```cmake
target_compile_definitions(htmsr_core
    PUBLIC
        BOOST_ALL_NO_LIB
        NOMINMAX
)
```

推荐示例二：编译选项。

```cmake
if(MSVC)
    target_compile_options(htmsr_core PRIVATE /utf-8)
endif()
```

推荐示例三：只链接第三方库，不继承脏宏。

```cmake
target_link_libraries(htmsr_core
    PUBLIC
        Eigen3::Eigen
        ${OpenCV_LIBS}
    PRIVATE
        ${HTMSR_PCL_LINK_LIBRARIES}
)
```

### 4.6 禁止写法

禁止示例一：全局宏污染。

```cmake
add_definitions(-DBOOST_ALL_NO_LIB)
```

禁止示例二：宏定义里手写 `-D`。

```cmake
target_compile_definitions(target PRIVATE -DDEBUG)
```

禁止示例三：将编译选项放入宏定义接口。

```cmake
target_compile_definitions(target PRIVATE /utf-8)
```

禁止示例四：让只在实现层使用的第三方依赖以 `PUBLIC` 方式传播。

```cmake
target_link_libraries(htmsr_core PUBLIC ${PCL_LIBRARIES})
```

### 4.7 检查清单

每次修改 CMake 或第三方库接入方式后，至少检查：

1. 是否新增了 `add_definitions` 或 `remove_definitions`
2. 是否在 `target_compile_definitions` 中手写了 `-D`
3. `compile_commands.json` 中是否存在非法拼接宏
4. `build.ninja` 中是否存在非法拼接宏
5. 第三方库是否把不必要的宏传播到了 `htmsr_app`

推荐检查命令：

```powershell
cmake --preset vs2022-x64-debug
cmake --build --preset debug
Select-String -Path C:\PROJECT\HTMSR\out\build\vs2022-x64-debug\compile_commands.json `
  -Pattern "BOOST_ALL_NO_LIB-DBOOST_ALL_NO_LIB"
Select-String -Path C:\PROJECT\HTMSR\CMakeLists.txt `
  -Pattern "add_definitions|remove_definitions"
```

预期结果：

```text
非法拼接宏检查无输出
禁止写法检查无输出
```

## 5. 中文注释规范

### 5.1 目标

中文注释规范用于统一项目代码可读性，保证后续算法迁移、问题排查和团队协作时，代码的业务意图能够快速被理解。

本节注释风格参考 `references/src` 中的中文注释，核心特点是：

- 函数前使用三段式块注释
- 函数内部用少量中文行注释解释关键步骤
- 注释重点说明业务动作、算法步骤、坐标系、经验参数和副作用

不采用复杂的 Doxygen 标签体系，不追求形式化生成文档，而是优先服务当前工程开发和维护。

### 5.2 适用范围

必须写中文注释的场景：

- `src/core` 中的算法核心函数
- 文件读写、标定加载、点云导出等有明显副作用的函数
- 坐标变换、矩阵构造、参数换算、匹配逻辑
- 复杂槽函数、任务调度函数、采集接口实现

可以不写或简写的场景：

- 简单 getter / setter
- 一眼可见的 UI 控件初始化
- 业务含义非常明确的短小函数
- 第三方样板代码

### 5.3 强制规则

#### 5.3.1 核心函数必须写块注释

复杂函数和业务核心函数必须在定义前写块注释，格式固定为：

```cpp
/*
    函数功能：一句话说明函数完成的业务动作或算法动作
    输入：
        参数名：说明参数含义、单位、坐标系、范围或特殊约定
    输出：
        参数名：说明输出内容
        返回值：说明返回值含义
*/
```

#### 5.3.2 块注释必须使用三段式

块注释中固定使用以下标签：

- `函数功能：`
- `输入：`
- `输出：`

不要混用英文标签，例如 `Function`、`Input`、`Output`。

#### 5.3.3 副作用必须写清楚

如果函数没有返回值，但会执行以下操作，必须在 `输出：` 中说明：

- 写文件
- 覆盖文件
- 更新缓存
- 修改对象内部状态
- 触发日志输出
- 推送调试图或任务结果

#### 5.3.4 算法关键步骤必须写行注释

函数内部只在关键步骤处写 `//` 中文注释，说明：

- 这一段在做什么业务步骤
- 为什么要这样处理
- 参数或公式的经验来源
- 当前坐标系或单位约定

#### 5.3.5 坐标系、单位、经验公式必须可见

涉及以下内容时，必须写清楚注释：

- 像素坐标 / ROI 坐标 / 相机坐标 / 世界坐标
- `R` / `t` / 齐次矩阵 / 射线 / 归一化平面
- 阈值、线宽、sigma、匹配距离等经验参数
- 输出文件格式、点云单位、标定文件兼容性

#### 5.3.6 不允许保留长期注释掉的调试代码

参考代码中存在较多 `imshow`、`waitKey`、`cout` 的历史调试注释。新项目中禁止长期保留这类注释掉的调试代码。

需要调试时，应优先使用：

- `Logger`
- 调试预览图
- Qt 日志面板
- 结构化结果对象

### 5.4 推荐写法

推荐示例一：核心算法函数块注释。

```cpp
/*
    函数功能：使用灰度重心法在指定 ROI 区域内提取激光条纹中心线
    输入：
        srcImg：输入图像
        rect：待处理的 ROI 区域
    输出：
        dstPoints：提取出的激光中心点集合，坐标为整张图中的位置
        imgline：绘制了中心线点的可视化结果图
*/
void LaserExtractionService::extractGrayCentroid(
    const cv::Mat& srcImg,
    const cv::Rect& rect,
    std::vector<Eigen::Vector2d>& dstPoints,
    cv::Mat& imgline)
{
    // 函数实现
}
```

推荐示例二：带返回值的文件读取函数。

```cpp
/*
    函数功能：从 yml 文件中读取左右相机标定参数和双目外参
    输入：
        filename：标定结果文件名或文件路径
    输出：
        result：读取到的双目标定参数
        返回值：若关键标定参数读取成功且有效则返回 true，否则返回 false
*/
bool CalibrationService::loadCalibrationFile(
    const std::string& filename,
    CalibrationResult& result)
{
    // 函数实现
}
```

推荐示例三：带副作用的导出函数。

```cpp
/*
    函数功能：将三维点云保存为 PCD 文件
    输入：
        filename：输出 PCD 文件路径
        points：待保存的三维点集合，单位与重建结果保持一致
    输出：
        无（函数执行后会在磁盘上生成或覆盖对应的 PCD 文件）
*/
void PointCloudService::savePcd(
    const std::string& filename,
    const std::vector<Eigen::Vector3d>& points) const
{
    // 函数实现
}
```

推荐示例四：关键步骤行注释。

```cpp
// 将 OpenCV 相机矩阵转换成 Eigen 矩阵，便于后续几何计算。
```

```cpp
// 在左右激光中心线上寻找最匹配的点对。
```

```cpp
// 对每一组匹配点构造两条空间射线，并求它们的最近点中点作为三维点。
```

推荐示例五：经验参数说明。

```cpp
// stripeWidth：激光线条宽度；经验公式：sigma = stripeWidth / sqrt(3)
const double sigma = config.stripeWidth / std::sqrt(3.0);
```

推荐示例六：坐标系说明。

```cpp
// rect 为原图坐标系下的 ROI，中心线点写回时需要加上 ROI 左上角偏移。
const double xInImage = xInRoi + rect.x;
```

### 5.5 禁止写法

禁止示例一：低价值语法翻译式注释。

```cpp
int count = 0; // 定义 count 等于 0
```

禁止示例二：只写“做了什么”但没有业务信息。

```cpp
// 创建矩阵
// 进入循环
// 赋值
```

禁止示例三：长期保留注释掉的调试代码。

```cpp
// imshow("imgline", imgline);
// waitKey(0);
// cout << value << endl;
```

禁止示例四：函数签名变了但注释没有同步更新。

```cpp
/*
    输入：
        file：旧参数名
*/
bool loadCalibration(const std::string& filename);
```

### 5.6 头文件注释规则

头文件中的结构体、枚举、接口类应使用简短中文行注释说明用途；复杂接口函数仍使用三段式块注释。

推荐示例：

```cpp
// 激光中心线提取算法类型。
enum class LaserExtractionMode {
    GrayCentroid,
    Steger
};
```

```cpp
// 双目标定输出结果，保存左右相机内参、畸变、双目外参和误差统计。
struct CalibrationResult {
    cv::Mat K1;
    cv::Mat D1;
    cv::Mat K2;
    cv::Mat D2;
    double rms = 0.0;
};
```

### 5.7 检查清单

代码审查时必须检查：

1. 新增核心函数是否有 `函数功能 / 输入 / 输出` 块注释
2. 注释中的参数名是否与函数签名一致
3. 修改函数副作用后是否同步更新注释
4. 是否说明了 ROI、像素坐标、相机坐标、世界坐标和单位
5. 是否说明了经验阈值、公式来源或特殊约定
6. 是否仍保留 `imshow`、`waitKey`、`cout` 注释掉的调试代码
7. 是否存在低价值、逐行翻译代码语法的注释

## 6. 后续扩展预留

### 6.1 预留原则

后续新增规范时，不要把新规则散落到多个独立文档中。优先补充到本文档，保持“一个主入口”的结构。

新增主题时：

- 先明确问题来源
- 再写成稳定规则
- 补充正反示例
- 给出检查清单

### 6.2 推荐后续补充主题

后续建议按以下顺序继续完善：

1. 线程与后台任务规范
2. 测试与回归验证规范
3. UI 交互与状态同步规范
4. 性能与批处理规范
5. 在线采集接口扩展规范
6. 文档与变更同步规范

### 6.3 新增规范模板

后续新增章节时，建议直接复制以下模板：

```text
## X. 规范主题
### X.1 目标
### X.2 适用范围
### X.3 强制规则
### X.4 推荐写法
### X.5 禁止写法
### X.6 示例
### X.7 检查清单
```

## 7. 命名规范

### 7.1 目标

命名规范用于提升代码可读性，降低沟通成本，避免“名字看不出职责”和“同类对象命名不统一”的问题。

### 7.2 适用范围

本节适用于：

- 类名
- 服务类名
- 配置对象名
- 结果对象名
- Qt 控件成员名
- 槽函数名
- 布尔变量名
- 路径变量名

### 7.3 强制规则

#### 7.3.1 类名统一使用 `PascalCase`

示例：

```cpp
class CalibrationService;
class PointCloudViewWidget;
class OfflineImageSequenceProvider;
```

#### 7.3.2 业务类名必须带职责后缀

统一约定：

- 服务类使用 `Service`
- 界面窗口使用 `Window`
- 界面组件使用 `Widget`
- 参数面板使用 `Panel`
- 提供者使用 `Provider`
- 配置对象使用 `Config`
- 输入对象使用 `Input`
- 结果对象使用 `Result`
- 统计对象使用 `Stats` 或 `Summary`
- 接口类使用 `I` 前缀

#### 7.3.3 成员变量统一使用尾缀 `_`

示例：

```cpp
Logger logger_;
CalibrationResult calibrationResult_;
QProgressBar* progressBar_ = nullptr;
```

#### 7.3.4 布尔变量必须可读

推荐使用：

- `isValid`
- `hasCalibration`
- `enablePreview`
- `removeEndPoints`

禁止使用：

- `flag`
- `ok1`
- `state2`

#### 7.3.5 路径变量命名必须区分文件与目录

统一约定：

- 文件路径使用 `xxxPath`
- 目录路径使用 `xxxDirectory`

示例：

```cpp
std::string calibrationFilePath;
std::string leftReconstructionDirectory;
std::string outputDirectory;
```

#### 7.3.6 槽函数名必须表达事件语义

统一使用以下风格之一，并在同一类中保持一致：

- `onXxx`
- `handleXxx`

示例：

```cpp
void onStartCalibration();
void onExportPointCloud();
void handleReconstructionFinished();
```

### 7.4 推荐写法

推荐示例：

```cpp
CalibrationInput calibrationInput;
CalibrationResult calibrationResult;
LaserExtractionConfig laserConfig;
PointCloudViewWidget* pointCloudView_ = nullptr;
bool hasValidCalibration = false;
std::string outputDirectory;
```

### 7.5 禁止写法

禁止示例：

```cpp
class dataMgr;
bool flag;
std::string path1;
std::string filePath2;
void slot1();
```

### 7.6 检查清单

代码审查时检查：

1. 名称是否能看出职责
2. 同类对象是否使用统一后缀
3. 布尔变量是否可直接读成判断句
4. 路径变量是否区分文件与目录
5. Qt 成员控件是否避免 `btn1`、`label2` 这类无语义命名

## 8. 目录与分层规范

### 8.1 目标

目录与分层规范用于控制耦合，保证算法核心、应用服务、界面层和采集层职责清晰，避免 `MainWindow.cpp` 逐步演化成业务大杂烩。

### 8.2 适用范围

本节适用于：

- `src/core`
- `src/app/services`
- `src/app/ui`
- `src/app/acquisition`

### 8.3 强制规则

#### 8.3.1 `src/core` 只放纯业务核心

允许放入：

- 标定、重建、点云、线提取算法
- 数据结构
- 文件读写工具
- 核心日志接口

禁止放入：

- `QWidget`、`QMainWindow` 等 UI 类型
- 具体页面逻辑
- Qt 控件操作

#### 8.3.2 `src/app/services` 负责应用编排

允许放入：

- 任务调度
- 参数校验
- 配置持久化
- 日志转发
- UI 与 `core` 的桥接逻辑

禁止放入：

- 具体算法实现
- 大量页面控件操作

#### 8.3.3 `src/app/ui` 只负责界面与交互

允许放入：

- 控件布局
- 信号槽连接
- 参数采集
- 日志显示
- 图像与点云展示

禁止放入：

- 直接实现标定、匹配、三维重建算法
- 直接拼装复杂业务流程

#### 8.3.4 `src/app/acquisition` 只负责采集抽象与实现

允许放入：

- 离线图像序列源
- 采集接口定义
- 在线相机占位实现

禁止放入：

- 标定算法
- 重建算法
- 页面 UI 逻辑

#### 8.3.5 依赖方向必须单向

允许依赖：

- `ui -> services -> core`
- `acquisition -> core`

禁止依赖：

- `core -> ui`
- `core -> services`
- `services -> ui`

### 8.4 推荐写法

推荐模式：

- `MainWindow` 负责接收用户操作
- `services` 负责把 UI 参数转换成核心输入对象
- `core` 返回结构化结果
- `ui` 只负责显示结果

### 8.5 禁止写法

禁止示例：

- 在 `MainWindow.cpp` 中直接写角点检测细节
- 在 `ParameterPanel.cpp` 中直接写三维点匹配过程
- 在 `core` 中直接弹 Qt 消息框
- 在 `services` 中直接操作某个具体控件的文本内容

### 8.6 检查清单

代码审查时检查：

1. 新代码是否放在正确目录
2. `core` 是否引入了 UI 依赖
3. UI 是否绕过服务层直写算法流程
4. 服务层是否承担了本应属于 `core` 的算法实现

## 9. 日志规范

### 9.1 目标

日志规范用于保证程序运行状态可追踪、错误可定位、批处理任务可回放，同时避免日志过量和格式混乱。

### 9.2 适用范围

本节适用于：

- `Logger`
- `QtLogSink`
- `core` 层业务日志
- `services` 层任务日志
- `ui` 层状态日志

### 9.3 强制规则

#### 9.3.1 禁止直接使用 `cout`、`printf`、`qDebug`

项目统一走 `Logger` 体系输出日志。

#### 9.3.2 日志级别使用规则固定

- `Debug`：中间状态、调试信息、参数细节、点数、耗时
- `Info`：正常流程开始、完成、导入成功、导出成功
- `Warning`：可恢复问题，例如部分帧失败、数量不一致、ROI 被裁剪
- `Error`：当前操作失败、必要输入无效、关键文件缺失

#### 9.3.3 模块名必须统一

推荐模块名：

- `Calibration`
- `Reconstruction`
- `LaserExtraction`
- `PointCloud`
- `Config`
- `Acquisition`
- `UI`
- `App`

#### 9.3.4 日志消息必须包含有效上下文

根据场景至少包含以下信息之一：

- 文件路径
- 图像名
- 帧号
- 点数
- 参数摘要
- 错误原因

### 9.4 推荐写法

推荐示例：

```cpp
Logger::instance().info("Calibration", "Loaded calibration file: " + filename);
Logger::instance().warning("Reconstruction", "Left/right image counts differ, using minimum pair count.");
Logger::instance().error("PointCloud", "Failed to write point cloud pcd: " + filename);
```

### 9.5 禁止写法

禁止示例：

```cpp
cout << "error" << endl;
qDebug() << "done";
Logger::instance().info("test", "ok");
```

问题在于：

- 模块名无语义
- 消息内容不可定位
- 无法统一展示到 UI

### 9.6 检查清单

代码审查时检查：

1. 是否仍存在 `cout`、`printf`、`qDebug`
2. 日志级别是否使用正确
3. 模块名是否统一
4. 日志消息是否能帮助定位问题
5. 长循环中是否出现过密日志

## 10. 错误处理规范

### 10.1 目标

错误处理规范用于保证程序在异常输入、部分失败和不可恢复错误下有一致行为，避免随机崩溃、静默失败或 UI 无反馈。

### 10.2 适用范围

本节适用于：

- 核心算法服务
- 文件读写
- 配置加载
- 批处理任务
- UI 触发的导入、导出、执行动作

### 10.3 强制规则

#### 10.3.1 不可恢复错误使用异常或明确失败结果

例如：

- 必要文件不存在
- 输出文件无法写入
- 标定文件格式非法
- 核心矩阵尺寸错误

#### 10.3.2 可恢复错误允许记录日志后继续

例如：

- 左右图像数量不一致，按最小配对数继续
- 单帧没有激光线
- 部分标定图角点失败

#### 10.3.3 批处理任务必须区分“整体失败”和“局部失败”

要求：

- 单帧失败不应默认导致整个任务失败
- 但关键输入无效时必须整体中断

#### 10.3.4 禁止 silent fail

任何失败都必须至少满足以下之一：

- 抛出异常
- 返回结构化错误状态
- 写出清晰日志

### 10.4 推荐写法

推荐策略：

- `core`：抛出明确异常或返回结构化失败结果
- `services`：捕获异常，转成任务状态和日志
- `ui`：展示用户可理解的错误提示

### 10.5 禁止写法

禁止示例：

- 捕获异常后什么都不做
- 返回空结果但没有日志
- 在 `core` 里直接弹框
- 在 `ui` 里猜测底层错误原因

### 10.6 检查清单

代码审查时检查：

1. 错误是否被正确分类为可恢复或不可恢复
2. 失败后是否留下可定位信息
3. 批处理是否能继续处理剩余帧
4. `core` 是否与 UI 错误展示解耦

## 11. 配置与参数规范

### 11.1 目标

配置与参数规范用于保持参数模型一致、默认值集中、持久化边界清晰，避免 UI、服务层和算法层各自维护一套不一致参数。

### 11.2 适用范围

本节适用于：

- `Types.h`
- 配置结构体
- 输入输出结构体
- `AppConfigService`
- 项目配置持久化

### 11.3 强制规则

#### 11.3.1 配置对象按职责分类

统一分类：

- `Input`：一次任务输入
- `Config`：算法参数
- `Result`：任务输出
- `ProjectConfig`：工程级持久化配置

#### 11.3.2 默认值统一放在结构体定义处

禁止把同一参数默认值分散写在：

- UI
- service
- core

#### 11.3.3 新增字段必须说明是否持久化

每个新增参数都应明确：

- 是否需要保存到项目配置
- 是否只是本次运行时参数
- 默认值是多少
- 单位或取值范围是什么

#### 11.3.4 运行时状态不得混入持久化配置

例如以下内容不应持久化：

- 当前进度
- 当前预览帧
- 临时调试数据
- 中间缓存结果

### 11.4 推荐写法

推荐模式：

- 通用任务输入进入 `Input`
- 算法阈值与开关进入 `Config`
- 导出路径、最近目录进入 `AppProjectConfig`

### 11.5 禁止写法

禁止示例：

- 同一个阈值在 UI 和 `Types.h` 各写一份默认值
- 直接在窗口类里拼一个匿名参数集合传给 `core`
- 把预览状态和工程配置写进同一个对象

### 11.6 检查清单

代码审查时检查：

1. 新参数是否放入正确的数据结构
2. 默认值是否集中定义
3. 是否明确持久化边界
4. 是否出现重复参数源

## 12. 文件与路径规范

### 12.1 目标

文件与路径规范用于统一离线处理软件中的输入输出组织方式，避免目录混乱、文件被误覆盖、路径含义不明确。

### 12.2 适用范围

本节适用于：

- 输入图像目录
- 输出目录
- 标定文件
- 点云文件
- 调试图
- 临时文件

### 12.3 强制规则

#### 12.3.1 文件路径和目录路径必须命名区分

- 文件使用 `xxxPath`
- 目录使用 `xxxDirectory`

#### 12.3.2 输入路径必须在进入核心流程前完成校验

至少校验：

- 路径是否存在
- 类型是否正确
- 文件是否可读取
- 目录是否为空

#### 12.3.3 输出目录由服务层或工具层统一负责创建

禁止让多个模块各自随意创建输出目录。

#### 12.3.4 标定文件、点云文件、调试图命名应稳定

推荐统一命名风格：

- `stereo_calibration.yml`
- `point_cloud.txt`
- `point_cloud.pcd`
- `left_preview.png`
- `right_preview.png`

#### 12.3.5 图像排序与配对规则必须明确

必须规定：

- 使用稳定排序
- 左右图按同一排序规则配对
- 数量不一致时记录日志并说明处理策略

### 12.4 推荐写法

推荐原则：

- 导入时尽早校验路径
- 输出时集中命名
- 日志中打印关键文件路径
- 对外接口统一传入明确的文件路径或目录路径，不传模糊字符串

### 12.5 禁止写法

禁止示例：

- `path1`、`path2` 这类无语义命名
- 在 `core`、`ui`、`services` 中重复创建同一输出目录
- 导出时默认覆盖文件但没有日志
- 没有校验目录存在就开始处理

### 12.6 检查清单

代码审查时检查：

1. 路径变量是否语义明确
2. 输入输出路径是否有校验
3. 输出文件命名是否稳定
4. 左右图配对规则是否明确
5. 路径错误时是否有清晰反馈

## 13. 当前版本结论

当前版本的 [docs/standard.md](C:/PROJECT/HTMSR/docs/standard.md) 已经明确覆盖以下主题：

- 宏定义规范
- 中文注释规范
- 命名规范
- 目录与分层规范
- 日志规范
- 错误处理规范
- 配置与参数规范
- 文件与路径规范

它现在已经可以作为 HTMSR 项目的代码规范主文档持续使用。后续新增规范时，继续在这份文档中扩展，不再拆分新的临时规范文档。

## 14. Qt / VTK 构建类型规范

### 14.1 目标

本节用于约束 Qt、VTK、PCL 等可视化相关依赖的构建类型，避免 Release 程序误加载 Debug DLL，或 Debug 程序误加载 Release DLL。

该问题在 HTMSR 中曾表现为 Release 启动时报错：

```text
QWidget: Must construct a QApplication before a QWidget
```

根因通常不是 `QApplication` 真的没有创建，而是 Release 程序加载了 `Qt5Cored.dll`、`Qt5Widgetsd.dll`、`vtk...d.dll` 等 Debug 依赖，导致 Qt 全局状态异常。

### 14.2 强制规则

1. Release 构建不得加载任何带 `d` 后缀的 Qt / VTK Debug DLL。
2. Debug 构建不得混用 Release 版 Qt / VTK import lib。
3. `VTK::GUISupportQt` 只有 Debug import lib 时，Release 必须禁用内嵌 VTK 点云视图或退回占位视图。
4. 不允许为了让程序临时启动，把 Debug DLL 复制到 Release 输出目录。
5. 点云显示依赖不可用时，应保证标定、重建、点云导出仍然可用。

### 14.3 推荐写法

在 CMake 中优先按构建类型判断 VTK Qt 支持库是否可用。如果 Release 只能找到 Debug 版 VTK Qt 支持库，应自动关闭 `HTMSR_WITH_VTK_VIEWER`：

```cmake
if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    # Release 不允许链接 Debug 版 VTK Qt 支持库。
    set(HTMSR_WITH_VTK_VIEWER 0)
endif()
```

实际项目中可以根据 imported target 的 `IMPORTED_IMPLIB_RELEASE`、`IMPORTED_LOCATION_RELEASE`、`IMPORTED_IMPLIB_DEBUG` 等属性做更精确判断。

### 14.4 检查清单

每次调整 Qt、VTK、PCL 路径后，至少检查：

1. Release 输出目录是否包含 `Qt5Cored.dll`。
2. Release 输出目录是否包含 `vtk*9.1d.dll`。
3. 运行日志或调试器模块列表中是否加载了 Debug 版 Qt / VTK。
4. 点云视图不可用时，界面是否显示占位提示，而不是直接崩溃。
5. 禁用 VTK 视图后，标定、重建、导出点云是否保持原行为。

### 14.5 文档同步要求

如果后续重新编译或替换 VTK，需要同步更新：

- `README.md`
- `docs/HTMSR_开发环境表.md`
- `docs/prepare.md`
- `docs/环境路径配置脚本说明.md`

这样可以避免新电脑部署时再次出现 Debug / Release 依赖混用问题。
