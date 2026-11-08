// 使用真实 GameWorld、原生 BeginPlay/UMG 和保存菜谱；LocalCameraManagerTick 验证生产自动相机获取，不代表 HMD 跟踪或渲染像素。
// 保存实例测试须由主线程保存最新 L_Cafe 后执行；只读注册，不加载备份或保存地图。
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Serving/OrderHologramActor.h"
#include "Serving/DrinkMenuData.h"
#include "Serving/DrinkRecipe.h"
#include "Serving/ServingSpotActor.h"
#include "UI/OrderHologramWidget.h"
#include "Tests/TutorialMenuTestHelpers.h"
#include "Blueprint/WidgetTree.h"
#include "Components/BoxComponent.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/WidgetSwitcher.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Level.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Math/RotationMatrix.h"
#include "CoreGlobals.h"
#include <limits>

namespace HeadsetMenuFacingTests
{
constexpr auto TestOptions = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

FTransform MenuPose()
{
    return FTransform(FRotator(0, 35.77, 0), FVector(-74, 131, 92), FVector::OneVector);
}

struct FWorld
{
    UWorld* World = nullptr;
    UGameInstance* GI = nullptr;
    AOrderHologramActor* Screen = nullptr;

    FWorld()
    {
        if (!GEngine || !IsInGameThread()) return;
        UWorld::InitializationValues IV;
        IV.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false)
            .CreateAISystem(false).ShouldSimulatePhysics(false);
        World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
            ERHIFeatureLevel::SM5, &IV);
        if (!World) return;
        auto& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
        GI = NewObject<UGameInstance>(GEngine);
        Context.OwningGameInstance = GI;
        World->SetGameInstance(GI);
        // UE5.6 SetCurrentWorld 回调 OwningGameInstance::OnWorldChanged，须在 Init 前绑定内部 Context。
        Context.SetCurrentWorld(World);
        GI->Init();
        Screen = World->SpawnActorDeferred<AOrderHologramActor>(AOrderHologramActor::StaticClass(),
            MenuPose(), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!Screen) return;
        // 配置测试实例，不修改保存资产或原生 CDO；菜单数据必须来自真实保存 DA。
        auto* DefaultRecipeMenu = LoadObject<UDrinkMenuData>(nullptr,
            TEXT("/Game/VRTemplate/MenuIngredients/Menu/DA_RecipeMenu.DA_RecipeMenu"));
        Screen->RecipeMenu = DefaultRecipeMenu;
        Screen->bFaceHeadset = true;
        Screen->FacingInterpSpeed = 4.f;
        Screen->PanelCenter = FVector(0, 0, 55);
        Screen->PanelWidthCM = 60.f;
        Screen->bOpenOnRecipePage = false;
        Screen->FinishSpawning(MenuPose());
    }

    bool Begin(FAutomationTestBase& Test)
    {
        if (!Test.TestNotNull(TEXT("真实 GameWorld"), World)
            || !Test.TestNotNull(TEXT("真实 GameInstance"), GI)
            || !Test.TestNotNull(TEXT("原生菜单实例"), Screen)
            || !Test.TestNotNull(TEXT("保存菜单 DA"), Screen->RecipeMenu.Get())
            || !Test.TestTrue(TEXT("保存菜谱完整有效"), Screen->RecipeMenu->IsValidMenu())) return false;
        World->InitializeActorsForPlay(FURL());
        World->GetWorldSettings()->NotifyBeginPlay();
        auto* Widget = Screen->GetDisplayWidget();
        return Test.TestTrue(TEXT("真实 GameWorld 已启动原生 BeginPlay"), World->IsGameWorld()
                && World->HasBegunPlay() && Screen->HasActorBegunPlay())
            && Test.TestNotNull(TEXT("BeginPlay 创建真实 UMG"), Widget)
            && Test.TestNotNull(TEXT("真实 UMG 控件树"), Widget->WidgetTree.Get())
            && Test.TestNull(TEXT("夹具没有活动本地 PC，不伪造自动相机更新"),
                World->GetFirstPlayerController());
    }

    bool TouchPower()
    {
        const FVector Away = Screen->GetActorLocation() + FVector(500, 500, 500);
        Screen->ProcessTouchSample(EControllerHand::Left, Away);
        Screen->ProcessTouchSample(EControllerHand::Right, Away);
        return Screen->ProcessTouchSample(EControllerHand::Left, Screen->ButtonZone->GetComponentLocation());
    }

    ~FWorld()
    {
        if (!World) return;
        if (World->HasBegunPlay()) World->EndPlay(EEndPlayReason::Quit);
        if (GI) GI->Shutdown();
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
    }
};

FVector FullCenter(const AOrderHologramActor* Screen)
{
    const USceneComponent* Parent = Screen->Panel->GetAttachParent();
    return Parent ? Parent->GetComponentTransform().TransformPosition(Screen->PanelCenter) : Screen->PanelCenter;
}

struct FFixedParts
{
    FTransform Actor;
    FTransform Root;
    FTransform Base;
    FTransform PowerZone;
    FVector Center;
    FVector FixedCenter;

    explicit FFixedParts(const AOrderHologramActor* Screen)
        : Actor(Screen->GetActorTransform()), Root(Screen->GetRootComponent()->GetComponentTransform()),
          Base(Screen->BaseMesh->GetComponentTransform()), PowerZone(Screen->ButtonZone->GetComponentTransform()),
          Center(Screen->PanelCenter), FixedCenter(FullCenter(Screen))
    {
    }

