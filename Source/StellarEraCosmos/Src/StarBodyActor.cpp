#include "StarBodyActor.h"

#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "CosmosOrbitComponent.h"
#include "StarBodyComponent.h"

AStarBodyActor::AStarBodyActor()
{
	PrimaryActorTick.bCanEverTick = false;

	StarMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StarMesh"));
	SetRootComponent(StarMesh);

	// 必须是 Movable：N 体子系统每帧把位置写回 Actor，Static 会拒绝移动并刷警告。
	StarMesh->SetMobility(EComponentMobility::Movable);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(
		TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		StarMesh->SetStaticMesh(SphereMesh.Object);
	}

	// BasicShapes/Sphere 半径 50cm。×200000 = 1e7cm = 100 公里半径。
	//
	// 关卡里行星轨道半径就是 1e8cm（100 公里），所以日面正好穿过行星球心 ——
	// 这是 2026-11-02 明确选定的观感（"恒星应该很大"），不是笔误：行星有半个
	// 身子埋在日面里。恒星材质是单面的，从行星上看不到日面内侧。
	// 想退回"真太阳"视角（0.57°）就把这里和 BP_Star 的 StarMesh 缩放一起改回 10000。
	StarMesh->SetRelativeScale3D(FVector(200000.0f));

	// 恒星不参与碰撞，也不投阴影：它是自发光体，阴影只由 DirectionalLight 产生。
	StarMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	StarMesh->SetCastShadow(false);

	// 默认吃 G2V（类太阳）预设，关卡实例按需换 M2V / B3V。
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> SunMaterial(
		TEXT("/Game/Code/Res/Star/Materials/MI_Sun_G2V.MI_Sun_G2V"));
	if (SunMaterial.Succeeded())
	{
		StarMesh->SetMaterial(0, SunMaterial.Object);
	}

	Orbit = CreateDefaultSubobject<UCosmosOrbitComponent>(TEXT("Orbit"));
	StarBody = CreateDefaultSubobject<UStarBodyComponent>(TEXT("StarBody"));
}
