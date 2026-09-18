// Attach one of these to every vehicle (player and each AI). Tracks
// that specific racer's checkpoint progress, lap count, and per-lap times
// independently of every other racer.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RaceProgressComponent.generated.h"

class ACheckpoint;
class ARaceManager;
class UStaticMeshComponent;
class UStaticMesh;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnLapCompleted, int32, LapNumber, float, LapTime);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRaceFinishedForRacer);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPickupCollected);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDeliveryCompleted);

UCLASS(ClassGroup = (Racing), meta = (BlueprintSpawnableComponent))
class CRAZIERCOURIER_API URaceProgressComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URaceProgressComponent();

	// Called by ACheckpoint when this racer's owning actor overlaps it.
	void NotifyCheckpointPassed(ACheckpoint* Checkpoint);

	UPROPERTY(BlueprintAssignable, Category = "Race")
	FOnLapCompleted OnLapCompleted;

	UPROPERTY(BlueprintAssignable, Category = "Race")
	FOnRaceFinishedForRacer OnRaceFinished;

	UFUNCTION(BlueprintPure, Category = "Race")
	int32 GetLapCount() const { return LapCount; }

	UFUNCTION(BlueprintPure, Category = "Race")
	bool IsRaceFinished() const { return bRaceFinished; }

	UFUNCTION(BlueprintPure, Category = "Race")
	const TArray<float>& GetLapTimes() const { return LapTimes; }

	// --- Pickup / delivery tasks ---
	UPROPERTY(BlueprintAssignable, Category = "Delivery")
	FOnPickupCollected OnPickupCollected;

	UPROPERTY(BlueprintAssignable, Category = "Delivery")
	FOnDeliveryCompleted OnDeliveryCompleted;

	UFUNCTION(BlueprintCallable, Category = "Delivery")
	void AssignRandomPickupDelivery();

	UFUNCTION(BlueprintPure, Category = "Delivery")
	ACheckpoint* GetCurrentTargetCheckpoint() const;

protected:
	virtual void BeginPlay() override;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// Index of the next checkpoint this racer must pass, within the race
	// manager's Checkpoints array. A checkpoint can only register if its index
	// matches this exactly to avoid checkpoint skipping.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Race")
	int32 CurrentCheckpointIndex = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Race")
	int32 LapCount = 0;

	// Sized to RaceManager's NumberOfLaps in BeginPlay. LapTimes[i] holds how
	// long lap (i+1) took, filled in as each lap completes.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Race")
	TArray<float> LapTimes;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Race")
	bool bRaceFinished = false;

	// The race manager's elapsed time at the moment this racer's last lap completed,
	// used to caculate how long each individual lap took.
	float LastLapCompletionTime = 0.0f;

	UPROPERTY()
	TObjectPtr<ARaceManager> RaceManagerRef;

	// --- Pickup / delivery states ---
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Delivery")
	TObjectPtr<ACheckpoint> PickupCheckpoint;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Delivery")
	TObjectPtr<ACheckpoint> DeliveryCheckpoint;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Delivery")
	bool bHasPickedUp = false;

	UPROPERTY(VisibleAnywhere, Category = "Delivery")
	TObjectPtr<UStaticMeshComponent> IndicatorMesh;

	UPROPERTY(EditAnywhere, Category = "Delivery")
	float IndicatorHeightAboveVehicle = 150.f;

	UPROPERTY(EditAnywhere, Category = "Delivery")
	TObjectPtr<UStaticMesh> IndicatorMeshAsset;
private:
	void HandlePickupDeliveryCheckpoint(ACheckpoint* Checkpoint);

	// DEBUG: tracks the last checkpoint the indicator pointed at, so TickComponent
	// can print a one-time message only when the target actually changes,
	// rather than spamming every frame.
	ACheckpoint* LastIndicatorTarget = nullptr;

};
