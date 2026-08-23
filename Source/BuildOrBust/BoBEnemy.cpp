// Build or Bust — 数据表驱动的敌人实现。

#include "BoBEnemy.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Camera/CameraComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Animation/AnimSequence.h"
#include "BoBAnimInstance.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EngineUtils.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "LootPickup.h"

#if !UE_BUILD_SHIPPING
/** 调试假人的标记。波次统计要按它排除，否则假人被当成本波敌人，
 *  HUD 数字虚高，而且这一波永远清不掉 */
const FName GBoBDummyTag(TEXT("BoBDummy"));

/**
 *  把点垂直投到地面上。
 *
 *  调试假人身上没有控制器，而 CharacterMovement 在没有控制器时不跑重力
 *  （bRunPhysicsWithNoController 默认关），生成在哪儿就悬在哪儿、不会掉下来。
 *  所以这里自己找地面，别指望重力。
 */
static FVector BoBDropToGround(UWorld* World, const FVector& Where, float HalfHeight)
{
	FHitResult Hit;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(BoBDropToGround), false);
	if (World->LineTraceSingleByChannel(Hit, Where + FVector(0.f, 0.f, 400.f),
		Where - FVector(0.f, 0.f, 3000.f), ECC_WorldStatic, Q))
	{
		return Hit.ImpactPoint + FVector(0.f, 0.f, HalfHeight);
	}
	return Where;
}

/**
 *  BoB.PoseProbe [变种]
 *
 *  只用一只敌人量：先关掉移动动画（只剩参考姿势 + 常驻姿态），读一遍
 *  各骨在**父骨空间**里的朝向，再把姿态清空读第二遍，两者之差就是
 *  姿态实际叠加了多少。
 *
 *  第一版拿基础型号和变种并排比组件空间的朝向，量出来的数全是错的——
 *  组件空间会把父骨的旋转一路累加下来（head 那根 9° 的量成了 80°），
 *  而且两只各自播着动画、相位不一定对齐。同一只、关掉动画、比父骨空间，
 *  才是能和表里数值直接对上的量法。
 */
