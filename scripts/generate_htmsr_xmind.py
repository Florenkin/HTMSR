# -*- coding: utf-8 -*-
"""生成 HTMSR 项目结构与三维重建流程 XMind/PNG 文档。

本脚本不依赖 XMind GUI，直接写入 XMind 2020+ 可识别的 zip 包结构，
并使用 Pillow 生成同名 PNG 预览图。
"""

from __future__ import annotations

import json
import math
import textwrap
import zipfile
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from typing import Iterable

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
DOCS = ROOT / "docs"
PROJECT_XMIND = DOCS / "HTMSR 项目结构图.xmind"
PROJECT_PNG = DOCS / "HTMSR 项目结构图.png"
RECON_XMIND = DOCS / "双目激光三维重建流程.xmind"
RECON_PNG = DOCS / "双目激光三维重建流程.png"


@dataclass
class Node:
    title: str
    children: list["Node"] = field(default_factory=list)
    notes: str = ""


counter = 0


def nid(prefix: str = "id") -> str:
    global counter
    counter += 1
    return f"{prefix}-{counter:04d}"


def n(title: str, children: Iterable[Node] | None = None, notes: str = "") -> Node:
    return Node(title=title, children=list(children or []), notes=notes)


def xmind_topic(node: Node) -> dict:
    data = {
        "id": nid("topic"),
        "class": "topic",
        "title": node.title,
    }
    if node.notes:
        data["notes"] = {"plain": {"content": node.notes}}
    if node.children:
        data["children"] = {"attached": [xmind_topic(child) for child in node.children]}
    return data


def xmind_sheet(title: str, root: Node) -> dict:
    return {
        "id": nid("sheet"),
        "class": "sheet",
        "title": title,
        "rootTopic": xmind_topic(root),
        "topicPositioning": "fixed",
        "extensions": [],
    }


def write_xmind(path: Path, sheets: list[tuple[str, Node]]) -> None:
    global counter
    counter = 0
    now = datetime.now().isoformat(timespec="seconds")
    content = [xmind_sheet(title, root) for title, root in sheets]
    metadata = {
        "creator": {"name": "Codex", "version": "GPT-5"},
        "created": now,
        "modified": now,
        "schemaVersion": "2.0",
    }
    manifest = {
        "file-entries": {
            "content.json": {},
            "metadata.json": {},
            "manifest.json": {},
        }
    }

    path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("content.json", json.dumps(content, ensure_ascii=False, indent=2))
        archive.writestr("metadata.json", json.dumps(metadata, ensure_ascii=False, indent=2))
        archive.writestr("manifest.json", json.dumps(manifest, ensure_ascii=False, indent=2))


def font(size: int, bold: bool = False) -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
    candidates = [
        r"C:\Windows\Fonts\msyhbd.ttc" if bold else r"C:\Windows\Fonts\msyh.ttc",
        r"C:\Windows\Fonts\simhei.ttf",
        r"C:\Windows\Fonts\arial.ttf",
    ]
    for candidate in candidates:
        try:
            return ImageFont.truetype(candidate, size)
        except OSError:
            continue
    return ImageFont.load_default()


def flatten(node: Node, depth: int = 0) -> list[tuple[int, Node]]:
    rows = [(depth, node)]
    for child in node.children:
        rows.extend(flatten(child, depth + 1))
    return rows


def wrap_title(title: str, width: int) -> list[str]:
    if len(title) <= width:
        return [title]
    return textwrap.wrap(title, width=width, break_long_words=False, replace_whitespace=False) or [title]


