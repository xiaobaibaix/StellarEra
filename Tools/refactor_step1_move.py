"""模块合并第一步：建目录 + git mv 全部源文件。

设计原则：先校验再动手。任何一个源文件不存在就整体中止，不产生半成品。
所有移动走 git mv，保留文件历史；删除走 git rm，同样进索引。
"""

import os
import subprocess
import sys

REPO = r"F:/workspace/ue/StellarEra-refactor"
SRC = "Source"

PLANET = "Source/StellarEraPlanet"
PAWNS = "Source/StellarEraPawns"
CORE = "Source/StellarEraCore"

DIRS = [
    f"{PLANET}/Public/Terrain", f"{PLANET}/Public/Atmosphere",
    f"{PLANET}/Public/Minerals", f"{PLANET}/Public/Edit",
    f"{PLANET}/Private/Terrain", f"{PLANET}/Private/Atmosphere",
    f"{PLANET}/Private/Minerals", f"{PLANET}/Private/Edit",
    f"{PLANET}/Private/Tests",
    f"{PAWNS}/Public/Character", f"{PAWNS}/Public/Spaceship", f"{PAWNS}/Public/Camera",
    f"{PAWNS}/Private/Character", f"{PAWNS}/Private/Spaceship", f"{PAWNS}/Private/Camera",
    f"{PAWNS}/Private/Tests",
    f"{CORE}/Public/Interfaces",
    "Source/StellarEraCore/Private/Tests",
    "Source/StellarEraCosmos/Private/Tests",
    "Source/StellarEraUI/Private/Tests",
    "Source/StellarEra/Private/Tests",
    "Source/StellarEraGeology/Private/Tests",
]

