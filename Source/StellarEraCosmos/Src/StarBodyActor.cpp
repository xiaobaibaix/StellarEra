#include "StarBodyActor.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "CosmosOrbitComponent.h"
#include "StarBodyComponent.h"

AStarBodyActor::AStarBodyActor()
{
	PrimaryActorTick.bCanEverTick = false;

	// 根是空场景组件而不是网格：×200000 视觉缩放会沿层级传给挂在根下的轨迹线。
	// 缩放只让 StarMesh 自己吃。
	StarRoot = CreateDefaultSubobject<USceneComponent>(TEXT("StarRoot"));
	SetRootComponent(StarRoot);

	StarMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StarMesh"));
	StarMesh->SetupAttachment(StarRoot);

	// 必须是 Movable：N 体子系统每帧把位置写回 Actor，Static 会拒绝移动并刷警告。
	StarMesh->SetMobility(EComponentMobility::Movable);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(
		TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		StarMesh->SetStaticMesh(SphereMesh.Object);
	}

	// 旧渲染标定：BasicShapes/Sphere 半径 50cm，×10000 = 5 公里；物理模式改由 ApplyScaleMode 切成真实半径。
	ApplyScaleMode(false);

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

void AStarBodyActor::ApplyScaleMode(bool bPhysical)
{
	// BasicShapes/Sphere 半径 50cm；物理模式用真实半径，远场由 Actor 缩放压到 FarScale。
	const double LocalRadiusCm = bPhysical ? double(PhysicalRadiusKm) * 1.0e5 : 5.0e5;
	StarMesh->SetRelativeScale3D(FVector(LocalRadiusCm / 50.0));
}