def draw_sheet_preview(draw: ImageDraw.ImageDraw, sheet_title: str, root: Node, x: int, y: int, width: int) -> int:
    title_font = font(30, True)
    root_font = font(24, True)
    text_font = font(20)
    small_font = font(17)
    accent = "#1f6feb"
    border = "#d0d7de"
    fill = "#f6f8fa"
    line_color = "#8c959f"

    draw.rounded_rectangle((x, y, x + width, y + 54), radius=16, fill="#0f172a")
    draw.text((x + 22, y + 10), sheet_title, fill="white", font=title_font)
    y += 76

    rows = flatten(root)
    row_heights: list[int] = []
    wrapped_rows: list[tuple[int, Node, list[str]]] = []
    for depth, node in rows:
        max_chars = max(12, 42 - depth * 4)
        lines = wrap_title(node.title, max_chars)
        wrapped_rows.append((depth, node, lines))
        row_heights.append(max(42, 24 * len(lines) + 18))

    positions: list[tuple[int, int, int, int]] = []
    current_y = y
    for (depth, node, lines), height in zip(wrapped_rows, row_heights):
        box_x = x + 28 + depth * 44
        box_w = width - 56 - depth * 44
        positions.append((box_x, current_y, box_w, height))
        current_y += height + 12

    for index, ((depth, node, lines), (box_x, box_y, box_w, box_h)) in enumerate(zip(wrapped_rows, positions)):
        if depth > 0:
            parent_index = next(i for i in range(index - 1, -1, -1) if wrapped_rows[i][0] == depth - 1)
            parent = positions[parent_index]
            x1 = parent[0] + 22
            y1 = parent[1] + parent[3]
            x2 = box_x + 22
            y2 = box_y
            draw.line((x1, y1, x1, y2 - 6, x2, y2 - 6, x2, y2), fill=line_color, width=2)

        fill_color = "#dbeafe" if depth == 0 else fill
        outline_color = accent if depth <= 1 else border
        draw.rounded_rectangle((box_x, box_y, box_x + box_w, box_y + box_h), radius=12, fill=fill_color, outline=outline_color, width=2)
        used_font = root_font if depth == 0 else text_font
        text_x = box_x + 16
        text_y = box_y + 10
        for line in lines:
            draw.text((text_x, text_y), line, fill="#111827", font=used_font)
            text_y += 24
        if node.notes:
            draw.text((box_x + box_w - 70, box_y + box_h - 24), "note", fill="#57606a", font=small_font)

    return current_y + 24


def write_png(path: Path, sheets: list[tuple[str, Node]], title: str) -> None:
    preview_width = 1800
    margin = 60
    content_width = preview_width - margin * 2
    heights = []
    for _, root in sheets:
        row_count = len(flatten(root))
        heights.append(100 + row_count * 64)
    total_height = 130 + sum(heights) + len(sheets) * 28
    total_height = min(max(total_height, 900), 12000)

    image = Image.new("RGB", (preview_width, total_height), "#ffffff")
    draw = ImageDraw.Draw(image)
    title_font = font(38, True)
    subtitle_font = font(19)

    draw.rectangle((0, 0, preview_width, 110), fill="#111827")
    draw.text((margin, 28), title, fill="white", font=title_font)
    draw.text((margin, 76), f"Generated: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}", fill="#cbd5e1", font=subtitle_font)

    y = 138
    for sheet_title, root in sheets:
        y = draw_sheet_preview(draw, sheet_title, root, margin, y, content_width)
        if y > total_height - 160:
            break

    path.parent.mkdir(parents=True, exist_ok=True)
    image.save(path)


