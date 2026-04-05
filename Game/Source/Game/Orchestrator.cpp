#include "Orchestrator.h"
#include "Components/SceneComponent.h"
#include "AI/MazeNavigator.h"
#include "AI/MazeRunner.h"
#include "Conversion/CubeToSphere.h"
#include "Maze/Maze.h"
#include "DrawDebugHelpers.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"

/**
 * desc : Default constructor. Creates root + wall/path instanced mesh components and sets basic collision rules.
 * args : None
 * result: None
 */
AOrchestrator::AOrchestrator()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent *Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	WallHISM = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("WallHISM"));
	WallHISM->SetupAttachment(Root);
	WallHISM->SetCollisionProfileName(TEXT("BlockAll"));
	WallHISM->SetMobility(EComponentMobility::Movable);

	WallHISM->SetCanEverAffectNavigation(false);

	PathHISM = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("PathHISM"));
	PathHISM->SetupAttachment(Root);
	PathHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PathHISM->SetMobility(EComponentMobility::Movable);

	PathHISM->SetCanEverAffectNavigation(false);
}

/**
 * desc : Editor/runtime construction hook. Rebuilds the maze when placed/edited in the editor.
 * args : Transform - current actor transform during construction.
 * result: None
 */
void AOrchestrator::OnConstruction(const FTransform &Transform)
{
	Super::OnConstruction(Transform);
	Rebuild();
}

/**
 * desc : Searches attached ChildActorComponents and assigns SphereActor if a CubeToSphere child is found.
 * args : None
 * result: None
 */
void AOrchestrator::ResolveSphereFromChild()
{
	SphereActor = nullptr;

	TArray<UChildActorComponent *> ChildComps;
	GetComponents<UChildActorComponent>(ChildComps);

	for (UChildActorComponent *CAC : ChildComps)
	{
		if (CAC && CAC->GetChildActor() == nullptr)
		{
			CAC->CreateChildActor();
		}

		if (CAC && CAC->GetChildActor())
		{
			SphereActor = Cast<ACubeToSphere>(CAC->GetChildActor());
			if (SphereActor)
			{
				return;
			}
		}
	}
}

/**
 * desc : Full pipeline rebuild:
 * - resolves SphereActor from child actor component
 * - locks Resolution = CellsPerFace + 1
 * - builds sphere surface
 * - generates maze data
 * - builds wall instances
 * - initializes Navigator
 * args : None
 * result: None
 */
void AOrchestrator::Rebuild()
{
	ResolveSphereFromChild();

	if (!SphereActor)
		return;

	Resolution = CellsPerFace + 1;

	SphereActor->SetRadius(SphereRadius);
	SphereActor->SetResolution(Resolution);
	SphereActor->BuildSurface();

	EnsureMazeGenerated();
	BuildWallsFromMaze();

	if (!Navigator)
	{
		Navigator = NewObject<UMazeNavigator>(this);
	}
	Navigator->Init(Maze, SphereActor);
}

/**
 * desc : Ensures Maze object exists and regenerates maze data using current CellsPerFace + Seed.
 * args : None
 * result: None
 */
void AOrchestrator::EnsureMazeGenerated()
{
	if (!Maze)
	{
		Maze = NewObject<UMaze>(this);
	}

	Maze->CellsPerFace = CellsPerFace;
	Maze->Seed = Seed;
	Maze->Generate();
}

static FORCEINLINE int32 CountOpenSides(const FMazeCell &C)
{
	return (C.OpenN ? 1 : 0) + (C.OpenE ? 1 : 0) + (C.OpenS ? 1 : 0) + (C.OpenW ? 1 : 0);
}

/**
 * desc : Checks if a given cell meets spawn requirements (at least MinOpenSides open directions).
 * args :
 * - Face, X, Y: cell indices
 * - MinOpenSides: minimum number of open sides required
 * result: True if spawnable; otherwise False.
 */
bool AOrchestrator::IsCellSpawnable(int32 Face, int32 X, int32 Y, int32 MinOpenSides) const
{
	if (!Maze)
		return false;

	const FMazeCell &C = Maze->GetCell(Face, X, Y);
	return CountOpenSides(C) >= MinOpenSides;
}

