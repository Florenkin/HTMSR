# -*- coding: utf-8 -*-
import json
import zipfile
from datetime import datetime
from pathlib import Path


OUT = Path(r"C:\PROJECT\HTMSR\docs\HTMSR_Project_Call_Offline_Reconstruction.xmind")


counter = 0


def nid(prefix="id"):
    global counter
    counter += 1
    return f"{prefix}-{counter:04d}"


def topic(title, children=None, notes=None):
    data = {
        "id": nid("topic"),
        "class": "topic",
        "title": title,
    }
    if notes:
        data["notes"] = {"plain": {"content": notes}}
    if children:
        data["children"] = {"attached": children}
    return data


def sheet(title, root_title, children):
    return {
        "id": nid("sheet"),
        "class": "sheet",
        "title": title,
        "rootTopic": topic(root_title, children),
        "topicPositioning": "fixed",
        "extensions": [],
    }


content = [
    sheet("01 工程分层与功能", "HTMSR Qt/VS 项目总览", [
        topic("htmsr_core 纯业务核心", [
            topic("数据契约: Types.h"),
            topic("标定: CalibrationService"),
            topic("线提取: LaserExtractionService"),
            topic("重建: ReconstructionService"),
            topic("点云: PointCloudService"),
            topic("工具: FileSystemUtils / Logger"),
        ]),
        topic("htmsr_app Qt 桌面程序", [
            topic("MainWindow: 菜单、工具栏、Dock、任务入口"),
            topic("ParameterPanel: 项目、标定、重建、采集参数"),
            topic("ImageViewWidget: 左图、右图、调试图显示"),
            topic("PointCloudViewWidget: VTK 可用时显示点云，否则占位"),
            topic("LogPanel: UI 日志表"),
        ]),
        topic("app/services 应用服务", [
            topic("AppConfigService: QSettings 参数持久化"),
            topic("QtLogSink: Logger 消息转 Qt signal"),
        ]),
        topic("app/acquisition 采集预留", [
            topic("ICameraDevice: 在线相机设备接口"),
            topic("IAcquisitionProvider: 帧来源接口"),
            topic("FramePair: 左右帧数据对象"),
            topic("OfflineImageSequenceProvider: 离线图片序列实现"),
        ]),
    ]),
    sheet("02 包含关系", "主要 include / 依赖关系", [
        topic("Types.h 是数据中心", [
            topic("Logger.h 包含 Types.h"),
            topic("FileSystemUtils.h 包含 Types.h"),
            topic("CalibrationService.h 包含 Types.h"),
            topic("LaserExtractionService.h 包含 Types.h"),
            topic("PointCloudService.h 包含 Types.h"),
            topic("ParameterPanel.h 包含 Types.h"),
            topic("AppConfigService.h 包含 Types.h"),
        ]),
        topic("ReconstructionService.h", [
            topic("包含 LaserExtractionService.h"),
            topic("包含 PointCloudService.h"),
            topic("包含 Types.h"),
            topic("说明: 重建服务目前直接依赖线提取和点云合并"),
        ]),
        topic("MainWindow.h", [
            topic("包含 AppConfigService.h"),
            topic("包含 QtLogSink.h"),
            topic("包含 CalibrationService.h"),
            topic("包含 ReconstructionService.h"),
            topic("包含 PointCloudService.h"),
            topic("说明: UI 入口持有核心服务对象和 FutureWatcher"),
        ]),
        topic("OfflineImageSequenceProvider.h", [
            topic("包含 AcquisitionTypes.h"),
            topic("包含 core/Types.h"),
            topic("实现离线图像序列读取，但当前尚未接入 MainWindow 重建入口"),
        ]),
        topic("第三方库边界", [
            topic("OpenCV: 图像读写、标定、去畸变、图像处理"),
            topic("Eigen: 射线、矩阵、三维点计算"),
            topic("PCL: PCD 写出和点云可视化适配"),
            topic("Qt: UI、异步任务、配置持久化"),
            topic("VTK: 可选的 Qt 内嵌点云视图"),
        ]),
    ]),
    sheet("03 UI 调用链", "从用户操作到核心服务", [
        topic("程序启动", [
            topic("main.cpp::main"),
            topic("创建 QApplication"),
            topic("注册 LogMessage 元类型"),
            topic("创建并显示 MainWindow"),
            topic("MainWindow 构造 UI、加载配置、注册日志 Sink"),
        ]),
        topic("保存项目", [
            topic("MainWindow::saveProjectSettings"),
            topic("ParameterPanel::projectConfig"),
            topic("AppConfigService::save"),
            topic("refreshProjectTree"),
            topic("Logger::info"),
        ]),
        topic("加载标定", [
            topic("MainWindow::loadCalibration"),
            topic("QFileDialog 选择 yml/yaml"),
            topic("CalibrationService::loadCalibration"),
            topic("写入 MainWindow::calibration_"),
            topic("失败时 QMessageBox + Logger"),
        ]),
        topic("执行标定", [
            topic("MainWindow::runCalibration"),
            topic("ParameterPanel::calibrationInput"),
            topic("QtConcurrent::run 后台执行"),
            topic("CalibrationService::calibrate"),
            topic("onCalibrationFinished 接收结果"),
            topic("弹窗显示 RMS"),
        ]),
        topic("执行重建", [
            topic("MainWindow::runReconstruction"),
            topic("必要时 loadCalibration"),
            topic("ParameterPanel::reconstructionInput"),
            topic("QtConcurrent::run 后台执行"),
            topic("ReconstructionService::reconstruct"),
            topic("onReconstructionFinished 更新点云和预览图"),
        ]),
        topic("导出点云", [
            topic("MainWindow::exportTxt -> PointCloudService::saveTxt"),
            topic("MainWindow::exportPcd -> PointCloudService::savePcd"),
            topic("空点云时提示用户"),
        ]),
    ]),
    sheet("04 标定流程", "离线双目标定逻辑", [
        topic("输入", [
            topic("左/右标定图像目录"),
            topic("棋盘格内角点尺寸 boardSize"),
            topic("方格物理尺寸 squareSize"),
            topic("图像索引范围 ImageRange"),
            topic("输出文件 stereo_calibration.yml"),
        ]),
        topic("CalibrationService::calibrate", [
            topic("listImageFiles 扫描左右图片"),
            topic("左右数量不一致时取最小配对数并记录 Warning"),
            topic("readImages 读取有效图像"),
            topic("calibrateSingleCamera 左相机单目标定"),
            topic("calibrateSingleCamera 右相机单目标定"),
            topic("逐对 findChessboardCornersSB 检测角点"),
            topic("cornerSubPix 亚像素优化"),
            topic("cv::stereoCalibrate 固定内参求 R/t/E/F"),
            topic("saveCalibration 写出 OpenCV YAML"),
        ]),
        topic("输出 CalibrationResult", [
            topic("K1/D1/K2/D2"),
            topic("P1/P2"),
            topic("R/t/E/F"),
            topic("rms"),
            topic("左右每帧误差"),
            topic("successfulPairs / failedPairs"),
        ]),
    ]),
    sheet("05 离线重建流程", "从左右图片到三维点云", [
        topic("准备阶段", [
            topic("UI 读取左右重建目录和算法参数"),
            topic("检查 calibration_.isValid"),
            topic("无有效标定时尝试加载 calibrationFile"),
            topic("构造 ReconstructionInput"),
        ]),
        topic("批处理阶段 ReconstructionService::reconstruct", [
            topic("校验 CalibrationResult 有效"),
            topic("listImageFiles 扫描左右重建图"),
            topic("左右数量不一致时按最小数量配对"),
            topic("逐帧 imread 左右图"),
            topic("调用 reconstructFrame"),
            topic("记录每帧点数日志"),
        ]),
        topic("单帧阶段 reconstructFrame", [
            topic("LaserExtractionService::extract 左图中心线"),
            topic("LaserExtractionService::extract 右图中心线"),
            topic("保存 leftPreview/rightPreview"),
            topic("空中心线时返回空点集并记录 Warning"),
            topic("cv::undistortPoints 去畸变"),
            topic("OpenCV Mat 转 Eigen 矩阵"),
            topic("pixelToRay 像素转相机射线"),
            topic("用 R/t 建立左右相机几何关系"),
            topic("最近射线/极线误差匹配左右中心线点"),
            topic("closestPointBetweenLines 求空间最近点中点"),
            topic("转换回左相机坐标系得到 Eigen::Vector3d"),
        ]),
        topic("合并与显示", [
            topic("PointCloudService::mergeFrames 合并逐帧点云"),
            topic("onReconstructionFinished 保存 reconstruction_"),
            topic("PointCloudViewWidget::setPoints 显示点云/占位数量"),
            topic("ImageViewWidget::setImage 显示第一帧线提取预览"),
            topic("refreshProjectTree 更新点数"),
        ]),
        topic("导出", [
            topic("saveTxt: x y z 文本"),
            topic("savePcd: PCL binary PCD"),
            topic("ensureParentDirectory 自动创建父目录"),
        ]),
    ]),
    sheet("06 激光中心线提取", "GrayCentroid 与 Steger", [
        topic("公共入口 LaserExtractionService::extract", [
            topic("检查空图"),
            topic("clampRoi 防止 ROI 越界"),
            topic("根据 mode 选择 GrayCentroid 或 Steger"),
        ]),
        topic("灰度重心法 extractGrayCentroid", [
            topic("按 LaserColor 转灰度/取通道"),
            topic("grayThreshold 二值化"),
            topic("形态学开运算去噪"),
            topic("connectedRanges 找连通区域"),
            topic("每行按灰度加权求中心 x"),
            topic("minGray 和 total 阈值过滤"),
            topic("绘制红色中心线预览"),
            topic("可选 trimEndpoints"),
        ]),
        topic("Steger 法 extractSteger", [
            topic("thresholdLaser 得到激光区域"),
            topic("connectedRanges 找候选区域"),
            topic("根据 stripeWidth 生成高斯导数核"),
            topic("计算一阶/二阶导数 dx/dy/dxx/dxy/dyy"),
            topic("Hessian 特征方向求法线"),
            topic("亚像素 offset 过滤"),
            topic("按行可选筛选中间候选点"),
            topic("绘制预览并可选剔除端点"),
        ]),
    ]),
    sheet("07 扩展点与注意事项", "后续开发建议", [
        topic("在线采集扩展", [
            topic("实现 ICameraDevice 接入相机 SDK"),
            topic("实现 IAcquisitionProvider 输出 FramePair"),
            topic("让 ReconstructionService 支持 Provider 或抽象 FrameSource"),
            topic("UI 采集页连接设备、开始采集、状态展示"),
        ]),
        topic("点云素材兼容", [
            topic("新增 PointCloudImportService"),
            topic("支持 txt/pcd/ply/asc"),
            topic("用于 C:/PROJECT/Cases 现有素材查看"),
        ]),
        topic("日志与任务", [
            topic("Logger 当前为单例 + Sink"),
            topic("建议增加任务 ID 和进度回调"),
            topic("QtConcurrent 当前只有完成信号，暂未逐帧进度"),
        ]),
        topic("当前风险", [
            topic("UI 源码中文字符串出现乱码，应统一 UTF-8 编码修复"),
            topic("VTK Qt 组件未找到时点云只能占位显示"),
            topic("重建流程尚未复用 acquisition Provider"),
            topic("匹配逻辑为近似射线误差匹配，后续需用真实数据回归调参"),
        ]),
    ]),
]

metadata = {
    "creator": {"name": "Codex", "version": "GPT-5"},
    "created": datetime.now().isoformat(timespec="seconds"),
    "modified": datetime.now().isoformat(timespec="seconds"),
    "schemaVersion": "2.0",
}

manifest = {
    "file-entries": {
        "content.json": {},
        "metadata.json": {},
        "manifest.json": {},
    }
}

OUT.parent.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(OUT, "w", compression=zipfile.ZIP_DEFLATED) as z:
    z.writestr("content.json", json.dumps(content, ensure_ascii=False, indent=2))
    z.writestr("metadata.json", json.dumps(metadata, ensure_ascii=False, indent=2))
    z.writestr("manifest.json", json.dumps(manifest, ensure_ascii=False, indent=2))

print(OUT)
