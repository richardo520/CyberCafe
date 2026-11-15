#include "Liquid/LiquidInventoryComponent.h"

double ULiquidInventoryComponent::Debit(double RequestedML, FLiquidComposition* OutComposition)
{
    if (!FMath::IsFinite(RequestedML) || RequestedML <= 0) return 0;
    const double Actual = FMath::Min(RequestedML, VolumeML);
    if (Actual <= 0) return 0;
    const FLiquidComposition Taken = Contents.Portion(Actual);
    if (OutComposition) *OutComposition = Taken;
    Contents.Remove(Taken);
    VolumeML -= Actual;
    if (VolumeML == 0) Contents.Ingredients.Empty();
    ++Revision;
    return Actual;
}

double ULiquidInventoryComponent::Credit(double RequestedML, double CapacityML, FLinearColor& Color,
    const FLinearColor& InColor, const FLiquidComposition* InComposition)
{
    if (!FMath::IsFinite(RequestedML) || RequestedML <= 0 || !FMath::IsFinite(InColor.R) ||
        !FMath::IsFinite(InColor.G) || !FMath::IsFinite(InColor.B) || !FMath::IsFinite(InColor.A)) return 0;
    const double Capacity = FMath::IsFinite(CapacityML) ? FMath::Max(0.0, CapacityML) : 0;
    const double Actual = FMath::Min(RequestedML, FMath::Max(0.0, Capacity - VolumeML));
    if (Actual <= 0) return 0;
    const FLiquidComposition Received = InComposition ? InComposition->Portion(Actual) : FLiquidComposition::Pure(TEXT("Unknown"), Actual);
    if (FMath::Abs(Received.Total() - Actual) > 1.e-6) return 0;
    Contents.Add(Received);
    ++Revision;
    const double Total = VolumeML + Actual;
    Color = Color * static_cast<float>(VolumeML / Total) + InColor * static_cast<float>(Actual / Total);
    Color.A = 1.f;
    VolumeML = Total;
    return Actual;
}
