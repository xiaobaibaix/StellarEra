// 匹配头必须是第一个 include（UBT 的 IWYU 检查）；它带了 CoreMinimal.h。
#include "GeoMaterialBuilder.h"

// 路径常量与 getter 在 WITH_EDITOR 之外：Game 目标也要按路径加载资产，只有构建本身是编辑器专属。
namespace
{
	const TCHAR* const GeoDecodeFunctionPath = TEXT("/Game/Code/Planet/Materials/Functions/MF_GeoDecode");
	const TCHAR* const GeoPlanetMaterialPath = TEXT("/Game/Code/Planet/Materials/M_GeoPlanet");
	const TCHAR* const GeoDecodeFunctionName = TEXT("MF_GeoDecode");
	const TCHAR* const GeoPlanetMaterialName = TEXT("M_GeoPlanet");
} // namespace

const TCHAR* GeoMaterialBuild::GetDecodeFunctionPath()
{
	return GeoDecodeFunctionPath;
}

const TCHAR* GeoMaterialBuild::GetPlanetMaterialPath()
{
	return GeoPlanetMaterialPath;
}

#if WITH_EDITOR

#include "Engine/EngineTypes.h"
#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant2Vector.h"
#include "Materials/MaterialExpressionDivide.h"
#include "Materials/MaterialExpressionFloor.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionNormalize.h"
#include "Materials/MaterialExpressionObjectPositionWS.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionSmoothStep.h"
#include "Materials/MaterialExpressionStep.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureSampleParameterCube.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialFunction.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "UObject/SavePackage.h"

namespace
{
	// 名字带 Geo 前缀是必须的：UBT 的 unity 构建会把本文件与其它材质构建器拼进同一翻译单元，
	// 两个匿名 namespace 合并后同名的 AddExpression / AddConstant 直接 C2084 重定义。

	/** 由名字派生固定 GUID：随机 Guid 会让「重建一次」把资产字节全部改一遍，diff 里全是噪声。 */
	FGuid GeoStableGuid(const TCHAR* Kind, const TCHAR* Name)
	{
		const FString Key = FString::Printf(TEXT("%s:%s"), Kind, Name);
		return FGuid(GetTypeHash(Key), GetTypeHash(Key + TEXT("#1")),
			GetTypeHash(Key + TEXT("#2")), GetTypeHash(Key + TEXT("#3")));
	}

	struct FGeoGraph
	{
		UObject* Owner = nullptr;
		UMaterial* Material = nullptr;
		UMaterialFunction* Function = nullptr;

		FMaterialExpressionCollection& Collection()
		{
			return Material ? Material->GetExpressionCollection() : Function->GetExpressionCollection();
		}

		template <typename TExpression>
		TExpression* Add(int32 PosX, int32 PosY)
		{
			TExpression* Expr = NewObject<TExpression>(Owner);
			// 材质图里的表达式必须回指所属材质；函数图里保持为空，否则编译会当成「正在编辑的函数」。
			Expr->Material = Material;
			Expr->MaterialExpressionEditorX = PosX;
			Expr->MaterialExpressionEditorY = PosY;
			// 节点 Guid 也要固定：随机值会写进 uasset，重建一次 diff 全是噪声。
			const FString NodeKey = FString::Printf(TEXT("%s@%d,%d"), *Owner->GetPathName(), PosX, PosY);
			Expr->MaterialExpressionGuid = GeoStableGuid(TEXT("Node"), *NodeKey);
			Collection().AddExpression(Expr);
			return Expr;
		}
	};

	void GeoWire(FExpressionInput& Input, UMaterialExpression* Source, int32 OutputIndex = 0)
	{
		Input.Expression = Source;
		Input.OutputIndex = OutputIndex;
	}

	template <typename TExpression>
	void GeoSetStableId(TExpression* Expr, const TCHAR* Kind, const TCHAR* Name)
	{
		Expr->Id = GeoStableGuid(Kind, Name);
		// bForce=false：上面已给有效 Id，这里只是兜底，不会再生成随机值。
		Expr->ConditionallyGenerateId(false);
	}

	UMaterialExpressionConstant* GeoConstant(FGeoGraph& Graph, float Value, int32 PosX, int32 PosY)
	{
		UMaterialExpressionConstant* Expr = Graph.Add<UMaterialExpressionConstant>(PosX, PosY);
		Expr->R = Value;
		return Expr;
	}

