// Copyright Voxel Plugin SAS. All Rights Reserved.
// 玩家控制器：构造输入资产、分派输入、编排登船/离船。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Templates/SubclassOf.h"
#include "SessionCommands.h"
#include "StarMapUIInputs.h"
#include "StarMapCameraInputComponent.h"
#include "StellarEraInputActions.h"
#include "StellarEraInputSource.h"
#include "StellarEraPlayerController.generated.h"

class APlanetCharacter;
class AStarMapCameraPawn;
class ASpaceshipPawn;
class UPlanetMineralViewComponent;
class UPlanetEditComponent;

/**
 * 本项目的玩家控制器。
 *
 * 输入资产（IMC / IA）在运行时用 C++ 构造：零手工步骤，改完代码立刻能跑，
 * 不需要先在编辑器里点出一堆资产。代价是键位暂时不能在编辑器里改；等键位
 * 定稿后再换成 .uasset 是平滑的替换（数据结构一样，只是来源变了）。
 *
 * 输入绑定按「谁被操控谁绑」分：
 *   - **角色 / 飞船 / 星图相机**在自己的 `SetupPlayerInputComponent` 里绑全部输入，
 *     动作表由本类经 `IStellarEraInputSource` 提供 —— 所以这里没有按 Pawn 类型
 *     分派的三路 `Cast`，新增一种可操控 Pawn 也不用改本类；
 *   - **本类不再绑任何输入**（`SetupInputComponent` 已删除）。M（星图开关）与
 *     E（上下船）在每个需要它的 Pawn 表里各有一条动作，按下去经
 *     `IStellarEraSessionCommands` 转发回来；星图左键选择归相机，角色左键只
 *     服务编辑模式；星图的视角、缩放、编辑、矿物透视（H 与数字键）也都归相机。
 *
 * 同时实现四个跨模块接口，全部是**依赖倒置**的钩子：声明方在 Pawns / UI
 * （下游），实现方是本类（Game，最上层），消费方各自 `Cast<接口>(Controller)`。
 *   - `IStellarEraInputSource`：三个 Pawn 的输入组件来取运行时构造的动作表；
 *   - `IStarMapUIInputs`：HUD / 星图面板查询鼠标位置、屏幕拾取、矿物透视；
 *   - `IStarMapLookContext`：星图相机进不了矿物透视时借控制器弹提示；
 *   - `IStellarEraSessionCommands`：各 Pawn 的 M / E 转发到这里的会话流程。
 * 除了提示出口与会话命令，其余都只读。再加接口前先确认：这个状态是不是真的
 * 只有控制器知道。
 */
UCLASS()
class AStellarEraPlayerController : public APlayerController, public IStarMapUIInputs,
	public IStellarEraInputSource, public IStarMapLookContext,
	public IStellarEraSessionCommands
{
	GENERATED_BODY()

public:
	// ── 构造与生命周期 ──

	AStellarEraPlayerController();

	virtual void BeginPlay() override;

	// ── 接口实现 ──
	// 四个都是对别的模块的契约，统一放 public：没必要一个 public 一个 protected。

	//~ Begin IStellarEraInputSource Interface
	// 三张表各交给它的消费方；聚合结构体只有本类看得见。实现见 .cpp（幂等 Build）。
	virtual const FPlanetCharacterInputActions& GetPlanetCharacterInputActions() override;
	virtual const FSpaceshipInputActions& GetSpaceshipInputActions() override;
	virtual const FStarMapCameraInputActions& GetStarMapCameraInputActions() override;
	//~ End IStellarEraInputSource Interface

	//~ Begin IStarMapUIInputs Interface
	/** 把一颗天体投到屏幕；成功时 OutCenter 为中心、OutRadiusPx 为轮廓半径（像素）。 */
	virtual bool ProjectBodyToScreen(AActor* Body, FVector2D& OutCenter, float& OutRadiusPx) const override;

	/** 任意屏幕点下的已注册天体。悬停与点击共用。 */
	virtual AActor* PickBodyUnderScreenPosition(const FVector2D& ScreenPosition) const override;

	/** 当前激活的矿物透视组件；未激活时为空。星图 UI 用它画图例、喂矿体显隐。 */
	virtual UPlanetMineralViewComponent* GetActiveMineralView() const override;

	virtual bool GetStarMapMousePositionPx(FVector2D& OutPosition) const override;
	//~ End IStarMapUIInputs Interface

	//~ Begin IStarMapLookContext Interface
	/** 相机没有 HUD：矿物透视进不去时借这里弹一条带时长的提示。 */
	virtual void ShowStarMapPrompt(const FString& Text, float Seconds) override;
	//~ End IStarMapLookContext Interface

	//~ Begin IStellarEraSessionCommands Interface
	/**
	 * M：进入 / 退出星图模式。
	 *
	 * 方向由「当前 Possess 的是不是星图 Pawn」判定，**不用单独的状态标志** ——
	 * 幂等，也不会和实际 Possess 的 Pawn 不同步。
	 *
	 * 进入时聚焦**借用了相机的那颗行星**（行星相机就挂在它身上）；深空退回单例
	 * 相机时，聚焦离进场 Pawn 最近的、带星图相机的已注册天体。飞船没有星图相机，
	 * 不再被聚焦（见 AStarMapCameraPawn::HasStarMapCamera）。
	 */
	virtual void ToggleStarMap() override;

	/** E：按 E 时的分派 —— 没在船上就尝试登船，在船上就尝试离船。 */
	virtual void InteractWithShip() override;
	//~ End IStellarEraSessionCommands Interface

	// ── 静态查询（无状态，能无头测）──

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

	// ── 会话：登船 ──

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

	// ── 会话：星图 ──

	/**
	 * 退出星图的收尾：Possess 回 ReturnPawn，再把星图相机搬回球心。
	 *
	 * 由退出分支的定时器在视点混合结束后调用（见 StarMapExitBlendSeconds）；
	 * 混合时长为 0 或没有返回 Pawn 时直接调用。
	 */
	void FinishStarMapExit(APawn* ReturnPawn);

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

	/** 三张 Pawn 动作表与共享 IMC。定义见 StellarEraInputActions.h。 */
	UPROPERTY(Transient)
	FStellarEraInputActions Input;

	// ── 可调参数 ──

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
	UPROPERTY(EditDefaultsOnly, Category = "StellarEra")
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