    void Check(FAutomationTestBase& Test, const AOrderHologramActor* Screen) const
    {
        Test.TestTrue(TEXT("ActorTransform 未变化"), Screen->GetActorTransform().Equals(Actor, .00001));
        Test.TestTrue(TEXT("Root 世界变换未变化"), Screen->GetRootComponent()->GetComponentTransform().Equals(Root, .00001));
        Test.TestTrue(TEXT("BaseMesh 世界变换未变化"), Screen->BaseMesh->GetComponentTransform().Equals(Base, .00001));
        Test.TestTrue(TEXT("实体电源 ButtonZone 世界变换未变化"), Screen->ButtonZone->GetComponentTransform().Equals(PowerZone, .00001));
        Test.TestTrue(TEXT("PanelCenter 配置未变化"), Screen->PanelCenter.Equals(Center, .00001));
        Test.TestTrue(TEXT("父变换下完全展开中心未漂移"), FullCenter(Screen).Equals(FixedCenter, .00001));
    }
};

void CheckFacing(FAutomationTestBase& Test, const AOrderHologramActor* Screen, const FVector& CameraWorld)
{
    const FVector Direction = (CameraWorld - FullCenter(Screen)).GetSafeNormal();
    const FVector Forward = Screen->Panel->GetForwardVector();
    const FVector WorldUpInPlane = (FVector::UpVector - Direction * Direction.Z).GetSafeNormal();
    Test.TestTrue(TEXT("面板正面完整朝向相机位置，点积大于 .999"), FVector::DotProduct(Forward, Direction) > .999);
    Test.TestTrue(TEXT("世界 Roll 为零"), FMath::Abs(Screen->Panel->GetComponentRotation().Roll) < .001);
    Test.TestTrue(TEXT("面板上方向为世界 up 在面内投影，不随父级滚转"),
        FVector::DotProduct(Screen->Panel->GetUpVector(), WorldUpInPlane) > .999);
}

double AngleDegrees(const FQuat& A, const FQuat& B)
{
    return FMath::RadiansToDegrees(A.AngularDistance(B));
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeadsetMenuSavedMapOptinConfiguration,
    "CyberCafe.HeadsetMenu.SavedMapOptinConfiguration", HeadsetMenuFacingTests::TestOptions)
bool FHeadsetMenuSavedMapOptinConfiguration::RunTest(const FString&)
{
    if (!TestTrue(TEXT("保存资产只在主线程加载和注册"), IsInGameThread())) return false;
    auto* Map = LoadObject<UWorld>(nullptr, TEXT("/Game/VRTemplate/Maps/L_Cafe.L_Cafe"));
    if (!TestNotNull(TEXT("最新保存的原 L_Cafe，不回套备份地图"), Map)
        || !TestNotNull(TEXT("保存的 PersistentLevel"), Map->PersistentLevel.Get())) return false;
    auto* Screen = FindObject<AOrderHologramActor>(Map->PersistentLevel, TEXT("BP_OrderHologram_Tutorial"));
    if (!TestNotNull(TEXT("原地图具名教学菜单实例"), Screen)) return false;
    // 离线加载的组件尚未注册，不能把零世界变换当作保存姿态。仅注册，不启动地图。
    if (!Map->bIsWorldInitialized)
    {
        UWorld::InitializationValues IV;
        IV.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false)
            .CreateAISystem(false).ShouldSimulatePhysics(false);
        Map->InitWorld(IV);
    }
    Map->UpdateWorldComponents(true, false);
    if (!TestNotNull(TEXT("保存实例真实面板"), Screen->Panel.Get())) return false;
    TestTrue(TEXT("读取的是原地图实例"), Screen->GetWorld() == Map);
    TestTrue(TEXT("保存实例明确 opt-in"), Screen->bFaceHeadset);
    TestEqual(TEXT("保存宽度为 60 cm"), Screen->PanelWidthCM, 60.f);
    TestTrue(TEXT("保存中心为父级局部 (0,0,55)"), Screen->PanelCenter.Equals(FVector(0, 0, 55), .00001));
    TestTrue(TEXT("保存原实例位置 (-74,131,92)，不重新赋值"), Screen->GetActorLocation().Equals(FVector(-74, 131, 92), .01));
    TestTrue(TEXT("保存原 yaw 35.77，容差仅匹配给定小数精度"),
        Screen->GetActorRotation().Equals(FRotator(0, 35.77, 0), .01));
    TestTrue(TEXT("保存原根缩放为一"), Screen->GetActorScale3D().Equals(FVector::OneVector, .00001));
    TestTrue(TEXT("保存面板已注册，姿态读取不是未注册的零变换"), Screen->Panel->IsRegistered());
    auto* DefaultRecipeMenu = LoadObject<UDrinkMenuData>(nullptr,
        TEXT("/Game/VRTemplate/MenuIngredients/Menu/DA_RecipeMenu.DA_RecipeMenu"));
    if (!TestNotNull(TEXT("保存菜单数据"), DefaultRecipeMenu)) return false;
    TestEqual(TEXT("原实例引用真实保存菜单"), Screen->RecipeMenu.Get(), DefaultRecipeMenu);
    TestTrue(TEXT("保存菜单有效"), DefaultRecipeMenu->IsValidMenu());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeadsetMenuSeatedStandingFrontAlignment,
    "CyberCafe.HeadsetMenu.SeatedStandingFrontAlignment", HeadsetMenuFacingTests::TestOptions)
