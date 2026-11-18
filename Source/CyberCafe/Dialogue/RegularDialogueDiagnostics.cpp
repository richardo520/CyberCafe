#include "Dialogue/RegularDialogueDiagnostics.h"

FString RegularDialogueDiagnostics::SourceLabel(ERegularDialogueSource Source)
{
    if (Source == ERegularDialogueSource::AI) return TEXT("AI 台词");
    if (Source == ERegularDialogueSource::Fallback) return TEXT("已回退 · 本地台词");
    return TEXT("本地台词");
}

ERegularDialogueSource RegularDialogueDiagnostics::SourceForResult(bool bGenerated, const FString& Reason)
{
    if (bGenerated) return ERegularDialogueSource::AI;
    return Reason == TEXT("disabled_or_test_run") || Reason == TEXT("missing_key") || Reason == TEXT("authored_line")
        ? ERegularDialogueSource::Local : ERegularDialogueSource::Fallback;
}

FString RegularDialogueDiagnostics::ReasonLabel(const FString& Reason)
{
    if (Reason == TEXT("authored_line")) return TEXT("本地确定性台词");
    if (Reason == TEXT("not_requested")) return TEXT("尚无请求");
    if (Reason == TEXT("validated")) return TEXT("回复通过检查");
    if (Reason == TEXT("pending")) return TEXT("正在生成");
    if (Reason == TEXT("cancelled")) return TEXT("已取消");
    if (Reason == TEXT("disabled_or_test_run")) return TEXT("云端已关闭或当前为测试运行");
    if (Reason == TEXT("missing_key")) return TEXT("编辑器未读取到密钥");
    if (Reason == TEXT("invalid_key_format")) return TEXT("密钥格式无效");
    if (Reason == TEXT("invalid_configuration")) return TEXT("接口配置无效");
    if (Reason == TEXT("minute_budget")) return TEXT("最近60秒请求额度已用完");
    if (Reason == TEXT("provider_cooldown")) return TEXT("服务冷却中，稍后可恢复");
    if (Reason == TEXT("timeout")) return TEXT("请求超时");
    if (Reason == TEXT("transport_not_started") || Reason == TEXT("http_0")) return TEXT("网络请求未能完成");
    if (Reason == TEXT("http_401") || Reason == TEXT("http_403")) return TEXT("鉴权或访问权限失败");
    if (Reason == TEXT("http_429")) return TEXT("服务限流或账户额度不足");
    if (Reason.StartsWith(TEXT("http_"))) return TEXT("API 返回错误状态");
    if (Reason == TEXT("incomplete")) return TEXT("回复被截断或未完成");
    if (Reason == TEXT("refusal")) return TEXT("模型未提供台词");
    if (Reason == TEXT("invalid_response")) return TEXT("回复格式不符合要求");
    return TEXT("回复未通过台词约束检查");
}
