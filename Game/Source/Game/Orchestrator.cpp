#include "Orchestrator.h"
#include "Components/SceneComponent.h"
#include "AI/MazeNavigator.h"
#include "AI/MazeRunner.h"
#include "Conversion/CubeToSphere.h"
#include "Maze/Maze.h"
#include "DrawDebugHelpers.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMeshActor.h" // Add this include at the top

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
 *        - resolves SphereActor from child actor component
 *        - locks Resolution = CellsPerFace + 1
 *        - builds sphere surface
 *        - generates maze data
 *        - builds wall instances
 *        - initializes Navigator
 *        - runs A* debug/path visualization
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
 *   - Face, X, Y: cell indices
 *   - MinOpenSides: minimum number of open sides required
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
 */
bool AOrchestrator::FindRandomSpawnCell(int32 &OutFace, int32 &OutX, int32 &OutY, int32 MinOpenSides, int32 MaxTries, int32 PointSeed) const
{
	if (!Maze)
		return false;

	const int32 N = Maze->CellsPerFace;
	if (N <= 0)
		return false; // <-- The compiler was mad about a stray semicolon near here!

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
 * desc : Randomly searches for a spawnable cell within MaxTries attempts.
 * args :
 *   - OutFace, OutX, OutY: returned cell indices if found
 *   - MinOpenSides: minimum open sides required
 *   - MaxTries: maximum random attempts
 * result: True if a cell was found; otherwise False.
 */
// bool AOrchestrator::FindRandomSpawnCell(int32 &OutFace, int32 &OutX, int32 &OutY,
// 										int32 MinOpenSides, int32 MaxTries) const
// {
// 	if (!Maze)
// 		return false;

// 	const int32 N = Maze->CellsPerFace;
// 	if (N <= 0)
// 		return false;

// 	for (int32 Try = 0; Try < MaxTries; ++Try)
// 	{
// 		const int32 Face = FMath::RandRange(0, 5);
// 		const int32 X = FMath::RandRange(0, N - 1);
// 		const int32 Y = FMath::RandRange(0, N - 1);

// 		if (IsCellSpawnable(Face, X, Y, MinOpenSides))
// 		{
// 			OutFace = Face;
// 			OutX = X;
// 			OutY = Y;
// 			return true;
// 		}
// 	}

// 	return false;
// }

/**
 * desc : Finds a random maze cell that is "open enough" and returns a spawn transform
 *        aligned to the sphere surface + corridor direction.
 * args :
 *   - OutTransform: returned spawn transform (rotation aligns to surface + hallway).
 *   - CapsuleHalfHeight: character capsule half height used to offset spawn above surface.
 *   - MinOpenSides: minimum number of open sides required for a cell to be spawnable.
 *   - MaxTries: maximum random attempts before failing.
 * result: True if a valid spawn cell was found; otherwise False (OutTransform becomes Identity).
 */
// bool AOrchestrator::GetRandomSpawnTransform(FTransform &OutTransform,
// 											float CapsuleHalfHeight, int32 MinOpenSides, int32 MaxTries) const
// {
// 	if (!SphereActor || !Maze)
// 		return false;

// 	int32 Face, X, Y;
// 	if (!FindRandomSpawnCell(Face, X, Y, MinOpenSides, MaxTries))
// 	{
// 		OutTransform = FTransform::Identity;
// 		return false;
// 	}

// 	const FVector CenterWorld = SphereActor->GetCellCenterWorld(Face, X, Y);
// 	const FVector SphereCenter = SphereActor->GetActorLocation();

// 	const FVector UpDir = (CenterWorld - SphereCenter).GetSafeNormal();

// 	FVector EdgeA, EdgeB;
// 	SphereActor->GetCellWallEdgeWorld(Face, X, Y, EMazeDir::W, EdgeA, EdgeB);

// 	const FVector ForwardDir = (EdgeA - EdgeB).GetSafeNormal();
// 	const FRotator SpawnRot = FRotationMatrix::MakeFromXZ(ForwardDir, UpDir).Rotator();

// 	const FVector SpawnLoc = CenterWorld + UpDir * (CapsuleHalfHeight + 2.f);

// 	OutTransform = FTransform(SpawnRot, SpawnLoc, FVector::OneVector);
// 	return true;
// }

/**
 * desc : Converts Maze walls into instanced mesh wall segments on the sphere surface.
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
 * desc : Debug/test pathfinding routine. Picks start/end points, runs Navigator->FindPath(),
 *        then draws results via instanced meshes or debug spheres.
 * args : None
 * result: None
 */

void AOrchestrator::BeginPlay()
{
	Super::BeginPlay();

	if (!SphereActor || !MazeRunnerClass || !MarkerMesh)
		return;

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	FTransform StartTransform;
	// Pick ONE random spot to spawn the Runner at the very beginning of the game
	if (GetRandomSpawnTransform(StartTransform, 15.0f, 1, 5000, 1))
	{
		StartMarkerRef = GetWorld()->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), StartTransform, SpawnParams);
		if (StartMarkerRef)
		{
			StartMarkerRef->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
			StartMarkerRef->AttachToActor(SphereActor, FAttachmentTransformRules::KeepWorldTransform);
			StartMarkerRef->GetStaticMeshComponent()->SetStaticMesh(MarkerMesh);
			if (StartMaterial)
				StartMarkerRef->GetStaticMeshComponent()->SetMaterial(0, StartMaterial);
			StartMarkerRef->SetActorScale3D(FVector(0.25f));
		}

		ActiveRunner = GetWorld()->SpawnActor<AMazeRunner>(MazeRunnerClass, StartTransform, SpawnParams);
		if (ActiveRunner)
		{
			ActiveRunner->AttachToActor(SphereActor, FAttachmentTransformRules::KeepWorldTransform);

			// Hook up the ears! When it finishes a path, it runs our Brain function.
			ActiveRunner->OnPathCompleted.AddDynamic(this, &AOrchestrator::OnRunnerReachedArtifact);
		}
	}
}

