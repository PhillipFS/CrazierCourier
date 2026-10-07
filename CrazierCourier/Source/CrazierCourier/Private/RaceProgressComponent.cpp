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

		// Bind collision damage to the vehicle's own physics body. Only
		// UPrimitiveComponents can generate hit events - the vehicle's root
		// (its Mesh/Box) should already be one, since it's what's physically
		// simulated.
		if (UPrimitiveComponent* OwnerPrimitive = Cast<UPrimitiveComponent>(Owner->GetRootComponent()))
		{
			// Ensure hit events are enabled, in case the "Simulation
			// Generates Hit Events" checkbox is off in the editor.
			OwnerPrimitive->SetNotifyRigidBodyCollision(true);
			OwnerPrimitive->OnComponentHit.AddDynamic(this, &URaceProgressComponent::OnVehicleHit);
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

	// NOTE:
	// checkpoints are set as invisible when passed individually in the HandlePickupDeliveryCheckpoint().

	// Set next pair of checkpoints to visible so the player can see where to go next.
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

		if (PickupCheckpoint)
		{
			PickupCheckpoint->SetVisualMeshVisible(false);
		}
	}
	else if (Checkpoint == DeliveryCheckpoint)
	{
		if (bHasPickedUp)
		{
			bHasPickedUp = false;
			OnDeliveryCompleted.Broadcast();

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

			// Assign delivery checkpoint values to the player when
			// reached. (Used by default to reset item health and weight
			// variables to 0, but can be used to set different values.)
			AssignPickupItemValues(Checkpoint);

			// make mesh invisible, now that delivery is complete and the next random pair will be assigned.
			if (DeliveryCheckpoint)
			{
				DeliveryCheckpoint->SetVisualMeshVisible(false);
			}

			// Hand out a new pair of checkpoints for the next delivery cycle.
			AssignRandomPickupDelivery();
		}
		else
		{
			// Delivery touched without Pickup first:
			if (GEngine && GetOwner())
			{
				// DEBUG:
				// GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Red, FString::Printf(TEXT("%s: Delivery ignored - no pickup yet"), *GetOwner()->GetName()));
			}
		}
	}
}

void URaceProgressComponent::AwardPoints(float TimePercentUsed)
{
	// (DEBUGS PRINT IN EMERALD:)
	
	// + + T I M E  M O D I F I E R S
	float PointsToAward = 0.0f;
	
	// DEBUG: Show awarded points before time modifier is applied.
	if (GEngine && GetOwner())
	{
		const FString Msg = FString::Printf(TEXT("MAX points to award: %.1f"),
			MaximumPoints);
		GEngine->AddOnScreenDebugMessage(-1, 25.0f, FColor::Emerald, Msg);
	}

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
	// *


	// + + H E A L T H  M O D I F I E R S
	// get percentage of remaining health relative to full heatlh value of that item:
	float PercentPickupHealth = 0.0f;

	// Guard for 0 value:
	if (PickupItemCurrentHealth > 0.0f)
	{
		PercentPickupHealth = PickupItemCurrentHealth / PickupItemHealth;
		if (GEngine && GetOwner())
		{
			const FString Msg = FString::Printf(TEXT("Health percent at DELIVERY: %.1f"),
				PercentPickupHealth);
			GEngine->AddOnScreenDebugMessage(-1, 25.0f, FColor::Emerald, Msg);
		}
	}

	// DEBUG: Show awarded points after time modifier, before health modifier is applied.
	if (GEngine && GetOwner())
	{
		const FString Msg = FString::Printf(TEXT("Award post TIME modifier: %.1f"),
			PointsToAward);
		GEngine->AddOnScreenDebugMessage(-1, 25.0f, FColor::Emerald, Msg);
	}

	if (PercentPickupHealth < 0.75f) {
		// Simply scale the points awarded by the percentage of health remaining if it's below 75%. 
		// For example, if the player delivers the item with 50% health, they will receive only 50% 
		// of the points they would have earned otherwise.
		PointsToAward =  PointsToAward * PercentPickupHealth;
	}

	else {
		// Modify score with a bonus for delivering the pickup item with more than 75% health remaining. 
		// For example, if the player delivers the item with 90% health, they will receive a 15% bonus to their score.
		// (1.0 + (0.9 - 0.75) = 1.15).
		PointsToAward = PointsToAward * (1.0f + (PercentPickupHealth - 0.75f));
	}

	// DEBUG: Show awarded points after health modifier is applied.
	if (GEngine && GetOwner())
	{
		const FString Msg = FString::Printf(TEXT("Award post HEALTH modifier: %.1f"),
			PointsToAward);
		GEngine->AddOnScreenDebugMessage(-1, 25.0f, FColor::Emerald, Msg);
	}
	// *


	// + + T O T A L  P O I N T S  A W A R D E D

	TotalPoints += PointsToAward;
	OnPointsAwarded.Broadcast(PointsToAward, TotalPoints);

	if (GEngine && GetOwner())
	{
		// DEBUG: print the awarded points and total so far, along with the percentage of time used to earn it.
		const FString Msg = FString::Printf(TEXT("%s: (%.0f%% of time used) - TOTAL POINTS: %.1f"),
			*GetOwner()->GetName(), TimePercentUsed * 100.0f, TotalPoints);
		GEngine->AddOnScreenDebugMessage(-1, 25.0f, FColor::Emerald, Msg);
	}
	// *
}

