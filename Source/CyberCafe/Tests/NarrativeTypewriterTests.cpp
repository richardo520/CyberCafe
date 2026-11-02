// @trace NAR-01 NAR-02 NAR-03 NAR-04 NAR-05
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "UI/Narrative/NarrativeTextPolicy.h"
#include "UI/Narrative/NarrativeWidget.h"
#include "UI/Narrative/NarrativeSubsystem.h"
#include "UI/Narrative/NarrativeHUDActor.h"
#include "Tutorial/TutorialDirector.h"
#include "Tutorial/Steps/TutorialStep_ShowMessage.h"
#include "Tutorial/Steps/TutorialStep_WaitForServe.h"
#include "Components/WidgetComponent.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Tutorial/CyberCafeEventSubsystem.h"
#include "TimerManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNarrativePolicyTest, "CyberCafe.Narrative.TextPolicy", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNarrativePolicyTest::RunTest(const FString&)
{
    using namespace CyberCafeNarrative;
    TestEqual(TEXT("Chinese character"), NextBoundary(TEXT("牛奶"), 0), 1);
    FString Emoji;
    Emoji.AppendChar(static_cast<TCHAR>(0xD83E));
    Emoji.AppendChar(static_cast<TCHAR>(0xDD5B));
    Emoji.AppendChar(TEXT('!'));
    TestEqual(TEXT("No partial UTF16 emoji"), NextBoundary(Emoji, 0), 2);
    const FString Variation = FString(TEXT("A")) + static_cast<TCHAR>(0xFE0F) + TEXT("B");
    TestEqual(TEXT("Variation selector stays with character"), NextBoundary(Variation, 0), 2);
    const FString Combining = FString(TEXT("e")) + static_cast<TCHAR>(0x0301) + TEXT("x");
    TestEqual(TEXT("Combining mark stays attached"), NextBoundary(Combining, 0), 2);
    TestTrue(TEXT("Chinese punctuation pause"), PauseAfter(0xFF0C, .12f, .25f) > 0.f);
    TestTrue(TEXT("Sentence pause exceeds comma"), PauseAfter(0x3002, .12f, .25f) > PauseAfter(',', .12f, .25f));
    TestFalse(TEXT("Punctuation never voices"), IsVoiceCharacter(0x3002));
    TestFalse(TEXT("Whitespace never voices"), IsVoiceCharacter(' '));
    TestTrue(TEXT("Chinese voices"), IsVoiceCharacter(0x725B));
    TestTrue(TEXT("Reveal-only first press"), ResolveSkip(true, true, false) == ESkipAction::Reveal);
    TestTrue(TEXT("Second press advances"), ResolveSkip(false, true, false) == ESkipAction::Advance);
    TestTrue(TEXT("No skip for a task"), ResolveSkip(true, false, false) == ESkipAction::Ignore);
    TestTrue(TEXT("Grace ignores input"), ResolveSkip(true, true, true) == ESkipAction::Ignore);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNarrativeWidgetTest, "CyberCafe.Narrative.TypewriterLifecycle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNarrativeWidgetTest::RunTest(const FString&)
{
    UWorld::InitializationValues IV;
    IV.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::SM5, &IV);
    UNarrativeWidget* W = NewObject<UNarrativeWidget>(World);
    W->bEnableVoice = false;
    TestEqual(TEXT("Widget resolves test World"), W->GetWorld(), World);
    W->PlayTypewriter(FText::FromString(TEXT("牛奶，咖啡。")), 30.f);
    TestTrue(TEXT("Typing begins"), W->IsTyping());
    TestTrue(TEXT("Body begins empty"), W->GetVisibleText().IsEmpty());
    W->TickTypewriter();
    TestEqual(TEXT("First character"), W->GetVisibleText(), FString(TEXT("牛")));
    W->SkipTypewriter();
    TestFalse(TEXT("Reveal stops typing"), W->IsTyping());
    TestEqual(TEXT("Reveal completes text"), W->GetVisibleText(), FString(TEXT("牛奶，咖啡。")));
    const FString Full = W->GetVisibleText();
    W->SkipTypewriter();
    TestEqual(TEXT("Second reveal is no-op"), W->GetVisibleText(), Full);
    W->PlayTypewriter(FText::FromString(TEXT("旧文字")), 30.f);
    W->SetText(FText::FromString(TEXT("操作提示")));
    TestFalse(TEXT("Immediate replacement cancels old typing"), W->IsTyping());
    TestEqual(TEXT("Immediate body"), W->GetVisibleText(), FString(TEXT("操作提示")));
    W->PlayTypewriter(FText::FromString(TEXT("取消")), 30.f);
    W->StopTypewriter();
    TestFalse(TEXT("Hidden text has no pending typing"), W->IsTyping());
    TestFalse(TEXT("Timer cleared"), World->GetTimerManager().IsTimerActive(W->TypewriterTimer));
    W->PlayTypewriter(FText::GetEmpty(), 30.f);
    TestFalse(TEXT("Empty line completes immediately"), W->IsTyping());
    W->PlayTypewriter(FText::FromString(TEXT("零速")), 0.f);
    TestEqual(TEXT("Zero speed is immediate"), W->GetVisibleText(), FString(TEXT("零速")));
    W->PlayTypewriter(FText::FromString(TEXT("末尾")), 30.f);
    W->TickTypewriter(); W->TickTypewriter();
    TestFalse(TEXT("Natural completion"), W->IsTyping());
    World->DestroyWorld(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNarrativeFlowTest, "CyberCafe.Narrative.SkipAndTiming", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNarrativeFlowTest::RunTest(const FString&)
{
    UWorld::InitializationValues IV;
    IV.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::SM5, &IV);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    UGameInstance* GI = NewObject<UGameInstance>();
    World->SetGameInstance(GI);
    GI->Init();
    UCyberCafeEventSubsystem* Events = GI->GetSubsystem<UCyberCafeEventSubsystem>();
    if (!TestNotNull(TEXT("Real input event bus initialized"), Events)) { GI->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false); return false; }
    ANarrativeHUDActor* HUD = World->SpawnActor<ANarrativeHUDActor>();
    UNarrativeWidget* W = NewObject<UNarrativeWidget>(World);
    W->bEnableVoice = false;
    HUD->FindComponentByClass<UWidgetComponent>()->SetWidget(W);
    UNarrativeSubsystem* N = World->GetSubsystem<UNarrativeSubsystem>();
    ATutorialDirector* Director = World->SpawnActor<ATutorialDirector>();
    Director->bAutoStart = false;
    UTutorialStep_ShowMessage* Step = NewObject<UTutorialStep_ShowMessage>(Director);
    Step->HintText = FText::FromString(TEXT("欢迎，先认识工作台。"));
    Step->Duration = 1.f; Step->InputGracePeriod = 0.f;
    Step->EnterStep(Director);
    N->SetInputHintEnabled(true);
    N->PlayTypewriter(Step->HintText, 30.f);
    TestFalse(TEXT("Duration does not start while typing"), World->GetTimerManager().IsTimerActive(Step->DurationTimer));
    Events->OnPlayerSkipRequested.Broadcast();
    TestFalse(TEXT("First input reveals"), N->IsTyping());
    TestFalse(TEXT("First input does not complete step"), Step->IsCompleted());
    TestTrue(TEXT("Duration starts after reveal"), World->GetTimerManager().IsTimerActive(Step->DurationTimer));
    Events->OnPlayerSkipRequested.Broadcast();
    TestFalse(TEXT("Same-frame repeated input cannot advance"), Step->bAdvanceQueued);
    ++GFrameCounter;
    World->Tick(LEVELTICK_All, .2f);
    Events->OnPlayerSkipRequested.Broadcast();
    TestTrue(TEXT("Fresh second input queues advance"), Step->bAdvanceQueued);
    ++GFrameCounter;
    World->Tick(LEVELTICK_All, .02f);
    TestTrue(TEXT("Deferred advance executes next world tick"), Step->IsCompleted());
    Step->ExitStep();
    Step->HandlePlayerSkipRequested();
    TestFalse(TEXT("Exited snapshot cannot queue another completion"), Step->bAdvanceQueued);
    UTutorialStep_ShowMessage* Timed = NewObject<UTutorialStep_ShowMessage>(Director);
    Timed->HintText = FText::FromString(TEXT("牛")); Timed->Duration = .1f;
    Timed->EnterStep(Director);
    N->PlayTypewriter(Timed->HintText, 30.f);
    ++GFrameCounter; World->Tick(LEVELTICK_All, .04f);
    TestFalse(TEXT("Full reveal does not immediately end reading"), Timed->IsCompleted());
    ++GFrameCounter; World->Tick(LEVELTICK_All, .05f);
    TestFalse(TEXT("Read interval still running"), Timed->IsCompleted());
    ++GFrameCounter; World->Tick(LEVELTICK_All, .07f);
    TestTrue(TEXT("Read interval completes after reveal plus duration"), Timed->IsCompleted());
    Timed->ExitStep();
    UTutorialStep_ShowMessage* Cancelled = NewObject<UTutorialStep_ShowMessage>(Director);
    Cancelled->HintText = FText::FromString(TEXT("正在取消的长消息"));
    Cancelled->EnterStep(Director); N->PlayTypewriter(Cancelled->HintText, 30.f);
    Cancelled->ExitStep();
    TestFalse(TEXT("Exiting step cancels only owned reveal"), N->IsTyping());
    UTutorialStep_ShowMessage* Restarted = NewObject<UTutorialStep_ShowMessage>(Director);
    Restarted->HintText = FText::FromString(TEXT("旧消息"));
    Restarted->EnterStep(Director); N->PlayTypewriter(Restarted->HintText, 30.f);
    const uint32 OldSerial = N->GetPresentationSerial();
    Restarted->ExitStep();
    Restarted->HintText = FText::FromString(TEXT("新的消息正在逐字显示"));
    Restarted->EnterStep(Director); N->PlayTypewriter(Restarted->HintText, 30.f);
    Restarted->HandleOwnedPresentationFinished(OldSerial);
    TestFalse(TEXT("Old completion cannot start restarted step duration"), World->GetTimerManager().IsTimerActive(Restarted->DurationTimer));
    TestTrue(TEXT("Restarted line remains typing"), N->IsTyping());
    Restarted->ExitStep();
    TestFalse(TEXT("Exit clears duration"), World->GetTimerManager().IsTimerActive(Step->DurationTimer));
    TestFalse(TEXT("Exit clears deferred advance"), World->GetTimerManager().IsTimerActive(Step->AdvanceTimer));
    N->PlayTypewriter(FText::FromString(TEXT("自动隐藏测试")), 30.f, 1.f);
    TestFalse(TEXT("Hide timer waits for complete line"), World->GetTimerManager().IsTimerActive(N->AutoHideTimer));
    N->SkipTypewriter();
    TestTrue(TEXT("Hide timer starts after complete line"), World->GetTimerManager().IsTimerActive(N->AutoHideTimer));
    N->HideText();
    TestFalse(TEXT("Visibility follows component"), N->IsVisible());
    TestFalse(TEXT("Hiding clears auto-hide"), World->GetTimerManager().IsTimerActive(N->AutoHideTimer));
    TestFalse(TEXT("Hiding stops typing"), W->IsTyping());
    // Actual task steps remain immediate and do not gain a skip subscription.
    UTutorialStep_WaitForServe* Task = NewObject<UTutorialStep_WaitForServe>(Director);
    Task->HintText = FText::FromString(TEXT("做好后放下杯子"));
    Director->Steps = {Task};
    Director->StartTutorial();
    TestFalse(TEXT("Action task shows full text immediately"), N->IsTyping());
    TestFalse(TEXT("Action task stays incomplete"), Task->IsCompleted());
    Events->OnPlayerSkipRequested.Broadcast();
    TestFalse(TEXT("Real input cannot skip action task"), Task->IsCompleted());
    Director->SkipTutorial();
    GI->Shutdown();
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNarrativeAssetBindingTest, "CyberCafe.Narrative.SavedWidgetBindings", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNarrativeAssetBindingTest::RunTest(const FString&)
{
    UClass* WidgetClass = LoadClass<UNarrativeWidget>(nullptr, TEXT("/Game/VRTemplate/Blueprints/Tutorial/UI/WBP_NarrativeHUD.WBP_NarrativeHUD_C"));
    if (!TestNotNull(TEXT("Original widget blueprint reloads"), WidgetClass)) return false;
    UWorld::InitializationValues IV;
    IV.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::SM5, &IV);
    UNarrativeWidget* W = CreateWidget<UNarrativeWidget>(World, WidgetClass);
    if (!TestNotNull(TEXT("Real UMG instance created"), W)) { World->DestroyWorld(false); return false; }
    W->bEnableVoice = false;
    UTextBlock* Body = Cast<UTextBlock>(W->GetWidgetFromName(TEXT("Text_Body")));
    UTextBlock* Speaker = Cast<UTextBlock>(W->GetWidgetFromName(TEXT("Text_Speaker")));
    UTextBlock* Hint = Cast<UTextBlock>(W->GetWidgetFromName(TEXT("Text_ContinueHint")));
    if (!TestNotNull(TEXT("Body remains bound"), Body) || !TestNotNull(TEXT("Speaker remains bound"), Speaker) || !TestNotNull(TEXT("Continue hint bound"), Hint))
    { World->DestroyWorld(false); return false; }
    W->SetSpeaker(FText::FromString(TEXT("向导")));
    TestEqual(TEXT("Speaker binding writes"), Speaker->GetText().ToString(), FString(TEXT("向导")));
    W->SetSpeaker(FText::GetEmpty());
    TestTrue(TEXT("Empty speaker collapses"), Speaker->GetVisibility() == ESlateVisibility::Collapsed);
    W->SetInputHintEnabled(true);
    W->PlayTypewriter(FText::FromString(TEXT("牛奶和咖啡")), 30.f);
    TestTrue(TEXT("Actual body starts empty"), Body->GetText().IsEmpty());
    TestTrue(TEXT("Typing hint requests full reveal"), Hint->GetText().ToString().Contains(TEXT("完整")));
    W->SkipTypewriter();
    TestEqual(TEXT("Actual body receives complete line"), Body->GetText().ToString(), FString(TEXT("牛奶和咖啡")));
    TestTrue(TEXT("Finished hint now requests skip"), Hint->GetText().ToString().Contains(TEXT("跳过")));
    W->SetInputHintEnabled(false);
    TestTrue(TEXT("Task hint collapses"), Hint->GetVisibility() == ESlateVisibility::Collapsed);
    W->SetText(FText::FromString(TEXT("放下杯子")));
    TestEqual(TEXT("Task writes instantly"), Body->GetText().ToString(), FString(TEXT("放下杯子")));
    W->StopTypewriter();
    World->DestroyWorld(false);
    return true;
}
#endif
