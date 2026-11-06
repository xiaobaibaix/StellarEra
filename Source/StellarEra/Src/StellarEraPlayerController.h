// Copyright Voxel Plugin SAS. All Rights Reserved.
// 玩家控制器：构造输入资产、分派输入、编排登船/离船。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Templates/SubclassOf.h"
#include "StarMapUIInputs.h"
#include "StarMapCameraInputComponent.h"
#include "StellarEraInputActions.h"
#include "StellarEraPlayerController.generated.h"

class APlanetCharacter;
class AStarMapCameraPawn;
class ASpaceshipPawn;
class AVoxelPlanetActor;
class UPlanetMineralViewComponent;
class UPlanetEditComponent;
struct FInputActionValue;
struct FStarMapScreenCandidate;

/**
 * 本项目的玩家控制器。
 *
 * 输入资产（IMC / IA）在运行时用 C++ 构造：零手工步骤，改完代码立刻能跑，
 * 不需要先在编辑器里点出一堆资产。代价是键位暂时不能在编辑器里改；等键位
 * 定稿后再换成 .uasset 是平滑的替换（数据结构一样，只是来源变了）。
 *
 * 输入绑定按「谁被操控谁绑」分：
 *   - **角色 / 飞船 / 星图相机**在自己的 `SetupPlayerInputComponent` 里绑移动、视角、
 *     推力、编辑与笔刷等，动作表由本类经 `IStellarEraInputSource` 提供 —— 所以这里
 *     没有按 Pawn 类型分派的三路 `Cast`，新增一种可操控 Pawn 也不用改本类；
 *   - **本类**只绑控制器级的东西：E 上下船、M 星图、H 矿物、数字键，
 *     以及星图模式下的**选择**（左键按下 / 拖拽 / 连挖收尾）；星图的视角与缩放归
 *     相机自己的 `UStarMapCameraInputComponent`。
 *
 * 同时实现三个跨模块接口，全部是**依赖倒置**的查询钩子：声明方在 Pawns / UI
 * （下游），实现方是本类（Game，最上层），消费方各自 `Cast<接口>(Controller)`。
 *   - `IStellarEraInputSource`：三个 Pawn 的输入组件来取运行时构造的动作表；
 *   - `IStarMapUIInputs`：HUD / 星图面板查询鼠标位置、屏幕拾取、矿物透视；
 *   - `IStarMapLookContext`：星图相机判「鼠标位移该绕行还是转头」时问的两句状态。
 * 三者都只读、不改状态。再加接口前先确认：这个状态是不是真的只有控制器知道。
 */
