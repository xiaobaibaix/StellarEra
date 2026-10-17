// Copyright Voxel Plugin SAS. All Rights Reserved.
// 屏幕 HUD：常驻的飞船状态条 + 按 Tab 等操作弹出的短暂提示。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "StellarEraHUD.generated.h"

class ASpaceshipPawn;
struct FSpaceshipStatus;

/**
 * 纯 C++ 的调试 HUD。
 *
 * 用 AHUD + Canvas 直接画，不建 UMG 资产：与本项目「输入资产也在 C++ 里造」的
 * 做法一致 —— 零手工步骤，改完代码立刻能跑。
 *
 * 依赖方向：本类在 StellarEra 模块，飞船在 StellarEraVehicle 模块。Build.cs 里
 * 后者是 **私有**依赖，所以这里只前置声明 ASpaceshipPawn / FSpaceshipStatus，
 * 真正的 include 放在 .cpp —— 否则本头文件（公开头）就要求把 StellarEraVehicle
 * 提升成公开依赖，整个依赖层级都要跟着改。
 *
 * 字体用的是引擎默认字体（GEngine->GetMediumFont()），与
 * AStellarEraPlayerController::ShowPrompt 走 AddOnScreenDebugMessage 时同一个。
 * 项目 Content 下没有任何字体资产，所以中文能不能显示取决于该字体有没有中文字形
 * —— 若屏幕上的「按 E 登船」显示正常，这里就正常。
 */
UCLASS()
class AStellarEraHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

	/**
	 * 弹一条提示，NoticeDuration 秒后自动消失（末尾 NoticeFadeDuration 秒淡出）。
	 * 重复调用会替换掉上一条，不排队。
	 */
	void ShowNotice(const FString& Text);

protected:
	/** 提示停留时长（秒），之后开始淡出。 */
	UPROPERTY(EditAnywhere, Category = "HUD")
	float NoticeDuration = 3.f;

	/** 淡出时长（秒），取 NoticeDuration 的末尾这一段。 */
	UPROPERTY(EditAnywhere, Category = "HUD")
	float NoticeFadeDuration = 0.6f;

	/** 状态条距屏幕左上角的边距（像素）。 */
	UPROPERTY(EditAnywhere, Category = "HUD")
	float PanelMargin = 20.f;

	/** 面板内边距（像素），同时用作提示框的内边距。 */
	UPROPERTY(EditAnywhere, Category = "HUD")
	float PanelPadding = 10.f;

	/** 行间距（像素）。 */
	UPROPERTY(EditAnywhere, Category = "HUD")
	float LineSpacing = 4.f;

	/** 文字缩放。 */
	UPROPERTY(EditAnywhere, Category = "HUD")
	float TextScale = 1.1f;

private:
	/** 当前 Possess 的 Pawn 是飞船就返回它，否则返回空（此时不画状态条）。 */
	ASpaceshipPawn* GetViewedShip() const;

	/** 画左上角的状态条。 */
	void DrawStatusPanel(const FSpaceshipStatus& Status);

	/** 画屏幕中间的提示框；没有提示或已过期就什么都不做。 */
	void DrawNotice();

	/** 当前提示的文案。空表示没有提示。 */
	FString NoticeText;

	/** 提示出现的时刻（世界时间，秒）。 */
	float NoticeStartTime = 0.f;
};
