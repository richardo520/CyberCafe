// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "InputActionValue.h"
#include "VRPawn.generated.h"

class UCameraComponent;
class UStaticMeshComponent;
class USkeletalMeshComponent;
class UMotionControllerComponent;
class UWidgetInteractionComponent;
class UNiagaraComponent;
class UNiagaraSystem;
class UVRNotificationsComponent;
class UInputMappingContext;
class UInputAction;
class UGrabComponent;
class UUserWidget;
class AActor;

/**
 * AVRPawn
 * 对应工程内蓝图 /Game/VRTemplate/Blueprints/VRPawn 的C++版本。
 *
 * 提供VR基础交互：
 *   - 双手MotionController + 骨骼手模型
 *   - 抓取 / 拉拽 / 手部触觉反馈（通过UGrabComponent）
 *   - 抛物线传送 + 导航网格投影
 *   - 平滑转向 / 快速转向（Snap Turn）
 *   - VR UI 交互（WidgetInteraction）
 *   - 动画层曲线值：Grasp / IndexCurl / Point / ThumbUp
 */
UCLASS(Blueprintable, BlueprintType, config = Game)
class CYBERCAFE_API AVRPawn : public APawn
{
    GENERATED_BODY()

public:
    AVRPawn();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;
    virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

    //=====================================================================
    // 组件（对应蓝图 SimpleConstructionScript）
    //=====================================================================

    /** VR原点（Pawn根下的偏移节点） */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR|Components")
    TObjectPtr<USceneComponent> VROrigin;

    /** HMD相机 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR|Components")
    TObjectPtr<UCameraComponent> Camera;

    /** HMD头显Mesh（仅编辑器可见的通用HMD模型） */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR|Components")
    TObjectPtr<UStaticMeshComponent> HeadMountedDisplayMesh;

    /** 左手Grip MotionController（抓取用） */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR|Components")
    TObjectPtr<UMotionControllerComponent> MotionControllerLeftGrip;

    /** 右手Grip MotionController */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR|Components")
    TObjectPtr<UMotionControllerComponent> MotionControllerRightGrip;

    /** 左手Aim MotionController（瞄准/UI射线） */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR|Components")
    TObjectPtr<UMotionControllerComponent> MotionControllerLeftAim;

    /** 右手Aim MotionController */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR|Components")
    TObjectPtr<UMotionControllerComponent> MotionControllerRightAim;

    /** 左手骨骼Mesh（Mannequins XR） */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR|Components")
    TObjectPtr<USkeletalMeshComponent> HandLeft;

    /** 右手骨骼Mesh */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR|Components")
    TObjectPtr<USkeletalMeshComponent> HandRight;

    /** 左手Widget交互组件 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR|Components")
    TObjectPtr<UWidgetInteractionComponent> WidgetInteractionLeft;

    /** 右手Widget交互组件 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR|Components")
    TObjectPtr<UWidgetInteractionComponent> WidgetInteractionRight;

    /** Teleport Trace VFX Niagara组件 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR|Components")
    TObjectPtr<UNiagaraComponent> TeleportTraceNiagaraSystem;

    /** VR系统事件通知组件（挂载头显、恢复焦点等） */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR|Components")
    TObjectPtr<UVRNotificationsComponent> VRNotifications;

    //=====================================================================
    // Enhanced Input 配置（对应蓝图公开变量）
    //=====================================================================

    /** 主输入映射上下文（IMC_Default） */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VR|Input")
    TObjectPtr<UInputMappingContext> DefaultMappingContext;

    /** 手部动画输入映射上下文（IMC_Hands） */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VR|Input")
    TObjectPtr<UInputMappingContext> HandsMappingContext;