	UMaterialExpressionConstant2Vector* GeoConstant2(FGeoGraph& Graph, float X, float Y, int32 PosX, int32 PosY)
	{
		UMaterialExpressionConstant2Vector* Expr = Graph.Add<UMaterialExpressionConstant2Vector>(PosX, PosY);
		Expr->R = X;
		Expr->G = Y;
		return Expr;
	}

	template <typename TExpression>
	TExpression* GeoBinary(FGeoGraph& Graph, UMaterialExpression* A, UMaterialExpression* B,
		int32 PosX, int32 PosY)
	{
		TExpression* Expr = Graph.Add<TExpression>(PosX, PosY);
		GeoWire(Expr->A, A);
		GeoWire(Expr->B, B);
		return Expr;
	}

	template <typename TExpression>
	TExpression* GeoUnary(FGeoGraph& Graph, UMaterialExpression* Source, int32 PosX, int32 PosY)
	{
		TExpression* Expr = Graph.Add<TExpression>(PosX, PosY);
		GeoWire(Expr->Input, Source);
		return Expr;
	}

	UMaterialExpressionComponentMask* GeoMask(FGeoGraph& Graph, UMaterialExpression* Source,
		bool bR, bool bG, bool bB, bool bA, int32 PosX, int32 PosY)
	{
		UMaterialExpressionComponentMask* Expr = Graph.Add<UMaterialExpressionComponentMask>(PosX, PosY);
		GeoWire(Expr->Input, Source);
		Expr->R = bR;
		Expr->G = bG;
		Expr->B = bB;
		Expr->A = bA;
		return Expr;
	}

	UMaterialExpressionLinearInterpolate* GeoLerp(FGeoGraph& Graph, UMaterialExpression* A,
		UMaterialExpression* B, UMaterialExpression* Alpha, int32 PosX, int32 PosY)
	{
		UMaterialExpressionLinearInterpolate* Expr = Graph.Add<UMaterialExpressionLinearInterpolate>(PosX, PosY);
		GeoWire(Expr->A, A);
		GeoWire(Expr->B, B);
		GeoWire(Expr->Alpha, Alpha);
		return Expr;
	}

	/** 一次调色板采样：UV = (id/255, 行中心)，行号由 RowCenter 给出。 */
	UMaterialExpressionTextureSample* GeoSamplePalette(FGeoGraph& Graph, UMaterialExpression* Palette,
		UMaterialExpression* Id, float RowCenter, int32 PosX, int32 PosY)
	{
		UMaterialExpressionConstant* Row = GeoConstant(Graph, RowCenter, PosX - 500, PosY + 80);
		UMaterialExpressionAppendVector* UvIn = Graph.Add<UMaterialExpressionAppendVector>(PosX - 350, PosY + 20);
		GeoWire(UvIn->A, Id);
		GeoWire(UvIn->B, Row);

		// 除数 (255,1)：U 是编号归一化，V 已是行中心，不能再除。
		UMaterialExpressionConstant2Vector* Divisor = GeoConstant2(Graph, 255.f, 1.f, PosX - 350, PosY - 60);
		UMaterialExpressionDivide* Uv = GeoBinary<UMaterialExpressionDivide>(Graph, UvIn, Divisor, PosX - 200, PosY);

		UMaterialExpressionTextureSample* Sample = Graph.Add<UMaterialExpressionTextureSample>(PosX, PosY);
		GeoWire(Sample->TextureObject, Palette);
		GeoWire(Sample->Coordinates, Uv);
		Sample->SamplerType = SAMPLERTYPE_Color;
		// 强制 mip 0：256×3 的 LUT 自动 mip 会降到只剩一行的层级，三行颜色全糊在一起。
		Sample->MipValueMode = TMVM_MipLevel;
		Sample->ConstMipValue = 0;
		return Sample;
	}

