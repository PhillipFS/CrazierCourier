#include "Checkpoint.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "RaceProgressComponent.h"

ACheckpoint::ACheckpoint()
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	TriggerVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("TriggerVolume"));
	TriggerVolume->SetupAttachment(Root);
	TriggerVolume->InitBoxExtent(FVector(200.f, 200.f, 200.f));

	// Overlap-only: detects overlaps with dynamic actors (like the
	// vehicle) but never physically blocks or pushes anything.
	TriggerVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	TriggerVolume->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	TriggerVolume->SetGenerateOverlapEvents(true);

	TriggerVolume->OnComponentBeginOverlap.AddDynamic(this, &ACheckpoint::OnTriggerBeginOverlap);

	// Cosmetic mesh for visualizing the checkpoint in the editor and game.
	VisualMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VisualMesh"));
	VisualMesh->SetupAttachment(Root);
	VisualMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	VisualMesh->SetGenerateOverlapEvents(false);
}

void ACheckpoint::OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (!OtherActor || OtherActor == this)
	{
		return;
	}

	// DEBUG: print if checkpoint is touched and by what actor.
	if (GEngine)
	{
		const FString Msg = FString::Printf(TEXT("Checkpoint %d touched by %s"), CheckpointIndex, *OtherActor->GetName());
		GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Cyan, Msg);
	}
	//
	
	// Find the race progress tracker on whatever just drove through.
	if (URaceProgressComponent* Progress = OtherActor->FindComponentByClass<URaceProgressComponent>())
	{
		Progress->NotifyCheckpointPassed(this);
	}
}
