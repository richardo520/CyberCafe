// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "InputCoreTypes.h"
#include "GrabComponent.generated.h"

class UMotionControllerComponent;
class UPrimitiveComponent;
class USkeletalMeshComponent;
class UHapticFeedbackEffect_Base;
class UAnimInstance;

/**
 * 抓取类型枚举
 * 对应UE官方VR模板中的EGrabType
 */
UENUM(BlueprintType)
enum class EGrabType : uint8
{
    /** 不可抓取 */
    None            UMETA(DisplayName = "None"),
    /** 自由抓取：物体保持相对手部的当前偏移 */
    Free            UMETA(DisplayName = "Free"),
    /** 吸附抓取：物体被吸附到手部的锚点位置 */
    Snap            UMETA(DisplayName = "Snap"),
    /** 自定义抓取：由外部逻辑处理抓取行为 */
    Custom          UMETA(DisplayName = "Custom"),
    /** 原地吸附：物体保持在世界中的位置不变，仅登记为已抓取（如武器扳机） */
    SnapInPlace     UMETA(DisplayName = "SnapInPlace")
};

/** 抓取事件委托（无参数） */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnGrabbedSignature);
/** 释放事件委托（无参数） */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDroppedSignature);


/**
 * UGrabComponent
 * 附加到可抓取Actor上的场景组件，用于处理VR手柄抓取交互。
 *
 * 本组件在官方VR模板GrabComponent的基础上，额外扩展了以下能力（与工程内蓝图版保持一致）：
 *   1) 拉拽（Pull）：TryPull / StopPull，用于抽出、拉动类交互
 *   2) 手部Mesh接管：TryCaptureHandMesh / TryReleaseHandMesh，用于抓握时替换/隐藏手部
 *   3) 双手（主/副手）抓取：PrimaryGrabComponent 记录当前主抓取，副手抓取时相对主抓取旋转
 *   4) 左右手判定：GetHeldByHand 返回EControllerHand
 *   5) 触觉反馈：抓取时播放OnGrabHapticEffect
 *   6) 便捷Setter：SetShouldSimulateOnDrop / SetPrimitiveCompPhysics / SetSimulatingParent
 *
 * 使用方法：将该组件添加到需要被抓取的Actor上，并确保Actor的RootComponent是一个可模拟物理的Primitive组件。
 */
UCLASS(ClassGroup = (VR), meta = (BlueprintSpawnableComponent, DisplayName = "Grab Component"))
class CYBERCAFE_API UGrabComponent : public USceneComponent
{
    GENERATED_BODY()

public:
    UGrabComponent();

protected:
    virtual void BeginPlay() override;

public:
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    //~ Begin Grab API

    /**
     * 尝试抓取本组件所属的物体。
     * @param MotionController 发起抓取的手柄组件
     * @return 如果成功抓取则返回true
     */
    UFUNCTION(BlueprintCallable, Category = "Grab")
    bool TryGrab(UMotionControllerComponent* MotionController);

    /**
     * 尝试释放本组件。
     * @return 如果成功释放则返回true
     */
    UFUNCTION(BlueprintCallable, Category = "Grab")
    bool TryRelease();

    /** 当前是否已被抓取 */
    UFUNCTION(BlueprintPure, Category = "Grab")
    bool IsHeld() const { return bIsHeld; }

    /** 当前是否处于拉拽状态 */
    UFUNCTION(BlueprintPure, Category = "Grab|Pull")
    bool IsPulled() const { return bIsPulled; }

    /** 获取当前持有该物体的手柄，未持有则返回nullptr */
    UFUNCTION(BlueprintPure, Category = "Grab")
    UMotionControllerComponent* GetHoldingController() const { return MotionControllerRef; }

    /**
     * 获取当前持有该物体的手枚举。
     * @param bSuccess 是否成功找到持有手
     * @return 左手/右手
     */
    UFUNCTION(BlueprintPure, Category = "Grab")
    EControllerHand GetHeldByHand() const;

    //~ End Grab API

    //~ Begin Pull API (工程内新增：拉拽交互)

    /**
     * 尝试拉拽本组件所属的物体（如抽剑、拉杆等）。
     * @param MotionController 发起拉拽的手柄组件
     * @return 是否成功进入拉拽状态
     */
    UFUNCTION(BlueprintCallable, Category = "Grab|Pull")
    bool TryPull();

    /** 结束拉拽 */
    UFUNCTION(BlueprintCallable, Category = "Grab|Pull")
    void StopPull();

    //~ End Pull API

    //~ Begin Hand Mesh API (工程内新增：抓取时接管手部Mesh)

    /**
     * 在指定的MotionController下寻找骨骼手Mesh。
     * @param MotionController 手柄组件
     * @param Mesh 输出的手部SkeletalMeshComponent
     * @return 是否找到
     */
    UFUNCTION(BlueprintCallable, Category = "Grab|HandMesh")
    bool TryFindHandMeshOnController() ;

    /** 抓取时接管/替换手部Mesh动画层（HandAnimLayer） */
    UFUNCTION(BlueprintCallable, Category = "Grab|HandMesh")
    bool TryCaptureHandMesh();

    /** 释放时还原手部Mesh动画层 */
    UFUNCTION(BlueprintCallable, Category = "Grab|HandMesh")
    void TryReleaseHandMesh();

    //~ End Hand Mesh API

    //~ Begin Attach / Physics Helpers

