# 本机构建注意事项

## 症状

`Build.bat StellarEraEditor Win64 Development` 在链接阶段失败，报错交替出现：

```
LINK : fatal error LNK1201: 写入程序数据库"...\Binaries\Win64\UnrealEditor-StellarEraCore.pdb"时出错
...UnrealEditor-StellarEraCore.lib : fatal error LNK1136: 无效或损坏的文件
```

`Binaries/Win64` 下的 `.pdb` 全是 **0 字节**，`.lib` 也是 0 字节。因为 `Core` 的 import lib 损坏，
所有依赖它的模块跟着报 `LNK1136`，看起来像"到处都在坏"。

## 根因

**安全软件（火绒 / Defender）拒绝向名为 `Binaries` 的目录写入 `.pdb`。**

判定依据（逐项实测，非推测）：

| 实验 | 结果 |
|---|---|
| `dd` 往 `Binaries/Win64/` 写 50 MB 普通文件 | 成功 → 不是磁盘空间、不是权限 |
| 独占方式打开已存在的 0 字节 pdb | 成功 → 没有进程占用 |
| 同一 rsp，手动 link，`/PDB:` 指向 `_pdbprobe/core3.pdb` | 成功，61 MB |
| 同一 rsp，手动 link，`/PDB:` 指向 `BinariesTest/Win64/...pdb` | 成功，61 MB |
| 同一 rsp，手动 link，`/PDB:` 指向 `Binaries/Win64/...pdb` | **LNK1201** |
| 用 `touch` 触发 `StellarEraCore` 重链（与本次重构无关的模块） | **同样 LNK1201** → 与本项目代码无关 |

即：同样的文件内容、同样的链接器，只因输出目录叫 `Binaries` 就失败。

## 解决

两步都必须做。

### 1. `Binaries` 改成指向别的目录的联接

```powershell
# 在工程根目录执行
Rename-Item Binaries BinariesReal
New-Item -ItemType Junction -Path Binaries -Target (Resolve-Path BinariesReal)
```

`BinariesReal` 已加入 `.gitignore`。UBT 读写 `Binaries/Win64/...` 时经联接落到真实目录，写入不再被拦。

如果哪天删了 `Binaries`，重编前要重做这一步，否则又回到 `LNK1201`。

### 2. 关掉 UBA 加速器

```bash
Build.bat StellarEraEditor Win64 Development -Project=... -WaitMutex -NoUBA
```

只做第 1 步时，UBT 经 UBA 执行 link 仍会失败；两个条件同时满足才通过。

UBA 的缓存目录 `C:\ProgramData\Epic\UnrealBuildAccelerator` 也在报 `Access is denied`，
两者应是同一个安全软件拦截行为。

## 已排除的假设

- 磁盘空间不足：F 盘剩 33 GB，C 盘剩 19 GB
- 路径过长 / 非法字符：同目录别的文件名可写
- NTFS 压缩或加密：目录属性 `Directory`，卷为 NTFS
- `mspdbsrv` 未启动：手动启动后无变化
- 文件被进程占用：独占打开成功
- 重构引入的代码问题：未改动的 `StellarEraCore` 重链同样失败；手动 link 同一批 obj 可成功
