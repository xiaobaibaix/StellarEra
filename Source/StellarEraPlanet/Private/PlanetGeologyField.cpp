// Copyright Voxel Plugin SAS. All Rights Reserved.
// JSON 解析与地质场采样。

#include "PlanetGeologyField.h"

#include "Dom/JsonObject.h"
#include "FastNoise/VoxelFastNoise.inl"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	constexpr double ThreeSigma = 3.0;

	bool ParseJsonObject(const FString& Json, const TCHAR* SourceName,
		TSharedPtr<FJsonObject>& OutRoot, FPlanetGeologyLoadReport& Report)
	{
		if (Json.IsEmpty())
		{
			Report.Errors.Add(FString::Printf(TEXT("%s: 内容为空"), SourceName));
			return false;
		}

		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		if (!FJsonSerializer::Deserialize(Reader, OutRoot) || !OutRoot.IsValid())
		{
			Report.Errors.Add(FString::Printf(TEXT("%s: JSON 语法错误"), SourceName));
			return false;
		}
		return true;
	}

	bool TryReadObject(const TSharedPtr<FJsonValue>& Value, const TSharedPtr<FJsonObject>*& OutObject)
	{
		OutObject = nullptr;
		return Value.IsValid()
			&& Value->TryGetObject(OutObject)
			&& OutObject
			&& OutObject->IsValid();
	}

	void ReadVersion(const TSharedPtr<FJsonObject>& Root, const TCHAR* SourceName,
		FPlanetGeologyLoadReport& Report)
	{
		double Version = 1.0;
		if (!Root->TryGetNumberField(TEXT("version"), Version))
		{
			return;
		}
		if (!FMath::IsNearlyEqual(Version, 1.0))
		{
			Report.Warnings.Add(FString::Printf(
				TEXT("%s: version %g != 1，仍按当前 schema 解析"), SourceName, Version));
		}
	}

	float ReadClampedFloat(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName,
		float Default, float Min, float Max, const FString& Context,
		FPlanetGeologyLoadReport& Report)
	{
		double Raw = 0.0;
		if (!Object->TryGetNumberField(FieldName, Raw))
		{
			return Default;
		}

		const float Value = float(Raw);
		const float Clamped = FMath::Clamp(Value, Min, Max);
		if (!FMath::IsNearlyEqual(Value, Clamped))
		{
			Report.Warnings.Add(FString::Printf(
				TEXT("%s.%s = %f 越界，夹到 [%f, %f]"), *Context, FieldName, Value, Min, Max));
		}
		return Clamped;
	}

	int32 ReadClampedInt(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName,
		int32 Default, int32 Min, int32 Max, const FString& Context,
		FPlanetGeologyLoadReport& Report)
	{
		double Raw = 0.0;
		if (!Object->TryGetNumberField(FieldName, Raw))
		{
			return Default;
		}

		const int32 Value = FMath::RoundToInt(Raw);
		const int32 Clamped = FMath::Clamp(Value, Min, Max);
		if (Value != Clamped)
		{
			Report.Warnings.Add(FString::Printf(
				TEXT("%s.%s = %d 越界，夹到 [%d, %d]"), *Context, FieldName, Value, Min, Max));
		}
		return Clamped;
	}

	bool ParseElementTable(const TSharedPtr<FJsonObject>& Root,
		TMap<FString, FPlanetGeologyElement>& OutTable, FPlanetGeologyLoadReport& Report)
	{
		ReadVersion(Root, TEXT("Elements.json"), Report);

		const TArray<TSharedPtr<FJsonValue>>* Elements = nullptr;
		if (!Root->TryGetArrayField(TEXT("elements"), Elements) || !Elements)
		{
			Report.Errors.Add(TEXT("Elements.json: 缺少 elements 数组"));
			return false;
		}

		for (int32 Index = 0; Index < Elements->Num(); ++Index)
		{
			const TSharedPtr<FJsonObject>* ObjectPtr = nullptr;
			if (!TryReadObject((*Elements)[Index], ObjectPtr))
			{
				Report.Warnings.Add(FString::Printf(
					TEXT("Elements.json[%d]: 不是对象，跳过"), Index));
				continue;
			}
			const TSharedPtr<FJsonObject>& Object = *ObjectPtr;

			FString Id;
			if (!Object->TryGetStringField(TEXT("id"), Id) || Id.IsEmpty())
			{
				Report.Warnings.Add(FString::Printf(
					TEXT("Elements.json[%d]: 缺少 id，跳过"), Index));
				continue;
			}
			if (OutTable.Contains(Id))
			{
				Report.Warnings.Add(FString::Printf(
					TEXT("Elements.json: 重复 id '%s'，保留第一条"), *Id));
				continue;
			}

			FPlanetGeologyElement Element;
			Element.Id = Id;
			Object->TryGetStringField(TEXT("displayName"), Element.DisplayName);

			const TArray<TSharedPtr<FJsonValue>>* ColorValues = nullptr;
			if (Object->TryGetArrayField(TEXT("color"), ColorValues) && ColorValues && ColorValues->Num() >= 3)
			{
				double R = 1.0;
				double G = 1.0;
				double B = 1.0;
				if ((*ColorValues)[0]->TryGetNumber(R)
					&& (*ColorValues)[1]->TryGetNumber(G)
					&& (*ColorValues)[2]->TryGetNumber(B))
				{
					Element.Color = FLinearColor(
						FMath::Clamp(float(R), 0.f, 1.f),
						FMath::Clamp(float(G), 0.f, 1.f),
						FMath::Clamp(float(B), 0.f, 1.f),
						1.f);
				}
			}

			const TSharedPtr<FJsonObject>* NoisePtr = nullptr;
			if (Object->TryGetObjectField(TEXT("noise"), NoisePtr) && NoisePtr && NoisePtr->IsValid())
			{
				const TSharedPtr<FJsonObject>& Noise = *NoisePtr;
				const FString Context = FString::Printf(TEXT("Elements.json[%s].noise"), *Id);
				Element.Frequency = ReadClampedFloat(Noise, TEXT("frequency"),
					Element.Frequency, 0.01f, 64.f, Context, Report);
				Element.Lacunarity = ReadClampedFloat(Noise, TEXT("lacunarity"),
					Element.Lacunarity, 1.f, 8.f, Context, Report);
				Element.Gain = ReadClampedFloat(Noise, TEXT("gain"),
					Element.Gain, 0.f, 1.f, Context, Report);
				Element.DepthScale = ReadClampedFloat(Noise, TEXT("depthScale"),
					Element.DepthScale, 0.01f, 1.f, Context, Report);
				Element.Octaves = ReadClampedInt(Noise, TEXT("octaves"),
					Element.Octaves, 1, 6, Context, Report);
				Element.Seed = ReadClampedInt(Noise, TEXT("seed"),
					Element.Seed, MIN_int32, MAX_int32, Context, Report);
			}

			OutTable.Add(Id, Element);
		}
		return true;
	}

	bool ParsePlanet(const TSharedPtr<FJsonObject>& Root,
		const TMap<FString, FPlanetGeologyElement>& Table,
		FPlanetGeologyAppearance& OutAppearance, float& OutGeometryAmplitude,
		TArray<FPlanetGeologyElement>& OutElements, FPlanetGeologyLoadReport& Report)
	{
		ReadVersion(Root, TEXT("Planets/<preset>.json"), Report);

		double GeometryAmplitude = 1.5;
		if (Root->TryGetNumberField(TEXT("geometryAmplitude"), GeometryAmplitude))
		{
			if (GeometryAmplitude < 0.0)
			{
				Report.Warnings.Add(FString::Printf(
					TEXT("geometryAmplitude %f < 0，按 0 处理"), GeometryAmplitude));
				GeometryAmplitude = 0.0;
			}
		}
		OutGeometryAmplitude = float(GeometryAmplitude);

		const TSharedPtr<FJsonObject>* AppearancePtr = nullptr;
		if (Root->TryGetObjectField(TEXT("appearance"), AppearancePtr) && AppearancePtr && AppearancePtr->IsValid())
		{
			const TSharedPtr<FJsonObject>& Appearance = *AppearancePtr;
			OutAppearance.ColorThreshold = ReadClampedFloat(Appearance, TEXT("colorThreshold"),
				OutAppearance.ColorThreshold, 0.f, 1.f, TEXT("appearance"), Report);
			OutAppearance.FadeRange = ReadClampedFloat(Appearance, TEXT("fadeRange"),
				OutAppearance.FadeRange, 0.001f, 1.f, TEXT("appearance"), Report);
			OutAppearance.PurityForFullColor = ReadClampedFloat(Appearance, TEXT("purityForFullColor"),
				OutAppearance.PurityForFullColor, 0.01f, 1.f, TEXT("appearance"), Report);
		}

		const TSharedPtr<FJsonObject>* ElementsPtr = nullptr;
		if (!Root->TryGetObjectField(TEXT("elements"), ElementsPtr) || !ElementsPtr || !ElementsPtr->IsValid())
		{
			Report.Errors.Add(TEXT("行星 JSON: 缺少 elements 对象"));
			return false;
		}

		// UE 5.8 的 FJsonObject 键是 UE::FSharedString（不是 FString），
		// 取值时显式转成 FString。
		for (const auto& Pair : (*ElementsPtr)->Values)
		{
			const FString ElementId(*Pair.Key);

			const FPlanetGeologyElement* Definition = Table.Find(ElementId);
			if (!Definition)
			{
				Report.Warnings.Add(FString::Printf(
					TEXT("行星 JSON: 未知元素 '%s'，跳过"), *ElementId));
				continue;
			}

			const TSharedPtr<FJsonObject>* ElementPtr = nullptr;
			if (!Pair.Value.IsValid()
				|| !Pair.Value->TryGetObject(ElementPtr)
				|| !ElementPtr
				|| !ElementPtr->IsValid())
			{
				Report.Warnings.Add(FString::Printf(
					TEXT("行星 JSON: 元素 '%s' 的值不是对象，跳过"), *ElementId));
				continue;
			}

			const TSharedPtr<FJsonObject>& ElementObject = *ElementPtr;
			const float Abundance = ReadClampedFloat(ElementObject, TEXT("abundance"),
				0.f, 0.f, 1.f, ElementId, Report);
			if (Abundance <= 0.f)
			{
				continue;
			}

			double PeakDepth = 0.0;
			ElementObject->TryGetNumberField(TEXT("peakDepth"), PeakDepth);

			double LayerThickness = 0.0;
			if (!ElementObject->TryGetNumberField(TEXT("layerThickness"), LayerThickness)
				|| LayerThickness <= 0.0)
			{
				Report.Warnings.Add(FString::Printf(
					TEXT("行星 JSON: '%s' 的 layerThickness 必须 > 0，按不存在处理"), *ElementId));
				continue;
			}

			FPlanetGeologyElement Element = *Definition;
			Element.Abundance = Abundance;
			Element.PeakDepth = float(PeakDepth);
			Element.LayerThickness = float(LayerThickness);
			Element.ReachDepth = float(FMath::Abs(PeakDepth) + ThreeSigma * LayerThickness);
			OutElements.Add(Element);
		}
		return true;
	}

	bool LoadTextFile(const FString& RelativePath, FString& OutText)
	{
		const FString FullPath = FPaths::Combine(FPaths::ProjectContentDir(), RelativePath);
		return FFileHelper::LoadFileToString(OutText, *FullPath);
	}

	bool IsValidPresetName(const FString& PresetName)
	{
		return !PresetName.IsEmpty()
			&& !PresetName.Contains(TEXT("/"))
			&& !PresetName.Contains(TEXT("\\"))
			&& !PresetName.Contains(TEXT(".."));
	}
}

