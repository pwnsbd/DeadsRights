#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "../Maze/MazeTypes.h"
#include "../Conversion/CubeToSphere.h"
#include "Navigator.generated.h"

class UMaze;
class ACubeToSphere;

// // defines the node structure for the A* pathfinding algorithm
// USTRUCT(BlueprintType)
// struct FMazeNode
// {
//     GENERATED_BODY()

//     UPROPERTY(EditAnywhere, BlueprintReadWrite)
//     int32 Face = -1;
//     UPROPERTY(EditAnywhere, BlueprintReadWrite)
//     int32 X = -1;
//     UPROPERTY(EditAnywhere, BlueprintReadWrite)
//     int32 Y = -1;

//     FMazeNode() {}
//     FMazeNode(int32 InFace, int32 InX, int32Y InY) : Face(InFace), X(InX), Y(InY) {}

//     // equality operator for comparing two FMazeNode instances
//     bool operator==(const FMazeNode &Other) const
//     {
//         return Face == Other.Face && X == Other.X && Y == Other.Y;
//     }
// };

// hashing function for TMap and TSet
FORCEINLINE uint32 GetTypeHash(const FMazeNode &Node)
{
    return HashCombine(Node.Face, HashCombine(Node.X, Node.Y));
}

// function to calculate path through the maze using A* algorithm
UCLASS(BlueprintType)
class GAME_API UMazeNavigator : public UObject
{
    GENERATED_BODY()

public:
    // initializes the data and visuals
    UFUNCTION(BlueprintCallable)
    void Init(UMaze *InMaze, ACubeToSphere *InSphere);

    // returns a list of world positions to navigate through
    UFUNCTION(BlueprintCallable)
    bool FindPath(FVector StartPos, FVector EndPos, TArray<FVector> &OutPath);

private:
    UPROPERTY()
    UMaze *Maze = nullptr;
    UPROPERTY()
    ACubeToSphere *Sphere = nullptr;

    // helper function
    TArray<FMazeNode> GetNeighbors(const FMazeNode &Node) const;

    // helper to find the closest grid cell
    FMazeNode WorldToNode(FVector WorldPos) const;
};
