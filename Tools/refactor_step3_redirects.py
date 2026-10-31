"""模块合并第三步：重写 Config/DefaultEngine.ini 的 [CoreRedirects] 段。

按字节处理，保留 BOM 与 CRLF。

关键点：新模块复用了历史名 StellarEraPlanet，因此早先那批
OldName="/Script/StellarEraPlanet.X" 的条目必须删掉 —— 它们现在会和新模块
自映射，留着等于让旧资产指向不存在的地方。所有映射压平为「历史名 → 最终模块」。
"""

import os

REPO = r"F:/workspace/ue/StellarEra-refactor"
INI = os.path.join(REPO, "Config/DefaultEngine.ini")

# 待替换的行号区间（1-based，含首尾），对应现有的注释头 + [CoreRedirects] 整段
START, END = 108, 157

NEW_BLOCK = """; ─────────────────────────────────────────────────────────────────────
; 类路径兼容层 —— 两次结构变动叠加后的结果。
;
; UE 的原生类路径是 /Script/<模块名>.<类名>，类换了模块路径就变，
; 继承它的蓝图（父类）、引用它的数据资产与关卡里的实例都会断链。
; 历史上项目动过两次结构：
;   1) 单体 StellarEra（更早还叫 StellarEraVehicle）拆成 11 个模块
;   2) 11 个模块合并成 7 个（2026-10）
; 下面每一条把某个历史模块路径直接映射到**最终位置**，中间不留跳板 ——
; 链式 redirect（A→B→C）的解析顺序在 UE 里不保证，能压平就压平。
;
; **这是永久兼容层，不要删。** 删掉之后任何回滚到更早版本的资产
; 会再次变成「丢失父类 / unknown class」。
;
; 特别注意：新模块复用了历史名 StellarEraPlanet，早先那批
; OldName="/Script/StellarEraPlanet.X" 的条目已全部删除 —— 它们现在与新模块
; 自映射，留着会让旧资产被重定向到不存在的地方。
;
; 未列出的类（StellarEraGameMode / StellarEraPlayerController / StellarEraHUD /
; StarMapUISystem）模块路径没变，因此不需要 redirect。
; ─────────────────────────────────────────────────────────────────────
[CoreRedirects]

; ── 行星侧 → StellarEraPlanet ──────────────────────────────────────────
; 来自本次合并前的 StellarEraTerrain
+ClassRedirects=(OldName="/Script/StellarEraTerrain.VoxelPlanetActor",NewName="/Script/StellarEraPlanet.VoxelPlanetActor")
+ClassRedirects=(OldName="/Script/StellarEraTerrain.VoxelPlanetNoiseTerrain",NewName="/Script/StellarEraPlanet.VoxelPlanetNoiseTerrain")
+ClassRedirects=(OldName="/Script/StellarEraTerrain.VoxelPlanetGenerator",NewName="/Script/StellarEraPlanet.VoxelPlanetGenerator")
+ClassRedirects=(OldName="/Script/StellarEraTerrain.VoxelPlanetSphereTerrain",NewName="/Script/StellarEraPlanet.VoxelPlanetSphereTerrain")
+ClassRedirects=(OldName="/Script/StellarEraTerrain.VoxelPlanetTerrainModule",NewName="/Script/StellarEraPlanet.VoxelPlanetTerrainModule")
+ClassRedirects=(OldName="/Script/StellarEraTerrain.PlanetSurfaceGridComponent",NewName="/Script/StellarEraPlanet.PlanetSurfaceGridComponent")
+ClassRedirects=(OldName="/Script/StellarEraTerrain.AlwaysLocalVoxelInvokerComponent",NewName="/Script/StellarEraPlanet.AlwaysLocalVoxelInvokerComponent")
+ClassRedirects=(OldName="/Script/StellarEraTerrain.AlwaysLocalVoxelInvokerWithPredictionComponent",NewName="/Script/StellarEraPlanet.AlwaysLocalVoxelInvokerWithPredictionComponent")
; 来自 StellarEraAtmosphere / StellarEraMinerals / StellarEraPlanetEdit
+ClassRedirects=(OldName="/Script/StellarEraAtmosphere.PlanetAtmosphereComponent",NewName="/Script/StellarEraPlanet.PlanetAtmosphereComponent")
+ClassRedirects=(OldName="/Script/StellarEraMinerals.PlanetMineralViewActor",NewName="/Script/StellarEraPlanet.PlanetMineralViewActor")
+ClassRedirects=(OldName="/Script/StellarEraMinerals.PlanetMineralViewComponent",NewName="/Script/StellarEraPlanet.PlanetMineralViewComponent")
+ClassRedirects=(OldName="/Script/StellarEraPlanetEdit.PlanetEditComponent",NewName="/Script/StellarEraPlanet.PlanetEditComponent")
+EnumRedirects=(OldName="/Script/StellarEraPlanetEdit.EPlanetBrushShape",NewName="/Script/StellarEraPlanet.EPlanetBrushShape")
; 更早的两个名字（单体时代），同样直接指向最终位置
+ClassRedirects=(OldName="/Script/StellarEra.VoxelPlanetActor",NewName="/Script/StellarEraPlanet.VoxelPlanetActor")
+ClassRedirects=(OldName="/Script/StellarEra.VoxelPlanetNoiseTerrain",NewName="/Script/StellarEraPlanet.VoxelPlanetNoiseTerrain")
+ClassRedirects=(OldName="/Script/StellarEra.VoxelPlanetGenerator",NewName="/Script/StellarEraPlanet.VoxelPlanetGenerator")
+ClassRedirects=(OldName="/Script/StellarEra.VoxelPlanetSphereTerrain",NewName="/Script/StellarEraPlanet.VoxelPlanetSphereTerrain")
+ClassRedirects=(OldName="/Script/StellarEra.VoxelPlanetTerrainModule",NewName="/Script/StellarEraPlanet.VoxelPlanetTerrainModule")
+ClassRedirects=(OldName="/Script/StellarEraVehicle.PlanetEditComponent",NewName="/Script/StellarEraPlanet.PlanetEditComponent")
+EnumRedirects=(OldName="/Script/StellarEraVehicle.EPlanetBrushShape",NewName="/Script/StellarEraPlanet.EPlanetBrushShape")

; ── 受控实体侧 → StellarEraPawns ────────────────────────────────────────
; 来自 StellarEraCharacter / StellarEraSpaceship，以及从 StellarEraUI 收编的相机
+ClassRedirects=(OldName="/Script/StellarEraCharacter.PlanetCharacter",NewName="/Script/StellarEraPawns.PlanetCharacter")
+ClassRedirects=(OldName="/Script/StellarEraCharacter.PlanetCharacterAnimInstance",NewName="/Script/StellarEraPawns.PlanetCharacterAnimInstance")
+ClassRedirects=(OldName="/Script/StellarEraCharacter.PlanetCharacterMovementComponent",NewName="/Script/StellarEraPawns.PlanetCharacterMovementComponent")
+ClassRedirects=(OldName="/Script/StellarEraSpaceship.SpaceshipPawn",NewName="/Script/StellarEraPawns.SpaceshipPawn")
+ClassRedirects=(OldName="/Script/StellarEraUI.StarMapCameraPawn",NewName="/Script/StellarEraPawns.StarMapCameraPawn")
; 更早的名字
+ClassRedirects=(OldName="/Script/StellarEra.PlanetCharacter",NewName="/Script/StellarEraPawns.PlanetCharacter")
+ClassRedirects=(OldName="/Script/StellarEra.SpaceshipPawn",NewName="/Script/StellarEraPawns.SpaceshipPawn")
+ClassRedirects=(OldName="/Script/StellarEra.StarMapCameraPawn",NewName="/Script/StellarEraPawns.StarMapCameraPawn")
+ClassRedirects=(OldName="/Script/StellarEraVehicle.PlanetCharacter",NewName="/Script/StellarEraPawns.PlanetCharacter")
+ClassRedirects=(OldName="/Script/StellarEraVehicle.PlanetCharacterAnimInstance",NewName="/Script/StellarEraPawns.PlanetCharacterAnimInstance")
+ClassRedirects=(OldName="/Script/StellarEraVehicle.PlanetCharacterMovementComponent",NewName="/Script/StellarEraPawns.PlanetCharacterMovementComponent")
+ClassRedirects=(OldName="/Script/StellarEraVehicle.SpaceshipPawn",NewName="/Script/StellarEraPawns.SpaceshipPawn")

; ── 模块路径未变的类（仅从单体时代迁出，本次合并不涉及）──────────────────
+ClassRedirects=(OldName="/Script/StellarEra.CosmosOrbitComponent",NewName="/Script/StellarEraCosmos.CosmosOrbitComponent")
+ClassRedirects=(OldName="/Script/StellarEra.CosmosSimulationSubsystem",NewName="/Script/StellarEraCosmos.CosmosSimulationSubsystem")
+ClassRedirects=(OldName="/Script/StellarEra.CosmosTrajectoryComponent",NewName="/Script/StellarEraCosmos.CosmosTrajectoryComponent")
+ClassRedirects=(OldName="/Script/StellarEra.StarMapUISystem",NewName="/Script/StellarEraUI.StarMapUISystem")
+ClassRedirects=(OldName="/Script/StellarEra.StellarEraHUD",NewName="/Script/StellarEraUI.StellarEraHUD")

; ── 属性改名（跨模块 + 改名一次到位）──────────────────────────────────────
; 观测视图的「每囊 N 格」改名成「每特征 N 格」。本地这份 UniverseMap 外链 Actor 包
; （Content/__ExternalActors__/Maps/UniverseMap/4/W8/Z5L3UJGKQYPNHRCJ37BF7A.uasset）
; 里的体素行星上存着 32，没有这条 redirect 会掉回默认 12。删掉不会报错，只会静默改分辨率。
+PropertyRedirects=(OldName="/Script/StellarEraMinerals.PlanetMineralViewComponent.CellsPerPod",NewName="/Script/StellarEraPlanet.PlanetMineralViewComponent.CellsPerFeature")
+PropertyRedirects=(OldName="/Script/StellarEraPlanet.PlanetMineralViewComponent.CellsPerPod",NewName="/Script/StellarEraPlanet.PlanetMineralViewComponent.CellsPerFeature")"""


def main():
    with open(INI, "rb") as f:
        raw = f.read()

    lines = raw.split(b"\n")
    print(f"原文件 {len(lines)} 行，替换 {START}-{END}")

    head = lines[: START - 1]
    tail = lines[END:]

    new_lines = [l.encode("utf-8") + b"\r" for l in NEW_BLOCK.split("\n")]
    # 最后一行后要接原文件的空行，去掉多余的 \r
    new_lines[-1] = NEW_BLOCK.split("\n")[-1].encode("utf-8") + b"\r"

    out = b"\n".join(head + new_lines + tail)
    with open(INI, "wb") as f:
        f.write(out)
    print(f"写回完成，{len(out.split(b'\\n'))} 行")


if __name__ == "__main__":
    main()
