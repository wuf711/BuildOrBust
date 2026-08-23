// Build or Bust — CS-07 三阶段状态机实现。

#include "Boss_CS07.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"
#include "BoBFalseRelic.h"
#include "EngineUtils.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "GameFramework/CharacterMovementComponent.h"

#if !UE_BUILD_SHIPPING
/**
 *  BoB.BossTest —— 不打完九潮也能验状态机。
 *
 *  走一遍完整链路：开火烧到节点一 → 验证阶段二折射盾把伤害挡在抗性之外
 *  → 击破能量柱推到节点二 → 再开火烧到节点三，逐步核对倒计时是否各减 15 秒。
 */
static void BoBBossTestCmd(const TArray<FString>& Args, UWorld* World)
{
	if (!World) { return; }

	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ABoss_CS07* B = World->SpawnActor<ABoss_CS07>(
		ABoss_CS07::StaticClass(), FVector(0.f, 0.f, 500.f), FRotator::ZeroRotator, SP);
	if (!B)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BossTest] 生成失败"));
		return;
	}
	B->Activate();
	UE_LOG(LogTemp, Log, TEXT("[BossTest] 起始 抗性=%.0f%% 阶段=%d 倒计时=%.0f"),
		B->GetResistance() * 100.f, (int32)B->GetPhase(), B->GetExtractionRemaining());

	TWeakObjectPtr<ABoss_CS07> WB(B);
	TSharedPtr<int32> Step = MakeShared<int32>(0);
	TSharedPtr<FTimerHandle> TH = MakeShared<FTimerHandle>();
	World->GetTimerManager().SetTimer(*TH,
		FTimerDelegate::CreateLambda([WB, Step, TH, World]()
	{
		ABoss_CS07* Boss = WB.Get();
		if (!Boss) { World->GetTimerManager().ClearTimer(*TH); return; }
		(*Step)++;

		const float Before = Boss->GetResistance();
		const bool bShield = Boss->IsShielded();
		FString Act;

		if (bShield)
		{
			// 盾立着的时候故意开一枪，验证它真的不吃伤害
			UGameplayStatics::ApplyDamage(Boss, Boss->ResistancePool * 0.10f, nullptr, nullptr, nullptr);
			const bool bBlocked = FMath::IsNearlyEqual(Before, Boss->GetResistance());
			Act = FString::Printf(TEXT("盾下开火 %s"), bBlocked ? TEXT("已挡住(正确)") : TEXT("<<竟然掉抗性"));
			// 再打柱子推进
			Boss->BreakPillar();
			Act += TEXT(" + 击破能量柱");
		}
		else
		{
			UGameplayStatics::ApplyDamage(Boss, Boss->ResistancePool * 0.10f, nullptr, nullptr, nullptr);
			Act = TEXT("开火 10%");
		}

		UE_LOG(LogTemp, Log,
			TEXT("[BossTest] #%02d %-28s 抗性=%5.1f%% 阶段=%d 倒计时=%.0f"),
			*Step, *Act, Boss->GetResistance() * 100.f,
			(int32)Boss->GetPhase(), Boss->GetExtractionRemaining());

		if (Boss->GetResistance() <= 0.f || *Step >= 20)
		{
			UE_LOG(LogTemp, Log,
				TEXT("[BossTest] 收尾: 抗性=%.0f%% 阶段=%d 倒计时=%.0f（起始 90，三节点应共减 45）"),
				Boss->GetResistance() * 100.f, (int32)Boss->GetPhase(),
				Boss->GetExtractionRemaining());
			World->GetTimerManager().ClearTimer(*TH);
			Boss->Destroy();
		}
	}), 0.6f, true);
}

