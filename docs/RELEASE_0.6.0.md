# 0.6.0 发行说明

运行时 **0.6.0**，Blender作者插件及独立作者工具 **0.4.4**，SDK **0.4.0**。发行标签为 [v0.6.0](https://github.com/Mi1ice/MHWI-EquipmentMorph/releases/tag/v0.6.0)。

## 主要变化

- 新增可选Dear ImGui游戏内面板，按 **Del** 呼出；支持多个装备部位、形态键滑块与精确输入、中文输入法。
- 新增整套预设的预览、手动保存、匹配筛选、搜索、重命名、回收、永久删除及导入／导出。
- “应用预设”建立启动／换装自动使用关联；关闭面板结束预览并恢复各部位已应用预设，未保存修改提示保存、放弃或取消。提供持久捏人总开关，重启使用默认窗口布局。
- 保留独立核心API，面板及预设后台由可选DLL提供。新增仅核心／核心＋ImGui两种组件包，支持安装、添加组件、回退和卸载；拒绝覆盖未知新增组件。
- 包含装备重绑修复与已完成的dev12性能优化，缩短滑块调节响应时间；修正跨屏返回后的面板输入失配及光标隐藏循环，用户确认风扇突增、闪烁和光标消失已消除。

## 下载与安装

需要游戏内调节和预设时选择 `MHWI-EquipmentMorph-0.6.0-imgui.zip`；只需要核心API时选择 `MHWI-EquipmentMorph-0.6.0-core.zip`，两者二选一。作者插件、独立作者工具和精确绑定标签提交的源码ZIP另行提供。附件摘要见 `SHA256SUMS.txt`，来源提交见 `release-manifest.json`。

先正常退出游戏，解压运行包并执行其中的 `Manage-Installation.ps1`。从旧原生／Tauri控制窗口切换，或移除已安装面板时，先使用原包卸载受管组件再安装目标组合。新组件包不安装旧控制桥接或窗口EXE；历史源码保留。

公共 `runtime.ini` 位于运行包解压根目录，与安装器同级；首次安装时由安装器放入 `nativePC/plugins/MHWEquipmentMorph/`。MOD、Blender插件和独立作者工具不各自提供这份配置。完整步骤见标签下的[安装说明](https://github.com/Mi1ice/MHWI-EquipmentMorph/blob/v0.6.0/docs/INSTALLATION.md)和[预设说明](https://github.com/Mi1ice/MHWI-EquipmentMorph/blob/v0.6.0/docs/PRESETS.md)。

本次不提供开发测试期旧预设或配置迁移。资源路径、MOD3与形态键包指纹继续严格核验。安装器不转换或删除用户预设、配置与MOD数据；保留文件不代表保证旧格式可读。

## 验证范围

通过643项核心、99项输入、33项控制、309项预设、14项SDK、26项作者后端及29项组件安装检查。安装验证使用合成目录，覆盖安装、添加组件、回退、卸载和冲突保护。

用户已确认调节、多部位控制、预设与自动恢复、中文输入及跨屏问题修正。发行包核心和面板DLL与已实测载荷的摘要一致。图形和游戏测试由用户完成，构建清单的 `formal_acceptance_complete=false` 保留，避免将当前观察扩大为所有环境的实机验收。

功能合并见 [PR #2](https://github.com/Mi1ice/MHWI-EquipmentMorph/pull/2)。本轮性能优化已结束；非零权重仍有轻微开销，同场景采样与CPU／GPU归因限制见源码中的 `docs/DEV12_BASELINE.md`。