/**
 * desc : Randomly searches for a spawnable cell within MaxTries attempts.
 * args :
 * - OutFace, OutX, OutY: returned cell indices if found
 * - MinOpenSides: minimum open sides required
 * - MaxTries: maximum random attempts
 * - PointSeed: Additional offset to ensure varied locations.
 * result: True if a cell was found; otherwise False.
 */
bool AOrchestrator::FindRandomSpawnCell(int32 &OutFace, int32 &OutX, int32 &OutY, int32 MinOpenSides, int32 MaxTries, int32 PointSeed) const
{
	if (!Maze)
		return false;

	const int32 N = Maze->CellsPerFace;
	if (N <= 0)
		return false;

	// LOCK THE RANDOMNESS TO YOUR MAZE SEED
	FRandomStream Stream(Seed + PointSeed);

	for (int32 Try = 0; Try < MaxTries; ++Try)
	{
		const int32 Face = Stream.RandRange(0, 5);
		const int32 X = Stream.RandRange(0, N - 1);
		const int32 Y = Stream.RandRange(0, N - 1);

		if (IsCellSpawnable(Face, X, Y, MinOpenSides))
		{
			OutFace = Face;
			OutX = X;
			OutY = Y;
			return true;
		}
	}

	return false;
}

/**
 * desc : Finds a random maze cell that is "open enough" and returns a spawn transform
 * aligned to the sphere surface + corridor direction.
 * args :
 * - OutTransform: returned spawn transform (rotation aligns to surface + hallway).
 * - CapsuleHalfHeight: character capsule half height used to offset spawn above surface.
 * - MinOpenSides: minimum number of open sides required for a cell to be spawnable.
 * - MaxTries: maximum random attempts before failing.
 * - PointSeed: Offset seed to ensure multiple calls generate different locations.
 * result: True if a valid spawn cell was found; otherwise False (OutTransform becomes Identity).
 */
bool AOrchestrator::GetRandomSpawnTransform(FTransform &OutTransform, float CapsuleHalfHeight, int32 MinOpenSides, int32 MaxTries, int32 PointSeed) const
{
	if (!SphereActor || !Maze)
		return false;

	int32 Face, X, Y;
	// Pass the PointSeed down into the math
	if (!FindRandomSpawnCell(Face, X, Y, MinOpenSides, MaxTries, PointSeed))
	{
		OutTransform = FTransform::Identity;
		return false;
	}

	const FVector CenterWorld = SphereActor->GetCellCenterWorld(Face, X, Y);
	const FVector SphereCenter = SphereActor->GetActorLocation();

	const FVector UpDir = (CenterWorld - SphereCenter).GetSafeNormal();

	FVector EdgeA, EdgeB;
	SphereActor->GetCellWallEdgeWorld(Face, X, Y, EMazeDir::W, EdgeA, EdgeB);

	const FVector ForwardDir = (EdgeA - EdgeB).GetSafeNormal();
	const FRotator SpawnRot = FRotationMatrix::MakeFromXZ(ForwardDir, UpDir).Rotator();

	const FVector SpawnLoc = CenterWorld + UpDir * (CapsuleHalfHeight + 2.f);

	OutTransform = FTransform(SpawnRot, SpawnLoc, FVector::OneVector);
	return true;
}

/**
 * desc : Converts Maze logical walls into physical instanced mesh wall segments on the sphere surface.
 * args : None
 * result: None
 */
