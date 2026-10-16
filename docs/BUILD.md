# 源码构建与验证

## 环境

- Windows x64，PowerShell 7，Git。
- Visual Studio C++ Build Tools（MSVC x64）及 Windows 10/11 SDK。构建脚本通过 vswhere 自动发现工具链。
- Python 3.11 或更新版本及 `requirements-dev.txt` 中的 NumPy。
- 可选联合导出验证：Blender 5.1.2，已安装且启用匹配源码摘要的 MHW Model Editor 0.12。适配摘要位于 `blender_addon_v044/mhwi_equipmentmorph/direct_export_adapter.json`。

在仓库根目录执行：

```powershell
python -m venv .venv
./.venv/Scripts/python.exe -m pip install -r requirements-dev.txt
./tools/check_source.ps1 -Python './.venv/Scripts/python.exe'
```

加入 Blender 联合导出验证：

```powershell
./tools/check_source.ps1 -Python './.venv/Scripts/python.exe' -Blender '你的Blender目录/blender.exe'
```

统一入口构建宿主、检查器、桥接、窗口和 SDK 示例，执行原生、作者、日志、控制会话、管道、窗口及模拟安装回滚检查。提供 `-Blender` 时，还会启动独立后台合成场景，检查配对导出、ID迁移和MOD安装冲突；不会打开或保存用户工程。

当前0.5.1-dev9候选使用同一入口，新增[并行输入校验](CONCURRENT_INPUT.md)回归，保留[运行时最小校验](RUNTIME_INPUT.md)，保留[批量读取](BATCHED_INPUT_READS.md)、[空闲采样](IDLE_SAMPLING.md)、[启动检查](LAUNCH_VALIDATION.md)、[零值轻量路径](ZERO_INPUT_FAST_PATH.md)及[重绑补正](REBIND_FIX.md)。包位于 `.build/packages/MHWI-EquipmentMorph-0.5.1-dev9-source`，待用户实机复测，不覆盖正式0.5.0发行。

装备目录结构与选择器检查不需要工作簿或解包资源。提供 Blender 时额外执行精简／Debug 导出、公共配置归属、安装回滚及独立副本预览回归。目录重建才需要用户提供的六列工作簿、openpyxl和解包文件名，见[装备目录说明](EQUIPMENT_CATALOG.md)。

输出目录为 `.build/`，打包结果为 `.build/packages/`。联合导出的深层安装测试使用系统临时目录，结束后清理，避免Windows路径长度限制。脚本不安装到真实游戏目录，不启动游戏。单独重复打包可使用 `python ./tools/package_source.py`。

## 指定工具链

可设置 `MHWI_MSVC_ROOT`、`MHWI_WINDOWS_SDK_ROOT` 和 `MHWI_WINDOWS_SDK_VERSION`。MSVC 根目录需包含 `bin/Hostx64/x64/cl.exe`、`include` 和 `lib/x64`。原实机验收载荷使用 MSVC 14.51.36231、Windows SDK 10.0.26100.0；源码重新编译的产物不自动等同于已验收的二进制。

## 发行与验收

本目录从0.5.0当前源码独立整理，生产源码保持原字节。公开构建入口不依赖私人测试收据、旧发行包或历史冻结链。本地构建包的 `formal_acceptance_complete` 固定为false，防止将重新构建的运行文件自动声明为已实机验收。

官方0.5.0发行包及其二进制摘要仍保留在维护者原项目中。发布时应将经核对的发行包作为 Release 附件上传，而非提交生成目录。
