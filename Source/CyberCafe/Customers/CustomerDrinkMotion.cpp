#include "Customers/CustomerDrinkMotion.h"
#include "Animation/AnimSequence.h"
bool UCustomerDrinkMotion::IsValidMotion() const
{
 if(!Animation||!SupportedCupMesh||!FMath::IsFinite(SampleRate)||SampleRate<=0||CupSamples.Num()<2||SupportedCupScale.ContainsNaN()||SupportedCupScale.GetMin()<=0)return false;
 const float D=Duration();if(!FMath::IsFinite(D)||D<=0||FMath::Abs(D-Animation->GetPlayLength())>.05f)return false;
 if(!(0<=GraspTime&&GraspTime<SipStart&&SipStart<SipEnd&&SipEnd<ReleaseTime&&ReleaseTime<D)||!FMath::IsFinite(SipVolumeML)||SipVolumeML<=0||!FMath::IsFinite(MaxStartOffsetCM)||MaxStartOffsetCM<0)return false;
 for(const auto& T:CupSamples)if(T.ContainsNaN()||!T.GetScale3D().Equals(FVector::OneVector,.001))return false;
 if(!FMath::IsFinite(LiftBlendEnd)||!FMath::IsFinite(ReturnBlendStart)||LiftBlendEnd<=GraspTime||LiftBlendEnd>ReleaseTime||ReturnBlendStart<GraspTime||ReturnBlendStart>=ReleaseTime||!FMath::IsFinite(SipSoundVolume)||SipSoundVolume<0)return false;
 for(const auto& Cue:BlinkCues)if(!FMath::IsFinite(Cue.Time)||!FMath::IsFinite(Cue.HalfWidth)||Cue.HalfWidth<=0)return false;
 return true;
}
float UCustomerDrinkMotion::BlinkWeight(float Seconds) const
{
 float Weight=0;
 for(const auto& Cue:BlinkCues)Weight=FMath::Max(Weight,FMath::Clamp(1.f-FMath::Abs(Seconds-Cue.Time)/FMath::Max(Cue.HalfWidth,UE_SMALL_NUMBER),0.f,1.f));
 return Weight;
}
float UCustomerDrinkMotion::GripBlend(float Seconds) const
{
 return FMath::Min(FMath::SmoothStep(GraspTime,LiftBlendEnd,Seconds),1.f-FMath::SmoothStep(ReturnBlendStart,ReleaseTime,Seconds));
}
float UCustomerDrinkMotion::Duration() const{return SampleRate>0?(CupSamples.Num()-1)/SampleRate:0;}
FTransform UCustomerDrinkMotion::Sample(float Seconds) const
{
 if(CupSamples.IsEmpty()||!FMath::IsFinite(Seconds)||SampleRate<=0)return FTransform::Identity;
 const double F=FMath::Clamp(static_cast<double>(Seconds)*SampleRate,0.0,static_cast<double>(CupSamples.Num()-1));const int A=FMath::FloorToInt(F);const int B=FMath::Min(A+1,CupSamples.Num()-1);FTransform T;T.Blend(CupSamples[A],CupSamples[B],F-A);return T;
}
