# OpenCorr-GUI_3.0 项目中文深度说明

## 1. 项目定位

这个仓库的核心不是一个完整 GUI 应用源码，而是一个面向 **2D DIC**、**Stereo / 3D DIC**、**DVC** 的 C++ 算法库与示例集合。

如果你的目标是“学习借鉴这个开源项目，然后做自己的项目”，那么最值得研究的部分是：

- `src/` 中的对象设计和算法实现
- `examples/` 中的完整处理链路
- `gpu_lib/` 中 CPU/GPU 分层接口思路

需要先明确三件事：

- 仓库名里有 `GUI_3.0`，但这里不是完整 GUI 前端工程。
- 这里的主价值在于算法内核、数据结构、示例和工程组织方式。
- 这个项目更像“研究型算法 SDK”，而不是“现成产品框架”。

## 2. 项目适用范围

OpenCorr 适合这些场景：

- 2D 平面位移与应变测量
- 双目表面三维重建与三维位移测量
- 体数据位移场分析与 DVC
- 大变形、旋转、复杂局部形变条件下的测量算法研究
- DIC / DVC 软件原型开发
- 动态测量系统中的高精度离线复核模块

更具体一些，它适合：

- 实验力学软件
- 材料拉伸/压缩/疲劳过程测量
- 双目表面形变测量
- CT 或显微体数据位移分析
- 高精度传统算法基线构建

## 3. 仓库结构

```text
OpenCorr-GUI_3.0/
|- src/                      核心算法、对象、公共模块
|- examples/                 示例程序
|- gpu_lib/                  GPU 预编译接口库
|- img/                      文档图片
|- README.md                 英文总览
|- README_ZH.md              本文
|- 1_Get_started*.md         入门说明
|- 2_Framework*.md           框架说明
|- 3_Data_structures*.md     数据结构说明
|- 4_Processing_methods*.md  方法说明
|- 5_GPU_acceleration*.md    GPU 说明
|- 6_Examples*.md            示例说明
|- 7_Software_with_GUI*.md   GUI 使用说明
```

统一导出头文件是 [opencorr.h](/D:/PROJECT/OpenCorr-GUI_3.0/src/opencorr.h)。

## 4. 项目架构

从代码组织上，这个项目可以拆成 5 层。

### 4.1 基础数学与内存层

代表文件：

- [oc_point.h](/D:/PROJECT/OpenCorr-GUI_3.0/src/oc_point.h)
- [oc_array.h](/D:/PROJECT/OpenCorr-GUI_3.0/src/oc_array.h)

职责：

- 定义 `Point2D` / `Point3D`
- 定义小矩阵别名
- 提供 2D/3D/4D 连续数组分配工具

这是整个项目的最底层数值支撑。

### 4.2 图像、体数据与局部窗口层

代表文件：

- [oc_image.h](/D:/PROJECT/OpenCorr-GUI_3.0/src/oc_image.h)
- [oc_subset.h](/D:/PROJECT/OpenCorr-GUI_3.0/src/oc_subset.h)
- [oc_calibration.h](/D:/PROJECT/OpenCorr-GUI_3.0/src/oc_calibration.h)

职责：

- 读入和存储 2D/3D 数据
- 构造 subset 局部窗口
- 管理标定、畸变、投影矩阵和反畸变映射

### 4.3 公共数值能力层

代表文件：

- [oc_gradient.h](/D:/PROJECT/OpenCorr-GUI_3.0/src/oc_gradient.h)
- [oc_interpolation.h](/D:/PROJECT/OpenCorr-GUI_3.0/src/oc_interpolation.h)
- [oc_cubic_bspline.h](/D:/PROJECT/OpenCorr-GUI_3.0/src/oc_cubic_bspline.h)
- [oc_nearest_neighbor.h](/D:/PROJECT/OpenCorr-GUI_3.0/src/oc_nearest_neighbor.h)
- [oc_sift.h](/D:/PROJECT/OpenCorr-GUI_3.0/src/oc_sift.h)

职责：

- 梯度计算
- 亚像素 / 亚体素插值
- 邻域搜索
- 稀疏特征提取与匹配

### 4.4 DIC / DVC 求解层

代表文件：

