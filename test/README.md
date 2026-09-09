# 测试素材目录说明

本目录用于集中存放 HTMSR 软件测试素材与测试结果，按用途分为四类：

- `01_calibration`
  标定素材，用于测试双目标定流程是否可以正常运行。
- `02_reconstruction`
  重建素材，用于测试激光中心线提取、左右点匹配和三维重建流程。
- `03_reference_results`
  参考结果，用于和软件输出结果做对比，不是当前程序的直接输入。
- `04_actual_outputs`
  程序实际运行后生成的输出结果，建议将标定文件、点云结果、截图和日志统一放在这里。

## 目录结构

```text
test
├── 01_calibration
│   └── opencv_official_stereo_chessboard
│       ├── left
│       ├── right
│       └── SOURCE_README.txt
├── 02_reconstruction
│   └── laser_scan_sequence_case01
│       ├── left
│       └── right
└── 03_reference_results
    └── point_cloud_txt
        ├── p3d_1.txt
        └── p3d_2.txt
└── 04_actual_outputs
    ├── calibration
    ├── point_cloud
    └── logs_and_screenshots
```

## 在 HTMSR 中的使用方式

### 1. 标定测试

左标定目录：

- `C:\PROJECT\HTMSR\test\01_calibration\opencv_official_stereo_chessboard\left`

右标定目录：

- `C:\PROJECT\HTMSR\test\01_calibration\opencv_official_stereo_chessboard\right`

建议优先尝试参数：

- 棋盘格内角点：`9 x 6`
- 方格尺寸：可先填一个一致测试值，例如 `25 x 25`

说明：

- 这套数据用于测试“标定流程是否跑通”。
- 如果需要真实尺度正确的结果，必须使用真实棋盘格物理尺寸。

### 2. 重建测试

左重建目录：

- `C:\PROJECT\HTMSR\test\02_reconstruction\laser_scan_sequence_case01\left`

右重建目录：

- `C:\PROJECT\HTMSR\test\02_reconstruction\laser_scan_sequence_case01\right`

说明：

- 这套数据是左右相机的激光图像序列。
- 左右文件名一一对应，可直接按排序后配对。
- 该数据不能用于标定，必须配合已有 `stereo_calibration.yml` 使用。

### 3. 参考结果

参考点云目录：

- `C:\PROJECT\HTMSR\test\03_reference_results\point_cloud_txt`

说明：

- `p3d_1.txt`、`p3d_2.txt` 是已有三维点云文本结果。
- 当前 HTMSR 只能导出点云，暂不支持导入这些文件直接显示。
- 它们主要用于和新重建结果做数量级、形状和分布上的对比。

### 4. 程序实际输出

建议将你后续测试软件时生成的内容放到这里：

- 标定输出：
  - `C:\PROJECT\HTMSR\test\04_actual_outputs\calibration`
- 点云输出：
  - `C:\PROJECT\HTMSR\test\04_actual_outputs\point_cloud`
- 截图、日志、说明：
  - `C:\PROJECT\HTMSR\test\04_actual_outputs\logs_and_screenshots`

说明：

- `03_reference_results` 是已有参考结果，不要和你新跑出来的结果混放。
- `04_actual_outputs` 才是本次软件测试过程中真实生成的结果目录。

## 测试建议顺序

建议按下面顺序测试软件：

1. 先用 `01_calibration` 测试双目标定流程能否正常运行。
2. 得到有效的 `stereo_calibration.yml` 后，再用 `02_reconstruction` 测试重建流程。
3. 将本次运行输出保存到 `04_actual_outputs`。
4. 最后将 `04_actual_outputs` 中的新结果与 `03_reference_results` 中的点云做对比。

## 注意事项

- 标定素材和重建素材不要混用。
- 重建时不要一次性先跑完整序列，建议先用较小图像范围测试。
- 如果激光提取效果不好，优先调整 ROI 和阈值，而不是怀疑图像配对本身。
