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

	// Used by maze generation (DFS etc.)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	bool bVisited = false;

	// True = corridor is open in that direction
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) bool OpenN = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) bool OpenE = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) bool OpenS = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) bool OpenW = false;
};