static void BoBPoseProbeCmd(const TArray<FString>& Args, UWorld* World)
{
	if (!World) { return; }
	const FName VarId = Args.Num() > 0 ? FName(*Args[0]) : FName("Martyr_Warden");

	// 按玩家朝向摆位，不能用世界 +X——否则生成在背后，看上去像"什么都没发生"
	FVector At = FVector::ZeroVector;
	FVector Fwd = FVector::ForwardVector, Side = FVector::RightVector;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (APlayerController* PC = It->Get())
		{
			if (APawn* P = PC->GetPawn())
			{
				At = P->GetActorLocation();
				Fwd = P->GetActorRotation().Vector();
				Side = FRotationMatrix(P->GetActorRotation()).GetUnitAxis(EAxis::Y);
				break;
			}
		}
	}

	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ABoBEnemy* E = World->SpawnActor<ABoBEnemy>(ABoBEnemy::StaticClass(),
		BoBDropToGround(World, At + Fwd * 400.f, 96.f), FRotator::ZeroRotator, SP);
	if (!E || !E->ApplyEnemyRow(VarId))
	{
		UE_LOG(LogTemp, Warning, TEXT("[BoBPose] %s 生成失败"), *VarId.ToString());
		if (E) { E->Destroy(); }
		return;
	}

	USkeletalMeshComponent* M = E->GetMesh();
	// 无头跑没有渲染，默认的"只在被渲染时更新骨骼"会让姿态永远不求值
	M->VisibilityBasedAnimTickOption =
		EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	// 关掉移动动画：留着的话读到的是动画+姿态，分不出哪部分是姿态
	UBoBAnimInstance* Anim = Cast<UBoBAnimInstance>(M->GetAnimInstance());
	if (!Anim)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BoBPose] 没挂上 UBoBAnimInstance"));
		E->Destroy();
		return;
	}
	Anim->SetLoopSequence(nullptr);

	// 对照组：同一行、同样关掉动画，唯独把姿态清空。
	// 两只在同一拍里读，测量里就不含任何时间因素——
	// 前一版是同一只先后读两次，量出来的角度对不上表，就是这个原因
	ABoBEnemy* Ctrl = World->SpawnActor<ABoBEnemy>(ABoBEnemy::StaticClass(),
		BoBDropToGround(World, At + Fwd * 400.f + Side * 200.f, 96.f), FRotator::ZeroRotator, SP);
	if (Ctrl && Ctrl->ApplyEnemyRow(VarId))
	{
		Ctrl->GetMesh()->VisibilityBasedAnimTickOption =
			EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		if (UBoBAnimInstance* CA = Cast<UBoBAnimInstance>(Ctrl->GetMesh()->GetAnimInstance()))
		{
			CA->SetLoopSequence(nullptr);
			CA->SetPoseOverride(TMap<FName, FVector>());
		}
	}

	// 第三只：什么都不改，保持 ApplyEnemyRow 出厂状态（动画照播）。
	// 用来确认换掉 PlayAnimation 之后行走循环还在动——
	// 姿态准不准是一回事，把原本能动的动画弄死了是另一回事
	ABoBEnemy* Anim3 = World->SpawnActor<ABoBEnemy>(ABoBEnemy::StaticClass(),
		BoBDropToGround(World, At + Fwd * 400.f - Side * 200.f, 96.f), FRotator::ZeroRotator, SP);
	if (Anim3 && Anim3->ApplyEnemyRow(VarId))
	{
		Anim3->GetMesh()->VisibilityBasedAnimTickOption =
			EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	}

	TWeakObjectPtr<ABoBEnemy> WE(E), WC(Ctrl), WA(Anim3);
	FTimerHandle H1;
	World->GetTimerManager().SetTimer(H1, FTimerDelegate::CreateLambda([WE, WC, WA, VarId, World]()
	{
		ABoBEnemy* Foe = WE.Get();
		ABoBEnemy* Bare = WC.Get();
		if (!Foe || !Bare) { return; }
		USkeletalMeshComponent* MP = Foe->GetMesh();
		USkeletalMeshComponent* MB = Bare->GetMesh();

		// 动画求值跑在工作线程上。直接从游戏线程读骨骼是在和它抢数据——
		// 同样的输入连跑两次量出两组不同的角度，就是这么来的。
		// 先等在飞的求值落地，再同步求值一次，读到的才是稳定姿势
		for (USkeletalMeshComponent* M2 : { MP, MB })
		{
			M2->HandleExistingParallelEvaluationTask(true, true);
			M2->TickAnimation(0.f, false);
			M2->RefreshBoneTransforms();
		}

		UE_LOG(LogTemp, Log,
			TEXT("[BoBPose] %s —— 父骨空间，带姿态 vs 不带姿态（同一拍）"), *VarId.ToString());
		int32 Moved = 0, Match = 0;
		for (const TPair<FName, FVector>& P : Foe->ActivePose)
		{
			const FQuat QP = MP->GetSocketTransform(P.Key, RTS_ParentBoneSpace).GetRotation();
			const FQuat QB = MB->GetSocketTransform(P.Key, RTS_ParentBoneSpace).GetRotation();
			const FQuat Delta = QB.Inverse() * QP;
			const float Deg = FMath::RadiansToDegrees(Delta.GetAngle());
			const float WantDeg = FMath::RadiansToDegrees(
				UBoBAnimInstance::EulerToQuat(P.Value).GetAngle());
			if (Deg > 0.5f) { Moved++; }
			const bool bOk = FMath::Abs(WantDeg - Deg) < 1.0f;
			if (bOk) { Match++; }

			UE_LOG(LogTemp, Log,
				TEXT("[BoBPose]   %-14s 表里%-22s 应转%5.1f°  实转%5.1f°  %s"),
				*P.Key.ToString(), *P.Value.ToCompactString(), WantDeg, Deg,
				bOk ? TEXT("对上") : TEXT("<< 不符"));
		}
		UE_LOG(LogTemp, Log, TEXT("[BoBPose] %d/%d 根生效，%d/%d 根角度与表一致"),
			Moved, Foe->ActivePose.Num(), Match, Foe->ActivePose.Num());
		Foe->Destroy();
		Bare->Destroy();

		// 行走循环还活着吗：隔半秒采两次同一根骨，动了就说明序列在播
		if (ABoBEnemy* Live = WA.Get())
		{
			USkeletalMeshComponent* ML = Live->GetMesh();
			ML->HandleExistingParallelEvaluationTask(true, true);
			ML->TickAnimation(0.f, false);
			ML->RefreshBoneTransforms();
			const FQuat T0 = ML->GetSocketTransform(
				FName("thigh_l"), RTS_ParentBoneSpace).GetRotation();

			FTimerHandle H2;
			World->GetTimerManager().SetTimer(H2,
				FTimerDelegate::CreateLambda([WA, T0]()
			{
				ABoBEnemy* L2 = WA.Get();
				if (!L2) { return; }
				USkeletalMeshComponent* M3 = L2->GetMesh();
				M3->HandleExistingParallelEvaluationTask(true, true);
				M3->TickAnimation(0.f, false);
				M3->RefreshBoneTransforms();
				const FQuat T1 = M3->GetSocketTransform(
					FName("thigh_l"), RTS_ParentBoneSpace).GetRotation();
				const float Deg = FMath::RadiansToDegrees(T0.AngularDistance(T1));
				UE_LOG(LogTemp, Log,
					TEXT("[BoBPose] 行走循环自检: thigh_l 半秒内转了 %.1f°  %s"),
					Deg, Deg > 1.0f ? TEXT("动画在播") : TEXT("<< 动画没在播"));
				L2->Destroy();
			}), 0.5f, false);
		}
	}), 2.0f, false);
}