bool FHeadsetMenuSeatedStandingFrontAlignment::RunTest(const FString&)
{
    HeadsetMenuFacingTests::FWorld F;
    if (!F.Begin(*this)) return false;
    auto* Screen = F.Screen;
    TestTrue(TEXT("原生实例采用实际菜单位置、yaw 和根缩放"),
        Screen->GetActorTransform().Equals(HeadsetMenuFacingTests::MenuPose(), .00001));
    const HeadsetMenuFacingTests::FFixedParts Fixed(Screen);
    const FVector Center = HeadsetMenuFacingTests::FullCenter(Screen);
    const FVector Cameras[] = { Center + FVector(-40, -90, -30), Center + FVector(-40, -90, 35) };
    for (int32 I = 0; I < 2; ++I)
    {
        const FTransform Before = Screen->Panel->GetComponentTransform();
        if (!TestTrue(TEXT("公开 API 接受真实场景坐/站位置输入，Dt=0 对准"),
            Screen->UpdatePanelFacing(Cameras[I], 0.f))) return false;
        HeadsetMenuFacingTests::CheckFacing(*this, Screen, Cameras[I]);
        TestTrue(TEXT("座姿负 pitch，站姿正 pitch，不只改 yaw"), I == 0
            ? Screen->Panel->GetComponentRotation().Pitch < 0 : Screen->Panel->GetComponentRotation().Pitch > 0);
        TestTrue(TEXT("朝向 API 只改 Panel 旋转，不改它的位置和缩放"),
            Screen->Panel->GetComponentLocation().Equals(Before.GetLocation(), .00001)
            && Screen->Panel->GetComponentTransform().GetScale3D().Equals(Before.GetScale3D(), .00001));
        Fixed.Check(*this, Screen);
    }
    TestFalse(TEXT("对准不会凭空打开菜单"), Screen->IsPanelOpen());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeadsetMenuLateralYawAndTrackingInvalid,
    "CyberCafe.HeadsetMenu.LateralYawAndTrackingInvalid", HeadsetMenuFacingTests::TestOptions)
bool FHeadsetMenuLateralYawAndTrackingInvalid::RunTest(const FString&)
{
    HeadsetMenuFacingTests::FWorld F;
    if (!F.Begin(*this)) return false;
    auto* Screen = F.Screen;
    const HeadsetMenuFacingTests::FFixedParts Fixed(Screen);
    const FVector Center = HeadsetMenuFacingTests::FullCenter(Screen);
    const FVector Left = Center + FVector(-90, -40, -30);
    const FVector Right = Center + FVector(40, -90, -30);
    if (!TestTrue(TEXT("左侧有限位置有效"), Screen->UpdatePanelFacing(Left, 0))) return false;
    HeadsetMenuFacingTests::CheckFacing(*this, Screen, Left);
    const FQuat LeftFacing = Screen->Panel->GetComponentQuat();
    if (!TestTrue(TEXT("右侧有限位置有效"), Screen->UpdatePanelFacing(Right, 0))) return false;
    HeadsetMenuFacingTests::CheckFacing(*this, Screen, Right);
    TestTrue(TEXT("横向移动确实改变世界 yaw"),
        HeadsetMenuFacingTests::AngleDegrees(LeftFacing, Screen->Panel->GetComponentQuat()) > 30);
    // 世界原点本身是合法位置；无跟踪不能被伪装成传入 (0,0,0) 的硬编码特殊规则。
    if (!TestTrue(TEXT("相机在世界原点但不与中心重合仍是有效几何输入"),
        Screen->UpdatePanelFacing(FVector::ZeroVector, 0))) return false;
    HeadsetMenuFacingTests::CheckFacing(*this, Screen, FVector::ZeroVector);
    const FTransform Last = Screen->Panel->GetComponentTransform();
    const auto Reject = [this, Screen, &Last](const FString& Label, const FVector& CameraWorld, float Dt)
    {
        TestFalse(Label, Screen->UpdatePanelFacing(CameraWorld, Dt));
        TestTrue(Label + TEXT("：保留最后有效 Panel 完整变换"), Screen->Panel->GetComponentTransform().Equals(Last, .00001));
    };
    Reject(TEXT("零方向：相机与固定中心重合"), Center, 0);
    Reject(TEXT("距离不足一厘米"), Center + FVector(.5, 0, 0), 0);
    Reject(TEXT("正上方无法确定无滚转朝向"), Center + FVector(0, 0, 100), 0);
    Reject(TEXT("正下方无法确定无滚转朝向"), Center - FVector(0, 0, 100), 0);
    Reject(TEXT("负 Dt 拒绝"), Left, -.01f);
    Reject(TEXT("非有限 Dt 拒绝"), Left, std::numeric_limits<float>::infinity());
    Reject(TEXT("NaN Dt 拒绝"), Left, std::numeric_limits<float>::quiet_NaN());
    for (int32 Axis = 0; Axis < 3; ++Axis)
    {
        FVector Invalid = Left;
        Invalid[Axis] = std::numeric_limits<double>::quiet_NaN();
        Reject(FString::Printf(TEXT("相机轴 %d 为 NaN 拒绝"), Axis), Invalid, 0);
        Invalid[Axis] = std::numeric_limits<double>::infinity();
        Reject(FString::Printf(TEXT("相机轴 %d 为 Inf 拒绝"), Axis), Invalid, 0);
    }
    Fixed.Check(*this, Screen);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeadsetMenuSmoothNoSpin,
    "CyberCafe.HeadsetMenu.SmoothNoSpin", HeadsetMenuFacingTests::TestOptions)
