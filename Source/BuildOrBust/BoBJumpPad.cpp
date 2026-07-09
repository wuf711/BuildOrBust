#include "BoBJumpPad.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Character.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

ABoBJumpPad::ABoBJumpPad()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	// Trigger box as root, slightly above the floor; player entering it gets launched
	Trigger = CreateDefaultSubobject<UBoxComponent>(TEXT("Trigger"));
	SetRootComponent(Trigger);
	Trigger->SetBoxExtent(FVector(90.0f, 90.0f, 60.0f));
	Trigger->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	Trigger->SetGenerateOverlapEvents(true);

	// Visual mesh: flat glowing disc (teleport-pad look). No solid collision, so any
	// unit can walk across it; only the trigger box launches players. Zombies use
	// direct-steering movement and don't need the pad to block or affect navigation.
	PadMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PadMesh"));
	PadMesh->SetupAttachment(Trigger);
	PadMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PadMesh->SetCanEverAffectNavigation(false);
}

void ABoBJumpPad::BeginPlay()
{
	Super::BeginPlay();

	Trigger->OnComponentBeginOverlap.AddDynamic(this, &ABoBJumpPad::OnTriggerBeginOverlap);

	// Force the "teleport pad" look at runtime: no collision, flattened to the floor,
	// glowing material. All applied at runtime so it never depends on per-instance
	// serialized values (the project's #1 gremlin) - consistent in PIE and packaged.
	if (PadMesh)
	{
		PadMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PadMesh->SetCanEverAffectNavigation(false);
		const FVector S = PadMesh->GetRelativeScale3D();
		PadMesh->SetRelativeScale3D(FVector(S.X, S.Y, 0.05f));
		if (UMaterialInterface* Glow = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Game/Variant_Shooter/Skins/M_BoBPadGlow.M_BoBPadGlow")))
		{
			PadMesh->SetMaterial(0, Glow);
		}
	}

	// Ambient particles above the pad, reusing the template's NS_JumpPad system.
	// Attached at runtime (no per-instance serialization involved).
	if (UNiagaraSystem* FX = LoadObject<UNiagaraSystem>(nullptr,
		TEXT("/Game/LevelPrototyping/Interactable/JumpPad/Assets/NS_JumpPad.NS_JumpPad")))
	{
		PadFX = UNiagaraFunctionLibrary::SpawnSystemAttached(
			FX, Trigger, NAME_None, FVector(0.0f, 0.0f, 10.0f), FRotator::ZeroRotator,
			EAttachLocation::KeepRelativeOffset, false);
	}
}

void ABoBJumpPad::OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep)
{
	ACharacter* Char = Cast<ACharacter>(OtherActor);
	if (!Char)
	{
		return;
	}

	// Only launch players: zombies are AI-controlled, IsPlayerControlled() is false -> ignored
	if (!Char->IsPlayerControlled())
	{
		return;
	}

	// Launch on the server; LaunchCharacter velocity replicates to clients (listen-server 2P)
	if (!HasAuthority())
	{
		return;
	}

	FVector LaunchVel(0.0f, 0.0f, LaunchZSpeed);
	const bool bHorizontal = !FMath::IsNearlyZero(LaunchForwardSpeed);
	if (bHorizontal)
	{
		LaunchVel += GetActorForwardVector() * LaunchForwardSpeed;
	}

	// Vertical launch keeps horizontal momentum (XYOverride=false); override XY when a forward push is set
	Char->LaunchCharacter(LaunchVel, bHorizontal, true);

	// Launch feedback: re-trigger the pad particles as a burst
	if (PadFX)
	{
		PadFX->Activate(true);
	}
}
