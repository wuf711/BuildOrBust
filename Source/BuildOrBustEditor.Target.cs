// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class BuildOrBustEditorTarget : TargetRules
{
	public BuildOrBustEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_7;
		ExtraModuleNames.Add("BuildOrBust");

		// 源码含 UTF-8 中文注释：强制 MSVC 按 UTF-8 读取，根治编辑器内编译（GBK 代码页）
		// 误读多字节尾字节为续行符导致的 C1075（仅 Win64；Clang 默认即 UTF-8）。
		// 编辑器目标与引擎共享构建环境，修改编译参数必须声明覆盖（UBT 要求）
		if (Platform == UnrealTargetPlatform.Win64)
		{
			bOverrideBuildEnvironment = true;
			AdditionalCompilerArguments += " /utf-8";
		}
	}
}
