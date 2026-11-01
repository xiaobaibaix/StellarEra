// Copyright Voxel Plugin SAS. All Rights Reserved.
// 玩家控制器：构造输入资产、分派输入、编排登船/离船。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Templates/SubclassOf.h"
#include "StarMapUIInputs.h"
#include "StellarEraPlayerController.generated.h"

class AStarMapCameraPawn;
class ASpaceshipPawn;
class AVoxelPlanetActor;
class UInputAction;
class UInputMappingContext;
class UPlanetMineralViewComponent;
class UPlanetEditComponent;
struct FInputActionValue;
struct FStarMapScreenCandidate;

/**
 * 星图里鼠标位移该拿去干什么。
 *
 * - `Orbit`：绕焦点转（相机位置跟着变），见 AStarMapCameraPawn::OrbitLook。
 * - `Aim`：只转视线朝向（相机位置不变），见 AStarMapCameraPawn::AimLook。
 * - `None`：正在编辑 / 连续挖，视角一律不动。
 *
 * 与「哪个键按着」分开建模：右键在普通模式与编辑模式里都归 Aim，左键在编辑
 * 模式里归挖掘（None）—— 一个 bool 表达不了这种三路分派。
 */
enum class EStarMapLookMode : uint8
{
	None,
	Orbit,
	Aim,
};

/**
 * 本项目的玩家控制器。
 *
 * 输入资产（IMC / IA）在运行时用 C++ 构造：零手工步骤，改完代码立刻能跑，
 * 不需要先在编辑器里点出一堆资产。代价是键位暂时不能在编辑器里改；等键位
 * 定稿后再换成 .uasset 是平滑的替换（数据结构一样，只是来源变了）。
 *
 * 所有动作都绑定在本控制器上，再按“当前 Possess 的是哪个 Pawn”分派。
 * 这样切换角色/飞船时不需要重新绑定输入。
 */
UCLASS()
class AStellarEraPlayerController : public APlayerController, public IStarMapUIInputs
{
	GENERATED_BODY()

public:
	AStellarEraPlayerController();

	virtual void SetupInputComponent() override;
	virtual void BeginPlay() override;

	/**
	 * 开局直接坐进最近的飞船。由 AStellarEraGameMode 在 BeginPlay 之后延迟一帧调用。
	 *
	 * 复用 BoardShip，所以角色会被登记为已登船：隐藏、附着在船上、MOVE_None。
	 * 飞船停稳时按 E 就能正常离船 —— 这是与 DebugPossessShip 的关键差别，后者
	 * 直接 Possess，不登记登船角色，因此永远不会出现离船提示。
	 *
	 * @return 找到飞船并完成登船为 true；当前 Pawn 不是角色、或关卡里没有
	 *         飞船时为 false。
	 */
	bool EnterNearestShip();

	/** 把一颗天体投到屏幕；成功时 OutCenter 为中心、OutRadiusPx 为轮廓半径（像素）。 */
	virtual bool ProjectBodyToScreen(AActor* Body, FVector2D& OutCenter, float& OutRadiusPx) const override;

	/** 任意屏幕点下的已注册天体。悬停与点击共用。 */
	virtual AActor* PickBodyUnderScreenPosition(const FVector2D& ScreenPosition) const override;

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

	/**
	 * 星图的鼠标位移该拿去哪。纯函数，OnLook 调它，测试直接调它。
	 *
	 * 星图的鼠标位移只在按住鼠标（CaptureDuringMouseDown）时才有值，所以
	 * 「按的是哪个键」只能靠按住的标志位区分。分派规则（按优先级）：
	 *   - 连续挖期间一律 None —— 挖掘点跟着光标走，视角一动就画歪（转头也算，
	 *     视线一变，屏幕射线就变了）。
	 *   - 右键 → Aim：只转头、不挪相机，任何模式都可用。
	 *   - 中键 → Orbit：编辑 / 矿物透视下左键被借走时，绕行改由它承担。
	 *   - 左键 → Orbit，但编辑模式（挖掘 / 填充）与矿物透视（图例）下左键已被
	 *     借走，只能是 None。
	 *   - 都没按 → None。
	 *
	 * 右键优先于其它键：转头不改变相机位置，同时按住时先满足它更符合直觉。
	 * 只决定「拿去哪」；缩放走 OnZoom（滚轮），任何状态下都能用。
	 */
	static EStarMapLookMode ResolveStarMapLookMode(
		bool bRightHeld,
		bool bLeftHeld,
		bool bMiddleHeld,
		bool bEditMode,
		bool bContinuousDigging,
		bool bActiveMineralView);

