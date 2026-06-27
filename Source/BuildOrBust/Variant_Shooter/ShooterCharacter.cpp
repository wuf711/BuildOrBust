// Copyright Epic Games, Inc. All Rights Reserved.


#include "ShooterCharacter.h"
#include "ShooterWeapon.h"
#include "EnhancedInputComponent.h"
#include "Components/InputComponent.h"
#include "Components/PawnNoiseEmitterComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Camera/CameraComponent.h"
#include "TimerManager.h"
#include "ShooterGameMode.h"

AShooterCharacter::AShooterCharacter()
{
	// create the noise emitter component
	PawnNoiseEmitter = CreateDefaultSubobject<UPawnNoiseEmitterComponent>(TEXT("Pawn Noise Emitter"));

	// configure movement
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 600.0f, 0.0f);
}

void AShooterCharacter::BeginPlay()
{
	Super::BeginPlay();

	// reset HP to max
	CurrentHP = MaxHP;

	// update the HUD
	OnDamaged.Broadcast(1.0f);
}

void AShooterCharacter::EndPlay(EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	// clear the respawn timer
	GetWorld()->GetTimerManager().ClearTimer(RespawnTimer);
}

void AShooterCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	// base class handles move, aim and jump inputs
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		// Firing
		EnhancedInputComponent->BindAction(FireAction, ETriggerEvent::Started, this, &AShooterCharacter::DoStartFiring);
		EnhancedInputComponent->BindAction(FireAction, ETriggerEvent::Completed, this, &AShooterCharacter::DoStopFiring);

		// Switch weapon
		EnhancedInputComponent->BindAction(SwitchWeaponAction, ETriggerEvent::Triggered, this, &AShooterCharacter::DoSwitchWeapon);
	}

}

float AShooterCharacter::TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	// ignore if already dead
	if (CurrentHP <= 0.0f)
	{
		return 0.0f;
	}

	// 应用护甲减伤
	Damage *= (1.0f - DamageReductionPct);

	// Reduce HP
	CurrentHP -= Damage;

	// Have we depleted HP?
	if (CurrentHP <= 0.0f)
	{
		Die();
	}

	// update the HUD
	OnDamaged.Broadcast(FMath::Max(0.0f, CurrentHP / MaxHP));

	return Damage;
}

void AShooterCharacter::GrantMaxHealthBonus(float BonusMax)
{
	MaxHP += BonusMax;
	CurrentHP = MaxHP;
	OnDamaged.Broadcast(FMath::Max(0.0f, CurrentHP / MaxHP));
}

void AShooterCharacter::HealPlayer(float Amount)
{
	if (CurrentHP <= 0.0f)
	{
		return;
	}
	CurrentHP = FMath::Min(MaxHP, CurrentHP + Amount);
	OnDamaged.Broadcast(FMath::Max(0.0f, CurrentHP / MaxHP));
}

void AShooterCharacter::AddDamageReduction(float Pct)
{
	DamageReductionPct = FMath::Clamp(DamageReductionPct + Pct, 0.0f, 0.8f);
}

void AShooterCharacter::AddCombo()
{
	ComboCount++;
	// 倍率 1.0 → 3.0，每击杀 +0.1
	ComboMultiplier = FMath::Min(1.0f + ComboCount * 0.1f, 3.0f);

	// 4 秒内无击杀则重置
	GetWorld()->GetTimerManager().SetTimer(
		ComboResetTimer, this, &AShooterCharacter::ResetCombo, 4.0f, false);
}

void AShooterCharacter::ResetCombo()
{
	ComboCount = 0;
	ComboMultiplier = 1.0f;
}

void AShooterCharacter::DoAim(float Yaw, float Pitch)
{
	// only route inputs if the character is not dead
	if (!IsDead())
	{
		Super::DoAim(Yaw, Pitch);
	}
}

void AShooterCharacter::DoMove(float Right, float Forward)
{
	// only route inputs if the character is not dead
	if (!IsDead())
	{
		Super::DoMove(Right, Forward);
	}
}

void AShooterCharacter::DoJumpStart()
{
	// only route inputs if the character is not dead
	if (!IsDead())
	{
		Super::DoJumpStart();
	}
}

void AShooterCharacter::DoJumpEnd()
{
	// only route inputs if the character is not dead
	if (!IsDead())
	{
		Super::DoJumpEnd();
	}
}

void AShooterCharacter::DoStartFiring()
{
	if (IsDead())
	{
		return;
	}

	// 本地立即开火：手感/视觉响应。客户端本地子弹不造成伤害（见 ShooterProjectile::ProcessHit）
	if (CurrentWeapon)
	{
		CurrentWeapon->StartFiring();
	}

	// 客户端：通知服务器在权威端开火 → 命中/击杀/计分对所有玩家生效（修复 P2 击杀不记分）
	if (!HasAuthority())
	{
		Server_StartFiring();
	}
}

void AShooterCharacter::DoStopFiring()
{
	if (CurrentWeapon)
	{
		CurrentWeapon->StopFiring();
	}

	if (!HasAuthority())
	{
		Server_StopFiring();
	}
}

void AShooterCharacter::Server_StartFiring_Implementation()
{
	// 在服务器权威端开火：生成造成伤害的子弹，击杀归属本玩家、正确计分
	if (CurrentWeapon && !IsDead())
	{
		CurrentWeapon->StartFiring();
	}
}

void AShooterCharacter::Server_StopFiring_Implementation()
{
	if (CurrentWeapon)
	{
		CurrentWeapon->StopFiring();
	}
}

