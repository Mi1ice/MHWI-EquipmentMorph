# MHWI-EquipmentMorph

Monster Hunter: World — Iceborne 装备形态键运行时、Blender 作者插件、公开控制接口和手动控制窗口。

运行时 0.5.0，Blender 插件 0.4.4，SDK 0.4.0。两套不同装备的身体模型已完成用户实机验收：三个形态键、释放恢复、换装自动绑定与权重归零、连续往返均正常。作者插件 0.4.4 已通过用户导出测试。验收范围见 [验证说明](docs/VALIDATION.md)。

当前源码为0.5.1-dev3候选，按原生构造器的UINT64_MAX初值补正[未更新缓冲的换装重新绑定修复](docs/REBIND_FIX.md)，并保留进程查询失败保护和日志锁修复。源码构建包使用0.5.1-dev3-source，尚未实机验收；上述0.5.0结论仅属于既有发行，Release附件保持原样。

## 功能

- 从 Blender 的 MOD3 集合联合导出模型与形态键数据，自动保留顶点对应关系。
- 通过本地装备目录选择性别、套装和部位，自动确定资源路径；也支持手动完整路径。装备目录随插件提供，来源与重建方法，见 [装备目录说明](docs/EQUIPMENT_CATALOG.md)。
- 搜索候选形态键，确认后单独配置别名；用独立副本同步预览多个网格的同名键，不改写源键值。
- 默认只导出模型和三个形态键包文件；勾选 Debug 后保留作者与验证资料。公共 `runtime.ini` 随游戏运行时安装，MOD 不携带它。
- 每个 MOD3 对应相邻的同名 `.morph` 目录；修改目标套装 ID 时同步迁移模型路径和包身份。
- 同一装备部位可安装多个模型，游戏换装时自动选择对应数据包，新目标权重从零开始。
- 连续权重、平滑变化、多个键叠加、逐键控制来源和释放，以及失效后重新绑定。
- 独立 Windows 控制窗口和原生 C API；自动表情控制可由外部项目通过 SDK 实现。

## 使用与开发

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
