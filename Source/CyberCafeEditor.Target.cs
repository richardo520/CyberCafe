// Fill out your copyright notice in the Description page of Project Settings.

using UnrealBuildTool;
using System.Collections.Generic;

public class CyberCafeEditorTarget : TargetRules
{
	public CyberCafeEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V5;   // 加这行（或把旧的 V2/V3/V4 改成 V7）
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest; 

		ExtraModuleNames.AddRange( new string[] { "CyberCafe" } );
	}
}
