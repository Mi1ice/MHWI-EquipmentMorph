# 下载与安装

当前正式版本为 [v0.6.0](https://github.com/Mi1ice/MHWI-EquipmentMorph/releases/tag/v0.6.0)。从该发行页按需下载：

- `MHWI-EquipmentMorph-0.6.0-imgui.zip`：核心＋游戏内面板，供需要调节形态键与预设的玩家使用。
- `MHWI-EquipmentMorph-0.6.0-core.zip`：仅形态键核心，供只需要运行时与API支持的场景使用。与上面的包二选一。
- `MHWI-EquipmentMorph-Blender-0.4.4.zip`：Blender作者插件，用于制作与导出形态键资源。
- `MHWI-EquipmentMorph-AuthorTools-0.4.4.zip`：独立资源检查、MOD安装及已导出模型的资源编号调整工具；运行包 `Tools/` 已含同套工具，无需重复下载。
- `MHWI-EquipmentMorph-0.6.0-Source.zip`：该版本对应源码，包括SDK 0.4.0。
- `SHA256SUMS.txt`：附件校验值。`release-manifest.json`记录来源提交和载荷摘要。

[v0.5.0](https://github.com/Mi1ice/MHWI-EquipmentMorph/releases/tag/v0.5.0)及其旧控制窗口附件作为历史保留，勿将新旧两种控制窗口组合安装。

## 游戏运行时

需要 Windows x64、项目验证过的 MHW Iceborne 游戏构建、DX12 和 Stracker's Loader。先正常退出游戏，将选中的运行时 ZIP 解压到普通目录，再在其中运行：

```powershell
./Manage-Installation.ps1 -Mode Install -GameDirectory '包含MonsterHunterWorld.exe的游戏目录'
```

已有同组件安装收据时用 `-Mode Upgrade`；仅核心可以升级添加ImGui。移除面板，或从旧原生／Tauri窗口切换时，先使用原安装包执行 `-Mode Uninstall`，再安装目标组件。未知同名文件会使安装拒绝，备份和冲突处理见[组件安装说明](IMGUI_INSTALLATION.md)。

需要恢复上一次操作使用 `-Mode Rollback`；移除本安装器管理的文件使用 `-Mode Uninstall`。不要把 `payload` 目录本身直接复制到游戏根目录。安装器保留用户改过的配置、预设和MOD数据；二进制回退不会回退用户数据。

公共 `runtime.ini` 来自**运行时压缩包解压后的根目录**，与 `Manage-Installation.ps1` 同级；源码对应 `config/runtime.ini`。安装器在缺失时建立 `nativePC/plugins/MHWEquipmentMorph/runtime.ini`。手动安装时，除相应DLL外也需将这份配置放到该位置；已有配置应保留。Blender插件、独立作者工具及导出的MOD不提供这份公共配置，多MOD共用它。

使用ImGui包后，按 **Del** 呼出面板；文字编辑期间Del保留删除功能。预览、保存和启动／换装自动应用见[预设说明](PRESETS.md)。本次不提供开发测试期旧预设／配置迁移，资源身份校验继续保留。安装器保留文件不等于保证旧格式可读。

## Blender 作者插件

已验证 Blender 5.1.2。联合导出还需自行安装并启用匹配适配摘要的 MHW Model Editor 0.12；本项目不附带该外部插件或游戏素材。

在Blender偏好设置中从磁盘安装Blender ZIP并启用MHWI-EquipmentMorph。更新后重新载入插件或重启Blender，再打开工程。在N侧栏选择性别、套装、部位与MOD3集合，确认形态键后导出。详细流程见[作者使用说明](USAGE.md)。

Debug默认关闭，发布建议使用空输出目录。插件不会主动清理旧输出中的历史Debug资料或旧runtime.ini。装备名称与资源路径见[装备目录](EQUIPMENT_CATALOG.md)。

## 独立作者工具

独立工具需要Python 3.10或更新版本，包含格式后端与原生验证器，无需Blender或NumPy。解压后查看README.md。游戏目录以外的导出检查，通过RuntimeConfig指定游戏已安装的公共配置或运行时解压目录。

资源编号调整工具处理已导出的MOD3与形态键数据，完整MOD ZIP及材质、纹理、物理关联换装暂未实现。仅使用游戏面板时无需安装独立作者工具。
