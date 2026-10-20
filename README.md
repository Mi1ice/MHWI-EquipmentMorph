# MHWI-EquipmentMorph

Monster Hunter: World — Iceborne 装备形态键运行时、Blender 作者插件、公开控制接口和手动控制窗口。

当前正式发行：运行时 0.5.0，Blender 插件及独立作者工具 0.4.4，SDK 0.4.0。两套不同装备的身体模型已完成用户实机验收：三个形态键、释放恢复、换装自动绑定与权重归零、连续往返均正常。作者插件 0.4.4 已通过用户导出测试。验收范围见 [验证说明](docs/VALIDATION.md)。

默认分支 `main` 已合入重绑修复与本轮性能优化；当前源码保留0.5.1-dev12开发基线，包含[无关绘制回调早退](docs/DRAW_FILTER.md)、[输入计时与私有资源诊断](docs/INPUT_DIAGNOSTICS.md)、[相同权重复用](docs/SAME_WEIGHT_REUSE.md)及现有重绑路径。用户确认dev12调节、释放和换装重绑通过，多部位同时非零时帧率略有降低，并结束本轮优化。七段PresentMon对照约为零值205.14 FPS、非零202.24 FPS，未观察到GPUBusy增长，CPU/GPU百分比归因仍未完成；详见[验证结论](docs/DEV12_BASELINE.md)。正式0.5.0标签和Release附件保持原样。当前没有正式0.5.1发行；版本、验收和后续方向统一见[当前项目状态](docs/PROJECT_STATUS.md)。

## 功能

2026-10-19 已暂停 [Tauri 控制窗口候选](controls/MHWI-EquipmentMorph-Desktop/README.md)，并清理旧部署。[可选 Dear ImGui 游戏内面板](controls/MHWI-EquipmentMorph-ImGui/README.md)的第二阶段窗口、快捷键与输入已通过用户实机测试。第三阶段已接入装备下拉及“形态键名／权重滑块／权重输入框／当前权重值”四列，22 项无窗口控制回归通过；2026-10-20 用户确认调节、多部位控制、显隐／释放及换装后的列表更新正常，上述功能实机验收通过。第三阶段当时隐藏保留权重、× 释放控制；第九阶段将 × 统一为隐藏保留。dev12 核心、配置及装备数据保留，Tauri 源码与历史候选保留供追溯。

2026-10-20 已复现并修复初版在交换链创建后加载时无法呼出的故障，修复版已获用户确认。此后的界面测试交由用户完成；助手只执行构建和非界面逻辑验证。

- 从 Blender 的 MOD3 集合联合导出模型与形态键数据，自动保留顶点对应关系。
- 通过本地装备目录选择性别、套装和部位，自动确定资源路径；也支持手动完整路径。装备目录随插件提供，来源与重建方法，见 [装备目录说明](docs/EQUIPMENT_CATALOG.md)。
- 搜索候选形态键，确认后单独配置别名；用独立副本同步预览多个网格的同名键，不改写源键值。
- 默认只导出模型和三个形态键包文件；勾选 Debug 后保留作者与验证资料。公共 `runtime.ini` 随游戏运行时安装，MOD 不携带它。
- 每个 MOD3 对应相邻的同名 `.morph` 目录；修改目标套装 ID 时同步迁移模型路径和包身份。
- 同一装备部位可安装多个模型，游戏换装时自动选择对应数据包，新目标权重从零开始。
- 连续权重、平滑变化、多个键叠加、逐键控制来源和释放，以及失效后重新绑定。
- 独立 Windows 控制窗口和原生 C API；自动表情控制可由外部项目通过 SDK 实现。

2026-10-20 新增快速响应候选：缩短核心平滑时间，并将可见面板的实际权重读回提高至约每 16ms，装备元数据仍约 250ms 扫描。643 项核心原生检查与 33 项面板控制检查通过；候选核心与 GUI 配套测试，用户实机验收待完成，详见[面板说明](controls/MHWI-EquipmentMorph-ImGui/README.md)。

## 使用与开发

2026-10-20 用户确认第八阶段“功能符合描述”。第九阶段加入[穿戴默认与预设编辑](docs/PRESETS.md)：选择预设立即应用，修改后手动保存；按模型与形态键包身份逐部位在启动／换装时自动恢复已保存默认。× 隐藏保留，暂时还原可暂停自动使用；每次启动使用默认布局。只更新可选 GUI，核心、原预设格式与复选框语义保持，新候选待用户实机测试。

运行环境为 Windows x64、已验证的 MHW Iceborne 游戏构建和 DX12，使用 Stracker's Loader。作者侧联合导出已验证 Blender 5.1.2 与源码摘要匹配的 MHW Model Editor 0.12；这些外部依赖需要自行安装。项目不包含游戏模型或 MHW Model Editor。

- [作者操作、同名包和安装流程](docs/USAGE.md)
- [下载安装包](docs/INSTALLATION.md)
- [源码构建与验证](docs/BUILD.md)
- [SDK 与调用示例](sdk/MHWI-EquipmentMorph-0.4.0/README.md)
- [手动控制窗口](controls/MHWI-EquipmentMorph-ControlPanel/README.md)
- [项目结构与来源](docs/REPOSITORY.md)
- [版本记录](CHANGELOG.md)
- [GitHub发布准备](docs/PUBLISHING.md)

源码仓库提供当前代码、必要依赖与合成测试，可安装包通过 GitHub Releases 提供。构建产物写入 `.build/`，发行二进制不进入源码提交。本阶段不兼容旧单包配置，当前配置使用 `format=2`。

## 许可证

项目主代码采用 **GPL-3.0-or-later**，完整条款见 [LICENSE](LICENSE)，适用范围见 [许可证说明](LICENSING.md)。第三方 SafetyHook 与 Zydis 保留各自原始许可证，见 [第三方说明](THIRD_PARTY_NOTICES.md)。
