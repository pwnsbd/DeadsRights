#include "Orchestrator.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"

// Optional includes you'll likely need later (keep commented until you implement)
// #include "CubeToSphere.h"
// #include "SphereMaze.h"
// #include "Engine/StaticMesh.h"

AOrchestrator::AOrchestrator()
{
	PrimaryActorTick.bCanEverTick = false;

	// Create placeholder wall renderer (HISM)
	WallHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("WallHISM"));
	SetRootComponent(WallHISM);

	WallHISM->SetCollisionProfileName(TEXT("BlockAll"));
	WallHISM->SetMobility(EComponentMobility::Movable);
}

void AOrchestrator::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// Stub: later you'll call Rebuild() here if you want editor-time rebuild.
	// Rebuild();
}

void AOrchestrator::Rebuild()
{
	// Stub:
	// 1) EnsureMazeGenerated()
	// 2) SphereActor->BuildSurface()
	// 3) BuildWallsFromMaze()
}

bool AOrchestrator::GetRandomSpawnTransform(FTransform& OutTransform,
                                           float CapsuleHalfHeight,
                                           int32 MinOpenSides,
                                           int32 MaxTries) const
{
	// Stub: return false until implemented
	OutTransform = FTransform::Identity;
	return false;
}

void AOrchestrator::EnsureMazeGenerated()
{
	// Stub:
	// - create Maze object if needed
	// - set CellsPerFace, Seed
	// - Maze->Generate()
}

void AOrchestrator::BuildWallsFromMaze()
{
	// Stub:
	// - ClearInstances
	// - Loop maze cells
	// - Query sphere edges
	// - Add wall instances
}

bool AOrchestrator::IsCellSpawnable(int32 Face, int32 X, int32 Y, int32 MinOpenSides) const
{
	// Stub: always false until maze data exists here
	return false;
}

bool AOrchestrator::FindRandomSpawnCell(int32& OutFace, int32& OutX, int32& OutY,
                                       int32 MinOpenSides, int32 MaxTries) const
{
	// Stub
	OutFace = 0;
	OutX = 0;
	OutY = 0;
	return false;
}
