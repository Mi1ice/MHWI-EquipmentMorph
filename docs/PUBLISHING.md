# GitHub 发布准备

本目录是独立源码仓库，使用codex/github-ready分支与干净的初始提交。原项目完整历史及私人验收档案保留在本地，不用于公开推送。

## 提交源码

在GitHub创建一个空的公开仓库，名称可使用MHWI-EquipmentMorph。首次创建时不要由网站额外初始化README或LICENSE，本目录已提供这两个文件。

在此源码目录内配置实际仓库地址并推送：

```powershell
git remote add origin https://github.com/<owner>/MHWI-EquipmentMorph.git
git push -u origin HEAD
```

将`<owner>`替换为实际账号或组织。发布前执行`python ./tools/check_repository.py`，核对提交范围；`git status --short`应为空。上述远程操作需要维护者在自己的GitHub账号完成认证。

## 发行附件

发行二进制与源代码提交分别管理。现有0.5.0正式包在维护者原项目中，运行文件已通过实机验收。发布它时同时提供GPL许可证、第三方许可证以及该版本对应源码获取入口；这个源码快照保留了生产代码原字节。

源码目录中的`.build/packages/`属于本机新构建，只声明软件检查结果，不自动继承原正式包的二进制验收。不要将其误标为原已验收载荷。

所有生成文件、用户工程和MOD素材均已从Git提交范围排除。源码压缩包不包含`.git`、生成目录或原项目历史。

当前完整发行使用运行时0.5.0与作者插件0.4.4。运行时发行载荷保留已验收DLL和控制窗口原字节，配套工具升级为读取公共配置的0.4.4版本；附件同时包含原生检查器、源码及许可证。完整源码由发行标签对应提交提供，附件摘要见SHA256SUMS.txt。先上传草稿并核对附件，再公开发行。

下载和安装说明见[INSTALLATION.md](INSTALLATION.md)。完整MOD压缩包换装工具暂缓，不列入本版功能。
