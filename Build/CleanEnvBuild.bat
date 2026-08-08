@echo off
rem ============================================================================
rem  CyberCafe (UE5) build wrapper
rem  ----------------------------------------------------------------------------
rem  Purpose:
rem    Visual Studio 2022 injects MSBuildSDKsPath=INVALID_SDK_DIR into the
rem    child process environment when it launches a Makefile-style vcxproj
rem    build. This poisons UE's bundled dotnet (10.0.203) so that the internal
rem    `dotnet msbuild UnrealBuildTool.sln -t:Scan` call in DotnetDepends.bat
rem    fails to resolve $(MSBuildSDKsPath) and errors out with:
rem
rem      Microsoft.NET.Sdk.Solution.targets(14,3): error MSB4019 :
rem      import "$(MSBuildSDKsPath)\Microsoft.NET.Sdk\targets\...common.targets"
rem      resolves to "INVALID_SDK_DIR\..." which does not exist.
rem
rem    This wrapper clears the poisoned variable BEFORE delegating to UE's
rem    real Build.bat so that dotnet can resolve the SDK correctly.
rem
rem  Scope:
rem    Only this project (CyberCafe) references this wrapper via
rem    Directory.Build.props override of $(BuildBatchScript). Other UE
rem    projects (including UE4.26 legacy ones) are unaffected.
rem ============================================================================

set "MSBuildSDKsPath="
set "MSBuildExtensionsPath="
set "MSBuildToolsPath="
set "MSBuildToolsPath32="
set "MSBuildToolsPath64="
set "MSBuildToolsRoot="
set "MSBuildFrameworkToolsPath="
set "MSBuildFrameworkToolsPath32="
set "MSBuildFrameworkToolsPath64="
set "MSBuildLoadMicrosoftTargetsReadOnly="
set "MSBuildEnableWorkloadResolver="

rem Delegate to UE's real Build.bat with all original arguments untouched.
call "E:\UnrealEngine\Engine\Build\BatchFiles\Build.bat" %*
exit /B %ERRORLEVEL%
