# 当前版本与开发状态

更新：2026-10-19。

## 正式发行

当前正式发行是 [v0.5.0](https://github.com/Mi1ice/MHWI-EquipmentMorph/releases/tag/v0.5.0)，对应源码提交 `9de8545`，包括运行时 0.5.0、Blender 作者插件及独立作者工具 0.4.4、SDK 0.4.0。安装包名称和公共 `runtime.ini` 的来源见[安装说明](INSTALLATION.md)。

两套身体模型的形态键调节、释放恢复、换装自动绑定与归零、连续往返已完成该正式版的用户实机验收。正式标签和发行附件没有因后续开发合并而改变。

## 当前开发基线

仓库默认分支为 `main`。2026-10-19，[PR #1](https://github.com/Mi1ice/MHWI-EquipmentMorph/pull/1)以 Squash merge 合入，提交为 `effe4561`，包括保持控制时的装备重绑修复和本轮性能优化。

当前源码配置为运行时 `0.5.1-dev12-source`、作者插件 0.4.4、SDK 0.4.0。用户已确认 dev12 调节、释放及保持控制的换装重绑通过，并补测多个部位同时非零，本轮性能优化已结束。非零时仍有轻微帧率开销；同场景采样及 CPU/GPU 归因边界见[dev12 基线](DEV12_BASELINE.md)。

这些结果属于已测试的 dev12 开发载荷。当前没有发布正式 0.5.1，也没有为任意重新编译的载荷自动授予实机验收。本地构建包的 `formal_acceptance_complete` 仍为 false，版本以包内 `manifest.json` 为准。

## 构建与历史记录

[Tauri 控制窗口候选](../controls/MHWI-EquipmentMorph-Desktop/README.md)已于 2026-10-19 暂停。实机出现自动启动后 WebView 初始化停滞；游戏外重启相同 EXE 可恢复，Steam Overlay 注入及启动时序的独立因果尚未确认。按用户选择转向独立、可选的 Dear ImGui 游戏内面板，直接调用现有核心 API。

已归档并清理测试游戏目录中的 Tauri 面板、旧原生面板、控制桥接及旧安装收据和面板启动日志。dev12 核心、runtime.ini 和全部 354 个装备文件共 356 项摘要保持一致；游戏当前为仅核心部署。源码与历史候选保留，本地开发分支为 `codex/imgui-control-panel`，ImGui 尚未实现。首版范围限定为装备下拉选择及“形态键名／权重滑块／权重输入框／当前已应用值”四列，外观优化后置。

开发使用本仓库的 `config/mhwi-equipmentmorph-current.json` 和 `tools/check_source.ps1`，详见[构建说明](BUILD.md)。软件检查和实机验证范围见[验证说明](VALIDATION.md)。

dev1 至 dev11 的设计说明、候选安装步骤和当时的“待复测”状态保留为历史记录；它们不代表当前基线仍需重做整轮测试。以本页和 dev12 基线判断当前状态，以对应标签、发行附件和摘要判断正式二进制。

## 后续方向

- 作者配置保存／加载的必要性待用户评估，尚未排入实现；控制窗口预设也未确定实施顺序。
- 完整 MOD 压缩包迁移工具继续暂缓。
- 自动表情控制仍由独立项目通过 SDK 接入。

正式标签和发行附件未因文档整理或控制窗口开发而改变；作者配置保存／加载仍待评估。