void AOrchestrator::BuildWallsFromMaze()
{
	if (!WallHISM || !SphereActor || !Maze)
		return;
	if (!WallMesh)
		return;

	WallHISM->SetStaticMesh(WallMesh);
	if (!WallHISM->GetStaticMesh())
		return;

	WallHISM->ClearInstances();

	const int32 N = Maze->CellsPerFace;
	if (N <= 0)
		return;

	// Helps with maze and sphere being on the same center when we move sphere in view
	auto AddWallFromEdgeLocal = [&](const FVector &A, const FVector &B)
	{
		if (A.ContainsNaN() || B.ContainsNaN())
			return;

		const FVector Edge = (B - A);
		const float EdgeLen = Edge.Size();

		if (EdgeLen <= 0.1f || !FMath::IsFinite(EdgeLen))
			return;
		if (WallMeshBaseLength <= 0.1f)
			return;

		const FVector Mid = (A + B) * 0.5f;
		const FVector Up = Mid.GetSafeNormal();

		if (Up.IsNearlyZero() || Up.ContainsNaN())
			return;

		const FVector Fwd = Edge / EdgeLen;
		if (Fwd.ContainsNaN())
			return;

		// THE MISSING LINK: Prevent collinear matrix collapse!
		// If Forward and Up are parallel, the rotation matrix explodes.
		if (FMath::Abs(FVector::DotProduct(Fwd, Up)) > 0.99f)
			return;

		FQuat Rot = FRotationMatrix::MakeFromXZ(Fwd, Up).ToQuat();

		// Force check the quaternion
		if (Rot.ContainsNaN() || !Rot.IsNormalized())
			return;

		const FVector Loc = Mid + Up * (WallHeight * 0.5f + WallSurfaceOffset);

		// Hard-clamp scales so they can never reach Infinity or 0
		const FVector Scale(
			FMath::Clamp(EdgeLen / WallMeshBaseLength, 0.01f, 1000.0f),
			FMath::Clamp(WallThickness / WallMeshBaseLength, 0.01f, 1000.0f),
			FMath::Clamp(WallHeight / WallMeshBaseLength, 0.01f, 1000.0f));

		if (Loc.ContainsNaN() || Scale.ContainsNaN())
			return;

		// Explicitly build the transform
		FTransform InstanceTransform;
		InstanceTransform.SetComponents(Rot, Loc, Scale);

		if (InstanceTransform.IsValid() && WallHISM)
		{
			WallHISM->AddInstance(InstanceTransform);
		}
	};

	auto IsOpen = [&](const FMazeCell &C, EMazeDir Dir) -> bool
	{
		switch (Dir)
		{
		case EMazeDir::N:
			return C.OpenN;
		case EMazeDir::E:
			return C.OpenE;
		case EMazeDir::S:
			return C.OpenS;
		case EMazeDir::W:
			return C.OpenW;
		}
		return false;
	};

	for (int32 Face = 0; Face < 6; ++Face)
	{
		for (int32 Y = 0; Y < N; ++Y)
		{
			for (int32 X = 0; X < N; ++X)
			{
				const FMazeCell &Cell = Maze->GetCell(Face, X, Y);
				FVector A, B;

				if (!IsOpen(Cell, EMazeDir::E) && SphereActor->GetCellWallEdgeLocal(Face, X, Y, EMazeDir::E, A, B))
					AddWallFromEdgeLocal(A, B);

				if (!IsOpen(Cell, EMazeDir::S) && SphereActor->GetCellWallEdgeLocal(Face, X, Y, EMazeDir::S, A, B))
					AddWallFromEdgeLocal(A, B);

				if (X == 0 && !IsOpen(Cell, EMazeDir::W) && SphereActor->GetCellWallEdgeLocal(Face, X, Y, EMazeDir::W, A, B))
					AddWallFromEdgeLocal(A, B);

				if (Y == 0 && !IsOpen(Cell, EMazeDir::N) && SphereActor->GetCellWallEdgeLocal(Face, X, Y, EMazeDir::N, A, B))
					AddWallFromEdgeLocal(A, B);
			}
		}
	}
}

/**
 * desc : Called when the game starts. Spawns the initial Start Marker and idle AI Runner.
 * args : None
 * result: None
 */
void AOrchestrator::BeginPlay()
{
	Super::BeginPlay();

	if (!SphereActor || !MazeRunnerClass || !MarkerMesh)
		return;

	// 1. ADD THIS LINE (It was missing/commented out)
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	SpawnParams.Owner = this;

	for (int32 i = 0; i < 2; i++)
	{
		FTransform SpawnTransform;
		if (GetRandomSpawnTransform(SpawnTransform, 15.0f, 1, 5000, 10 + i))
		{
			AMazeRunner *NewRunner = GetWorld()->SpawnActor<AMazeRunner>(MazeRunnerClass, SpawnTransform, SpawnParams);
			if (NewRunner)
			{
				NewRunner->AttachToActor(SphereActor, FAttachmentTransformRules::KeepWorldTransform);
				ActiveRunners.Add(NewRunner); // Adds to our 2-AI list

				AssignTargetToRunner(NewRunner);
			}
		}
	}
}

