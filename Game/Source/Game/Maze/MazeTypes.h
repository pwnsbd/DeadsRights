#pragma once

#include "CoreMinimal.h"
#include "MazeTypes.generated.h"

UENUM(BlueprintType)
enum class EMazeDir : uint8
{
	N UMETA(DisplayName = "North"),
	E UMETA(DisplayName = "East"),
	S UMETA(DisplayName = "South"),
	W UMETA(DisplayName = "West"),
};

// represents single cell in the maze; tracks which directions are open and whether it has been visited (for generation)
USTRUCT(BlueprintType)
struct FMazeCell
{
	GENERATED_BODY()

	UPROPERTY()
	bool OpenN = false;
	UPROPERTY()
	bool OpenE = false;
	UPROPERTY()
	bool OpenS = false;
	UPROPERTY()
	bool OpenW = false;
	UPROPERTY()
	bool bVisited = false;

	FMazeCell()
		: OpenN(false), OpenE(false), OpenS(false), OpenW(false), bVisited(false)
	{
	}
};

// Represents a node in the maze graph for pathfinding; identifies a specific cell by its face and (x,y) coordinates on that face
// USTRUCT(BlueprintType)
// struct FMazeNode
// {
// 	GENERATED_BODY()

// 	UPROPERTY(VisibleAnywhere, BlueprintReadWrite)
// 	int32_t Face;
// 	UPROPERTY(VisibleAnywhere, BlueprintReadWrite)
// 	int32_t X;
// 	UPROPERTY(VisibleAnywhere, BlueprintReadWrite)
// 	int32_t Y;
// };

USTRUCT(BlueprintType)
struct FMazeNode
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Face = -1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 X = -1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Y = -1;

	FMazeNode() {}
	FMazeNode(int32 InFace, int32 InX, int32 InY) : Face(InFace), X(InX), Y(InY) {}

	// equality operator for comparing two FMazeNode instances
	bool operator==(const FMazeNode &Other) const
	{
		return Face == Other.Face && X == Other.X && Y == Other.Y;
	}
};