static FAutoConsoleCommandWithWorldAndArgs GBoBBossTest(
	TEXT("BoB.BossTest"),
	TEXT("生成 CS-07 并跑一遍三阶段状态机自检"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(BoBBossTestCmd));
#endif

ABoss_CS07::ABoss_CS07()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f;   // 倒计时和阶段判定不需要每帧
	bReplicates = true;
	bAlwaysRelevant = true;                 // 十米高的塔，任何角落都该看见它的状态

	// 塔碑本体十米高，用一个粗胶囊接子弹就够，不必上每骨碰撞
	HitVolume = CreateDefaultSubobject<UCapsuleComponent>(TEXT("HitVolume"));
	HitVolume->InitCapsuleSize(320.f, 500.f);
	HitVolume->SetCollisionProfileName(TEXT("Pawn"));
	SetRootComponent(HitVolume);

	Tower = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Tower"));
	Tower->SetupAttachment(HitVolume);
	Tower->SetRelativeLocation(FVector(0.f, 0.f, -500.f));
	Tower->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	static ConstructorHelpers::FObjectFinder<USkeletalMesh>
		MeshFinder(TEXT("/Game/BoB/Foes/SK_Terminal/SK_Terminal"));
	if (MeshFinder.Succeeded())
	{
		Tower->SetSkeletalMeshAsset(MeshFinder.Object);
	}

	// 五段动画按名字取。Port_Retract 目前没导入，状态机也不需要它
	struct FClipBind { const TCHAR* Path; TObjectPtr<UAnimSequence>* Slot; };
	const FClipBind Binds[] = {
		{ TEXT("/Game/BoB/Foes/SK_Terminal/A_Terminal_Idle_Pulse"),  &IdlePulse },
		{ TEXT("/Game/BoB/Foes/SK_Terminal/A_Terminal_Break_Mid"),   &BreakMid },
		{ TEXT("/Game/BoB/Foes/SK_Terminal/A_Terminal_Break_Top"),   &BreakTop },
		{ TEXT("/Game/BoB/Foes/SK_Terminal/A_Terminal_Break_Base"),  &BreakBase },
		{ TEXT("/Game/BoB/Foes/SK_Terminal/A_Terminal_Shield_Open"), &ShieldOpen },
	};
	for (const FClipBind& B : Binds)
	{
		ConstructorHelpers::FObjectFinder<UAnimSequence> F(B.Path);
		if (F.Succeeded())
		{
			*B.Slot = F.Object;
		}
	}
}

void ABoss_CS07::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABoss_CS07, Resistance);
	DOREPLIFETIME(ABoss_CS07, ExtractionRemain);
	DOREPLIFETIME(ABoss_CS07, Phase);
	DOREPLIFETIME(ABoss_CS07, bActive);
	DOREPLIFETIME(ABoss_CS07, bShieldUp);
	DOREPLIFETIME(ABoss_CS07, TargetShape);
	DOREPLIFETIME(ABoss_CS07, TargetVein);
}

void ABoss_CS07::BeginPlay()
{
	Super::BeginPlay();
	ExtractionRemain = ExtractionSeconds;
	PlayClip(IdlePulse, true);
}

void ABoss_CS07::PlayClip(UAnimSequence* Clip, bool bLoop)
{
	if (Clip && Tower)
	{
		Tower->PlayAnimation(Clip, bLoop);
	}
}

void ABoss_CS07::Activate()
{
	if (bActive) { return; }
	bActive = true;
	ExtractionRemain = ExtractionSeconds;
	EnterPhase(EBoBBossPhase::Authority);
	UE_LOG(LogTemp, Log, TEXT("[BoBBoss] CS-07 上线，回收倒计时 %.0f 秒"), ExtractionRemain);
}

void ABoss_CS07::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority() || !bActive || bExtractionFired) { return; }

	if (Phase == EBoBBossPhase::Corridor)
	{
		TickCorridor(DeltaSeconds);
	}

	ExtractionRemain = FMath::Max(ExtractionRemain - DeltaSeconds, 0.f);
	if (ExtractionRemain <= 0.f)
	{
		// 通道开了，Boss 不死也不停——契约 C7 要的就是这个反差
		bExtractionFired = true;
		UE_LOG(LogTemp, Log, TEXT("[BoBBoss] 回收通道开启（CS-07 仍在场）"));
		OnExtractionReady.Broadcast();
	}
}

