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

/**
 * AOrchestrator
 * Job: Orchestrate the current pipeline:
 * - Owns Maze data (UMaze)
 * - Calls Maze->Generate() & SphereActor->BuildSurface()
 * - Builds wall instances using Maze + Sphere mapping
 * - Manages the AI Runner and Artifact spawning logic
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
	virtual void OnConstruction(const FTransform &Transform) override;

	/**
	 * desc : Called when the game starts. Spawns the initial Start Marker and idle AI Runner.
	 * args : None
	 * result: None
	 */
	virtual void BeginPlay() override;

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
	UFUNCTION(BlueprintCallable, Category = "Orchestrator")
	void Rebuild();

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
	UFUNCTION(BlueprintCallable, Category = "Orchestrator|Spawn")
	bool GetRandomSpawnTransform(
		FTransform &OutTransform,
		float CapsuleHalfHeight = 88.f,
		int32 MinOpenSides = 2,
		int32 MaxTries = 5000,
		int32 PointSeed = 0) const;

	/**
	 * desc : Triggers the spawn of new artifacts on the maze and wakes up the AI to hunt them.
	 * args : None
	 * result: None
	 */
	UFUNCTION(BlueprintCallable, Category = "Orchestrator | AI")
	void TriggerNextRun();

	UMaze *GetMaze() const { return Maze; }

	/**
	 * desc : Brain function bound to the AI Runner's delegate. Calculates shortest distance to
	 * remaining artifacts, runs A* once, and dispatches the runner.
	 * args : None
	 * result: None
	 */
	UFUNCTION()
	void OnRunnerReachedArtifact();

	// =========================================================
	// Parameters / References (Public)
	// =========================================================

	// ---------- AI ----------

	/** Navigator object used for pathfinding over the maze graph. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Orchestrator | AI")
	UMazeNavigator *Navigator = nullptr;

	/** The Blueprint class of your AI Character */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orchestrator | AI")
	TSubclassOf<class AMazeRunner> MazeRunnerClass;

	/** Pointer to ensure we only ever spawn and manage ONE character */
	// UPROPERTY()
	// AMazeRunner *ActiveRunner = nullptr;

	/** We now track multiple runners. */
	UPROPERTY()
	TArray<AMazeRunner *> ActiveRunners;

	/** Finds the node on the sphere furthest from the player's current position. */
	FVector GetFarthestNodeFromActor(AActor *TargetActor);

	/** Assigns a unique artifact to a runner. */
	void AssignTargetToRunner(AMazeRunner *Runner);

	/** Mesh used to mark the Start and End points of the path. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orchestrator | AI")
	UStaticMesh *MarkerMesh = nullptr;

	/** Material for the Start marker. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orchestrator | AI")
	UMaterialInterface *StartMaterial = nullptr;

	/** Material to apply to the End markers/artifacts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orchestrator | AI")
	UMaterialInterface *EndMaterial = nullptr;

	/** Tracks how many times we've spawned artifacts to generate fresh seeds. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Orchestrator | AI")
	int32 RuntimeSeedOffset = 0;

	/** How many artifacts should spawn every time TriggerNextRun() is called? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orchestrator | AI")
	int32 NumArtifactsToSpawn = 1;

	/** List of all artifacts currently active on the sphere. */
	UPROPERTY()
	TArray<AStaticMeshActor *> ActiveArtifacts;

	/** The specific artifact the Runner is currently navigating towards. */
	UPROPERTY()
	AStaticMeshActor *CurrentTargetArtifact = nullptr;

	/** The starting spawn point reference. */
	UPROPERTY()
	AStaticMeshActor *StartMarkerRef = nullptr;

	/** Called automatically when the player touches an artifact. */
	UFUNCTION()
	void OnArtifactOverlapped(AActor *OverlappedActor, AActor *OtherActor);

	// ---------- Sphere / Refs ----------

	/** Reference to the sphere generator actor (usually found via child actor component). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orchestrator | Refs")
	ACubeToSphere *SphereActor = nullptr;

	/** Sphere radius used when building surface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orchestrator | Sphere", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float SphereRadius = 600.f;

	/** Sphere mesh resolution (kept locked to CellsPerFace + 1 inside Rebuild()). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orchestrator | Sphere")
	int32 Resolution = 32;

	// ---------- Maze ----------

	/** Maze random seed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orchestrator | Maze")
	int32 Seed = 122;

	/** Maze grid size per cube face. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orchestrator | Maze")
	int32 CellsPerFace = 31;

	// ---------- Walls ----------

	/** The static mesh used for individual wall segments. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orchestrator | Walls")
	UStaticMesh *WallMesh = nullptr;

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

	// ---------- Path Debug / Visualization ----------

	/** Mesh used for rendering debug path segments (if applicable). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orchestrator | Path")
	UStaticMesh *PathMesh = nullptr;

	/** Material applied to debug path segments. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orchestrator | Path")
	UMaterialInterface *PathMaterial = nullptr;

protected:
	// =========================================================
	// Owned Data / Components (Protected)
	// =========================================================

	/** Owned maze data object (logical maze structure). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Orchestrator | Refs")
	UMaze *Maze = nullptr;

	/** Instanced mesh component holding all physical wall segments. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Orchestrator | Walls")
	UInstancedStaticMeshComponent *WallHISM = nullptr;

	/** Instanced mesh component holding path markers. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Orchestrator | Path")
	UInstancedStaticMeshComponent *PathHISM = nullptr;

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
	 * desc : Converts Maze logical walls into physical instanced mesh wall segments on the sphere surface.
	 * args : None
	 * result: None
	 */
	void BuildWallsFromMaze();

	/**
	 * desc : Checks if a given cell meets spawn requirements (at least MinOpenSides open directions).
	 * args :
	 * - Face, X, Y: cell indices
	 * - MinOpenSides: minimum number of open sides required
	 * result: True if spawnable; otherwise False.
	 */
	bool IsCellSpawnable(int32 Face, int32 X, int32 Y, int32 MinOpenSides) const;

	/**
	 * desc : Randomly searches for a spawnable cell within MaxTries attempts using a seed offset.
	 * args :
	 * - OutFace, OutX, OutY: returned cell indices if found
	 * - MinOpenSides: minimum open sides required
	 * - MaxTries: maximum random attempts
	 * - PointSeed: Additional offset to ensure varied locations.
	 * result: True if a cell was found; otherwise False.
	 */
	bool FindRandomSpawnCell(
		int32 &OutFace,
		int32 &OutX,
		int32 &OutY,
		int32 MinOpenSides,
		int32 MaxTries,
		int32 PointSeed) const;
};
