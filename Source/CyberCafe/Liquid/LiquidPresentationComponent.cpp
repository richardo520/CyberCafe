#include "Liquid/LiquidPresentationComponent.h"
// Fill out your copyright notice in the Description page of Project Settings.

// @trace LQ-01 LQ-02 LQ-05
#include "Liquid/LiquidContainerActor.h"
#include "Liquid/LiquidTransferSubsystem.h"
#include "Liquid/GlassBreakableComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundAttenuation.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MaterialTypes.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraTypes.h"
#include "NiagaraVariant.h"
#include "NiagaraDataInterfaceExport.h"
#include "GrabComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "CollisionQueryParams.h"
#include "Core/Events/CyberCafeEventSubsystem.h"


void ULiquidPresentationComponent::RefreshLiquidFX()
{
    auto& Vessel = *CastChecked<ALiquidContainerActor>(GetOwner());
    if (Vessel.bBroken)
    {
        Vessel.FillAmount = 0.f;
        if (Vessel.LiquidFX)
        {
            Vessel.LiquidFX->SetNiagaraVariableFloat(TEXT("User.Fill"), 0.f);
            Vessel.LiquidFX->DeactivateImmediate();
            Vessel.LiquidFX->SetVisibility(false);
        }
        return;
    }
    if (Vessel.Inventory->IsInitialized())
        Vessel.FillAmount = Vessel.MaxVolumeML > 0.f ? static_cast<float>(FMath::Clamp(Vessel.Inventory->GetVolumeML() / Vessel.MaxVolumeML, 0.0, 1.0)) : 0.f;
    if (!Vessel.LiquidFX)
    {
        return;
    }

    // 容器空了则隐藏整个液体网格：
    // P_Liquid 内部即使 Fill=0 也会保留一个"最小可见层"，
    // 且普通 Deactivate 只停止 Spawn、已有粒子会走完 lifetime 才消失（视觉上像"倒不干净"）。
    // 因此这里：
    //   1) 先把 User.Fill = 0 写下去，防止残留渲染时还用旧值
    //   2) 用 DeactivateImmediate 立即清除所有存活粒子，让瓶底那点液面瞬间消失
    if (Vessel.FillAmount <= KINDA_SMALL_NUMBER)
    {
        Vessel.LiquidFX->SetNiagaraVariableFloat(TEXT("User.Fill"), 0.f);
        if (Vessel.LiquidFX->IsActive())
        {
            Vessel.LiquidFX->DeactivateImmediate();
        }
        return;
    }

    // 从空变非空：重新启动 LiquidFX（bReset=true 确保 Niagara 用当前 User 参数重新生成液面）
    if (!Vessel.LiquidFX->IsActive())
    {
        Vessel.LiquidFX->Activate(/*bReset=*/true);
    }

    // 只写入 P_Liquid 定义的 User 参数。
    // 颜色不再通过 Niagara 参数或整块材质替换来切，而是通过 LiquidMID 的
    // Liquid_Color01 向量参数实时写入——这样 AddLiquid 的加权混色结果会自然
    // 反映在杯子液体上，避免"整帧突变"造成的僵硬感。
    Vessel.LiquidFX->SetNiagaraVariableFloat(TEXT("User.Fill"),       Vessel.FillAmount);
    Vessel.LiquidFX->SetNiagaraVariableFloat(TEXT("User.Opacity"),    Vessel.LiquidOpacity);
    Vessel.LiquidFX->SetNiagaraVariableFloat(TEXT("User.AddWaves"),   Vessel.AddWaves);
    Vessel.LiquidFX->SetNiagaraVariableFloat(TEXT("User.WavesScale"), Vessel.WavesScale);
    Vessel.LiquidFX->SetNiagaraVariableFloat(TEXT("User.Viscosity"),  Vessel.Viscosity);

    // 把当前混色结果同步到 MID，驱动液体材质颜色平滑过渡。
    ApplyLiquidColorToMID();
}