    // 移动 / 转向
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VR|Input|Actions") TObjectPtr<UInputAction> IA_Move;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VR|Input|Actions") TObjectPtr<UInputAction> IA_Turn;

    // 抓取
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VR|Input|Actions") TObjectPtr<UInputAction> IA_Grab_Left_Pressed;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VR|Input|Actions") TObjectPtr<UInputAction> IA_Grab_Left_Released;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VR|Input|Actions") TObjectPtr<UInputAction> IA_Grab_Right_Pressed;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VR|Input|Actions") TObjectPtr<UInputAction> IA_Grab_Right_Released;

    // 菜单 / 射击
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VR|Input|Actions") TObjectPtr<UInputAction> IA_Menu_Toggle_Left;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VR|Input|Actions") TObjectPtr<UInputAction> IA_Menu_Toggle_Right;
    
    //=====================================================================
    // 蓝图暴露的配置属性
    //=====================================================================

    /** 快速转向的角度（每次） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Locomotion")
    float SnapTurnDegrees;

    /** 抓取半径：以Grip位置为球心的球体检测半径 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Grab")
    float GrabRadiusFromGripPosition;

    /** 传送投射的初速度 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Teleport")
    float LocalTeleportLaunchSpeed;

    /** 传送投射的球体半径 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Teleport")
    float LocalTeleportProjectileRadius;

    /** 本地导航网格Cell高度（用于Y轴投影补偿） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Teleport")
    float LocalNavMeshCellHeight;

    /** 传送点投影到NavMesh时的查询范围 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Teleport")
    FVector TeleportProjectPointToNavigationQueryExtent;

    /** 传送可视化Actor类（VRTeleportVisualizer） */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "VR|Teleport")
    TSubclassOf<AActor> TeleportVisualizerClass;

    /** 菜单Widget类（Menu） */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "VR|Menu")
    TSubclassOf<UUserWidget> MenuClass;

    //=====================================================================
    // 运行时状态变量
    //=====================================================================

    /** 左手当前抓取的组件 */
    UPROPERTY(BlueprintReadOnly, Transient, Category = "VR|Runtime")
    TObjectPtr<UGrabComponent> HeldComponentLeft;

    /** 右手当前抓取的组件 */
    UPROPERTY(BlueprintReadOnly, Transient, Category = "VR|Runtime")
    TObjectPtr<UGrabComponent> HeldComponentRight;

    /** 左手当前拉拽的组件 */
    UPROPERTY(BlueprintReadOnly, Transient, Category = "VR|Runtime")
    TObjectPtr<UGrabComponent> PulledGrabComponentLeft;

    /** 右手当前拉拽的组件 */
    UPROPERTY(BlueprintReadOnly, Transient, Category = "VR|Runtime")
    TObjectPtr<UGrabComponent> PulledGrabComponentRight;

    /** 左手当前"目标"抓取组件（在范围内但尚未抓取） */
    UPROPERTY(BlueprintReadOnly, Transient, Category = "VR|Runtime")
    TObjectPtr<UGrabComponent> TargetGrabComponentLeft;

    /** 右手当前"目标"抓取组件 */
    UPROPERTY(BlueprintReadOnly, Transient, Category = "VR|Runtime")
    TObjectPtr<UGrabComponent> TargetGrabComponentRight;

    /** 传送轨迹是否处于激活状态 */
    UPROPERTY(BlueprintReadOnly, Transient, Category = "VR|Runtime")
    bool bTeleportTraceActive;

    /** 当前传送落点是否有效 */
    UPROPERTY(BlueprintReadOnly, Transient, Category = "VR|Runtime")
    bool bValidTeleportLocation;

    /** 投影到NavMesh后的传送目标位置 */
    UPROPERTY(BlueprintReadOnly, Transient, Category = "VR|Runtime")
    FVector ProjectedTeleportLocation;

    /** 传送弹道路径点（供Niagara VFX使用） */
    UPROPERTY(BlueprintReadOnly, Transient, Category = "VR|Runtime")
    TArray<FVector> TeleportTracePathPositions;

    /** 传送可视化Actor引用 */
    UPROPERTY(BlueprintReadOnly, Transient, Category = "VR|Runtime")
    TObjectPtr<AActor> TeleportVisualizerReference;

    /** 菜单Widget运行时实例 */
    UPROPERTY(BlueprintReadOnly, Transient, Category = "VR|Runtime")
    TObjectPtr<UUserWidget> MenuReference;

    /** 当前打开菜单的那只手是否为右手 */
    UPROPERTY(BlueprintReadOnly, Transient, Category = "VR|Runtime")
    bool bActiveMenuHandRight;

    //=====================================================================
    // 蓝图函数 - 抓取
    //=====================================================================

    /** 左手抓取（对应蓝图 TryGrabLeft） */
    UFUNCTION(BlueprintCallable, Category = "VR|Grab")
    bool TryGrabLeft(UGrabComponent* GrabComponent);

    /** 右手抓取（对应蓝图 TryGrabRight） */
    UFUNCTION(BlueprintCallable, Category = "VR|Grab")
    bool TryGrabRight(UGrabComponent* GrabComponent);

    /**
     * 找到MotionController附近的GrabComponent中最近的一个。
     * 对应蓝图 GetGrabComponentNearMotionController。
     */
    UFUNCTION(BlueprintCallable, Category = "VR|Grab")
    UGrabComponent* GetGrabComponentNearMotionController(UMotionControllerComponent* MotionController, UGrabComponent* TargetGrabComponent) const;

    /**
     * 通过Aim射线找到当前瞄准的GrabComponent。
     * 对应蓝图 GetGrabComponentUnderAim。
     */
    UFUNCTION(BlueprintCallable, Category = "VR|Grab")
    UGrabComponent* GetGrabComponentUnderAim(UMotionControllerComponent* MotionControllerAim, UGrabComponent* TargetGrabComponent) const;

    /** 更新拉拽状态（每Tick调用），对应蓝图 UpdatePulledObject。返回是否仍可继续抓取 */
    UFUNCTION(BlueprintCallable, Category = "VR|Grab")
    bool UpdatePulledObject(UGrabComponent* InGrabComponent, UMotionControllerComponent* InMotionController, float DeltaTime);

    /** 更新指定手的"目标"抓取组件（每Tick调用），对应蓝图 UpdateTargetGrabComponent */
    void UpdateTargetGrabComponent(UGrabComponent* NewTarget,TObjectPtr<UGrabComponent>& TargetGrabComponent);

    /** 标记潜在目标（高亮），对应蓝图 UpdatePotentialTarget */
    void UpdatePotentialTarget(UMotionControllerComponent* MotionControllerAim,TObjectPtr<UGrabComponent>& TargetGrabComponent);

    /** 标记/取消标记GrabComponent的可抓取高亮，对应蓝图 MarkForGrab */
    UFUNCTION(BlueprintCallable, Category = "VR|Grab")
    void MarkForGrab(UGrabComponent* InGrabComponent, bool bCanBeGrab);

    //=====================================================================
    // 蓝图函数 - 传送
    //=====================================================================

    /** 开启传送轨迹显示 */
    UFUNCTION(BlueprintCallable, Category = "VR|Teleport")
    void StartTeleportTrace();

    /** 结束传送并尝试执行传送 */
    UFUNCTION(BlueprintCallable, Category = "VR|Teleport")
    void EndTeleportTrace();

    /** 更新传送轨迹（每Tick调用） */
    UFUNCTION(BlueprintCallable, Category = "VR|Teleport")
    void TeleportTrace();

    /** 执行传送 */
    UFUNCTION(BlueprintCallable, Category = "VR|Teleport")
    bool TryTeleport();

    /** 判断落点是否在导航网格上，返回投影后的位置 */
    UFUNCTION(BlueprintCallable, Category = "VR|Teleport")
    bool IsValidTeleportLocation(const FHitResult& Hit, FVector& OutProjectedLocation) const;

    //=====================================================================
    // 蓝图函数 - 其他
    //=====================================================================

    /** 快速转向：右转true / 左转false */
    UFUNCTION(BlueprintCallable, Category = "VR|Locomotion")
    void SnapTurn(bool bRightTurn);

    /** 切换菜单显示 */
    UFUNCTION(BlueprintCallable, Category = "VR|Menu")
    void ToggleMenu(bool bRightHand);

    /** 关闭菜单 */
    UFUNCTION(BlueprintCallable, Category = "VR|Menu")
    void CloseMenu();

    /** 显示/隐藏单只手Mesh（用于抓取物体时隐藏空手），对应蓝图 HideUnhideHand */
    UFUNCTION(BlueprintCallable, Category = "VR|Hands")
    void HideUnhideHand(bool bRightHand, bool bHide);

    /**
     * 沿Aim方向做球体扫描以查找可抓取物体。
     * 对应蓝图 TraceAim。
     */
    UFUNCTION(BlueprintCallable, Category = "VR|Aim")
    bool TraceAim(UMotionControllerComponent* MotionControllerAim, FHitResult& OutHit) const;

protected:
    //=====================================================================
    // Enhanced Input 回调
    //=====================================================================

    void OnMoveStarted(const FInputActionValue& Value);
    void OnMoveTriggered(const FInputActionValue& Value);
    void OnMoveCompleted(const FInputActionValue& Value);
    
    void OnTurnStarted(const FInputActionValue& Value);
    void OnTurnTriggered(const FInputActionValue& Value);
    void OnTurnCompleted(const FInputActionValue& Value);

    void OnGrabLeftPressed(const FInputActionValue& Value);
    void OnGrabLeftReleased(const FInputActionValue& Value);
    void OnGrabRightPressed(const FInputActionValue& Value);
    void OnGrabRightReleased(const FInputActionValue& Value);

    void OnMenuToggleLeft(const FInputActionValue& Value);
    void OnMenuToggleRight(const FInputActionValue& Value);

private:
    /** 快速转向的Y轴累计，用于避免摇杆持续推动重复触发 */
    bool bTurnConsumed;
};