	template <typename TAsset>
	TAsset* GeoFindOrCreateAsset(const TCHAR* PackagePath, const TCHAR* AssetName, FString& OutError)
	{
		UPackage* Package = CreatePackage(PackagePath);
		if (!Package)
		{
			OutError = FString::Printf(TEXT("CreatePackage 失败：%s"), PackagePath);
			return nullptr;
		}

		TAsset* Asset = FindObject<TAsset>(Package, AssetName);
		if (!Asset && FPackageName::DoesPackageExist(PackagePath))
		{
			// 先读盘：拿到旧图才能判断这次重建是否真的改了；否则每次都新建对象再落盘，
			// 同样的图也会因为对象名后缀/导入表顺序写出不同字节。
			Asset = LoadObject<TAsset>(nullptr,
				*FString::Printf(TEXT("%s.%s"), PackagePath, AssetName), nullptr, LOAD_NoWarn | LOAD_Quiet);
		}
		if (!Asset)
		{
			Asset = NewObject<TAsset>(Package, AssetName, RF_Public | RF_Standalone);
		}
		if (!Asset)
		{
			OutError = FString::Printf(TEXT("NewObject 失败：%s"), AssetName);
			return nullptr;
		}
		// 磁盘上已有同名文件时，新建的内存包 IsFullyLoaded() 为 false，SavePackage 会以「只加载了一部分」拒绝保存。
		// 本包内容全由本函数生成，没有待完成的加载。
		Package->MarkAsFullyLoaded();
		// 包 Guid 随包汇总信息一起落盘；不固定的话每次重建资产字节都不同。
		Package->SetPersistentGuid(GeoStableGuid(TEXT("Package"), PackagePath));
		return Asset;
	}

	bool GeoSaveAsset(UObject* Asset, const TCHAR* PackagePath, FString& OutError)
	{
		Asset->GetOutermost()->MarkPackageDirty();

		const FString PackageFileName = FPackageName::LongPackageNameToFilename(
			PackagePath, FPackageName::GetAssetPackageExtension());

		// 失败只回 false + 错误串：调用方（命令/测试）负责打印，这里再弹窗会挂住无头进程。
		// SAVE_NoError 的代价是错误细节不进日志，所以失败串里至少带上文件名。
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		if (!UPackage::SavePackage(Asset->GetOutermost(), Asset, *PackageFileName, SaveArgs))
		{
			OutError = FString::Printf(TEXT("SavePackage 失败：%s"), *PackageFileName);
			return false;
		}
		return true;
	}