- [oc_dic.h](/D:/PROJECT/OpenCorr-GUI_3.0/src/oc_dic.h)
- [oc_fftcc.h](/D:/PROJECT/OpenCorr-GUI_3.0/src/oc_fftcc.h)
- [oc_feature_affine.h](/D:/PROJECT/OpenCorr-GUI_3.0/src/oc_feature_affine.h)
- [oc_icgn.h](/D:/PROJECT/OpenCorr-GUI_3.0/src/oc_icgn.h)
- [oc_iclm.h](/D:/PROJECT/OpenCorr-GUI_3.0/src/oc_iclm.h)
- [oc_nr.h](/D:/PROJECT/OpenCorr-GUI_3.0/src/oc_nr.h)
- [oc_epipolar_search.h](/D:/PROJECT/OpenCorr-GUI_3.0/src/oc_epipolar_search.h)
- [oc_stereovision.h](/D:/PROJECT/OpenCorr-GUI_3.0/src/oc_stereovision.h)
- [oc_strain.h](/D:/PROJECT/OpenCorr-GUI_3.0/src/oc_strain.h)

职责：

- 粗初始化
- 精配准
- 双目匹配与重建
- 应变后处理

### 4.5 应用组装层

代表目录：

- [examples](/D:/PROJECT/OpenCorr-GUI_3.0/examples)

职责：

- 构造 POI
- 设置参数
- 组织处理链路
- 导出结果

这一层最适合你借鉴去搭自己的软件流程。

## 5. 主要对象类型

### 5.1 `Point2D` / `Point3D`

文件：

- [oc_point.h](/D:/PROJECT/OpenCorr-GUI_3.0/src/oc_point.h)

作用：

- 坐标点
- 小向量
- 基础几何运算对象

特点：

- 非常轻量
- 运算直接
- 适合在热路径中使用

### 5.2 `Image2D` / `Image3D` / `ColorfulImage2D`

文件：

- [oc_image.h](/D:/PROJECT/OpenCorr-GUI_3.0/src/oc_image.h)

作用：

- 2D 图像输入
- 3D 体数据输入
- 彩色图像处理支持

特点：

- `Image2D` 同时保留 `cv::Mat` 和 `Eigen::MatrixXf`
- `Image3D` 直接使用 `float***` 体数据

这说明项目非常重视“数值模块”和“图像模块”的双生态兼容。

### 5.3 `Subset2D` / `Subset3D`

文件：

- [oc_subset.h](/D:/PROJECT/OpenCorr-GUI_3.0/src/oc_subset.h)

作用：

- 从图像或体数据中提取局部窗口
- 零均值归一化
- 作为相关计算的最小工作单元

这是理解 DIC/DVC 的关键对象。

### 5.4 `Deformation2D1` / `Deformation2D2` / `Deformation3D1`

文件：

- [oc_deformation.h](/D:/PROJECT/OpenCorr-GUI_3.0/src/oc_deformation.h)

作用：

- 保存位移和位移梯度
- 维护 warp matrix
- 执行局部坐标变换

要点：

- 一阶模型表示平移与线性梯度
- 二阶模型增加曲率项
- 同时保留参数形式和矩阵形式，便于不同更新策略使用

### 5.5 `POI2D` / `POI2DS` / `POI3D`

文件：

- [oc_poi.h](/D:/PROJECT/OpenCorr-GUI_3.0/src/oc_poi.h)

这是整个项目最核心的数据对象。

每个 POI 通常包含：

- 位置
- deformation
- result
- strain
- subset 半径

这个设计的意义非常大：

- 初始化模块写 POI
- 精配准模块继续在原 POI 上覆盖更新
- 应变模块再读写同一批 POI

整个项目的数据流都围绕 POI 展开。

## 6. POI 结果组织方式

### 6.1 POI 为什么这样设计

OpenCorr 不是把位移、应变、质量指标拆成很多松散数组，而是把“一个采样点的所有状态”聚合在一个对象里。

这意味着：

- 一个 POI 就是一条完整数据记录
- 一个算法模块只需要接 POI 队列
- 模块之间无需复杂格式转换

这是一种非常适合工程落地的设计。

### 6.2 `poi` 这个对象有哪些成员

不同类型略有差异，但核心成员是一致的。

以 `POI2D` 为例：

- 位置：`x, y`
- `deformation`
- `result`
- `strain`
- `subset_radius`

以 `POI2DS` 为例，额外有：

- `ref_coor`
- `tar_coor`
- 三维位移 `u, v, w`

以 `POI3D` 为例：

- 位置升级为 `x, y, z`
- deformation 是 3D 形函数参数
- strain 是 3D 应变分量

### 6.3 `result` 里是什么数据

`result` 不是最终物理量，而是 **算法过程状态 + 质量指标**。

例如 `Result2D` 中主要包含：

