#include "RaceManager.h"
#include "Checkpoint.h"

ARaceManager::ARaceManager()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ARaceManager::BeginPlay()
{
	Super::BeginPlay();

	// Assign each checkpoint's index based on its position in this array, so the
	// order you arrange them in the editor is the official otder in which racers must pass through them.
	for (int32 i = 0; i < Checkpoints.Num(); ++i)
	{
		if (Checkpoints[i])
		{
			Checkpoints[i]->SetCheckpointIndex(i);
		}
	}
}

void ARaceManager::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bRaceStarted)
	{
		RaceElapsedTime += DeltaSeconds;
	}
}

void ARaceManager::StartRace()
{
	bRaceStarted = true;
	RaceElapsedTime = 0.f;
}
