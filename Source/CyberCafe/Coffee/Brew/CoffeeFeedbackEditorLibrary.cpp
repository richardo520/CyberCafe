#include "Coffee/Brew/CoffeeFeedbackEditorLibrary.h"

#if WITH_EDITOR
#include "AssetRegistry/IAssetRegistry.h"
#include "Coffee/Grinder/CoffeeGrinderActor.h"
#include "Components/AudioComponent.h"
#include "Distributions/DistributionFloatConstant.h"
#include "Distributions/DistributionFloatUniform.h"
#include "Distributions/DistributionVector.h"
#include "Dom/JsonObject.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "Particles/Lifetime/ParticleModuleLifetime.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleLODLevel.h"
#include "Particles/ParticleModule.h"
#include "Particles/ParticleModuleRequired.h"
#include "Particles/ParticleSpriteEmitter.h"
#include "Particles/ParticleSystem.h"
#include "Particles/Size/ParticleModuleSize.h"
#include "Particles/Size/ParticleModuleSizeBase.h"
#include "Particles/Size/ParticleModuleSizeMultiplyLife.h"
#include "Particles/Size/ParticleModuleSizeScale.h"
#include "Particles/Spawn/ParticleModuleSpawn.h"
#include "Particles/TypeData/ParticleModuleTypeDataBase.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundCue.h"
#include "Sound/SoundNodeWavePlayer.h"
#include "Sound/SoundWave.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace CoffeeFeedbackEditorPrivate
{
    constexpr const TCHAR* SteamPath = TEXT("/Game/Coffee_Equipment/Particles/P_Steam_Lit.P_Steam_Lit");
    constexpr const TCHAR* GrindPath = TEXT("/Game/coffee_grinder_scene/Sound/S_Grind.S_Grind");
    constexpr const TCHAR* OwnedCuePrefix = TEXT("/Game/VRTemplate/CoffeeBrew/Audio/SC_Grind_Feedback_");
    constexpr const TCHAR* GrinderClassPath = TEXT("/Game/VRTemplate/Blueprints/Interaction/Coffee/Grinder/BP_CoffeeGrinder.BP_CoffeeGrinder_C");
    constexpr int32 SampleSeed = 56012;

    FString ToJson(const TSharedRef<FJsonObject>& Report)
    {
        FString Result;
        const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
        return FJsonSerializer::Serialize(Report, Writer) ? Result : TEXT("{\"ok\":false,\"error\":\"JSON 序列化失败\"}");
    }

    FString FeedbackFailureJson(const FString& Message)
    {
        const TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
        Report->SetBoolField(TEXT("ok"), false);
        Report->SetStringField(TEXT("error"), Message);
        return ToJson(Report);
    }

    void Number(const TSharedRef<FJsonObject>& Report, const TCHAR* Name, double Value)
    {
        if (FMath::IsFinite(Value)) Report->SetNumberField(Name, Value);
        else Report->SetField(Name, MakeShared<FJsonValueNull>());
    }

    TArray<TSharedPtr<FJsonValue>> Vector(const FVector& Value)
    {
        TArray<TSharedPtr<FJsonValue>> Result;
        for (int32 Axis = 0; Axis < 3; ++Axis)
        {
            if (FMath::IsFinite(Value[Axis])) Result.Add(MakeShared<FJsonValueNumber>(Value[Axis]));
            else Result.Add(MakeShared<FJsonValueNull>());
        }
        return Result;
    }

    // 属性查找限定原生 Engine 类；只读元素，绝不通过脚本数组接口写引擎字段。
    template <typename ElementType>
    TArray<ElementType*> ReadEngineArray(const UObject* Object, const UClass* DeclaringClass,
        const TCHAR* Name, const TSharedRef<FJsonObject>& Resolution)
    {
        TArray<ElementType*> Result;
        const FArrayProperty* Property = FindFProperty<FArrayProperty>(DeclaringClass, FName(Name));
        const FObjectPropertyBase* Inner = Property ? CastField<FObjectPropertyBase>(Property->Inner) : nullptr;
        const bool bResolved = Object && Object->IsA(DeclaringClass) && Property && Inner
            && Property->GetOwnerClass()->GetOutermost()->GetName() == TEXT("/Script/Engine")
            && Inner->PropertyClass->IsChildOf(ElementType::StaticClass());
        Resolution->SetBoolField(Name, bResolved);
        if (!bResolved) return Result;
        Resolution->SetStringField(FString(Name) + TEXT("PropertyPath"), Property->GetPathName());
        const void* ArrayAddress = Property->ContainerPtrToValuePtr<void>(Object);
        FScriptArrayHelper Array(Property, ArrayAddress);
        for (int32 Index = 0; Index < Array.Num(); ++Index)
            Result.Add(Cast<ElementType>(Inner->GetObjectPropertyValue(Array.GetRawPtr(Index))));
        return Result;
    }

    bool SampleFloat(const FRawDistributionFloat& Raw, float Time, float& Value, FString& Method)
    {
        FRandomStream Stream(SampleSeed);
        // GetValue/HasLookupTable 默认会 Initialize 并清源 Distribution.bIsDirty。
        // 局部副本断开 UObject 引用后才可使用 Raw.GetValue；缺表/脏表用 const 源接口。
        FRawDistributionFloat Copy = Raw;
        const UDistributionFloat* Distribution = Raw.Distribution.Get();
        Copy.Distribution = nullptr;
        if ((!Distribution || !Distribution->bIsDirty) && Copy.HasLookupTable(false))
        {
            Value = Copy.GetValue(Time, nullptr, &Stream);
            Method = TEXT("FRawDistributionFloat 局部脱离引用副本.GetValue");
            return FMath::IsFinite(Value);
        }
        if (Distribution)
        {
            Value = Distribution->GetValue(Time, nullptr, &Stream);
            Method = TEXT("UDistributionFloat::GetValue const；无有效干净查找表时的源分布采样");
            return FMath::IsFinite(Value);
        }
        Method = TEXT("无源分布且无可用查找表；不将缺失数据当成零");
        return false;
    }

    bool SampleVector(const FRawDistributionVector& Raw, float Time, FVector& Value, FString& Method)
    {
        FRandomStream Stream(SampleSeed);
        FRawDistributionVector Copy = Raw;
        const UDistributionVector* Distribution = Raw.Distribution.Get();
        Copy.Distribution = nullptr;
        if ((!Distribution || !Distribution->bIsDirty) && Copy.HasLookupTable(false))
        {
            Value = Copy.GetValue(Time, nullptr, 0, &Stream);
            Method = TEXT("FRawDistributionVector 局部脱离引用副本.GetValue");
            return !Value.ContainsNaN();
        }
        if (Distribution)
        {
            Value = Distribution->GetValue(Time, nullptr, 0, &Stream);
            Method = TEXT("UDistributionVector::GetValue const；无有效干净查找表时的源分布采样");
            return !Value.ContainsNaN();
        }
        Method = TEXT("无源分布且无可用查找表；不将缺失数据当成零");
        return false;
    }

    bool SimpleMean(const FRawDistributionFloat& Raw, double& Mean)
    {
        const UDistributionFloat* Distribution = Raw.Distribution.Get();
        if (Distribution && Distribution->GetClass() == UDistributionFloatConstant::StaticClass())
            Mean = CastChecked<UDistributionFloatConstant>(Distribution)->Constant;
        else if (Distribution && Distribution->GetClass() == UDistributionFloatUniform::StaticClass())
        {
            const UDistributionFloatUniform* Uniform = CastChecked<UDistributionFloatUniform>(Distribution);
            Mean = (static_cast<double>(Uniform->Min) + Uniform->Max) * .5;
        }
        else return false;
        return FMath::IsFinite(Mean);
    }

    TSharedRef<FJsonObject> FloatDistribution(const FRawDistributionFloat& Raw)
    {
        const TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
        const UDistributionFloat* Distribution = Raw.Distribution.Get();
        Report->SetStringField(TEXT("distributionClass"), Distribution ? Distribution->GetClass()->GetPathName() : TEXT(""));
        Report->SetStringField(TEXT("distributionPath"), GetPathNameSafe(Distribution));
        FRawDistributionFloat Copy = Raw;
        Copy.Distribution = nullptr;
        Report->SetBoolField(TEXT("hasLookupTableWithoutInitialize"), Copy.HasLookupTable(false));
        if (Distribution || Copy.HasLookupTable(false))
        {
            float Min = 0, Max = 0;
            if (Distribution) Distribution->GetOutRange(Min, Max);
            else Copy.GetOutRange(Min, Max);
            Number(Report, TEXT("min"), Min);
            Number(Report, TEXT("max"), Max);
            Report->SetStringField(TEXT("rangeBasis"), Distribution ? TEXT("源分布 const GetOutRange") : TEXT("Raw 已存缓存范围"));
        }
        TArray<TSharedPtr<FJsonValue>> Samples;
        for (const float Time : {0.f, .5f, 1.f})
        {
            const TSharedRef<FJsonObject> Sample = MakeShared<FJsonObject>();
            float Value = 0;
            FString Method;
            const bool bResolved = SampleFloat(Raw, Time, Value, Method);
            Sample->SetNumberField(TEXT("input"), Time);
            Sample->SetBoolField(TEXT("resolved"), bResolved);
            Sample->SetStringField(TEXT("method"), Method);
            if (bResolved) Number(Sample, TEXT("value"), Value);
            Samples.Add(MakeShared<FJsonValueObject>(Sample));
        }
        Report->SetArrayField(TEXT("samples"), Samples);
        double Mean = 0;
        if (SimpleMean(Raw, Mean)) Number(Report, TEXT("constantOrUniformMean"), Mean);
        else Report->SetField(TEXT("constantOrUniformMean"), MakeShared<FJsonValueNull>());
        return Report;
    }

    TSharedRef<FJsonObject> VectorDistribution(const FRawDistributionVector& Raw)
    {
        const TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
        const UDistributionVector* Distribution = Raw.Distribution.Get();
        Report->SetStringField(TEXT("distributionClass"), Distribution ? Distribution->GetClass()->GetPathName() : TEXT(""));
        Report->SetStringField(TEXT("distributionPath"), GetPathNameSafe(Distribution));
        FRawDistributionVector Copy = Raw;
        Copy.Distribution = nullptr;
        Report->SetBoolField(TEXT("hasLookupTableWithoutInitialize"), Copy.HasLookupTable(false));
        if (Distribution || Copy.HasLookupTable(false))
        {
            FVector Min, Max;
            if (Distribution) Distribution->GetRange(Min, Max);
            else Copy.GetRange(Min, Max);
            Report->SetArrayField(TEXT("min"), Vector(Min));
            Report->SetArrayField(TEXT("max"), Vector(Max));
            Report->SetStringField(TEXT("rangeBasis"), Distribution ? TEXT("源分布 const GetRange") : TEXT("Raw 已存缓存范围"));
        }
        TArray<TSharedPtr<FJsonValue>> Samples;
        for (const float Time : {0.f, .5f, 1.f})
        {
            const TSharedRef<FJsonObject> Sample = MakeShared<FJsonObject>();
            FVector Value;
            FString Method;
            const bool bResolved = SampleVector(Raw, Time, Value, Method);
            Sample->SetNumberField(TEXT("input"), Time);
            Sample->SetBoolField(TEXT("resolved"), bResolved);
            Sample->SetStringField(TEXT("method"), Method);
            if (bResolved) Sample->SetArrayField(TEXT("value"), Vector(Value));
            Samples.Add(MakeShared<FJsonValueObject>(Sample));
        }
        Report->SetArrayField(TEXT("samples"), Samples);
        return Report;
    }

    TSharedRef<FJsonObject> ModuleReport(const UParticleModule* Module)
    {
        const TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
        Report->SetBoolField(TEXT("resolved"), Module != nullptr);
        if (!Module) return Report;
        Report->SetStringField(TEXT("class"), Module->GetClass()->GetPathName());
        Report->SetStringField(TEXT("path"), Module->GetPathName());
        Report->SetBoolField(TEXT("enabled"), Module->bEnabled);
        Report->SetBoolField(TEXT("spawnModule"), Module->bSpawnModule);
        Report->SetBoolField(TEXT("updateModule"), Module->bUpdateModule);
        const TSharedRef<FJsonObject> Distributions = MakeShared<FJsonObject>();
        for (TFieldIterator<FStructProperty> It(Module->GetClass()); It; ++It)
        {
            const FStructProperty* Property = *It;
            const UClass* OwnerClass = Property->GetOwnerClass();
            if (!OwnerClass || OwnerClass->GetOutermost()->GetName() != TEXT("/Script/Engine")) continue;
            TSharedPtr<FJsonObject> Field;
            if (Property->Struct == FRawDistributionFloat::StaticStruct())
                Field = FloatDistribution(*Property->ContainerPtrToValuePtr<FRawDistributionFloat>(Module));
            else if (Property->Struct == FRawDistributionVector::StaticStruct())
                Field = VectorDistribution(*Property->ContainerPtrToValuePtr<FRawDistributionVector>(Module));
            if (Field)
            {
                Field->SetBoolField(TEXT("reflectionResolved"), true);
                Field->SetStringField(TEXT("propertyPath"), Property->GetPathName());
                Field->SetStringField(TEXT("inputMeaning"), Property->GetFName() == TEXT("LifeMultiplier") || Property->GetFName() == TEXT("SizeScale")
                    ? TEXT("粒子 RelativeTime，0..1；1 为寿命边界，不保证仍有存活粒子")
                    : TEXT("原分布输入；StartSize/Lifetime/Rate 为发射器时间秒；其他字段以模块语义为准"));
                Distributions->SetObjectField(Property->GetName(), Field);
            }
        }
        Report->SetObjectField(TEXT("rawDistributions"), Distributions);
        if (const UParticleModuleSizeMultiplyLife* Life = Cast<UParticleModuleSizeMultiplyLife>(Module))
        {
            Report->SetBoolField(TEXT("MultiplyX"), Life->MultiplyX);
            Report->SetBoolField(TEXT("MultiplyY"), Life->MultiplyY);
            Report->SetBoolField(TEXT("MultiplyZ"), Life->MultiplyZ);
        }
        if (Cast<UParticleModuleSizeScale>(Module))
            Report->SetStringField(TEXT("sizeScaleSemantics"), TEXT("Size = BaseSize * SizeScale；覆盖此前尺寸修正；EnableX/Y/Z 在 UE5.6 中忽略"));
        return Report;
    }

    // 只计算已知 CPU 尺寸模块，不读取活粒子、不 Tick；0.12 是诊断假设而非改写组件配置。
    TSharedRef<FJsonObject> SizeModel(const UParticleLODLevel* LOD, const TArray<UParticleModule*>& Modules)
    {
        const TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
        Report->SetNumberField(TEXT("assumedUniformComponentScale"), .12);
        Report->SetNumberField(TEXT("emitterTimeSeconds"), 0);
        Report->SetBoolField(TEXT("liveParticleMeasurement"), false);
        Report->SetStringField(TEXT("basis"), TEXT("源分布实读后推算；CPU sprite 全边长 cm，不是半径；初始尺寸不是整个蒸汽云包围盒"));
        FVector BaseSize = FVector::ZeroVector;
        bool bResolved = LOD->RequiredModule != nullptr && LOD->TypeDataModule == nullptr;
        bool bFoundSize = false;
        for (const UParticleModule* Module : Modules)
        {
            if (!Module || !Module->bEnabled) continue;
            if (const UParticleModuleSize* Size = Cast<UParticleModuleSize>(Module))
            {
                FVector Value;
                FString Method;
                bFoundSize = true;
                const bool bSampled = SampleVector(Size->StartSize, 0.f, Value, Method);
                bResolved &= bSampled;
                if (bSampled && Size->bSpawnModule) BaseSize += Value;
            }
            else if (Module->IsA<UParticleModuleSizeBase>() && !Module->IsA<UParticleModuleSizeMultiplyLife>() && !Module->IsA<UParticleModuleSizeScale>())
                bResolved = false;
        }
        bResolved &= bFoundSize;
        const TSharedRef<FJsonObject> Resolution = MakeShared<FJsonObject>();
        TArray<UParticleModule*> Updates = ReadEngineArray<UParticleModule>(LOD, UParticleLODLevel::StaticClass(), TEXT("UpdateModules"), Resolution);
        Report->SetObjectField(TEXT("updateArrayResolution"), Resolution);
        if (Updates.IsEmpty())
        {
            for (UParticleModule* Module : Modules)
                if (Module && Module->bEnabled && Module->bUpdateModule) Updates.Add(Module);
            Report->SetStringField(TEXT("updateOrderBasis"), TEXT("UpdateModules 为空；按原 Modules 顺序筛选 bUpdateModule，仅推算，不重建引擎数组"));
        }
        else Report->SetStringField(TEXT("updateOrderBasis"), TEXT("只读原 UpdateModules 的实际顺序"));
        TArray<TSharedPtr<FJsonValue>> Samples;
        for (const float RelativeTime : {0.f, .5f, 1.f})
        {
            FVector Size = BaseSize;
            bool bSampleResolved = bResolved;
            for (const UParticleModule* Module : Updates)
            {
                if (!Module || !Module->bEnabled) continue;
                FString Method;
                FVector Factor;
                if (const UParticleModuleSizeMultiplyLife* Life = Cast<UParticleModuleSizeMultiplyLife>(Module))
                {
                    const bool bSampled = SampleVector(Life->LifeMultiplier, RelativeTime, Factor, Method);
                    bSampleResolved &= bSampled;
                    if (bSampled)
                    {
                        if (Life->MultiplyX) Size.X *= Factor.X;
                        if (Life->MultiplyY) Size.Y *= Factor.Y;
                        if (Life->MultiplyZ) Size.Z *= Factor.Z;
                    }
                }
                else if (const UParticleModuleSizeScale* Scale = Cast<UParticleModuleSizeScale>(Module))
                {
                    const bool bSampled = SampleVector(Scale->SizeScale, RelativeTime, Factor, Method);
                    bSampleResolved &= bSampled;
                    if (bSampled) Size = BaseSize * Factor;
                }
            }
            const TSharedRef<FJsonObject> Sample = MakeShared<FJsonObject>();
            Sample->SetNumberField(TEXT("relativeTime"), RelativeTime);
            Sample->SetBoolField(TEXT("resolvedKnownSizeModules"), bSampleResolved);
            if (bSampleResolved)
            {
                Sample->SetArrayField(TEXT("particleSizeBeforeComponentScale"), Vector(Size));
                double Width = FMath::Abs(Size.X * .12);
                double Height = FMath::Abs(Size.Y * .12);
                const EParticleScreenAlignment Alignment = LOD->RequiredModule->ScreenAlignment;
                if (Alignment == PSA_Square || Alignment == PSA_FacingCameraPosition || Alignment == PSA_FacingCameraDistanceBlend) Height = Width;
                Number(Sample, TEXT("spriteWidthCmAtScale012"), Width);
                Number(Sample, TEXT("spriteHeightCmAtScale012"), Height);
                Number(Sample, TEXT("spriteHalfWidthCmAtScale012"), Width * .5);
            }
            Samples.Add(MakeShared<FJsonValueObject>(Sample));
        }
        Report->SetArrayField(TEXT("samples"), Samples);
        Report->SetStringField(TEXT("limitations"), TEXT("同一诊断 seed 的单次样本不是均值或所有粒子的尺寸；不包含实例参数、真实 seed、速度尺寸、材质 WPO、相机偏移、Orbit、SubUV 透明边缘或运行时模块改写；TypeData 非空时不套用 sprite 模型"));
        return Report;
    }

    TSharedRef<FJsonObject> WaveReport(const USoundWave* Wave)
    {
        const TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
        Report->SetStringField(TEXT("path"), Wave->GetPathName());
        Number(Report, TEXT("nativeWaveDurationSeconds"), Wave->Duration);
        Report->SetNumberField(TEXT("numChannels"), Wave->NumChannels);
        Report->SetStringField(TEXT("channelLayout"), Wave->NumChannels == 1 ? TEXT("mono") : Wave->NumChannels == 2 ? TEXT("stereo") : TEXT("multichannel/unknown"));
        // UE5.6 这两个原生字段为 protected，只读精确 FIntProperty，避开缓存 getter。
        for (const TCHAR* Name : {TEXT("SampleRate"), TEXT("ImportedSampleRate")})
        {
            const FIntProperty* Property = FindFProperty<FIntProperty>(USoundWave::StaticClass(), FName(Name));
            const bool bResolved = Property && Property->GetOwnerClass() == USoundWave::StaticClass();
            Report->SetBoolField(FString(Name) + TEXT("ReflectionResolved"), bResolved);
            if (bResolved)
            {
                Report->SetNumberField(Name, Property->GetPropertyValue_InContainer(Wave));
                Report->SetStringField(FString(Name) + TEXT("PropertyPath"), Property->GetPathName());
            }
        }
        Report->SetBoolField(TEXT("sourceLooping"), Wave->bLooping);
        Report->SetStringField(TEXT("sourceSoundClass"), GetPathNameSafe(Wave->SoundClassObject.Get()));
        Report->SetBoolField(TEXT("sourcePackageDirty"), Wave->GetOutermost()->IsDirty());
        Report->SetStringField(TEXT("basis"), TEXT("只读原字段；不调用可能写缓存的采样率 getter，不改原音源 looping/volume/class，不解码播放"));
        return Report;
    }
}
#endif