void ULiquidPresentationComponent::UpdateDynamicWaves(float DeltaTime)
{
    auto& Vessel = *CastChecked<ALiquidContainerActor>(GetOwner());
    if (!Vessel.LiquidFX)
    {
        return;
    }

    // 关闭动态波动时，直接使用静态 AddWaves
    if (!Vessel.bDynamicWaves)
    {
        Vessel.LiquidFX->SetNiagaraVariableFloat(TEXT("User.AddWaves"), Vessel.AddWaves);
        return;
    }

    // 1) 采集容器当前的线速度与角速度
    float LinearSpeed  = 0.f;
    float AngularSpeed = 0.f;
    if (Vessel.ContainerMesh)
    {
        // 优先用物理速度（仅模拟物理时有效）
        LinearSpeed = Vessel.ContainerMesh->GetPhysicsLinearVelocity().Size();
        AngularSpeed = FMath::RadiansToDegrees(Vessel.ContainerMesh->GetPhysicsAngularVelocityInRadians().Size());

        // 若没上物理（比如被 Snap 到手上），退化用位移差分估算
        if (LinearSpeed < KINDA_SMALL_NUMBER && AngularSpeed < KINDA_SMALL_NUMBER && DeltaTime > KINDA_SMALL_NUMBER)
        {
            const FVector CurLoc = Vessel.ContainerMesh->GetComponentLocation();
            LinearSpeed = ((CurLoc - PrevLocation) / DeltaTime).Size();
            PrevLocation = CurLoc;

            const FQuat CurRot = Vessel.ContainerMesh->GetComponentQuat();
            const FQuat DeltaQuat = CurRot * PrevRotation.Inverse();
            FVector Axis; float AngleRad = 0.f;
            DeltaQuat.ToAxisAndAngle(Axis, AngleRad);
            AngleRad = FMath::UnwindRadians(AngleRad);
            AngularSpeed = FMath::RadiansToDegrees(FMath::Abs(AngleRad) / DeltaTime);
            PrevRotation = CurRot;
        }
        else
        {
            PrevLocation = Vessel.ContainerMesh->GetComponentLocation();
            PrevRotation = Vessel.ContainerMesh->GetComponentQuat();
        }
    }

    // 2) 将速度归一化到 0~1，取两者最大作为"运动强度"
    const float LinearNorm  = FMath::Clamp(LinearSpeed  / FMath::Max(Vessel.LinearVelocityRefCms,  1.f), 0.f, 1.f);
    const float AngularNorm = FMath::Clamp(AngularSpeed / FMath::Max(Vessel.AngularVelocityRefDegs, 1.f), 0.f, 1.f);
    const float MotionNorm  = FMath::Max(LinearNorm, AngularNorm);

    // 3) 目标波动强度：在 [IdleAddWaves, MaxAddWaves] 之间插值
    const float TargetWaves = FMath::Lerp(Vessel.IdleAddWaves, Vessel.MaxAddWaves, MotionNorm);

    // 4) 平滑：上升时快、下降时慢，避免抖动
    const bool bRising = TargetWaves > CurrentDynamicWaves;
    const float Speed  = bRising ? Vessel.WavesRiseSpeed : Vessel.WavesDecaySpeed;
    const float Alpha  = FMath::Clamp(Speed * DeltaTime, 0.f, 1.f);
    CurrentDynamicWaves = FMath::Lerp(CurrentDynamicWaves, TargetWaves, Alpha);

    // 5) 写入 P_Liquid
    Vessel.LiquidFX->SetNiagaraVariableFloat(TEXT("User.AddWaves"), CurrentDynamicWaves);
}