	/** 当前激活的矿物透视组件；未激活时为空。星图 UI 用它画图例、喂矿体显隐。 */
	virtual UPlanetMineralViewComponent* GetActiveMineralView() const override { return ActiveMineralView; }

	virtual bool GetStarMapMousePositionPx(FVector2D& OutPosition) const override;

protected:
	/** 构造 IMC 与全部 IA。幂等：重复调用会直接返回。 */
	void BuildInputAssets();

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

	virtual void OnPossess(APawn* InPawn) override;

	/**
	 * 当前 Pawn 上、且编辑模式开着的编辑组件，否则 nullptr。
	 * 角色与星图相机各有一个这样的组件，**两者都会命中**。
	 *
	 * 不能用来进入编辑模式：模式关着时它返回空。G 键的切换不看模式，直接找 Pawn 上的组件。
	 */
	UPlanetEditComponent* GetEditComponentInEditMode() const;

	/**
	 * 只认角色：当前 Possess 的是角色、且编辑模式开着时返回它的组件。
	 *
	 * 右键「点放」的按下/松开用它。**不能用通用版** —— 星图里右键是转视角，
	 * 通用版会让松开右键变成 PlaceAtScreenPosition。
	 */
	UPlanetEditComponent* GetCharacterEditComponentInEditMode() const;

	/** Pawn 上开着的编辑模式一律退出。上船 / 进出星图 / 调试换 Pawn 共用。 */
	void ForceExitEditMode(APawn* InPawn);

	// --- 角色输入回调 ---
	void OnMove(const FInputActionValue& Value);
	void OnLook(const FInputActionValue& Value);

	/** 右键按下 / 松开。只当角色视角的开关用，飞船的转向不看它。 */
	void OnLookActivateStarted(const FInputActionValue& Value);
	void OnLookActivateCompleted(const FInputActionValue& Value);

	/** 中键按下 / 松开。星图里它顶替被借走的左键做绕行，其它 Pawn 不看它。 */
	void OnLookMiddleStarted(const FInputActionValue& Value);
	void OnLookMiddleCompleted(const FInputActionValue& Value);
	void OnJump(const FInputActionValue& Value);
	void OnSprintStarted(const FInputActionValue& Value);
	void OnSprintCompleted(const FInputActionValue& Value);

	/** G 键：切换编辑模式。角色与星图相机都有编辑组件；飞船等没有，按 G 无反应。 */
	void OnEditModeToggle(const FInputActionValue& Value);

	/** `[` / `]`：编辑模式下调笔刷尺寸（±1）。星图里滚轮归缩放，所以尺寸另占一对键；角色编辑模式下滚轮也能调。 */
	void OnBrushSize(const FInputActionValue& Value);

	// --- 飞船输入回调 ---
	void OnThrust(const FInputActionValue& Value);
	void OnRoll(const FInputActionValue& Value);
	void OnVertical(const FInputActionValue& Value);
	void OnBoostStarted(const FInputActionValue& Value);
	void OnBoostCompleted(const FInputActionValue& Value);
	void OnCruise(const FInputActionValue& Value);
	void OnZoom(const FInputActionValue& Value);

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

	/** 带时长的提示：几秒后自动消失，与每帧刷新的 ShowPrompt 分开。 */
	void ShowPromptFor(const FString& Text, float DurationSeconds);

	/** 左键按下：记下位置，用来区分「点击」和「拖拽」。 */
	void OnStarMapSelectStarted(const FInputActionValue& Value);

	/** 左键松开：位移小于阈值算点击（拾取），否则算拖拽结束（不改变选中）。 */
	void OnStarMapSelectCompleted(const FInputActionValue& Value);

	/** 投影单颗天体。拾取与选中面板锚点共用这一条路径。 */
	bool MakeScreenCandidate(AActor* Body, FStarMapScreenCandidate& Out) const;

	/**
	 * 调试用：直接在角色与飞船之间切换 Possess，跳过登船流程。
	 * 在 PIE 控制台输入 `DebugPossessShip` 调用。登船流程做好之后，
	 * 这个命令仍保留作为开发工具。
	 */
	UFUNCTION(Exec)
	void DebugPossessShip();

	virtual void PlayerTick(float DeltaTime) override;

	/** 按 E 时的分派：没在船上就尝试登船，在船上就尝试离船。 */
	void OnInteract(const FInputActionValue& Value);

	/** 尝试让角色登上指定飞船。 */
	void BoardShip(ASpaceshipPawn* Ship);

	/** 尝试从当前飞船下船。 */
	void LeaveShip(ASpaceshipPawn* Ship);

	/** 在屏幕上显示一行提示（每帧刷新，duration 0）。 */
	void ShowPrompt(const FString& Text);