- `u0, v0`：初始位移
- `zncc`：相关质量
- `iteration`：迭代次数
- `convergence`：最终收敛量
- `feature`：特征支持信息

它的作用是：

- 判断结果是否可信
- 筛除失败点
- 为下一阶段算法提供参考
- 导出时保留可解释性

### 6.4 `poi` 这个类能做什么

严格来说，POI 本身不是“算法类”，而是“状态容器”。

它能做的是：

- 承载输入位置
- 保存算法写入的 deformation
- 保存算法写入的 result
- 保存 strain 后处理结果
- 作为算法之间传递的数据主线

所以它最重要的能力不是“计算”，而是“组织整个处理流程的数据”。

## 7. 功能实现链路

### 7.1 2D DIC 典型链路

参考：

- [test_2d_dic_fftcc_icgn1.cpp](/D:/PROJECT/OpenCorr-GUI_3.0/examples/test_2d_dic_fftcc_icgn1.cpp)

流程通常是：

1. 读入参考图与目标图
2. 构造 POI 网格
3. `FFTCC2D` 给出粗位移
4. `ICGN2D1` / `ICLM2D1` 做精配准
5. 导出位移表和场图

复杂场景下，也可能是：

1. `SIFT2D`
2. `FeatureAffine2D`
3. `ICGN2D2` / `ICLM2D2`
4. `Strain`

### 7.2 Stereo / 3D DIC 典型链路

参考：

- [test_3d_reconstruction_sift_epipolar.cpp](/D:/PROJECT/OpenCorr-GUI_3.0/examples/test_3d_reconstruction_sift_epipolar.cpp)

流程通常是：

1. 读左右图像
2. 设置标定参数
3. `Calibration::prepare()` 预建反畸变映射
4. `SIFT2D + FeatureAffine2D` 做第一轮引导匹配
5. `ICGN2D2` 精配准
6. 用高质量点估计视差模型
7. `EpipolarSearch` 对低质量点补匹配
8. `Stereovision` 做三角重建
9. 输出 `POI2DS`

### 7.3 DVC 典型链路

参考：

- [test_dvc_fftcc_icgn1.cpp](/D:/PROJECT/OpenCorr-GUI_3.0/examples/test_dvc_fftcc_icgn1.cpp)

流程通常是：

1. 读参考体和目标体
2. 构造 3D POI 网格
3. `FFTCC3D` 做粗初始化
4. `ICGN3D1` 精配准
5. 输出 3D 表格或二进制矩阵

## 8. 数据存储和调用逻辑

整个项目的数据主线非常统一：

```text
Image / Volume
-> POI queue
-> Initializer
-> High-accuracy solver
-> Optional reconstruction / strain
-> IO export
```

更细化地说：

1. 图像层负责输入原始数据
2. POI 队列负责承载采样点状态
3. 初始化模块写入 `poi.deformation`
4. 精配准模块更新 `poi.deformation` 和 `poi.result`
5. 应变模块写入 `poi.strain`
6. IO 模块导出表格或场图

这套组织方式有几个明显优点：

- 模块边界清晰
- 数据流清晰
- 新算法容易插入
- 便于做流程级调试

## 9. 核心模块特点与优势

### 9.1 FFTCC

特点：

- 快
- 适合做粗初始化
- 提供整数位移级估计

适用：

- 纹理良好
- 中小位移
- 需要快速初值

### 9.2 FeatureAffine

特点：

- 用局部特征匹配拟合仿射变换
- 比 FFTCC 更适合大变形和旋转

适用：

- 大位移
- 复杂局部变形
- 初值困难场景

### 9.3 ICGN

特点：

- 项目主力精配准方法
- 参考侧梯度和 Hessian 可预计算
- 精度高、效率好

### 9.4 ICLM

特点：

- 在 ICGN 基础上增加阻尼
- 收敛更稳
- 对困难点更友好

### 9.5 NR

特点：

- 经典 Newton-Raphson 形式
- 每轮都重建目标梯度和 Hessian
- 计算代价更大

更适合研究和对比，而不是主力工程路径。

### 9.6 EpipolarSearch

特点：

- 把双目搜索从 2D 降成极线 1D 搜索
- 再用局部 ICGN 精化

### 9.7 Stereovision

特点：

- 用标定参数构建投影矩阵
- 对匹配点做三角重建

### 9.8 Strain

特点：

- 通过邻域位移场拟合得到应变
- 不是单点直接差分
- 更适合工程测量输出

## 10. 整个项目的优势