void ULiquidPresentationComponent::InitLiquidFX()
{
    auto& Vessel = *CastChecked<ALiquidContainerActor>(GetOwner());
    if (Vessel.bBroken || !Vessel.LiquidFX)
    {
        return;
    }

    // 1) 附上 Niagara 模板（P_Liquid）
    if (Vessel.LiquidFXTemplate && Vessel.LiquidFX->GetAsset() != Vessel.LiquidFXTemplate)
    {
        Vessel.LiquidFX->SetAsset(Vessel.LiquidFXTemplate);
    }

    // 注意：User.Mesh / User.Material / User.BottleSize 不再在此处写入，
    // 美术直接在蓝图的 LiquidFX 组件 Details 面板上配置即可（可在蓝图编辑器实时预览）。
    // C++ 仅在 SetLiquidMaterialAsset（倒液到杯子时）主动覆写 User.Material。

    // 2) 重启以确保 Niagara 与美术在组件上配置的 Override 正常生效
    Vessel.LiquidFX->ReinitializeSystem();
}

void ULiquidPresentationComponent::ResolveLiquidMaterialFromFX()
{
    auto& Vessel = *CastChecked<ALiquidContainerActor>(GetOwner());
    if (!Vessel.LiquidFX)
    {
        return;
    }

    // 从 Niagara 组件反查美术在 Details 面板上为 User.Material 指定的 Override。
    // FindParameterOverride 会查 InstanceParameterOverrides + TemplateParameterOverrides，
    // 覆盖美术在蓝图 LiquidFX 组件里配置的所有情形。
    // 使用精确的 GetUMaterialDef() 类型，与 P_Liquid 里 User.Material 的类型声明保持一致，
    // 也与写入端 SetVariableMaterial 使用的类型对齐。
    const FNiagaraVariableBase UserMaterialVar(FNiagaraTypeDefinition::GetUMaterialDef(), FName(TEXT("User.Material")));
#if WITH_EDITOR
    const FNiagaraVariant Variant = Vessel.LiquidFX->FindParameterOverride(UserMaterialVar);
    if (Variant.IsValid())
    {
        if (UMaterialInterface* Resolved = Cast<UMaterialInterface>(Variant.GetUObject()))
        {
            Vessel.LiquidMaterialAsset = Resolved;
        }
    }
#else
    // Cook serializes authored overrides into the runtime store. UE5.6's
    // FindParameterOverride is editor-only; invalid overrides keep the cached material.
    const FNiagaraVariable RuntimeMaterialVar(FNiagaraTypeDefinition::GetUMaterialDef(), FName(TEXT("User.Material")));
    const FNiagaraParameterStore& RuntimeOverrides = Vessel.LiquidFX->GetOverrideParameters();
    if (UMaterialInterface* Resolved = Cast<UMaterialInterface>(RuntimeOverrides.GetUObject(RuntimeMaterialVar).Get()))
    {
        Vessel.LiquidMaterialAsset = Resolved;
    }
#endif
}

void ULiquidPresentationComponent::InitPourFX()
{
    auto& Vessel = *CastChecked<ALiquidContainerActor>(GetOwner());
    if (!Vessel.PourFX)
    {
        return;
    }

    // 安全检查：提醒美术在蓝图 PourFX 组件的 Niagara Asset 一栏指定 P_Ribbon
    if (!Vessel.PourFX->GetAsset())
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[%s] PourFX 未指定 Niagara Asset，请在蓝图选中 PourFX 组件，Details → Niagara → Asset 里指定 P_Ribbon。"),
            *GetName());
    }

    // 预写入静态参数
    Vessel.PourFX->SetNiagaraVariableLinearColor(TEXT("User.Color"),        Vessel.LiquidColor);
    Vessel.PourFX->SetNiagaraVariableFloat      (TEXT("User.FlowStrength"), Vessel.FlowStrength);
    Vessel.PourFX->SetNiagaraVariableBool       (TEXT("User.NoSplashes"),   Vessel.bNoSplashes);
    Vessel.PourFX->SetNiagaraVariableBool       (TEXT("User.NoList"),       Vessel.bNoList);
    Vessel.PourFX->SetNiagaraVariableBool       (TEXT("User.Decal"),        Vessel.bEnableDecal);

    // 将本 Actor 注册为 P_Ribbon 里“Export Particle Data to Blueprint”模块的
    // CallbackHandler——仅保留旧资产接口兼容，不再由粒子决定液量；数据仍会回调
    // 到 ReceiveParticleData_Implementation。
    // 注意：不同的 Niagara 资产可能把 Handler Parameter 绑定到不同名字上，
    // 这里一次写入三个最常见命名。Niagara 对不存在的 User 参数会静默忽略，
    // 不会报错，也不会重复回调（回调取决于 Export DI 实际绑定的那个）。
    Vessel.PourFX->SetNiagaraVariableObject(TEXT("User.Data"),            &Vessel);
    Vessel.PourFX->SetNiagaraVariableObject(TEXT("User.Handler"),         &Vessel);
    Vessel.PourFX->SetNiagaraVariableObject(TEXT("User.CallbackHandler"), &Vessel);

    if (Vessel.bUseRibbonTrajectory && Vessel.PourFX->GetAsset() && Vessel.PourFX->GetAsset()->GetFName() == TEXT("P_Ribbon"))
    {
        Vessel.PourFX->SetNiagaraVariableFloat(TEXT("User.Velocity"), Vessel.PourRibbonVelocity);
        Vessel.PourFX->SetNiagaraVariableFloat(TEXT("User.Offset"), Vessel.PourRibbonOffset);
    }
    // 默认关闭，要倒液时才 Activate
    Vessel.PourFX->Deactivate();
}