FString UCoffeeFeedbackEditorLibrary::InspectSteam()
{
#if WITH_EDITOR
    using namespace CoffeeFeedbackEditorPrivate;
    if (!GIsEditor || !IsInGameThread()) return FeedbackFailureJson(TEXT("仅可在编辑器游戏线程显式诊断"));
    const UParticleSystem* System = LoadObject<UParticleSystem>(nullptr, SteamPath);
    if (!System || System->GetPathName() != SteamPath) return FeedbackFailureJson(TEXT("无法解析指定原 P_Steam_Lit，拒绝诊断其他素材"));
    const TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
    Report->SetBoolField(TEXT("ok"), true);
    Report->SetStringField(TEXT("assetPath"), System->GetPathName());
    Report->SetStringField(TEXT("engineHeaderVersion"), TEXT("UE5.6"));
    Report->SetBoolField(TEXT("readOnly"), true);
    Report->SetBoolField(TEXT("liveSimulationMeasured"), false);
    Report->SetNumberField(TEXT("diagnosticSampleSeed"), SampleSeed);
    Report->SetStringField(TEXT("samplingBasis"), TEXT("当前已加载原素材字段实读；Raw 在局部脱离引用副本上 GetValue；缺表/脏表回退 const Distribution.GetValue；Data=nullptr，未使用实际粒子实例参数；不触碰全局 RNG/CVar"));
    const TSharedRef<FJsonObject> Bounds = MakeShared<FJsonObject>();
    Bounds->SetBoolField(TEXT("useFixedRelativeBoundingBox"), System->bUseFixedRelativeBoundingBox);
    Bounds->SetBoolField(TEXT("valid"), System->FixedRelativeBoundingBox.IsValid != 0);
    Bounds->SetArrayField(TEXT("minCm"), Vector(System->FixedRelativeBoundingBox.Min));
    Bounds->SetArrayField(TEXT("maxCm"), Vector(System->FixedRelativeBoundingBox.Max));
    Bounds->SetStringField(TEXT("basis"), TEXT("源 FixedRelativeBoundingBox；禁用时不是实际动态 Bounds；实际云范围需运行模拟"));
    Report->SetObjectField(TEXT("bounds"), Bounds);
    const TSharedRef<FJsonObject> Resolution = MakeShared<FJsonObject>();
    const TArray<UParticleEmitter*> Emitters = ReadEngineArray<UParticleEmitter>(System, UParticleSystem::StaticClass(), TEXT("Emitters"), Resolution);
    Report->SetObjectField(TEXT("reflectionResolution"), Resolution);
    if (!Resolution->GetBoolField(TEXT("Emitters"))) return FeedbackFailureJson(TEXT("Engine UParticleSystem.Emitters 数组反射解析失败"));
    TArray<TSharedPtr<FJsonValue>> EmitterReports;
    for (int32 EmitterIndex = 0; EmitterIndex < Emitters.Num(); ++EmitterIndex)
    {
        const UParticleEmitter* Emitter = Emitters[EmitterIndex];
        const TSharedRef<FJsonObject> EmitterReport = MakeShared<FJsonObject>();
        EmitterReport->SetNumberField(TEXT("index"), EmitterIndex);
        EmitterReport->SetBoolField(TEXT("resolved"), Emitter != nullptr);
        if (Emitter)
        {
            EmitterReport->SetStringField(TEXT("name"), Emitter->EmitterName.ToString());
            EmitterReport->SetStringField(TEXT("class"), Emitter->GetClass()->GetPathName());
            const TSharedRef<FJsonObject> LODResolution = MakeShared<FJsonObject>();
            const TArray<UParticleLODLevel*> LODs = ReadEngineArray<UParticleLODLevel>(Emitter, UParticleEmitter::StaticClass(), TEXT("LODLevels"), LODResolution);
            EmitterReport->SetObjectField(TEXT("reflectionResolution"), LODResolution);
            TArray<TSharedPtr<FJsonValue>> LODReports;
            for (const UParticleLODLevel* LOD : LODs)
            {
                const TSharedRef<FJsonObject> LODReport = MakeShared<FJsonObject>();
                LODReport->SetBoolField(TEXT("resolved"), LOD != nullptr);
                if (LOD)
                {
                    LODReport->SetNumberField(TEXT("level"), LOD->Level);
                    LODReport->SetBoolField(TEXT("enabled"), LOD->bEnabled);
                    LODReport->SetStringField(TEXT("typeDataClass"), LOD->TypeDataModule ? LOD->TypeDataModule->GetClass()->GetPathName() : TEXT(""));
                    const TSharedRef<FJsonObject> Required = ModuleReport(LOD->RequiredModule.Get());
                    if (const UParticleModuleRequired* Module = LOD->RequiredModule.Get())
                    {
                        Number(Required, TEXT("EmitterDurationSeconds"), Module->EmitterDuration);
                        Number(Required, TEXT("EmitterDurationLowSeconds"), Module->EmitterDurationLow);
                        Required->SetBoolField(TEXT("bEmitterDurationUseRange"), Module->bEmitterDurationUseRange);
                        Required->SetBoolField(TEXT("bDurationRecalcEachLoop"), Module->bDurationRecalcEachLoop);
                        Required->SetNumberField(TEXT("EmitterLoops"), Module->EmitterLoops);
                        Required->SetBoolField(TEXT("loopsForever"), Module->EmitterLoops == 0);
                        Required->SetBoolField(TEXT("UseLocalSpace"), Module->bUseLocalSpace);
                        Required->SetBoolField(TEXT("bKillOnDeactivate"), Module->bKillOnDeactivate);
                        Required->SetBoolField(TEXT("bKillOnCompleted"), Module->bKillOnCompleted);
                        Required->SetArrayField(TEXT("EmitterOriginCm"), Vector(Module->EmitterOrigin));
                        Required->SetArrayField(TEXT("EmitterRotationPitchYawRollDegrees"), Vector(FVector(Module->EmitterRotation.Pitch, Module->EmitterRotation.Yaw, Module->EmitterRotation.Roll)));
                        Required->SetNumberField(TEXT("ScreenAlignment"), static_cast<int32>(Module->ScreenAlignment.GetValue()));
                        Required->SetStringField(TEXT("MaterialName"), GetNameSafe(Module->Material.Get()));
                        Required->SetStringField(TEXT("MaterialPath"), GetPathNameSafe(Module->Material.Get()));
                        TArray<TSharedPtr<FJsonValue>> Overrides;
                        for (const FName Name : Module->NamedMaterialOverrides) Overrides.Add(MakeShared<FJsonValueString>(Name.ToString()));
                        Required->SetArrayField(TEXT("NamedMaterialOverrides"), Overrides);
                        Required->SetStringField(TEXT("movingEmitterTail"), Module->bUseLocalSpace
                            ? TEXT("true：旧粒子随 PSC 新世界变换整体移动。现有 HotWaterSteamComponent 每次落点刷新都会 SetWorldLocation；SetAbsolute 只取消父级继承，不会把局部粒子改为世界空间。停发后若组件不再移动且未被 bKillOnDeactivate/强制停止清除，尾汽留在最后落点。")
                            : TEXT("false：已有粒子在世界空间，不因 PSC 平移整体搬走；新粒子随新落点生成。SourceMovement/Orbit 等模块仍可能另行改变位置。"));
                    }
                    LODReport->SetObjectField(TEXT("required"), Required);
                    const TSharedRef<FJsonObject> Spawn = ModuleReport(LOD->SpawnModule.Get());
                    if (const UParticleModuleSpawn* Module = LOD->SpawnModule.Get())
                    {
                        Spawn->SetNumberField(TEXT("burstCount"), Module->BurstList.Num());
                        Spawn->SetBoolField(TEXT("bApplyGlobalSpawnRateScale"), Module->bApplyGlobalSpawnRateScale);
                        double RateMean = 0, ScaleMean = 0;
                        const bool bMeanResolved = SimpleMean(Module->Rate, RateMean) && SimpleMean(Module->RateScale, ScaleMean);
                        Spawn->SetBoolField(TEXT("meanSpawnRateResolved"), bMeanResolved);
                        if (bMeanResolved) Number(Spawn, TEXT("meanSpawnRateBeforeGlobalScalePerSecond"), RateMean * ScaleMean);
                        Spawn->SetStringField(TEXT("meanBasis"), TEXT("只在 Rate/RateScale 为原生 Constant/Uniform 时取期望乘积；未应用全局 CVar、质量缩放、实例参数、Burst 或其他 Spawn 模块；单次 GetValue(0) 不是均值"));
                    }
                    LODReport->SetObjectField(TEXT("spawn"), Spawn);
                    const TSharedRef<FJsonObject> ModuleResolution = MakeShared<FJsonObject>();
                    const TArray<UParticleModule*> Modules = ReadEngineArray<UParticleModule>(LOD, UParticleLODLevel::StaticClass(), TEXT("Modules"), ModuleResolution);
                    LODReport->SetObjectField(TEXT("reflectionResolution"), ModuleResolution);
                    TArray<TSharedPtr<FJsonValue>> ModuleReports;
                    for (const UParticleModule* Module : Modules) ModuleReports.Add(MakeShared<FJsonValueObject>(ModuleReport(Module)));
                    LODReport->SetArrayField(TEXT("modules"), ModuleReports);
                    LODReport->SetObjectField(TEXT("spriteSizeModelAtScale012"), SizeModel(LOD, Modules));
                }
                LODReports.Add(MakeShared<FJsonValueObject>(LODReport));
            }
            EmitterReport->SetArrayField(TEXT("lods"), LODReports);
        }
        EmitterReports.Add(MakeShared<FJsonValueObject>(EmitterReport));
    }
    Report->SetArrayField(TEXT("emitters"), EmitterReports);
    Report->SetBoolField(TEXT("sourcePackageDirtyAfterRead"), System->GetOutermost()->IsDirty());
    Report->SetStringField(TEXT("limitations"), TEXT("这是调用时的原素材内省与分布采样，不是本次已运行实测；默认不扫描世界或输入设备、不启动 FX、不调整组件/全局开关。材质实例覆盖、动态 Bounds 和实际 0.12 组件状态需另行显式验证。"));
    return ToJson(Report);
#else
    return TEXT("{\"ok\":false,\"error\":\"WITH_EDITOR 不可用\"}");
#endif
}

