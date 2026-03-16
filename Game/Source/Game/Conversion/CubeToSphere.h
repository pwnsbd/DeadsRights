#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "../Maze/MazeTypes.h" // EMazeDir
#include "CubeToSphere.generated.h"

class UProceduralMeshComponent;
class UMaterialInterface;

/**
 * ACubeToSphere
 * Job: Build cube->sphere surface mesh + provide mapping helpers (cell centers / wall edges).
 */
UCLASS()
class GAME_API ACubeToSphere : public AActor
{
	GENERATED_BODY()

public:
	// =========================================================
	// Functions (Public)
	// =========================================================

	/**
	 * desc : Default constructor. Creates procedural mesh component, sets root, and initializes face rotations.
	 * args : None
	 * result: None
	 */
	ACubeToSphere();

	/**
	 * desc : Editor/runtime construction hook. Rebuilds the sphere surface so changes reflect in editor.
	 * args : Transform - current actor transform during construction.
	 * result: None
	 */
	virtual void OnConstruction(const FTransform& Transform) override;

	/**
	 * desc : Builds (or rebuilds) the full cube->sphere mesh:
	 *        - clamps resolution
	 *        - creates face rotations
	 *        - creates shared face grid
	 *        - generates per-face sphere vertices
	 *        - creates mesh sections (1 per face)
	 * args : None
	 * result: None
	 */
	UFUNCTION(BlueprintCallable, Category = "CubeToSphere|Build")
	void BuildSurface();

	/**
	 * desc : Returns number of maze cells per face (Resolution-1), clamped to at least 1.
	 * args : None
	 * result: CellsPerFace (>=1)
	 */
	UFUNCTION(BlueprintPure, Category = "CubeToSphere|Grid")
	int32 GetCellsPerFace() const { return FMath::Max(1, Resolution - 1); }

	/**
	 * desc : Returns the local-space center position of a maze cell on the sphere surface.
	 *        Uses the four corner vertices of the cell, averages them, then re-projects to Radius.
	 * args :
	 *   - Face: cube face index [0..5]
	 *   - CellX: cell x index [0..CellsPerFace-1]
	 *   - CellY: cell y index [0..CellsPerFace-1]
	 * result: Local-space center on the sphere surface, or ZeroVector if invalid.
	 */
	UFUNCTION(BlueprintCallable, Category = "CubeToSphere|Grid")
	FVector GetCellCenterLocal(int32 Face, int32 CellX, int32 CellY) const;

	/**
	 * desc : Returns the world-space center position of a maze cell on the sphere surface.
	 * args :
	 *   - Face: cube face index [0..5]
	 *   - CellX: cell x index
	 *   - CellY: cell y index
	 * result: World-space center on the sphere surface.
	 */
	UFUNCTION(BlueprintCallable, Category = "CubeToSphere|Grid")
	FVector GetCellCenterWorld(int32 Face, int32 CellX, int32 CellY) const;

	/**
	 * desc : Returns local-space endpoints of a given cell wall edge (N/E/S/W) on the sphere.
	 *        Contract: Other systems can build walls without knowing vertex indexing.
	 * args :
	 *   - Face, CellX, CellY: cell coordinate
	 *   - Dir: requested wall direction (N/E/S/W)
	 *   - OutA: endpoint A (local-space)
	 *   - OutB: endpoint B (local-space)
	 * result: True if endpoints were computed successfully; False if inputs/caches invalid.
	 */
	UFUNCTION(BlueprintCallable, Category = "CubeToSphere|Grid")
	bool GetCellWallEdgeLocal(int32 Face, int32 CellX, int32 CellY, EMazeDir Dir,
	                          FVector& OutA, FVector& OutB) const;

	/**
	 * desc : Returns world-space endpoints of a given cell wall edge (N/E/S/W) on the sphere.
	 * args :
	 *   - Face, CellX, CellY: cell coordinate
	 *   - Dir: requested wall direction (N/E/S/W)
	 *   - OutA: endpoint A (world-space)
	 *   - OutB: endpoint B (world-space)
	 * result: True if endpoints were computed successfully; False otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category = "CubeToSphere|Grid")
	bool GetCellWallEdgeWorld(int32 Face, int32 CellX, int32 CellY, EMazeDir Dir,
	                          FVector& OutA, FVector& OutB) const;

	// ---- Helper functions for gameplay ----
	/**
	 * Convert a world position to the closest maze cell
	 * Returns FMazeNode with Face, X, Y coordinates
	 */
	UFUNCTION(BlueprintCallable, Category="CubeToSphere|Helper")
	FMazeNode WorldToMazeCell(FVector WorldPosition) const;

