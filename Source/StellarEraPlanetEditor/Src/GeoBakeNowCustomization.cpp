#include "GeoBakeNowCustomization.h"
#include "DetailWidgetRow.h"
#include "IPropertyUtilities.h"
#include "VoxelPlanetActor.h"
#include "Widgets/Input/SButton.h"

TSharedRef<IPropertyTypeCustomization> FGeoBakeNowCustomization::MakeInstance()
{
	return MakeShareable(new FGeoBakeNowCustomization);
}

void FGeoBakeNowCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	// 属性属于行星 Actor；拿它触发现有 BakeNow()。
	TArray<UObject*> Outers;
	PropertyHandle->GetOuterObjects(Outers);
	TWeakObjectPtr<AVoxelPlanetActor> Planet;
	for (UObject* Object : Outers)
	{
		if (AVoxelPlanetActor* Actor = Cast<AVoxelPlanetActor>(Object))
		{
			Planet = Actor;
			break;
		}
	}

	HeaderRow
	.NameContent()[PropertyHandle->CreatePropertyNameWidget()]
	.ValueContent().HAlign(HAlign_Fill)
	[
		SNew(SButton)
		.HAlign(HAlign_Center)
		.Text(FText::FromString(TEXT("立即烘焙")))
		.ToolTipText(FText::FromString(TEXT("忽略磁盘缓存，强制重跑一遍地质烘焙")))
		.IsEnabled_Lambda([Planet]() { return Planet.IsValid(); })
		.OnClicked_Lambda([Planet]()
		{
			if (AVoxelPlanetActor* Actor = Planet.Get())
			{
				Actor->BakeNow();
			}
			return FReply::Handled();
		})
	];
}