# (源, 目标) —— 逐文件映射，不用通配，避免误伤
MOVES = [
    # ── Terrain：星球本体与外层 ──────────────────────────────
    ("StellarEraTerrain/Public/VoxelPlanetActor.h",                 f"{PLANET}/Public/VoxelPlanetActor.h"),
    ("StellarEraTerrain/Private/VoxelPlanetActor.cpp",              f"{PLANET}/Private/VoxelPlanetActor.cpp"),

    # ── Terrain：地形生成 ───────────────────────────────────
    ("StellarEraTerrain/Public/VoxelPlanetGenerator.h",             f"{PLANET}/Public/Terrain/VoxelPlanetGenerator.h"),
    ("StellarEraTerrain/Public/VoxelPlanetTerrainModule.h",         f"{PLANET}/Public/Terrain/VoxelPlanetTerrainModule.h"),
    ("StellarEraTerrain/Public/VoxelPlanetNoiseTerrain.h",          f"{PLANET}/Public/Terrain/VoxelPlanetNoiseTerrain.h"),
    ("StellarEraTerrain/Public/VoxelPlanetSphereTerrain.h",         f"{PLANET}/Public/Terrain/VoxelPlanetSphereTerrain.h"),
    ("StellarEraTerrain/Public/PlanetSurfaceRay.h",                 f"{PLANET}/Public/Terrain/PlanetSurfaceRay.h"),
    ("StellarEraTerrain/Public/PlanetSurfaceGridComponent.h",       f"{PLANET}/Public/Terrain/PlanetSurfaceGridComponent.h"),
    ("StellarEraTerrain/Public/PlanetBiomeCubemap.h",               f"{PLANET}/Public/Terrain/PlanetBiomeCubemap.h"),
    ("StellarEraTerrain/Public/AlwaysLocalVoxelInvokerComponent.h", f"{PLANET}/Public/Terrain/AlwaysLocalVoxelInvokerComponent.h"),
    ("StellarEraTerrain/Private/VoxelPlanetGenerator.cpp",          f"{PLANET}/Private/Terrain/VoxelPlanetGenerator.cpp"),
    ("StellarEraTerrain/Private/VoxelPlanetTerrainModule.cpp",      f"{PLANET}/Private/Terrain/VoxelPlanetTerrainModule.cpp"),
    ("StellarEraTerrain/Private/VoxelPlanetNoiseTerrain.cpp",       f"{PLANET}/Private/Terrain/VoxelPlanetNoiseTerrain.cpp"),
    ("StellarEraTerrain/Private/VoxelPlanetSphereTerrain.cpp",      f"{PLANET}/Private/Terrain/VoxelPlanetSphereTerrain.cpp"),
    ("StellarEraTerrain/Private/PlanetSurfaceRay.cpp",              f"{PLANET}/Private/Terrain/PlanetSurfaceRay.cpp"),
    ("StellarEraTerrain/Private/PlanetSurfaceGridComponent.cpp",    f"{PLANET}/Private/Terrain/PlanetSurfaceGridComponent.cpp"),
    ("StellarEraTerrain/Private/PlanetBiomeCubemap.cpp",            f"{PLANET}/Private/Terrain/PlanetBiomeCubemap.cpp"),
    ("StellarEraTerrain/Private/AlwaysLocalVoxelInvokerComponent.cpp", f"{PLANET}/Private/Terrain/AlwaysLocalVoxelInvokerComponent.cpp"),

    # ── Terrain：编辑网格几何归 Edit ────────────────────────
    ("StellarEraTerrain/Public/PlanetEditGridGeometry.h",           f"{PLANET}/Public/Edit/PlanetEditGridGeometry.h"),
    ("StellarEraTerrain/Private/PlanetEditGridGeometry.cpp",        f"{PLANET}/Private/Edit/PlanetEditGridGeometry.cpp"),

    # ── 视点接口：头拆两半，头进 Core，实现留 Planet ────────
    ("StellarEraTerrain/Public/VoxelViewpointInvokers.h",           f"{CORE}/Public/Interfaces/VoxelViewpointInvokers.h"),
    ("StellarEraTerrain/Private/VoxelViewpointInvokers.cpp",        f"{PLANET}/Private/Terrain/VoxelViewpointInvokers.cpp"),

    # ── Terrain 测试 ────────────────────────────────────────
    ("StellarEraTerrain/Private/PlanetBiomeCubemapTests.cpp",       f"{PLANET}/Private/Tests/PlanetBiomeCubemapTests.cpp"),
    ("StellarEraTerrain/Private/PlanetGeologyPodTests.cpp",         f"{PLANET}/Private/Tests/PlanetGeologyPodTests.cpp"),
    ("StellarEraTerrain/Private/PlanetGeologyPresetTests.cpp",      f"{PLANET}/Private/Tests/PlanetGeologyPresetTests.cpp"),
    ("StellarEraTerrain/Private/PlanetLODTransitionTests.cpp",      f"{PLANET}/Private/Tests/PlanetLODTransitionTests.cpp"),
    ("StellarEraTerrain/Private/PlanetSurfaceRayTests.cpp",         f"{PLANET}/Private/Tests/PlanetSurfaceRayTests.cpp"),
    ("StellarEraTerrain/Private/PlanetTerrainEarlyOutTests.cpp",    f"{PLANET}/Private/Tests/PlanetTerrainEarlyOutTests.cpp"),
    ("StellarEraTerrain/Private/PlanetTerrainGradientNormalTests.cpp", f"{PLANET}/Private/Tests/PlanetTerrainGradientNormalTests.cpp"),
    ("StellarEraTerrain/Private/VoxelPlanetBiomeMaterialTests.cpp", f"{PLANET}/Private/Tests/VoxelPlanetBiomeMaterialTests.cpp"),

    # ── Atmosphere → Planet ─────────────────────────────────
    ("StellarEraAtmosphere/Public/PlanetAtmosphereComponent.h",         f"{PLANET}/Public/Atmosphere/PlanetAtmosphereComponent.h"),
    ("StellarEraAtmosphere/Public/PlanetAtmosphereParams.h",            f"{PLANET}/Public/Atmosphere/PlanetAtmosphereParams.h"),
    ("StellarEraAtmosphere/Private/PlanetAtmosphereComponent.cpp",      f"{PLANET}/Private/Atmosphere/PlanetAtmosphereComponent.cpp"),
    ("StellarEraAtmosphere/Private/PlanetAtmosphereParams.cpp",         f"{PLANET}/Private/Atmosphere/PlanetAtmosphereParams.cpp"),
    ("StellarEraAtmosphere/Private/PlanetAtmosphereMaterialBuilder.cpp", f"{PLANET}/Private/Atmosphere/PlanetAtmosphereMaterialBuilder.cpp"),
    ("StellarEraAtmosphere/Private/PlanetAtmosphereParamsTests.cpp",    f"{PLANET}/Private/Tests/PlanetAtmosphereParamsTests.cpp"),

    # ── Minerals → Planet ───────────────────────────────────
    ("StellarEraMinerals/Public/PlanetMineralSurface.h",            f"{PLANET}/Public/Minerals/PlanetMineralSurface.h"),
    ("StellarEraMinerals/Public/PlanetMineralViewComponent.h",      f"{PLANET}/Public/Minerals/PlanetMineralViewComponent.h"),
    ("StellarEraMinerals/Public/PlanetMineralViewActor.h",          f"{PLANET}/Public/Minerals/PlanetMineralViewActor.h"),
    ("StellarEraMinerals/Public/PlanetMineralMaterialBuilder.h",    f"{PLANET}/Public/Minerals/PlanetMineralMaterialBuilder.h"),
    ("StellarEraMinerals/Private/PlanetMineralSurface.cpp",         f"{PLANET}/Private/Minerals/PlanetMineralSurface.cpp"),
    ("StellarEraMinerals/Private/PlanetMineralViewComponent.cpp",   f"{PLANET}/Private/Minerals/PlanetMineralViewComponent.cpp"),
    ("StellarEraMinerals/Private/PlanetMineralViewActor.cpp",       f"{PLANET}/Private/Minerals/PlanetMineralViewActor.cpp"),
    ("StellarEraMinerals/Private/PlanetMineralMaterialBuilder.cpp", f"{PLANET}/Private/Minerals/PlanetMineralMaterialBuilder.cpp"),
    ("StellarEraMinerals/Private/PlanetMineralSurfaceTests.cpp",    f"{PLANET}/Private/Tests/PlanetMineralSurfaceTests.cpp"),
    ("StellarEraMinerals/Private/PlanetMineralViewTests.cpp",       f"{PLANET}/Private/Tests/PlanetMineralViewTests.cpp"),

    # ── PlanetEdit → Planet ─────────────────────────────────
    ("StellarEraPlanetEdit/Public/PlanetEditComponent.h",           f"{PLANET}/Public/Edit/PlanetEditComponent.h"),
    ("StellarEraPlanetEdit/Public/PlanetEditBrush.h",               f"{PLANET}/Public/Edit/PlanetEditBrush.h"),
    ("StellarEraPlanetEdit/Public/PlanetBrushApplicator.h",         f"{PLANET}/Public/Edit/PlanetBrushApplicator.h"),
    ("StellarEraPlanetEdit/Public/PlanetProgressiveEdit.h",         f"{PLANET}/Public/Edit/PlanetProgressiveEdit.h"),
    ("StellarEraPlanetEdit/Private/PlanetEditComponent.cpp",        f"{PLANET}/Private/Edit/PlanetEditComponent.cpp"),
    ("StellarEraPlanetEdit/Private/PlanetEditBrush.cpp",            f"{PLANET}/Private/Edit/PlanetEditBrush.cpp"),
    ("StellarEraPlanetEdit/Private/PlanetBrushApplicator.cpp",      f"{PLANET}/Private/Edit/PlanetBrushApplicator.cpp"),
    ("StellarEraPlanetEdit/Private/PlanetProgressiveEdit.cpp",      f"{PLANET}/Private/Edit/PlanetProgressiveEdit.cpp"),
    ("StellarEraPlanetEdit/Private/PlanetProgressiveEditKernel.h",  f"{PLANET}/Private/Edit/PlanetProgressiveEditKernel.h"),
    ("StellarEraPlanetEdit/Private/PlanetProgressiveEditKernel.cpp", f"{PLANET}/Private/Edit/PlanetProgressiveEditKernel.cpp"),
    ("StellarEraPlanetEdit/Private/PlanetProgressiveEditTests.cpp", f"{PLANET}/Private/Tests/PlanetProgressiveEditTests.cpp"),

    # ── Character → Pawns ───────────────────────────────────
    ("StellarEraCharacter/Public/PlanetCharacter.h",                    f"{PAWNS}/Public/Character/PlanetCharacter.h"),
    ("StellarEraCharacter/Public/PlanetCharacterAnimInstance.h",        f"{PAWNS}/Public/Character/PlanetCharacterAnimInstance.h"),
    ("StellarEraCharacter/Public/PlanetCharacterMovementComponent.h",   f"{PAWNS}/Public/Character/PlanetCharacterMovementComponent.h"),
    ("StellarEraCharacter/Private/PlanetCharacter.cpp",                 f"{PAWNS}/Private/Character/PlanetCharacter.cpp"),
    ("StellarEraCharacter/Private/PlanetCharacterAnimInstance.cpp",     f"{PAWNS}/Private/Character/PlanetCharacterAnimInstance.cpp"),
    ("StellarEraCharacter/Private/PlanetCharacterMovementComponent.cpp", f"{PAWNS}/Private/Character/PlanetCharacterMovementComponent.cpp"),
    ("StellarEraCharacter/Private/PlanetCharacterAnimTests.cpp",        f"{PAWNS}/Private/Tests/PlanetCharacterAnimTests.cpp"),

    # ── Spaceship → Pawns ───────────────────────────────────
    ("StellarEraSpaceship/Public/SpaceshipPawn.h",                      f"{PAWNS}/Public/Spaceship/SpaceshipPawn.h"),
    ("StellarEraSpaceship/Private/SpaceshipPawn.cpp",                   f"{PAWNS}/Private/Spaceship/SpaceshipPawn.cpp"),
    ("StellarEraSpaceship/Private/SpaceshipAtmosphereDragTests.cpp",    f"{PAWNS}/Private/Tests/SpaceshipAtmosphereDragTests.cpp"),
    ("StellarEraSpaceship/Private/SpaceshipCollisionTests.cpp",         f"{PAWNS}/Private/Tests/SpaceshipCollisionTests.cpp"),
    ("StellarEraSpaceship/Private/SpaceshipFreeLookTests.cpp",          f"{PAWNS}/Private/Tests/SpaceshipFreeLookTests.cpp"),
    ("StellarEraSpaceship/Private/SpaceshipLODInvokerTests.cpp",        f"{PAWNS}/Private/Tests/SpaceshipLODInvokerTests.cpp"),
    ("StellarEraSpaceship/Private/SpaceshipSpeedLimitTests.cpp",        f"{PAWNS}/Private/Tests/SpaceshipSpeedLimitTests.cpp"),
    ("StellarEraSpaceship/Private/SpaceshipThrustDirectionTests.cpp",   f"{PAWNS}/Private/Tests/SpaceshipThrustDirectionTests.cpp"),

    # ── 星图相机：UI → Pawns ────────────────────────────────
    ("StellarEraUI/Public/StarMapCameraPawn.h",                         f"{PAWNS}/Public/Camera/StarMapCameraPawn.h"),
    ("StellarEraUI/Private/StarMapCameraPawn.cpp",                      f"{PAWNS}/Private/Camera/StarMapCameraPawn.cpp"),
    ("StellarEraUI/Private/StarMapCameraInvokerTests.cpp",              f"{PAWNS}/Private/Tests/StarMapCameraInvokerTests.cpp"),

    # ── 测试归位：主模块里三个跨模块测试按引用分发 ──────────
    ("StellarEra/Private/StarMapTests.cpp",                "Source/StellarEraCosmos/Private/Tests/StarMapTests.cpp"),
    ("StellarEra/Private/PlanetEditTests.cpp",             "Source/StellarEraUI/Private/Tests/PlanetEditTests.cpp"),
    ("StellarEra/Private/VoxelViewpointInvokerTests.cpp",  "Source/StellarEra/Private/Tests/VoxelViewpointInvokerTests.cpp"),

    # ── 其余模块的测试进各自 Tests/ ─────────────────────────
    ("StellarEraCore/Private/NBodySimulatorTests.cpp",                 "Source/StellarEraCore/Private/Tests/NBodySimulatorTests.cpp"),
    ("StellarEraGeology/Private/PlanetGeologyDistributionTests.cpp",   "Source/StellarEraGeology/Private/Tests/PlanetGeologyDistributionTests.cpp"),
    ("StellarEraCosmos/Private/CosmosOrbitComponentTests.cpp",         "Source/StellarEraCosmos/Private/Tests/CosmosOrbitComponentTests.cpp"),
    ("StellarEraCosmos/Private/CosmosTrajectoryComponentTests.cpp",    "Source/StellarEraCosmos/Private/Tests/CosmosTrajectoryComponentTests.cpp"),
    ("StellarEraUI/Private/MineralLegendTests.cpp",                    "Source/StellarEraUI/Private/Tests/MineralLegendTests.cpp"),
    ("StellarEraUI/Private/StarMapUISystemTests.cpp",                  "Source/StellarEraUI/Private/Tests/StarMapUISystemTests.cpp"),
    ("StellarEraUI/Private/StellarEraHUDTests.cpp",                    "Source/StellarEraUI/Private/Tests/StellarEraHUDTests.cpp"),
]

