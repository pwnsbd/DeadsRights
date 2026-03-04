#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MazeTypes.h"
#include "Maze.generated.h"

/**
 * UMaze
 * Job: Generate and store maze connectivity on 6 cube faces (each face is a CellsPerFace x CellsPerFace grid).
 * Output data: Cells[] where each FMazeCell has OpenN/E/S/W (corridors) + bVisited (generation-only).
 */
UCLASS(BlueprintType)
class GAME_API UMaze : public UObject
{
	GENERATED_BODY()

public:
	// ---- Parameters ----

	// Number of cells per face (face grid resolution)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze")
	int32 CellsPerFace = 32;

	// How many portals per shared border
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze")
	int32 CorridorsPerBorder = 30;

	// Seed used to generate deterministic mazes
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze")
	int32 Seed = 12345;

	// Flat storage for all 6 faces
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Maze")
	TArray<FMazeCell> Cells;

public:
	// ---- Public API ----

	// Generate maze for all faces + stitch between faces
	UFUNCTION(BlueprintCallable, Category = "Maze")
	void Generate();

	// Access a cell (returns Empty cell if invalid)
	UFUNCTION(BlueprintCallable, Category = "Maze")
	const FMazeCell &GetCell(int32 Face, int32 X, int32 Y) const;

	// Total number of cells across all faces
	UFUNCTION(BlueprintPure, Category = "Maze")
	int32 GetTotalCells() const { return 6 * CellsPerFace * CellsPerFace; }

	// Returns a list of valid neighboring nodes that can be traversed to from the input node (used for AI pathfinding)
	UFUNCTION(BlueprintCallable, Category = "Maze")
	TArray<FMazeNode> GetTraversableNeighbors(const FMazeNode &Node) const;

	// --- Traversal Utilities ---
	UFUNCTION(BlueprintCallable)
	FMazeNode GetNeighborCell(
		const FMazeNode &Node,
		EMazeDir Dir,
		bool bIgnoreWalls = true) const;

	UFUNCTION(BlueprintCallable)
	TArray<FMazeNode> GetCellsInLine(
		const FMazeNode &Start,
		EMazeDir Dir,
		int32 Distance,
		bool bIgnoreWalls = true) const;

	bool TryFaceTransition(
		const FMazeNode &Node,
		EMazeDir Dir,
		FMazeNode &OutNode) const;

private:
	// Convert (Face, X, Y) to flat index
	FORCEINLINE int32 Index(int32 Face, int32 X, int32 Y) const
	{
		return Face * CellsPerFace * CellsPerFace + Y * CellsPerFace + X;
	}

	// Bounds check
	bool IsValid(int32 Face, int32 X, int32 Y) const;

private:
	// ---- Internal Steps ----

	// Reset all cells to default (all walls closed, not visited)
	void ResetCells();

	// Carve DFS maze on a single face
	void CarveDFSSingleFace(int32 TargetFace);

	// Open a corridor between two adjacent cells on the same face
	void OpenBetween(int32 Face, int32 X1, int32 Y1,
					 int32 X2, int32 Y2,
					 EMazeDir DirFromAtoB);

	// Open corridors between faces (cube layout stitching)
	void StitchFaces();
};
