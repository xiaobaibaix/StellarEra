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
class UInputAction;
class UInputMappingContext;
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
	 * 进入星图时该以谁为焦点。
	 *
	 * 驾驶中 → 当前飞船；步行 → 角色最近的那艘飞船（玩家口中的「当前飞船」）；
	 * 其他 Pawn / 空 → 空（相机保持冻结）。
	 *
	 * 步行也必须给焦点：没有焦点时星图相机停在角色眼位、UI 又因为没有选中
	 * 而不画任何东西，玩家看到的就是「按 M 没反应」。
	 */
	static ASpaceshipPawn* ResolveStarMapFocusShip(APawn* EnteringPawn);

	/**
	 * 换视点的唯一驱动：把 World 里所有实现 IVoxelViewpointInvokers 的 Pawn
	 * 按 InPawn 设成视点，只有 InPawn 拿到 true。InPawn 为空（OnUnPossess
	 * 路径）时全部关掉。
	 *
	 * 静态是为了让测试能在没有控制器实例的情况下验它 —— 与
	 * ResolveStarMapFocusShip 同理。不实现本接口的 Pawn 不受影响。
	 */
	static void ApplyViewpointInvokerGate(UWorld* World, const APawn* InPawn);

	/**
	 * 星图的鼠标位移该不该用来转视角。纯函数，OnLook 调它，测试直接调它。
	 *
	 * 星图的鼠标位移只在按住鼠标（CaptureDuringMouseDown）时才有值，所以
	 * 「现在按的是左键还是右键」只能靠 bLookHeld（右键）区分：
	 *   - 连续挖期间：左右都不转 —— 挖掘点跟着光标走，视角一动就画歪。
	 *   - 编辑模式 / 矿物透视：左键已被借走（挖掘填充 / 图例），只有右键能转。
	 *   - 其余：左键拖拽就转（也是选中时区分点击与拖拽的那条路）。
	 *
	 * 只决定「转不转」；缩放走 OnZoom（滚轮），任何状态下都能用。
	 */
	static bool ShouldOrbitStarMapLook(
		bool bLookHeld, bool bEditMode, bool bContinuousDigging, bool bActiveMineralView);

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
	 * 进入时若当前是飞船 → 聚焦它；若当前是角色 → 聚焦角色最近的飞船
	 * （见 ResolveStarMapFocusShip）。
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
	 * 按 M 进星图时，相机以焦点飞船为中心拉远到的距离（厘米）。
	 * 驾驶中焦点就是当前飞船；步行时是角色最近的飞船。
	 *
	 * 世界尺度：1 AU = 1e6 厘米。5e5 = 0.5 AU 只是**期望**距离，实际会被
	 * AStarMapCameraPawn 的「不穿行星」区间夹住：视线上有行星时只能拉到命中点
	 * 之前，玩家把视角抬离地面后才能继续拉远。
	 */
	UPROPERTY(EditAnywhere, Category = "StarMap", meta = (ClampMin = "0.0"))
	double StarMapFocusDistanceCm = 5.0e5;

	/**
	 * 生成星图相机用的类：优先蓝图子类 BP_StarMapCameraPawn。
	 *
	 * 笔刷区间 / 默认尺寸 / 连续挖间隔都在它的 PlanetEdit 组件细节面板里，
	 * 不用重编译就能调。蓝图缺失时退回原生 AStarMapCameraPawn，功能不坏，
	 * 但参数只能改 C++（构造函数里有 Warning）。
	 */
	UPROPERTY()
	TSubclassOf<AStarMapCameraPawn> StarMapPawnClass;

	/** 星图相机 Pawn。懒创建并复用，不每次进出都 Spawn/Destroy。 */
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
	 * 星图里选中天体 / 拖拽转视角（鼠标左键）。按下与松开的位移区分两者。
	 * 编辑模式与矿物透视下左键已被借走，那时拖拽是编辑 / 图例，转视角改用右键
	 * —— 见 ShouldOrbitStarMapLook。
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