	/**
	 * Convert a world direction vector to a maze direction (N/E/S/W) for a given cell
	 * ForwardVector: typically the character's forward vector
	 * CurrentCell: the cell the character is standing in
	 */
	UFUNCTION(BlueprintCallable, Category="CubeToSphere|Helper")
	EMazeDir GetDirectionFromVector(FVector ForwardVector, const FMazeNode& CurrentCell) const;
	
	/**
	 * desc : Returns current sphere radius used for projection.
	 * args : None
	 * result: Radius value
	 */
	UFUNCTION(BlueprintPure, Category="CubeToSphere|Shape")
	float GetRadius() const { return Radius; }

	/**
	 * desc : Sets sphere radius used for projection (does not auto-rebuild).
	 * args : val - new radius
	 * result: None
	 */
	void SetRadius(float val) { Radius = val; }

	/**
	 * desc : Sets mesh resolution (vertices per side) (does not auto-rebuild).
	 * args : resolution - new resolution value
	 * result: None
	 */
	void SetResolution(int32 resolution) { Resolution = resolution; }

	// =========================================================
	// Parameters (Public)
	// =========================================================

	// ---------- Shape ----------

	/** vertices per side (>=2). cells per face = Resolution - 1 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CubeToSphere|Shape")
	int32 Resolution = 32;

	/** sphere radius for projection */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CubeToSphere|Shape")
	float Radius = 100.f;

	/** spacing used by CreateGridMeshWelded before projection */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CubeToSphere|Shape")
	float GridSpacing = 1.f;

	// ---------- Rendering ----------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CubeToSphere|Rendering")
	UMaterialInterface* CustomMaterial = nullptr;

protected:
	// =========================================================
	// Components (Protected)
	// =========================================================

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CubeToSphere|Components")
	UProceduralMeshComponent* Mesh = nullptr;

	// =========================================================
	// Face Orientation / Cache (Protected)
	// =========================================================

	/** Face rotations used to orient the shared grid onto each cube face. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CubeToSphere|Faces")
	TArray<FRotator> FaceRotations;

	/** Shared grid verts for a single face (local, before face rotation & projection). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CubeToSphere|Cache")
	TArray<FVector> FaceGridVertsLocal;

	/** Shared grid triangles for a single face. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CubeToSphere|Cache")
	TArray<int32> FaceTriangles;

	/** Shared grid UVs for a single face. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CubeToSphere|Cache")
	TArray<FVector2D> FaceUVs;

	/** Convenience cache: number of vertices in a single face section. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CubeToSphere|Cache")
	int32 VerticesPerSection = 0;

private:
	// =========================================================
	// Internal Data (Private)
	// =========================================================

	/** Cached per-face sphere vertices in LOCAL space: [Face][Resolution*Resolution] */
	TArray<TArray<FVector>> FaceSphereVerts;

	// =========================================================
	// Internal Functions (Private)
	// =========================================================

	/**
	 * desc : Precomputes 6 face rotation matrices so the same grid can be reused per face.
	 * args : None
	 * result: None
	 */
	void CreateFaceRotations();

	/**
	 * desc : Builds the base welded grid (verts/triangles/uvs) for one face.
	 *        Also recenters the grid in local face space for consistent rotation.
	 * args : None
	 * result: None
	 */
	void CreateFaceGrid();

	/**
	 * desc : Generates projected sphere vertices for a given face index, using FaceGridVertsLocal.
	 * args :
	 *   - FaceIndex: face [0..5]
	 *   - OutVerts: output array of LOCAL-space sphere vertices for this face
	 * result: None
	 */
	void VertsPerFace(int32 FaceIndex, TArray<FVector>& OutVerts) const;

	/**
	 * desc : Creates (or updates) a procedural mesh section for one face using the provided vertices.
	 *        Normals are derived by normalizing each vertex direction from origin.
	 * args :
	 *   - FaceIndex: section index [0..5]
	 *   - FaceVerts: LOCAL-space sphere vertices for this face
	 * result: None
	 */
	void BuildFaceSection(int32 FaceIndex, const TArray<FVector>& FaceVerts);

	/**
	 * desc : Converts a (VX,VY) vertex coordinate into a linear index in a Resolution x Resolution grid.
	 * args :
	 *   - VX: vertex x index [0..Resolution-1]
	 *   - VY: vertex y index [0..Resolution-1]
	 * result: Linear vertex index.
	 */
	FORCEINLINE int32 VertIndex(int32 VX, int32 VY) const { return VY * Resolution + VX; }
};