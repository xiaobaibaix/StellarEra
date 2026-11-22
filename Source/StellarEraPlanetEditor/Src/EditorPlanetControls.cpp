#include "EditorPlanetControls.h"

#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

AEditorPlanetControls::AEditorPlanetControls()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
}

void AEditorPlanetControls::ApplyPreviewCvar()
{
	if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("StellarEra.Cosmos.EditorPreview")))
	{
		CVar->Set(bEditorMappingPreview ? 1 : 0);
	}
}

void AEditorPlanetControls::PostLoad()
{
	Super::PostLoad();

	// 关卡里保存过勾选状态时，加载后要把它重新推给 CVar（CVar 不随关卡存）。
	ApplyPreviewCvar();
}

void AEditorPlanetControls::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(AEditorPlanetControls, bEditorMappingPreview))
	{
		ApplyPreviewCvar();
	}
}

void AEditorPlanetControls::BeginPlay()
{
	Super::BeginPlay();

	// 双保险：万一 PIE 把编辑器专用 Actor 也复制进来，这里直接销毁。
	if (GetWorld() && GetWorld()->IsGameWorld())
	{
		Destroy();
	}
}