float ABoss_CS07::TakeDamage(float Damage, const FDamageEvent& DamageEvent,
	AController* EventInstigator, AActor* DamageCauser)
{
	if (!HasAuthority() || !bActive || Damage <= 0.f)
	{
		return 0.f;
	}

	// 阶段二折射盾：普通伤害不进抗性，原样弹回开火者
	if (IsShielded())
	{
		if (ShieldReflectRatio > 0.f && EventInstigator)
		{
			if (APawn* Shooter = EventInstigator->GetPawn())
			{
				UGameplayStatics::ApplyDamage(Shooter, Damage * ShieldReflectRatio,
					EventInstigator, this, nullptr);
			}
		}
		return 0.f;
	}

	// 打它不掉血，只烧抗性——抗性又只换倒计时。DPS 的唯一意义是早点走
	BurnResistance(Damage / FMath::Max(ResistancePool, 1.f));
	return Damage;
}

void ABoss_CS07::RegisterFalseRelic()
{
	if (HasAuthority() && bActive)
	{
		BurnResistance(RelicBurn);
		UE_LOG(LogTemp, Log, TEXT("[BoBBoss] 无效遗构录入，抗性剩 %.0f%%"), Resistance * 100.f);
	}
}

void ABoss_CS07::BreakPillar()
{
	if (HasAuthority() && bActive)
	{
		BurnResistance(PillarBurn);
		UE_LOG(LogTemp, Log, TEXT("[BoBBoss] 能量柱击破，抗性剩 %.0f%%"), Resistance * 100.f);
	}
}

void ABoss_CS07::BurnResistance(float Delta)
{
	if (Delta <= 0.f) { return; }
	Resistance = FMath::Clamp(Resistance - Delta, 0.f, 1.f);

	// 三个节点：66% / 33% / 0%。每跨一个减一次时间、播一次结构崩塌。
	// 用已兑现节点数计数，抗性来回浮动也不会重复扣时间
	const float Thresholds[3] = { 0.66f, 0.33f, 0.0f };
	while (NodesCleared < 3 && Resistance <= Thresholds[NodesCleared] + KINDA_SMALL_NUMBER)
	{
		NodesCleared++;
		ExtractionRemain = FMath::Max(ExtractionRemain - NodeTimeCut, 0.f);

		switch (NodesCleared)
		{
		case 1:
			// 中段崩裂，碎块砸地，露出青铜刻纹
			PlayClip(BreakMid, false);
			EnterPhase(EBoBBossPhase::Symmetry);
			break;
		case 2:
			// 上段崩落，接缝开始喷火花并持续到战斗结束
			PlayClip(BreakTop, false);
			EnterPhase(EBoBBossPhase::Corridor);
			break;
		default:
			// 基座崩尽 → 击败（结局二）。原契约 C7 是"打不死"，已被推翻
			PlayClip(BreakBase, false);
			bActive = false;
			UE_LOG(LogTemp, Log, TEXT("[BoBBoss] CS-07 抗性烧尽 —— 击败（结局二）"));
			OnBossDefeated.Broadcast();
			break;
		}

		UE_LOG(LogTemp, Log,
			TEXT("[BoBBoss] 节点 %d 烧穿，倒计时 -%.0f 秒 -> 剩 %.0f 秒"),
			NodesCleared, NodeTimeCut, ExtractionRemain);
	}
}

void ABoss_CS07::SpawnPhaseOneProps()
{
	UWorld* World = GetWorld();
	if (!World || bPropsPlaced) { return; }
	bPropsPlaced = true;

	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// 侧面的残存接收端口
	ABoBBossPort* Port = World->SpawnActor<ABoBBossPort>(ABoBBossPort::StaticClass(),
		GetActorLocation() + FVector(0.f, 420.f, -440.f), FRotator::ZeroRotator, SP);
	if (Port) { Port->BossOwner = this; }

	// 3 件无效遗构撒在场地边缘，彼此拉开——一次只能扛一件，
	// 拉开距离双人才有分头搬的理由
	for (int32 i = 0; i < FalseRelicCount; ++i)
	{
		const float Ang = (2.f * PI * i) / FMath::Max(FalseRelicCount, 1) + PI * 0.25f;
		const FVector At = GetActorLocation()
			+ FVector(FMath::Cos(Ang), FMath::Sin(Ang), 0.f) * RelicRingRadius
			+ FVector(0.f, 0.f, -440.f);
		World->SpawnActor<ABoBFalseRelic>(ABoBFalseRelic::StaticClass(), At, FRotator::ZeroRotator, SP);
	}
	UE_LOG(LogTemp, Log, TEXT("[BoBBoss] 阶段一布场：%d 件无效遗构 + 接收端口"), FalseRelicCount);
}