void ULiquidPresentationComponent::SetLiquidMaterialAsset(UMaterialInterface* NewMaterial, bool bReadColor)
{
    auto& Vessel = *CastChecked<ALiquidContainerActor>(GetOwner());
    if (Vessel.bBroken || !NewMaterial)
    {
        return;
    }

    // ==============================================================
    // 平滑过渡策略（方案 A：MID + 颜色参数插值）
    //
    // 现在杯子液体的外观由 LiquidMID + 其 Liquid_Color01 参数共同决定。
    // 大部分"倒酒进杯"场景下，我们不再整块替换材质，只依靠 AddLiquid 的
    // 体积加权混色更新 LiquidColor，然后由 RefreshLiquidFX → ApplyLiquidColorToMID
    // 把颜色平滑写进 MID，避免"第一颗粒子命中就整杯变色"的僵硬感。
    //
    // 两种情形分别处理：
    //   1) 目标杯子几乎为空（FillAmount ≈ 0）——这是"换酒"场景，允许整体切换：
    //      - 更新 LiquidMaterialAsset
    //      - 用新材质派生一个新 MID 并绑到 LiquidFX.User.Material
    //      - 从材质读色一次（bReadColor）
    //      - 由 RefreshLiquidFX 走一遍参数刷新（不做 Reinitialize，Fill=0 时它会
    //        DeactivateImmediate，等下一颗粒子加液再 Activate 自然重启）
    //   2) 目标杯子已经有液体——保持当前 MID 不动，只做"目标色引导"：
    //      - 从新材质读一次目标颜色，但只用来作为"下一次混色的另一头"，不覆盖当前混色
    //        （颜色实际过渡完全由 AddLiquid 的加权公式驱动）
    //      - 不替换 User.Material、不 Reinitialize，Niagara 不会闪一下
    //
    // 注：本函数依然可以被蓝图强行调用来"硬切材质"（例如清空杯子/道具重置），
    //     调用前把 FillAmount 置 0 即可走情形 1。
    // ==============================================================

    const bool bTargetEmpty = (Vessel.FillAmount <= KINDA_SMALL_NUMBER);

    if (bTargetEmpty)
    {
        // 情形 1：空杯换酒——允许整体切换材质
        Vessel.LiquidMaterialAsset = NewMaterial;

        if (bReadColor)
        {
            TryReadColorFromMaterial();
        }

        // 用新材质派生 MID 并绑定到 LiquidFX.User.Material
        Vessel.EnsureLiquidMID(/*bForceRecreate=*/true);
    }
    else
    {
        // 情形 2：杯里已经有液体——只更新"数据源材质"的引用（用于 PourFX/Splash 读色引导），
        //         不动 LiquidMID、不动 User.Material，颜色由 AddLiquid 混色 + RefreshLiquidFX 写 MID 自然过渡。
        Vessel.LiquidMaterialAsset = NewMaterial;
        // 注意：这里刻意不再调用 TryReadColorFromMaterial()——那会把当前混色结果冲掉，
        //       导致杯子颜色"瞬跳到源色"，正是我们要避免的僵硬感来源。
    }

    // 广播一次事件，便于蓝图侧联动（例如刷新 3D UI 上的酒名）
    Vessel.OnLiquidChanged.Broadcast(Vessel.FillAmount, Vessel.LiquidColor);
}

