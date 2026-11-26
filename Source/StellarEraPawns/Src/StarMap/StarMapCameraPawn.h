// Copyright Voxel Plugin SAS. All Rights Reserved.
// 星图相机载体：被 Possess 期间由 UStarMapRigComponent 接管相机。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "StarMapCameraPawn.generated.h"

class UInputComponent;
class USceneComponent;
class UStarMapInputComponent;
class UStarMapRigComponent;
struct FMinimalViewInfo;

/**
 * 星图相机 Pawn：控制器按 M 时生成并 Possess，内部 rig 现算相机位置。
 * 焦点天体、编辑组件与退场相机都从源 Pawn（飞船 / 角色）取，本类不自己维护。
 */
UCLASS()
class STELLARERAPAWNS_API AStarMapCameraPawn : public APawn
{
	GENERATED_BODY()

public:
	AStarMapCameraPawn();

	//~ Begin APawn Interface
	virtual void CalcCamera(float DeltaSeconds, FMinimalViewInfo& OutResult) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	//~ End APawn Interface

	void SetSourcePawn(APawn* InSourcePawn);
	APawn* GetSourcePawn() const { return SourcePawn.Get(); }

	UStarMapRigComponent* GetRig() const { return StarMapRig; }

private:
	UPROPERTY(VisibleAnywhere, Category = "StarMap", meta = (DisplayName = "StarMapRoot"))
	TObjectPtr<USceneComponent> StarMapRoot;

	UPROPERTY(VisibleAnywhere, Category = "StarMap", meta = (DisplayName = "StarMapRig"))
	TObjectPtr<UStarMapRigComponent> StarMapRig;

	UPROPERTY(VisibleAnywhere, Category = "StarMap", meta = (DisplayName = "StarMapInput"))
	TObjectPtr<UStarMapInputComponent> StarMapInput;

	/** 退图时 Possess 回去的 Pawn；星图期间可能失效。 */
	TWeakObjectPtr<APawn> SourcePawn;
};
