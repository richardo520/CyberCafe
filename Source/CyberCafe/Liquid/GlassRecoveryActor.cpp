#include "Liquid/GlassRecoveryActor.h"
#include "Liquid/GlassBreakableComponent.h"
#include "Liquid/LiquidContainerActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Character/VRPawn.h"
#include "MotionControllerComponent.h"
#include "GrabComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "UObject/ConstructorHelpers.h"
#include "Serving/DrinkRecipe.h"
#include "Tutorial/TutorialDirector.h"
#include "Tutorial/Steps/TutorialStep_Grab.h"
#include "Materials/MaterialInstanceDynamic.h"
AGlassRecoveryActor::AGlassRecoveryActor()
{
 PrimaryActorTick.bCanEverTick=true;
 auto* Root=CreateDefaultSubobject<USceneComponent>(TEXT("Root"));SetRootComponent(Root);
 static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
 Base=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RecoveryPad"));Base->SetupAttachment(Root);Base->SetStaticMesh(Cube.Object);Base->SetRelativeScale3D(FVector(.16,.10,.012));Base->SetRelativeLocation(FVector(0,0,.6));Base->SetCollisionEnabled(ECollisionEnabled::NoCollision);
 CupButton=CreateDefaultSubobject<UBoxComponent>(TEXT("CupButton"));CupButton->SetupAttachment(Root);CupButton->SetBoxExtent(FVector(2.5,2.8,1.5));CupButton->SetRelativeLocation(FVector(-4,0,2));CupButton->SetCollisionEnabled(ECollisionEnabled::NoCollision);
 BottleButton=CreateDefaultSubobject<UBoxComponent>(TEXT("BottleButton"));BottleButton->SetupAttachment(Root);BottleButton->SetBoxExtent(FVector(2.5,2.8,1.5));BottleButton->SetRelativeLocation(FVector(4,0,2));BottleButton->SetCollisionEnabled(ECollisionEnabled::NoCollision);
 for(int32 I=0;I<2;++I){auto* C=CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Key%d"),I));C->SetupAttachment(Root);C->SetStaticMesh(Cube.Object);C->SetRelativeScale3D(FVector(.045,.05,.007));C->SetRelativeLocation(FVector(I==0?-4:4,0,1.5));C->SetCollisionEnabled(ECollisionEnabled::NoCollision);Keys.Add(C);auto* T=CreateDefaultSubobject<UTextRenderComponent>(*FString::Printf(TEXT("Label%d"),I));T->SetupAttachment(Root);T->SetRelativeLocation(FVector(I==0?-7:1,-2.5,2));T->SetRelativeRotation(FRotator(90,90,0));T->SetWorldSize(1.1f);T->SetText(FText::FromString(I==0?TEXT("CUP"):TEXT("MILK")));Labels.Add(T);}
}
void AGlassRecoveryActor::BindPrototype(ALiquidContainerActor* Cup,ALiquidContainerActor* Bottle,UGlassBreakProfile* CupData,UGlassBreakProfile* BottleData)
{PrototypeCup=Cup;PrototypeBottle=Bottle;CupProfile=CupData;BottleProfile=BottleData;}
void AGlassRecoveryActor::Install(ALiquidContainerActor* A,UGlassBreakProfile* Data)
{
 if(!A||!Data||A->FindComponentByClass<UGlassBreakableComponent>())return;
 auto* Comp=NewObject<UGlassBreakableComponent>(A);Comp->Profile=Data;A->AddInstanceComponent(Comp);Comp->RegisterComponent();
}
void AGlassRecoveryActor::BeginPlay()
{
 Super::BeginPlay();Slots.SetNum(2);
 for(int32 I=0;I<2;++I){auto* A=I==0?PrototypeCup.Get():PrototypeBottle.Get();auto* P=I==0?CupProfile.Get():BottleProfile.Get();if(!A||!P)continue;auto& S=Slots[I];S.Current=A;S.Profile=P;S.ContainerClass=A->GetClass();S.SpawnTransform=A->GetActorTransform();S.InitialFill=A->FillAmount;S.Capacity=A->MaxVolumeML;S.Ingredient=A->InitialIngredientId;S.Color=A->LiquidColor;S.Mesh=A->ContainerMesh->GetStaticMesh();for(int32 M=0;M<A->ContainerMesh->GetNumMaterials();++M){UMaterialInterface* Material=A->ContainerMesh->GetMaterial(M);if(auto* MID=Cast<UMaterialInstanceDynamic>(Material))Material=MID->Parent;S.Materials.Add(Material);}Install(A,P);}
}
ALiquidContainerActor* AGlassRecoveryActor::GetCurrent(int32 I) const{return Slots.IsValidIndex(I)?Slots[I].Current.Get():nullptr;}
bool AGlassRecoveryActor::CanRecover(int32 I) const{return Slots.IsValidIndex(I)&&Slots[I].Current&&Slots[I].Current->IsBroken();}
bool AGlassRecoveryActor::Recover(int32 I)
{
 if(!CanRecover(I))return false;auto& S=Slots[I];
 // A manual reclaim adds a new configured supply, not a resurrection or a reset
 // of an in-flight packet. No intact vessel can be duplicated with this button.
 auto* A=GetWorld()->SpawnActorDeferred<ALiquidContainerActor>(S.ContainerClass,S.SpawnTransform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
 if(!A)return false;
 A->FillAmount=S.InitialFill;A->MaxVolumeML=S.Capacity;A->InitialIngredientId=S.Ingredient;A->LiquidColor=S.Color;A->bInitialVolumeIsReplacement=true;A->ContainerMesh->SetStaticMesh(S.Mesh);for(int32 M=0;M<S.Materials.Num();++M)A->ContainerMesh->SetMaterial(M,S.Materials[M]);A->FinishSpawning(S.SpawnTransform);
 A->InitializeLiquidState();A->ContainerMesh->SetVisibility(true,true);if(A->GrabComp)A->GrabComp->Activate();
 auto* Old=S.Current.Get();S.Current=A;Install(A,S.Profile);
 for(TActorIterator<ATutorialDirector> D(GetWorld());D;++D)for(const auto& Step:D->Steps)if(auto* G=Cast<UTutorialStep_Grab>(Step.Get()))if(G->RequiredActor==Old)G->RequiredActor=A;
 if(Old)Old->Destroy();return true;
}
void AGlassRecoveryActor::Tick(float Dt)
{
 Super::Tick(Dt);
 // No menu/input remapping. Both buttons only work while that prototype is broken.
 for(int32 I=0;I<2;++I){Labels[I]->SetTextRenderColor(CanRecover(I)?FColor(255,190,70):FColor(95,110,110));UBoxComponent* Box=I==0?CupButton.Get():BottleButton.Get();bool AnyInside=false;
  for(TActorIterator<AVRPawn> It(GetWorld());It;++It){AVRPawn* Pawn=*It;if(!Pawn->IsLocallyControlled())continue;
   for(int32 Hand=0;Hand<2;++Hand){auto* Grip=Hand==0?Pawn->MotionControllerLeftGrip.Get():Pawn->MotionControllerRightGrip.Get();auto* Held=Hand==0?Pawn->HeldComponentLeft.Get():Pawn->HeldComponentRight.Get();if(!Grip||!Grip->IsTracked()||(Held&&Held->IsHeld()))continue;auto* Mesh=Hand==0?Pawn->HandLeft.Get():Pawn->HandRight.Get();FName Bone=Hand==0?TEXT("index_03_l"):TEXT("index_03_r");const FVector P=Mesh&&Mesh->DoesSocketExist(Bone)?Mesh->GetSocketLocation(Bone):Grip->GetComponentLocation();const FVector L=Box->GetComponentTransform().InverseTransformPosition(P);const FVector E=Box->GetUnscaledBoxExtent();if(FMath::Abs(L.X)<E.X+2&&FMath::Abs(L.Y)<E.Y+2&&FMath::Abs(L.Z)<E.Z+2)AnyInside=true;if(FMath::Abs(L.X)<E.X&&FMath::Abs(L.Y)<E.Y&&FMath::Abs(L.Z)<E.Z&&!Slots[I].bLatched){Slots[I].bLatched=true;Recover(I);}}
  }if(!AnyInside)Slots[I].bLatched=false;
 }
}