/**
 *  BoB.Show <型号> [型号2] [型号3] ...
 *
 *  在玩家正前方一字排开生成指定型号，面朝玩家、不挂 AI（站着不动，
 *  方便绕着看）。基础型号和变种并排放就能直接肉眼比对体态差异。
 *
 *  例：BoB.Show Martyr Martyr_Warden Martyr_Cracked
 */
static void BoBShowCmd(const TArray<FString>& Args, UWorld* World)
{
	if (!World || Args.Num() == 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[BoBShow] 用法: BoB.Show <型号> [型号2] ...  例: BoB.Show Martyr Martyr_Warden"));
		return;
	}

	FVector At = FVector::ZeroVector;
	FRotator Look = FRotator::ZeroRotator;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (APlayerController* PC = It->Get())
		{
			if (APawn* P = PC->GetPawn())
			{
				At = P->GetActorLocation();
				Look = P->GetActorRotation();
				break;
			}
		}
	}

	// 一字排开，间距 180cm，整体摆在玩家前方 500cm
	const FVector Fwd = Look.Vector();
	const FVector Right = FRotationMatrix(Look).GetUnitAxis(EAxis::Y);
	const float Span = 180.f * (Args.Num() - 1);

	for (int32 i = 0; i < Args.Num(); ++i)
	{
		const FName Id(*Args[i]);
		const FVector Where = At + Fwd * 500.f + Right * (180.f * i - Span * 0.5f);

		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		// 用 C++ 类而不是蓝图：不挂 AI，站着不动才好看清体态
		ABoBEnemy* E = World->SpawnActor<ABoBEnemy>(ABoBEnemy::StaticClass(),
			BoBDropToGround(World, Where, 96.f), (At - Where).Rotation(), SP);
		if (!E) { continue; }
		E->Tags.AddUnique(GBoBDummyTag);
		if (!E->ApplyEnemyRow(Id))
		{
			UE_LOG(LogTemp, Warning, TEXT("[BoBShow] 型号表里没有 %s"), *Id.ToString());
			E->Destroy();
			continue;
		}
		E->GetMesh()->VisibilityBasedAnimTickOption =
			EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		UE_LOG(LogTemp, Log, TEXT("[BoBShow] %s 已放在 %s，姿态 %d 根骨"),
			*Id.ToString(), *Where.ToCompactString(), E->ActivePose.Num());

		// 每半秒采一次，连采 8 次。"出现一下就没了"有好几种可能：
		// Actor 被销毁 / 网格被卸掉 / 包围盒塌成一点被视锥剔除 / 整体漂移出去，
		// 这几种在日志里长得完全不一样。按时间连续采样才能看出是"哪一刻"变的
		TWeakObjectPtr<ABoBEnemy> WE(E);
		const FVector Spawned = Where;
		TSharedPtr<FTimerHandle> TH = MakeShared<FTimerHandle>();
		TSharedPtr<int32> N = MakeShared<int32>(0);
		World->GetTimerManager().SetTimer(*TH,
			FTimerDelegate::CreateLambda([WE, Id, Spawned, TH, N, World]()
		{
			(*N)++;
			const float T = *N * 0.5f;
			ABoBEnemy* F = WE.Get();
			if (!F)
			{
				UE_LOG(LogTemp, Warning, TEXT("[BoBShow] %s t=%.1fs 已被销毁"),
					*Id.ToString(), T);
				World->GetTimerManager().ClearTimer(*TH);
				return;
			}
			USkeletalMeshComponent* M = F->GetMesh();
			const FBoxSphereBounds B = M->Bounds;
			UE_LOG(LogTemp, Log,
				TEXT("[BoBShow] %s t=%.1fs 网格=%s 可见=%d 半径=%.1f 盒=%s 骨骼数=%d 动画=%s 漂移=%.0f"),
				*Id.ToString(), T,
				M->GetSkeletalMeshAsset() ? TEXT("有") : TEXT("空"),
				M->IsVisible() ? 1 : 0, B.SphereRadius, *B.BoxExtent.ToCompactString(),
				M->GetComponentSpaceTransforms().Num(),
				M->GetAnimInstance() ? *M->GetAnimInstance()->GetClass()->GetName() : TEXT("无"),
				FVector::Dist(F->GetActorLocation(), Spawned));
			if (*N >= 8)
			{
				World->GetTimerManager().ClearTimer(*TH);
			}
		}), 0.5f, true);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GBoBShow(
	TEXT("BoB.Show"),
	TEXT("在玩家前方并排生成若干型号，用来肉眼比对变种体态"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(BoBShowCmd));

static FAutoConsoleCommandWithWorldAndArgs GBoBPoseProbe(
	TEXT("BoB.PoseProbe"),
	TEXT("并排生成基础型号与变种，逐骨比较常驻姿态是否真的生效"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(BoBPoseProbeCmd));
#endif

ABoBEnemy::ABoBEnemy()
{
	PrimaryActorTick.bCanEverTick = true;

	// 第一人称摄像机默认挂在 FP 网格的 head 插槽上（见 ABuildOrBustCharacter）。
	// NPC 永远不用第一人称视角，FP 网格也就一直是空的，于是摄像机每帧都去
	// 一个没有骨骼网格的组件上找插槽——一次 100 秒的测试刷了 7865 条警告。
	// 挂回胶囊，摄像机对 NPC 本来就没用，问题从根上消失。
	if (UCameraComponent* Cam = GetFirstPersonCameraComponent())
	{
		Cam->SetupAttachment(GetCapsuleComponent());
	}
}

void ABoBEnemy::BeginPlay()
{
	Super::BeginPlay();
	// 没人调 ApplyEnemyRow 的话保持蓝图里配好的样子，不强行读表
}

bool ABoBEnemy::ApplyEnemyRow(FName RowId)
{
	FBoBEnemyRow Raw;
	if (!UBoBEnemyLib::GetEnemyRow(RowId, Raw))
	{
		UE_LOG(LogTemp, Warning, TEXT("[BoB] 型号表里没有 %s"), *RowId.ToString());
		return false;
	}

	// 变种自己不带 mesh 和动画，要先从 VariantOf 那行继承过来
	if (!UBoBEnemyLib::ResolveAssets(Raw, Row))
	{
		UE_LOG(LogTemp, Warning, TEXT("[BoB] %s 解析资产失败"), *RowId.ToString());
		return false;
	}
	EnemyId = RowId;

	USkeletalMeshComponent* MeshComp = GetMesh();
	if (!MeshComp) { return false; }

	if (USkeletalMesh* SK = Row.Mesh.LoadSynchronous())
	{
		MeshComp->SetSkeletalMeshAsset(SK);
	}
	else
	{
		// 加载失败时以前是静默略过，结果是一只看不见却打得中的敌人——
		// 排查起来毫无线索，所以这里必须喊出来
		UE_LOG(LogTemp, Error, TEXT("[BoB] %s 的骨骼网格加载不到: %s"),
			*RowId.ToString(), *Row.Mesh.ToString());
	}

	// —— 属性 ——
	CurrentHP = Row.HP;
	MeleeDamage = Row.Damage;
	ScoreValue = FMath::Max(Row.SpawnCost, 1);
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->MaxWalkSpeed = Row.MoveSpeed;
	}

	// —— 体型 ——
	if (!FMath::IsNearlyEqual(Row.MeshScale, 1.f))
	{
		MeshComp->SetRelativeScale3D(FVector(Row.MeshScale));
	}

	ApplyHiddenBones();
	ApplyMaterialScalars();

	ActivePose = Row.PoseOverride;

	// 镜像出场就锁定一名宿主。随机挑，双人时两人都可能被选中——
	// 谁被选中谁就打不动它，这一局的分工当场被打乱
	if (IsMirror())
	{
		TArray<AShooterCharacter*> Players;
		for (TActorIterator<AShooterCharacter> It(GetWorld()); It; ++It)
		{
			if (It->GetController()) { Players.Add(*It); }
		}
		if (Players.Num() > 0)
		{
			MirrorHost = Players[FMath::RandHelper(Players.Num())];
			UE_LOG(LogTemp, Log, TEXT("[BoB] 完美镜像锁定宿主 %s"), *MirrorHost->GetName());
		}
	}

	// 喂给基类的步态状态机。我们的骨架只有走/攻击/死亡三段，没有单独的待机，
	// 待机也用移动段——原地踏步总比僵成雕像强
	if (UAnimSequence* Move = Row.AnimMove.LoadSynchronous())
	{
		IdleAnim = Move;
		WalkAnim = Move;
		RunAnim = Move;
	}
	AttackAnims.Reset();
	if (UAnimSequence* Atk = Row.AnimAttack.LoadSynchronous())
	{
		AttackAnims.Add(Atk);
	}

	// —— 动画 ——
	// 蓝图里显式配了 AnimBP 就尊重它（以后要做状态机会走这条路）；
	// 没配就用我们自己的实例：循环播移动动画 + 叠变种常驻姿态。
	// 单节点播放做不到后者，所以这里不再退化成 PlayAnimation
	const TSubclassOf<UAnimInstance> Existing = MeshComp->GetAnimClass();
	if (!Existing || Existing->IsChildOf(UBoBAnimInstance::StaticClass()))
	{
		MeshComp->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		MeshComp->SetAnimInstanceClass(UBoBAnimInstance::StaticClass());
		if (UBoBAnimInstance* Anim = Cast<UBoBAnimInstance>(MeshComp->GetAnimInstance()))
		{
			Anim->SetLoopSequence(Row.AnimMove.LoadSynchronous());
			Anim->SetPoseOverride(ActivePose);
		}
	}
	return true;
}

void ABoBEnemy::PlayLocoAnim(UAnimSequence* Anim, bool bLoop)
{
	USkeletalMeshComponent* MeshComp = GetMesh();
	if (UBoBAnimInstance* Inst =
		MeshComp ? Cast<UBoBAnimInstance>(MeshComp->GetAnimInstance()) : nullptr)
	{
		// 换片段而已，AnimInstance 原地保留，姿态叠加不受影响
		Inst->SetLoopSequence(Anim);
		return;
	}
	// 实例不在（比如蓝图里配了别的 AnimBP）就退回基类的老路
	Super::PlayLocoAnim(Anim, bLoop);
}

void ABoBEnemy::ApplyHiddenBones()
{
	USkeletalMeshComponent* MeshComp = GetMesh();
	if (!MeshComp) { return; }

	// 加法变种（双冠、双环、双筒、双持、双臂）是这么来的：
	// 所有候选部件都预埋在同一个 mesh 里，基础型号把不属于自己的藏掉。
	// 反过来说，这里漏藏一根骨，基础型号身上就会多出一件变种专属的东西。
	for (const FName& Bone : Row.HiddenBones)
	{
		if (Bone.IsNone()) { continue; }
		if (MeshComp->GetBoneIndex(Bone) == INDEX_NONE)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[BoB] %s 要藏的骨骼 %s 在骨架里不存在"),
				*EnemyId.ToString(), *Bone.ToString());
			continue;
		}
		MeshComp->HideBoneByName(Bone, EPhysBodyOp::PBO_None);
	}
}

