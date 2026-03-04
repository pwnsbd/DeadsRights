#include "Orchestrator.h"
#include "Components/SceneComponent.h"
#include "AI/MazeNavigator.h"
#include "Conversion/CubeToSphere.h"
#include "Maze/Maze.h"
#include "DrawDebugHelpers.h" // A* star testing
#include "Components/InstancedStaticMeshComponent.h"

AOrchestrator::AOrchestrator()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent *Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	// Change to UInstancedStaticMeshComponent
	WallHISM = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("WallHISM"));
	WallHISM->SetupAttachment(Root);
	WallHISM->SetCollisionProfileName(TEXT("BlockAll"));
	WallHISM->SetMobility(EComponentMobility::Movable);

	// Change to UInstancedStaticMeshComponent
	PathHISM = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("PathHISM"));
	PathHISM->SetupAttachment(Root);
	PathHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PathHISM->SetMobility(EComponentMobility::Movable);
}

void AOrchestrator::OnConstruction(const FTransform &Transform)
{
	Super::OnConstruction(Transform);

	// Useful for editor iteration
	Rebuild();
}

void AOrchestrator::ResolveSphereFromChild()
{
	// 1. Clear the pointer so we don't accidentally hold onto a "dead" editor sphere
	SphereActor = nullptr;

	// 2. Ask Unreal to find all Child Actor Components attached to this Blueprint
	TArray<UChildActorComponent *> ChildComps;
	GetComponents<UChildActorComponent>(ChildComps);

	// 3. Loop through them and extract the actual CubeToSphere actor
	for (UChildActorComponent *CAC : ChildComps)
	{
		// Force the child actor to spawn if it hasn't yet
		if (CAC && CAC->GetChildActor() == nullptr)
		{
			CAC->CreateChildActor();
		}

		if (CAC && CAC->GetChildActor())
		{
			SphereActor = Cast<ACubeToSphere>(CAC->GetChildActor());
			if (SphereActor)
			{
				return; // We successfully found and linked it!
			}
		}
	}
}

void AOrchestrator::Rebuild()
{
	// 1. ALWAYS find the blueprint's child actor first
	ResolveSphereFromChild();

	// If it's still null, safely abort so we don't crash
	if (!SphereActor)
		return;

	// 2. Lock the Sphere's resolution
	Resolution = CellsPerFace + 1;

	// 3. Build the floor
	SphereActor->SetRadius(SphereRadius);
	SphereActor->SetResolution(Resolution);
	SphereActor->BuildSurface();

	// 4. Generate the logical maze grid
	EnsureMazeGenerated();

	// 5. Draw the physical walls
	BuildWallsFromMaze();

	// 6. Initialize the AI brain
	if (!Navigator)
	{
		Navigator = NewObject<UMazeNavigator>(this);
	}
	Navigator->Init(Maze, SphereActor);

	// 7. Draw the path!
	Astar();
}

void AOrchestrator::BeginPlay()
{
	Super::BeginPlay();

	// When you hit Play, force the blueprint to rebuild the Live maze and run A*!
	Rebuild();
}

void AOrchestrator::EnsureMazeGenerated()
{
	if (!Maze)
	{
		Maze = NewObject<UMaze>(this);
	}

	// ALWAYS update these variables and force a regeneration when the slider changes!
	Maze->CellsPerFace = CellsPerFace;
	Maze->Seed = Seed;
	Maze->Generate();
}

static FORCEINLINE int32 CountOpenSides(const FMazeCell &C)
{
	return (C.OpenN ? 1 : 0) + (C.OpenE ? 1 : 0) + (C.OpenS ? 1 : 0) + (C.OpenW ? 1 : 0);
}

bool AOrchestrator::IsCellSpawnable(int32 Face, int32 X, int32 Y, int32 MinOpenSides) const
{
	if (!Maze)
		return false;

	const FMazeCell &C = Maze->GetCell(Face, X, Y);
	return CountOpenSides(C) >= MinOpenSides;
}

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

