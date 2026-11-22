// 编辑器专用控制 Actor：什么都不做，只承载编辑器里要挂在关卡上的参数开关。
// IsEditorOnly + 游戏世界自毁：打包与 PIE 里不会留下它。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EditorPlanetControls.generated.h"

class USceneComponent;

UCLASS(meta = (DisplayName = "EditorPlanetControls"))
class STELLARERAPLANETEDITOR_API AEditorPlanetControls : public AActor
{
	GENERATED_BODY()

public:
	AEditorPlanetControls();

	virtual bool IsEditorOnly() const override { return true; }
	virtual void PostLoad() override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void BeginPlay() override;

	/** 勾上后编辑器里按映射摆放天体（不跑物理）；取消即可自由编辑。 */
	UPROPERTY(EditAnywhere, Category = "Editor|Mapping", meta = (DisplayName = "EditorMappingPreview"))
	bool bEditorMappingPreview = false;

private:
	void ApplyPreviewCvar();
};
