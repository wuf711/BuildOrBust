// Copyright Epic Games, Inc. All Rights Reserved.


#include "Variant_Shooter/ShooterPlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerStart.h"
#include "ShooterCharacter.h"
#include "ShooterBulletCounterUI.h"
#include "BuildOrBust.h"
#include "Widgets/Input/SVirtualJoystick.h"
#include "Components/InputComponent.h"
#include "Engine/Engine.h"

void AShooterPlayerController::BeginPlay()
{
	Super::BeginPlay();

#if !WITH_EDITOR
	// 打包版（手机/独立进程）关闭引擎的全部屏幕警告消息：
	// 隐藏左上角红色引擎警告（如 SkyAtmosphere 移动端提示），编辑器内保留便于调试。
	// 注：这类渲染器警告走全局屏幕消息通道，必须用 DisableAllScreenMessages 才能关闭
	if (IsLocalPlayerController())
	{
		ConsoleCommand(TEXT("DisableAllScreenMessages"));
	}
	if (GEngine)
	{
		GEngine->bEnableOnScreenDebugMessages = false;
	}
#endif

	// only spawn touch controls on local player controllers
	if (IsLocalPlayerController())
	{
		if (ShouldUseTouchControls())
		{
			// spawn the mobile controls widget
			MobileControlsWidget = CreateWidget<UUserWidget>(this, MobileControlsWidgetClass);

			if (MobileControlsWidget)
			{
				// add the controls to the player screen
				MobileControlsWidget->AddToPlayerScreen(0);

			} else {

				UE_LOG(LogBuildOrBust, Error, TEXT("Could not spawn mobile controls widget."));

			}
		}

		// create the bullet counter widget and add it to the screen
		BulletCounterUI = CreateWidget<UShooterBulletCounterUI>(this, BulletCounterUIClass);

		if (BulletCounterUI)
		{
			BulletCounterUI->AddToPlayerScreen(0);

		} else {

			UE_LOG(LogBuildOrBust, Error, TEXT("Could not spawn bullet counter widget."));

		}
		
	}
}

void AShooterPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// only add IMCs for local player controllers
	if (IsLocalPlayerController())
	{
		// add the input mapping contexts
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
			{
				Subsystem->AddMappingContext(CurrentContext, 0);
			}

			// only add these IMCs if we're not using mobile touch input
			if (!ShouldUseTouchControls())
			{
				for (UInputMappingContext* CurrentContext : MobileExcludedMappingContexts)
				{
					Subsystem->AddMappingContext(CurrentContext, 0);
				}
			}
		}

		// 移动端：拖动右半屏幕转视角（不依赖 UMG 摇杆，避免摇杆失效导致无法瞄准）
		if (InputComponent)
		{
			InputComponent->BindTouch(IE_Pressed, this, &AShooterPlayerController::OnTouchLookStarted);
			InputComponent->BindTouch(IE_Repeat, this, &AShooterPlayerController::OnTouchLookMoved);
			InputComponent->BindTouch(IE_Released, this, &AShooterPlayerController::OnTouchLookEnded);
		}
	}
}

void AShooterPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	// subscribe to the pawn's OnDestroyed delegate
	InPawn->OnDestroyed.AddDynamic(this, &AShooterPlayerController::OnPawnDestroyed);

	// is this a shooter character?
	if (AShooterCharacter* ShooterCharacter = Cast<AShooterCharacter>(InPawn))
	{
		// add the player tag
		ShooterCharacter->Tags.Add(PlayerPawnTag);

		// subscribe to the pawn's delegates
		ShooterCharacter->OnBulletCountUpdated.AddDynamic(this, &AShooterPlayerController::OnBulletCountUpdated);
		ShooterCharacter->OnDamaged.AddDynamic(this, &AShooterPlayerController::OnPawnDamaged);

		// force update the life bar
		ShooterCharacter->OnDamaged.Broadcast(1.0f);
	}
}

