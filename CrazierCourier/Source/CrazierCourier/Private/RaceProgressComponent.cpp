#include "RaceProgressComponent.h"
#include "Checkpoint.h"
#include "RaceManager.h"
#include "Kismet/GameplayStatics.h"

URaceProgressComponent::URaceProgressComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
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
}

void URaceProgressComponent::NotifyCheckpointPassed(ACheckpoint* Checkpoint)
{
	if (!Checkpoint || !RaceManagerRef || bRaceFinished)
	{
		return;
	}

	if (!RaceManagerRef->IsRaceStarted())
	{
		// Race hasn't officially started counting yet - ignore checkpoint touches
		// during free-roam, pre-race positioning, etc.
		return;
	}

	const int32 PassedIndex = Checkpoint->GetCheckpointIndex();

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
}
