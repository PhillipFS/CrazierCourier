#include "RaceProgressComponent.h"
#include "Checkpoint.h"
#include "RaceManager.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"

namespace
{
	// Picks a random entry from Pool, skipping any unassigned (None/nullptr)
	// slots - protects against empty array elements in the editor causing a
	// crash later when the picked "checkpoint" turns out to be null.
	ACheckpoint* PickRandomValidCheckpoint(const TArray<ACheckpoint*>& Pool)
	{
		TArray<ACheckpoint*> ValidEntries;
		ValidEntries.Reserve(Pool.Num());
		for (ACheckpoint* Entry : Pool)
		{
			if (Entry)
			{
				ValidEntries.Add(Entry);
			}
		}

		if (ValidEntries.Num() == 0)
		{
			return nullptr;
		}

		return ValidEntries[FMath::RandRange(0, ValidEntries.Num() - 1)];
	}
}

URaceProgressComponent::URaceProgressComponent()
{
	// Ticking is needed now so the indicator mesh can continuously 
	// rotate to face the current pickup/delivery target.
	PrimaryComponentTick.bCanEverTick = true;

	IndicatorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("IndicatorMesh"));
	IndicatorMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	IndicatorMesh->SetGenerateOverlapEvents(false);

	// Must be Movable - it needs to follow the vehicle around and rotate every
	// frame in TickComponent.
	IndicatorMesh->SetMobility(EComponentMobility::Movable);
}

void URaceProgressComponent::BeginPlay()
{
	Super::BeginPlay();

	// Find the single RaceManager placed in the level.
	TArray<AActor*> FoundManagers;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ARaceManager::StaticClass(), FoundManagers);

	if (FoundManagers.Num() > 0)
	{
		RaceManagerRef = Cast<ARaceManager>(FoundManagers[0]);
	}

	if (!RaceManagerRef)
	{
		UE_LOG(LogTemp, Warning, TEXT("URaceProgressComponent: No ARaceManager found in the level."));
		return;
	}

	// Lap times array size matches the editable lap count, per lap slot.
	LapTimes.SetNumZeroed(RaceManagerRef->GetNumberOfLaps());

	// For indicator:
	if (AActor* Owner = GetOwner())
	{
		if (USceneComponent* OwnerRoot = Owner->GetRootComponent())
		{
			IndicatorMesh->RegisterComponent();
			IndicatorMesh->AttachToComponent(OwnerRoot, FAttachmentTransformRules::KeepRelativeTransform);
			IndicatorMesh->SetRelativeLocation(FVector(0.0f, 0.0f, IndicatorHeightAboveVehicle));

			// Apply whatever mesh was picked in the Details panel:
			if (IndicatorMeshAsset)
			{
				IndicatorMesh->SetStaticMesh(IndicatorMeshAsset);
			}
		}
	}

	// Issue first random pickup/delivery pair:
	AssignRandomPickupDelivery();
}

