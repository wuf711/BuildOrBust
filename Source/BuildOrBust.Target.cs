// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class BuildOrBustTarget : TargetRules
{
	public BuildOrBustTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_7;
		ExtraModuleNames.Add("BuildOrBust");

		// 源码含 UTF-8 中文注释：强制 MSVC 按 UTF-8 读取，避免中文 Windows 的 GBK
		// 代码页把多字节尾字节误读成续行符导致 C1075（仅 Win64；Clang 默认即 UTF-8）。
		// 与引擎共享构建环境，修改编译参数必须声明覆盖（UBT 要求）
		if (Platform == UnrealTargetPlatform.Win64)
		{
			bOverrideBuildEnvironment = true;
			AdditionalCompilerArguments += " /utf-8";
		}
	}
}
