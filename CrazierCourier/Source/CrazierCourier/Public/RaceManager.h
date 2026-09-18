// Place one of these in the level. Assign Checkpoint actors to the
// Checkpoints array in the order racers should pass through them, and set
// how many laps the race is. Call StartRace() whenever your countdown/start
// logic determines the race should begin timing.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RaceManager.generated.h"

class ACheckpoint;

UCLASS()
class CRAZIERCOURIER_API ARaceManager : public AActor
{
	GENERATED_BODY()

public:
	ARaceManager();

	// Assign your placed Checkpoint actors here, in the order racers must pass
	// through them. This way, players or AI can't skip a checkpoint.
	UPROPERTY(EditAnywhere, Category = "Race")
	TArray<ACheckpoint*> Checkpoints;
	UPROPERTY(EditAnywhere, Category = "Delivery")
	TArray<ACheckpoint*> PickupCheckpoints;
	UPROPERTY(EditAnywhere, Category = "Delivery")

	TArray<ACheckpoint*> DeliveryCheckpoints;
	// Editable per-race lap count. Also determines the size of each racer's LapTimes array.
	UPROPERTY(EditAnywhere, Category = "Race")
	int32 NumberOfLaps = 3;

	// Call this when the race should actually begin counting time
	// (e.g. from a countdown widget or start-light sequence).
	UFUNCTION(BlueprintCallable, Category = "Race")
	void StartRace();

	UFUNCTION(BlueprintPure, Category = "Race")
	bool IsRaceStarted() const { return bRaceStarted; }

	UFUNCTION(BlueprintPure, Category = "Race")
	float GetElapsedTime() const { return RaceElapsedTime; }

	UFUNCTION(BlueprintPure, Category = "Race")
	int32 GetNumberOfLaps() const { return NumberOfLaps; }

	const TArray<ACheckpoint*>& GetCheckpoints() const { return Checkpoints; }
	const TArray<ACheckpoint*>& GetPickupCheckpoints() const { return PickupCheckpoints; }
	const TArray<ACheckpoint*>& GetDeliveryCheckpoints() const { return DeliveryCheckpoints; }

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// True once the race has actually started (set via StartRace()).
	// The timer only advances while this is true.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Race")
	bool bRaceStarted = false;

	// Total race time elapsed since StartRace() was called, running continuously
	// in the background - each racer's individual lap times are derived from this.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Race")
	float RaceElapsedTime = 0.f;
};
