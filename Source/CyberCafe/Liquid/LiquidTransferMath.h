// @trace LQ-03
#pragma once
#include "CoreMinimal.h"

namespace CyberCafeLiquid
{
inline FTransform Interpolate(const FTransform& A,const FTransform& B,double T)
{
    return FTransform(FQuat::Slerp(A.GetRotation(),B.GetRotation(),T).GetNormalized(),FMath::Lerp(A.GetLocation(),B.GetLocation(),T),FMath::Lerp(A.GetScale3D(),B.GetScale3D(),T));
}
// Captured from P_Ribbon FloatFromCurve002/003/004 (UE5.6.1).
// These are age-driven displacements, NOT an acceleration of -980.
inline double Hermite(double T,double T0,double T1,double V0,double V1,double M0,double M1)
{
    const double U=FMath::Clamp((T-T0)/(T1-T0),0.0,1.0),U2=U*U,U3=U2*U;
    return (2*U3-3*U2+1)*V0+(U3-2*U2+U)*(T1-T0)*M0+(-2*U3+3*U2)*V1+(U3-U2)*(T1-T0)*M1;
}
inline FVector RibbonDisplacement(double Age,const FVector& Axis,double Velocity=20,double Offset=3)
{
    const double Startup=FMath::Lerp(-1.0,0.0,FMath::Clamp(Age/.15,0.0,1.0));
    const double Forward=Hermite(Age,.15,1,0,1,2.248709,0);
    const double Down=Hermite(Age,.15,5,0,5,0,.980113);
    return Axis*(Offset*Startup+Velocity*Forward)-FVector::UpVector*(980*Down);
}
inline bool CrossMouth(const FVector& Start,const FVector& End,const FTransform& PreviousMouth,const FTransform& CurrentMouth,double LocalRadius,double& OutAlpha)
{
    if(!FMath::IsFinite(LocalRadius)||LocalRadius<=0)return false;
    auto LocalAt=[&](double T){return Interpolate(PreviousMouth,CurrentMouth,T).InverseTransformPosition(FMath::Lerp(Start,End,T));};
    // Evaluate the actual interpolated mouth pose, not a chord between two
    // incompatible local coordinate systems. Tiny substeps bracket the root.
    const FVector A=LocalAt(0),B=LocalAt(1);
    if(A.Z < -1.e-6 || B.Z >= -1.e-6 || A.Z-B.Z <= 1.e-9)return false;
    if (PreviousMouth.Equals(CurrentMouth, 1.e-9))
    {
        const double T=FMath::Clamp(A.Z/(A.Z-B.Z),0.0,1.0);
        const FVector P=FMath::Lerp(A,B,T);
        if(P.X*P.X+P.Y*P.Y>LocalRadius*LocalRadius)return false;
        OutAlpha=T;return true;
    }
    double Lo=0,Hi=1;
    for(int I=0;I<32;++I)
    {
        const double Mid=(Lo+Hi)*.5;
        if(LocalAt(Mid).Z>0)Lo=Mid;else Hi=Mid;
    }
    const double T=(Lo+Hi)*.5;
    const FVector P=LocalAt(T);
    if(P.X*P.X+P.Y*P.Y>LocalRadius*LocalRadius)return false;
    OutAlpha=T;return true;
}
}