void ABoss_CS07::SetCorridorMovement(bool bOn)
{
	UWorld* World = GetWorld();
	if (!World) { return; }

	for (TActorIterator<AShooterCharacter> It(World); It; ++It)
	{
		AShooterCharacter* C = *It;
		UCharacterMovementComponent* M = C->GetCharacterMovement();
		if (!M) { continue; }

		if (bOn)
		{
			// 叙事上是"武器锁死＝卸掉负重"，玩家一进阶段三就该觉得身体变轻。
			// 存一份原值，阶段结束要还回去——直接乘上去不记录的话，
			// 打完 Boss 回主循环玩家就永久快 25%
			if (!CorridorBaseSpeed.Contains(C))
			{
				CorridorBaseSpeed.Add(C, M->MaxWalkSpeed);
			}
			M->MaxWalkSpeed = CorridorBaseSpeed[C] * CorridorSpeedMul;
		}
		else if (const float* Base = CorridorBaseSpeed.Find(C))
		{
			M->MaxWalkSpeed = *Base;
		}
	}
	if (!bOn) { CorridorBaseSpeed.Reset(); }
}

void ABoss_CS07::TickCorridor(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	if (!World) { return; }

	// 单人把力场伤害减半（规格书容错第三条）
	int32 NumPlayers = 0;
	for (TActorIterator<AShooterCharacter> It(World); It; ++It)
	{
		if (It->GetController()) { NumPlayers++; }
	}
	const float SoloMul = (NumPlayers <= 1) ? 0.5f : 1.0f;

	for (TActorIterator<AShooterCharacter> It(World); It; ++It)
	{
		AShooterCharacter* C = *It;
		if (!C->GetController() || C->IsDead()) { continue; }

		// 在谐振灯范围内伤害减半。现在的灯是放置式信标（BoBFloodlight 标签），
		// 不是规格书 6.4 那盏手持灯——手持那套没实现，这里按现有的灯来判定
		bool bLit = false;
		for (TActorIterator<AActor> LIt(World); LIt; ++LIt)
		{
			if (LIt->Tags.Contains(FName("BoBFloodlight")) &&
				FVector::Dist(LIt->GetActorLocation(), C->GetActorLocation()) <= LampRadius)
			{
				bLit = true;
				break;
			}
		}

		// 血量下限保护在 ApplyFieldDrain 里：压到 15% 就停手。
		// 这条不告诉玩家——屏幕边缘一直在闪，但环境永远杀不死人，
		// 唯一致命源是朝圣者接触。这是阶段三敢做那么吓人的前提
		const float Rate = (bLit ? FieldDpsLit : FieldDpsBare) * SoloMul;
		C->ApplyFieldDrain(Rate * DeltaSeconds, CorridorHpFloor);
	}
}