	bool BuildDecodeFunction(UMaterialFunction* Function, FString& OutError)
	{
		FGeoGraph Graph;
		Graph.Owner = Function;
		Graph.Function = Function;

		UMaterialExpressionFunctionInput* VertexColor = Graph.Add<UMaterialExpressionFunctionInput>(-1600, -600);
		VertexColor->InputName = TEXT("VertexColor");
		VertexColor->InputType = FunctionInput_Vector4;
		VertexColor->PreviewValue = FVector4f(0.f, 0.f, 0.f, 1.f);
		VertexColor->SortPriority = 0;
		GeoSetStableId(VertexColor, TEXT("Input"), TEXT("VertexColor"));

		UMaterialExpressionFunctionInput* Uv1 = Graph.Add<UMaterialExpressionFunctionInput>(-1600, -400);
		Uv1->InputName = TEXT("UV1");
		Uv1->InputType = FunctionInput_Vector2;
		Uv1->PreviewValue = FVector4f(1.f, 1.f, 0.f, 0.f);
		Uv1->SortPriority = 1;
		GeoSetStableId(Uv1, TEXT("Input"), TEXT("UV1"));

		UMaterialExpressionFunctionInput* Palette = Graph.Add<UMaterialExpressionFunctionInput>(-1600, -200);
		Palette->InputName = TEXT("GeoPalette");
		Palette->InputType = FunctionInput_Texture2D;
		Palette->SortPriority = 2;
		GeoSetStableId(Palette, TEXT("Input"), TEXT("GeoPalette"));

		// 顶点属性在三角形内插值，编号会出现中间值；floor(x·255+0.5) 取最近再硬切，
		// 侵入体与围岩才保持硬接触。Grade 与 G 用原始通道（它们本来就是 0–1）。
		UMaterialExpressionConstant* Scale255 = GeoConstant(Graph, 255.f, -1400, -700);
		UMaterialExpressionMultiply* Scaled = GeoBinary<UMaterialExpressionMultiply>(
			Graph, VertexColor, Scale255, -1250, -650);
		UMaterialExpressionConstant* Half = GeoConstant(Graph, 0.5f, -1400, -550);
		UMaterialExpressionAdd* Biased = GeoBinary<UMaterialExpressionAdd>(Graph, Scaled, Half, -1100, -620);
		UMaterialExpressionFloor* Floored = GeoUnary<UMaterialExpressionFloor>(Graph, Biased, -950, -620);

		UMaterialExpressionComponentMask* Lith = GeoMask(Graph, Floored, true, false, false, false, -800, -800);
		UMaterialExpressionComponentMask* Mineral = GeoMask(Graph, Floored, false, true, false, false, -800, -700);
		UMaterialExpressionComponentMask* A255 = GeoMask(Graph, Floored, false, false, false, true, -800, -600);
		UMaterialExpressionComponentMask* RawG = GeoMask(Graph, VertexColor, false, true, false, false, -800, -500);
		UMaterialExpressionComponentMask* Grade = GeoMask(Graph, VertexColor, false, false, true, false, -800, -400);

		// Clue = floor(A255/16)；Strength = (A255 − Clue·16)/15。
		UMaterialExpressionConstant* Sixteen = GeoConstant(Graph, 16.f, -700, -950);
		UMaterialExpressionDivide* ClueDiv = GeoBinary<UMaterialExpressionDivide>(Graph, A255, Sixteen, -650, -900);
		UMaterialExpressionFloor* Clue = GeoUnary<UMaterialExpressionFloor>(Graph, ClueDiv, -500, -900);
		UMaterialExpressionMultiply* ClueTimes16 = GeoBinary<UMaterialExpressionMultiply>(Graph, Clue, Sixteen, -500, -1000);
		UMaterialExpressionSubtract* Remainder = GeoBinary<UMaterialExpressionSubtract>(
			Graph, A255, ClueTimes16, -350, -950);
		UMaterialExpressionConstant* Fifteen = GeoConstant(Graph, 15.f, -500, -1100);
		UMaterialExpressionDivide* Strength = GeoBinary<UMaterialExpressionDivide>(
			Graph, Remainder, Fifteen, -200, -1000);

		UMaterialExpressionTextureSample* LithColor = GeoSamplePalette(
			Graph, Palette, Lith, 1.f / 6.f, -700, -300);
		UMaterialExpressionTextureSample* MinColor = GeoSamplePalette(
			Graph, Palette, Mineral, 3.f / 6.f, -700, -100);
		UMaterialExpressionTextureSample* ClueColor = GeoSamplePalette(
			Graph, Palette, Clue, 5.f / 6.f, -700, 100);

		UMaterialExpressionSmoothStep* Smooth = Graph.Add<UMaterialExpressionSmoothStep>(-700, 500);
		Smooth->ConstMin = 0.15f;
		Smooth->ConstMax = 0.75f;
		GeoWire(Smooth->Value, Grade);

		// 阈值 0.002 ≈ 8 位编号半步：G=0 表示无矿物，必须把遮罩压到 0。
		UMaterialExpressionStep* HasMineral = Graph.Add<UMaterialExpressionStep>(-700, 650);
		HasMineral->ConstY = 0.002f;
		GeoWire(HasMineral->X, RawG);

		UMaterialExpressionMultiply* MineralCover = GeoBinary<UMaterialExpressionMultiply>(
			Graph, Smooth, HasMineral, -500, 550);

		UMaterialExpressionConstant* MinWeight = GeoConstant(Graph, 0.85f, -500, 200);
		UMaterialExpressionMultiply* MinScale = GeoBinary<UMaterialExpressionMultiply>(
			Graph, MineralCover, MinWeight, -350, 400);
		UMaterialExpressionLinearInterpolate* Rock = GeoLerp(
			Graph, LithColor, MinColor, MinScale, -200, -100);

		UMaterialExpressionConstant* ClueWeight = GeoConstant(Graph, 0.35f, -350, -600);
		UMaterialExpressionMultiply* ClueScale = GeoBinary<UMaterialExpressionMultiply>(
			Graph, Strength, ClueWeight, -200, -800);
		UMaterialExpressionLinearInterpolate* RockFinal = GeoLerp(Graph, Rock, ClueColor, ClueScale, 0, -300);

		UMaterialExpressionComponentMask* RockRgb = GeoMask(Graph, RockFinal, true, true, true, false, 200, -400);
		UMaterialExpressionComponentMask* LithRgb = GeoMask(Graph, LithColor, true, true, true, false, 200, -200);
		UMaterialExpressionComponentMask* SurfaceMask = GeoMask(Graph, Uv1, true, false, false, false, 200, 0);

		UMaterialExpressionFunctionOutput* OutRock = Graph.Add<UMaterialExpressionFunctionOutput>(400, -500);
		OutRock->OutputName = TEXT("RockColor");
		OutRock->SortPriority = 0;
		GeoWire(OutRock->A, RockRgb);
		GeoSetStableId(OutRock, TEXT("Output"), TEXT("RockColor"));

		UMaterialExpressionFunctionOutput* OutCover = Graph.Add<UMaterialExpressionFunctionOutput>(400, -400);
		OutCover->OutputName = TEXT("MineralCover");
		OutCover->SortPriority = 1;
		GeoWire(OutCover->A, MineralCover);
		GeoSetStableId(OutCover, TEXT("Output"), TEXT("MineralCover"));

		UMaterialExpressionFunctionOutput* OutMask = Graph.Add<UMaterialExpressionFunctionOutput>(400, -300);
		OutMask->OutputName = TEXT("SurfaceMask");
		OutMask->SortPriority = 2;
		GeoWire(OutMask->A, SurfaceMask);
		GeoSetStableId(OutMask, TEXT("Output"), TEXT("SurfaceMask"));

		UMaterialExpressionFunctionOutput* OutDebug = Graph.Add<UMaterialExpressionFunctionOutput>(400, -200);
		OutDebug->OutputName = TEXT("Debug");
		OutDebug->SortPriority = 3;
		GeoWire(OutDebug->A, LithRgb);
		GeoSetStableId(OutDebug, TEXT("Output"), TEXT("Debug"));

		Function->Description = TEXT("解顶点色里的地质编码：岩性/矿物/线索三行调色板合成岩石色、矿物覆盖度与地表遮罩。");
		return true;
	}

