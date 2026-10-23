# 核心与游戏内面板的安装

本说明用于0.6.0组件包；两种组件包分别标记 `control_panel: none` 或 `control_panel: imgui`。对应正式发行为 [v0.6.0](https://github.com/Mi1ice/MHWI-EquipmentMorph/releases/tag/v0.6.0)，构建清单不自动扩大用户实机验收范围。

## 选择组件

- `-core` 包只安装 `nativePC/plugins/MHWEquipmentMorphConfiguredPrototype.dll`。它独立提供形态键运行时和 API，不需要控制窗口或控制桥接。
- `-imgui` 包另含 `nativePC/plugins/MHWEquipmentMorphImGui.dll`。游戏内预设、编辑和启动／换装自动应用由此可选插件提供。使用 Del 打开或关闭面板；文字编辑期间 Del 用于删除。

两种包均需要项目支持的 MHW Iceborne 构建、Windows x64、DX12 和 Stracker's Loader。MOD 仍需提供相邻的模型与形态键包。新包不安装旧的原生／Tauri 窗口 EXE，也不安装控制桥接。

## 安装与更换组件

先正常退出游戏，将压缩包解压至普通目录，在该目录运行：

```powershell
./Manage-Installation.ps1 -Mode Install -GameDirectory '包含MonsterHunterWorld.exe的游戏目录'
```

安装器只接受与清单一致的文件。已有本安装器收据时使用 `-Mode Upgrade`；仅核心安装可以通过核心＋ImGui 包升级，新增面板。升级新增组件时，若目标位置已存在未知 DLL，会拒绝覆盖。

从核心＋ImGui 改为仅核心，或从旧控制窗口整包改为新组件组合，先使用旧安装包执行 `-Mode Uninstall`，再安装目标包。升级不会悄悄遗留已管理的组件。手动安装而没有收据的测试载荷，只允许接管摘要完全相同的文件；其他冲突应先将冲突文件移至游戏插件扫描目录以外，再安装。

不要把整个 `payload` 目录复制到游戏根目录。手动安装时，仅复制清单中的 DLL 到相应插件位置，并放置公共 `runtime.ini`。

## 配置归属与本轮格式约定

公共 `runtime.ini` 来自压缩包根目录，与 `Manage-Installation.ps1` 同级；源码对应 `config/runtime.ini`。安装器在缺失时建立 `nativePC/plugins/MHWEquipmentMorph/runtime.ini`。MOD、Blender 插件与作者工具不各自携带这份公共配置。

ImGui 面板按需在 `nativePC/plugins/MHWEquipmentMorph/` 下创建：

- `presets/`：整套预设；`.trash/` 为回收区。
- `auto-presets.json`：已应用预设与各装备部位的关联。
- `panel-settings.json`：形态键控制总开关；不保存窗口布局。
- `imports/`、`exports/`：用户放入或导出的预设文件。

本次升级不承诺兼容开发测试期的旧预设或配置，不提供旧格式迁移。当前预设、自动关联和开关文件各自只接受 `schema: 1`；不因取消兼容承诺而人为更换格式版本。当前格式可继续使用，但不保证更早测试文件可读。格式不受支持或损坏时报告错误，不猜测字段、不按装备显示名称强行应用。

MOD3、资源路径及形态键包指纹的严格匹配继续保留。这属于防止预设应用到错误模型的必要校验，与旧配置兼容无关。

需要重新开始配置时，退出游戏，将 `presets/`、`auto-presets.json` 和 `panel-settings.json` 移到独立备份目录，再启动。首次无配置时总开关开启、没有自动关联、没有预设；窗口使用默认布局。核心配置需重置时，单独使用当前包根目录的 `runtime.ini`。不要删除装备的 `.mod3` 或 `.morph` 数据。仅核心包不读取或创建面板配置。

## 回退与卸载

执行 `-Mode Rollback` 恢复上一次安装操作之前的二进制；例如仅核心升级为核心＋ImGui后，回退会移除新增面板并恢复仅核心收据。执行 `-Mode Uninstall` 恢复首次安装前已有的受管文件，移除本安装新建的受管 DLL。被外部修改的受管二进制会使操作拒绝，避免覆盖未知内容。

安装器不读取、转换或删除预设、自动关联、面板开关、导入／导出、装备模型或其他插件。保留文件不代表承诺旧格式可用。它只会在回退／卸载首次安装时，删除由自己创建且仍与初始模板完全一致的 `runtime.ini`；用户改过的公共配置保留。二进制回退不会回退用户数据。

所有安装测试使用合成游戏目录，不启动游戏。游戏内界面与实机测试由用户完成。
