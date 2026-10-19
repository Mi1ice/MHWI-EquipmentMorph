# GitHub 发布准备

本目录是独立源码仓库，默认分支为main。2026-10-19已将原默认分支codex/github-ready重命名为main，并通过PR #1合入dev12实现。原项目完整历史及私人验收档案保留在本地，不用于公开推送。当前版本关系见[项目状态](PROJECT_STATUS.md)。

## 后续源码维护

公开仓库[Mi1ice/MHWI-EquipmentMorph](https://github.com/Mi1ice/MHWI-EquipmentMorph)已存在。后续改动从main创建工作分支，核对差异并执行对应验证后通过PR审阅。不要重新创建仓库、重复初始化远程或推送原完整开发仓库的历史。

新建源码检出使用：

```powershell
git clone https://github.com/Mi1ice/MHWI-EquipmentMorph.git
cd MHWI-EquipmentMorph
```

提交前执行`python ./tools/check_repository.py`，并按[构建说明](BUILD.md)运行与变更相关的检查；核对`git status --short`与完整差异，提交后工作区应为空。推送与发行需要维护者在自己的GitHub账号完成认证。

## 发行附件

发行二进制与源代码提交分别管理。现有v0.5.0已正式发布，对应源码提交9de8545、运行时0.5.0及作者工具0.4.4，原发行文件与摘要保留。维护发行附件时同时提供GPL许可证、第三方许可证以及该版本对应源码获取入口。正式v0.5.0使用其标签下的源码；当前main已经包含后续开发修改。

源码目录中的`.build/packages/`属于本机新构建，当前版本为0.5.1-dev12-source。用户已验收的是此前测试的dev12载荷；任意新构建不自动继承该载荷或原正式包的二进制验收。不要将其误标为原已验收载荷。

所有生成文件、用户工程和MOD素材均已从Git提交范围排除。源码压缩包不包含`.git`、生成目录或原项目历史。

当前完整发行使用运行时0.5.0与作者插件0.4.4。运行时发行载荷保留已验收DLL和控制窗口原字节，配套工具升级为读取公共配置的0.4.4版本；附件同时包含原生检查器、源码及许可证。完整源码由发行标签对应提交提供，附件摘要见SHA256SUMS.txt。先上传草稿并核对附件，再公开发行。

下载和安装说明见[INSTALLATION.md](INSTALLATION.md)。完整MOD压缩包换装工具暂缓，不列入本版功能。