void ULiquidPresentationComponent::EnsureLiquidMID(bool bForceRecreate)
{
    auto& Vessel = *CastChecked<ALiquidContainerActor>(GetOwner());
    if (Vessel.bBroken || !Vessel.LiquidFX || !Vessel.LiquidMaterialAsset)
    {
        return;
    }

    // 已有 MID 且父级仍是当前 LiquidMaterialAsset —— 直接复用
    if (!bForceRecreate && Vessel.LiquidMID && Vessel.LiquidMID->Parent == Vessel.LiquidMaterialAsset)
    {
        return;
    }

    // 用当前 LiquidMaterialAsset 派生一个新 MID
    Vessel.LiquidMID = UMaterialInstanceDynamic::Create(Vessel.LiquidMaterialAsset, &Vessel);
    if (!Vessel.LiquidMID)
    {
        return;
    }

    // 立即把当前 LiquidColor 写进 MID，防止第一帧出现"MID 用父级默认色"的闪烁
    Vessel.LiquidMID->SetVectorParameterValue(Vessel.LiquidColorParamName, Vessel.LiquidColor);

    // 绑定到 P_Liquid.User.Material —— 使用与旧 SetLiquidMaterialAsset 一致的 SetVariableMaterial 路径，
    // 它会正确匹配 Niagara 里 User.Material 的类型声明，并触发 PSO 重编译。
    Vessel.LiquidFX->SetVariableMaterial(FName(TEXT("User.Material")), Vessel.LiquidMID);

    // 材质换了根，Niagara 需要重启一次让 Mesh Renderer 立刻用新 MID 渲染。
    // 只有在"派生了新 MID"这条路径上才需要重启——常规颜色渐变不再走这里。
    Vessel.LiquidFX->ReinitializeSystem();
}

void ULiquidPresentationComponent::ApplyLiquidColorToMID()
{
    auto& Vessel = *CastChecked<ALiquidContainerActor>(GetOwner());
    if (!Vessel.LiquidMID || Vessel.LiquidColorParamName.IsNone())
    {
        return;
    }

    Vessel.LiquidMID->SetVectorParameterValue(Vessel.LiquidColorParamName, Vessel.LiquidColor);
}

bool ULiquidPresentationComponent::TryReadColorFromMaterial()
{
    auto& Vessel = *CastChecked<ALiquidContainerActor>(GetOwner());
    if (!Vessel.LiquidMaterialAsset || Vessel.LiquidColorParamName.IsNone())
    {
        return false;
    }

    // UMaterialInterface::GetVectorParameterValue 对 MI / M 都能用；
    // 参数不存在时返回 false，不修改 OutValue。
    FLinearColor Out;
    if (Vessel.LiquidMaterialAsset->GetVectorParameterValue(FMaterialParameterInfo(Vessel.LiquidColorParamName), Out))
    {
        Vessel.LiquidColor = Out;
        Vessel.LiquidColor.A = 1.f;
        return true;
    }
    return false;
}

void ULiquidPresentationComponent::ResetMotionHistory()
{
    const auto* Vessel=Cast<ALiquidContainerActor>(GetOwner());
    if(Vessel&&Vessel->ContainerMesh)
    {
        PrevLocation=Vessel->ContainerMesh->GetComponentLocation();
        PrevRotation=Vessel->ContainerMesh->GetComponentQuat();
    }
}