void URaceProgressComponent::NotifyCheckpointPassed(ACheckpoint* Checkpoint)
{
	// Pickup/delivery is a separate task from the race lap sequence - it
	// runs regardless of whether this checkpoint happens to be the next one
	// expected in the lap order.
	HandlePickupDeliveryCheckpoint(Checkpoint);

	const int32 PassedIndex = Checkpoint->GetCheckpointIndex();

	// (UNUSED) RACING:
	/*
	// 	if (!Checkpoint || !RaceManagerRef || bRaceFinished)
	{
		return;
	}

	if (!RaceManagerRef->IsRaceStarted())
	{
		// Race hasn't officially started counting yet - ignore checkpoint touches
		// during free-roam, pre-race positioning, etc.
		return;
	}

	// "no skipping" rule: the checkpoint that was touched must
	// be exactly the next one this racer is expecting. Touching checkpoint 3
	// while expecting checkpoint 1 does nothing; checkpoint 1 must be passed first.
	if (PassedIndex != CurrentCheckpointIndex)
	{
		// DEBUG:
		if (GEngine && GetOwner())
		{
			const FString Msg = FString::Printf(TEXT("%s: IGNORED checkpoint %d, expected %d next"),
				*GetOwner()->GetName(), PassedIndex, CurrentCheckpointIndex);
			GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Red, Msg);
		}
		//
		return;
	}

	CurrentCheckpointIndex++;

	const int32 TotalCheckpoints = RaceManagerRef->GetCheckpoints().Num();
	if (CurrentCheckpointIndex >= TotalCheckpoints)
	{
		// All checkpoints cleared = a full lap is complete.
		const float Now = RaceManagerRef->GetElapsedTime();
		const float ThisLapTime = Now - LastLapCompletionTime;
		LastLapCompletionTime = Now;

		if (LapTimes.IsValidIndex(LapCount))
		{
			LapTimes[LapCount] = ThisLapTime;
		}

		LapCount++;
		CurrentCheckpointIndex = 0;

		// DEBUG:
		if (GEngine && GetOwner())
		{
			const FString Msg = FString::Printf(TEXT("%s: LAP %d complete (%.2fs)"), *GetOwner()->GetName(), LapCount, ThisLapTime);
			GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Yellow, Msg);
		}
		//

		OnLapCompleted.Broadcast(LapCount, ThisLapTime);

		// if number of laps completed is equal to the total number of laps, the race is finished.
		if (LapCount >= RaceManagerRef->GetNumberOfLaps())
		{
			bRaceFinished = true;
			OnRaceFinished.Broadcast();
		}
	}
	*/
}

void URaceProgressComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!IndicatorMesh)
	{
		return;
	}

	// --- Delivery timer countdown ---
	if (PickupCheckpoint && DeliveryCheckpoint && !bTimeoutAlreadyHandled)
	{
		DeliveryTimeRemaining = FMath::Max(0.0f, DeliveryTimeRemaining - DeltaTime);

		if (GEngine && GetOwner())
		{
			const int32 TimerDebugKey = static_cast<int32>(GetUniqueID()) + 1;
			const FString Msg = FString::Printf(TEXT("%s: Time to deliver -> %.1fs"),
				*GetOwner()->GetName(), DeliveryTimeRemaining);
			GEngine->AddOnScreenDebugMessage(TimerDebugKey, 0.0f, FColor::Turquoise, Msg);
		}

		if (DeliveryTimeRemaining <= 0.0f)
		{
			bTimeoutAlreadyHandled = true;
			AwardPoints(1.0f); // 1.0f is 100% of the time used.
			AssignRandomPickupDelivery();
		}
	}

	ACheckpoint* Target = GetCurrentTargetCheckpoint();

	// TEMP DEBUG: if no real pickup/delivery target is assigned yet, fall back
	// to whatever checkpoint happens to be first in RaceManager's list, just to
	// test the indicator's rotation logic on its own. Remove this block once
	// AssignRandomPickupDelivery is confirmed working correctly.
	if (!Target && RaceManagerRef)
	{
		const TArray<ACheckpoint*>& AllCheckpoints = RaceManagerRef->GetCheckpoints();
		if (AllCheckpoints.Num() > 0)
		{
			Target = AllCheckpoints[0];
		}
	}

	if (!Target)
	{
		// No pickup/delivery pair assigned yet, hide the indicator rather than point at nothing.
		IndicatorMesh->SetVisibility(false);
		return;
	}

	IndicatorMesh->SetVisibility(true);

	// DEBUG: print once, only when the target actually changes (not every frame).
	if (Target != LastIndicatorTarget)
	{
		LastIndicatorTarget = Target;

		if (GEngine && GetOwner())
		{
			const FString Msg = FString::Printf(TEXT("%s: Indicator target updated -> %s"),
				*GetOwner()->GetName(), *Target->GetName());
			GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Orange, Msg);
		}
	}

	// DEBUG: persistent readout of what the indicator is currently pointing at,
	// refreshed every frame on the same line (Key = this component's unique ID,
	// so multiple racers each get their own line instead of overwriting each other).
	if (GEngine && GetOwner())
	{
		const int32 DebugKey = static_cast<int32>(GetUniqueID());
		const FString Msg = FString::Printf(TEXT("%s: Indicator -> %s"),
			*GetOwner()->GetName(), *Target->GetName());
		GEngine->AddOnScreenDebugMessage(DebugKey, 0.0f, FColor::White, Msg);
	}

	const FVector MeshLocation = IndicatorMesh->GetComponentLocation();
	const FVector TargetLocation = Target->GetActorLocation();
	const FRotator LookAtRotation = UKismetMathLibrary::FindLookAtRotation(MeshLocation, TargetLocation);
	IndicatorMesh->SetWorldRotation(LookAtRotation);
}