TSharedPtr<const FPlanetGeologyField> FPlanetGeologyField::LoadFromString(
	const FString& ElementsJson, const FString& PlanetJson, FPlanetGeologyLoadReport& OutReport)
{
	TSharedPtr<FPlanetGeologyField> Field = MakeShared<FPlanetGeologyField>();

	TSharedPtr<FJsonObject> ElementsRoot;
	if (!ParseJsonObject(ElementsJson, TEXT("Elements.json"), ElementsRoot, OutReport))
	{
		return Field;
	}

	TMap<FString, FPlanetGeologyElement> Table;
	if (!ParseElementTable(ElementsRoot, Table, OutReport))
	{
		return Field;
	}

	TSharedPtr<FJsonObject> PlanetRoot;
	if (!ParseJsonObject(PlanetJson, TEXT("Planets/<preset>.json"), PlanetRoot, OutReport))
	{
		return Field;
	}

	TArray<FPlanetGeologyElement> Definitions;
	FPlanetGeologyAppearance Appearance;
	float GeometryAmplitude = 1.5f;
	if (!ParsePlanet(PlanetRoot, Table, Appearance, GeometryAmplitude, Definitions, OutReport))
	{
		return Field;
	}

	Field->Appearance = Appearance;
	Field->GeometryAmplitude = FMath::Max(0.f, GeometryAmplitude);

	for (const FPlanetGeologyElement& Definition : Definitions)
	{
		FElementRuntime& Runtime = Field->Elements.AddDefaulted_GetRef();
		Runtime.Definition = Definition;
		Runtime.Noise.SetSeed(Definition.Seed);
		Runtime.Noise.SetFractalLacunarity(Definition.Lacunarity);
		// FractalBounding 只在这里算一次；octaves / gain 必须与采样时传进
		// GetPerlinFractal_3D 的值完全一致，否则归一化系数会错位。
		Runtime.Noise.SetFractalOctavesAndGain(Definition.Octaves, Definition.Gain);

		Field->TotalAbundance += Definition.Abundance;
		Field->MaxReachDepth = FMath::Max(Field->MaxReachDepth, Definition.ReachDepth);
	}

	return Field;
}