bool AOrchestrator::GetRandomSpawnTransform(FTransform &OutTransform,
											float CapsuleHalfHeight,
											int32 MinOpenSides,
											int32 MaxTries) const
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

	// 1. Get the perfectly smooth UP direction from the sphere center
	const FVector UpDir = (CenterWorld - SphereCenter).GetSafeNormal();

	// 2. Find the exact alignment of the walls!
	// We check the West wall of this specific cell (which runs North-to-South).
	FVector EdgeA, EdgeB;
	SphereActor->GetCellWallEdgeWorld(Face, X, Y, EMazeDir::W, EdgeA, EdgeB);

	// In your grid, EdgeA is the North-West corner, and EdgeB is the South-West corner.
	// By subtracting B from A, we get a vector pointing perfectly "North" along the hallway.
	const FVector ForwardDir = (EdgeA - EdgeB).GetSafeNormal();

	// 3. Create a rotation that locks BOTH the Up vector and the Forward vector
	const FRotator SpawnRot = FRotationMatrix::MakeFromXZ(ForwardDir, UpDir).Rotator();

	const FVector SpawnLoc = CenterWorld + UpDir * (CapsuleHalfHeight + 2.f);

	OutTransform = FTransform(SpawnRot, SpawnLoc, FVector::OneVector);
	return true;
}

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
		const FVector Edge = (B - A);
		const float EdgeLen = Edge.Size();
		if (EdgeLen <= KINDA_SMALL_NUMBER)
			return;

		const FVector Mid = (A + B) * 0.5f;

		// Local sphere center is (0,0,0) when child is identity
		const FVector Up = Mid.GetSafeNormal();
		const FVector Fwd = Edge / EdgeLen;

		const FQuat Rot = FRotationMatrix::MakeFromXZ(Fwd, Up).ToQuat();
		const FVector Loc = Mid + Up * (WallHeight * 0.5f + WallSurfaceOffset);

		const FVector Scale(
			EdgeLen / WallMeshBaseLength,
			WallThickness / WallMeshBaseLength,
			WallHeight / WallMeshBaseLength);

		// IMPORTANT: Add in component/local space (will follow orchestrator moves)
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

	// Dedupe: per cell build E + S, plus borders W (X==0) and N (Y==0)
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

void AOrchestrator::Astar()
{
	if (!SphereActor)
	{
		UE_LOG(LogTemp, Error, TEXT("A* TEST FAILED: SphereActor is null!"));
		return;
	}

	// 1. Erase the old paths so the viewport stays clean
	FlushPersistentDebugLines(GetWorld());

	// 2. Get the dynamically sized max cell!
	int32 MaxCell = SphereActor->GetCellsPerFace() - 1;

	// 3. Connect the absolute corners of Face 4
	FVector StartPos = SphereActor->GetCellCenterWorld(1, MaxCell / 2, MaxCell / 2);
	FVector EndPos = SphereActor->GetCellCenterWorld(0, MaxCell / 2, MaxCell / 2);

	UE_LOG(LogTemp, Warning, TEXT("StartPos (Blue) is at: %s"), *StartPos.ToString());
	UE_LOG(LogTemp, Warning, TEXT("EndPos (Red) is at: %s"), *EndPos.ToString());

	// 4. Draw them smaller (Radius 15 instead of 30) so they don't eat the green spheres!
	DrawDebugSphere(GetWorld(), StartPos, 30.0f, 12, FColor::Blue, true, 20.0f);
	DrawDebugSphere(GetWorld(), EndPos, 30.0f, 12, FColor::Red, true, 20.0f);

	TArray<FVector> PathResult;

	if (Navigator != nullptr)
	{
		bool bFoundPath = Navigator->FindPath(StartPos, EndPos, PathResult);

		// If you assigned a mesh in the editor, draw the physical path!
		if (bFoundPath)
		{
			UE_LOG(LogTemp, Warning, TEXT("A* TEST SUCCESS: Path found with %d steps!"), PathResult.Num());

			// 1. If you used the custom Variable, apply it to the component
			if (PathHISM && PathMesh)
			{
				PathHISM->SetStaticMesh(PathMesh);

				if (PathMaterial)
				{
					PathHISM->SetMaterial(0, PathMaterial);
				}
			}

			// 2. If the component has ANY mesh assigned to it, draw the physical blocks!
			if (PathHISM && PathHISM->GetStaticMesh())
			{
				PathHISM->ClearInstances();

				for (const FVector &Point : PathResult)
				{
					FVector LocalPos = GetActorTransform().InverseTransformPosition(Point);
					FVector UpDir = LocalPos.GetSafeNormal();

					// Push it 30 units up so it physically hovers over the maze walls
					LocalPos += UpDir * 10.0f;

					// Scale of 0.5 makes them nice and chunky
					FTransform InstanceTransform(FRotator::ZeroRotator, LocalPos, FVector(0.1f));
					PathHISM->AddInstance(InstanceTransform);
				}
			}
			else
			{
				// 3. Fallback to debug paint only if absolutely no mesh exists
				for (const FVector &Point : PathResult)
				{
					DrawDebugSphere(GetWorld(), Point, 15.0f, 12, FColor::Green, true, 20.0f);
				}
			}
		}
	}
}