void ABoss_CS07::StartPillarRound()
{
	UWorld* World = GetWorld();
	if (!World) { return; }

	// 清掉上一轮残留
	for (TActorIterator<ABoBEnergyPillar> It(World); It; ++It) { It->Destroy(); }

	// 先抽弱点的形状与纹路
	TargetShape = (EBoBPillarShape)FMath::RandRange(0, 3);
	TargetVein = (EBoBPillarVein)FMath::RandRange(0, 3);

	// 另找一个不同的形状和一个不同的纹路当干扰项
	EBoBPillarShape OtherShape = TargetShape;
	while (OtherShape == TargetShape) { OtherShape = (EBoBPillarShape)FMath::RandRange(0, 3); }
	EBoBPillarVein OtherVein = TargetVein;
	while (OtherVein == TargetVein) { OtherVein = (EBoBPillarVein)FMath::RandRange(0, 3); }

	// 四根柱子的属性配对是这个机制的命门：
	//   0 弱点        = 目标形状 + 目标纹路
	//   1 同形状诱饵  = 目标形状 + 别的纹路   -> A 光看形状会在 0 和 1 之间卡住
	//   2 同纹路诱饵  = 别的形状 + 目标纹路   -> B 光看纹路会在 0 和 2 之间卡住
	//   3 无关        = 都不是
	// 少了 1 或 2，任何一个人单独就能锁定答案，非对称就白设了
	struct FCombo { EBoBPillarShape S; EBoBPillarVein V; bool bTarget; };
	FCombo Combos[4] = {
		{ TargetShape, TargetVein,  true  },
		{ TargetShape, OtherVein,   false },
		{ OtherShape,  TargetVein,  false },
		{ OtherShape,  OtherVein,   false },
	};
	// 洗牌，别让弱点每轮都站同一个位置
	for (int32 i = 3; i > 0; --i)
	{
		Swap(Combos[i], Combos[FMath::RandRange(0, i)]);
	}

	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (int32 i = 0; i < 4; ++i)
	{
		const float Ang = PI * 0.5f * i + PI * 0.25f;
		const FVector At = GetActorLocation()
			+ FVector(FMath::Cos(Ang), FMath::Sin(Ang), 0.f) * PillarRingRadius
			+ FVector(0.f, 0.f, -440.f);
		if (ABoBEnergyPillar* P = World->SpawnActor<ABoBEnergyPillar>(
			ABoBEnergyPillar::StaticClass(), At, FRotator::ZeroRotator, SP))
		{
			P->Setup(this, Combos[i].S, Combos[i].V, Combos[i].bTarget);
		}
	}
	UE_LOG(LogTemp, Log, TEXT("[BoBBoss] 能量柱一轮：弱点 = %s / %s"),
		ABoBEnergyPillar::ShapeName(TargetShape), ABoBEnergyPillar::VeinName(TargetVein));
}

void ABoss_CS07::ResolvePillarHit(bool bCorrect)
{
	if (!HasAuthority()) { return; }

	if (bCorrect)
	{
		BreakPillar();
		// 还在阶段二就再来一轮；已经推进到阶段三就收场
		if (Phase == EBoBBossPhase::Symmetry)
		{
			StartPillarRound();
		}
		else
		{
			for (TActorIterator<ABoBEnergyPillar> It(GetWorld()); It; ++It) { It->Destroy(); }
		}
	}
	else
	{
		// 打错引一波朝圣晶簇。生怪统一交给 Director，散在柱子里会绕过存活统计
		OnWrongPillar.Broadcast();
		UE_LOG(LogTemp, Log, TEXT("[BoBBoss] 打错柱子，请求增援朝圣晶簇"));
	}
}

void ABoss_CS07::EnterPhase(EBoBBossPhase NewPhase)
{
	Phase = NewPhase;

	if (NewPhase == EBoBBossPhase::Authority)
	{
		SpawnPhaseOneProps();
	}
	else if (NewPhase == EBoBBossPhase::Symmetry)
	{
		StartPillarRound();
	}
	else if (NewPhase == EBoBBossPhase::Corridor)
	{
		SetCorridorMovement(true);
	}

	// 折射盾只在阶段二立着。阶段三收盾——那一段的压力全在环境上，
	// 规格书写明致命性要收回，再顶个反伤盾就变成劝退了
	bShieldUp = (NewPhase == EBoBBossPhase::Symmetry);
	if (bShieldUp)
	{
		PlayClip(ShieldOpen, false);
	}

	// 阶段切换时那句结构化短句由表现层接（阶段一→二「权限校对失败」、
	// 二→三「接受同化」），这里只广播事件，不在 C++ 里写死播报
	OnPhaseChanged.Broadcast(NewPhase);

	static const TCHAR* Names[] = { TEXT("权限剥夺"), TEXT("对称性破缺"), TEXT("静默走廊") };
	UE_LOG(LogTemp, Log, TEXT("[BoBBoss] 进入阶段：%s"), Names[static_cast<uint8>(NewPhase)]);
}
