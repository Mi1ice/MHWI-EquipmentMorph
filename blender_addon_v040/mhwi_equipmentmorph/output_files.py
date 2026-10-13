"""Canonical resource paths and transactional writes into a selected directory."""
from pathlib import Path
import os
import re
import shutil
import tempfile

def model_resource_id(value):
    value=value.strip().replace('\\','/')
    if not value:raise ValueError('请填写游戏资源路径')
    if value.lower().endswith('.mod3'):value=value[:-5]
    if value.lower().startswith('nativepc/'):value=value[len('nativepc/'):]
    if not re.fullmatch(r'pl/(?:[A-Za-z0-9_]+/)*[A-Za-z0-9_]+',value):
        raise ValueError('游戏资源路径格式无效：请使用 pl/.../模型名，可使用 / 或 \\ 分隔')
    if len(value)>=80:raise ValueError('游戏资源路径过长：必须少于80个字符')
    return value

def retarget_model(value,equipment_id=''):
    value=model_resource_id(value)
    if not equipment_id:return value
    if not re.fullmatch(r'\d{3}',equipment_id):raise ValueError('目标套装 ID 必须是三位数字，例如001')
    folder=re.search(r'/pl(\d{3})_(\d{4})/',value)
    name=re.search(r'/([fm]_[A-Za-z_]+)(\d{3})_(\d{4})$',value)
    if not folder or not name or folder[1]!=name[2]:raise ValueError('资源路径中的套装目录与模型 ID 不一致，不能自动迁移')
    return value[:folder.start(1)]+equipment_id+value[folder.end(1):name.start(2)]+equipment_id+value[name.end(2):]

def publish(stage,output):
    """Replace only exported files; keep unrelated files and roll back on failure."""
    stage=Path(stage).resolve();output=Path(output).resolve()
    if output.exists() and not output.is_dir():raise ValueError('输出位置必须是文件夹')
    files=sorted(p for p in stage.rglob('*') if p.is_file())
    for source in files:
        destination=output/source.relative_to(stage)
        for path in [destination,*destination.parents]:
            if path==output.parent:break
            if path.is_symlink() or (hasattr(path,'is_junction') and path.is_junction()):raise ValueError('导出路径包含链接或联接：'+str(path))
            if path.exists() and (path==destination and not path.is_file() or path!=destination and not path.is_dir()):raise ValueError('导出路径与已有文件夹/文件冲突：'+str(path))
    output.parent.mkdir(parents=True,exist_ok=True)
    backup=Path(tempfile.mkdtemp(prefix='morph-output-backup-',dir=output if output.is_dir() else output.parent))
    replaced=[];created=[];directories=[];recovery_failed=False
    try:
        for source in files:
            destination=output/source.relative_to(stage)
            missing=[];parent=destination.parent
            while not parent.exists():missing.append(parent);parent=parent.parent
            for directory in reversed(missing):directory.mkdir();directories.append(directory)
            if destination.exists():
                saved=backup/source.relative_to(stage);saved.parent.mkdir(parents=True,exist_ok=True)
                os.replace(destination,saved);replaced.append((saved,destination))
            else:created.append(destination)
            os.replace(source,destination)
    except Exception as error:
        try:
            for destination in reversed(created):
                if destination.exists():destination.unlink()
            for saved,destination in reversed(replaced):os.replace(saved,destination)
            for directory in reversed(directories):directory.rmdir()
        except Exception as recovery:
            recovery_failed=True
            raise RuntimeError('导出失败且自动恢复未完成；原文件备份保留在 '+str(backup)) from recovery
        raise error
    finally:
        if not recovery_failed:shutil.rmtree(backup)
