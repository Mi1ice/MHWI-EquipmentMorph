# 源码构建与验证

作者角点法线保真修复加入独立计算组件。从源码运行联合导出前执行 `./tools/build_authored_shading.ps1`；容量Paired／All和完整源码入口会自动执行。组件仅供Windows x64作者插件使用，不进入游戏核心。完整作者ZIP带有当前组件与许可证，见[作者法线与切线](AUTHORED_SHADING.md)。

## 1024键容量候选

`codex/capacity-paired-export` 的容量候选使用独立入口，不沿用下面历史组件包的验收：

```powershell
./tools/check_morph_capacity.ps1 -Stage All -Python 'python.exe' -Blender '你的Blender目录/blender.exe'
```

也可单独执行 `Data`、`Sdk`、`Paired` 或 `Host`。Paired需要上述固定版本的Blender与模型导出插件，独立后台运行；Host只用合成原生资源，不安装钩子。任一阶段失败都会令入口失败。报告写入 `.build/capacity-validation/all-report.json`，包含执行状态、源码与产物摘要；核心候选位于 `.build/cleanup-validation/native/MHWEquipmentMorphConfiguredPrototype.dll`。

候选仅验证核心、当前SDK和作者联合导出组合，未重新适配现有面板／预设／自动表情，也未执行完整 `check_source.ps1 -ImGui`。不能将已有发行安装包当作本候选。容量、兼容边界及后续测量见[容量候选](CAPACITY_CANDIDATE.md)。

## 环境

- Windows x64，PowerShell 7，Git。
- Visual Studio C++ Build Tools（MSVC x64）及 Windows 10/11 SDK。构建脚本通过 vswhere 自动发现工具链。
- Python 3.11 或更新版本及 `requirements-dev.txt` 中的 NumPy。
- 可选联合导出验证：Blender 5.1.2，已安装且启用匹配源码摘要的 MHW Model Editor 0.12。适配摘要位于 `blender_addon_v044/mhwi_equipmentmorph/direct_export_adapter.json`。

在仓库根目录执行：

```powershell
python -m venv .venv
./.venv/Scripts/python.exe -m pip install -r requirements-dev.txt
./tools/check_source.ps1 -ImGui -Python './.venv/Scripts/python.exe'
```

加入 Blender 联合导出验证：

```powershell
./tools/check_source.ps1 -ImGui -Python './.venv/Scripts/python.exe' -Blender '你的Blender目录/blender.exe'
```

ImGui 入口构建核心、检查器、游戏内面板和 SDK 示例，执行原生、作者、日志、输入、控制、预设及模拟组件安装回滚检查。提供 `-Blender` 时，还会启动独立后台合成场景，检查配对导出、ID迁移和MOD安装冲突；不会打开或保存用户工程。

当前0.6.0交付源码包含已验证的dev12[无关绘制回调早退](DRAW_FILTER.md)、[输入计时与资源诊断](INPUT_DIAGNOSTICS.md)和既有优化，以及后续ImGui与预设功能。组件包位于 `.build/packages/MHWI-EquipmentMorph-0.6.0-core` 与 `-imgui`。用户已确认已测试dev12载荷的调节、释放和换装重绑通过，本轮性能优化结束；重新构建的包仍需按实际变更核验，不自动继承该载荷验收，也不覆盖正式0.5.0发行。当前版本关系见[项目状态](PROJECT_STATUS.md)。

装备目录结构与选择器检查不需要工作簿或解包资源。提供 Blender 时额外执行精简／Debug 导出、公共配置归属、安装回滚及独立副本预览回归。目录重建才需要用户提供的六列工作簿、openpyxl和解包文件名，见[装备目录说明](EQUIPMENT_CATALOG.md)。

输出目录为 `.build/`，打包结果为 `.build/packages/`。联合导出的深层安装测试使用系统临时目录，结束后清理，避免Windows路径长度限制。脚本不安装到真实游戏目录，不启动游戏。单独重复打包可使用 `python ./tools/package_source.py --core-only` 或 `--imgui`。

## 指定工具链

可选 Dear ImGui 游戏内面板使用 `./tools/build_mhwi_equipmentmorph_imgui.ps1` 独立构建，输出 `.build/current-validation/imgui/`，包括面板 DLL、软件测试程序、开发候选清单与许可证。它不重建／安装核心，也不需要 Tauri 工具链；验证范围和实机步骤见[面板说明](../controls/MHWI-EquipmentMorph-ImGui/README.md)。

ImGui 构建入口自动执行无窗口的输入、控制和预设检查：当前分支为99项输入、54项控制与筛选和307项合成预设检查；指定 `-CoreCandidate` 时另核对2项本地核心导出，共309项。图形窗口程序仅编译，不启动。用户指定后续界面／实机测试由其手动完成；助手不执行会创建窗口的历史 `test_mhwi_equipmentmorph_imgui.ps1`。历史先／后加载和队列回归的记录仍保留。

当前面板的完整源码验证使用 `./tools/check_source.ps1 -ImGui -Python './.venv/Scripts/python.exe'`，会构建核心和ImGui，生成仅核心与核心＋ImGui两种包，并执行合成目录安装、升级添加组件、回退、卸载和冲突检查。与 `-Desktop` 互斥，不启动游戏或界面。只重复打包时使用 `python ./tools/package_source.py --core-only` 或 `--imgui`；后者读取 `.build/current-validation/imgui/MHWEquipmentMorphImGui.dll`。包输出分别为 `.build/packages/MHWI-EquipmentMorph-0.6.0-core` 与 `-imgui`，使用说明见[组件安装说明](IMGUI_INSTALLATION.md)。这些包不携带旧预设或配置迁移器。

Tauri 控制窗口候选另需 Node.js 22.12+、Rust stable MSVC 和 WebView2。使用 `./tools/check_source.ps1 -Desktop` 可追加前后端构建、独立四文件候选包与 39 项模拟安装检查，默认入口仍构建原三文件包。浏览器交互及软件联动检查的操作见[控制中心说明](../controls/MHWI-EquipmentMorph-Desktop/README.md)。

可设置 `MHWI_MSVC_ROOT`、`MHWI_WINDOWS_SDK_ROOT` 和 `MHWI_WINDOWS_SDK_VERSION`。MSVC 根目录需包含 `bin/Hostx64/x64/cl.exe`、`include` 和 `lib/x64`。原实机验收载荷使用 MSVC 14.51.36231、Windows SDK 10.0.26100.0；源码重新编译的产物不自动等同于已验收的二进制。

## 发行与验收

本目录最初由0.5.0源码独立整理；此后main已合入重绑修复及dev12性能优化，当前源码不等同于v0.5.0标签。公开构建入口不依赖私人测试收据、旧发行包或历史冻结链。本地构建包的 `formal_acceptance_complete` 固定为false，防止将重新构建的运行文件自动声明为已实机验收。

官方0.5.0发行包及其二进制摘要仍保留在维护者原项目中。发布时应将经核对的发行包作为 Release 附件上传，而非提交生成目录。