# 旧模块入口与 Build.cs，合并后不复存在
DELETES = [
    "StellarEraTerrain/StellarEraTerrain.Build.cs",
    "StellarEraTerrain/StellarEraTerrain.cpp",
    "StellarEraTerrain/StellarEraTerrain.h",
    "StellarEraAtmosphere/StellarEraAtmosphere.Build.cs",
    "StellarEraAtmosphere/StellarEraAtmosphere.cpp",
    "StellarEraAtmosphere/StellarEraAtmosphere.h",
    "StellarEraMinerals/StellarEraMinerals.Build.cs",
    "StellarEraMinerals/StellarEraMinerals.cpp",
    "StellarEraMinerals/StellarEraMinerals.h",
    "StellarEraPlanetEdit/StellarEraPlanetEdit.Build.cs",
    "StellarEraPlanetEdit/StellarEraPlanetEdit.cpp",
    "StellarEraPlanetEdit/StellarEraPlanetEdit.h",
    "StellarEraCharacter/StellarEraCharacter.Build.cs",
    "StellarEraCharacter/StellarEraCharacter.cpp",
    "StellarEraCharacter/StellarEraCharacter.h",
    "StellarEraSpaceship/StellarEraSpaceship.Build.cs",
    "StellarEraSpaceship/StellarEraSpaceship.cpp",
    "StellarEraSpaceship/StellarEraSpaceship.h",
]


