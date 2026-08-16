// Fill out your copyright notice in the Description page of Project Settings.

using UnrealBuildTool;

public class CyberCafe : ModuleRules
{
	public CyberCafe(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		// VR项目常用模块：
		// - HeadMountedDisplay：UMotionControllerComponent
		// - XRBase：UVRNotificationsComponent 等VR基础运行时类型
		// - EnhancedInput：Enhanced Input系统（IA / IMC）
		// - UMG：VR UI交互
		// - Niagara：Teleport Trace VFX
		// - NavigationSystem：Teleport点投影到导航网格
		// - PhysicsCore：物理相关类型
		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine", "InputCore",
			"HeadMountedDisplay", "XRBase", "EnhancedInput",
			"UMG", "Niagara", "NavigationSystem", "PhysicsCore"
		});

		PrivateDependencyModuleNames.AddRange(new string[] {  });

		// 显式将模块根目录及所有子目录加入头文件搜索路径，
		// 避免因非Public/Private目录结构导致的include定位失败
		PublicIncludePaths.AddRange(new string[] {
			ModuleDirectory,
			System.IO.Path.Combine(ModuleDirectory, "Character")
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });
		
		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
