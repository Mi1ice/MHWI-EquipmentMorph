# 装备目录来源

`equipment_catalog.json`由用户新提供的 `Book.xlsx` 提取。提供者明确授权自由使用和再分发，不附加使用限制。它独立替代旧目录，没有读取此前受限制的数据表。项目代码仍适用 GPL-3.0-or-later；目录使用授权见 [装备目录说明](https://github.com/Mi1ice/MHWI-EquipmentMorph/blob/codex/github-ready/docs/EQUIPMENT_CATALOG.md)。

原始六列的部位、防具ID、部件名称、套装ID和男女目录逐项保留。`chest`映射为插件的`body`；`None`表示该性别未提供目录，不借用另一性别路径。套装搜索名称从同套装部件名称的共有前缀及等级后缀生成。具体部位路径优先于套装显示用的代表路径。

完整资源路径只对照解包MOD3文件名核验，没有读取或包含游戏模型内容。来源摘要和文件名清单摘要保留在JSON中。