UCLASS()
class AStellarEraPlayerController : public APlayerController, public IStarMapUIInputs,
	public IStellarEraInputSource, public IStarMapLookContext
{
	GENERATED_BODY()

public:
	// ── 构造与生命周期 ──

	AStellarEraPlayerController();

	virtual void SetupInputComponent() override;
	virtual void BeginPlay() override;

	// ── 接口实现 ──
	// 三个都是对别的模块的契约，统一放 public：没必要一个 public 一个 protected。

	//~ Begin IStellarEraInputSource Interface
	/** 把运行时构造的动作表交给 Pawn 去绑定。实现见 .cpp（幂等 Build）。 */
	virtual const FStellarEraInputActions& GetStellarEraInputActions() override;
	//~ End IStellarEraInputSource Interface

	//~ Begin IStarMapUIInputs Interface
	/** 把一颗天体投到屏幕；成功时 OutCenter 为中心、OutRadiusPx 为轮廓半径（像素）。 */
	virtual bool ProjectBodyToScreen(AActor* Body, FVector2D& OutCenter, float& OutRadiusPx) const override;

	/** 任意屏幕点下的已注册天体。悬停与点击共用。 */
	virtual AActor* PickBodyUnderScreenPosition(const FVector2D& ScreenPosition) const override;

	/** 当前激活的矿物透视组件；未激活时为空。星图 UI 用它画图例、喂矿体显隐。 */
	virtual UPlanetMineralViewComponent* GetActiveMineralView() const override { return ActiveMineralView; }

	virtual bool GetStarMapMousePositionPx(FVector2D& OutPosition) const override;
	//~ End IStarMapUIInputs Interface

	//~ Begin IStarMapLookContext Interface
	virtual bool IsStarMapSelectHeld() const override;
	virtual bool IsMineralViewActive() const override;
	//~ End IStarMapLookContext Interface

	// ── 静态查询（无状态，能无头测）──

	/**
	 * 这个天体身上（含它的挂载物）有没有星图相机。
	 *
	 * 星图的聚焦目标**只认带相机的天体**：相机是「照这颗天体的视点」，没有相机的
	 * 天体（飞船）在星图里既不被进场自动聚焦，也点不中。纯查询，不碰世界，
	 * 所以能无头测。
	 */
	static bool HasStarMapCamera(const AActor* Body);

	/**
	 * 「进图时的参考行星」：进星图该借用谁的星图相机。
	 *
	 * 驾驶中 → 飞船的参考行星（ASpaceshipPawn::GetPlanetActor，子系统每帧维护）；
	 * 步行 → 角色当前所在的行星（APlanetCharacter::PlanetActor）；
	 * 其他 Pawn / 空 → 空。**只做类型解析，不做距离搜索** —— 那是 Pawn 自己的职责，
	 * 这里越俎代庖会把「最近的行星」与「玩家心中的当前行星」混成一件事。
	 */
	static AVoxelPlanetActor* ResolveReferencePlanet(const APawn* EnteringPawn);

	/**
	 * 在 Owner 的组件里找「行星自带的星图相机」：第一台作为
	 * UChildActorComponent 子 Actor 存在的 AStarMapCameraPawn。
	 *
	 * BP_VoxelPlanet 挂一个 Child Actor Component（子类 BP_StarMapCameraPawn），
	 * 「星球 + 它的星图相机」就成了一个可整体复制、每个实例可单独换类（Details
	 * 里 Child Actor Class 可编辑）的组合 —— 这是 B2 路线（用户选定，2026-11-01）。
	 *
	 * 注意 CAC 的子 Actor 在类默认对象（CDO）里不存在、实例化才创建，所以
	 * 本函数对 CDO 恒返回空 —— 这是 CAC 的机制，不是 bug。
	 * 纯函数（只读组件与 Cast），无头可测；Owner / 子 Actor 不是星图相机时返回空。
	 */
	static AStarMapCameraPawn* FindChildStarMapCamera(const AActor* PlanetOwner);

	/**
	 * 在 Owner 上找直接 Attach 的第一台 AStarMapCameraPawn。
	 *
	 * 这条路径覆盖「相机是关卡里的独立 Actor、手动 Attach 到星球」的旧摆法
	 * （UniverseMap 里现存的 3 台就是这种）。CAC 的子 Actor 其实也会 Attach 到
	 * 宿主，所以本函数同样能取到 CAC 相机 —— 保留它是为了让 CAC 尚未普及、
	 * 或临时手摆一台相机的关卡也能正常工作。
	 *
	 * 不递归子挂载物，避免飞船 / 角色 / 采矿平台上挂的相机被误认成行星相机。
	 */
	static AStarMapCameraPawn* FindAttachedStarMapCamera(const AActor* PlanetOwner);

	/**
	 * 上面两步的串联：进星图时优先用焦点行星自带的相机。
	 * 先认 CAC（显式声明的那台），没有再退回附加关系（手动摆的那台）；
	 * 都没有、或进图时解析不出行星（深空）→ 返回空，调用方退回单例相机。
	 */
	static AStarMapCameraPawn* ResolvePlanetStarMapCamera(const APawn* EnteringPawn);

	/**
	 * 换视点的唯一驱动：把 World 里所有实现 IVoxelViewpointInvokers 的 Pawn
	 * 按 InPawn 设成视点，只有 InPawn 拿到 true。InPawn 为空（OnUnPossess
	 * 路径）时全部关掉。
	 *
	 * 静态是为了让测试能在没有控制器实例的情况下验它 —— 与 HasStarMapCamera
	 * 同理。不实现本接口的 Pawn 不受影响。
	 */
	static void ApplyViewpointInvokerGate(UWorld* World, const APawn* InPawn);

protected:
	// ── Possess / 逐帧 ──

	virtual void OnPossess(APawn* InPawn) override;

	virtual void PlayerTick(float DeltaTime) override;

	/**
	 * 按当前 Possess 的 Pawn 切换鼠标模式。
	 *
	 * 角色：光标常驻可见，鼠标只在按住右键时被视口捕获（＝只在按住右键时
	 * 有位移值，相机也只在按住右键时转）。飞船：光标隐藏、永久捕获。
	 *
	 * 挂在 OnPossess 上而不是登船/离船两处：本工程换 Pawn 的每一条路径
	 * （开局 Possess、登船、离船、DebugPossessShip）最后都走 Possess。
	 */
	void ApplyMouseModeForPawn(APawn* InPawn);

	/**
	 * 当前 Pawn 上、且编辑模式开着的编辑组件，否则 nullptr。
	 * 角色与星图相机各有一个这样的组件，**两者都会命中**。
	 *
	 * 不能用来进入编辑模式：模式关着时它返回空。G 键的切换不看模式，直接找 Pawn 上的组件。
	 */
	UPlanetEditComponent* GetEditComponentInEditMode() const;

	/** Pawn 上开着的编辑模式一律退出。上船 / 进出星图 / 调试换 Pawn 共用。 */
	void ForceExitEditMode(APawn* InPawn);

	// ── 输入：交互 / 登船 ──

	/** 按 E 时的分派：没在船上就尝试登船，在船上就尝试离船。 */
	void OnInteract(const FInputActionValue& Value);

	/** 尝试让角色登上指定飞船。 */
	void BoardShip(ASpaceshipPawn* Ship);

	/**
	 * 从当前飞船下船。
	 *
	 * 舱里没有角色时**生成**一个（开局操控的是飞船，角色要等第一次下船才存在）：
	 * 按 GameMode 的 DefaultPawnClass 生成在船侧，贴好球面，再 Possess。
	 */
	void LeaveShip(ASpaceshipPawn* Ship);

	/**
	 * 在飞船旁边生成一个角色（按 GameMode 的 DefaultPawnClass）。
	 *
	 * 生成位置就是离船的落脚点：船右侧 400 cm。之后的贴球面由 `LeaveShip` 统一做。
	 * @return 生成失败（类不对 / 世界里生成不出来）时返回 nullptr。
	 */
	APlanetCharacter* SpawnCrewCharacter(ASpaceshipPawn* Ship);

	// ── 输入：星图 ──

	/**
	 * 进入 / 退出星图模式。
	 *
	 * 方向由「当前 Possess 的是不是星图 Pawn」判定，**不用单独的状态标志** ——
	 * 幂等，也不会和实际 Possess 的 Pawn 不同步。
	 *
	 * 进入时聚焦**借用了相机的那颗行星**（行星相机就挂在它身上）；深空退回单例
	 * 相机时，聚焦离进场 Pawn 最近的、带星图相机的已注册天体。飞船没有星图相机，
	 * 不再被聚焦（见 HasStarMapCamera）。
	 *
	 * 做成无参是为了直接绑给 Enhanced Input：FEnhancedInputActionHandlerSignature
	 * 就是一个无参委托（EnhancedInputComponent.h:490 的 DEFINE_BIND_ACTION）。
	 */
	void ToggleStarMap();

	/**
	 * 退出星图的收尾：Possess 回 ReturnPawn，再把星图相机搬回球心。
	 *
	 * 由退出分支的定时器在视点混合结束后调用（见 StarMapExitBlendSeconds）；
	 * 混合时长为 0 或没有返回 Pawn 时直接调用。
	 */
	void FinishStarMapExit(APawn* ReturnPawn);

	/** 左键按下：记下位置，用来区分「点击」和「拖拽」。 */
	void OnStarMapSelectStarted(const FInputActionValue& Value);

	/** 左键松开：位移小于阈值算点击（拾取），否则算拖拽结束（不改变选中）。 */
	void OnStarMapSelectCompleted(const FInputActionValue& Value);

	/** 投影单颗天体。拾取与选中面板锚点共用这一条路径。 */
	bool MakeScreenCandidate(AActor* Body, FStarMapScreenCandidate& Out) const;

	// ── 输入：矿物透视 ──

	/**
	 * H 键：进入 / 退出矿物透视。只在星图里生效。
	 *
	 * 进入条件：当前选中的是体素行星、且它带地质场。失败时给 2 秒提示，不改变状态。
	 * 退出条件：再按一次 H、退出星图、换 Pawn、或选中天体变了（见 PlayerTick）。
	 */
	void OnMineralViewToggle();

	/** 数字键 1..9：切换第 ElementIndex 个元素的显示（下标从 0 起）。 */
	void OnMineralFilter(const FInputActionValue& Value, int32 ElementIndex);

	/** 数字键 0：全部打开。 */
	void OnMineralShowAll(const FInputActionValue& Value);

	/**
	 * 退出矿物透视（幂等）。退出星图 / 换 Pawn / 选中变化都走这里；
	 * 关卡结束时由组件自身的 EndPlay 兜底恢复，控制器不覆写 EndPlay。
	 */
	void ExitMineralViewIfActive();

	// ── 提示 ──

	/** 在屏幕上显示一行提示（每帧刷新，duration 0）。 */
	void ShowPrompt(const FString& Text);

	/** 带时长的提示：几秒后自动消失，与每帧刷新的 ShowPrompt 分开。 */
	void ShowPromptFor(const FString& Text, float DurationSeconds);

	// ── 调试命令 ──

	/**
	 * 调试用：直接在角色与飞船之间切换 Possess，跳过登船流程。
	 * 在 PIE 控制台输入 `DebugPossessShip` 调用。登船流程做好之后，
	 * 这个命令仍保留作为开发工具。
	 */
	UFUNCTION(Exec)
	void DebugPossessShip();

	/**
	 * 调试用：把当前飞船当成「已落地」直接走一遍下船流程（等价于按 E）。
	 *
	 * 与 DebugPossessShip 配对：它能让人进入「坐在船上但没有角色」的状态，
	 * 这条能把角色补回来。也用于无头验证（`-ExecCmds="DebugDisembark"`）。
	 */
	UFUNCTION(Exec)
	void DebugDisembark();

	// ── 运行时状态 ──

	/**
	 * 星图里左键是否按住。换 Pawn 时必须清零，否则按着左键
	 * 进出星图之后，松开事件送到新 Pawn 身上，这个标志就永远停在 true。
	 */
	bool bStarMapSelectHeld = false;

	/** 左键按下的屏幕位置（像素）。用 double 存 —— 与 GetMousePosition 的 double 重载对齐。 */
	FVector2D StarMapPressPosition = FVector2D::ZeroVector;

	/**
	 * 兜底星图相机 Pawn。懒创建并复用，不每次进出都 Spawn/Destroy。
	 *
	 * 2026-11-01 起**只是兜底**：进图优先用焦点行星自带的星图相机
	 * （见 ResolvePlanetStarMapCamera），行星没挂相机或从深空进入才用这台。
	 * 用了行星相机时本成员保持为空（或保留上一台旧的），退出不销毁任何一台 ——
	 * 行星相机随行星生死，单例兜底随控制器生死。
	 */
	UPROPERTY(Transient)
	TObjectPtr<AStarMapCameraPawn> StarMapPawn;

	/** 进入星图前 Possess 的 Pawn。退出时换回去。弱引用 —— 它可能已经没了。 */
	TWeakObjectPtr<APawn> PreviousPawn;

	/** 等待视点混合结束的定时器。还活着时再按 M 会被忽略。 */
	FTimerHandle StarMapExitTimerHandle;

	/** 全部输入动作与按键映射。定义见 StellarEraInputActions.h。 */
	UPROPERTY(Transient)
	FStellarEraInputActions Input;

	/** 当前激活的矿物透视组件。行星销毁时 GC 会把它置空。 */
	UPROPERTY(Transient)
	TObjectPtr<UPlanetMineralViewComponent> ActiveMineralView;

	// ── 可调参数 ──

	/**
	 * 拾取的像素容差。20 像素保证远处的星球也点得中。
	 *
	 * 夹到 ≥ 0：负值在拾取里没有意义，而且**新旧行为不同** —— 旧代码把
	 * `PixelRadius * PixelRadius` 当半径平方用，`-5` 会被当成 `5`；现在逐候选取
	 * `max(PixelRadius, ScreenRadiusPx)`，`-5` 被当成 `0`。这个字段是 EditAnywhere，
	 * 编辑器里能被改成负数，所以在这里挡住。
	 */
	UPROPERTY(EditAnywhere, Category = "StarMap", meta = (ClampMin = "0.0"))
	float SelectionPixelRadius = 20.f;

	/**
	 * 焦点是行星时（按 M 进场、按 H 进矿物透视），相机拉远到「地表半径 × 本倍率」。
	 *
	 * 3 倍半径能把整颗行星连同它附近的空间一起框进画面，是这个项目里行星构图的
	 * 默认档。实际距离仍会被 AStarMapCameraPawn 的「不穿行星」区间夹住。
	 */
	UPROPERTY(EditAnywhere, Category = "StarMap", meta = (ClampMin = "0.0"))
	double StarMapPlanetFocusRadiusScale = 3.0;

	/**
	 * 退出星图时视点混合的时长（秒）。0 = 立刻切回（旧行为）。
	 *
	 * 用引擎的 APlayerController::SetViewTargetWithBlend：起点是仍被 Possess 的
	 * 星图相机、终点是返回 Pawn 的相机，引擎逐帧插两条 FMinimalViewInfo ——
	 * 位置、朝向、FOV 一起过渡，飞船在动也跟着走。混合结束才真正 Possess。
	 */
	UPROPERTY(EditAnywhere, Category = "StarMap", meta = (ClampMin = "0.0"))
	float StarMapExitBlendSeconds = 0.5f;

	/**
	 * 生成星图相机用的类：优先蓝图子类 BP_StarMapCameraPawn。
	 *
	 * 笔刷区间 / 默认尺寸 / 连续挖间隔都在它的 PlanetEdit 组件细节面板里，
	 * 不用重编译就能调。蓝图缺失时退回原生 AStarMapCameraPawn，功能不坏，
	 * 但参数只能改 C++（构造函数里有 Warning）。
	 */
	UPROPERTY()
	TSubclassOf<AStarMapCameraPawn> StarMapPawnClass;

	/**
	 * 按 E 下船时生成的角色类：优先蓝图子类 BP_PlanetCharacter。
	 *
	 * 和 StarMapPawnClass 同一类东西 —— 由本控制器负责"生成谁、操控谁"，
	 * 所以配置也放在这里，不绕去 GameMode。蓝图缺失时退回原生
	 * APlanetCharacter（移动、重力、相机都在 C++ 里，只是没有模型）。
	 */
	UPROPERTY(EditDefaultsOnly, Category = "StellarEra")
	TSubclassOf<APlanetCharacter> CrewCharacterClass;
};
