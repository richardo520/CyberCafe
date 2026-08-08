@echo off
rem See CleanEnvBuild.bat for full description.
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

call "E:\UnrealEngine\Engine\Build\BatchFiles\Rebuild.bat" %*
exit /B %ERRORLEVEL%
