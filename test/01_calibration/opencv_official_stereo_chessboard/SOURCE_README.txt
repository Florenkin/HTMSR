OpenCV 官方双目标定样例

来源：
- GitHub: https://github.com/opencv/opencv/tree/4.x/samples/data

当前目录结构：
- left: 左相机棋盘格图像
- right: 右相机棋盘格图像

已下载图像对：
- left01/right01
- left02/right02
- left03/right03
- left04/right04
- left05/right05
- left06/right06
- left07/right07
- left08/right08
- left09/right09
- left11/right11
- left12/right12
- left13/right13
- left14/right14

说明：
- 这套数据适合作为“软件流程是否能跑通”的双目标定测试素材。
- 当前 HTMSR 支持 jpg 图像，因此可直接作为左右标定目录使用。
- 建议优先尝试棋盘格内角点参数：9 x 6。
- 如果只是验证标定流程是否正常运行，方格物理尺寸可先填一个一致的测试值，例如 25 x 25。
- 如果后续需要真实尺度正确的重建结果，则必须使用真实棋盘格的物理尺寸。

在 HTMSR 中的使用方式：
- 左标定目录：
  C:\PROJECT\HTMSR\cases\opencv_stereo_calib\left
- 右标定目录：
  C:\PROJECT\HTMSR\cases\opencv_stereo_calib\right

注意：
- 该数据仅用于标定测试，不是激光重建图像。
- 激光重建仍应使用 cases 下 LeftCamera(...) / RightCamera(...) 那组图像序列。