	/** 按名字取函数调用的输出索引；材质插槽只认索引，不认名字。 */
	int32 FindGeoFunctionOutput(UMaterialExpressionMaterialFunctionCall* Call, const TCHAR* OutputName,
		FString& OutError)
	{
		for (int32 Index = 0; Index < Call->FunctionOutputs.Num(); ++Index)
		{
			const UMaterialExpressionFunctionOutput* Output = Call->FunctionOutputs[Index].ExpressionOutput;
			if (Output && Output->OutputName == FName(OutputName))
			{
				return Index;
			}
		}
		OutError = FString::Printf(TEXT("MF_GeoDecode 缺少输出 %s"), OutputName);
		return INDEX_NONE;
	}

	bool WireGeoFunctionInputs(UMaterialExpressionMaterialFunctionCall* Call, UMaterialExpression* VertexColor,
		UMaterialExpression* Uv1, UMaterialExpression* Palette, FString& OutError)
	{
		for (FFunctionExpressionInput& FunctionInput : Call->FunctionInputs)
		{
			const FName InputName = FunctionInput.ExpressionInput
				? FunctionInput.ExpressionInput->InputName : NAME_None;

			UMaterialExpression* Source = nullptr;
			if (InputName == FName(TEXT("VertexColor"))) { Source = VertexColor; }
			else if (InputName == FName(TEXT("UV1"))) { Source = Uv1; }
			else if (InputName == FName(TEXT("GeoPalette"))) { Source = Palette; }

			if (!Source)
			{
				OutError = FString::Printf(TEXT("M_GeoPlanet: 函数输入 %s 没有对应源节点"), *InputName.ToString());
				return false;
			}
			GeoWire(FunctionInput.Input, Source);
		}

		if (Call->FunctionInputs.Num() != 3)
		{
			OutError = FString::Printf(TEXT("M_GeoPlanet: MF_GeoDecode 输入数 %d（期望 3）"), Call->FunctionInputs.Num());
			return false;
		}
		return true;
	}

