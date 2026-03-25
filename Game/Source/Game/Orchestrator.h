#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AI/MazeNavigator.h"
#include "Components/ChildActorComponent.h"
#include "Orchestrator.generated.h"

class ACubeToSphere;
class UMaze;
class UInstancedStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;
class AMyCharacterBase;

/**
 * AOrchestrator
 * Job: Orchestrate the current pipeline:
 * - Own Maze data (UMaze)
 * - Call Maze->Generate()
 * - Call SphereActor->BuildSurface()
 * - Build wall instances using Maze + Sphere mapping
 * - Provide a spawn transform on an open cell
 */
UCLASS()
class GAME_API AOrchestrator : public AActor
{
	GENERATED_BODY()

public:
	// =========================================================
	// Functions (Public)
	// =========================================================

	/**
	 * desc : Default constructor. Creates root + wall/path instanced mesh components and sets basic collision rules.
	 * args : None
	 * result: None
	 */
	AOrchestrator();

	/**
	 * desc : Editor/runtime construction hook. Rebuilds the maze when placed/edited in the editor.
	 * args : Transform - current actor transform during construction.
	 * result: None
	 */
	virtual void OnConstruction(const FTransform& Transform) override;

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
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Orchestrator")
	void Rebuild();
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
	UFUNCTION(BlueprintCallable, Category = "Orchestrator|Spawn")
	bool GetRandomSpawnTransform(
		FTransform& OutTransform,
		float CapsuleHalfHeight = 88.f,
		int32 MinOpenSides = 2,
		int32 MaxTries = 5000
	) const;

	// =========================================================
	// Parameters / References (Public)
	// =========================================================

	// ---------- AI ----------

	/**
	 * desc : Navigator object used for pathfinding over the maze graph.
	 * args : None
	 * result: None
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Orchestrator | AI")
	UMazeNavigator* Navigator = nullptr;

	// ---------- Sphere / Refs ----------

	/**
	 * desc : Reference to the sphere generator actor (usually found via child actor component).
	 * args : None
	 * result: None
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orchestrator | Refs")
	ACubeToSphere* SphereActor = nullptr;

	/** Sphere radius used when building surface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orchestrator | Sphere", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float SphereRadius = 600.f;

	// If true, every editor rebuild picks a new random seed automatically
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Maze")
	bool bRandomizeSeed = true;

	// Button in Details panel
	UFUNCTION(CallInEditor, Category="Maze")
	void RandomizeSeedNow();
	
	
	// Optional: set maze size here; Sphere Resolution should be CellsPerFace+1
	/** Sphere mesh resolution (kept locked to CellsPerFace + 1 inside Rebuild()). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orchestrator | Sphere")
	int32 Resolution = 7;

	// ---------- Maze ----------

	/** Maze random seed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orchestrator | Maze")
	int32 Seed = 122;

	/** Maze grid size per cube face. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orchestrator | Maze")
	int32 CellsPerFace = 6;

	// ---------- Walls ----------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orchestrator | Walls")
	UStaticMesh* WallMesh = nullptr;

	/** Reference mesh edge length (Engine cube default ~100 units). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orchestrator | Walls")
	float WallMeshBaseLength = 100.f;

	/** Wall height along local Up (sphere normal). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orchestrator | Walls")
	float WallHeight = 20.f;

	/** Wall thickness along local Right. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orchestrator | Walls")
	float WallThickness = 2.f;

	/** Push walls slightly away from surface to avoid z-fighting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orchestrator | Walls")
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

	/**
	 * desc : Rolls the maze opposite the desired move direction while stepping from one logical cell to the next.
	 * args :
	 *   - FromFace, FromX, FromY: current logical cell before the move
	 *   - ToFace, ToX, ToY: next logical cell after the move
	 *   - DesiredWorldMoveDirection: camera-relative tangent direction the player tried to move toward
	 *   - Duration: rotation tween duration in seconds
	 * result: None
	 */
	UFUNCTION(BlueprintCallable, Category = "Orchestrator|Rotation")
	void RotateMazeAgainstMoveDirection(
		int32 FromFace, int32 FromX, int32 FromY,
		int32 ToFace,   int32 ToX,   int32 ToY,
		const FVector& DesiredWorldMoveDirection,
		float Duration = 0.12f
	);