void ABoBEnemy::ApplyMaterialScalars()
{
	USkeletalMeshComponent* MeshComp = GetMesh();
	if (!MeshComp || Row.MaterialScalars.Num() == 0) { return; }

	DynMaterials.Reset();
	const int32 Num = MeshComp->GetNumMaterials();
	for (int32 i = 0; i < Num; ++i)
	{
		if (UMaterialInstanceDynamic* MID = MeshComp->CreateDynamicMaterialInstance(i))
		{
			for (const TPair<FName, float>& P : Row.MaterialScalars)
			{
				MID->SetScalarParameterValue(P.Key, P.Value);
			}
			DynMaterials.Add(MID);
		}
	}
}

bool ABoBEnemy::IsMirror() const
{
	return Row.Role == EBoBRole::Elite && Row.WeakpointBone.IsNone();
}

void ABoBEnemy::BreakMirror(const FString& Why)
{
	if (!IsMirror() || bMirrorBroken) { return; }
	bMirrorBroken = true;
	UE_LOG(LogTemp, Log, TEXT("[BoB] 完美镜像破防：%s"), *Why);
}

void ABoBEnemy::TickMirror()
{
	if (!HasAuthority() || bMirrorBroken || !MirrorHost) { return; }

	// 宿主站进谐振灯光圈就破防。规格书另一条是"打空当前弹匣"，
	// 那条要由开火链路调 BreakMirror——两条都是"做一件反直觉的事"，
	// 但站灯圈这条对联机更安全（丢枪那种方案可能把枪穿进地板）
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->Tags.Contains(FName("BoBFloodlight")) &&
			FVector::Dist(It->GetActorLocation(), MirrorHost->GetActorLocation())
				<= MirrorLampRadius)
		{
			BreakMirror(TEXT("宿主进入谐振场"));
			return;
		}
	}
}

