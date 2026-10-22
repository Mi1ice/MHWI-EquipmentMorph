# 0.4.4 作者与 MOD 管理工具

后续自动表情适配约定（功能开发中，尚未交付）：若目标面部使用形态键跟随游戏表情，
由 MOD 作者移除该网格上原生表情骨骼的权重，避免重复变形；头部、颈部等必要跟随权重仍须保留。
隐藏原生头部与读取其表情信号是两件事，具体 MOD 组合仍需验证隐藏后原生面部动画是否更新。

单个MOD只携带模型和模型旁的形态键包。公共`runtime.ini`由游戏端运行时安装，工具不从MOD复制这份文件。检查、安装与换装均不需要Blender导出的Debug资料。

检查精简导出：

```powershell
./Check-EquipmentMorph.ps1 -ExportDirectory 'D:/MODExport/MyMod' -RuntimeConfig 'D:/Runtime/MHWI-EquipmentMorph-0.5.0'
```

运行时配置目录也可填写`游戏目录/nativePC/plugins/MHWEquipmentMorph`。检查已安装的全部包：

```powershell
./Check-EquipmentMorph.ps1 -GameDirectory '游戏目录'
```

计划安装（只读），确认后追加`--install`；已有模型内容冲突时需审阅并明确添加`--replace`：

```powershell
python ./manage_equipment_morph_mod.py --source 'D:/MODExport/MyMod' --game '游戏目录'
```

先关闭游戏并安装游戏端运行时。只安装MOD3和每个包的三个文件，保留共享配置与其它MOD。安装失败会回滚本次模型文件，备份／收据仍在游戏的`.mod-installation`目录。

换装生成新目录，原文件保持：

```powershell
python ./prepare_mhwi_equipmentmorph_resource_path.py --source 'D:/MODExport/MyMod' --output 'D:/MODExport/MyMod-Pl001' --part body --equipment-id 001 --runtime-config 'D:/Runtime/MHWI-EquipmentMorph-0.5.0'
```

也可使用`--model-id`指定完整资源路径，与`--equipment-id`互斥。同部位存在多个模型时先选择只包含一个待迁移模型的输入。这个入口管理导出的MOD3与形态键数据；完整MOD的材质、纹理、物理关联迁移仍属于后续压缩包工具的范围。

需要Python 3.10或更新版本；所需标准库及包格式后端随工具包提供，无需Blender或numpy。原生验证器沿用运行时0.5.0，只读验证模型身份与包内容；源码来自项目公开仓库的`tools/validate_equipment_morph_packages.cpp`及其引用的runtime头文件。本工具源码随包提供，GPL许可见LICENSE。
