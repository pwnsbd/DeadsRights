#include "Orchestrator.h"
#include "Components/SceneComponent.h"
#include "AI/MazeNavigator.h"
#include "Conversion/CubeToSphere.h"
#include "Maze/Maze.h"
#include "DrawDebugHelpers.h"
#include "Components/InstancedStaticMeshComponent.h"

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

	PathHISM = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("PathHISM"));
	PathHISM->SetupAttachment(Root);
	PathHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PathHISM->SetMobility(EComponentMobility::Movable);
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

	Astar();
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
 * args :
 *   - OutFace, OutX, OutY: returned cell indices if found
 *   - MinOpenSides: minimum open sides required
 *   - MaxTries: maximum random attempts
 * result: True if a cell was found; otherwise False.
 */
bool AOrchestrator::FindRandomSpawnCell(int32 &OutFace, int32 &OutX, int32 &OutY,
										int32 MinOpenSides, int32 MaxTries) const
{
	if (!Maze)
		return false;

	const int32 N = Maze->CellsPerFace;
	if (N <= 0)
		return false;

	for (int32 Try = 0; Try < MaxTries; ++Try)
	{
		const int32 Face = FMath::RandRange(0, 5);
		const int32 X = FMath::RandRange(0, N - 1);
		const int32 Y = FMath::RandRange(0, N - 1);

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
 *        aligned to the sphere surface + corridor direction.
 * args :
 *   - OutTransform: returned spawn transform (rotation aligns to surface + hallway).
 *   - CapsuleHalfHeight: character capsule half height used to offset spawn above surface.
 *   - MinOpenSides: minimum number of open sides required for a cell to be spawnable.
 *   - MaxTries: maximum random attempts before failing.
 * result: True if a valid spawn cell was found; otherwise False (OutTransform becomes Identity).
 */
bool AOrchestrator::GetRandomSpawnTransform(FTransform &OutTransform,
											float CapsuleHalfHeight, int32 MinOpenSides, int32 MaxTries) const
{
	if (!SphereActor || !Maze)
		return false;

	int32 Face, X, Y;
	if (!FindRandomSpawnCell(Face, X, Y, MinOpenSides, MaxTries))
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

	auto AddWallFromEdgeLocal = [&](const FVector &A, const FVector &B)
	{
		const FVector Edge = (B - A);
		const float EdgeLen = Edge.Size();
		if (EdgeLen <= KINDA_SMALL_NUMBER)
			return;

		const FVector Mid = (A + B) * 0.5f;
		const FVector Up = Mid.GetSafeNormal();
		const FVector Fwd = Edge / EdgeLen;

		const FQuat Rot = FRotationMatrix::MakeFromXZ(Fwd, Up).ToQuat();
		const FVector Loc = Mid + Up * (WallHeight * 0.5f + WallSurfaceOffset);

		const FVector Scale(
			EdgeLen / WallMeshBaseLength,
			WallThickness / WallMeshBaseLength,
			WallHeight / WallMeshBaseLength);

		WallHISM->AddInstance(FTransform(Rot, Loc, Scale));
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
void AOrchestrator::Astar()
{
	if (!SphereActor || !MazeRunnerClass || !Navigator)
		return;

	// Clear old debug lines
	FlushPersistentDebugLines(GetWorld());

	// Get a random START point
	FTransform StartTransform;
	if (!GetRandomSpawnTransform(StartTransform, 20.0f, 1))
		return;

	// Get a random END point
	FTransform EndTransform;
	if (!GetRandomSpawnTransform(EndTransform, 20.0f, 1))
		return;

	FVector StartPos = StartTransform.GetLocation();
	FVector EndPos = EndTransform.GetLocation();

	DrawDebugSphere(GetWorld(), StartPos, 30.0f, 12, FColor::Blue, true, 20.0f);
	DrawDebugSphere(GetWorld(), EndPos, 30.0f, 12, FColor::Red, true, 20.0f);

	TArray<FVector> PathResult;

	if (Navigator != nullptr)
	{
		bool bFoundPath = Navigator->FindPath(StartPos, EndPos, PathResult);

		if (bFoundPath)
		{
			if (PathHISM && PathMesh)
			{
				PathHISM->SetStaticMesh(PathMesh);

				if (PathMaterial)
				{
					PathHISM->SetMaterial(0, PathMaterial);
				}
			}

			if (PathHISM && PathHISM->GetStaticMesh())
			{
				PathHISM->ClearInstances();

				for (const FVector &Point : PathResult)
				{
					FVector LocalPos = GetActorTransform().InverseTransformPosition(Point);
					FVector UpDir = LocalPos.GetSafeNormal();

					LocalPos += UpDir * 10.0f;

					FTransform InstanceTransform(FRotator::ZeroRotator, LocalPos, FVector(0.1f));
					PathHISM->AddInstance(InstanceTransform);
				}
			}
			else
			{
				for (const FVector &Point : PathResult)
				{
					DrawDebugSphere(GetWorld(), Point, 15.0f, 12, FColor::Green, true, 20.0f);
				}
			}
		}
	}
}