def run(args, check=True):
    r = subprocess.run(args, cwd=REPO, capture_output=True, text=True)
    if check and r.returncode != 0:
        print(f"FAILED: {' '.join(args)}\n{r.stdout}\n{r.stderr}")
        sys.exit(1)
    return r


def main():
    # 1. 校验（已移动过的视为通过，保证脚本可重入）
    pending = []
    for s, d in MOVES:
        if os.path.isfile(os.path.join(REPO, SRC, s)):
            pending.append((s, d))
        elif not os.path.isfile(os.path.join(REPO, d)):
            print(f"源与目标都不存在，已中止：{s}")
            sys.exit(1)
    print(f"校验通过：{len(pending)} 个待移动（{len(MOVES) - len(pending)} 个已完成）")

    # 2. 目录
    for d in DIRS:
        os.makedirs(os.path.join(REPO, d), exist_ok=True)
    print(f"确认 {len(DIRS)} 个目录")

    # 3. 移动
    for s, d in pending:
        run(["git", "mv", f"{SRC}/{s}", d])
    print(f"移动 {len(pending)} 个文件")

    # 4. 删除旧模块骨架
    for f in DELETES:
        p = os.path.join(REPO, SRC, f)
        if os.path.isfile(p):
            run(["git", "rm", "-q", f"{SRC}/{f}"])
    print(f"删除 {len(DELETES)} 个旧模块骨架文件")


if __name__ == "__main__":
    main()