最值得借鉴的优点有这些：

1. 数据对象统一，尤其是 POI 主线非常清晰。
2. 初始化、精配准、重建、应变、导出分层明确。
3. 支持 2D、stereo、DVC 三条路线，但架构思想统一。
4. 公共梯度、插值、邻域搜索模块复用度高。
5. 示例完整，方便看清实际调用链。
6. CPU/GPU 接口风格接近，便于扩展高性能后端。
7. 研究价值和工程价值兼具。

## 11. `gpu_lib/` 是干嘛的

目录：

- [gpu_lib](/D:/PROJECT/OpenCorr-GUI_3.0/gpu_lib)

关键文件：

- [opencorr_gpu.h](/D:/PROJECT/OpenCorr-GUI_3.0/gpu_lib/opencorr_gpu.h)
- `OpenCorrGPU.lib`

这部分不是 GPU 源码，而是 **GPU 预编译接口库**。

它的作用是：

- 对外暴露稳定的 C++ 调用接口
- 内部隐藏真实 GPU 实现
- 让外部程序像调用 CPU 类一样调用 GPU 版 ICGN

当前主要暴露：

- `ICGN2D1GPU`
- `ICGN2D2GPU`
- `ICGN3D1GPU`

设计上很值得借鉴的点：

- API 与 CPU 版求解器尽量一致
- 输入使用扁平缓冲区 `Img2D` / `Img3D`
- 内部实现通过 `void* _self` 隐藏

这非常适合你以后做自己的动态测量软件：

- UI 和业务逻辑不依赖 CUDA 细节
- CPU/GPU 可切换
- 二进制发布更方便

## 12. 你现在要开发动态测量软件，可以怎么利用这个库

如果你的方向是动态测量软件，那么 OpenCorr 最适合被当成：

- 高精度计算内核参考
- 离线复核引擎
- 实时估计后的精修模块

### 12.1 作为离线高精度基线

适合：

- 实时系统已经有快速位移估计
- 需要高精度离线验证

建议：

- 实时链路用轻量方法
- 离线链路借鉴 `FFTCC / FeatureAffine + ICGN / ICLM + Strain`

### 12.2 作为精修模块

适合：

- 上游已有光流、模板匹配、深度学习粗位移

建议：

- 上游给粗初值
- 下游用 ICGN / ICLM 做局部高精度 refinement

这对动态测量尤其有价值，因为：

- 快速方法负责实时性
- OpenCorr 风格模块负责精度

### 12.3 作为双目动态测量内核

适合：

- 双目表面位移测量
- 动态三维重建

建议重点借鉴：

- `Calibration`
- `EpipolarSearch`
- `Stereovision`
- `POI2DS`

### 12.4 作为 GPU 分层设计参考

适合：

- 你未来要做 GPU 加速

建议借鉴：

- 统一 CPU / GPU 接口
- 抽象输入 buffer
- 隐藏底层实现

## 13. 开发自己项目时需要注意的点

### 13.1 不要直接把这个项目当成完整产品框架照搬

要学的是：

- 数据组织思路
- 算法分层方式
- 模块边界

而不是完全照抄目录和实现细节。

### 13.2 手工内存管理较多

尤其 3D 数据、插值系数、缓存实例里比较明显。

如果你做长期项目，建议后续逐步改成：

- 更明确的 RAII 封装
- 更稳定的 buffer/view 抽象
- 更好的生命周期管理

### 13.3 POI 就地更新很高效，但要注意可调试性

优点：

- 少拷贝
- 流程简洁

缺点：

- 多阶段流程里不容易回溯某一步改坏了什么

建议：

- 保留 POI 主线
- 但增加阶段性快照、日志或状态标志

### 13.4 多线程缓存要小心

你会看到项目里很多求解器用了线程实例池。

这说明：

- subset cache
- Hessian cache
- 插值器临时缓存

都不适合直接跨线程共享写入。

如果你扩展自己的模块，这一点必须延续。

### 13.5 标定模块必须是一等公民

在双目和动态三维测量里，标定不是小工具，而是核心模块。

你必须明确管理：

- 标定参数版本
- 畸变模型
- 坐标系定义
- 投影矩阵构造方式

## 14. 推荐阅读顺序

如果你准备系统学习源码，推荐按这个顺序读。

### 第一阶段：先看数据骨架