float ABoBEnemy::TakeDamage(float Damage, const FDamageEvent& DamageEvent,
	AController* EventInstigator, AActor* DamageCauser)
{
	// 宿主对未破防的镜像伤害为 0。规格书要的"团队信任"就建立在这上面：
	// 宿主打不动，只能靠队友；或者自己去做那件反直觉的事把它破防
	if (IsMirror() && !bMirrorBroken && MirrorHost && EventInstigator
		&& EventInstigator->GetPawn() == MirrorHost)
	{
		LastHitDamage = 0.f;
		LastHitTime = GetWorld()->GetTimeSeconds();
		LastHitLoc = GetActorLocation() + FVector(0.f, 0.f, 90.f);
		return 0.f;
	}
	return Super::TakeDamage(Damage, DamageEvent, EventInstigator, DamageCauser);
}

bool ABoBEnemy::IsScavenger() const
{
	return Row.Role == EBoBRole::Elite
		&& Row.GazeHookBand == EBoBGazeBand::Dissonance70;
}

void ABoBEnemy::CancelRepair()
{
	if (RepairUntil <= 0.f) { return; }
	RepairUntil = 0.f;
	RepairTarget = nullptr;
	if (UCharacterMovementComponent* M = GetCharacterMovement())
	{
		M->MaxWalkSpeed = CachedWalkSpeed;
	}
}

