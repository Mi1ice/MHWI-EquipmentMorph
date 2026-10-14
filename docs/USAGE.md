# 作者插件 0.4.4 与多装备运行时 0.5.0

## 作者操作

在 Blender 的 N 侧栏打开 MHWI-EquipmentMorph，添加装备模型并选择 MOD3 集合。选择性别、套装和部位后自动确定游戏资源路径；没有对应模型文件的组合不能导出。装备目录随插件提供，见[来源与重建说明](EQUIPMENT_CATALOG.md)。高级手动路径直接使用所填完整路径，不再叠加另一个目标套装 ID。

例如女性冰狼身体对应 `pl/f_equip/pl105_0000/body/mod/f_body105_0000`，皮质身体对应 `pl/f_equip/pl001_0000/body/mod/f_body001_0000`。这些路径相对 nativePC，不需要填写 .mod3 后缀。具体部位编号和四位变体按目录保留，不用套装默认地址覆盖头部例外。

刷新候选形态键后，可以搜索并勾选候选，点击“使用选中形态键”。确认后的键在单独列表中配置接口别名与移除；接口名称为1至63个ASCII字符，同一模型内不重复。非零键作为固定修正保留，动态键必须为零；确认列表中失效或缺失的键会阻止导出。

进入预览后，可以组合调节已确认键，统一查看多个网格的同名形态键。预览使用独立副本，源形态键值不改写；退出、导出、保存、载入、撤销或卸载时清理预览。预览要求对象模式和受支持的静态场景，界面会说明不能预览的原因。

输出文件夹可选任意可写的普通文件夹，包括现有目录。插件自动生成 nativePC 子目录，不需要选择游戏目录。已有目录仅替换本次生成文件，失败回滚；不会清理旧目标模型、旧 Debug 文件或历史 runtime.ini。发布精简包建议使用空目录。

默认关闭 Debug，每个模型只导出四个必要文件：

```text
nativePC/pl/f_equip/pl105_0000/body/mod/
  f_body105_0000.mod3
  f_body105_0000.morph/
    asset.mhmpack
    model.mhmprofile
    profile.json
```

开启 Debug 额外保留 authoring、generated 和 result.json。两种模式均执行完整校验，均不携带 runtime.ini。公共配置由游戏运行时安装，多个 MOD 共用一份。

## 检查和安装

先关闭游戏并安装运行时。配置为 `[runtime]` 下的 `format=2`；运行时扫描 nativePC/pl 中与模型同名的 .morph 包。目录、模型与包内身份必须一致，两个 MOD 覆盖同一路径时只能使用最终安装的一组模型与包。

在源码仓库根目录检查精简导出：

```powershell
./tools/Check-EquipmentMorph.ps1 -ExportDirectory 'D:/MODExport/MyMod' -RuntimeConfig './config'
```

也可将 RuntimeConfig 指向已安装运行时的 nativePC/plugins/MHWEquipmentMorph、运行时解压目录或其中的 runtime.ini。发行工具包内没有源码 config，需要显式指定配置位置。检查游戏安装使用 `-GameDirectory '游戏目录'`。

先生成只读安装计划：

```powershell
python ./tools/manage_equipment_morph_mod.py --source 'D:/MODExport/MyMod' --game '游戏目录'
```

确认后追加 `--install`。内容冲突会拒绝；审阅后明确添加 `--replace` 才备份并替换。MOD安装器只管理MOD3和三个包文件，不创建、替换或删除公共配置。宿主DLL由运行时安装器管理。安装后的原生检查失败会恢复本次模型文件，备份和收据保留。

## 已导出模型的 ID 迁移

生成独立副本，源文件和游戏保持原样：

```powershell
python ./tools/prepare_mhwi_equipmentmorph_resource_path.py --source 'D:/MODExport/MyMod' --output 'D:/MODExport/MyMod-Pl001' --part body --equipment-id 001 --runtime-config './config'
```

输出目录须尚不存在。也可用 `--model-id 'pl/.../完整目标路径'`，与 equipment-id 互斥。同部位存在多个模型时，先选择只包含一个待迁移模型的输入。工具同步更改模型路径和包内身份，保留MOD3和稀疏数据字节，不依赖 Debug 记录。这只处理MOD3与形态键数据，完整MOD材质、纹理和物理关联的压缩包迁移仍待后续实现。

## 换装行为与资源边界

皮质模型pl001出现时选择它的包，冰狼模型pl105出现时选择另一包；无包的装备不提供形态键目标。新目标使用新句柄、权重从0开始，不重放旧装备的控制贡献。控制窗口随公开接口的目标变化显示当前装备和键。

模型目录启动时读取，游戏中更换文件或包后需要重启。文件读取和GPU分配不在输入回调中进行。工作线程在旧上传及绘制完成后切换，每个模型保存独立三缓冲，返装时复用已完成退役的缓冲。缓冲保留至进程退出；最多64个模型，累计顶点基准128MiB、输入文件512MiB，三缓冲有效载荷上限384MiB。超限或损坏的包停用并记录原因，其它有效模型继续工作。

当前支持的游戏构建、DX12路径、三种顶点格式和静止骨架要求延续0.4.2。单MOD历史验收不能替代新切换机制验证。软件检查涵盖不同缓冲大小的A/B模型、无包目标、旧句柄失效、权重清除、缓存归属、文件身份不符、安装冲突和失败回滚。

## 实机验收范围

指定两套身体MOD的三个键、释放恢复、换装绑定及权重归零、连续往返已通过，边界见[验证说明](VALIDATION.md)。
