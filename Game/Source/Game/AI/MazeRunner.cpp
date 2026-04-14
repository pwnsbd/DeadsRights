#include "MazeRunner.h"
#include "Components/StaticMeshComponent.h"
#include "../Conversion/CubeToSphere.h"
#include "../Orchestrator.h"
#include "../Artifact/Artifact.h"
#include "GameFramework/Pawn.h"
#include "MazeNavigator.h"

AMazeRunner::AMazeRunner()
{
	PrimaryActorTick.bCanEverTick = true;
	MeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComp"));
	SetRootComponent(MeshComp);

	MeshComp->SetCollisionProfileName(TEXT("Trigger"));
	MeshComp->SetGenerateOverlapEvents(true);
	MeshComp->SetCanEverAffectNavigation(false);
	MeshComp->SetWorldScale3D(FVector(0.5f, 0.5f, 0.5f));
}

void AMazeRunner::BeginPlay()
{
	Super::BeginPlay();
	if (MeshComp)
		DynamicMat = MeshComp->CreateDynamicMaterialInstance(0);
	SetAIColor(FLinearColor::Green);
}

void AMazeRunner::SetAIColor(FLinearColor NewColor)
{
	if (DynamicMat)
		DynamicMat->SetVectorParameterValue(TEXT("Color"), NewColor);
}

// IN MAZERUNNER.CPP

int32 AMazeRunner::FindClosestPathIndex(const TArray<FVector> &NewPath)
{
	if (NewPath.Num() == 0)
		return 0;

	FVector CurrentLocal = GetRootComponent()->GetRelativeLocation();
	float BestDistSq = FLT_MAX;
	int32 BestIndex = 0;

	for (int32 i = 0; i < NewPath.Num(); i++)
	{
		// Path nodes are stored as directions from center; convert to local surface loc
		FVector NodeLocal = NewPath[i] + (NewPath[i].GetSafeNormal() * 17.0f);
		float DistSq = FVector::DistSquared(CurrentLocal, NodeLocal);

		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			BestIndex = i;
		}
	}
	// Return the next node after the closest one so we keep moving forward
	return FMath::Min(BestIndex + 1, NewPath.Num() - 1);
}

void AMazeRunner::SetPath(const TArray<FVector> &NewLocalPath, ACubeToSphere *InSphereActor)
{
	TargetSphere = InSphereActor;

	// --- SMART INTERSECTION FIX ---
	// Instead of resetting to 0, find where we are in the new path
	if (bIsMoving && PathToFollow.Num() > 0)
	{
		PathToFollow = NewLocalPath;
		CurrentTargetIndex = FindClosestPathIndex(NewLocalPath);
	}
	else
	{
		PathToFollow = NewLocalPath;
		CurrentTargetIndex = 0;
	}

	bIsMoving = (PathToFollow.Num() > 0 && TargetSphere);
}

void AMazeRunner::Die()
{
	GetWorldTimerManager().ClearTimer(EscapeTimerHandle);
	GetWorldTimerManager().ClearTimer(RePathTimerHandle);

	// Safely drop the artifact if we are holding it
	if (MyTarget && CurrentState == EAIState::Escaping)
	{
		AArtifact *DroppedArtifact = MyTarget;
		MyTarget = nullptr;

		if (DroppedArtifact)
		{
			DroppedArtifact->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

			if (TargetSphere)
			{
				DroppedArtifact->AttachToActor(TargetSphere, FAttachmentTransformRules::KeepWorldTransform);
				// FIX 1: Tell the manager exactly what cell the AI died on!
				DroppedArtifact->CurrentCell = TargetSphere->WorldToMazeCell(GetActorLocation());
			}

			DroppedArtifact->SetActorLocation(GetActorLocation());
			DroppedArtifact->SetActorHiddenInGame(false);
			DroppedArtifact->SetActorEnableCollision(true);

			// FIX 2: Officially tell the game the item is dropped!
			DroppedArtifact->bIsCarried = false;
			DroppedArtifact->Carrier = nullptr;
		}
	}

	this->Destroy();
}

void AMazeRunner::NotifyActorBeginOverlap(AActor *OtherActor)
{
	if (OtherActor && OtherActor->IsA(APawn::StaticClass()))
	{
		// Keep it clean: If the player overlaps us, just run the exact same death logic!
		Die();
	}
}