	/** 鼠标灵敏度（度/像素）。 */
	UPROPERTY(EditAnywhere, Category = "Input")
	float LookSensitivity = 1.2f;

	/** 上下视角反向。觉得鼠标上下颠倒时把它改成 true。 */
	UPROPERTY(EditAnywhere, Category = "Input")
	bool bInvertLookY = false;

	/**
	 * 右键是否按住。松开时角色的视角不响应鼠标 —— 鼠标划过屏幕不带画面，
	 * 角色自己拐弯（A/D 横移时 bOrientRotationToMovement 转的身）也不会被拖走。
	 */
	bool bLookHeld = false;

	/**
	 * 星图里左键是否按住。与 bLookHeld 同理：换 Pawn 时必须清零，否则按着左键
	 * 进出星图之后，松开事件送到新 Pawn 身上，这个标志就永远停在 true。
	 */
	bool bStarMapSelectHeld = false;

	/**
	 * 星图里中键是否按住。用途与 bStarMapSelectHeld 同理，但归绕行 —— 编辑模式
	 * 与矿物透视把左键借走之后，绕行只能靠中键。换 Pawn 时一并清零。
	 */
	bool bStarMapMiddleHeld = false;

	/** 左键按下的屏幕位置（像素）。用 double 存 —— 与 GetMousePosition 的 double 重载对齐。 */
	FVector2D StarMapPressPosition = FVector2D::ZeroVector;

	/** 编辑模式右键按下的屏幕位置。松开时用它区分「点击放置」与「拖拽转视角」。 */
	FVector2D EditRightPressPosition = FVector2D::ZeroVector;

	/** 右键按下时是否记下了编辑位置（不是编辑模式 / 没拿到鼠标时为 false）。 */
	bool bEditRightPressValid = false;

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
	 * 生成星图相机用的类：优先蓝图子类 BP_StarMapCameraPawn。
	 *
	 * 笔刷区间 / 默认尺寸 / 连续挖间隔都在它的 PlanetEdit 组件细节面板里，
	 * 不用重编译就能调。蓝图缺失时退回原生 AStarMapCameraPawn，功能不坏，
	 * 但参数只能改 C++（构造函数里有 Warning）。
	 */
	UPROPERTY()
	TSubclassOf<AStarMapCameraPawn> StarMapPawnClass;

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

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> MappingContext;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> IA_Move;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> IA_Look;

	/**
	 * 视角开关（鼠标右键）。按住才让角色的视角跟鼠标动。
	 *
	 * 单独做成一个布尔动作，而不是在 OnLook 里直接查 EKeys::RightMouseButton 的
	 * 键状态：键位以后要能在编辑器里改（IMC/IA 就是为此才在运行时构造的），
	 * 写死键名去轮询等于把右键钉死在这一行上。
	 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> IA_LookActivate;

	/**
	 * 星图的中键（绕行）。与 IA_LookActivate 同一个套路：IA_Look 只有鼠标位移，
	 * 「按的是哪个键」得另有布尔动作来分辨。
	 */
	TObjectPtr<UInputAction> IA_LookMiddle;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> IA_Jump;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> IA_Sprint;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> IA_Thrust;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> IA_Roll;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> IA_Vertical;

	/** 飞船加速键（Shift）。与角色的 IA_Sprint 共用按键，靠 Pawn 类型分派。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> IA_Boost;

	/** 巡航开关（Tab）。切换飞船的速度保持状态，只绑 Started。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> IA_Cruise;

	/** 相机缩放（鼠标滚轮）。轴值，每格 ±1。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> IA_Zoom;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> IA_Interact;

	/** 星图模式开关（M）。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> IA_StarMapToggle;

	/**
	 * 星图里选中天体 / 拖拽绕行（鼠标左键）。按下与松开的位移区分两者。
	 * 编辑模式与矿物透视下左键已被借走，那时拖拽是编辑 / 图例，绕行改由中键承担、
	 * 转头仍是右键 —— 见 ResolveStarMapLookMode。
	 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> IA_StarMapSelect;

	/** 编辑模式开关（G）。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> IA_EditModeToggle;

	/** 笔刷尺寸（`[` / `]`）。Axis1D，只绑 Started，不随按住每帧刷。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> IA_BrushSize;

	/** 矿物透视开关（H）。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> IA_MineralViewToggle;

	/** 数字键 1..9：按下标切换元素显示。下标 = 运行期元素顺序。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UInputAction>> IA_MineralFilter;

	/** 数字键 0：全部打开。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> IA_MineralShowAll;

	/** 当前激活的矿物透视组件。行星销毁时 GC 会把它置空。 */
	UPROPERTY(Transient)
	TObjectPtr<UPlanetMineralViewComponent> ActiveMineralView;
};