/**
 * desc : Triggers the spawn of new artifacts on the maze and wakes up the AI to hunt them.
 * args : None
 * result: None
 */
void AOrchestrator::TriggerNextRun()
{
	RuntimeSeedOffset += 100;

	// FIX: Check if the array has any runners instead of looking for a single 'ActiveRunner'
	if (!SphereActor || !MarkerMesh || ActiveRunners.Num() == 0)
		return;

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	for (int32 i = 0; i < NumArtifactsToSpawn; ++i)
	{
		FTransform ArtTransform;
		if (GetRandomSpawnTransform(ArtTransform, 15.0f, 1, 5000, 2 + RuntimeSeedOffset + i))
		{
			AStaticMeshActor *Artifact = GetWorld()->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), ArtTransform, SpawnParams);
			if (Artifact)
			{
				Artifact->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
				Artifact->AttachToActor(SphereActor, FAttachmentTransformRules::KeepWorldTransform);
				Artifact->GetStaticMeshComponent()->SetStaticMesh(MarkerMesh);
				if (EndMaterial)
					Artifact->GetStaticMeshComponent()->SetMaterial(0, EndMaterial);
				Artifact->SetActorScale3D(FVector(0.25f));

				// --- NEW: Enable Player Overlaps ---
				Artifact->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
				Artifact->GetStaticMeshComponent()->SetGenerateOverlapEvents(true);
				Artifact->OnActorBeginOverlap.AddDynamic(this, &AOrchestrator::OnArtifactOverlapped);

				ActiveArtifacts.Add(Artifact);
			}
		}
	}

	// If the Runner is idle (has no target), wake it up to hunt the new item!
	if (!CurrentTargetArtifact)
	{
		OnRunnerReachedArtifact();
	}
}

/**
 * desc : Brain function bound to the AI Runner's delegate. Calculates shortest distance to
 * remaining artifacts, runs A* once, and dispatches the runner.
 * args : None
 * result: None
 */

/**
 * desc : Finds the node on the sphere geographically furthest from the specified actor.
 */
FVector AOrchestrator::GetFarthestNodeFromActor(AActor *TargetActor)
{
	if (!TargetActor || !SphereActor || !Maze)
		return GetActorLocation();

	FVector TargetLoc = TargetActor->GetActorLocation();
	float MaxDistSq = 0.0f;
	FVector BestPoint = TargetLoc;

	for (int32 Face = 0; Face < 6; Face++)
	{
		for (int32 X = 0; X < Maze->CellsPerFace; X += 4)
		{ // Scan every 4th for speed
			for (int32 Y = 0; Y < Maze->CellsPerFace; Y += 4)
			{
				FVector NodeLoc = SphereActor->GetCellCenterWorld(Face, X, Y);
				float DistSq = FVector::DistSquared(NodeLoc, TargetLoc);
				if (DistSq > MaxDistSq)
				{
					MaxDistSq = DistSq;
					BestPoint = NodeLoc;
				}
			}
		}
	}
	return BestPoint;
}

void AOrchestrator::OnArtifactOverlapped(AActor *OverlappedActor, AActor *OtherActor)
{
	// If the player touches the artifact
	if (OtherActor && OtherActor->IsA(APawn::StaticClass()))
	{
		AStaticMeshActor *Artifact = Cast<AStaticMeshActor>(OverlappedActor);
		if (Artifact && ActiveArtifacts.Contains(Artifact))
		{
			ActiveArtifacts.Remove(Artifact);
			Artifact->Destroy();

			// CRITICAL FIX: Aggressively remove dead AI pointers before looping!
			ActiveRunners.RemoveAll([](AMazeRunner *Val)
									{ return !IsValid(Val); });

			// Tell any surviving AI that was hunting this specific artifact to pick a new one
			for (AMazeRunner *Runner : ActiveRunners)
			{
				// Added IsValid safety check just in case
				if (IsValid(Runner) && Runner->MyTarget == Artifact)
				{
					Runner->MyTarget = nullptr;
					AssignTargetToRunner(Runner);
				}
			}
		}
	}
}