void ABoBEnemy::TickScavenge(float DeltaSeconds)
{
	if (!HasAuthority() || IsDead()) { return; }

	// 失谐越档后"修补"变"吸收"——够得着的距离一下拉远，
	// 玩家把失谐拉高换伤害的同时，也把这只喂胖了。代价要同时存在才叫取舍
	const bool bRemote = IsGazeHookActive();
	const float Reach = bRemote ? AbsorbReach : RepairReach;

	// 读条中：时间到就吸收
	if (RepairUntil > 0.f)
	{
		if (GetWorld()->GetTimeSeconds() < RepairUntil) { return; }

		ALootPickup* L = RepairTarget.Get();
		CancelRepair();
		if (!L || L->bTaken) { return; }

		L->SetTaken(true);
		MaxHP += MaxHP * RepairHealFrac;
		CurrentHP = FMath::Min(CurrentHP + MaxHP * RepairHealFrac, MaxHP);
		UE_LOG(LogTemp, Log, TEXT("[BoB] 拾荒残躯吸收了一件遗构，HP -> %.0f/%.0f"),
			CurrentHP, MaxHP);
		return;
	}

	// 找最近的、还没被拿走的遗构
	ALootPickup* Best = nullptr;
	float BestSq = Reach * Reach;
	for (TActorIterator<ALootPickup> It(GetWorld()); It; ++It)
	{
		if (It->bTaken) { continue; }
		const float D = FVector::DistSquared(It->GetActorLocation(), GetActorLocation());
		if (D < BestSq) { BestSq = D; Best = *It; }
	}
	if (!Best) { return; }

	// 停步读条。停下来这 3 秒就是规格书要给玩家的爆发窗口
	RepairTarget = Best;
	RepairUntil = GetWorld()->GetTimeSeconds() + RepairTime;
	if (UCharacterMovementComponent* M = GetCharacterMovement())
	{
		CachedWalkSpeed = M->MaxWalkSpeed;
		M->MaxWalkSpeed = 0.f;
	}
}

