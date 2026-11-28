#include "CoreMinimal.h"

#if WITH_EDITOR
#include "Components/ActorComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/InheritableComponentHandler.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/Package.h"

namespace
{
	// 一次性清理：原生组件移除后，蓝图资产里可能残留 SCS 节点 / 组件模板 / 继承覆盖，
	// 这三处都删掉，组件才会从 BP_VoxelPlanet 上真正消失。
	FAutoConsoleCommand RemoveInheritedSunDriver(
		TEXT("StellarEra.Planet.RemoveInheritedSunDriver"),
		TEXT("删除 BP_VoxelPlanet 里 SunLightDriver 的残留组件数据"),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr,
				TEXT("/Game/Code/Planet/BP_VoxelPlanet.BP_VoxelPlanet"));
			if (!Blueprint)
			{
				UE_LOG(LogTemp, Warning, TEXT("[清理] 找不到 BP_VoxelPlanet"));
				return;
			}

			const FName TargetName(TEXT("SunLightDriver"));
			int32 Removed = 0;

			if (USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript)
			{
				TArray<USCS_Node*> Nodes = SCS->GetAllNodes();
				for (USCS_Node* Node : Nodes)
				{
					UE_LOG(LogTemp, Display, TEXT("[清理] SCS 节点：%s"),
						Node ? *Node->GetVariableName().ToString() : TEXT("null"));
					if (Node && Node->GetVariableName() == TargetName)
					{
						SCS->RemoveNode(Node);
						++Removed;
					}
				}
			}

			if (UBlueprintGeneratedClass* BPGC = Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass))
			{
				for (int32 Index = BPGC->ComponentTemplates.Num() - 1; Index >= 0; --Index)
				{
					UActorComponent* Template = BPGC->ComponentTemplates[Index];
					UE_LOG(LogTemp, Display, TEXT("[清理] 模板：%s"),
						Template ? *Template->GetName() : TEXT("null"));
					if (Template && Template->GetFName() == TargetName)
					{
						BPGC->ComponentTemplates.RemoveAt(Index);
						++Removed;
					}
				}
			}

			if (UInheritableComponentHandler* Handler = Blueprint->GetInheritableComponentHandler(false))
			{
				const FComponentKey Key = Handler->FindKey(TargetName);
				if (Handler->GetOverridenComponentTemplate(Key))
				{
					Handler->RemoveOverridenComponentTemplate(Key);
					++Removed;
				}
			}

			// 原生组件早先被序列化进 CDO：把它挪出蓝图包，保存后不再随资产加载。
			if (UClass* GeneratedClass = Blueprint->GeneratedClass)
			{
				if (AActor* ActorCDO = Cast<AActor>(GeneratedClass->GetDefaultObject()))
				{
					TArray<UActorComponent*> Components;
					ActorCDO->GetComponents(Components);
					for (UActorComponent* Component : Components)
					{
						if (Component && Component->GetFName() == TargetName)
						{
							Component->Rename(nullptr, GetTransientPackage(),
								REN_DontCreateRedirectors | REN_NonTransactional);
							++Removed;
						}
					}
				}
			}

			FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
			FKismetEditorUtilities::CompileBlueprint(Blueprint);
			UE_LOG(LogTemp, Display, TEXT("[清理] SunLightDriver 清理完成，删除 %d 处"), Removed);
		}));
}
#endif
