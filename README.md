# MHWI-EquipmentMorph

Monster Hunter: World — Iceborne 装备形态键运行时、Blender 作者插件、公开控制接口和手动控制窗口。

运行时 0.5.0，Blender 插件 0.4.1，SDK 0.4.0。两套不同装备的身体模型已完成用户实机验收：三个形态键、释放恢复、换装自动绑定与权重归零、连续往返均正常。验收范围见 [验证说明](docs/VALIDATION.md)。

## 功能

- 从 Blender 的 MOD3 集合联合导出模型与形态键数据，自动保留顶点对应关系。
- 每个 MOD3 对应相邻的同名 `.morph` 目录；修改目标套装 ID 时同步迁移模型路径和包身份。
- 同一装备部位可安装多个模型，游戏换装时自动选择对应数据包，新目标权重从零开始。
- 连续权重、平滑变化、多个键叠加、逐键控制来源和释放，以及失效后重新绑定。
- 独立 Windows 控制窗口和原生 C API；自动表情控制可由外部项目通过 SDK 实现。

## 使用与开发

运行环境为 Windows x64、已验证的 MHW Iceborne 游戏构建和 DX12，使用 Stracker's Loader。作者侧联合导出已验证 Blender 5.1.2 与源码摘要匹配的 MHW Model Editor 0.12；这些外部依赖需要自行安装。项目不包含游戏模型或 MHW Model Editor。

- [作者操作、同名包和安装流程](docs/USAGE.md)
- [源码构建与验证](docs/BUILD.md)
- [SDK 与调用示例](sdk/MHWI-EquipmentMorph-0.4.0/README.md)
- [手动控制窗口](controls/MHWI-EquipmentMorph-ControlPanel/README.md)
- [项目结构与来源](docs/REPOSITORY.md)
- [版本记录](CHANGELOG.md)
- [GitHub发布准备](docs/PUBLISHING.md)

本仓库仅提供当前源码、必要依赖与合成测试。构建产物写入 `.build/`，发行二进制应作为 GitHub Release 附件管理。本阶段不兼容旧单包配置，当前配置使用 `format=2`。

## 许可证

项目主代码采用 **GPL-3.0-or-later**，完整条款见 [LICENSE](LICENSE)，适用范围见 [许可证说明](LICENSING.md)。第三方 SafetyHook 与 Zydis 保留各自原始许可证，见 [第三方说明](THIRD_PARTY_NOTICES.md)。