void AOrchestrator::TriggerNextRun() // Bound to your Shift + 1
{
	RuntimeSeedOffset += 100;
	if (!SphereActor || !MarkerMesh || !ActiveRunner)
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

void AOrchestrator::OnRunnerReachedArtifact()
{
	// 1. Destroy collected artifact
	if (CurrentTargetArtifact)
	{
		ActiveArtifacts.Remove(CurrentTargetArtifact);
		CurrentTargetArtifact->Destroy();
		CurrentTargetArtifact = nullptr;
	}

	// 2. Are we out of artifacts? Do nothing! Stand idle.
	if (ActiveArtifacts.IsEmpty())
	{
		return;
	}

	// 3. PERFORMANCE FIX: Find closest artifact using straight-line distance FIRST!
	AStaticMeshActor *BestArtifact = nullptr;
	float ClosestDistance = MAX_FLT; // Start with infinitely far away
	FVector StartLoc = ActiveRunner->GetActorLocation();

	for (AStaticMeshActor *Artifact : ActiveArtifacts)
	{
		// DistSquared is heavily optimized for CPUs because it skips calculating square roots!
		float Dist = FVector::DistSquared(StartLoc, Artifact->GetActorLocation());
		if (Dist < ClosestDistance)
		{
			ClosestDistance = Dist;
			BestArtifact = Artifact;
		}
	}

	// 4. ONLY RUN A* EXACTLY ONCE FOR THE WINNING ARTIFACT
	if (BestArtifact)
	{
		TArray<FVector> BestPath;

		// We only calculate the maze path for the closest item
		if (Navigator->FindPath(StartLoc, BestArtifact->GetActorLocation(), BestPath))
		{
			CurrentTargetArtifact = BestArtifact;

			TArray<FVector> LocalPath;
			FTransform SphereTransform = SphereActor->GetTransform();
			for (const FVector &WorldPoint : BestPath)
			{
				LocalPath.Add(SphereTransform.InverseTransformPosition(WorldPoint));
			}

			ActiveRunner->SetPath(LocalPath, SphereActor);
		}
	}
}
