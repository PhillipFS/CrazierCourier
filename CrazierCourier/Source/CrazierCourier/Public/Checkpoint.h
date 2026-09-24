// A non-colliding trigger volume placed in the level. Registers when a racer's
// vehicle passes through it, but does not physically block or affect movement.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Checkpoint.generated.h"

class UBoxComponent;
class UStaticMeshComponent; // added for visual representation of the checkpoint

UCLASS()
class CRAZIERCOURIER_API ACheckpoint : public AActor
{
	GENERATED_BODY()

public:
	ACheckpoint();

	// The position of this checkpoint within the race's ordered sequence.
	// Assigned automatically by ARaceManager on BeginPlay, based on the order
	// checkpoints appear in the manager's Checkpoints array.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Checkpoint")
	int32 CheckpointIndex = 0;

	void SetCheckpointIndex(int32 NewIndex) { CheckpointIndex = NewIndex; }
	int32 GetCheckpointIndex() const { return CheckpointIndex; }

	// Values for whatever item this checkpoint hands out when it's used as a
	// pickup point. - SET IN THE EDITOR FOR SIMPLICITY, INDIVIDUALLY FOR EACH PICKUP
	UPROPERTY(EditAnywhere, Category = "Pickup Item")
	float ItemHealth = 0.f;

	UPROPERTY(EditAnywhere, Category = "Pickup Item")
	float ItemWeight = 0.f;

	float GetItemHealth() const { return ItemHealth; }
	float GetItemWeight() const { return ItemWeight; }

	// used by the pickup/delivery system to only show whichever checkpoints are 
	// part of the currently assigned pair.
	void SetVisualMeshVisible(bool bVisible);

protected:
	UPROPERTY(VisibleAnywhere, Category = "Checkpoint")
	TObjectPtr<USceneComponent> Root;

	// Overlap-only volume - no physical collision with the vehicle, only detection.
	UPROPERTY(VisibleAnywhere, Category = "Checkpoint")
	TObjectPtr<UBoxComponent> TriggerVolume;
	// Static Mesh field in the Details panel - Has no collision of its own.
	UPROPERTY(VisibleAnywhere, Category = "Checkpoint")
	TObjectPtr<UStaticMeshComponent> VisualMesh;

	UFUNCTION()
	void OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);
};
