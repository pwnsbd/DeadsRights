#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "../LevelManager/GameLevelTypes.h"
#include "../Artifact/Artifact.h"
#include "GameLevelManager.generated.h"

class AOrchestrator;
class AMyCharacterBase;

// ── Delegates (declared before UCLASS so Blueprint can use them) ──────────────

/** Fired each second during countdown. SecondsRemaining counts down from N to 1. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCountdownTick, int32, SecondsRemaining);

/** Fired when countdown reaches 0 and the level starts. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnCountdownComplete);

/** Fired when an AI escapes with an artifact — player loses. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnGameLost);

/** Fired when all artifacts on a level are resolved — player wins the level. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLevelWon, int32, CompletedLevelIndex);

/** Fired when the player clears the final level. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnGameComplete);

/**
 * AGameLevelManager
 *
 * Top-level game loop director. Sits above AOrchestrator and drives:
 *   Countdown → Level Setup → Win Detection → Next Level
 *
 * C++ owns all logic. Create a Blueprint child class to configure
 * LevelConfigs array and bind HUD/audio delegates without touching C++.
 */
UCLASS(BlueprintType, Blueprintable)
class GAME_API AGameLevelManager : public AActor
{
	GENERATED_BODY()

public:
	AGameLevelManager();

protected:
	virtual void BeginPlay() override;

	// =========================================================
	// Config
	// =========================================================

	/** Define one entry per level. Edit in the Blueprint CDO Details panel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level Manager|Config")
	TArray<FLevelConfig> LevelConfigs;

	/** The Blueprint artifact class to spawn — set this in BP_GameLevelManager Details panel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level Manager|Config")
	TSubclassOf<AArtifact> ArtifactClass;

	// =========================================================
	// State (read-only in Blueprint)
	// =========================================================

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Level Manager|State")
	EGameLevelState CurrentState = EGameLevelState::Idle;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Level Manager|State")
	int32 CurrentLevelIndex = 0;

	// =========================================================
	// References  (drag-drop in editor, or auto-found at BeginPlay)
	// =========================================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level Manager|Refs")
	AOrchestrator* Orchestrator = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level Manager|Refs")
	AMyCharacterBase* PlayerCharacter = nullptr;

	// =========================================================
	// Public API
	// =========================================================

	/** Searches the world for Orchestrator and PlayerCharacter if not set. */
	UFUNCTION(BlueprintCallable, Category = "Level Manager")
	void FindReferences();

	/** Returns true only when both Orchestrator and PlayerCharacter are valid. */
	UFUNCTION(BlueprintPure, Category = "Level Manager")
	bool HasValidReferences() const;

	// =========================================================
	// Countdown
	// =========================================================

	/** Kicks off the pre-level countdown. Called automatically from BeginPlay
	 *  and again at the start of each new level. */
	UFUNCTION(BlueprintCallable, Category = "Level Manager")
	void StartCountdown();

	/** Applies the current FLevelConfig to Orchestrator and spawns AI + artifacts. */
	UFUNCTION(BlueprintCallable, Category = "Level Manager")
	void SetupCurrentLevel();

	/** Called when an AI successfully escapes — resets inventory and restarts from Level 1. */
	UFUNCTION(BlueprintCallable, Category = "Level Manager")
	void OnGameLost();

	/** Polls artifact count; fires when all artifacts are gone. */
	UFUNCTION(BlueprintCallable, Category = "Level Manager")
	void CheckWinCondition();

	/** Returns how many artifacts are still uncollected on the current level. */
	UFUNCTION(BlueprintPure, Category = "Level Manager")
	int32 GetRemainingArtifactCount() const;

	/** Returns how many AI runners are still alive on the current level. */
	UFUNCTION(BlueprintPure, Category = "Level Manager")
	int32 GetRemainingRunnerCount() const;

	/** Blueprint-bindable so HUD/audio can react to a loss. */
	UPROPERTY(BlueprintAssignable, Category = "Level Manager|Events")
	FOnGameLost OnGameLostDelegate;

	/** Fired every second — binds to HUD to show the number on screen. */
	UPROPERTY(BlueprintAssignable, Category = "Level Manager|Events")
	FOnCountdownTick OnCountdownTickDelegate;

	/** Fired when countdown hits 0 (GO!). Bind HUD/audio here in Blueprint. */
	UPROPERTY(BlueprintAssignable, Category = "Level Manager|Events")
	FOnCountdownComplete OnCountdownCompleteDelegate;

	/** Fired when the level is cleared (all artifacts resolved). */
	UPROPERTY(BlueprintAssignable, Category = "Level Manager|Events")
	FOnLevelWon OnLevelWonDelegate;

	/** Fired when the player clears every level in LevelConfigs. */
	UPROPERTY(BlueprintAssignable, Category = "Level Manager|Events")
	FOnGameComplete OnGameCompleteDelegate;

	/** Seconds to wait after a level win before starting the next countdown. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level Manager|Config")
	float LevelTransitionDelay = 2.f;

private:
	/** Fired one tick after BeginPlay — all actors are ready by then. */
	void OnWorldReady();

	/** Seed captured from Orchestrator at startup — used as the base for per-level offsets. */
	int32 BaseSeed = 0;

	/** Timer used for both level transition and loss restart delays. */
	FTimerHandle TransitionTimerHandle;

	/** Repeating 0.5s timer that polls GetRemainingArtifactCount for the win condition. */
	FTimerHandle WinCheckTimerHandle;

	/** Called after loss delay — resets state and starts countdown for Level 1. */
	void OnLostRestartCountdown();

	/** Called when remaining artifact count hits 0 — handles win + progression. */
	void OnLevelWon();

	/** Called after win transition delay — increments level or fires game-complete. */
	void TransitionToNextLevel();

	/** Repeating 1-second timer that drives the countdown. */
	FTimerHandle CountdownTimerHandle;

	/** Current countdown value, decrements each tick. */
	int32 CountdownSecondsRemaining = 0;

	/** Called every second by CountdownTimerHandle. */
	void OnCountdownTick();

	/** Called when countdown reaches 0. */
	void OnCountdownComplete();
};