TSharedPtr<const FPlanetGeologyField> FPlanetGeologyField::LoadFromFiles(
	const FString& PresetName, FPlanetGeologyLoadReport& OutReport)
{
	if (!IsValidPresetName(PresetName))
	{
		OutReport.Errors.Add(FString::Printf(TEXT("地质预设名非法: '%s'"), *PresetName));
		return MakeShared<FPlanetGeologyField>();
	}

	FString ElementsJson;
	if (!LoadTextFile(TEXT("Geology/Elements.json"), ElementsJson))
	{
		OutReport.Errors.Add(TEXT("读取失败: Content/Geology/Elements.json"));
		return MakeShared<FPlanetGeologyField>();
	}

	const FString PlanetRelativePath = FString::Printf(
		TEXT("Geology/Planets/%s.json"), *PresetName);
	FString PlanetJson;
	if (!LoadTextFile(PlanetRelativePath, PlanetJson))
	{
		OutReport.Errors.Add(FString::Printf(
			TEXT("读取失败: Content/%s"), *PlanetRelativePath));
		return MakeShared<FPlanetGeologyField>();
	}

	return LoadFromString(ElementsJson, PlanetJson, OutReport);
}

TArray<FString> FPlanetGeologyField::ListPresetNames()
{
	TArray<FString> PresetNames;

	const FString Directory = FPaths::Combine(
		FPaths::ProjectContentDir(), TEXT("Geology/Planets"));
	TArray<FString> FileNames;
	IFileManager::Get().FindFiles(
		FileNames, *FPaths::Combine(Directory, TEXT("*.json")), true, false);

	for (const FString& FileName : FileNames)
	{
		PresetNames.Add(FPaths::GetBaseFilename(FileName));
	}
	PresetNames.Sort();
	return PresetNames;
}

// 采样数学在 Task 2 实现；先留桩让解析测试能单独跑。
FPlanetGeologySample FPlanetGeologyField::Sample(const FVector& UnitDir, double Depth) const
{
	return FPlanetGeologySample();
}

float FPlanetGeologyField::SampleGeometryDelta(const FVector& UnitDir, double Depth) const
{
	return 0.f;
}