void AMazeRunner::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	APawn *PlayerPawn = GetWorld()->GetFirstPlayerController()->GetPawn();
	AOrchestrator *Orchestrator = Cast<AOrchestrator>(GetOwner());
	if (!PlayerPawn || !Orchestrator || !TargetSphere || !Orchestrator->Navigator)
		return;

	FVector CurrentWorldLoc = GetActorLocation();
	FVector PlayerLoc = PlayerPawn->GetActorLocation();
	float DistToPlayer = FVector::Dist(CurrentWorldLoc, PlayerLoc);

	// --- NEW: Foolproof Player Collision Check ---
	// If Unreal's physics engine fails to register the overlap, this math will catch it!
	if (DistToPlayer < 25.0f)
	{
		Die();
		return; // Stop ticking, we are dead!
	}

	// --- 1. STATE COLORS ---
	if (CurrentState == EAIState::Escaping)
		SetAIColor(FLinearColor::Blue);
	else if (CurrentState == EAIState::Fleeing)
		SetAIColor(FLinearColor::Red);
	else
		SetAIColor(FLinearColor::Green);

	// --- 2. SURVIVAL LOGIC ---
	if (CurrentState != EAIState::Escaping)
	{
		if (DistToPlayer < FleeThreshold && CurrentState != EAIState::Fleeing)
		{
			CurrentState = EAIState::Fleeing;
			MyTarget = nullptr;
			// Removed bIsMoving = false to prevent jitter!
		}
		else if (DistToPlayer > SafeThreshold && CurrentState == EAIState::Fleeing)
		{
			CurrentState = EAIState::Hunting;
			// Removed bIsMoving = false to prevent jitter!
		}
	}

	// --- 3. NON-STOP DYNAMIC RE-PATHING ---
	// Instead of stopping, we just trigger a recalculation request
	bool bNeedsNewPath = !bIsMoving;

	if (bIsMoving && FVector::Dist(PlayerLoc, LastPlayerPosForPath) > 100.0f)
	{
		bNeedsNewPath = true;
	}

	if (bNeedsNewPath)
	{
		LastPlayerPosForPath = PlayerLoc;
		TArray<FVector> Path;
		FVector TargetLoc;

		if (CurrentState == EAIState::Escaping || CurrentState == EAIState::Fleeing)
		{
			TargetLoc = Orchestrator->GetFarthestNodeFromActor(PlayerPawn);
		}
		else
		{
			if (!MyTarget)
				Orchestrator->AssignTargetToRunner(this);
			TargetLoc = MyTarget ? MyTarget->GetActorLocation() : Orchestrator->GetFarthestNodeFromActor(PlayerPawn);
		}

		// Linear penalty in MazeNavigator ensures it finds side corridors without freezing
		if (Orchestrator->Navigator->FindPath(CurrentWorldLoc, TargetLoc, Path, PlayerLoc, PathAvoidanceRadius) && Path.Num() > 0)
		{
			TArray<FVector> LocalPath;
			for (FVector P : Path)
				LocalPath.Add(TargetSphere->GetTransform().InverseTransformPosition(P));

			// SetPath now handles the seamless transition
			SetPath(LocalPath, TargetSphere);
		}
	}

	if (!bIsMoving)
		return;

	// --- 4. MOVEMENT (Unchanged) ---
	FVector CurrentLocal = GetRootComponent()->GetRelativeLocation();
	FVector BaseTarget = PathToFollow[CurrentTargetIndex];
	FVector TargetLocal = BaseTarget + (BaseTarget.GetSafeNormal() * 17.0f);

	if (FVector::Dist(CurrentLocal, TargetLocal) < 25.0f)
	{
		CurrentTargetIndex++;
		if (!PathToFollow.IsValidIndex(CurrentTargetIndex))
		{
			if (CurrentState == EAIState::Hunting && MyTarget && !MyTarget->IsHidden())
			{
				MyTarget->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);
				MyTarget->SetActorHiddenInGame(true);
				MyTarget->SetActorEnableCollision(false);
				CurrentState = EAIState::Escaping;
				GetWorldTimerManager().SetTimer(EscapeTimerHandle, this, &AMazeRunner::FinishEscape, 10.0f, false);
			}
			bIsMoving = false;
			return;
		}
		BaseTarget = PathToFollow[CurrentTargetIndex];
		TargetLocal = BaseTarget + (BaseTarget.GetSafeNormal() * 17.0f);
	}

	FVector NewLocal = FMath::VInterpConstantTo(CurrentLocal, TargetLocal, DeltaTime, MovementSpeed);
	NewLocal = NewLocal.GetSafeNormal() * TargetLocal.Size();
	SetActorRelativeLocation(NewLocal);

	FVector ForwardDir = (TargetLocal - NewLocal).GetSafeNormal();
	if (ForwardDir.SizeSquared() > 0.001f)
	{
		SetActorRelativeRotation(FRotationMatrix::MakeFromXZ(ForwardDir, NewLocal.GetSafeNormal()).Rotator());
	}
}

// void AMazeRunner::NotifyActorBeginOverlap(AActor *OtherActor)
// {
// 	if (OtherActor && OtherActor->IsA(APawn::StaticClass()))
// 	{
// 		GetWorldTimerManager().ClearTimer(EscapeTimerHandle);

// 		// If we are killed while holding the artifact, drop it back onto the sphere
// 		if (MyTarget && CurrentState == EAIState::Escaping)
// 		{
// 			AArtifact *DroppedArtifact = MyTarget;
// 			MyTarget = nullptr;

// 			DroppedArtifact->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
// 			if (TargetSphere)
// 				DroppedArtifact->AttachToActor(TargetSphere, FAttachmentTransformRules::KeepWorldTransform);

// 			DroppedArtifact->SetActorLocation(GetActorLocation());
// 			DroppedArtifact->SetActorHiddenInGame(false);
// 			DroppedArtifact->SetActorEnableCollision(true);
// 			DroppedArtifact->bIsCarried = false;
// 			DroppedArtifact->Carrier = nullptr;
// 		}

// 		this->Destroy();
// 	}
// }

void AMazeRunner::FinishEscape()
{
	AOrchestrator *Orchestrator = Cast<AOrchestrator>(GetOwner());

	if (MyTarget)
	{
		MyTarget->Destroy();
		MyTarget = nullptr;

		UE_LOG(LogTemp, Log, TEXT("[MazeRunner] AI successfully escaped with an artifact!"));

		if (Orchestrator)
			Orchestrator->OnArtifactStolen.Broadcast();
	}

	SetAIColor(FLinearColor::Green);
	CurrentState = EAIState::Hunting;
	bIsMoving = false; // Forces it to ask Orchestrator for a new target
}
