# Dear ImGui 游戏内面板：第三阶段

提供独立、可选的 `MHWEquipmentMorphImGui.dll`。本阶段已通过公开 API 接入装备选择和形态键控制，使用默认 ImGui 样式；外观优化后置。核心插件继续独立提供形态键支持，不需要面板或旧控制桥接。

## 使用

1. 在游戏获得焦点时按 **Ctrl＋Shift＋M** 呼出／隐藏。
2. 下拉选择当前已绑定、具有形态键数据的装备部位。列表来自核心 API，不列出未穿戴／尚未绑定的模型，也不替玩家换装。
3. 每行四列为 **形态键名 | 权重滑块 | 权重输入框 | 当前权重值**。拖动滑块即时提交；输入框按回车提交。仅查看和选择不会接管权重。
4. 修改一个键即接管该键；名称前的勾选表示本面板是否持有贡献。取消勾选仅释放这一键；“释放全部控制”释放本面板全部贡献。
5. 快捷键隐藏、失去焦点、最小化会保留贡献；点击面板 **×** 释放本面板控制并隐藏。释放会显露其他来源或恢复基准，实际形变仍沿核心既有平滑流程恢复。

输入框显示 float32 的 9 位有效数字。滑块显示 3 位小数但不据此量化提交值。最后一列读取核心的 **已应用权重**，因此平滑时可能与编辑值暂时不同；悬停可查看请求值及是否有其他来源优先。无效／越界输入和互斥冲突会显示错误，并保留此前已接受的贡献。

切换下拉选择会保留其他已控制键和部位。换装后的新目标不继承旧目标权重；旧目标排队中的编辑也不会重放到同名新模型。来源租期过期时丢弃旧命令，需重新调节。

## 安装与回滚

需要 Windows x64、DX12 模式和 Stracker's Loader。正常退出游戏后，仅将构建输出的 `MHWEquipmentMorphImGui.dll` 放入 `nativePC/plugins/`。保留已有 `MHWEquipmentMorphConfiguredPrototype.dll`、`runtime.ini` 与装备资产，不需要外部 EXE、Tauri、WebView2 或控制桥接。

回滚：正常退出游戏后移除 GUI DLL，或恢复上一份 GUI 备份。核心仍可由其他插件通过公开 API 使用。许可证随源码／候选文件保留，不重新分发系统字体。

## 构建与非界面验证

在仓库根目录运行：

```powershell
./tools/build_mhwi_equipmentmorph_imgui.ps1
```

默认输出 `.build/current-validation/imgui/`，可用 `-OutputDirectory` 指定目录。使用现有 MSVC／Windows SDK，不重建／安装核心。脚本编译 DLL、历史软件窗口程序与无窗口的 `ImGuiControlTests.exe`，**只执行控制逻辑测试**。当前 22 项回归覆盖接口校验、命令合并、贡献隔离、实际应用值、冲突拒绝、隐藏续租、目标替换、释放取消排队编辑、租期过期、并发发布及 float32 精度。

用户已确认第二阶段修复版的界面与输入测试通过。此后界面交互、窗口呈现及实机测试统一交由用户执行，助手不启动或自动操作测试窗口。2026-10-20 用户确认第三阶段的调节、多部位控制、显隐／释放及换装后的列表更新正常，上述功能实机验收通过。该结果对应源码提交 `d30799d` 生成并部署的 DLL（SHA-256：`a35bdcde36690b591811ef7636725a11a3f583c93fa56ed5fa6ce78e2118b469`），后续重新构建的产物需另行验证。

历史工具 `EquipmentMorphImGuiFixture.exe` 和 `tools/test_mhwi_equipmentmorph_imgui.ps1` 仍保留用于手动复现 DX12 先／后加载、队列干扰、Reset 与缩放。它们会创建窗口，不能作为助手默认检查。测试机未安装 DX12 Debug Layer，历史日志中的 `d3d12_errors=0` 不代表调试层验证通过。

## 运行方式与日志

- 可选 DLL 使用已验证的窗口、输入及 DX12 路径。支持插件加载时交换链已存在；优先使用创建时队列，或从后台缓冲最终转换与命令提交识别渲染队列。特殊跨队列呈现／交换链包装兼容性仍以实机为准。
- 权重 API 只由普通后台线程调用。渲染与输入线程交换不可变快照和合并后的编辑命令，不直接枚举目标或提交权重。状态每 250ms 刷新；显示时约 16ms 合并处理输入，隐藏时继续续租。会话复用现有控制会话逻辑，优先级 220、租期 2000ms。
- 隐藏时跳过 ImGui 帧与 GPU 提交。GUI 停用会请求释放本面板来源。其他来源的贡献保持独立。
- 快捷键只在游戏拥有焦点时注册；失焦解除注册。键盘／鼠标捕获沿用第二阶段已通过用户测试的路径；手柄和 GetRawInputBuffer 尚未接入。
- 日志：`nativePC/plugins/MHWEquipmentMorph/logs/imgui-<PID>.ndjson`。本阶段标记为 `stage=3`。正常为 `boot=1`、`error=none`、`window_ready=true`、`queue_ready=true`，快捷键注册为 `hotkey_registered=true`、`hotkey_error=0`。
- 接口字段：`api_connected`、`control_ready`、`targets`、`source`、`control_result`、`snapshot_result`。只查看时 `source=0`；接管后为本面板来源，释放后回到 0。错误码沿用公共 API。
- 依赖为固定 Dear ImGui v1.93.0，来源及逐文件摘要见 [UPSTREAM.json](../../research/imgui-v1.93.0/UPSTREAM.json)。系统字体使用 `msyh.ttc`，不重新分发字体。

当前为已通过上述用户实机功能验收的开发候选；外观优化仍后置，不改变正式 0.5.0 发行及已验收 dev12 核心。