void URaceProgressComponent::AssignPickupItemValues(ACheckpoint* Checkpoint)
{
	if (!Checkpoint)
	{
		return;
	}

	// Assign the pickup item values from the CHECKPOINT to THIS RACEPROGRESS component variables.
	PickupItemHealth = Checkpoint->GetItemHealth();
	PickupItemCurrentHealth = Checkpoint->GetItemHealth();
	PickupItemWeight = Checkpoint->GetItemWeight();

	if (GEngine && GetOwner())
	{
		const FString Msg = FString::Printf(TEXT("%s: Item values updated - CurrentHealth %.1f, Weight %.1f"),
			*GetOwner()->GetName(), PickupItemCurrentHealth, PickupItemWeight);
		GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Yellow, Msg);
	}
}

void URaceProgressComponent::OnVehicleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{

	if (!OtherActor || OtherActor == GetOwner())
	{
		return;
	}

	if (!OtherActor->ActorHasTag(CollideableTag))
	{
		return;
	}

	// D A M A G I N G		I T E M S	O N		C O L L I S I O N 
	//
	// Only while actually carrying an item - nothing to damage otherwise.
	if (!bHasPickedUp)
	{
		return;
	}

	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f; // (variable for marking the time of the last damage event)

	// ignore hits that happen too soon after the last damage-applying hit:
	if (Now - LastDamageTime < HitDamageCooldown)
	{
		return;
	}

	// NormalImpulse is the actual physical impulse the collision resolved with:
	const float ImpactStrength = NormalImpulse.Size();

	// Too soft to count as a real impact, doesn't damage an item:
	if (ImpactStrength < MinImpulseForDamage)
	{
		return;
	}

	// Otherwise, mark now as last damage event:
	LastDamageTime = Now;

	// Scale damage linearly between 0 (minimum threshold) and
	// full CollisionDamageAmount ( = or > MaxImpulseForFullDamage).
	const float ImpulseRange = FMath::Max(1.f, MaxImpulseForFullDamage - MinImpulseForDamage);
	const float DamageAlpha = FMath::Clamp((ImpactStrength - MinImpulseForDamage) / ImpulseRange, 0.f, 1.f);
	const float DamageToApply = CollisionDamageAmount * DamageAlpha;

	// Apply damage with modifiers from math above:
	PickupItemCurrentHealth = FMath::Max(0.f, PickupItemCurrentHealth - DamageToApply);

	if (GEngine && GetOwner())
	{
		const FString Msg = FString::Printf(TEXT("%s: Hit %s - Damage Taken: -%.1f (Health now %.1f)"),
			*GetOwner()->GetName(), *OtherActor->GetName(), DamageToApply, PickupItemCurrentHealth);
		GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Orange, Msg);
	}
}