bool FHeadsetMenuSmoothNoSpin::RunTest(const FString&)
{
    HeadsetMenuFacingTests::FWorld F;
    if (!F.Begin(*this)) return false;
    auto* Screen = F.Screen;
    const HeadsetMenuFacingTests::FFixedParts Fixed(Screen);
    const FVector Center = HeadsetMenuFacingTests::FullCenter(Screen);
    const FVector Start = Center + FRotator(-16, 179, 0).Vector() * 100;
    const FVector CameraWorld = Center + FRotator(-16, -176, 0).Vector() * 100;
    if (!TestTrue(TEXT("先建立有效世界朝向"), Screen->UpdatePanelFacing(Start, 0))) return false;
    const FQuat Before = Screen->Panel->GetComponentQuat();
    const FQuat Target = FRotationMatrix::MakeFromXZ((CameraWorld - Center).GetSafeNormal(), FVector::UpVector).ToQuat();
    const double Initial = HeadsetMenuFacingTests::AngleDegrees(Before, Target);
    TestTrue(TEXT("相机只跨 yaw 边界移动世界五度，不是三百五十五度"), FMath::IsNearlyEqual(Initial, 5.0, .001));
    if (!TestTrue(TEXT("Dt .01 使用默认速度 4 插值"), Screen->UpdatePanelFacing(CameraWorld, .01f))) return false;
    const FQuat After = Screen->Panel->GetComponentQuat();
    const double Moved = HeadsetMenuFacingTests::AngleDegrees(Before, After);
    double Remaining = HeadsetMenuFacingTests::AngleDegrees(After, Target);
    TestTrue(TEXT("小步转动非零但小于目标角，不直接硬切"), Moved > .001 && Moved < Initial);
    TestTrue(TEXT("第一步确实趋近且没有跨目标"), Remaining < Initial && FMath::IsNearlyEqual(Moved + Remaining, Initial, .001));
    for (int32 I = 0; I < 120; ++I)
    {
        const FQuat Previous = Screen->Panel->GetComponentQuat();
        if (!TestTrue(TEXT("连续有限输入被接受"), Screen->UpdatePanelFacing(CameraWorld, .01f))) return false;
        const FQuat Current = Screen->Panel->GetComponentQuat();
        const double Step = HeadsetMenuFacingTests::AngleDegrees(Previous, Current);
        const double Next = HeadsetMenuFacingTests::AngleDegrees(Current, Target);
        TestTrue(TEXT("每步趋近不越过目标、不绕圈"), Next <= Remaining + .001
            && Step <= Remaining + .001 && FMath::IsNearlyEqual(Step + Next, Remaining, .002));
        TestTrue(TEXT("平滑中世界 Roll 始终零"), FMath::Abs(Screen->Panel->GetComponentRotation().Roll) < .001);
        Remaining = Next;
    }
    HeadsetMenuFacingTests::CheckFacing(*this, Screen, CameraWorld);
    Fixed.Check(*this, Screen);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeadsetMenuRotatedParentAndOpenAnimation,
    "CyberCafe.HeadsetMenu.RotatedParentAndOpenAnimation", HeadsetMenuFacingTests::TestOptions)