    /** 将父Actor整体附加到MotionController（用于组合物体抓取时同步父级） */
    UFUNCTION(BlueprintCallable, Category = "Grab|Attach")
    void AttachParentToMotionController(UMotionControllerComponent* MotionController);

    /** 对所属Actor的Primitive组件统一设置物理模拟状态 */
    UFUNCTION(BlueprintCallable, Category = "Grab|Physics")
    void SetPrimitiveCompPhysics(bool bSimulate);

    /** 设置父/祖组件是否模拟物理（对复合物体有用） */
    UFUNCTION(BlueprintCallable, Category = "Grab|Physics")
    void SetSimulatingParent(bool bSimulate);

    UFUNCTION(BlueprintCallable, Category = "Grab|Physics")
    void TrySimulateOnDrop();

    //~ End Attach / Physics Helpers

    //~ Begin Throw (投掷手感)

    /**
     * 释放时把手部速度传给物体的放大系数。
     * VR 中真实速度直接给玩家常觉得"太轻飘"，1.1~1.3 通常手感更跟手。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grab|Throw", meta = (ClampMin = "0.0"))
    float ThrowVelocityScale = 1.1f;

    /** 释放时角速度的放大系数 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grab|Throw", meta = (ClampMin = "0.0"))
    float ThrowAngularVelocityScale = 1.0f;

    //~ End Throw

public:
    /** 抓取类型 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grab")
    EGrabType GrabType;
    
    /** 抓取组件优先级*/
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grab|Hand")
    int32 GrabPriority;

    /** 抓握时手部使用的Socket（HandSocket）名称 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grab|Hand")
    FName HandSocket;

    /** 抓握时挂载到手部动画的动画层类 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grab|Hand")
    TSubclassOf<UAnimInstance> HandAnimLayer;

    /** 是否在抓握时接管/替换手部Mesh */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grab|Hand")
    bool bCaptureHand;

    /** 抓取时播放的触觉效果 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grab|Haptics")
    TObjectPtr<UHapticFeedbackEffect_Base> OnGrabHapticEffect;

    /** 被抓取时触发 */
    UPROPERTY(BlueprintAssignable, Category = "Grab|Events")
    FOnGrabbedSignature OnGrabbed;

    /** 被释放时触发 */
    UPROPERTY(BlueprintAssignable, Category = "Grab|Events")
    FOnDroppedSignature OnDropped;

    /**
     * 当前正在持有本物体的手柄引用（对应蓝图变量 MotionControllerRef）。
     * Transient，运行时状态，不参与序列化。
     */
    UPROPERTY(BlueprintReadOnly, Transient, Category = "Grab|Runtime")
    TObjectPtr<UMotionControllerComponent> MotionControllerRef;

    /**
     * 双手抓取时的"主抓取组件"引用。若本组件为副手抓取，此指针指向已存在的主抓取组件。
     * 对应蓝图变量 PrimaryGrabComponent。
     */
    UPROPERTY(BlueprintReadWrite, Transient, Category = "Grab|Runtime")
    TObjectPtr<UGrabComponent> PrimaryGrabComponent;

    /** 双手抓取时副手相对主抓取的相对旋转，对应蓝图变量 PrimaryGrabRelativeRotation */
    UPROPERTY(BlueprintReadWrite, Transient, Category = "Grab|Runtime")
    FRotator PrimaryGrabRelativeRotation;

    /**
     * 抓取前对Actor物理模拟状态的缓存，用于释放时恢复。
     * 对应蓝图变量 bSimulateOnDrop。
     */
    UPROPERTY(BlueprintReadWrite, Transient, Category = "Grab|Runtime")
    bool bSimulateOnDrop;

    /** 拉拽（Pull）过程中缓存的手部初始变换，对应蓝图变量 CachedHandLocationTransform */
    UPROPERTY(BlueprintReadWrite, Transient, Category = "Grab|Runtime")
    FTransform CachedHandLocationTransform;
    
    UPROPERTY(BlueprintReadWrite, Transient, Category = "Grab|Runtime")
    USkeletalMeshComponent* HandMesh = nullptr;    

private:
    /** 当前是否处于被持有状态（对应蓝图变量 bIsHeld） */
    UPROPERTY(Transient)
    bool bIsHeld;

    /** 当前是否处于拉拽状态（对应蓝图变量 bIsPulled / bPulled） */
    UPROPERTY(Transient)
    bool bIsPulled;

    /** 抓取动作真正执行的内部方法（对应蓝图函数 PerformGrab） */
    void PerformGrab(UMotionControllerComponent* MotionController);

    /** 获取所属Actor上的可模拟物理的Primitive组件（通常是RootComponent） */
    UPrimitiveComponent* GetOwnerPrimitive() const;

    //~ Begin Throw Sampling
    /** 上一帧手柄（MotionControllerRef）的世界位置，用于估算释放瞬间的线速度 */
    FVector LastControllerLocation = FVector::ZeroVector;

    /** 上一帧手柄的世界旋转，用于估算释放瞬间的角速度 */
    FQuat LastControllerRotation = FQuat::Identity;

    /** 上一次采样对应的 DeltaTime */
    float LastSampleDeltaTime = 0.f;

    /** 是否已经拿到过至少一帧有效采样（避免用初值算出巨大速度） */
    bool bHasValidSample = false;
    //~ End Throw Sampling
};
