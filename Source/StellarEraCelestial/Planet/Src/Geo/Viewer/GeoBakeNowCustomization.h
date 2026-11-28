#if WITH_EDITOR
// 把 FGeoBakeNowAction 属性行画成「立即烘焙」按钮；只改行外观，不动分类嵌套。
#pragma once
#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

class FGeoBakeNowCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle,
		FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle,
		IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override {}
};
#endif
