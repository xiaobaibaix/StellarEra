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

	/**
	 * 解析一条矿层。两种写法二选一：
	 * - 绝对深度：peakDepth / layerThickness（体素，旧 schema）；
	 * - 半径比例：peakDepthRatio / layerThicknessRatio（占行星半径，0..1）。
	 * 比例字段存在时以比例为准；带比例却没有行星半径时跳过该带并留警告。
	 */
	bool ParseBand(const TSharedPtr<FJsonObject>& BandObject, const FString& Context,
		double PlanetRadiusVoxels, FPlanetGeologyBand& OutBand,
		FPlanetGeologyLoadReport& Report)
	{
		// 只有手填的绝对深度才需要上限：MaxReachDepth 靠它保持有限。
		// 比例深度天然被行星半径约束，再套这个绝对上限会把深层带夹没。
		constexpr double MaxAbsPeakDepth = 10000.0;
		constexpr double MaxLayerThickness = 10000.0;

		double PeakDepth = 0.0;
		double Raw = 0.0;
		if (BandObject->TryGetNumberField(TEXT("peakDepthRatio"), Raw))
		{
			if (PlanetRadiusVoxels <= 0.0)
			{
				Report.Warnings.Add(FString::Printf(
					TEXT("%s: peakDepthRatio 需要行星半径，跳过这条矿层"), *Context));
				return false;
			}
			if (!FMath::IsFinite(Raw))
			{
				Report.Warnings.Add(FString::Printf(
					TEXT("%s: peakDepthRatio 不是有限数，按 0 处理"), *Context));
				Raw = 0.0;
			}
			const double Ratio = FMath::Clamp(Raw, -1.0, 1.0);
			if (!FMath::IsNearlyEqual(Ratio, Raw, 1e-9))
			{
				Report.Warnings.Add(FString::Printf(
					TEXT("%s: peakDepthRatio %f 越界，夹到 [-1, 1]"), *Context, Raw));
			}
			PeakDepth = Ratio * PlanetRadiusVoxels;
		}
		else if (BandObject->TryGetNumberField(TEXT("peakDepth"), PeakDepth))
		{
			// 负峰值在 schema 里合法（矿层位于基准球面之上），只限制量级，
			// 避免 MaxReachDepth 被推到 inf、早退失效。
			if (!FMath::IsFinite(PeakDepth))
			{
				Report.Warnings.Add(FString::Printf(
					TEXT("%s: peakDepth 不是有限数，按 0 处理"), *Context));
				PeakDepth = 0.0;
			}
			else if (PeakDepth < -MaxAbsPeakDepth || PeakDepth > MaxAbsPeakDepth)
			{
				const double Clamped = FMath::Clamp(
					PeakDepth, -MaxAbsPeakDepth, MaxAbsPeakDepth);
				Report.Warnings.Add(FString::Printf(
					TEXT("%s: peakDepth %f 越界，夹到 [-%g, %g]"),
					*Context, PeakDepth, MaxAbsPeakDepth, MaxAbsPeakDepth));
				PeakDepth = Clamped;
			}
		}

		double LayerThickness = 0.0;
		bool bHasThickness = false;
		bool bThicknessFromRatio = false;
		if (BandObject->TryGetNumberField(TEXT("layerThicknessRatio"), Raw))
		{
			if (PlanetRadiusVoxels <= 0.0)
			{
				Report.Warnings.Add(FString::Printf(
					TEXT("%s: layerThicknessRatio 需要行星半径，跳过这条矿层"), *Context));
				return false;
			}
			if (!FMath::IsFinite(Raw) || Raw <= 0.0)
			{
				Report.Warnings.Add(FString::Printf(
					TEXT("%s: layerThicknessRatio 必须是有限正数，跳过这条矿层"), *Context));
				return false;
			}
			const double Ratio = FMath::Clamp(Raw, 1e-4, 1.0);
			if (!FMath::IsNearlyEqual(Ratio, Raw, 1e-9))
			{
				Report.Warnings.Add(FString::Printf(
					TEXT("%s: layerThicknessRatio %f 越界，夹到 [1e-4, 1]"), *Context, Raw));
			}
			LayerThickness = Ratio * PlanetRadiusVoxels;
			bHasThickness = true;
			bThicknessFromRatio = true;
		}
		else if (BandObject->TryGetNumberField(TEXT("layerThickness"), LayerThickness))
		{
			bHasThickness = true;
		}

		if (!bHasThickness)
		{
			Report.Warnings.Add(FString::Printf(
				TEXT("%s: 缺少 layerThickness / layerThicknessRatio，跳过这条矿层"), *Context));
			return false;
		}
		if (!FMath::IsFinite(LayerThickness) || LayerThickness <= 0.0)
		{
			Report.Warnings.Add(FString::Printf(
				TEXT("%s: layerThickness 必须是有限正数，跳过这条矿层"), *Context));
			return false;
		}
		if (!bThicknessFromRatio && LayerThickness > MaxLayerThickness)
		{
			Report.Warnings.Add(FString::Printf(
				TEXT("%s: layerThickness %f > %g，夹到上界"),
				*Context, LayerThickness, MaxLayerThickness));
			LayerThickness = MaxLayerThickness;
		}

		OutBand.PeakDepth = float(PeakDepth);
		OutBand.LayerThickness = float(LayerThickness);
		OutBand.ReachDepth = float(FMath::Abs(PeakDepth) + ThreeSigma * LayerThickness);
		return true;
	}

	bool ParseElementTable(const TSharedPtr<FJsonObject>& Root,
		TMap<FString, FPlanetGeologyElement>& OutTable,
		TArray<FString>& OutOrder, FPlanetGeologyLoadReport& Report)
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
				Element.VeinThreshold = ReadClampedFloat(Noise, TEXT("veinThreshold"),
					Element.VeinThreshold, 0.f, 0.95f, Context, Report);
				Element.Octaves = ReadClampedInt(Noise, TEXT("octaves"),
					Element.Octaves, 1, 6, Context, Report);
				Element.Seed = ReadClampedInt(Noise, TEXT("seed"),
					Element.Seed, MIN_int32, MAX_int32, Context, Report);
			}

			OutTable.Add(Id, Element);
			OutOrder.Add(Id);
		}
		return true;
	}

	bool ParsePlanet(const TSharedPtr<FJsonObject>& Root,
		const TMap<FString, FPlanetGeologyElement>& Table,
		FPlanetGeologyAppearance& OutAppearance, float& OutGeometryAmplitude,
		double PlanetRadiusVoxels, TArray<FPlanetGeologyElement>& OutElements,
		FPlanetGeologyLoadReport& Report)
	{
		ReadVersion(Root, TEXT("Planets/<preset>.json"), Report);

		double GeometryAmplitude = 1.5;
		if (Root->TryGetNumberField(TEXT("geometryAmplitude"), GeometryAmplitude))
		{
			// 内容 JSON 是人工编辑入口：非有限值或极端值会把地表推出世界、
			// 让整颗行星变成实心。坏数据必须退化成「没有矿」，而不是毁掉行星。
			if (!FMath::IsFinite(GeometryAmplitude))
			{
				Report.Warnings.Add(TEXT("geometryAmplitude 不是有限数，按 0 处理"));
				GeometryAmplitude = 0.0;
			}
			else if (GeometryAmplitude < 0.0 || GeometryAmplitude > 64.0)
			{
				const double Clamped = FMath::Clamp(GeometryAmplitude, 0.0, 64.0);
				Report.Warnings.Add(FString::Printf(
					TEXT("geometryAmplitude %f 越界，夹到 [0, 64]"), GeometryAmplitude));
				GeometryAmplitude = Clamped;
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

			FPlanetGeologyElement Element = *Definition;
			Element.Abundance = Abundance;
			Element.Bands.Reset();
			Element.ReachDepth = 0.f;

			const FString ElementContext = FString::Printf(
				TEXT("行星 JSON: '%s'"), *ElementId);

			const TArray<TSharedPtr<FJsonValue>>* Bands = nullptr;
			if (ElementObject->TryGetArrayField(TEXT("bands"), Bands) && Bands)
			{
				for (int32 BandIndex = 0; BandIndex < Bands->Num(); ++BandIndex)
				{
					const TSharedPtr<FJsonObject>* BandPtr = nullptr;
					if (!TryReadObject((*Bands)[BandIndex], BandPtr))
					{
						Report.Warnings.Add(FString::Printf(
							TEXT("行星 JSON: '%s' 的 bands[%d] 不是对象，跳过"),
							*ElementId, BandIndex));
						continue;
					}

					FPlanetGeologyBand Band;
					const FString BandContext = FString::Printf(
						TEXT("行星 JSON: '%s' 的 bands[%d]"), *ElementId, BandIndex);
					if (!ParseBand(*BandPtr, BandContext, PlanetRadiusVoxels, Band, Report))
					{
						continue;
					}
					Element.ReachDepth = FMath::Max(Element.ReachDepth, Band.ReachDepth);
					Element.Bands.Add(Band);
				}
			}
			else
			{
				// 旧 schema：顶层 peakDepth / layerThickness 就是唯一一条带。
				FPlanetGeologyBand Band;
				if (ParseBand(ElementObject, ElementContext, PlanetRadiusVoxels, Band, Report))
				{
					Element.ReachDepth = Band.ReachDepth;
					Element.Bands.Add(Band);
				}
			}

			if (Element.Bands.Num() == 0)
			{
				Report.Warnings.Add(FString::Printf(
					TEXT("%s 没有任何有效矿层，跳过"), *ElementContext));
				continue;
			}

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
	const FString& ElementsJson, const FString& PlanetJson, FPlanetGeologyLoadReport& OutReport,
	double PlanetRadiusVoxels)
{
	TSharedPtr<FPlanetGeologyField> Field = MakeShared<FPlanetGeologyField>();

	TSharedPtr<FJsonObject> ElementsRoot;
	if (!ParseJsonObject(ElementsJson, TEXT("Elements.json"), ElementsRoot, OutReport))
	{
		return Field;
	}

	TMap<FString, FPlanetGeologyElement> Table;
	TArray<FString> ElementOrder;
	if (!ParseElementTable(ElementsRoot, Table, ElementOrder, OutReport))
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
	if (!ParsePlanet(PlanetRoot, Table, Appearance, GeometryAmplitude,
		PlanetRadiusVoxels, Definitions, OutReport))
	{
		return Field;
	}

	Field->Appearance = Appearance;
	Field->GeometryAmplitude = FMath::Max(0.f, GeometryAmplitude);
	Field->ElementOrder = ElementOrder;

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
	const FString& PresetName, FPlanetGeologyLoadReport& OutReport,
	double PlanetRadiusVoxels)
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

	return LoadFromString(ElementsJson, PlanetJson, OutReport, PlanetRadiusVoxels);
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

FPlanetGeologySample FPlanetGeologyField::Sample(const FVector& UnitDir, double Depth) const
{
	FPlanetGeologySample Result;
	if (Elements.Num() == 0 || TotalAbundance <= 0.f)
	{
		return Result;
	}

	FAccumulation Accumulation;
	FLinearColor WeightedColor = FLinearColor::Black;
	Accumulate(UnitDir, Depth, true, Accumulation, WeightedColor);

	if (Accumulation.Total <= UE_DOUBLE_KINDA_SMALL_NUMBER)
	{
		return Result;
	}

	Result.TotalNorm = FMath::Clamp(
		float(Accumulation.Total / double(TotalAbundance)), 0.f, 1.f);
	Result.DominantNorm = FMath::Clamp(float(Accumulation.DominantNorm), 0.f, 1.f);
	Result.DominantIndex = Accumulation.DominantIndex;
	Result.Purity = FMath::Clamp(
		float(Accumulation.MaxRaw / Accumulation.Total), 0.f, 1.f);
	Result.MineralColor = WeightedColor / float(Accumulation.Total);
	// 累加器初值 A = 1，除法会把 alpha 也除掉；MineralColor 是纯颜色，A 恒为 1。
	Result.MineralColor.A = 1.f;
	return Result;
}

float FPlanetGeologyField::SampleGeometryDelta(const FVector& UnitDir, double Depth) const
{
	if (Elements.Num() == 0 || TotalAbundance <= 0.f)
	{
		return 0.f;
	}
	// 高斯尾部之外没有贡献；绝大多数行星内部体积走这一条。
	if (Depth < -double(MaxReachDepth) || Depth > double(MaxReachDepth))
	{
		return 0.f;
	}

	FAccumulation Accumulation;
	FLinearColor UnusedColor = FLinearColor::Black;
	Accumulate(UnitDir, Depth, false, Accumulation, UnusedColor);

	if (Accumulation.Total <= 0.0)
	{
		return 0.f;
	}

	const float TotalNorm = FMath::Clamp(
		float(Accumulation.Total / double(TotalAbundance)), 0.f, 1.f);
	return GeometryAmplitude * TotalNorm;
}

void FPlanetGeologyField::Accumulate(const FVector& UnitDir, double Depth, bool bComputeColor,
	FAccumulation& OutAccumulation, FLinearColor& OutWeightedColor) const
{
	OutAccumulation = FAccumulation();
	OutWeightedColor = FLinearColor::Black;

	double WeightedR = 0.0;
	double WeightedG = 0.0;
	double WeightedB = 0.0;

	for (int32 ElementIndex = 0; ElementIndex < Elements.Num(); ++ElementIndex)
	{
		const FElementRuntime& Runtime = Elements[ElementIndex];
		const FPlanetGeologyElement& Definition = Runtime.Definition;

		// 同一元素的各条带在深度上基本互不重叠：Raw 相加，Norm 取最大。
		// 单带元素的行为与旧实现逐位一致。
		double ElementRaw = 0.0;
		double ElementNorm = 0.0;

		for (const FPlanetGeologyBand& Band : Definition.Bands)
		{
			const double LayerThickness = FMath::Max(double(Band.LayerThickness), 1e-3);
			const double DepthRatio = (Depth - double(Band.PeakDepth)) / LayerThickness;
			const double Gaussian = FMath::Exp(-DepthRatio * DepthRatio);

			// 单带在任一深度上常常没有贡献；先算 exp 再决定是否花一次分形 Perlin。
			if (Gaussian < 0.01)
			{
				continue;
			}

			// SampleZ 用无量纲的深度比：GetPerlinFractal_3D 内部会把三个坐标都乘频率，
			// 直接加体素深度会让噪声随深度剧烈振荡。
			const double SampleZ =
				double(UnitDir.Z) + (Depth / LayerThickness) * double(Definition.DepthScale);

			const double RawNoise = double(Runtime.Noise.GetPerlinFractal_3D(
				v_flt(UnitDir.X), v_flt(UnitDir.Y), v_flt(SampleZ),
				v_flt(Definition.Frequency), Definition.Octaves));

			// Perlin 输出 [-1,1]，映射到 [0,1] 才是「含量」。
			const double Noise01 = 0.5 * (RawNoise + 1.0);

			// 矿脉门：阈值以下完全为 0，阈值以上线性升到 1。
			// 每个元素的门限不同，独立噪声场下同一地点通常只有一种矿在门内，
			// 于是 Purity 能被推到接近 1，而不是所有元素各占一份的灰色平均值。
			const double VeinThreshold = FMath::Clamp(double(Definition.VeinThreshold), 0.0, 0.95);
			const double Vein = VeinThreshold > 0.0
				? FMath::Clamp((Noise01 - VeinThreshold) / (1.0 - VeinThreshold), 0.0, 1.0)
				: Noise01;

			ElementRaw += double(Definition.Abundance) * Gaussian * Vein;
			// 自身强度：去掉丰度，只保留「这一层有多接近峰值 × 脉有多强」。
			// 低丰度元素（铀 0.10）也能在自己的脉里达到 1，不会被全局总量压死。
			ElementNorm = FMath::Max(ElementNorm, Gaussian * Vein);
		}

		if (ElementNorm <= 0.0)
		{
			continue;
		}

		OutAccumulation.Total += ElementRaw;
		OutAccumulation.MaxRaw = FMath::Max(OutAccumulation.MaxRaw, ElementRaw);
		if (ElementNorm > OutAccumulation.DominantNorm)
		{
			OutAccumulation.DominantNorm = ElementNorm;
			OutAccumulation.DominantIndex = ElementIndex;
		}

		if (bComputeColor && ElementRaw > 0.0)
		{
			WeightedR += ElementRaw * double(Definition.Color.R);
			WeightedG += ElementRaw * double(Definition.Color.G);
			WeightedB += ElementRaw * double(Definition.Color.B);
		}
	}

	OutWeightedColor = FLinearColor(
		float(WeightedR), float(WeightedG), float(WeightedB), 1.f);
}

TArray<FPlanetGeologyElement> FPlanetGeologyField::GetElementDefinitions() const
{
	TArray<FPlanetGeologyElement> Out;
	Out.Reserve(Elements.Num());

	// 先按 Elements.json 的数组顺序输出：图例顺序要与元素表一致（spec §3.3）。
	// Elements 本身（运行期采样顺序）刻意不动 —— DominantIndex 与地形都依赖它。
	for (const FString& Id : ElementOrder)
	{
		for (const FElementRuntime& Runtime : Elements)
		{
			if (Runtime.Definition.Id == Id)
			{
				Out.Add(Runtime.Definition);
				break;
			}
		}
	}

	// 防御：任何不在 ElementOrder 里的定义追加在后面，保证不丢数据。
	for (const FElementRuntime& Runtime : Elements)
	{
		if (!ElementOrder.Contains(Runtime.Definition.Id))
		{
			Out.Add(Runtime.Definition);
		}
	}

	return Out;
}

TArray<FPlanetGeologyElement> FPlanetGeologyField::GetSampledElementDefinitions() const
{
	TArray<FPlanetGeologyElement> Out;
	Out.Reserve(Elements.Num());
	for (const FElementRuntime& Runtime : Elements)
	{
		Out.Add(Runtime.Definition);
	}
	return Out;
}