def project_structure_sheets() -> list[tuple[str, Node]]:
    return [
        ("01 工程总览", n("HTMSR 当前项目结构", [
            n("构建与发布", [
                n("CMakeLists.txt: htmsr_core + htmsr_app"),
                n("CMakePresets.json: 公共 VS2022 x64 preset"),
                n("CMakeUserPresets.json: 本机 local preset"),
                n("scripts/Configure-HtmsrEnvironment.ps1: 环境路径配置"),
                n("scripts/Package-HtmsrRelease.ps1: Debug/Release 打包"),
            ]),
            n("源码分层", [
                n("src/core: 纯 C++ 业务核心"),
                n("src/app/services: Qt 应用服务"),
                n("src/app/ui: Qt Widgets 界面"),
                n("src/app/acquisition: 离线/在线采集抽象"),
            ]),
            n("发布输出", [
                n("out/package/HTMSR_debug: Debug 包，保留 VTK 点云视图"),
                n("out/package/HTMSR_release: Release 包，稳定优先，默认占位点云视图"),
            ]),
        ])),
        ("02 core 业务核心", n("src/core", [
            n("Types.h 数据契约", [
                n("CalibrationInput / CalibrationResult"),
                n("LaserExtractionConfig / LaserExtractionResult"),
                n("ReconstructionInput / ReconstructionResult"),
                n("AppProjectConfig / LogMessage"),
            ]),
            n("CalibrationService", [
                n("单目标定、双目标定"),
                n("OpenCV YAML 读写"),
                n("兼容 stereo_calibration.yml"),
            ]),
            n("LaserExtractionService", [
                n("GrayCentroid 灰度重心法"),
                n("Steger 亚像素中心线"),
                n("ROI / 阈值 / 端点剔除"),
            ]),
            n("ReconstructionService", [
                n("批量左右图配对"),
                n("去畸变、射线匹配、三维点恢复"),
                n("逐帧结果与调试图"),
            ]),
            n("PointCloudService", [
                n("逐帧点云合并"),
                n("TXT 导出"),
                n("PCD 导出"),
            ]),
            n("Logger / FileSystemUtils", [
                n("统一日志 Sink"),
                n("目录创建、图片扫描、路径检查"),
            ]),
        ])),
        ("03 app 桌面层", n("src/app", [
            n("ui", [
                n("MainWindow: 菜单、工具栏、Dock、任务入口"),
                n("ParameterPanel: 项目/标定/重建参数"),
                n("AcquisitionPanel: 在线采集参数与设备列表"),
                n("ImageViewWidget: 左图、右图、调试图"),
                n("PointCloudViewWidget: VTK 视图或占位视图"),
                n("LogPanel: 时间/级别/模块/消息"),
            ]),
            n("services", [
                n("AppConfigService: QSettings 配置持久化"),
                n("QtLogSink: Logger 转 Qt signal"),
                n("AcquisitionService: 采集 Session、保存 left/right"),
            ]),
            n("acquisition", [
                n("ICameraDevice: 相机设备抽象"),
                n("IAcquisitionProvider: 左右帧来源抽象"),
                n("MockAcquisitionProvider: 无相机模拟采集"),
                n("OfflineImageSequenceProvider: 离线图片序列"),
                n("HikCameraDevice: 海康 SDK 适配边界"),
                n("HikStereoCameraProvider: 双海康相机 Provider"),
            ]),
        ])),
        ("04 依赖与开关", n("第三方依赖与构建开关", [
            n("Qt 5.15.2", [
                n("Widgets / Concurrent / OpenGL"),
                n("发布包需要 platforms/qwindows.dll"),
            ]),
            n("OpenCV 4.5.0", [
                n("图像读写、标定、去畸变、图像处理"),
            ]),
            n("Eigen / PCL / VTK", [
                n("Eigen: 几何计算"),
                n("PCL common/io: PCD 保存"),
                n("PCL visualization + VTK Qt: 可选内嵌点云视图"),
            ]),
            n("海康 MVS SDK", [
                n("HTMSR_ENABLE_HIK_CAMERA=ON/OFF"),
                n("MvCameraControl.h / MvCameraControl.lib"),
                n("Runtime DLL/CTI/INI 复制到发布目录"),
            ]),
            n("发布策略", [
                n("Debug: HTMSR_ENABLE_VTK_VIEWER=ON，开发机点云交互"),
                n("Release: HTMSR_ENABLE_VTK_VIEWER=OFF，避免 Debug VTK/Qt 混入"),
                n("脚本检查并清理 Debug DLL"),
            ]),
        ])),
    ]