void URaceProgressComponent::AssignRandomPickupDelivery()
{
	if (!RaceManagerRef)
	{
		return;
	}

	const TArray<ACheckpoint*>& PickupPool = RaceManagerRef->GetPickupCheckpoints();
	const TArray<ACheckpoint*>& DeliveryPool = RaceManagerRef->GetDeliveryCheckpoints();

	if (PickupPool.Num() == 0 || DeliveryPool.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("URaceProgressComponent: RaceManager needs at least 1 entry in both PickupCheckpoints and DeliveryCheckpoints."));
		return;
	}

	ACheckpoint* PreviousPickup = PickupCheckpoint;
	ACheckpoint* PreviousDelivery = DeliveryCheckpoint;

	ACheckpoint* NewPickup = nullptr;
	int32 PickupSafetyCounter = 0;
	do
	{
		NewPickup = PickRandomValidCheckpoint(PickupPool);
		PickupSafetyCounter++;
	} while (NewPickup == PreviousDelivery && PickupPool.Num() > 1 && PickupSafetyCounter < 20);

	PickupCheckpoint = NewPickup;
	if (!PickupCheckpoint)
	{
		UE_LOG(LogTemp, Warning, TEXT("URaceProgressComponent: PickupCheckpoints has no valid (non-null) entries - check for empty slots in RaceManager's array."));
		return;
	}

	// Guard against picking the exact same checkpoint actor for both, in case
	// the same one was accidentally added to both pools. If DeliveryPool only
	// has one valid entry and it happens to be that same actor, this just
	// accepts it rather than looping forever.
	ACheckpoint* NewDelivery = nullptr;
	int32 SafetyCounter = 0;
	do
	{
		NewDelivery = PickRandomValidCheckpoint(DeliveryPool);
		SafetyCounter++;
	} while (NewDelivery == PickupCheckpoint && DeliveryPool.Num() > 1 && SafetyCounter < 20);

	if (!NewDelivery)
	{
		UE_LOG(LogTemp, Warning, TEXT("URaceProgressComponent: DeliveryCheckpoints has no valid (non-null) entries - check for empty slots in RaceManager's array."));
		return;
	}

	DeliveryCheckpoint = NewDelivery;
	bHasPickedUp = false;

	// Start the delivery timer fresh for this new pair.
	DeliveryTimeRemaining = TimeToReachCheckpoint;
	bTimeoutAlreadyHandled = false;

	// Hide whichever checkpoints made up the outgoing pair, then show the new
	// pair - so at any given moment, only the two checkpoints actually
	// relevant to this racer's current task are visible.
	if (PreviousPickup)
	{
		PreviousPickup->SetVisualMeshVisible(false);
	}
	if (PreviousDelivery)
	{
		PreviousDelivery->SetVisualMeshVisible(false);
	}
	PickupCheckpoint->SetVisualMeshVisible(true);
	DeliveryCheckpoint->SetVisualMeshVisible(true);

	if (GEngine && GetOwner())
	{
		const FString Msg = FString::Printf(TEXT("%s: New task - Pickup %s, Deliver %s"),
			*GetOwner()->GetName(), *PickupCheckpoint->GetName(), *DeliveryCheckpoint->GetName());
		GEngine->AddOnScreenDebugMessage(-1, 4.0f, FColor::Cyan, Msg);
	}
}

