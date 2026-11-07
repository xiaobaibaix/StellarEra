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

	// 根是空场景组件而不是网格：×200000 视觉缩放会沿层级传给挂在根下的星图相机
	// 与轨迹线。缩放只让 StarMesh 自己吃。
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

	// BasicShapes/Sphere 半径 50cm，×200000 = 100 公里。行星轨道半径也是 1e8cm，
	// 所以日面正好穿过行星球心 —— 这是选定的观感（行星半个身子埋在日面里），不是
	// 笔误；恒星材质单面，从行星上看不到内侧。想退回「真太阳」（0.57°）改回 10000。
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
