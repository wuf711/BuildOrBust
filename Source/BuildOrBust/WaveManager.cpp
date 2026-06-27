#include "WaveManager.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "ExperienceComponent.h"
#include "UpgradeComponent.h"
#include "UpgradeTypes.h"
#include "BaseCore.h"
#include "Variant_Shooter/ShooterGameMode.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"

AWaveManager::AWaveManager()
{
	bReplicates = true;
	PrimaryActorTick.bCanEverTick = false;
}

void AWaveManager::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AWaveManager, CurrentWave);
}

void AWaveManager::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		FTimerHandle StartTimer;
		GetWorld()->GetTimerManager().SetTimer(
			StartTimer, this, &AWaveManager::StartNextWave, InitialDelay, false);
	}
}

int32 AWaveManager::GetAliveEnemyCount() const
{
	int32 Count = 0;
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AShooterNPC> It(World); It; ++It)
		{
			AShooterNPC* NPC = *It;
			if (IsValid(NPC) && !NPC->IsActorBeingDestroyed() && !NPC->IsDead())
			{
				Count++;
			}
		}
	}
	return Count;
}

void AWaveManager::StartNextWave()
{
	CurrentWave++;

	// 超过最大波次 → 胜利
	if (CurrentWave > MaxWave)
	{
		OnAllWavesComplete.Broadcast();
		return;
	}

	FWaveConfig Config = BuildWaveConfig(CurrentWave);

	OnWaveStart.Broadcast(CurrentWave);

	const int32 Spawned = SpawnEnemies(Config);

	UE_LOG(LogTemp, Log, TEXT("第 %d 波开始，生成 %d 个敌人"), CurrentWave, Spawned);

	bWaveActive = true;

	// 启动循环监控
	GetWorld()->GetTimerManager().SetTimer(
		MonitorTimer, this, &AWaveManager::MonitorWave, MonitorInterval, true);
}

void AWaveManager::MonitorWave()
{
	const int32 Alive = GetAliveEnemyCount();

	if (bWaveActive && Alive <= 0)
	{
		bWaveActive = false;
		GetWorld()->GetTimerManager().ClearTimer(MonitorTimer);
		OnWaveCleared();
	}
}

void AWaveManager::OnWaveCleared()
{
	OnWaveComplete.Broadcast(CurrentWave);


	if (CurrentWave >= MaxWave)
	{
		OnAllWavesComplete.Broadcast();

		// 触发胜利结算
		if (AShooterGameMode* GM = Cast<AShooterGameMode>(GetWorld()->GetAuthGameMode()))
		{
			GM->TriggerWin();
		}

		return;
	}

	// ① 中央核心自动升级并回满（守家自动化：基地不再是玩家增益的投入对象，玩家增益专注战斗/PVP）
	for (TActorIterator<ABaseCore> It(GetWorld()); It; ++It)
	{
		It->AutoUpgradeAndHeal();
		break;
	}

	// ② 波间空档（无敌人）给全体玩家同时发一次三选一增益，避免战斗中选卡卡顿
	OfferUpgradesToAllPlayers();

	FTimerHandle TimerHandle;
	GetWorld()->GetTimerManager().SetTimer(
		TimerHandle, this, &AWaveManager::OnWaveIntervalEnd, WaveInterval, false);
}

void AWaveManager::OfferUpgradesToAllPlayers()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC)
		{
			continue;
		}
		APawn* Pawn = PC->GetPawn();
		if (!Pawn)
		{
			continue;
		}
		if (UExperienceComponent* Exp = Pawn->FindComponentByClass<UExperienceComponent>())
		{
			// Client RPC：只在拥有该角色的客户端弹卡（host 本地立即执行）
			Exp->Client_OfferUpgrade();
		}
	}
}

void AWaveManager::OnWaveIntervalEnd()
{
	StartNextWave();
}

