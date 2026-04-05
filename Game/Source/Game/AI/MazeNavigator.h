#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "../Maze/MazeTypes.h"
#include "../Conversion/CubeToSphere.h"
#include "MazeNavigator.generated.h"

class UMaze;
class ACubeToSphere;

/**
 * Generates a unique hash for a given FMazeNode.
 * Required by Unreal Engine's TMap and TSet to store FMazeNode structures as keys during pathfinding.
 * @param Node The maze node to hash.
 * @return The generated uint32 hash value.
 */
FORCEINLINE uint32 GetTypeHash(const FMazeNode &Node)
{
    return HashCombine(Node.Face, HashCombine(Node.X, Node.Y));
}

/**
 * UMazeNavigator
 * Implements the A* pathfinding algorithm for AI agents navigating the spherical maze.
 * Note: Requires the spherical maze data and visual mesh to be fully generated before use.
 */
UCLASS(BlueprintType)
class GAME_API UMazeNavigator : public UObject
{
    GENERATED_BODY()

public:
    // =========================================================================
    // Initialization & Core API
    // =========================================================================

    /**
     * Initializes the navigator with the required maze data and sphere geometry.
     * @param InMaze Pointer to the generated logical maze data.
     * @param InSphere Pointer to the physical spherical mesh actor.
     */
    UFUNCTION(BlueprintCallable)
    void Init(UMaze *InMaze, ACubeToSphere *InSphere);

    /**
     * Calculates the shortest path through the spherical maze using the A* algorithm.
     * @param StartPos The absolute 3D world coordinates where the AI currently is.
     * @param EndPos The absolute 3D world coordinates the AI wants to reach.
     * @param OutPath The generated array of safe step-by-step coordinates to follow.
     * @return True if a valid path was found, false if the destination is blocked or unreachable.
     */
    // UFUNCTION(BlueprintCallable)
    // bool FindPath(FVector StartPos, FVector EndPos, TArray<FVector> &OutPath);

    UFUNCTION(BlueprintCallable)
    bool FindPath(FVector StartPos, FVector EndPos, TArray<FVector> &OutPath, FVector ThreatPos = FVector::ZeroVector, float ThreatRadius = 0.0f);

private:
    // =========================================================================
    // Cached References
    // =========================================================================

    /** Cached reference to the logical maze grid data used to check for walls. */
    UPROPERTY()
    UMaze *Maze = nullptr;

    /** Cached reference to the physical sphere geometry used to calculate 3D coordinates. */
    UPROPERTY()
    ACubeToSphere *Sphere = nullptr;

    // =========================================================================
    // Internal Helper Functions
    // =========================================================================

    /**
     * Queries the maze data to find all traversable (unblocked) neighbors for a given node.
     * @param Node The current node being evaluated.
     * @return An array of valid, connected neighbor nodes.
     */
    TArray<FMazeNode> GetNeighbors(const FMazeNode &Node) const;

    /**
     * Converts an absolute 3D world position into a logical 2D grid node on the sphere.
     * @param WorldPos The 3D location in the game world.
     * @return The closest logical FMazeNode (Face, X, Y).
     */
    FMazeNode WorldToNode(FVector WorldPos) const;
};
