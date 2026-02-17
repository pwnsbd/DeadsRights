#pragma once

#include "CoreMinimal.h"
#include "MazeTypes.generated.h"

UENUM(BlueprintType)
enum class EMazeDir : uint8
{
	N UMETA(DisplayName="North"),
	E UMETA(DisplayName="East"),
	S UMETA(DisplayName="South"),
	W UMETA(DisplayName="West"),
};

//represents single cell in the maze; tracks which directions are open and whether it has been visited (for generation)
USTRUCT(BlueprintType)
struct FMazeCell
{
	GENERATED_BODY()

	UPROPERTY() bool OpenN = false;
	UPROPERTY() bool OpenE = false;
	UPROPERTY() bool OpenS = false;
	UPROPERTY() bool OpenW = false;
	UPROPERTY() bool bVisited = false;

	FMazeCell()
		: OpenN(false), OpenE(false), OpenS(false), OpenW(false), bVisited(false)
	{}
};