void ABoBEnemy::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (IsScavenger())
	{
		TickScavenge(DeltaSeconds);
	}
	if (IsMirror())
	{
		TickMirror();
	}
}

float ABoBEnemy::HighestPlayerGaze() const
{
	float Top = 0.f;
	for (TActorIterator<AShooterCharacter> It(GetWorld()); It; ++It)
	{
		const AShooterCharacter* C = *It;
		if (!C->GetController()) { continue; }
		Top = FMath::Max(Top, C->GetGaze());
	}
	return Top;
}

float ABoBEnemy::GazeBandThreshold() const
{
	switch (Row.GazeHookBand)
	{
	case EBoBGazeBand::Offset40:     return 40.f;
	case EBoBGazeBand::Dissonance70: return 70.f;
	case EBoBGazeBand::Dissonance80: return 80.f;
	default: return 0.f;
	}
}

float ABoBEnemy::GetGazeDamageMul() const
{
	if (GazeBandThreshold() <= 0.f) { return 1.f; }

	// 低于起点完全打不动；起点到 100 之间线性爬到上限。
	// 顶到 100 能吃满倍率，但处决者也在 100 降临——收益和代价挂在同一根轴上
	const float G = HighestPlayerGaze();
	if (G < GazeDamageStart) { return 0.f; }
	const float T = FMath::Clamp((G - GazeDamageStart)
		/ FMath::Max(100.f - GazeDamageStart, 1.f), 0.f, 1.f);
	return FMath::Lerp(1.f, FMath::Max(GazeDamageMax, 1.f), T);
}

