#include "Liquid/GlassEditorLibrary.h"
#if WITH_EDITOR
#include "Liquid/GlassRecoveryActor.h"
#include "Liquid/GlassBreakableComponent.h"
#include "Liquid/LiquidContainerActor.h"
#include "Engine/World.h"
#include "Engine/Level.h"
AGlassRecoveryActor* UGlassEditorLibrary::InstallPrototype(UWorld* World,UGlassBreakProfile* CupProfile,UGlassBreakProfile* BottleProfile)
{
 if(!World||World->IsGameWorld()||!World->PersistentLevel||!CupProfile||!BottleProfile)return nullptr;
 ALiquidContainerActor* Cup=nullptr;ALiquidContainerActor* Bottle=nullptr;
 for(auto& A:World->PersistentLevel->Actors){if(!A)continue;if(Cast<AGlassRecoveryActor>(A))return nullptr;if(A->GetFName()==TEXT("BP_Cup01_C_1"))Cup=Cast<ALiquidContainerActor>(A);if(A->GetFName()==TEXT("BP_Bottle04_Milk_Tutorial"))Bottle=Cast<ALiquidContainerActor>(A);}
 if(!Cup||!Bottle)return nullptr;
 FActorSpawnParameters Params;Params.Name=TEXT("GlassRecovery_Tutorial");Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
 auto* M=World->SpawnActor<AGlassRecoveryActor>(FVector(-60,92,92),FRotator::ZeroRotator,Params);
 if(M){M->BindPrototype(Cup,Bottle,CupProfile,BottleProfile);M->SetActorLabel(TEXT("Glass prototype recovery - CUP / MILK"));M->MarkPackageDirty();World->MarkPackageDirty();}return M;
}
#endif
