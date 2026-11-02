// @trace NAR-01 NAR-02 -- Pure, testable reveal/cadence/input policy.
#pragma once
#include "CoreMinimal.h"
#include "Internationalization/BreakIterator.h"

namespace CyberCafeNarrative
{
struct FScalar { uint32 Value; int32 Units; };
inline FScalar ScalarAt(const FString& Text, int32 Index)
{
    if (!Text.IsValidIndex(Index)) return {0, 0};
    const uint32 A = static_cast<uint16>(Text[Index]);
    if (A >= 0xD800 && A <= 0xDBFF && Text.IsValidIndex(Index + 1))
    {
        const uint32 B = static_cast<uint16>(Text[Index + 1]);
        if (B >= 0xDC00 && B <= 0xDFFF) return {0x10000 + ((A - 0xD800) << 10) + B - 0xDC00, 2};
    }
    return {A, 1};
}
inline bool IsExtender(uint32 C)
{
    return (C >= 0x0300 && C <= 0x036F) || (C >= 0xFE00 && C <= 0xFE0F) ||
        (C >= 0x1F3FB && C <= 0x1F3FF) || (C >= 0xE0100 && C <= 0xE01EF);
}
inline int32 NextBoundary(const FString& Text, int32 Index)
{
    if (Index >= Text.Len()) return Text.Len();
    const TSharedRef<IBreakIterator> Iterator = FBreakIterator::CreateCharacterBoundaryIterator();
    Iterator->SetString(Text);
    const int32 End = Iterator->MoveToCandidateAfter(FMath::Max(0, Index));
    return End == INDEX_NONE ? Text.Len() : End;
}
inline float PauseAfter(uint32 C, float CommaPause, float SentencePause)
{
    if (C == ',' || C == ';' || C == ':' || C == 0xFF0C || C == 0x3001 || C == 0xFF1B || C == 0xFF1A) return FMath::Max(0.f, CommaPause);
    if (C == '.' || C == '!' || C == '?' || C == 0x3002 || C == 0xFF01 || C == 0xFF1F || C == 0x2026) return FMath::Max(0.f, SentencePause);
    if (C == '\n') return FMath::Max(0.f, CommaPause);
    return 0.f;
}
inline bool IsVoiceCharacter(uint32 C)
{
    if (C >= 0x4E00 && C <= 0x9FFF) return true;
    if (C >= 0x3400 && C <= 0x4DBF) return true;
    if (C >= 0x20000 && C <= 0x3134F) return true;
    if (C < 128) return (C >= 'A' && C <= 'Z') || (C >= 'a' && C <= 'z') || (C >= '0' && C <= '9');
    return C >= 0x00C0 && C <= 0x02AF && C != 0x00D7 && C != 0x00F7;
}
enum class ESkipAction : uint8 { Ignore, Reveal, Advance };
inline ESkipAction ResolveSkip(bool bTyping, bool bAllowed, bool bInGrace)
{
    if (!bAllowed || bInGrace) return ESkipAction::Ignore;
    return bTyping ? ESkipAction::Reveal : ESkipAction::Advance;
}
}