FString UCoffeeFeedbackEditorLibrary::TunePourSteam(UParticleSystem* Target)
{
#if WITH_EDITOR
    using namespace CoffeeFeedbackEditorPrivate;
    constexpr const TCHAR* PourPackagePath = TEXT("/Game/VRTemplate/CoffeeBrew/Particles/P_Steam_Pour_Feedback");
    const FString PourObjectPath = FString(PourPackagePath) + TEXT(".P_Steam_Pour_Feedback");
    if (!GIsEditor || !IsInGameThread()) return FeedbackFailureJson(TEXT("仅可在编辑器游戏线程显式调整蒸汽副本"));
    if (!IsValid(Target) || Target->IsTemplate() || Target->GetClass() != UParticleSystem::StaticClass()
        || Target->GetPathName() != PourObjectPath || Target->GetOutermost()->GetName() != PourPackagePath
        || Target->GetOuter() != Target->GetOutermost())
        return FeedbackFailureJson(TEXT("仅接受调用方从 P_Steam_Lit 复制的指定 P_Steam_Pour_Feedback 资产；未修改目标"));
    const UParticleSystem* Source = LoadObject<UParticleSystem>(nullptr, SteamPath);
    if (!Source || Source == Target || Source->GetPathName() != SteamPath)
        return FeedbackFailureJson(TEXT("无法解析指定原 P_Steam_Lit；未修改目标"));

    const FString OwnedObjectPrefix = PourObjectPath + TEXT(":");
    const auto IsOwnedObject = [Target, &OwnedObjectPrefix](const UObject* Object)
    {
        return IsValid(Object) && !Object->IsTemplate() && Object->IsIn(Target)
            && Object->GetOutermost() == Target->GetOutermost()
            && Object->GetPathName().StartsWith(OwnedObjectPrefix, ESearchCase::CaseSensitive);
    };
    const auto ValidateOwnedModule = [Target, &IsOwnedObject](const UParticleModule* Module) -> FString
    {
        // UE5.6 UParticleModule::PostEditChangeProperty 对 GetOuter() 做 ParticleSystem CastChecked。
        if (!IsOwnedObject(Module) || Module->GetOuter() != Target
            || Module->GetClass()->GetOutermost()->GetName() != TEXT("/Script/Engine"))
            return FString::Printf(TEXT("模块不是副本直接拥有的原生 Engine 子对象：%s"), *GetPathNameSafe(Module));
        for (TFieldIterator<FStructProperty> It(Module->GetClass()); It; ++It)
        {
            const FStructProperty* Property = *It;
            const UObject* Distribution = nullptr;
            if (Property->Struct == FRawDistributionFloat::StaticStruct())
                Distribution = Property->ContainerPtrToValuePtr<FRawDistributionFloat>(Module)->Distribution.Get();
            else if (Property->Struct == FRawDistributionVector::StaticStruct())
                Distribution = Property->ContainerPtrToValuePtr<FRawDistributionVector>(Module)->Distribution.Get();
            if (Distribution && (!IsOwnedObject(Distribution) || !Distribution->IsIn(Module)))
                return FString::Printf(TEXT("分布未深复制或仍引用源子对象：%s = %s"),
                    *Property->GetPathName(), *Distribution->GetPathName());
        }
        return FString();
    };
    const FStructProperty* RateProperty = FindFProperty<FStructProperty>(UParticleModuleSpawn::StaticClass(), TEXT("Rate"));
    const FStructProperty* LifetimeProperty = FindFProperty<FStructProperty>(UParticleModuleLifetime::StaticClass(), TEXT("Lifetime"));
    if (!RateProperty || RateProperty->GetOwnerClass() != UParticleModuleSpawn::StaticClass()
        || RateProperty->Struct != FRawDistributionFloat::StaticStruct()
        || !LifetimeProperty || LifetimeProperty->GetOwnerClass() != UParticleModuleLifetime::StaticClass()
        || LifetimeProperty->Struct != FRawDistributionFloat::StaticStruct())
        return FeedbackFailureJson(TEXT("Engine Rate/Lifetime 原生 RawDistributionFloat 字段解析失败；未修改目标"));

    const TSharedRef<FJsonObject> Resolution = MakeShared<FJsonObject>();
    const TArray<UParticleEmitter*> Emitters = ReadEngineArray<UParticleEmitter>(Target, UParticleSystem::StaticClass(), TEXT("Emitters"), Resolution);
    const TArray<UParticleEmitter*> SourceEmitters = ReadEngineArray<UParticleEmitter>(Source, UParticleSystem::StaticClass(), TEXT("Emitters"), Resolution);
    if (Emitters.Num() != 1 || !IsOwnedObject(Emitters[0])
        || Emitters[0]->GetClass() != UParticleSpriteEmitter::StaticClass()
        || SourceEmitters.Num() != 1 || !SourceEmitters[0] || !SourceEmitters[0]->IsIn(Source)
        || SourceEmitters[0]->GetClass() != UParticleSpriteEmitter::StaticClass())
        return FeedbackFailureJson(TEXT("仅支持与原源匹配的单个 Engine ParticleSpriteEmitter；未修改目标"));
    const TArray<UParticleLODLevel*> LODs = ReadEngineArray<UParticleLODLevel>(Emitters[0], UParticleEmitter::StaticClass(), TEXT("LODLevels"), Resolution);
    const TArray<UParticleLODLevel*> SourceLODs = ReadEngineArray<UParticleLODLevel>(SourceEmitters[0], UParticleEmitter::StaticClass(), TEXT("LODLevels"), Resolution);
    if (LODs.IsEmpty() || LODs.Num() != SourceLODs.Num())
        return FeedbackFailureJson(TEXT("副本全部 LOD 必须存在且数量与原源一致；未修改目标"));

    struct FPourSteamLODPlan
    {
        UParticleLODLevel* LOD;
        const UParticleLODLevel* SourceLOD;
        UParticleModuleRequired* Required;
        UParticleModuleSpawn* Spawn;
        UParticleModuleLifetime* Lifetime;
        FRawDistributionFloat* RateRaw;
        FRawDistributionFloat* LifetimeRaw;
        TArray<UParticleModule*> Modules;
        TArray<UParticleModule*> SourceModules;
    };
    TArray<FPourSteamLODPlan> Plans;
    // 第一阶段只读：所有 LOD 的结构、模块与每个 Raw 分布归属均通过才允许 Modify/NewObject。
    for (int32 LODIndex = 0; LODIndex < LODs.Num(); ++LODIndex)
    {
        UParticleLODLevel* LOD = LODs[LODIndex];
        const UParticleLODLevel* SourceLOD = SourceLODs[LODIndex];
        if (!IsOwnedObject(LOD) || LOD->GetClass() != UParticleLODLevel::StaticClass()
            || !SourceLOD || !SourceLOD->IsIn(Source) || LOD->Level != SourceLOD->Level
            || LOD->TypeDataModule || SourceLOD->TypeDataModule)
            return FeedbackFailureJson(TEXT("LOD 缺失、归属/层级不匹配或含非 CPU sprite TypeData；未修改目标"));
        UParticleModuleRequired* Required = LOD->RequiredModule.Get();
        UParticleModuleSpawn* Spawn = LOD->SpawnModule.Get();
        if (!Required || Required->GetClass() != UParticleModuleRequired::StaticClass() || !Required->bEnabled
            || !Spawn || Spawn->GetClass() != UParticleModuleSpawn::StaticClass() || !Spawn->bEnabled
            || !SourceLOD->RequiredModule || !SourceLOD->SpawnModule
            || SourceLOD->RequiredModule->GetClass() != Required->GetClass()
            || SourceLOD->SpawnModule->GetClass() != Spawn->GetClass()
            || SourceLOD->RequiredModule->GetOuter() != Source || SourceLOD->SpawnModule->GetOuter() != Source
            || Required->Material != SourceLOD->RequiredModule->Material)
            return FeedbackFailureJson(TEXT("Required/Spawn 原生模块缺失、禁用或材质引用与原源不匹配；未修改目标"));
        if (Required->EmitterDelay != 0.f || (Required->bEmitterDelayUseRange && Required->EmitterDelayLow != 0.f))
            return FeedbackFailureJson(TEXT("副本存在发射延迟，不能保证开始时 Burst；不自动修改未请求字段，未修改目标"));
        TArray<UParticleModule*> Modules = ReadEngineArray<UParticleModule>(LOD, UParticleLODLevel::StaticClass(), TEXT("Modules"), Resolution);
        TArray<UParticleModule*> SourceModules = ReadEngineArray<UParticleModule>(SourceLOD, UParticleLODLevel::StaticClass(), TEXT("Modules"), Resolution);
        if (!Resolution->GetBoolField(TEXT("Modules")) || Modules.IsEmpty() || Modules.Num() != SourceModules.Num())
            return FeedbackFailureJson(TEXT("LOD.Modules 数组解析失败或与原源不匹配；未修改目标"));
        UParticleModuleLifetime* Lifetime = nullptr;
        for (int32 ModuleIndex = 0; ModuleIndex < Modules.Num(); ++ModuleIndex)
        {
            UParticleModule* Module = Modules[ModuleIndex];
            const UParticleModule* SourceModule = SourceModules[ModuleIndex];
            if (!Module || !SourceModule || SourceModule->GetOuter() != Source
                || Module->GetClass() != SourceModule->GetClass())
                return FeedbackFailureJson(TEXT("LOD 模块类/顺序与原源不匹配；未修改目标"));
            if (Module->IsA<UParticleModuleLifetime>())
            {
                if (Lifetime || Module->GetClass() != UParticleModuleLifetime::StaticClass() || !Module->bEnabled)
                    return FeedbackFailureJson(TEXT("仅支持每 LOD 一个启用的原生 Lifetime 模块；未修改目标"));
                Lifetime = CastChecked<UParticleModuleLifetime>(Module);
            }
        }
        if (!Lifetime) return FeedbackFailureJson(TEXT("LOD 缺少请求的 Lifetime 模块；未修改目标"));
        TArray<UParticleModule*> AllModules = Modules;
        AllModules.AddUnique(Required);
        AllModules.AddUnique(Spawn);
        for (const UParticleModule* Module : AllModules)
        {
            const FString Failure = ValidateOwnedModule(Module);
            if (!Failure.IsEmpty()) return FeedbackFailureJson(Failure + TEXT("；全部校验未通过，未修改目标"));
        }
        FRawDistributionFloat* RateRaw = RateProperty->ContainerPtrToValuePtr<FRawDistributionFloat>(Spawn);
        FRawDistributionFloat* LifetimeRaw = LifetimeProperty->ContainerPtrToValuePtr<FRawDistributionFloat>(Lifetime);
        if (!RateRaw->Distribution || !LifetimeRaw->Distribution || !Spawn->RateScale.Distribution || !Spawn->BurstScale.Distribution)
            return FeedbackFailureJson(TEXT("Rate/Lifetime/RateScale/BurstScale 分布缺失；未修改目标"));
        // 保留源倍率而非覆盖它们；这里拒绝会改变 10/s 和单颗 Burst 语义的副本。
        for (const FRawDistributionFloat* Scale : {&Spawn->RateScale, &Spawn->BurstScale})
        {
            float Min = 0, Max = 0;
            Scale->Distribution->GetOutRange(Min, Max);
            if (Min != 1.f || Max != 1.f)
                return FeedbackFailureJson(TEXT("RateScale/BurstScale 必须保持原源单位倍率；未修改目标"));
        }
        Plans.Add({LOD, SourceLOD, Required, Spawn, Lifetime, RateRaw, LifetimeRaw, MoveTemp(Modules), MoveTemp(SourceModules)});
    }

    const auto SnapshotModule = [&IsOwnedObject](const UParticleModule* Module)
    {
        const TSharedRef<FJsonObject> Result = ModuleReport(Module);
        Result->SetStringField(TEXT("outerPath"), GetPathNameSafe(Module->GetOuter()));
        Result->SetBoolField(TEXT("ownedByTarget"), IsOwnedObject(Module));
        return Result;
    };
    const auto SnapshotLOD = [&SnapshotModule](const UParticleLODLevel* LOD, const TArray<UParticleModule*>& Modules)
    {
        const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("path"), LOD->GetPathName());
        Result->SetNumberField(TEXT("level"), LOD->Level);
        Result->SetBoolField(TEXT("enabled"), LOD->bEnabled);
        const UParticleModuleRequired* Required = LOD->RequiredModule.Get();
        const TSharedRef<FJsonObject> RequiredReport = SnapshotModule(Required);
        RequiredReport->SetBoolField(TEXT("UseLocalSpace"), Required->bUseLocalSpace);
        RequiredReport->SetBoolField(TEXT("bKillOnDeactivate"), Required->bKillOnDeactivate);
        RequiredReport->SetBoolField(TEXT("bKillOnCompleted"), Required->bKillOnCompleted);
        RequiredReport->SetNumberField(TEXT("EmitterLoops"), Required->EmitterLoops);
        Number(RequiredReport, TEXT("EmitterDurationSeconds"), Required->EmitterDuration);
        Number(RequiredReport, TEXT("EmitterDurationLowSeconds"), Required->EmitterDurationLow);
        RequiredReport->SetBoolField(TEXT("bEmitterDurationUseRange"), Required->bEmitterDurationUseRange);
        Number(RequiredReport, TEXT("EmitterDelaySeconds"), Required->EmitterDelay);
        RequiredReport->SetStringField(TEXT("MaterialPath"), GetPathNameSafe(Required->Material.Get()));
        Result->SetObjectField(TEXT("required"), RequiredReport);
        const UParticleModuleSpawn* Spawn = LOD->SpawnModule.Get();
        const TSharedRef<FJsonObject> SpawnReport = SnapshotModule(Spawn);
        SpawnReport->SetBoolField(TEXT("bApplyGlobalSpawnRateScale"), Spawn->bApplyGlobalSpawnRateScale);
        TArray<TSharedPtr<FJsonValue>> Bursts;
        for (const FParticleBurst& Burst : Spawn->BurstList)
        {
            const TSharedRef<FJsonObject> BurstReport = MakeShared<FJsonObject>();
            Number(BurstReport, TEXT("Time"), Burst.Time);
            BurstReport->SetNumberField(TEXT("Count"), Burst.Count);
            BurstReport->SetNumberField(TEXT("CountLow"), Burst.CountLow);
            Bursts.Add(MakeShared<FJsonValueObject>(BurstReport));
        }
        SpawnReport->SetArrayField(TEXT("BurstList"), Bursts);
        Result->SetObjectField(TEXT("spawn"), SpawnReport);
        TArray<TSharedPtr<FJsonValue>> ModuleReports;
        for (const UParticleModule* Module : Modules) ModuleReports.Add(MakeShared<FJsonValueObject>(SnapshotModule(Module)));
        Result->SetArrayField(TEXT("modules"), ModuleReports);
        return Result;
    };
    const bool bSourceDirtyBefore = Source->GetOutermost()->IsDirty();
    const bool bTargetDirtyBefore = Target->GetOutermost()->IsDirty();
    TArray<TSharedPtr<FJsonValue>> Before, SourceBefore;
    for (const FPourSteamLODPlan& Plan : Plans)
    {
        Before.Add(MakeShared<FJsonValueObject>(SnapshotLOD(Plan.LOD, Plan.Modules)));
        SourceBefore.Add(MakeShared<FJsonValueObject>(SnapshotLOD(Plan.SourceLOD, Plan.SourceModules)));
    }

    // 第二阶段只写已验证的副本；共享 LOD 模块只更新一次，不复制/修复归属不明的模块。
    Target->Modify();
    TSet<UParticleModule*> ChangedModules;
    for (const FPourSteamLODPlan& Plan : Plans)
    {
        if (!ChangedModules.Contains(Plan.Required))
        {
            Plan.Required->Modify();
            Plan.Required->bUseLocalSpace = false;
            Plan.Required->bKillOnDeactivate = false;
            Plan.Required->EmitterLoops = 0;
            Plan.Required->EmitterDuration = 1.f;
            Plan.Required->EmitterDurationLow = 1.f;
            Plan.Required->bEmitterDurationUseRange = false;
            ChangedModules.Add(Plan.Required);
        }
        if (!ChangedModules.Contains(Plan.Spawn))
        {
            Plan.Spawn->Modify();
            UDistributionFloatConstant* Rate = NewObject<UDistributionFloatConstant>(Plan.Spawn, NAME_None, RF_Transactional);
            Rate->Constant = 10.f;
            Rate->bIsDirty = true;
            Plan.RateRaw->Distribution = Rate;
            Plan.RateRaw->Initialize(); // 脏标记使 UE 清除旧 Raw 查找表，再以新 Constant 烘焙。
            FParticleBurst Burst;
            Burst.Time = 0.f;
            Burst.Count = 1;
            Burst.CountLow = 1;
            Plan.Spawn->BurstList.Reset();
            Plan.Spawn->BurstList.Add(Burst);
            ChangedModules.Add(Plan.Spawn);
        }
        if (!ChangedModules.Contains(Plan.Lifetime))
        {
            Plan.Lifetime->Modify();
            UDistributionFloatUniform* Lifetime = NewObject<UDistributionFloatUniform>(Plan.Lifetime, NAME_None, RF_Transactional);
            Lifetime->Min = 1.2f;
            Lifetime->Max = 1.8f;
            Lifetime->bIsDirty = true;
            Plan.LifetimeRaw->Distribution = Lifetime;
            Plan.LifetimeRaw->Initialize();
            ChangedModules.Add(Plan.Lifetime);
        }
    }
    for (UParticleModule* Module : ChangedModules) Module->PostEditChange();
    Target->PostEditChange();
    Target->MarkPackageDirty();

    TArray<TSharedPtr<FJsonValue>> After, SourceAfter;
    for (const FPourSteamLODPlan& Plan : Plans)
    {
        After.Add(MakeShared<FJsonValueObject>(SnapshotLOD(Plan.LOD, Plan.Modules)));
        SourceAfter.Add(MakeShared<FJsonValueObject>(SnapshotLOD(Plan.SourceLOD, Plan.SourceModules)));
    }
    const TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
    Report->SetBoolField(TEXT("ok"), true);
    Report->SetStringField(TEXT("operation"), TEXT("TunePourSteam"));
    Report->SetStringField(TEXT("sourcePath"), Source->GetPathName());
    Report->SetStringField(TEXT("objectPath"), Target->GetPathName());
    Report->SetStringField(TEXT("packagePath"), Target->GetOutermost()->GetName());
    Report->SetStringField(TEXT("ownedObjectPrefix"), OwnedObjectPrefix);
    Report->SetStringField(TEXT("emitterClass"), Emitters[0]->GetClass()->GetPathName());
    Report->SetStringField(TEXT("emitterPath"), Emitters[0]->GetPathName());
    Report->SetBoolField(TEXT("allLODsValidatedBeforeWrite"), true);
    Report->SetBoolField(TEXT("allModulesAndRawObjectsOwnedByTarget"), true);
    Report->SetNumberField(TEXT("lodCount"), Plans.Num());
    Report->SetNumberField(TEXT("changedUniqueModuleCount"), ChangedModules.Num());
    Report->SetStringField(TEXT("ratePropertyPath"), RateProperty->GetPathName());
    Report->SetStringField(TEXT("lifetimePropertyPath"), LifetimeProperty->GetPathName());
    Report->SetArrayField(TEXT("before"), Before);
    Report->SetArrayField(TEXT("after"), After);
    Report->SetArrayField(TEXT("sourceBefore"), SourceBefore);
    Report->SetArrayField(TEXT("sourceAfter"), SourceAfter);
    Report->SetBoolField(TEXT("sourcePackageDirtyBefore"), bSourceDirtyBefore);
    Report->SetBoolField(TEXT("sourcePackageDirtyAfter"), Source->GetOutermost()->IsDirty());
    Report->SetBoolField(TEXT("packageDirtyBefore"), bTargetDirtyBefore);
    Report->SetBoolField(TEXT("packageDirty"), Target->GetOutermost()->IsDirty());
    Report->SetBoolField(TEXT("savedByCpp"), false);
    Report->SetBoolField(TEXT("liveSimulationMeasured"), false);
    Report->SetStringField(TEXT("duplicationBasis"), TEXT("调用方须先 E.duplicate 原 P_Steam_Lit；这里只核验路径、原源结构/材质及副本子对象归属，不能追溯编辑器的复制历史；不创建或克隆模块，不修改源子对象或共享材质"));
    Report->SetStringField(TEXT("preserved"), TEXT("Velocity、Size/Shape、尺寸寿命曲线、材质及 Alpha 曲线、RateScale/BurstScale 和全局 SpawnRateScale 开关均保持；只替换副本 Rate/Lifetime 分布及请求的 Required/Burst 字段"));
    Report->SetStringField(TEXT("burstSemantics"), TEXT("单条 Time=0 Count=1 CountLow=1；每个 1 秒无限循环都会触发，不是仅首次；启动需等组件首次粒子 Tick，实际生成仍受全局倍率/质量和启用状态影响"));
    Report->SetStringField(TEXT("particleBudgetBasis"), TEXT("单位倍率下连续部分约 10*1.8=18 颗，1 秒循环 Burst 在最长寿命窗口可再叠加约 2 颗；约 20 颗不是硬上限，帧离散/全局倍率/模块可改变实际数量，未运行模拟"));
    const TSharedRef<FJsonObject> Next = MakeShared<FJsonObject>();
    Next->SetStringField(TEXT("SteamSystem"), Target->GetPathName());
    Next->SetNumberField(TEXT("SteamScale"), .6);
    Next->SetNumberField(TEXT("ImpactHoldSeconds"), .35);
    Next->SetBoolField(TEXT("appliedByCpp"), false);
    Next->SetStringField(TEXT("instruction"), TEXT("主线程随后将 BP_HandPourKettle 的蒸汽 SteamSystem/SteamScale/ImpactHoldSeconds 设为上述值并保存；验证实际 PSC.GetTemplate() 指向副本、仍为单 PSC；C++ 不改蓝图或组件"));
    Next->SetStringField(TEXT("expectedSizeBasis"), TEXT("保留原 20–25cm 尺寸和约 .45–.75 寿命尺寸曲线，仅组件 scale=.6；起始约 6cm、尾部约 9cm 是推算，不是已运行视觉实测"));
    Report->SetObjectField(TEXT("nextRecommended"), Next);
    Report->SetStringField(TEXT("saveInstruction"), TEXT("由调用方 unreal.EditorAssetLibrary.save_loaded_asset(Target) 显式保存副本；不保存原 P_Steam_Lit、材质或任何 SoundWave，不写文件缓存"));
    return ToJson(Report);
