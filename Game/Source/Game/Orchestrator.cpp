#include "Orchestrator.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"

#include "Conversion/CubeToSphere.h"
#include "Maze/Maze.h"

AOrchestrator::AOrchestrator()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	WallHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("WallHISM"));
	WallHISM->SetupAttachment(Root);
	WallHISM->SetCollisionProfileName(TEXT("BlockAll"));
	WallHISM->SetMobility(EComponentMobility::Movable);
}

void AOrchestrator::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// Useful for editor iteration
	Rebuild();
}

void AOrchestrator::ResolveSphereFromChild()
{
	if (SphereActor) return;

	TArray<UChildActorComponent*> ChildComps;
	GetComponents<UChildActorComponent>(ChildComps);

	// Prefer the one named exactly "sphereCild"
	for (UChildActorComponent* CAC : ChildComps)
	{
		if (!CAC) continue;

		if (CAC->GetFName() == FName(TEXT("sphereChild")))
		{
			SphereActor = Cast<ACubeToSphere>(CAC->GetChildActor());
			if (SphereActor) return;
		}
	}

	// Fallback: if there's only one child actor and it's a CubeToSphere, use it
	for (UChildActorComponent* CAC : ChildComps)
	{
		if (!CAC) continue;

		if (ACubeToSphere* AsSphere = Cast<ACubeToSphere>(CAC->GetChildActor()))
		{
			SphereActor = AsSphere;
			return;
		}
	}
}


void AOrchestrator::Rebuild()
{
	ResolveSphereFromChild();
	EnsureMazeGenerated();

	if (SphereActor)
	{
		SphereActor->SetRadius(SphereRadius);
		SphereActor->BuildSurface();
	}

	BuildWallsFromMaze();
}

void AOrchestrator::EnsureMazeGenerated()
{
	if (!Maze)
	{
		Maze = NewObject<UMaze>(this);
	}

	// Prefer sphere’s current resolution if it exists
	const int32 N = SphereActor ? SphereActor->GetCellsPerFace() : FMath::Max(1, CellsPerFace);

	Maze->CellsPerFace = N;
	Maze->Seed = Seed;
	Maze->Generate();
}

static FORCEINLINE int32 CountOpenSides(const FMazeCell& C)
{
	return (C.OpenN ? 1 : 0) + (C.OpenE ? 1 : 0) + (C.OpenS ? 1 : 0) + (C.OpenW ? 1 : 0);
}

bool AOrchestrator::IsCellSpawnable(int32 Face, int32 X, int32 Y, int32 MinOpenSides) const
{
	if (!Maze) return false;

	const FMazeCell& C = Maze->GetCell(Face, X, Y);
	return CountOpenSides(C) >= MinOpenSides;
}

bool AOrchestrator::FindRandomSpawnCell(int32& OutFace, int32& OutX, int32& OutY,
                                        int32 MinOpenSides, int32 MaxTries) const
{
	if (!Maze) return false;

	const int32 N = Maze->CellsPerFace;
	if (N <= 0) return false;

	for (int32 Try = 0; Try < MaxTries; ++Try)
	{
		const int32 Face = FMath::RandRange(0, 5);
		const int32 X    = FMath::RandRange(0, N - 1);
		const int32 Y    = FMath::RandRange(0, N - 1);

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

bool AOrchestrator::GetRandomSpawnTransform(FTransform& OutTransform,
                                           float CapsuleHalfHeight,
                                           int32 MinOpenSides,
                                           int32 MaxTries) const
{
	if (!SphereActor || !Maze) return false;

	int32 Face, X, Y;
	if (!FindRandomSpawnCell(Face, X, Y, MinOpenSides, MaxTries))
	{
		OutTransform = FTransform::Identity;
		return false;
	}

	const FVector CenterWorld = SphereActor->GetCellCenterWorld(Face, X, Y);
	const FVector SphereCenter = SphereActor->GetActorLocation();

	const FVector UpDir = (CenterWorld - SphereCenter).GetSafeNormal();
	const FVector SpawnLoc = CenterWorld + UpDir * (CapsuleHalfHeight + 2.f);
	const FRotator SpawnRot = FRotationMatrix::MakeFromZ(UpDir).Rotator();

	OutTransform = FTransform(SpawnRot, SpawnLoc, FVector::OneVector);
	return true;
}

void AOrchestrator::BuildWallsFromMaze()
{
	if (!WallHISM || !SphereActor || !Maze) return;
	if (!WallMesh) return;

	WallHISM->SetStaticMesh(WallMesh);
	if (!WallHISM->GetStaticMesh()) return;

	WallHISM->ClearInstances();

	const int32 N = Maze->CellsPerFace;
	if (N <= 0) return;

	// Helps with maze and sphere being on the same center when we move sphere in view
	auto AddWallFromEdgeLocal = [&](const FVector& A, const FVector& B)
	{
		const FVector Edge = (B - A);
		const float EdgeLen = Edge.Size();
		if (EdgeLen <= KINDA_SMALL_NUMBER) return;

		const FVector Mid = (A + B) * 0.5f;

		// Local sphere center is (0,0,0) when child is identity
		const FVector Up  = Mid.GetSafeNormal();
		const FVector Fwd = Edge / EdgeLen;

		const FQuat Rot = FRotationMatrix::MakeFromXZ(Fwd, Up).ToQuat();
		const FVector Loc = Mid + Up * (WallHeight * 0.5f + WallSurfaceOffset);

		const FVector Scale(
			EdgeLen / WallMeshBaseLength,
			WallThickness / WallMeshBaseLength,
			WallHeight / WallMeshBaseLength
		);

		// IMPORTANT: Add in component/local space (will follow orchestrator moves)
		WallHISM->AddInstance(FTransform(Rot, Loc, Scale));
	};


	auto IsOpen = [&](const FMazeCell& C, EMazeDir Dir) -> bool
	{
		switch (Dir)
		{
			case EMazeDir::N: return C.OpenN;
			case EMazeDir::E: return C.OpenE;
			case EMazeDir::S: return C.OpenS;
			case EMazeDir::W: return C.OpenW;
		}
		return false;
	};

	// Dedupe: per cell build E + S, plus borders W (X==0) and N (Y==0)
	for (int32 Face = 0; Face < 6; ++Face)
	{
		for (int32 Y = 0; Y < N; ++Y)
		{
			for (int32 X = 0; X < N; ++X)
			{
				const FMazeCell& Cell = Maze->GetCell(Face, X, Y);

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
