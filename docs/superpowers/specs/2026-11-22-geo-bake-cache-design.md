# 地质烘焙缓存（2026-11-22）

## 目标
编辑器里改配方触发的地质烘焙结果落盘复用：重启编辑器/重新进关卡不再每次都烘 60+ 秒；提供手动烘焙按钮；烘焙后产生的缓存改动在关编辑器时静默保存。

## 组件
- `UGeoBakeCache`（`Geo/Snapshot/GeoBakeCache.h`）：Content 资产，每颗星一个包 `/Game/Code/Planet/BakeCache/PC_<关卡>_<Actor>`。字段 `FormatVersion` / `ConfigHash` / `RadiusVoxels` / `Blob`。
- `PlanetSnapshotSerialize`：快照 ↔ 二进制（magic + `GeoBakeCacheFormat::Version` + 全字段）。原语只存 `Prims`，读回时 `Build()` 重建 BVH。
- `AVoxelPlanetActor::StartGeoBake`：编辑器态先查缓存（版本/配置哈希/半径三者全等即命中）→ 命中直接走 `OnGeoBakeFinished`（0 s）；未命中照旧异步烘焙，完成后 `GeoBakeCache::Store` 写资产并 `MarkPackageDirty`（不立即落盘）。
- `AVoxelPlanetActor::BakeNow`（CallInEditor「立即烘焙」）：置 `bForceRebake` 跳过缓存。
- 编辑器模块：`FVoxelPlanetActorDetails` 把按钮画在 `Planet|Geo` 分类；`FCoreDelegates::OnPreExit` 调 `GeoBakeCache::SaveDirty()` 静默保存脏缓存；控制台 `StellarEra.Geo.SaveBakeCache` 手动保存。

## 缓存失效
`GeoBakeCacheFormat::Version` 必须随阶段产出逻辑变更 +1；否则读取旧缓存会得到与新代码不符的地形。日志会打印「命中 / 失效（版本/配置/半径不匹配）」。

## 验证
- `StellarEra.Planet.Geo.Cache.RoundTrip`：序列化往返（内容哈希、场采样、版本失效路径）。
- 实机：首次烘焙 67.7 s → 缓存资产 163.5 MB 落盘 → 重启后日志「命中（163.5 MB）」、烘焙 0.00 s。

## 取舍
- 缓存是 126–170 MB 的 Content 资产，走 LFS；不进包体（仅编辑器使用）。
- 命中不回写缓存（避免每次启动都重新序列化并标脏）。
