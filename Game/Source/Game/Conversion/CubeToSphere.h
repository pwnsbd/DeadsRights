#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "../Maze/MazeTypes.h"                 // for EMazeDir
#include "CubeToSphere.generated.h"

class UProceduralMeshComponent;

/**
 * ACubeToSphere
 * Job: Build cube->sphere surface mesh + provide mapping helpers (cell centers / wall edges).
 * It does NOT generate maze, does NOT spawn walls, does NOT do AI.
 */
UCLASS()
class GAME_API ACubeToSphere : public AActor
{
	GENERATED_BODY()

public:
	ACubeToSphere();
	virtual void OnConstruction(const FTransform& Transform) override;

	// ---- Build surface mesh only ----
	UFUNCTION(BlueprintCallable, Category="CubeToSphere|Build")
	void BuildSurface();

	// Backwards compatible name (old code calls Build()).
	UFUNCTION(BlueprintCallable, Category="CubeToSphere|Build")
	void Build();

	// ---- Grid helpers ----
	UFUNCTION(BlueprintPure, Category="CubeToSphere|Grid")
	int32 GetCellsPerFace() const { return FMath::Max(1, Resolution - 1); }

	UFUNCTION(BlueprintCallable, Category="CubeToSphere|Grid")
	FVector GetCellCenterLocal(int32 Face, int32 CellX, int32 CellY) const;

	UFUNCTION(BlueprintCallable, Category="CubeToSphere|Grid")
	FVector GetCellCenterWorld(int32 Face, int32 CellX, int32 CellY) const;

	/**
	 * Contract function: returns the endpoints of the requested cell wall edge on the sphere.
	 * This lets other systems build walls without knowing vertex indexing.
	 */
	UFUNCTION(BlueprintCallable, Category="CubeToSphere|Grid")
	bool GetCellWallEdgeLocal(int32 Face, int32 CellX, int32 CellY, EMazeDir Dir,
	                          FVector& OutA, FVector& OutB) const;
	
	UFUNCTION(BlueprintCallable, Category="CubeToSphere|Grid")
	bool GetCellWallEdgeWorld(int32 Face, int32 CellX, int32 CellY, EMazeDir Dir,
	                          FVector& OutA, FVector& OutB) const;

	UFUNCTION(BlueprintPure, Category="CubeToSphere|Shape")
	float GetRadius() const { return Radius; }
	
	void SetRadius(float val) { Radius = val; }
	
	// ---- Shape params ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="CubeToSphere|Shape")
	int32 Resolution = 32; // vertices per side (>=2). cells per face = Resolution - 1

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="CubeToSphere|Shape")
	float Radius = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="CubeToSphere|Shape")
	float GridSpacing = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="CubeToSphere|Rendering")
	UMaterialInterface* CustomMaterial = nullptr;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CubeToSphere")
	UProceduralMeshComponent* Mesh = nullptr;

	// Face rotations (6 cube faces)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="CubeToSphere|Faces")
	TArray<FRotator> FaceRotations;

	// ---- Cached per-face verts on the sphere (LOCAL space) ----
	TArray<TArray<FVector>> FaceSphereVerts; // [Face][Resolution*Resolution]

	// Shared grid for one face
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CubeToSphere|Cache")
	TArray<FVector> FaceGridVertsLocal;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CubeToSphere|Cache")
	TArray<int32> FaceTriangles;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CubeToSphere|Cache")
	TArray<FVector2D> FaceUVs;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CubeToSphere|Cache")
	int32 VerticesPerSection = 0;

protected:
	// Internal steps
	void CreateFaceRotations();
	void CreateFaceGrid();
	void VertsPerFace(int32 FaceIndex, TArray<FVector>& OutVerts) const;
	void BuildFaceSection(int32 FaceIndex, const TArray<FVector>& FaceVerts);

	// Helper: vertex indexing inside one face array (Resolution x Resolution)
	FORCEINLINE int32 VertIndex(int32 VX, int32 VY) const { return VY * Resolution + VX; }
};
