#pragma once

#include "CoreMinimal.h"
#include "../Artifact/Artifact.h"
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

	/** The one powered artifact introduced this level — spawns in the first slot.
	 *  None means only basic (no-ability) artifacts spawn. Ignored when bAllowAllArtifactTypes is true. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact Config")
	EArtifactType IntroducedArtifactType = EArtifactType::None;

	/** When true all four artifact types spawn in round-robin order (use for Wave 5+).
	 *  Overrides IntroducedArtifactType. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact Config")
	bool bAllowAllArtifactTypes = false;
};

/**
 * Scaling formula used to auto-generate level configs for waves 6 and beyond.
 * Edit on the BP_GameLevelManager Details panel.
 */
USTRUCT(BlueprintType)
struct GAME_API FProceduralWaveConfig
{
	GENERATED_BODY()

	/** Artifact count at the start of the first procedural wave (wave 6). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Procedural Config")
	int32 BaseArtifacts = 12;

	/** Additional artifacts added per procedural wave. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Procedural Config")
	int32 ArtifactsPerWaveGrowth = 2;

	/** Additional artifacts per level within a wave (level 2 = +1×, level 3 = +2×). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Procedural Config")
	int32 ArtifactsPerLevelGrowth = 2;

	/** Runner count at the start of the first procedural wave (wave 6). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Procedural Config")
	int32 BaseRunners = 5;

	/** Runner increase per procedural wave (fractional; result is rounded up). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Procedural Config")
	float RunnersPerWaveGrowth = 0.5f;

	/** Pre-level countdown duration for all procedural levels. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Procedural Config")
	float CountdownSeconds = 3.f;
};
