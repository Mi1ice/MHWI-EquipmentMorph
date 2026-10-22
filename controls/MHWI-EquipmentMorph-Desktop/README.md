# Tauri 控制窗口（开发候选）

**状态：2026-10-19 暂停。** 用户选择转向独立的 Dear ImGui 游戏内面板；测试游戏目录中的外部面板和控制桥接已清理，核心、配置和装备数据保留。以下内容记录历史候选的使用及构建方式，源码与候选包保留供追溯。

使用 Tauri 2、React 和系统 WebView2，窗口位于游戏进程之外。运行时仍使用已完成性能验收的 dev12 实现；本候选增加控制界面、自动启动和快捷键，不代表新的正式运行时版本。

## 使用

安装包含 ControlCenter 的完整候选包后，游戏加载控制桥接时自动在后台启动控制程序，无需手动打开应用。

- 游戏或控制窗口在前台时，**Ctrl＋Shift＋M** 呼出／隐藏窗口。
- **隐藏或最小化窗口保留本窗口设置的权重**，后台继续维持控制来源。最小化后按快捷键恢复窗口。
- 点击窗口 **×**：释放本窗口全部控制并隐藏；之后仍可用快捷键呼出。
- “取消接管”只释放选中的键；“释放全部控制”释放本窗口所有贡献。
- **权重 0 仍然属于接管**，与释放不同。精确输入保留运行时 float32 精度，滑块用于连续调节。
- 游戏退出后，自动启动的控制程序随之退出。选择“退出程序”也会释放控制；本次游戏会话内需手动打开 ControlCenter 才能再次使用快捷键。

控制程序使用外部桌面窗口。建议在无边框／窗口模式下使用；独占全屏时呼出窗口可能切换出游戏，尚待实机确认。它不是注入游戏画面的覆盖层。系统须已安装 Microsoft Edge WebView2 Runtime；可从 [Microsoft 官方页面](https://developer.microsoft.com/microsoft-edge/webview2/) 获取。

快捷键仅在游戏或控制窗口位于前台时注册，其他应用在前台时释放注册。如果注册失败，窗口会显示错误，便于检查快捷键占用及运行权限。默认启动无窗口；程序缺失、启动失败不会阻止原有桥接工作。

## 安装与兼容

候选包使用独立目录 `MHWI-EquipmentMorph-0.5.1-dev12-controlcenter-dev1`，不会替换正式 v0.5.0 或现有 dev12 压缩包。安装前正常退出游戏及旧控制窗口；在包目录中运行：

```powershell
.\Manage-Installation.ps1 -Mode Install -GameDirectory 'C:\Games\Monster Hunter World'
```

已有安装收据时将 `Install` 换为 `Upgrade`；恢复上次安装用 `Rollback`，移除受管理组件用 `Uninstall`。现有用户数据包及修改过的 runtime.ini 保留。`runtime.ini` 模板来自仓库 `config/runtime.ini`，仅在目标文件不存在时复制。

对于之前通过开发流程部署、没有通用安装收据的 dev12，仅接受与 `config/control-center-predecessors.json` 已知摘要完全一致的桥接及旧窗口；安装前备份原文件，回滚／卸载可恢复。其他未知文件仍拒绝覆盖。

新增文件为 `MHWI-EquipmentMorph/MHWI-EquipmentMorph-ControlCenter.exe`。桥接、运行时及原生 ControlPanel 仍在包内；ControlPanel 保留作为兼容工具。同一桥接只接受一个控制客户端，使用旧窗口前应退出新控制程序；需要恢复原三文件版本时先回滚或卸载，不支持直接用三文件包覆盖四文件安装。

启动诊断位于游戏的 `nativePC/plugins/MHWEquipmentMorph/logs/control-center-startup-<PID>.json`，记录后台启动结果及 Windows 错误码。该日志只说明进程创建结果，不能证明 WebView 初始化或实机形变已通过。

## 开发与验证

需要 Windows x64、Node.js 22.12+、Rust stable MSVC、Visual Studio C++ 工具链及 Windows SDK。依赖由 package-lock.json 和 Cargo.lock 固定。构建不需要修改系统 PATH；也可使用仓库 `.build/tauri-dependencies` 中的本地 Rust 工具链。

```powershell
.\tools\build_mhwi_equipmentmorph_control_panel.ps1 -OutputDirectory .build/current-validation/control-panel
.\tools\build_mhwi_equipmentmorph_desktop.ps1 -CheckOnly
.\tools\build_mhwi_equipmentmorph_desktop.ps1 -SkipInstall
python .\tools\package_source.py --desktop
.\tools\test_mhwi_equipmentmorph_candidate_installation.ps1 -PackageDirectory .build/packages/MHWI-EquipmentMorph-0.5.1-dev12-controlcenter-dev1
```

前端目录执行 `npm run dev`，再执行 `npx playwright test` 可验证合成界面的操作流程。开发预览使用 `?fixture=connected`，另有 empty、disconnected、conflict、many 状态；生产构建不提供合成数据入口。软件联动测试使用 `native/integration_fixture.cpp`，只加载隔离的 CPU 软件核心，不连接真实游戏或 GPU。

通信复用现有命名管道协议：校验服务器进程、重连先只读、断线清空待处理操作、64 位标识符以字符串跨越 JavaScript 边界。可见界面停止心跳时释放贡献，隐藏时由 Rust 后台维持心跳。修改权重仍需游戏实机验证，尤其是独占全屏、换装重绑及输入焦点恢复。