bool FHeadsetMenuRotatedParentAndOpenAnimation::RunTest(const FString&)
{
    HeadsetMenuFacingTests::FWorld F;
    if (!F.Begin(*this)) return false;
    auto* Screen = F.Screen;
    auto* Widget = Screen->GetDisplayWidget();
    TArray<UWidget*> Controls;
    Widget->WidgetTree->GetAllWidgets(Controls);
    TMap<int32, FIntRect> BaseRects;
    for (UWidget* Control : Controls)
        if (auto* Button = Cast<URecipeScreenButton>(Control))
            BaseRects.Add(Button->Action, UOrderHologramWidget::GetScreenActionRect(Button->Action));
    if (!TestEqual(TEXT("旋转前真实 UMG 的十六个基准 Rect"), BaseRects.Num(), 16)) return false;
    auto* Parent = NewObject<USceneComponent>(Screen, TEXT("FacingTestParent"));
    Screen->AddInstanceComponent(Parent);
    Parent->SetupAttachment(Screen->GetRootComponent());
    Parent->SetRelativeTransform(FTransform(FRotator(17, -62, 23), FVector(3, -4, 7), FVector::OneVector));
    Parent->RegisterComponent();
    if (!TestTrue(TEXT("Panel 挂接真实旋转父级"), Screen->Panel->AttachToComponent(Parent, FAttachmentTransformRules::KeepRelativeTransform))) return false;
    const HeadsetMenuFacingTests::FFixedParts Fixed(Screen);
    const FVector Center = HeadsetMenuFacingTests::FullCenter(Screen);
    const FVector CameraWorld = Center + FVector(-40, -90, -30);
    if (!TestTrue(TEXT("旋转父级下直接公开 API 对准"), Screen->UpdatePanelFacing(CameraWorld, 0))) return false;
    HeadsetMenuFacingTests::CheckFacing(*this, Screen, CameraWorld);
    const FQuat Facing = Screen->Panel->GetComponentQuat();
    if (!TestTrue(TEXT("真实电源打开订单页"), F.TouchPower())) return false;
    TestFalse(TEXT("默认开订单页而不是偷偷切配方"), Screen->IsRecipeMenuMode());
    TestFalse(TEXT("尚未展开，真实 Action1 不可点"), TutorialMenuTestHelpers::TouchAction(Screen, 1));
    Screen->Tick(.1f);
    const float Scale = Screen->PanelWidthCM / 1200.f;
    const float Opening = .1f / Screen->AnimationSeconds;
    const FVector ActualRelativeUp = Screen->Panel->GetRelativeRotation().RotateVector(FVector::UpVector);
    const FVector ExpectedRelativeCenter = Screen->PanelCenter - ActualRelativeUp * ((1 - Opening) * 750.f * Scale * .5f);
    TestTrue(TEXT("展开动画使用实际相对旋转的 up，而非原 PanelRotation"),
        Screen->Panel->GetRelativeLocation().Equals(ExpectedRelativeCenter, .0001));
    TestTrue(TEXT("半展开的固定中心仍是父 TM 下 PanelCenter"), HeadsetMenuFacingTests::FullCenter(Screen).Equals(Center, .00001));
    TestFalse(TEXT("未完全展开仍不能伪造点击"), TutorialMenuTestHelpers::TouchAction(Screen, 1));
    Screen->Tick(.5f);
    TestTrue(TEXT("完全展开中心精确恢复固定中心"), Screen->Panel->GetComponentLocation().Equals(Center, .0001));
    TestTrue(TEXT("无活动本地 PC 的 SetOpen/Tick 不覆盖最后有效朝向"), Screen->Panel->GetComponentQuat().Equals(Facing, .00001));
    HeadsetMenuFacingTests::CheckFacing(*this, Screen, CameraWorld);
    if (!TestTrue(TEXT("Action1 实际 Rect 中心手采样可开配方页"), TutorialMenuTestHelpers::TouchAction(Screen, 1))) return false;
    TSet<int32> AcceptedActions = {1};
    auto* Pages = Cast<UWidgetSwitcher>(Widget->WidgetTree->FindWidget(TEXT("OrderMenuSwitcher")));
    if (!TestNotNull(TEXT("真实 UMG 双页"), Pages)) return false;
    TestEqual(TEXT("实际切换到了配方页 1"), Pages->GetActiveWidgetIndex(), 1);
    const TArray<int32> Rows = Screen->GetVisibleMenuIndices();
    if (!TestEqual(TEXT("保存菜单实际十行"), Rows.Num(), 10)) return false;
    TSet<int32> Actions;
    for (UWidget* Control : Controls)
    {
        auto* Button = Cast<URecipeScreenButton>(Control);
        if (!Button) continue;
        TestFalse(TEXT("真实按钮 action 没有重复"), Actions.Contains(Button->Action));
        Actions.Add(Button->Action);
        const FIntRect Rect = UOrderHologramWidget::GetScreenActionRect(Button->Action);
        const FIntRect* BaseRect = BaseRects.Find(Button->Action);
        if (!TestNotNull(TEXT("旋转前对应 action 的真实基准 Rect"), BaseRect)) return false;
        TestTrue(TEXT("旋转及动画未改变原十六按钮 Rect"), Rect == *BaseRect);
        auto* Slot = Cast<UCanvasPanelSlot>(Button->Slot);
        if (!TestNotNull(TEXT("真实按钮 CanvasSlot"), Slot)) return false;
        const FVector2D PageOrigin(0, Button->Action <= 1 ? 0 : 88);
        TestTrue(TEXT("全部十六按钮仍使用原 Rect，未随朝向改布局"), Rect.Width() > 0 && Rect.Height() > 0
            && Slot->GetPosition().Equals(FVector2D(Rect.Min.X, Rect.Min.Y) - PageOrigin)
            && Slot->GetSize().Equals(FVector2D(Rect.Width(), Rect.Height())));
    }
    if (!TestEqual(TEXT("真实 UMG 全部十六个独立动作"), Actions.Num(), 16)) return false;
    for (int32 Action : {0, 1, 2, 10, 11, 12, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109})
        TestTrue(TEXT("实际按钮 action 集完整"), Actions.Contains(Action));
    for (int32 Row = 0; Row < Rows.Num(); ++Row)
    {
        const int32 Action = 100 + Row;
        if (!TestTrue(TEXT("旋转面板上的全部十行使用真实 Rect 中心手采样"),
            TutorialMenuTestHelpers::TouchAction(Screen, Action))) return false;
        AcceptedActions.Add(Action);
        TestEqual(TEXT("真实行点击映射原数据索引"), Screen->GetSelectedMenuIndex(), Rows[Row]);
        TestEqual(TEXT("实际接受的是该行动作"), Screen->GetLastScreenAction(), Action);
    }
    for (int32 Action : {11, 12, 10})
    {
        if (!TestTrue(TEXT("三种分类在实际配方页接受真实触屏"),
            TutorialMenuTestHelpers::TouchAction(Screen, Action))) return false;
        AcceptedActions.Add(Action);
        TestEqual(TEXT("触屏分类实际生效"), Screen->GetMenuFilter(), Action - 10);
    }
    if (!TestTrue(TEXT("真实 Action0 切订单页"), TutorialMenuTestHelpers::TouchAction(Screen, 0))) return false;
    AcceptedActions.Add(0);
    TestEqual(TEXT("实际订单页索引 0"), Pages->GetActiveWidgetIndex(), 0);
    if (!TestTrue(TEXT("真实 Action1 可再次切配方页"), TutorialMenuTestHelpers::TouchAction(Screen, 1))) return false;
    // Action2 只在订单页且有真实展示配方时有效，不通过脚本动作冒充手输入。
    auto* Recipe = LoadObject<UDrinkRecipe>(nullptr, TEXT("/Game/VRTemplate/Data/Recipes/DA_Latte.DA_Latte"));
    if (!TestNotNull(TEXT("真实保存首单配方"), Recipe)
        || !TestTrue(TEXT("真实配方自动展示到订单页"), Screen->RevealFirstOrderRecipe(Recipe))) return false;
    TestEqual(TEXT("自动展示实际页为 0"), Pages->GetActiveWidgetIndex(), 0);
    TestFalse(TEXT("自动展示本身不是配方页操作"), Screen->IsRecipeMenuMode());
    auto* Spot = F.World->SpawnActor<AServingSpotActor>();
    if (!TestNotNull(TEXT("真实服务点用于验证 serial 不被触屏推进"), Spot)) return false;
    Spot->SetActiveRecipe(Recipe);
    const uint32 SpotSerial = Spot->GetOrderSerial();
    const int64 MenuSerial = Screen->GetCurrentOrderSerial();
    if (!TestTrue(TEXT("重申座姿相机位置朝向"), Screen->UpdatePanelFacing(CameraWorld, 0))) return false;
    Screen->Tick(.5f);
    if (!TestTrue(TEXT("座姿旋转面板上真实 ProcessMenuTouchSample Action2 成功"),
        TutorialMenuTestHelpers::TouchAction(Screen, 2))) return false;
    AcceptedActions.Add(2);
    TestEqual(TEXT("实际分页条件下十六个动作均通过真实手采样接受"), AcceptedActions.Num(), 16);
    TestEqual(TEXT("接受的真实动作是 Action2"), Screen->GetLastScreenAction(), 2);
    TestEqual(TEXT("真实 UMG 显示对应配方页"), Pages->GetActiveWidgetIndex(), 1);
    const auto* Selected = Screen->GetSelectedMenuEntry();
    if (!TestNotNull(TEXT("实际选择的菜单条目"), Selected)) return false;
    TestEqual(TEXT("实际查阅的是保存首单配方"), Selected->Recipe.Get(), Recipe);
    TestNull(TEXT("查配方不提前建立菜单订单"), Screen->GetCurrentRecipe());
    TestFalse(TEXT("查配方不进入待提交"), Screen->IsPending());
    TestEqual(TEXT("菜单 serial 不变"), Screen->GetCurrentOrderSerial(), MenuSerial);
    TestEqual(TEXT("服务点 serial 不变"), Spot->GetOrderSerial(), SpotSerial);
    TestEqual(TEXT("服务点原配方不变"), Spot->ActiveRecipe.Get(), Recipe);
    TestEqual(TEXT("无伪造交付历史"), Screen->GetDeliveryRecords().Num(), 0);
    Fixed.Check(*this, Screen);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeadsetMenuClosedPanelNoFakeActions,
    "CyberCafe.HeadsetMenu.ClosedPanelNoFakeActions", HeadsetMenuFacingTests::TestOptions)
