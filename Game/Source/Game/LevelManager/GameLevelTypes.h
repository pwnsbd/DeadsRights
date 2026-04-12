#pragma once

#include "CoreMinimal.h"
#include "GameLevelTypes.generated.h"

/**
 * Tracks the current state of the game loop managed by AGameLevelManager.
 */
UENUM(BlueprintType)
enum class EGameLevelState : uint8
{
	Idle        UMETA(DisplayName = "Idle"),
	Countdown   UMETA(DisplayName = "Countdown"),
	Running     UMETA(DisplayName = "Running"),
	LevelWon    UMETA(DisplayName = "Level Won"),
	GameOver    UMETA(DisplayName = "Game Over")
};

/**
 * Defines the parameters for a single game level/wave.
 * Populate an array of these on AGameLevelManager (Blueprint-editable).
 */
USTRUCT(BlueprintType)
struct GAME_API FLevelConfig
{
	GENERATED_BODY()

	/** Uncheck to skip this level without removing it from the array. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level Config")
	bool bEnabled = true;

	/** How many AI runners to spawn for this level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level Config")
	int32 NumRunners = 2;

	/** How many artifacts to spawn for this level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level Config")
	int32 NumArtifacts = 4;

	/** Added to the base maze seed so each level has varied spawn positions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level Config")
	int32 SeedOffset = 0;

	/** How many seconds the pre-level countdown lasts (3 = "3...2...1...GO"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level Config")
	float CountdownSeconds = 3.f;
};