	bool BuildPlanetMaterial(UMaterial* Material, UMaterialFunction* DecodeFunction, FString& OutError)
	{
		FGeoGraph Graph;
		Graph.Owner = Material;
		Graph.Material = Material;

		UMaterialExpressionTextureObjectParameter* Palette =
			Graph.Add<UMaterialExpressionTextureObjectParameter>(-1600, -300);
		Palette->ParameterName = TEXT("GeoPalette");
		Palette->SamplerType = SAMPLERTYPE_Color;
		// 参数 GUID 默认随机生成并落盘，必须固定，否则每次重建资产都变。
		Palette->ExpressionGUID = GeoStableGuid(TEXT("Param"), TEXT("GeoPalette"));

		UMaterialExpressionTextureSampleParameterCube* BiomeCube =
			Graph.Add<UMaterialExpressionTextureSampleParameterCube>(-900, 400);
		BiomeCube->ParameterName = TEXT("BiomeCube");
		BiomeCube->SamplerType = SAMPLERTYPE_Color;
		BiomeCube->ExpressionGUID = GeoStableGuid(TEXT("Param"), TEXT("BiomeCube"));

		UMaterialExpressionScalarParameter* Blend =
			Graph.Add<UMaterialExpressionScalarParameter>(-900, 600);
		Blend->ParameterName = TEXT("BiomeCubeBlend");
		Blend->DefaultValue = 1.f;
		Blend->ExpressionGUID = GeoStableGuid(TEXT("Param"), TEXT("BiomeCubeBlend"));

		UMaterialExpressionVertexColor* VertexColor = Graph.Add<UMaterialExpressionVertexColor>(-1600, -700);

		UMaterialExpressionTextureCoordinate* Uv1 = Graph.Add<UMaterialExpressionTextureCoordinate>(-1600, -500);
		Uv1->CoordinateIndex = 1;

		UMaterialExpressionMaterialFunctionCall* Decode = Graph.Add<UMaterialExpressionMaterialFunctionCall>(-1200, -400);
		// SetMaterialFunction 同时按函数里的输入/输出节点重建 FunctionInputs / FunctionOutputs。
		if (!Decode->SetMaterialFunction(DecodeFunction))
		{
			OutError = TEXT("M_GeoPlanet: SetMaterialFunction(MF_GeoDecode) 失败");
			return false;
		}
		if (!WireGeoFunctionInputs(Decode, VertexColor, Uv1, Palette, OutError))
		{
			return false;
		}

		const int32 RockOutput = FindGeoFunctionOutput(Decode, TEXT("RockColor"), OutError);
		const int32 CoverOutput = FindGeoFunctionOutput(Decode, TEXT("MineralCover"), OutError);
		const int32 MaskOutput = FindGeoFunctionOutput(Decode, TEXT("SurfaceMask"), OutError);
		if (RockOutput == INDEX_NONE || CoverOutput == INDEX_NONE || MaskOutput == INDEX_NONE)
		{
			return false;
		}

		// 方向由渲染侧直接取（世界位置 − 物体原点），不走 Actor 位置：MID 里的世界坐标会慢一帧。
		UMaterialExpressionWorldPosition* WorldPosition = Graph.Add<UMaterialExpressionWorldPosition>(-1200, 200);
		UMaterialExpressionObjectPositionWS* ObjectPosition = Graph.Add<UMaterialExpressionObjectPositionWS>(-1200, 320);
		ObjectPosition->OriginType = EPositionOrigin::Absolute;
		UMaterialExpressionSubtract* Offset = GeoBinary<UMaterialExpressionSubtract>(
			Graph, WorldPosition, ObjectPosition, -1000, 250);

		UMaterialExpressionNormalize* Direction = Graph.Add<UMaterialExpressionNormalize>(-850, 250);
		GeoWire(Direction->VectorInput, Offset);
		GeoWire(BiomeCube->Coordinates, Direction);

		UMaterialExpressionComponentMask* BiomeRgb =
			GeoMask(Graph, BiomeCube, true, true, true, false, -700, 400);

		// 地表 U1=1 保持生物群系外观；挖开/剖面 U1→0 露出岩性。
		UMaterialExpressionMultiply* BlendWeight = Graph.Add<UMaterialExpressionMultiply>(-600, 100);
		GeoWire(BlendWeight->A, Decode, MaskOutput);
		GeoWire(BlendWeight->B, Blend);

		UMaterialExpressionLinearInterpolate* BaseColor = Graph.Add<UMaterialExpressionLinearInterpolate>(-350, -300);
		GeoWire(BaseColor->A, Decode, RockOutput);
		GeoWire(BaseColor->B, BiomeRgb);
		GeoWire(BaseColor->Alpha, BlendWeight);

		UMaterialExpressionConstant* RoughBase = GeoConstant(Graph, 0.85f, -600, -200);
		UMaterialExpressionConstant* RoughMineral = GeoConstant(Graph, 0.45f, -600, -100);
		UMaterialExpressionLinearInterpolate* Roughness = Graph.Add<UMaterialExpressionLinearInterpolate>(-350, -100);
		GeoWire(Roughness->A, RoughBase);
		GeoWire(Roughness->B, RoughMineral);
		GeoWire(Roughness->Alpha, Decode, CoverOutput);

		UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
		if (!EditorData)
		{
			OutError = TEXT("GetEditorOnlyData() 返回空（M_GeoPlanet）");
			return false;
		}
		GeoWire(EditorData->BaseColor, BaseColor);
		GeoWire(EditorData->Roughness, Roughness);

		Material->BlendMode = BLEND_Opaque;
		Material->SetShadingModel(MSM_DefaultLit);
		return true;
	}