void AWaveManager::HandleEnemyDeath(AController* Killer, int32 ScoreValue)
{
	if (!Killer)
	{
		return;
	}

	APawn* KillerPawn = Killer->GetPawn();
	if (!KillerPawn)
	{
		return;
	}

	// 加经验
	if (UExperienceComponent* Exp = KillerPawn->FindComponentByClass<UExperienceComponent>())
	{
		Exp->AddExperience(BaseExpPerKill);
	}

	// 连击倍率
	float ComboMult = 1.0f;
	if (AShooterCharacter* SC = Cast<AShooterCharacter>(KillerPawn))
	{
		SC->AddCombo();
		ComboMult = SC->GetComboMultiplier();
	}

	// 得分 = 该丧尸基础分值 × 连击倍率 × 得分增益
	float ScoreF = ScoreValue * ComboMult;
	bool bCritScore = false;
	if (UUpgradeComponent* Up = KillerPawn->FindComponentByClass<UUpgradeComponent>())
	{
		// 赏金猎人：得分 +30%/层
		ScoreF *= (1.0f + 0.30f * Up->GetUpgradeCount(EUpgradeType::ScoreBoost));
		// 致命彩头：每层 25% 概率本次击杀得分翻倍
		bCritScore = Up->HasUpgrade(EUpgradeType::CritScore) &&
			FMath::FRand() < FMath::Min(0.25f * Up->GetUpgradeCount(EUpgradeType::CritScore), 1.0f);
	}
	int32 GainedScore = FMath::Max(1, FMath::RoundToInt(ScoreF));
	if (bCritScore)
	{
		GainedScore *= 2;
	}

	// 加分：单人 HUD 用的累计分
	if (AShooterGameMode* GM = Cast<AShooterGameMode>(GetWorld()->GetAuthGameMode()))
	{
		GM->AddPlayerScore(GainedScore);
	}

	// 多人同场竞技：把分数计入击杀者自己的 PlayerState（内置复制，供计分板各自比高低）
	if (APlayerState* KillerPS = Killer->PlayerState)
	{
		KillerPS->SetScore(KillerPS->GetScore() + GainedScore);
	}
}

FWaveConfig AWaveManager::BuildWaveConfig(int32 WaveNumber) const
{
	FWaveConfig Config;
	Config.EnemyCount       = 5 + (WaveNumber - 1) * 3;
	Config.SpeedMultiplier  = FMath::Min(1.0f + (WaveNumber - 1) * 0.05f, 1.5f);
	Config.HealthMultiplier = 1.0f + (WaveNumber - 1) * 0.15f;
	Config.ExpPerKill       = BaseExpPerKill + WaveNumber * 2;
	return Config;
}

int32 AWaveManager::SpawnEnemies(const FWaveConfig& Config)
{
	int32 SpawnedCount = 0;

	// 至少要有一种敌人和一个刷新点
	if ((EnemyTypes.Num() == 0 && !EnemyClass) || SpawnPoints.Num() == 0)
	{
		return SpawnedCount;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return SpawnedCount;
	}

	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(World);

	for (int32 i = 0; i < Config.EnemyCount; i++)
	{
		AActor* SpawnPoint = SpawnPoints[i % SpawnPoints.Num()];
		if (!SpawnPoint)
		{
			continue;
		}

		// 刷新点附近随机水平偏移
		FVector Base = SpawnPoint->GetActorLocation();
		Base.X += FMath::RandRange(-150.f, 150.f);
		Base.Y += FMath::RandRange(-150.f, 150.f);

		// 投影到导航网格，确保落在可行走地面。投影失败则跳过，绝不刷出会掉出世界的敌人
		FVector Location;
		bool bValidLocation = false;
		if (NavSys)
		{
			FNavLocation NavLoc;
			if (NavSys->ProjectPointToNavigation(Base, NavLoc, FVector(800.f, 800.f, 800.f)))
			{
				Location = NavLoc.Location + FVector(0.f, 0.f, 95.f);
				bValidLocation = true;
			}
			else if (NavSys->ProjectPointToNavigation(SpawnPoint->GetActorLocation(), NavLoc, FVector(800.f, 800.f, 800.f)))
			{
				Location = NavLoc.Location + FVector(0.f, 0.f, 95.f);
				bValidLocation = true;
			}
		}

		if (!bValidLocation)
		{
			// 这个刷新点附近没有可行走地面，跳过，避免敌人掉出世界
			continue;
		}

		// 随机选一种敌人（种类池优先，否则用兜底单一类）
		TSubclassOf<AShooterNPC> ChosenType = EnemyClass;
		if (EnemyTypes.Num() > 0)
		{
			ChosenType = EnemyTypes[FMath::RandRange(0, EnemyTypes.Num() - 1)];
		}
		if (!ChosenType)
		{
			continue;
		}

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

		AShooterNPC* Enemy = World->SpawnActor<AShooterNPC>(
			ChosenType, Location, SpawnPoint->GetActorRotation(), Params);

		if (Enemy)
		{
			// 波次倍率
			Enemy->CurrentHP *= Config.HealthMultiplier;

			if (UCharacterMovementComponent* Move = Enemy->GetCharacterMovement())
			{
				Move->MaxWalkSpeed *= Config.SpeedMultiplier;
			}

			// 以乘过倍率的速度/血量作为基准与回血上限（疾跑/减速/自回血都基于它）
			Enemy->InitCombatBaseline();

			// 死亡发经验/连击/得分
			Enemy->OnPawnDeathWithKiller.AddDynamic(this, &AWaveManager::HandleEnemyDeath);

			SpawnedCount++;
		}
	}

	return SpawnedCount;
}
