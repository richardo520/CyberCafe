#include "Serving/DrinkMenuData.h"
#include "Serving/DrinkRecipe.h"
#include "Liquid/LiquidContainerActor.h"
bool UDrinkMenuData::IsValidMenu() const
{
 if(Entries.IsEmpty()||Entries.Num()>10)return false;
 TSet<FName> Seen;FString Previous;
 for(const auto& E:Entries)
 {
  const FString Code=E.PinyinIndex.ToString();if(E.PinyinIndex.IsNone()||Seen.Contains(E.PinyinIndex)||E.DisplayName.IsEmpty()||!IsValid(E.Recipe)||E.Ingredients.IsEmpty()||Code!=Code.ToUpper()||(!Previous.IsEmpty()&&Previous>=Code))return false;
  for(TCHAR C:Code)if(C<TEXT('A')||C>TEXT('Z'))return false;
  TArray<FString> Syllables;E.Pinyin.ParseIntoArrayWS(Syllables);FString Initials;for(const auto& S:Syllables)if(!S.IsEmpty())Initials.AppendChar(FChar::ToUpper(S[0]));if(Initials!=Code)return false;
  double Total=0;TSet<FName> Ingredients;
  for(const auto& I:E.Ingredients){if(I.IngredientId.IsNone()||Ingredients.Contains(I.IngredientId)||!FMath::IsFinite(I.VolumeML)||I.VolumeML<=0)return false;Ingredients.Add(I.IngredientId);Total+=I.VolumeML;}
  const UDrinkRecipe* R=E.Recipe;
  if(!FMath::IsFinite(R->TargetVolumeML)||R->TargetVolumeML<=0||!FMath::IsFinite(R->MinVolumeML)||!FMath::IsFinite(R->MaxVolumeML)||R->MinVolumeML>R->TargetVolumeML||R->MaxVolumeML<R->TargetVolumeML||FMath::Abs(Total-R->TargetVolumeML)>.001||R->Rules.Num()!=Ingredients.Num())return false;
  for(const auto& Rule:R->Rules){const auto* I=E.Ingredients.FindByPredicate([&](const FDrinkMenuIngredient& A){return A.IngredientId==Rule.IngredientId;});if(!I||!FMath::IsFinite(Rule.MinFraction)||!FMath::IsFinite(Rule.MaxFraction)||Rule.MinFraction<0||Rule.MaxFraction>1||Rule.MinFraction>Rule.MaxFraction||I->VolumeML/Total<Rule.MinFraction||I->VolumeML/Total>Rule.MaxFraction)return false;}
  if(R->AcceptedCupClass&&Total>R->AcceptedCupClass->GetDefaultObject<ALiquidContainerActor>()->MaxVolumeML+.001)return false;
  Seen.Add(E.PinyinIndex);Previous=Code;
 }
 return true;
}
int32 UDrinkMenuData::FindPinyinIndex(FName Index) const
{
 if(Index.IsNone())return INDEX_NONE;
 return Entries.IndexOfByPredicate([&](const FDrinkMenuEntry& E){return E.PinyinIndex==Index;});
}
FText UDrinkMenuData::IngredientDisplayName(FName Id)
{
 const TMap<FName,FString> Names={{TEXT("Coffee"),TEXT("咖啡液")},{TEXT("Milk"),TEXT("牛奶")},{TEXT("Water"),TEXT("调配水")},{TEXT("Gin"),TEXT("金酒")},{TEXT("Vodka"),TEXT("伏特加")},{TEXT("WhiteRum"),TEXT("白朗姆")},{TEXT("Tequila"),TEXT("龙舌兰")},{TEXT("OrangeSyrup"),TEXT("橙味糖浆")},{TEXT("StrawberrySyrup"),TEXT("草莓糖浆")},{TEXT("CoconutSyrup"),TEXT("椰子糖浆")}};
 if(const auto* Name=Names.Find(Id))return FText::FromString(*Name);return FText::FromName(Id);
}