1. [oc_point.h](/D:/PROJECT/OpenCorr-GUI_3.0/src/oc_point.h)
2. [oc_array.h](/D:/PROJECT/OpenCorr-GUI_3.0/src/oc_array.h)
3. [oc_image.h](/D:/PROJECT/OpenCorr-GUI_3.0/src/oc_image.h)
4. [oc_subset.h](/D:/PROJECT/OpenCorr-GUI_3.0/src/oc_subset.h)
5. [oc_deformation.h](/D:/PROJECT/OpenCorr-GUI_3.0/src/oc_deformation.h)
6. [oc_poi.h](/D:/PROJECT/OpenCorr-GUI_3.0/src/oc_poi.h)
7. [oc_dic.h](/D:/PROJECT/OpenCorr-GUI_3.0/src/oc_dic.h)

目标：

- 明白项目的基础对象
- 明白 POI 在项目中的中心地位

### 第二阶段：看公共能力层

1. [oc_gradient.h](/D:/PROJECT/OpenCorr-GUI_3.0/src/oc_gradient.h)
2. [oc_interpolation.h](/D:/PROJECT/OpenCorr-GUI_3.0/src/oc_interpolation.h)
3. [oc_cubic_bspline.h](/D:/PROJECT/OpenCorr-GUI_3.0/src/oc_cubic_bspline.h)
4. [oc_nearest_neighbor.h](/D:/PROJECT/OpenCorr-GUI_3.0/src/oc_nearest_neighbor.h)
5. [oc_calibration.h](/D:/PROJECT/OpenCorr-GUI_3.0/src/oc_calibration.h)

目标：

- 明白 solver 依赖哪些底层能力

### 第三阶段：看初始化

1. [oc_fftcc.h](/D:/PROJECT/OpenCorr-GUI_3.0/src/oc_fftcc.h)
2. [oc_feature.h](/D:/PROJECT/OpenCorr-GUI_3.0/src/oc_feature.h)
3. [oc_sift.h](/D:/PROJECT/OpenCorr-GUI_3.0/src/oc_sift.h)
4. [oc_feature_affine.h](/D:/PROJECT/OpenCorr-GUI_3.0/src/oc_feature_affine.h)

目标：

- 明白初值是怎么来的

### 第四阶段：看精配准

1. [oc_icgn.h](/D:/PROJECT/OpenCorr-GUI_3.0/src/oc_icgn.h)
2. [oc_iclm.h](/D:/PROJECT/OpenCorr-GUI_3.0/src/oc_iclm.h)
3. [oc_nr.h](/D:/PROJECT/OpenCorr-GUI_3.0/src/oc_nr.h)

目标：

- 明白主求解器的差异与取舍

### 第五阶段：看双目与三维

1. [oc_epipolar_search.h](/D:/PROJECT/OpenCorr-GUI_3.0/src/oc_epipolar_search.h)
2. [oc_stereovision.h](/D:/PROJECT/OpenCorr-GUI_3.0/src/oc_stereovision.h)
3. `examples/test_3d_reconstruction_*.cpp`

### 第六阶段：看后处理和导出

1. [oc_strain.h](/D:/PROJECT/OpenCorr-GUI_3.0/src/oc_strain.h)
2. [oc_io.h](/D:/PROJECT/OpenCorr-GUI_3.0/src/oc_io.h)

### 第七阶段：最后看 GPU 接口

1. [opencorr_gpu.h](/D:/PROJECT/OpenCorr-GUI_3.0/gpu_lib/opencorr_gpu.h)
2. `5_GPU_acceleration_CN.md`

## 15. 最值得你借鉴的 8 点

如果只提炼最核心的借鉴点，我建议你重点吸收这些：

1. 用 `POI` 作为统一数据主线。
2. 用 `Subset` 作为局部计算窗口。
3. 把“粗初始化”和“高精度精配准”严格分开。
4. 保留多种 initializer 并允许组合。
5. 用公共梯度/插值模块服务多个 solver。
6. 把双目几何、极线约束、重建拆成独立模块。
7. 把结果导出和可视化逻辑从 solver 中剥离。
8. 让 CPU/GPU 接口尽量一致。

## 16. 总结

OpenCorr 最强的地方不是“实现了几个算法名字”，而是它把：

- 数据对象
- 初始化方法
- 精配准方法
- 双目重建
- 应变后处理
- 导出层

组织成了一条非常清楚的数据链。

如果你准备做自己的动态测量或 DIC/DVC 软件，最应该学习的是这三件事：

1. 它怎样围绕 `POI` 组织整个流程。
2. 它怎样把初始化和精配准分层。
3. 它怎样让 2D、stereo、DVC 共用一套架构思想。