ACheckpoint* URaceProgressComponent::GetCurrentTargetCheckpoint() const
{
	return bHasPickedUp ? DeliveryCheckpoint : PickupCheckpoint;
}

void URaceProgressComponent::HandlePickupDeliveryCheckpoint(ACheckpoint* Checkpoint)
{
	if (!Checkpoint)
	{
		return;
	}

	if (Checkpoint == PickupCheckpoint && !bHasPickedUp)
	{
		bHasPickedUp = true;
		OnPickupCollected.Broadcast();
		// assign item values to player raceprogresscomponent:
		AssignPickupItemValues(Checkpoint);

		if (GEngine && GetOwner())
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Green, FString::Printf(TEXT("%s: Pickup collected"), *GetOwner()->GetName()));
		}
	}
	else if (Checkpoint == DeliveryCheckpoint)
	{
		if (bHasPickedUp)
		{
			bHasPickedUp = false;
			OnDeliveryCompleted.Broadcast();

			// Assign delivery checkpoint values to the player when
			// reached. (Used by default to reset item health and weight
			// variables to 0, but can be used to set different values.)
			AssignPickupItemValues(Checkpoint);

			if (GEngine && GetOwner())
			{
				// DEBUG:
				GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Red, FString::Printf(TEXT("%s: Delivery complete!"), *GetOwner()->GetName()));
			}

			// get used time percentage and award points based on that using the AwardPoints function:
			const float TimePercentUsed = (TimeToReachCheckpoint > 0.0f)
				? FMath::Clamp((TimeToReachCheckpoint - DeliveryTimeRemaining) / TimeToReachCheckpoint, 0.0f, 1.0f)
				: 1.0f;
			AwardPoints(TimePercentUsed);

			// Hand out a new random pair of checkpoints for the next delivery cycle.
			AssignRandomPickupDelivery();
		}
		else
		{
			// Delivery touched without Pickup first:
			if (GEngine && GetOwner())
			{
				// DEBUG:
				GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Red, FString::Printf(TEXT("%s: Delivery ignored - no pickup yet"), *GetOwner()->GetName()));
			}
		}
	}
}

void URaceProgressComponent::AwardPoints(float TimePercentUsed)
{
	float PointsToAward = 0.0f;

	if (TimePercentUsed >= 1.0f)
	{
		// Ran out of time, no points awarded:
		PointsToAward = 0.0f;
	}
	else if (TimePercentUsed <= 0.25f)
	{
		// Delivered within the first 25% of the allotted time - full reward.
		PointsToAward = MaximumPoints;
	}
	else
	{
		// Deduct that percentage of time used from the maximum - e.g. 33% of
		// the time used deducts 33% of MaximumPoints, awarding the remaining
		// 67%. Never drops below MinimumPoints for an on-time delivery.
		PointsToAward = FMath::Max(MinimumPoints, MaximumPoints * (1.0f - TimePercentUsed));
	}

	TotalPoints += PointsToAward;
	OnPointsAwarded.Broadcast(PointsToAward, TotalPoints);

	if (GEngine && GetOwner())
	{
		// DEBUG: print the awarded points and total so far, along with the percentage of time used to earn it.
		const FString Msg = FString::Printf(TEXT("%s: Awarded %.1f points (%.0f%% of time used) - Total: %.1f"),
			*GetOwner()->GetName(), PointsToAward, TimePercentUsed * 100.0f, TotalPoints);
		GEngine->AddOnScreenDebugMessage(-1, 4.0f, FColor::Emerald, Msg);
	}
}

void URaceProgressComponent::AssignPickupItemValues(ACheckpoint* Checkpoint)
{
	if (!Checkpoint)
	{
		return;
	}

	PickupItemHealth = Checkpoint->GetItemHealth();
	PickupItemWeight = Checkpoint->GetItemWeight();

	if (GEngine && GetOwner())
	{
		const FString Msg = FString::Printf(TEXT("%s: Item values updated - Health %.1f, Weight %.1f"),
			*GetOwner()->GetName(), PickupItemHealth, PickupItemWeight);
		GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Yellow, Msg);
	}
}
