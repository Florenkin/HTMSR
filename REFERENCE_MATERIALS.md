# 参考资料

原 `src/references` 不参与 HTMSR 构建，已完整移动到本机 `reference-archives/references`，共 308 个文件。移动前后逐文件校验 SHA-256，清单保存为 `reference-archives/manifest.json`。

该目录由 Git 忽略，复制项目时如仍需参考资料，请单独复制整个 `reference-archives`。许可证保留在资料原目录内。历史版本仍可从整理前的提交 `23da9d3` 的 `src/references` 找回；本次不改写 Git 历史。

资料包括 OpenCorr 源码、说明、示例数据、视频、示例程序及早期扫描实现。它们不是当前软件功能入口，也不是当前项目的测试代码。当前开发与测试以 `src/app`、`src/core` 和 `test` 为准。