bool FHeadsetMenuClosedPanelNoFakeActions::RunTest(const FString&)
{
    HeadsetMenuFacingTests::FWorld F;
    if (!F.Begin(*this)) return false;
    auto* Screen = F.Screen;
    const HeadsetMenuFacingTests::FFixedParts Fixed(Screen);
    const FVector CameraWorld = HeadsetMenuFacingTests::FullCenter(Screen) + FVector(-40, -90, -30);
    if (!TestTrue(TEXT("关闭时仍可更新面板旋转"), Screen->UpdatePanelFacing(CameraWorld, 0))) return false;
    const FQuat Facing = Screen->Panel->GetComponentQuat();
    const int32 Selected = Screen->GetSelectedMenuIndex();
    TestFalse(TEXT("改变朝向未打开屏"), Screen->IsPanelOpen());
    for (int32 Action : {0, 1, 2, 10, 11, 12, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109})
        TestFalse(TEXT("关闭时真实 Rect 采样不产生屏幕动作"), TutorialMenuTestHelpers::TouchAction(Screen, Action));
    TestFalse(TEXT("关闭时 Action API 同样拒绝"), Screen->ActivateScreenAction(1));
    Screen->Tick(.5f);
    TestTrue(TEXT("无本地 PC 的关闭 Tick 保留最后朝向，不替换成世界原点"),
        Screen->Panel->GetComponentQuat().Equals(Facing, .00001));
    TestFalse(TEXT("朝向和关闭采样未开屏"), Screen->IsPanelOpen());
    TestEqual(TEXT("没有伪造屏幕动作"), Screen->GetLastScreenAction(), INDEX_NONE);
    TestEqual(TEXT("没有偷换菜单选择"), Screen->GetSelectedMenuIndex(), Selected);
    TestNull(TEXT("没有伪造订单"), Screen->GetCurrentRecipe());
    TestEqual(TEXT("没有伪造 serial"), Screen->GetCurrentOrderSerial(), int64(0));
    TestEqual(TEXT("没有伪造历史"), Screen->GetDeliveryRecords().Num(), 0);
    Fixed.Check(*this, Screen);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeadsetMenuSavedCdoOptout,
    "CyberCafe.HeadsetMenu.SavedCdoOptout", HeadsetMenuFacingTests::TestOptions)
bool FHeadsetMenuSavedCdoOptout::RunTest(const FString&)
{
    const auto* CDO = GetDefault<AOrderHologramActor>();
    if (!TestNotNull(TEXT("只读原生 CDO"), CDO)) return false;
    TestFalse(TEXT("原生 CDO 默认 opt-out，旧屏幕不被统一改朝向"), CDO->bFaceHeadset);
    TestEqual(TEXT("原生默认插值速度仍为 4"), CDO->FacingInterpSpeed, 4.f);
    HeadsetMenuFacingTests::FWorld F;
    if (!F.Begin(*this)) return false;
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* OldScreen = F.World->SpawnActor<AOrderHologramActor>(AOrderHologramActor::StaticClass(),
        FVector(200, 131, 92), FRotator(0, 35.77, 0), Params);
    if (!TestNotNull(TEXT("未设置 opt-in 的另一个原生旧屏幕"), OldScreen)) return false;
    TestTrue(TEXT("旧屏幕也经历了真实 BeginPlay"), OldScreen->HasActorBegunPlay());
    TestFalse(TEXT("新生成默认实例未开启头显朝向"), OldScreen->bFaceHeadset);
    const HeadsetMenuFacingTests::FFixedParts Fixed(OldScreen);
    const FTransform Before = OldScreen->Panel->GetComponentTransform();
    TestFalse(TEXT("默认旧实例拒绝朝向 API"), OldScreen->UpdatePanelFacing(
        HeadsetMenuFacingTests::FullCenter(OldScreen) + FVector(-40, -90, 35), 0));
    TestTrue(TEXT("默认旧实例 Panel 完整变换不变"), OldScreen->Panel->GetComponentTransform().Equals(Before, .00001));
    const FQuat Facing = OldScreen->Panel->GetComponentQuat();
    OldScreen->Tick(.5f);
    TestTrue(TEXT("默认旧屏 Tick 仍保留原朝向"), OldScreen->Panel->GetComponentQuat().Equals(Facing, .00001));
    Fixed.Check(*this, OldScreen);
    TestFalse(TEXT("测试实例 opt-in 未污染原生 CDO"), CDO->bFaceHeadset);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeadsetMenuLocalCameraManagerTick,
    "CyberCafe.HeadsetMenu.LocalCameraManagerTick", HeadsetMenuFacingTests::TestOptions)