def reconstruction_sheets() -> list[tuple[str, Node]]:
    return [
        ("01 输入来源", n("双目激光三维重建输入", [
            n("离线目录输入", [
                n("左重建目录"),
                n("右重建目录"),
                n("按文件名排序后最小数量配对"),
            ]),
            n("在线采集闭环", [
                n("AcquisitionPanel 配置左右相机/模拟采集"),
                n("AcquisitionService 创建 capture Session"),
                n("保存 output/capture/<session>/left"),
                n("保存 output/capture/<session>/right"),
                n("自动回填重建目录，复用离线重建流程"),
            ]),
            n("标定与参数", [
                n("stereo_calibration.yml"),
                n("CalibrationResult: K/D/R/t/E/F/P1/P2"),
                n("LaserExtractionConfig: ROI/阈值/模式/线宽"),
                n("matchDistanceThreshold"),
            ]),
        ])),
        ("02 主调用链", n("UI 到 ReconstructionService", [
            n("MainWindow::runReconstruction", [
                n("检查 calibration_.isValid"),
                n("必要时 CalibrationService::loadCalibration"),
                n("ParameterPanel::reconstructionInput"),
                n("QtConcurrent::run 后台任务"),
            ]),
            n("ReconstructionService::reconstruct", [
                n("校验标定结果"),
                n("扫描左右图片"),
                n("数量不一致时取最小配对数并写 Warning"),
                n("逐帧 imread"),
                n("调用 reconstructFrame"),
            ]),
            n("完成回调", [
                n("MainWindow::onReconstructionFinished"),
                n("保存 reconstruction_"),
                n("更新点云视图/占位数量"),
                n("显示左右中心线调试图"),
                n("更新项目树点数"),
            ]),
        ])),
        ("03 单帧重建", n("reconstructFrame 详细流程", [
            n("中心线提取", [
                n("LaserExtractionService::extract 左图"),
                n("LaserExtractionService::extract 右图"),
                n("失败或空中心线: 返回空点集并记录日志"),
            ]),
            n("去畸变与射线", [
                n("cv::undistortPoints"),
                n("像素点转换为归一化相机射线"),
                n("OpenCV Mat 转 Eigen 矩阵"),
            ]),
            n("左右几何匹配", [
                n("使用 R/t 建立左右相机位姿关系"),
                n("按最近射线/极线误差寻找候选匹配"),
                n("误差小于 matchDistanceThreshold 才保留"),
            ]),
            n("三维点恢复", [
                n("closestPointBetweenLines"),
                n("取两条空间射线最近点中点"),
                n("转换回左相机坐标系 Eigen::Vector3d"),
            ]),
        ])),
        ("04 中心线算法", n("LaserExtractionService", [
            n("公共前处理", [
                n("空图检查"),
                n("clampRoi 防止越界"),
                n("按 LaserExtractionMode 分支"),
            ]),
            n("GrayCentroid", [
                n("按激光颜色取灰度/通道"),
                n("阈值分割和形态学去噪"),
                n("逐行连通区域"),
                n("灰度加权中心 x"),
                n("minGray/线宽/端点过滤"),
            ]),
            n("Steger", [
                n("激光区域阈值"),
                n("高斯导数核"),
                n("一阶/二阶导数"),
                n("Hessian 法线方向"),
                n("亚像素 offset 过滤"),
            ]),
        ])),
        ("05 输出与发布差异", n("结果显示与导出", [
            n("点云合并", [
                n("PointCloudService::mergeFrames"),
                n("逐帧点云合并为 mergedPointCloud"),
            ]),
            n("UI 显示", [
                n("Debug 包: VTK Qt 组件启用时可旋转缩放点云"),
                n("Release 包: 默认关闭 VTK Viewer，显示占位数量"),
                n("左图/右图/调试图显示中心线预览"),
            ]),
            n("文件导出", [
                n("TXT 导出: point_cloud.txt，x y z 文本"),
                n("PCD 导出: point_cloud.pcd，PCL binary PCD"),
                n("导出前检查空点云"),
            ]),
            n("日志", [
                n("逐帧点数"),
                n("空图/ROI 越界/无中心线/标定无效"),
                n("左右数量不一致提示"),
            ]),
        ])),
    ]


def validate_xmind(path: Path, expected_keywords: list[str]) -> None:
    with zipfile.ZipFile(path) as archive:
        names = set(archive.namelist())
        required = {"content.json", "metadata.json", "manifest.json"}
        missing = required - names
        if missing:
            raise RuntimeError(f"{path} missing entries: {sorted(missing)}")
        content = archive.read("content.json").decode("utf-8")
    for keyword in expected_keywords:
        if keyword not in content:
            raise RuntimeError(f"{path} missing keyword: {keyword}")


def main() -> None:
    project = project_structure_sheets()
    reconstruction = reconstruction_sheets()

    write_xmind(PROJECT_XMIND, project)
    write_png(PROJECT_PNG, project, "HTMSR 项目结构图")
    write_xmind(RECON_XMIND, reconstruction)
    write_png(RECON_PNG, reconstruction, "双目激光三维重建流程")

    validate_xmind(PROJECT_XMIND, ["HikCameraDevice", "Package-HtmsrRelease.ps1", "HTMSR_debug", "HTMSR_release"])
    validate_xmind(RECON_XMIND, ["AcquisitionService", "reconstructFrame", "GrayCentroid", "Steger", "TXT", "PCD"])

    print(PROJECT_XMIND)
    print(PROJECT_PNG)
    print(RECON_XMIND)
    print(RECON_PNG)


if __name__ == "__main__":
    main()
