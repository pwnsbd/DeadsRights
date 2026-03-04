#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
// #include "MazeTypes.h"
#include "AI/MazeNavigator.h"
#include "Components/ChildActorComponent.h"
#include "Orchestrator.generated.h"

class ACubeToSphere;
class UMaze;
// class UHierarchicalInstancedStaticMeshComponent;
class UInstancedStaticMeshComponent;
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

	// implements navigator interface for AI pathfinding
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI")
	UMazeNavigator *Navigator;

	// ---- References ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Refs")
	ACubeToSphere *SphereActor = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sphere", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float SphereRadius = 600.f;

	// ---- Maze params ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Maze", meta=(ClampMin="2"))
	int32 CellsPerFace = 31;

	// If true, every editor rebuild picks a new random seed automatically
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Maze")
	bool bRandomizeSeed = true;

	// Used when bRandomizeSeed is false
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Maze", meta=(EditCondition="!bRandomizeSeed"))
	int32 Seed = 122;

	// Derived from CellsPerFace, do not edit directly
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Sphere")
	int32 Resolution = 0;

	// Button in Details panel
	UFUNCTION(CallInEditor, Category="Maze")
	void RandomizeSeedNow();


	
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


	UFUNCTION(BlueprintCallable, Category="Orchestrator|Refs")
	UMaze* GetMaze() const { return Maze; }

	UFUNCTION(BlueprintCallable, Category="Orchestrator|Refs")
	ACubeToSphere* GetSphereActor() const { return SphereActor; }


		virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintCallable, Category = "Orchestrator|Rotation")
	void RotateMazeToCell(
		int32 FromFace, int32 FromX, int32 FromY,
		int32 ToFace,   int32 ToX,   int32 ToY,
		float Duration = 0.12f
	);

protected:

	virtual void BeginPlay() override;

	bool bRotatingMaze = false;
	float RotateElapsed = 0.f;
	float RotateDuration = 0.12f;

	FQuat RotateStart;
	FQuat RotateTarget;


	// ---- Owned data/components ----
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Refs")
	UMaze *Maze = nullptr;

	// UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Walls")
	// UHierarchicalInstancedStaticMeshComponent *WallHISM = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Walls")
	UInstancedStaticMeshComponent *WallHISM = nullptr;

protected: // A* testing
	virtual void BeginPlay() override;
	void Astar();

	// UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Path")
	// UHierarchicalInstancedStaticMeshComponent *PathHISM = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Path")
	UInstancedStaticMeshComponent *PathHISM = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path")
	UStaticMesh *PathMesh = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path")
	UMaterialInterface *PathMaterial = nullptr;


protected:
	// Internal helpers (implemented later)
	void EnsureMazeGenerated();
	void BuildWallsFromMaze();

	// Spawn search helpers
	bool IsCellSpawnable(int32 Face, int32 X, int32 Y, int32 MinOpenSides) const;
	bool FindRandomSpawnCell(int32 &OutFace, int32 &OutX, int32 &OutY,
							 int32 MinOpenSides, int32 MaxTries) const;
};