bool FHeadsetMenuLocalCameraManagerTick::RunTest(const FString&)
{
    HeadsetMenuFacingTests::FWorld F;
    // Begin 保留原 no-PC 断言；仅在原生菜单 BeginPlay 完成后创建实际本地控制器。
    if (!F.Begin(*this)) return false;
    auto* Context = GEngine->GetWorldContextFromWorld(F.World);
    if (!TestNotNull(TEXT("夹具实际 WorldContext"), Context)
        || !TestTrue(TEXT("GI 内部 Context、OwningGameInstance 和当前 World 一致"),
            F.GI->GetWorldContext() == Context && Context->OwningGameInstance == F.GI
            && Context->World() == F.World && F.GI->GetWorld() == F.World
            && F.World->GetGameInstance() == F.GI)) return false;
    auto* Screen = F.Screen;
    const HeadsetMenuFacingTests::FFixedParts Fixed(Screen);
    const FVector Center = HeadsetMenuFacingTests::FullCenter(Screen);
    const FVector SeatedPos = Center + FVector(-40, -90, -30);
    const FVector StandingPos = Center + FVector(-40, -90, 35);
    const FQuat BeforeTick = Screen->Panel->GetComponentQuat();
    if (!TestTrue(TEXT("初始面板尚未对准座姿，不能把原姿态误当 Tick 成功"),
        FVector::DotProduct(Screen->Panel->GetForwardVector(), (SeatedPos - Center).GetSafeNormal()) < .999)) return false;

    FActorSpawnParameters Params;
    Params.ObjectFlags |= RF_Transient;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* PC = F.World->SpawnActor<APlayerController>(Params);
    ULocalPlayer* LocalPlayer = nullptr;
    ACameraActor* CameraActor = nullptr;
    APlayerCameraManager* Camera = nullptr;
    // 所有提前失败路径也在 FWorld 析构前销毁新增 Actor；原生 PC::Destroyed 清理其 CameraManager。
    ON_SCOPE_EXIT
    {
        if (IsValid(PC))
        {
            TestTrue(TEXT("清理实际 PC 及其 CameraManager"), PC->Destroy());
            TestFalse(TEXT("PC 销毁后 CameraManager 不再有效"), IsValid(Camera));
        }
        if (LocalPlayer) LocalPlayer->PlayerController = nullptr;
        if (IsValid(CameraActor)) TestTrue(TEXT("清理实际 ViewTarget CameraActor"), CameraActor->Destroy());
    };
    if (!TestNotNull(TEXT("World 实际生成原生 APlayerController"), PC)) return false;
    // UE5.6 无 viewport 的 CreateLocalPlayer 会触发 ensure；采用原生 ULocalPlayer，不覆盖 local 判定。
    LocalPlayer = NewObject<ULocalPlayer>(GEngine, NAME_None, RF_Transient);
    if (!TestNotNull(TEXT("实际 LocalPlayer 创建必须成功，失败即失败而非 skip"), LocalPlayer)) return false;
    PC->SetPlayer(LocalPlayer);
    if (!TestTrue(TEXT("原生 SetPlayer 实际绑定 LocalPlayer 并成为 localController"),
        PC->GetClass() == APlayerController::StaticClass() && PC->GetWorld() == F.World
        && F.World->GetFirstPlayerController() == PC && PC->GetLocalPlayer() == LocalPlayer
        && LocalPlayer->PlayerController == PC && PC->IsLocalController() && PC->IsLocalPlayerController())
        || !TestNull(TEXT("无受控 Pawn，不依赖 VRPawn 或手柄 MC 跟踪"), PC->GetPawn())) return false;
    Camera = PC->PlayerCameraManager.Get();
    if (!TestNotNull(TEXT("原生 PC 初始化实际创建 CameraManager"), Camera)
        || !TestTrue(TEXT("实际 CameraManager 属于当前 PC 和夹具 World"),
            Camera->GetOwningPlayerController() == PC && Camera->GetWorld() == F.World)) return false;
    CameraActor = F.World->SpawnActor<ACameraActor>(Params);
    if (!TestNotNull(TEXT("实际 ACameraActor ViewTarget"), CameraActor)
        || !TestNotNull(TEXT("ViewTarget 的真实 CameraComponent"), CameraActor->GetCameraComponent())) return false;
    // 只验证 Actor 相机位置进入生产自动获取路径，不注入 HMD pose，也不读取渲染像素。
    CameraActor->GetCameraComponent()->bLockToHmd = false;
    if (!TestTrue(TEXT("真实 CameraActor 移到座姿位置"), CameraActor->SetActorLocation(SeatedPos))) return false;
    PC->SetViewTarget(CameraActor);
    Camera->UpdateCamera(.1f);
    if (!TestTrue(TEXT("PC 的实际 ViewTarget 为 CameraActor"), PC->GetViewTarget() == CameraActor)
        || !TestTrue(TEXT("真实 CameraManager 缓存实际反映座姿位置"), Camera->GetCameraLocation().Equals(SeatedPos, .0001))
        || !TestTrue(TEXT("实际相机 FOV 有限且为正"), FMath::IsFinite(Camera->GetFOVAngle()) && Camera->GetFOVAngle() > 0)
        || !TestTrue(TEXT("只更新相机没有提前人工对准面板"), Screen->Panel->GetComponentQuat().Equals(BeforeTick, .00001))) return false;
    // FacingInterpSpeed 4 * Dt .5 = 2，生产 RInterpTo 的 alpha 钳制为 1；不调用 UpdatePanelFacing。
    Screen->Tick(.5f);
    const double ActualPanelForwardDot = FVector::DotProduct(Screen->Panel->GetForwardVector(),
        (Camera->GetCameraLocation() - Center).GetSafeNormal());
    TestTrue(TEXT("生产 Tick 自动获取实际本地相机，PanelForwardDot 大于 .999"), ActualPanelForwardDot > .999);
    HeadsetMenuFacingTests::CheckFacing(*this, Screen, Camera->GetCameraLocation());
    TestTrue(TEXT("自动相机 Tick 座姿低于中心，世界 pitch 为负"), Screen->Panel->GetComponentRotation().Pitch < 0);
    TestFalse(TEXT("自动朝向和无手柄 PollHands 不会凭空开屏"), Screen->IsPanelOpen());
    Fixed.Check(*this, Screen);
    const FQuat SeatedFacing = Screen->Panel->GetComponentQuat();

    if (!TestTrue(TEXT("真实 CameraActor 移到站姿位置"), CameraActor->SetActorLocation(StandingPos))) return false;
    Camera->UpdateCamera(.1f);
    if (!TestTrue(TEXT("真实 CameraManager 缓存实际反映站姿位置"), Camera->GetCameraLocation().Equals(StandingPos, .0001))) return false;
    Screen->Tick(.5f);
    HeadsetMenuFacingTests::CheckFacing(*this, Screen, Camera->GetCameraLocation());
    TestTrue(TEXT("自动相机 Tick 站姿高于中心，世界 pitch 为正"), Screen->Panel->GetComponentRotation().Pitch > 0);
    TestTrue(TEXT("坐到站的实际相机移动确实改变面板 pitch"),
        HeadsetMenuFacingTests::AngleDegrees(SeatedFacing, Screen->Panel->GetComponentQuat()) > 30);
    Fixed.Check(*this, Screen);

    auto* Recipe = LoadObject<UDrinkRecipe>(nullptr, TEXT("/Game/VRTemplate/Data/Recipes/DA_Latte.DA_Latte"));
    if (!TestNotNull(TEXT("Action2 依赖真实保存的首单配方"), Recipe)
        || !TestTrue(TEXT("真实首单预览打开订单页以接受 Action2"), Screen->RevealFirstOrderRecipe(Recipe))) return false;
    const FQuat FacingBeforeCameraTurn = Screen->Panel->GetComponentQuat();
    const FQuat CameraBeforeTurn = CameraActor->GetActorQuat();
    FRotator TurnedRotation = CameraActor->GetActorRotation();
    TurnedRotation.Yaw += 90;
    if (!TestTrue(TEXT("仅旋转 CameraActor 自身 yaw 九十度"), CameraActor->SetActorRotation(TurnedRotation))) return false;
    Camera->UpdateCamera(.1f);
    if (!TestTrue(TEXT("CameraActor 自身实际转过九十度"),
            FMath::IsNearlyEqual(HeadsetMenuFacingTests::AngleDegrees(CameraBeforeTurn, CameraActor->GetActorQuat()), 90.0, .001))
        || !TestTrue(TEXT("CameraManager 实际反映 CameraActor 新朝向"), Camera->GetCameraRotation().Equals(TurnedRotation, .001))
        || !TestTrue(TEXT("相机转向不改变 Actor 或 CameraManager 的站姿位置"),
            CameraActor->GetActorLocation().Equals(StandingPos, .0001) && Camera->GetCameraLocation().Equals(StandingPos, .0001))) return false;
    Screen->Tick(.5f);
    TestTrue(TEXT("真实相机仅转向时生产 Tick 保持面板目标，不跟随相机 yaw"),
        Screen->Panel->GetComponentQuat().Equals(FacingBeforeCameraTurn, .00001));
    HeadsetMenuFacingTests::CheckFacing(*this, Screen, Camera->GetCameraLocation());
    TestTrue(TEXT("实际展开动画恢复完全展开中心"), Screen->Panel->GetComponentLocation().Equals(Center, .0001));
    Fixed.Check(*this, Screen);
    for (EControllerHand Hand : {EControllerHand::Left, EControllerHand::Right})
    {
        TestFalse(TEXT("无跟踪手采样不产生实体电源动作"),
            Screen->ProcessTouchSample(Hand, Screen->ButtonZone->GetComponentLocation(), false, false));
        TestFalse(TEXT("无跟踪手采样不产生菜单动作"),
            Screen->ProcessMenuTouchSample(Hand, Screen->Panel->GetComponentLocation(), false, false));
    }
    Camera->UpdateCamera(.1f);
    Screen->Tick(.5f);
    TestTrue(TEXT("无手柄跟踪输入不干扰生产自动相机朝向"), Screen->Panel->GetComponentQuat().Equals(FacingBeforeCameraTurn, .00001));
    HeadsetMenuFacingTests::CheckFacing(*this, Screen, Camera->GetCameraLocation());
    if (!TestTrue(TEXT("相机自身转向后实际 Rect 手采样仍可点击 Action2"), TutorialMenuTestHelpers::TouchAction(Screen, 2))) return false;
    TestEqual(TEXT("实际接受的屏内动作是 Action2"), Screen->GetLastScreenAction(), 2);
    TestTrue(TEXT("真实 Action2 切到配方页"), Screen->IsRecipeMenuMode());
    const auto* Selected = Screen->GetSelectedMenuEntry();
    if (!TestNotNull(TEXT("Action2 的实际菜单条目"), Selected)) return false;
    TestEqual(TEXT("实际选择的仍为真实保存首单配方"), Selected->Recipe.Get(), Recipe);
    TestTrue(TEXT("真实 Action2 输入不改变面板朝向"), Screen->Panel->GetComponentQuat().Equals(FacingBeforeCameraTurn, .00001));
    Fixed.Check(*this, Screen);
    return true;
}
#endif
