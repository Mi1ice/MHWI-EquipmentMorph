# 下载与安装

本页列出的下载附件属于正式 v0.5.0。main 当前源码为已完成本轮测试的 0.5.1-dev12-source 开发基线，尚未发布正式 0.5.1；自行构建与正式附件的区别见[项目状态](PROJECT_STATUS.md)。

从 [GitHub Releases](https://github.com/Mi1ice/MHWI-EquipmentMorph/releases) 下载对应组件：

- `MHWI-EquipmentMorph-Runtime-0.5.0.zip`：游戏运行时、控制窗口、公共 runtime.ini 和新版工具。
- `MHWI-EquipmentMorph-Blender-0.4.4.zip`：Blender 作者插件，包含可自由使用的新装备目录。
- `MHWI-EquipmentMorph-AuthorTools-0.4.4.zip`：独立检查、MOD安装及已导出模型的 ID 迁移工具；运行时包中也包含同一套工具。
- `SHA256SUMS.txt`：附件校验值。

## 游戏运行时

需要 Windows x64、项目验证过的 MHW Iceborne 游戏构建、DX12 和 Stracker's Loader。先正常退出游戏，将运行时 ZIP 解压到普通目录，再在其中运行：

```powershell
./Manage-Installation.ps1 -Mode Install -GameDirectory '包含MonsterHunterWorld.exe的游戏目录'
```

已有本项目安装收据时用 `-Mode Upgrade`。冲突会拒绝，安装器保留备份；需要恢复本次操作使用 `-Mode Rollback`，移除本安装器管理的文件使用 `-Mode Uninstall`。不要把 payload 目录本身直接复制到游戏根目录。

运行时安装公共 `nativePC/plugins/MHWEquipmentMorph/runtime.ini`。每个 MOD 只携带模型和对应形态键包，多 MOD 共用此配置。工具检查或安装模型前，运行时应已安装。

`runtime.ini`的来源是**运行时压缩包解压后的根目录**，与 `Manage-Installation.ps1`同级；源码对应文件为 `config/runtime.ini`。使用上述安装器会在首次安装时建立配置。手动安装DLL时，也必须把同一个包根目录的 `runtime.ini`放入 `游戏目录/nativePC/plugins/MHWEquipmentMorph/`，不能只复制payload中的DLL。Blender插件包、独立作者工具包及导出的MOD均不提供这份公共配置；已有配置应保留。

发行中的运行时 DLL 与控制窗口沿用已验收 0.5.0 的原字节，更新的是配套工具、授权说明和打包资料。源码重新编译的产物须单独验证，不能自动继承这项二进制验收。

## Blender 作者插件

已验证 Blender 5.1.2。联合导出还需要自行安装并启用匹配适配摘要的 MHW Model Editor 0.12；本项目不附带该外部插件或游戏素材。

在 Blender 偏好设置中从磁盘安装 Blender ZIP，然后启用 MHWI-EquipmentMorph。更新已有插件后重新载入插件或重启 Blender，再打开工程。在 N 侧栏选择性别、套装、部位与 MOD3 集合，确认形态键后导出。

Debug 默认关闭，发布包建议使用空输出目录。新版不会主动清理旧输出中的历史 Debug 资料或旧 runtime.ini。插件使用的新目录源字段与游戏可用路径保持对应，详见[装备目录](EQUIPMENT_CATALOG.md)。

## MOD 管理工具

独立工具需要 Python 3.10 或更新版本，包含所需格式后端与原生验证器，无需 Blender 或 NumPy。解压后在 AuthorTools 目录查看 README.md。游戏目录以外的导出检查，需要通过 RuntimeConfig 指定游戏已安装的公共配置或运行时解压目录。

当前 ID 迁移工具处理已导出的 MOD3 与形态键数据。完整 MOD ZIP 的识别和材质、纹理、物理关联换装暂未实现。
