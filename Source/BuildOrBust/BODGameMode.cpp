#include "BODGameMode.h"
#include "BODPlayerState.h"
#include "GameFramework/PlayerController.h"
#include "EngineUtils.h"

ABODGameMode::ABODGameMode()
{
	// 在项目设置中指定默认PlayerState为BODPlayerState
}

void ABODGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);
	AlivePlayerCount++;
}

void ABODGameMode::NotifyEnemyKilled(APlayerController* Killer)
{
	if (!Killer) return;

	if (ABODPlayerState* PS = Killer->GetPlayerState<ABODPlayerState>())
	{
		PS->AddKill();
	}
}

void ABODGameMode::NotifyPlayerDied(APlayerController* DeadPlayer)
{
	AlivePlayerCount = FMath::Max(0, AlivePlayerCount - 1);
	CheckGameOver();
}

void ABODGameMode::CheckGameOver()
{
	if (AlivePlayerCount <= 0)
	{
		// 广播游戏结束给所有客户端（在蓝图中处理UI）
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* PC = It->Get();
			if (PC)
			{
				// 触发蓝图事件
				PC->ClientTravel(TEXT("GameOver"), ETravelType::TRAVEL_Absolute);
			}
		}
	}
}