void AOrchestrator::OnRunnerReachedArtifact()
{
	// 1. Artifact Cleanup
	if (CurrentTargetArtifact)
	{
		ActiveArtifacts.Remove(CurrentTargetArtifact);
		CurrentTargetArtifact->Destroy();
		CurrentTargetArtifact = nullptr;
	}

	if (ActiveArtifacts.IsEmpty())
		return;

	// CRITICAL FIX: Clean out dead runners here as well
	ActiveRunners.RemoveAll([](AMazeRunner *Val)
							{ return !IsValid(Val); });

	// 2. DISTRIBUTED TARGETING
	for (AMazeRunner *Runner : ActiveRunners)
	{
		if (IsValid(Runner) && (Runner->CurrentState == EAIState::Hunting || Runner->CurrentState == EAIState::Idle))
		{
			AssignTargetToRunner(Runner);
		}
	}
}

void AOrchestrator::AssignTargetToRunner(AMazeRunner *Runner)
{
	if (!Runner || !Navigator || ActiveArtifacts.Num() == 0)
		return;

	// Clean out dead pointers safely
	ActiveArtifacts.RemoveAll([](AStaticMeshActor *Val)
							  { return !IsValid(Val); });
	ActiveRunners.RemoveAll([](AMazeRunner *Val)
							{ return !IsValid(Val); });

	if (ActiveArtifacts.Num() == 0)
		return;

	APawn *PlayerPawn = GetWorld()->GetFirstPlayerController()->GetPawn();
	FVector PlayerLoc = PlayerPawn ? PlayerPawn->GetActorLocation() : FVector::ZeroVector;
	FVector RunnerLoc = Runner->GetActorLocation();

	AStaticMeshActor *BestArtifact = nullptr;
	float BestScore = -1.0f;
	bool bEnforceReservations = (ActiveArtifacts.Num() >= ActiveRunners.Num());

	for (AStaticMeshActor *Artifact : ActiveArtifacts)
	{
		if (!IsValid(Artifact))
			continue;

		bool bIsReserved = false;
		bool bIsBeingCarried = false;

		for (AMazeRunner *Other : ActiveRunners)
		{
			if (IsValid(Other) && Other != Runner && Other->MyTarget == Artifact)
			{
				if (Other->CurrentState == EAIState::Escaping)
				{
					bIsBeingCarried = true;
					break;
				}
				if (bEnforceReservations)
				{
					bIsReserved = true;
				}
			}
		}

		if (bIsBeingCarried || bIsReserved)
			continue;

		// Simple straight-line distance check
		float DistToArt = FVector::Dist(RunnerLoc, Artifact->GetActorLocation());

		// If player is sitting on the artifact, lower its score
		float ThreatDist = FVector::Dist(Artifact->GetActorLocation(), PlayerLoc);
		float ThreatPenalty = (ThreatDist < 600.0f) ? (600.0f - ThreatDist) * 10.0f : 0.0f;

		float Score = 10000.0f / (DistToArt + ThreatPenalty + 1.0f);

		if (Score > BestScore)
		{
			BestScore = Score;
			BestArtifact = Artifact;
		}
	}

	if (BestArtifact)
	{
		Runner->MyTarget = BestArtifact;
		TArray<FVector> Path;
		// Send ThreatRadius = 0 so A* never fails to find a path
		if (Navigator->FindPath(RunnerLoc, BestArtifact->GetActorLocation(), Path, PlayerLoc, 0.0f) && Path.Num() > 0)
		{
			TArray<FVector> LocalPath;
			FTransform SphereXform = SphereActor->GetTransform();
			for (FVector P : Path)
				LocalPath.Add(SphereXform.InverseTransformPosition(P));
			Runner->SetPath(LocalPath, SphereActor);
		}
	}
}