	/**
	 * 语义签名：只取连线（按集合下标）与属性值，不取对象名/导入表顺序。
	 * 用来判断「重建结果是否与已加载的资产一致」——一致就不落盘，避免每次保存把引擎内部的
	 * 导入表排序/对象名后缀变化写进资产（同样的图两次保存字节不同）。
	 */
	FString GeoGraphSignature(const FMaterialExpressionCollection& Collection)
	{
		TMap<const UMaterialExpression*, int32> Indices;
		for (int32 Index = 0; Index < Collection.Expressions.Num(); ++Index)
		{
			Indices.Add(Collection.Expressions[Index], Index);
		}

		auto ConnectionIndex = [&Indices](const UMaterialExpression* Source) -> int32
		{
			const int32* Found = Source ? Indices.Find(Source) : nullptr;
			return Found ? *Found : -1;
		};

		FString Signature;
		for (const UMaterialExpression* Expression : Collection.Expressions)
		{
			Signature += Expression->GetClass()->GetName();
			Signature += FString::Printf(TEXT("@%d,%d|"), Expression->MaterialExpressionEditorX,
				Expression->MaterialExpressionEditorY);

			// 函数调用的 FunctionInputs/FunctionOutputs 里嵌着瞬态对象指针，ExportText 会写进对象名，
			// 这里只记函数路径与每条输入的连线。
			if (const UMaterialExpressionMaterialFunctionCall* Call =
				Cast<UMaterialExpressionMaterialFunctionCall>(Expression))
			{
				Signature += Call->MaterialFunction ? Call->MaterialFunction->GetPathName() : TEXT("-");
				for (const FFunctionExpressionInput& FunctionInput : Call->FunctionInputs)
				{
					Signature += FString::Printf(TEXT("(%s:%d,%d)"),
						*FunctionInput.Input.InputName.ToString(),
						ConnectionIndex(FunctionInput.Input.Expression), FunctionInput.Input.OutputIndex);
				}
				Signature += TEXT(";");
				continue;
			}

			for (TFieldIterator<FProperty> It(Expression->GetClass()); It; ++It)
			{
				FProperty* Property = *It;
				if (Property->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient))
				{
					continue;
				}
				if (CastField<FObjectPropertyBase>(Property))
				{
					continue;
				}
				if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
				{
					// FExpressionInput 在 C++ 侧是普通 struct（UHT 侧是 noexport USTRUCT），只能按反射名识别。
					if (StructProperty->Struct && StructProperty->Struct->GetFName() == FName(TEXT("ExpressionInput")))
					{
						const FExpressionInput* Input = StructProperty->ContainerPtrToValuePtr<FExpressionInput>(Expression);
						Signature += FString::Printf(TEXT("%s:%d,%d;"), *Property->GetName(),
							ConnectionIndex(Input->Expression), Input->OutputIndex);
						continue;
					}
				}
				FString Value;
				Property->ExportText_InContainer(0, Value, Expression, Expression, nullptr, PPF_None);
				Signature += FString::Printf(TEXT("%s=%s;"), *Property->GetName(), *Value);
			}
			Signature += TEXT("|");
		}
		return Signature;
	}

	FAutoConsoleCommandWithWorldAndArgs GBuildGeoMaterialsCommand(
		TEXT("StellarEra.BuildGeoMaterials"),
		TEXT("重建 /Game/Code/Planet/Materials 下的 MF_GeoDecode 与 M_GeoPlanet。改完通道布局后跑一次。"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				FString Error;
				const bool bOk = GeoMaterialBuild::BuildAll(Error);
				if (bOk)
				{
					UE_LOG(LogTemp, Display, TEXT("[地质] 材质已生成：%s / %s"),
						GeoDecodeFunctionPath, GeoPlanetMaterialPath);
				}
				else
				{
					UE_LOG(LogTemp, Error, TEXT("[地质] 材质生成失败：%s"), *Error);
				}
				// -TestExit 靠日志里出现这一行退出进程：成功、失败都打，否则失败时无头进程会一直挂着。
				UE_LOG(LogTemp, Display, TEXT("StellarEra.GeoMaterials.Done ok=%d"), bOk ? 1 : 0);
			}));
} // namespace