#else
    return TEXT("{\"ok\":false,\"error\":\"WITH_EDITOR 不可用\"}");
#endif
}

FString UCoffeeFeedbackEditorLibrary::CreateGrindLoopCue(const FString& PackagePath)
{
#if WITH_EDITOR
    using namespace CoffeeFeedbackEditorPrivate;
    if (!GIsEditor || !IsInGameThread()) return FeedbackFailureJson(TEXT("仅可在编辑器游戏线程显式创建素材"));
    if (!PackagePath.StartsWith(OwnedCuePrefix, ESearchCase::CaseSensitive) || !FPackageName::IsValidLongPackageName(PackagePath))
        return FeedbackFailureJson(TEXT("仅允许 /Game/VRTemplate/CoffeeBrew/Audio/SC_Grind_Feedback_ 前缀的完整 package 路径，不接受文件路径或对象路径"));
    const FString Suffix = PackagePath.Mid(FCString::Strlen(OwnedCuePrefix));
    if (Suffix.IsEmpty()) return FeedbackFailureJson(TEXT("反馈 Cue 名称后缀不能为空"));
    for (const TCHAR Character : Suffix)
        if (!((Character >= 'a' && Character <= 'z') || (Character >= 'A' && Character <= 'Z')
            || (Character >= '0' && Character <= '9') || Character == '_'))
            return FeedbackFailureJson(TEXT("名称后缀仅允许 ASCII 字母、数字和下划线，禁止子目录或路径绕过"));

    USoundWave* Wave = LoadObject<USoundWave>(nullptr, GrindPath);
    if (!Wave || Wave->GetPathName() != GrindPath) return FeedbackFailureJson(TEXT("无法解析原 S_Grind SoundWave，拒绝创建替代音源"));
    const FString AssetName = FPackageName::GetLongPackageAssetName(PackagePath);
    const FString ObjectPath = PackagePath + TEXT(".") + AssetName;
    UObject* Existing = FindObject<UObject>(nullptr, *ObjectPath);
    const bool bDiskPackageExists = FPackageName::DoesPackageExist(PackagePath);
    if (!Existing && bDiskPackageExists) Existing = LoadObject<UObject>(nullptr, *ObjectPath);
    USoundCue* Cue = Cast<USoundCue>(Existing);
    bool bReused = false;
    FString AttenuationBasis;
    if (Existing)
    {
        const USoundNodeWavePlayer* Player = Cue ? Cast<USoundNodeWavePlayer>(Cue->FirstNode.Get()) : nullptr;
        if (!Cue || Cue->GetPathName() != ObjectPath || !Player || Player->GetOuter() != Cue
            || !Player->bLooping || Player->GetSoundWave() != Wave || !Player->ChildNodes.IsEmpty()
            || Cue->AllNodes.Num() != 1 || Cue->AllNodes[0] != Player)
            return FeedbackFailureJson(TEXT("目标路径已有非匹配素材；不会替换、修复或修改它"));
        bReused = true;
        AttenuationBasis = TEXT("匹配已有 Cue 只读复用；保留现有音量、Class 和衰减，不更新任何属性");
    }
    else
    {
        if (bDiskPackageExists || FindPackage(nullptr, *PackagePath))
            return FeedbackFailureJson(TEXT("目标 package 已存在但无匹配对象；拒绝覆盖或向既有 package 写入"));
        IAssetRegistry* Registry = IAssetRegistry::Get();
        if (!Registry) return FeedbackFailureJson(TEXT("编辑器 AssetRegistry 不可用，未创建 package"));
        const UClass* GrinderClass = LoadClass<ACoffeeGrinderActor>(nullptr, GrinderClassPath);
        const ACoffeeGrinderActor* GrinderDefaults = GrinderClass ? GrinderClass->GetDefaultObject<ACoffeeGrinderActor>() : nullptr;
        const UAudioComponent* SFX = GrinderDefaults ? GrinderDefaults->GrindSFX.Get() : nullptr;
        UPackage* Package = CreatePackage(*PackagePath);
        Cue = NewObject<USoundCue>(Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
        // UE 编辑器自身会建立 Cue 的编辑图；无需蓝图、图编译或另建 Graph 素材。
        USoundNodeWavePlayer* Player = Cue->ConstructSoundNode<USoundNodeWavePlayer>(USoundNodeWavePlayer::StaticClass(), false);
        Player->SetSoundWave(Wave);
        Player->bLooping = true;
        Cue->FirstNode = Player;
        Cue->VolumeMultiplier = .65f;
        Cue->PitchMultiplier = 1.f;
        Cue->SoundClassObject = Wave->SoundClassObject;
        Cue->bOverrideAttenuation = false;
        Cue->AttenuationSettings = nullptr;
        if (SFX)
        {
            if (SFX->bOverrideAttenuation)
            {
                Cue->bOverrideAttenuation = true;
                Cue->AttenuationOverrides = SFX->AttenuationOverrides;
                AttenuationBasis = TEXT("只读 BP_CoffeeGrinder CDO.GrindSFX 启用的 AttenuationOverrides，并仅复制到新 Cue");
            }
            else
            {
                Cue->AttenuationSettings = SFX->AttenuationSettings;
                AttenuationBasis = TEXT("只读 BP_CoffeeGrinder CDO.GrindSFX 的 AttenuationSettings 资产硬引用；不读取未启用的 Overrides");
            }
        }
        else AttenuationBasis = TEXT("无法解析 BP_CoffeeGrinder CDO.GrindSFX；新 Cue 无衰减覆盖，既有播放组件仍保留自己的距离设置");
        Cue->LinkGraphNodesFromSoundNodes();
        Cue->SetFlags(RF_Public | RF_Standalone);
        Registry->AssetCreated(Cue);
        Package->MarkPackageDirty();
    }
    const TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
    Report->SetBoolField(TEXT("ok"), true);
    Report->SetBoolField(TEXT("reused"), bReused);
    Report->SetStringField(TEXT("packagePath"), PackagePath);
    Report->SetStringField(TEXT("objectPath"), Cue->GetPathName());
    Report->SetBoolField(TEXT("packageDirty"), Cue->GetOutermost()->IsDirty());
    Report->SetBoolField(TEXT("savedByCpp"), false);
    Report->SetStringField(TEXT("saveInstruction"), TEXT("由调用方 Python 对 objectPath 调用 unreal.EditorAssetLibrary.save_loaded_asset；C++ 不写磁盘、不保存其他素材"));
    Number(Report, TEXT("cueVolumeMultiplier"), Cue->VolumeMultiplier);
    Report->SetStringField(TEXT("cueSoundClass"), GetPathNameSafe(Cue->SoundClassObject.Get()));
    Report->SetBoolField(TEXT("wavePlayerLooping"), true);
    Report->SetObjectField(TEXT("sourceWave"), WaveReport(Wave));
    Report->SetBoolField(TEXT("overrideAttenuation"), Cue->bOverrideAttenuation);
    Report->SetStringField(TEXT("attenuationAssetPath"), GetPathNameSafe(Cue->AttenuationSettings.Get()));
    Report->SetStringField(TEXT("attenuationBasis"), AttenuationBasis);
    Report->SetStringField(TEXT("cookReferenceBasis"), TEXT("FirstNode 原生 WavePlayer.SetSoundWave 同时设置软引用及已加载 SoundWave；UE5.6 SoundNodeWavePlayer::Serialize 根据 HardSoundReferences 序列化硬引用，ShouldHardReferenceAsset 默认为 true。Cue 自身仍须由调用方保存并显式接入待 Cook 内容。"));
    Report->SetStringField(TEXT("limitations"), TEXT("未播放、未验证无缝循环接缝、未验证 Cook；不修改 S_Grind/actor/GrindSFX/输入/全局音频开关。衰减读取蓝图默认组件，不代表关卡实例覆盖；未自动赋值 LoadedGrindSound，不强制指定 Music Class。"));
    return ToJson(Report);
#else
    return TEXT("{\"ok\":false,\"error\":\"WITH_EDITOR 不可用\"}");
#endif
}