	/**
	 * desc : Reports whether the maze actor is currently rotating between cells.
	 * args : None
	 * result: True while a maze-rotation move is active; otherwise False.
	 */
	UFUNCTION(BlueprintPure, Category = "Orchestrator|Rotation")
	bool IsMazeRotating() const { return bRotatingMaze; }
	
	
	//----- Character ------
	UPROPERTY(BlueprintReadWrite, Category="Spawn")
	AMyCharacterBase* SpawnedPawn = nullptr;

protected:

	//virtual void BeginPlay() override;

	bool bRotatingMaze = false;
	bool bUseSettledRollRotation = false;
	float RotateElapsed = 0.f;
	float RotateDuration = 0.12f;
	float RotatePrimaryAngleRadians = 0.f;
	float RotatePrimaryPhasePortion = 0.82f;

	FQuat RotateStart;
	FQuat RotateMidTarget;
	FQuat RotateTarget;
	FVector RotatePrimaryAxisWorld = FVector::UpVector;
	
	// ---------- Path Debug / Visualization ----------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orchestrator | Path")
	UStaticMesh* PathMesh = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orchestrator | Path")
	UMaterialInterface* PathMaterial = nullptr;


protected:
	// =========================================================
	// Owned Data / Components (Protected)
	// =========================================================

	/** Owned maze data object (logical maze). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Orchestrator | Refs")
	UMaze* Maze = nullptr;

	/** Instanced mesh component holding all wall segments. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Orchestrator | Walls")
	UInstancedStaticMeshComponent* WallHISM = nullptr;

	/** Instanced mesh component holding path markers (debug). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Orchestrator | Path")
	UInstancedStaticMeshComponent* PathHISM = nullptr;

private:
	// =========================================================
	// Functions (Private)
	// =========================================================

	/**
	 * desc : Searches attached ChildActorComponents and assigns SphereActor if a CubeToSphere child is found.
	 * args : None
	 * result: None
	 */
	void ResolveSphereFromChild();

	/**
	 * desc : Ensures Maze object exists and regenerates maze data using current CellsPerFace + Seed.
	 * args : None
	 * result: None
	 */
	void EnsureMazeGenerated();

	/**
	 * desc : Converts Maze walls into instanced mesh wall segments on the sphere surface.
	 * args : None
	 * result: None
	 */
	void BuildWallsFromMaze();

	/**
	 * desc : Debug/test pathfinding routine. Picks start/end points, runs Navigator->FindPath(),
	 *        then draws results via instanced meshes or debug spheres.
	 * args : None
	 * result: None
	 */
	void Astar();

	/**
	 * desc : Checks if a given cell meets spawn requirements (at least MinOpenSides open directions).
	 * args :
	 *   - Face, X, Y: cell indices
	 *   - MinOpenSides: minimum number of open sides required
	 * result: True if spawnable; otherwise False.
	 */
	bool IsCellSpawnable(int32 Face, int32 X, int32 Y, int32 MinOpenSides) const;

	/**
	 * desc : Randomly searches for a spawnable cell within MaxTries attempts.
	 * args :
	 *   - OutFace, OutX, OutY: returned cell indices if found
	 *   - MinOpenSides: minimum open sides required
	 *   - MaxTries: maximum random attempts
	 * result: True if a cell was found; otherwise False.
	 */
	bool FindRandomSpawnCell(
		int32& OutFace,
		int32& OutX,
		int32& OutY,
		int32 MinOpenSides,
		int32 MaxTries
	) const;
};