bool GeoMaterialBuild::BuildAll(FString& OutError)
{
	OutError.Reset();

	UMaterialFunction* Function = GeoFindOrCreateAsset<UMaterialFunction>(
		GeoDecodeFunctionPath, GeoDecodeFunctionName, OutError);
	if (!Function)
	{
		return false;
	}

	const bool bFunctionOnDisk = FPackageName::DoesPackageExist(GeoDecodeFunctionPath);
	const FString PreviousFunctionSignature = Function->GetExpressionCollection().Expressions.Num() > 0
		? GeoGraphSignature(Function->GetExpressionCollection()) : FString();

	Function->PreEditChange(nullptr);
	Function->GetExpressionCollection().Empty();
	if (!BuildDecodeFunction(Function, OutError))
	{
		return false;
	}

	// 图没变又已有资产就不落盘：引擎按对象名后缀/导入表顺序序列化，同样的图重建一次字节也会变。
	if (!bFunctionOnDisk
		|| PreviousFunctionSignature != GeoGraphSignature(Function->GetExpressionCollection()))
	{
		Function->PostEditChange();
		Function->UpdateFromFunctionResource();
		// StateId 决定调用方是否重编；引擎默认随机生成，重建一次就换一次。按节点集合派生，内容变了才变。
		{
			FString GraphKey;
			for (const UMaterialExpression* Expr : Function->GetExpressionCollection().Expressions)
			{
				GraphKey += Expr->GetClass()->GetName();
				GraphKey += Expr->MaterialExpressionGuid.ToString();
			}
			Function->StateId = GeoStableGuid(TEXT("FunctionState"), *GraphKey);
		}
		if (!GeoSaveAsset(Function, GeoDecodeFunctionPath, OutError))
		{
			return false;
		}
	}

	UMaterial* Material = GeoFindOrCreateAsset<UMaterial>(
		GeoPlanetMaterialPath, GeoPlanetMaterialName, OutError);
	if (!Material)
	{
		return false;
	}

	const bool bMaterialOnDisk = FPackageName::DoesPackageExist(GeoPlanetMaterialPath);
	const FString PreviousMaterialSignature = Material->GetExpressionCollection().Expressions.Num() > 0
		? GeoGraphSignature(Material->GetExpressionCollection()) : FString();

	Material->PreEditChange(nullptr);
	Material->GetExpressionCollection().Empty();
	if (!BuildPlanetMaterial(Material, Function, OutError))
	{
		return false;
	}

	if (!bMaterialOnDisk
		|| PreviousMaterialSignature != GeoGraphSignature(Material->GetExpressionCollection()))
	{
		Material->PostEditChange();
		{
			FString GraphKey;
			for (const UMaterialExpression* Expr : Material->GetExpressionCollection().Expressions)
			{
				GraphKey += Expr->GetClass()->GetName();
				GraphKey += Expr->MaterialExpressionGuid.ToString();
			}
			Material->StateId = GeoStableGuid(TEXT("MaterialState"), *GraphKey);
			// LightingGuid 私有且没有 setter，但同样随材质变更随机换值并落盘；只能反射写回。
			if (FProperty* LightingProperty =
				FindFProperty<FProperty>(UMaterialInterface::StaticClass(), TEXT("LightingGuid")))
			{
				*static_cast<FGuid*>(LightingProperty->ContainerPtrToValuePtr<void>(Material)) =
					GeoStableGuid(TEXT("MaterialLighting"), *GraphKey);
			}
		}
		return GeoSaveAsset(Material, GeoPlanetMaterialPath, OutError);
	}
	return true;
}

#endif // WITH_EDITOR
