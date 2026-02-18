#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
// #include "MazeTypes.h"
#include "AI/Navigator.h"
#include "Components/ChildActorComponent.h"
#include "Orchestrator.generated.h"

class ACubeToSphere;
class UMaze;
class UHierarchicalInstancedStaticMeshComponent;
class UStaticMesh;

/**
 * ASphereMazeOrchestrator
 * Job: Orchestrate what you have now:
 * - Own Maze data (USphereMaze)
 * - Call Maze->Generate
 * - Call SphereActor->BuildSurface
 * - Build placeholder wall instances (HISM) using Maze + Sphere mapping
 * - Provide spawn transform on an open cell
 */
UCLASS()
class GAME_API AOrchestrator : public AActor
{
	GENERATED_BODY()

public:
	AOrchestrator();
	virtual void OnConstruction(const FTransform &Transform) override;

	// Pipeline: generate + build surface + build walls
	UFUNCTION(BlueprintCallable, Category = "Orchestrator")
	void Rebuild();

	// UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI")
	// UMazeNavigator *Navigator;

	// Spawn helper callable from BP
	UFUNCTION(BlueprintCallable, Category = "Orchestrator|Spawn")
	bool GetRandomSpawnTransform(FTransform &OutTransform,
								 float CapsuleHalfHeight = 88.f,
								 int32 MinOpenSides = 2,
								 int32 MaxTries = 5000) const;
	void ResolveSphereFromChild();

	// ---- References ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Refs")
	ACubeToSphere *SphereActor = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sphere", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float SphereRadius = 600.f;

	// ---- Maze params ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze")
	int32 Seed = 122;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze")
	int32 CellsPerFace = 31;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sphere")
	int32 Resolution = CellsPerFace + 1;

	// Optional: set maze size here; Sphere Resolution should be CellsPerFace+1

	// ---- Wall placeholder params ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Walls")
	UStaticMesh *WallMesh = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Walls")
	float WallMeshBaseLength = 100.f; // Engine cube default size

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Walls")
	float WallHeight = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Walls")
	float WallThickness = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Walls")
	float WallSurfaceOffset = 1.f;

protected:
	// ---- Owned data/components ----
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Refs")
	UMaze *Maze = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Walls")
	UHierarchicalInstancedStaticMeshComponent *WallHISM = nullptr;

protected:
	// Internal helpers (implemented later)
	void EnsureMazeGenerated();
	void BuildWallsFromMaze();

	// Spawn search helpers
	bool IsCellSpawnable(int32 Face, int32 X, int32 Y, int32 MinOpenSides) const;
	bool FindRandomSpawnCell(int32 &OutFace, int32 &OutX, int32 &OutY,
							 int32 MinOpenSides, int32 MaxTries) const;
};