void AShooterCharacter::DoSwitchWeapon()
{
	// ensure we have at least two weapons two switch between
	if (OwnedWeapons.Num() > 1 && !IsDead())
	{
		// deactivate the old weapon
		CurrentWeapon->DeactivateWeapon();

		// find the index of the current weapon in the owned list
		int32 WeaponIndex = OwnedWeapons.Find(CurrentWeapon);

		// is this the last weapon?
		if (WeaponIndex == OwnedWeapons.Num() - 1)
		{
			// loop back to the beginning of the array
			WeaponIndex = 0;
		}
		else {
			// select the next weapon index
			++WeaponIndex;
		}

		// set the new weapon as current
		CurrentWeapon = OwnedWeapons[WeaponIndex];

		// activate the new weapon
		CurrentWeapon->ActivateWeapon();
	}
}

void AShooterCharacter::AttachWeaponMeshes(AShooterWeapon* Weapon)
{
	const FAttachmentTransformRules AttachmentRule(EAttachmentRule::SnapToTarget, false);

	// attach the weapon actor
	Weapon->AttachToActor(this, AttachmentRule);

	// attach the weapon meshes
	Weapon->GetFirstPersonMesh()->AttachToComponent(GetFirstPersonMesh(), AttachmentRule, FirstPersonWeaponSocket);
	Weapon->GetThirdPersonMesh()->AttachToComponent(GetMesh(), AttachmentRule, FirstPersonWeaponSocket);
	
}

void AShooterCharacter::PlayFiringMontage(UAnimMontage* Montage)
{
	// stub
}

void AShooterCharacter::AddWeaponRecoil(float Recoil)
{
	// apply the recoil as pitch input
	AddControllerPitchInput(Recoil);
}

void AShooterCharacter::UpdateWeaponHUD(int32 CurrentAmmo, int32 MagazineSize)
{
	OnBulletCountUpdated.Broadcast(MagazineSize, CurrentAmmo);
}

FVector AShooterCharacter::GetWeaponTargetLocation()
{
	// trace ahead from the camera viewpoint
	FHitResult OutHit;

	const FVector Start = GetFirstPersonCameraComponent()->GetComponentLocation();
	const FVector End = Start + (GetFirstPersonCameraComponent()->GetForwardVector() * MaxAimDistance);

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	GetWorld()->LineTraceSingleByChannel(OutHit, Start, End, ECC_Visibility, QueryParams);

	// return either the impact point or the trace end
	return OutHit.bBlockingHit ? OutHit.ImpactPoint : OutHit.TraceEnd;
}

void AShooterCharacter::AddWeaponClass(const TSubclassOf<AShooterWeapon>& WeaponClass)
{
	// do we already own this weapon?
	AShooterWeapon* OwnedWeapon = FindWeaponOfType(WeaponClass);

	if (!OwnedWeapon)
	{
		// spawn the new weapon
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this;
		SpawnParams.Instigator = this;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParams.TransformScaleMethod = ESpawnActorScaleMethod::MultiplyWithRoot;

		AShooterWeapon* AddedWeapon = GetWorld()->SpawnActor<AShooterWeapon>(WeaponClass, GetActorTransform(), SpawnParams);

		if (AddedWeapon)
		{
			// add the weapon to the owned list
			OwnedWeapons.Add(AddedWeapon);

			// if we have an existing weapon, deactivate it
			if (CurrentWeapon)
			{
				CurrentWeapon->DeactivateWeapon();
			}

			// switch to the new weapon
			CurrentWeapon = AddedWeapon;
			CurrentWeapon->ActivateWeapon();
		}
	}
}

void AShooterCharacter::OnWeaponActivated(AShooterWeapon* Weapon)
{
	// update the bullet counter
	OnBulletCountUpdated.Broadcast(Weapon->GetMagazineSize(), Weapon->GetBulletCount());

	// set the character mesh AnimInstances
	GetFirstPersonMesh()->SetAnimInstanceClass(Weapon->GetFirstPersonAnimInstanceClass());
	GetMesh()->SetAnimInstanceClass(Weapon->GetThirdPersonAnimInstanceClass());
}

void AShooterCharacter::OnWeaponDeactivated(AShooterWeapon* Weapon)
{
	// unused
}

void AShooterCharacter::OnSemiWeaponRefire()
{
	// unused
}

AShooterWeapon* AShooterCharacter::FindWeaponOfType(TSubclassOf<AShooterWeapon> WeaponClass) const
{
	// check each owned weapon
	for (AShooterWeapon* Weapon : OwnedWeapons)
	{
		if (Weapon->IsA(WeaponClass))
		{
			return Weapon;
		}
	}

	// weapon not found
	return nullptr;

}

void AShooterCharacter::Die()
{
	// deactivate the weapon
	if (IsValid(CurrentWeapon))
	{
		CurrentWeapon->DeactivateWeapon();
	}

	// increment the team score
	if (AShooterGameMode* GM = Cast<AShooterGameMode>(GetWorld()->GetAuthGameMode()))
	{
		GM->IncrementTeamScore(TeamByte);
	}

	// grant the death tag to the character
	Tags.Add(DeathTag);
		
	// stop character movement
	GetCharacterMovement()->StopMovementImmediately();

	// disable controls
	DisableInput(nullptr);

	// reset the bullet counter UI
	OnBulletCountUpdated.Broadcast(0, 0);

	// call the BP handler
	BP_OnDeath();

	// 玩家死亡 = 游戏失败（肉鸽不复活）
	if (AShooterGameMode* GM = Cast<AShooterGameMode>(GetWorld()->GetAuthGameMode()))
	{
		GM->TriggerLose();
	}
}

void AShooterCharacter::OnRespawn()
{
	// destroy the character to force the PC to respawn
	Destroy();
}

bool AShooterCharacter::IsDead() const
{
	// the character is dead if their current HP drops to zero
	return CurrentHP <= 0.0f;
}
