# MHWI-EquipmentMorph

Monster Hunter: World — Iceborne 装备形态键运行时、Blender 作者插件及公开控制接口，提供可选的 Dear ImGui 游戏内面板。

当前正式版本为 [v0.6.0](https://github.com/Mi1ice/MHWI-EquipmentMorph/releases/tag/v0.6.0)。Blender 作者插件与独立作者工具保持 **0.4.4**，SDK 保持 **0.4.0**。本轮变更见 [PR #2](https://github.com/Mi1ice/MHWI-EquipmentMorph/pull/2)、[版本记录](CHANGELOG.md)和[发行说明](docs/RELEASE_0.6.0.md)。

## 游戏内使用

- 核心运行时独立提供形态键支持及 C API；可选 ImGui 插件提供面板和预设后台。
- 按 **Del** 打开／关闭面板。选择当前装备后，可用滑块或精确输入调节形态键，支持多个装备部位。文字编辑期间 Del 保留删除功能，支持中文输入法。
- 选择整套预设立即预览；编辑后通过“保存当前”写回所选预设，或“另存为”建立新预设。关闭面板会结束预览并恢复各部位已应用的预设；未保存修改会提示保存、放弃或取消。
- “应用预设”建立启动／换装后的自动应用关联；“取消应用”移除关联。“启用捏人”总开关只控制本面板的贡献。
- 预设支持匹配筛选、搜索、重命名、回收、永久删除及导入／导出。匹配同时核验资源路径、MOD3 与形态键包指纹。
- 连续权重支持平滑变化、多键叠加和逐键控制来源。换装后按当前资源重新绑定；其他插件可通过 SDK 独立接入。

0.6.0 提供 `-core` 和 `-imgui` 两种组件包。安装、添加组件、回退、卸载及公共 `runtime.ini` 的来源见[下载安装](docs/INSTALLATION.md)和[组件安装说明](docs/IMGUI_INSTALLATION.md)。预设操作见[预设说明](docs/PRESETS.md)。本次不提供开发测试期旧预设／配置迁移。

运行环境为 Windows x64、已验证的 MHW Iceborne 构建、DX12 和 Stracker's Loader。MOD 需要相邻的模型与形态键数据；项目不包含游戏或用户模型素材。

## 作者工具

- Blender 插件联合导出 MOD3 模型和三个形态键包文件，保留顶点对应关系；支持装备目录选择、源键确认、别名及独立副本预览。
- 已验证 Blender 5.1.2 与匹配适配摘要的 MHW Model Editor 0.12；外部依赖需自行安装。
- 独立作者工具用于检查已导出资源、安装 MOD 及调整装备资源编号。运行包 `Tools/` 包含同套工具，普通面板用户无需另行下载。

作者操作见[使用说明](docs/USAGE.md)和[装备目录](docs/EQUIPMENT_CATALOG.md)。

## 开发与验证

完整无界面验证入口为 `tools/check_source.ps1 -ImGui`，组件打包入口为 `tools/package_source.py --core-only` 或 `--imgui`。构建产物位于 `.build/`，不提交到源码仓库。

用户已确认调节、多部位控制、预设、自动恢复、中文输入与跨屏输入修正正常；软件检查和实机范围分别记录，不将新构建自动视为已全面验收。本轮性能优化已结束，现有结果及归因限制见[dev12 基线](docs/DEV12_BASELINE.md)。

- [源码构建与验证](docs/BUILD.md)
- [当前项目状态](docs/PROJECT_STATUS.md)与[验证范围](docs/VALIDATION.md)
- [SDK 与调用示例](sdk/MHWI-EquipmentMorph-0.4.0/README.md)
- [项目结构与来源](docs/REPOSITORY.md)
- [发行准备](docs/PUBLISHING.md)

Tauri 与旧原生控制窗口源码保留作历史；0.6.0 组件包不安装这些窗口或控制桥接。

## 许可证

项目主代码采用 **GPL-3.0-or-later**，完整条款见 [LICENSE](LICENSE)，适用范围见[许可证说明](LICENSING.md)。第三方组件保留各自许可证，见[第三方说明](THIRD_PARTY_NOTICES.md)。
