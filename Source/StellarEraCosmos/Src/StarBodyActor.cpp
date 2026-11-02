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

	// BasicShapes/Sphere 半径 50cm。×10000 = 5e5cm = 5 公里半径，
	// 在 1e8cm（100 AU）处张角 0.57°，与真太阳的 0.53° 基本一致。
	StarMesh->SetRelativeScale3D(FVector(10000.0f));

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