bool ABoBEnemy::IsGazeHookActive() const
{
	const float Need = GazeBandThreshold();
	// 任意一名玩家越档即触发。双人时这条正是分工的支点：
	// 一人做诱饵把自己失谐拉上去，另一人保持低失谐负责输出
	return Need > 0.f && HighestPlayerGaze() >= Need;
}

bool ABoBEnemy::IsShellClosed() const
{
	// 精英 + 失谐≥80 钩 = 侵蚀温床的孢子囊。
	// 没在数据表另开一列，是因为这个组合在 22 行里唯一，
	// 而加一列要把表从文件系统层面删了重建（结构体加字段后覆盖导入不生效）
	return Row.Role == EBoBRole::Elite
		&& Row.GazeHookBand == EBoBGazeBand::Dissonance80
		&& HighestPlayerGaze() < GazeDamageStart;
}

float ABoBEnemy::ResolveIncomingDamage(float RawDamage, FName HitBone,
	const FVector& HitFromDirection)
{
	// 飘字位置取被命中的那根骨头，不取头顶——打腿和爆头飘在同一个地方，
	// 玩家就读不出自己打中了哪儿，弱点倍率也就失去了教学作用
	auto Record = [this, HitBone](float D)
	{
		LastHitDamage = D;
		LastHitTime = GetWorld()->GetTimeSeconds();
		const USkeletalMeshComponent* M = GetMesh();
		LastHitLoc = (M && !HitBone.IsNone() && M->GetBoneIndex(HitBone) != INDEX_NONE)
			? M->GetSocketLocation(HitBone)
			: GetActorLocation() + FVector(0.f, 0.f, 90.f);
		return D;
	};

	// 孢子囊闭合时绝对防弹。规格书：只有场上有人把失谐拉到 80 以上，
	// 温床才会锁定他并张开背部露出弱点——这是全作第一次让玩家发现
	// "污染自己是主动战术"，所以这里必须是 0 而不是减伤
	if (IsShellClosed())
	{
		return Record(0.f);
	}

	float Damage = RawDamage * GetGazeDamageMul();

	// 弱点：命中那根骨或它的子骨都算
	if (!Row.WeakpointBone.IsNone() && !HitBone.IsNone())
	{
		bool bWeak = (HitBone == Row.WeakpointBone);
		if (!bWeak)
		{
			if (const USkeletalMeshComponent* MeshComp = GetMesh())
			{
				FName Cur = HitBone;
				for (int32 Guard = 0; Guard < 8 && !Cur.IsNone(); ++Guard)
				{
					Cur = MeshComp->GetParentBone(Cur);
					if (Cur == Row.WeakpointBone) { bWeak = true; break; }
				}
			}
		}
		if (bWeak)
		{
			return Record(Damage * FMath::Max(Row.WeakpointMul, 1.f));
		}
	}

	// 正面减伤：只有从正面打进来才吃。绕到侧背就是全额——
	// 规格书给殉道重甲/拥抱者的设计就是逼玩家绕侧
	if (Row.FrontalDR > 0.f)
	{
		const FVector Fwd = GetActorForwardVector();
		// HitFromDirection 是伤害飞来的方向；取反得到"从我看向射手"
		const FVector ToShooter = (-HitFromDirection).GetSafeNormal();
		const float CosLimit = FMath::Cos(FMath::DegreesToRadians(FrontalArc));
		if (FVector::DotProduct(Fwd, ToShooter) > CosLimit)
		{
			Damage *= FMath::Clamp(1.f - Row.FrontalDR, 0.f, 1.f);
		}
	}
	return Record(Damage);
}