void AShooterPlayerController::OnPawnDestroyed(AActor* DestroyedActor)
{
	// reset the bullet counter HUD
	if (IsValid(BulletCounterUI))
	{
		BulletCounterUI->BP_UpdateBulletCounter(0, 0);
	}

	// find the player start
	TArray<AActor*> ActorList;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), APlayerStart::StaticClass(), ActorList);

	if (ActorList.Num() > 0)
	{
		// select a random player start
		AActor* RandomPlayerStart = ActorList[FMath::RandRange(0, ActorList.Num() - 1)];

		// spawn a character at the player start
		const FTransform SpawnTransform = RandomPlayerStart->GetActorTransform();

		if (AShooterCharacter* RespawnedCharacter = GetWorld()->SpawnActor<AShooterCharacter>(CharacterClass, SpawnTransform))
		{
			// possess the character
			Possess(RespawnedCharacter);
		}
	}
}

void AShooterPlayerController::OnBulletCountUpdated(int32 MagazineSize, int32 Bullets)
{
	// update the UI
	if (BulletCounterUI)
	{
		BulletCounterUI->BP_UpdateBulletCounter(MagazineSize, Bullets);
	}
}

void AShooterPlayerController::OnPawnDamaged(float LifePercent)
{
	if (IsValid(BulletCounterUI))
	{
		BulletCounterUI->BP_Damaged(LifePercent);
	}
}

bool AShooterPlayerController::ShouldUseTouchControls() const
{
	// are we on a mobile platform? Should we force touch?
	return SVirtualJoystick::ShouldDisplayTouchInterface() || bForceTouchControls;
}

void AShooterPlayerController::OnTouchLookStarted(ETouchIndex::Type FingerIndex, FVector Location)
{
	int32 VX = 0, VY = 0;
	GetViewportSize(VX, VY);
	// 右半屏按下即接管转视角（左半屏留给移动摇杆）。
	// 每次按下都重置基准坐标：安卓偶发丢失"抬起"事件，若沿用旧坐标，
	// 下次按下第一帧会算出巨大位移 → 镜头猛甩一下。按下即重置可根治。
	if (VX > 0 && Location.X > VX * 0.5f)
	{
		LookFingerIndex = static_cast<int32>(FingerIndex);
		LastLookPos = FVector2D(Location.X, Location.Y);
	}
}

void AShooterPlayerController::OnTouchLookMoved(ETouchIndex::Type FingerIndex, FVector Location)
{
	if (static_cast<int32>(FingerIndex) != LookFingerIndex)
	{
		return;
	}
	const FVector2D Cur(Location.X, Location.Y);
	FVector2D Delta = Cur - LastLookPos;

	// 死区过滤：手指按住不动时触屏坐标有 ~1px 传感器噪声，若不过滤会让镜头/持枪每帧微抖
	if (Delta.SizeSquared() < 2.0f)
	{
		return;   // 不更新 LastLookPos，避免噪声累积漂移
	}
	LastLookPos = Cur;

	// 单次事件位移限幅：吞掉系统偶发的异常坐标跳变（双保险，防镜头猛甩）
	Delta.X = FMath::Clamp(Delta.X, -60.0f, 60.0f);
	Delta.Y = FMath::Clamp(Delta.Y, -60.0f, 60.0f);

	// 拖动量换算成视角旋转：手指右移→视角右转，手指下移→视角向下（贴合直觉）
	if (AShooterCharacter* C = Cast<AShooterCharacter>(GetPawn()))
	{
		C->DoAim(Delta.X * TouchLookSensitivity, Delta.Y * TouchLookSensitivity);
	}
}

void AShooterPlayerController::OnTouchLookEnded(ETouchIndex::Type FingerIndex, FVector Location)
{
	if (static_cast<int32>(FingerIndex) == LookFingerIndex)
	{
		LookFingerIndex = -1;
	}
}
