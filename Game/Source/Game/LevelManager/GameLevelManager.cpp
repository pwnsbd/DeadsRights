#include "../LevelManager/GameLevelManager.h"
#include "../Orchestrator.h"
#include "../Movement/MyCharacterBase.h"
#include "../Artifact/MazeArtifactManager.h"
#include "../AI/MazeRunner.h"
#include "../Inventory/ItemStorageComponent.h"
#include "../GameAudioInstance.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundBase.h"
#include "Camera/PlayerCameraManager.h"

AGameLevelManager::AGameLevelManager()
{
	PrimaryActorTick.bCanEverTick = false;

	BackgroundMusic = LoadObject<USoundBase>(nullptr, TEXT("/Game/sound/CryptGameLoop.CryptGameLoop"));
	CountdownTickSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/sound/ButtonPress.ButtonPress"));
	CountdownCompleteSound = nullptr;
	LevelWonSound = nullptr;
	GameLostSound = nullptr;

	// ── Default level configs (15 hand-crafted levels) ──────────────────────
	// Wave 1 (L1-3): introduce Beam
	LevelConfigs = {
		// L1 — Beam introduced
		{ true, 1, 1, 0, 3.f, EArtifactType::Beam,      false },
		// L2
		{ true, 1, 2, 0, 3.f, EArtifactType::Basic,     false },
		// L3
		{ true, 1, 3, 0, 3.f, EArtifactType::Basic,     false },

		// Wave 2 (L4-6): introduce PhaseWalk
		// L4 — PhaseWalk introduced
		{ true, 2, 3, 0, 3.f, EArtifactType::PhaseWalk, false },
		// L5
		{ true, 2, 4, 0, 3.f, EArtifactType::Basic,     false },
		// L6
		{ true, 2, 5, 0, 3.f, EArtifactType::Basic,     false },

		// Wave 3 (L7-9): introduce PathFinder
		// L7 — PathFinder introduced
		{ true, 3, 5, 0, 3.f, EArtifactType::PathFinder, false },
		// L8
		{ true, 3, 6, 0, 3.f, EArtifactType::Basic,      false },
		// L9
		{ true, 3, 7, 0, 3.f, EArtifactType::Basic,      false },

		// Wave 4 (L10-12): introduce AoEBomb
		// L10 — AoEBomb introduced
		{ true, 4, 7, 0, 3.f, EArtifactType::AoEBomb,   false },
		// L11
		{ true, 4, 8, 0, 3.f, EArtifactType::Basic,     false },
		// L12
		{ true, 4, 9, 0, 3.f, EArtifactType::Basic,     false },

		// Wave 5 (L13-15): all artifact types allowed
		// L13
		{ true, 5, 9, 0, 3.f, EArtifactType::Basic, true },
		// L14
		{ true, 6, 9, 0, 3.f, EArtifactType::Basic, true },
		// L15
		{ true, 7, 9, 0, 3.f, EArtifactType::Basic, true },
	};
}

void AGameLevelManager::BeginPlay()
{
	Super::BeginPlay();

	// Defer one tick so all other actors (Orchestrator, PlayerCharacter) finish their BeginPlay
	GetWorldTimerManager().SetTimerForNextTick(this, &AGameLevelManager::OnWorldReady);
}

void AGameLevelManager::OnWorldReady()
{
	FindReferences();

	if (!HasValidReferences())
	{
		UE_LOG(LogTemp, Error, TEXT("[LevelManager] OnWorldReady: Missing references — Orchestrator or PlayerCharacter not found. Aborting."));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[LevelManager] Ready. Orchestrator=%s  PlayerCharacter=%s"),
		*Orchestrator->GetName(),
		*PlayerCharacter->GetName());

	// Capture base seed once — per-level offsets are added on top of this
	BaseSeed = Orchestrator->Seed;

	if (bStartBackgroundMusicOnBeginPlay && BackgroundMusic && !BackgroundMusicComponent)
	{
		// Background music normally starts in UGameAudioInstance so it can play on the main menu.
		// This stays as an optional fallback if that class is not configured.
		if (GetGameInstance() && !GetGameInstance()->IsA(UGameAudioInstance::StaticClass()))
		{
			BackgroundMusicComponent = UGameplayStatics::SpawnSound2D(this, BackgroundMusic, 1.0f, 1.0f, 0.0f, nullptr, true);
		}
	}

	if (bFadeInFromBlackOnBeginPlay)
	{
		if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
		{
			if (PC->PlayerCameraManager)
			{
				PC->PlayerCameraManager->StartCameraFade(1.0f, 0.0f, BeginPlayFadeInDuration, FLinearColor::Black, false, false);
			}
		}
	}

	StartCountdown();
}

void AGameLevelManager::FindReferences()
{
	// Orchestrator — search world if not set via Details panel
	if (!IsValid(Orchestrator))
	{
		for (TActorIterator<AOrchestrator> It(GetWorld()); It; ++It)
		{
			Orchestrator = *It;
			break;
		}
	}

	// PlayerCharacter — try UGameplayStatics first, then TActorIterator
	if (!IsValid(PlayerCharacter))
	{
		PlayerCharacter = Cast<AMyCharacterBase>(UGameplayStatics::GetPlayerPawn(GetWorld(), 0));
	}

	if (!IsValid(PlayerCharacter))
	{
		for (TActorIterator<AMyCharacterBase> It(GetWorld()); It; ++It)
		{
			PlayerCharacter = *It;
			break;
		}
	}
}

bool AGameLevelManager::HasValidReferences() const
{
	return IsValid(Orchestrator) && IsValid(PlayerCharacter);
}

// ── Countdown ────────────────────────────────────────────────────────────────

void AGameLevelManager::StartCountdown()
{
	if (!HasValidReferences()) return;

	CurrentState = EGameLevelState::Countdown;

	// Freeze player during countdown
	if (PlayerCharacter)
		PlayerCharacter->bInputFrozen = true;

	// Pull duration from the current level config (manual or procedural)
	const float Duration = GetConfigForLevel(CurrentLevelIndex).CountdownSeconds;

	CountdownSecondsRemaining = FMath::Max(1, FMath::RoundToInt(Duration));

	UE_LOG(LogTemp, Log, TEXT("[LevelManager] Starting countdown for Level %d (%d seconds)..."),
		CurrentLevelIndex + 1, CountdownSecondsRemaining);

	// Fire immediately then repeat every 1 second
	GetWorldTimerManager().SetTimer(
		CountdownTimerHandle,
		this,
		&AGameLevelManager::OnCountdownTick,
		1.0f,
		true,   // repeating
		0.0f);  // no initial delay — fires on the same frame
}

void AGameLevelManager::OnCountdownTick()
{
	UE_LOG(LogTemp, Log, TEXT("[LevelManager] %d..."), CountdownSecondsRemaining);
	if (CountdownTickSound)
	{
		UGameplayStatics::PlaySound2D(this, CountdownTickSound);
	}
	OnCountdownTickDelegate.Broadcast(CountdownSecondsRemaining);

	CountdownSecondsRemaining--;

	if (CountdownSecondsRemaining <= 0)
	{
		GetWorldTimerManager().ClearTimer(CountdownTimerHandle);
		OnCountdownComplete();
	}
}

void AGameLevelManager::OnCountdownComplete()
{
	UE_LOG(LogTemp, Log, TEXT("[LevelManager] GO! Level %d"), CurrentLevelIndex + 1);
	if (CountdownCompleteSound)
	{
		UGameplayStatics::PlaySound2D(this, CountdownCompleteSound);
	}

	// Unfreeze player and restore game input mode (cursor may have been shown on lose screen)
	if (PlayerCharacter)
		PlayerCharacter->bInputFrozen = false;

	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		PC->SetInputMode(FInputModeGameOnly());
		PC->SetShowMouseCursor(false);
	}

	OnCountdownCompleteDelegate.Broadcast();
	SetupCurrentLevel();
}

// ── Level Setup ───────────────────────────────────────────────────────────────

void AGameLevelManager::SetupCurrentLevel()
{
	if (!HasValidReferences()) return;

	const FLevelConfig Config = GetConfigForLevel(CurrentLevelIndex);

	UE_LOG(LogTemp, Log, TEXT("[LevelManager] SetupLevel %d | Runners=%d  Artifacts=%d  Seed=%d"),
		CurrentLevelIndex + 1, Config.NumRunners, Config.NumArtifacts, BaseSeed + Config.SeedOffset);

	// -- Push config to Orchestrator --
	Orchestrator->Seed = BaseSeed + Config.SeedOffset;
	Orchestrator->NumArtifactsToSpawn = Config.NumArtifacts;

	// -- Stamp wave index on player inventory --
	if (PlayerCharacter->StorageComponent)
		PlayerCharacter->StorageComponent->CurrentWaveIndex = CurrentLevelIndex;

	// -- Clear previous runners --
	for (AMazeRunner* Runner : Orchestrator->ActiveRunners)
	{
		if (IsValid(Runner)) Runner->Destroy();
	}
	Orchestrator->ActiveRunners.Empty();

	// -- Reset and re-spawn artifacts --
	if (Orchestrator->ArtifactManager)
	{
		Orchestrator->ArtifactManager->NumArtifacts              = Config.NumArtifacts;
		Orchestrator->ArtifactManager->bManagedExternally        = true;
		Orchestrator->ArtifactManager->ArtifactClass             = ArtifactClass;
		Orchestrator->ArtifactManager->IntroducedArtifactType    = Config.IntroducedArtifactType;
		Orchestrator->ArtifactManager->bAllowAllArtifactTypes    = Config.bAllowAllArtifactTypes;
		Orchestrator->ArtifactManager->ResetForNextLevel();
	}

	// -- Spawn fresh runners --
	Orchestrator->SpawnRunners(Config.NumRunners);

	// -- Spawn typed artifacts (the colored orbs with abilities) --
	if (Orchestrator->ArtifactManager)
		Orchestrator->ArtifactManager->SpawnArtifacts();

	// -- Wake AI: assign each runner a target from the freshly spawned artifacts --
	Orchestrator->OnRunnerReachedArtifact();

	// -- Bind loss condition (AddUniqueDynamic prevents duplicate binds across levels) --
	Orchestrator->OnArtifactStolen.AddUniqueDynamic(this, &AGameLevelManager::OnGameLost);

	// -- Start win-condition polling (0.5s interval) --
	GetWorldTimerManager().ClearTimer(WinCheckTimerHandle);
	GetWorldTimerManager().SetTimer(
		WinCheckTimerHandle,
		this,
		&AGameLevelManager::CheckWinCondition,
		0.5f,
		true);

	CurrentState = EGameLevelState::Running;
	UE_LOG(LogTemp, Log, TEXT("[LevelManager] Level %d is RUNNING"), CurrentLevelIndex + 1);
}

// ── Queries ───────────────────────────────────────────────────────────────────

int32 AGameLevelManager::GetRemainingArtifactCount() const
{
	if (!Orchestrator || !Orchestrator->ArtifactManager) return 0;
	return Orchestrator->ArtifactManager->GetRemainingArtifactCount();
}

int32 AGameLevelManager::GetRemainingBasicOrbCount() const
{
	if (!Orchestrator || !Orchestrator->ArtifactManager) return 0;
	return Orchestrator->ArtifactManager->GetRemainingBasicOrbCount();
}

int32 AGameLevelManager::GetRemainingRunnerCount() const
{
	if (!Orchestrator) return 0;
	int32 Count = 0;
	for (const AMazeRunner* R : Orchestrator->ActiveRunners)
		if (IsValid(R)) Count++;
	return Count;
}

// ── Win Condition ──────────────────────────────────────────────────────────────

void AGameLevelManager::CheckWinCondition()
{
	if (CurrentState != EGameLevelState::Running) return;

	if (!Orchestrator || !Orchestrator->ArtifactManager) return;

	const int32 RemainingArtifacts = Orchestrator->ArtifactManager->GetRemainingArtifactCount();
	const int32 TotalArtifacts     = Orchestrator->ArtifactManager->GetSpawnedArtifacts().FilterByPredicate(
		[](const AArtifact* A){ return IsValid(A) && A->ArtifactType != EArtifactType::Basic; }).Num();
	Orchestrator->ActiveRunners.RemoveAll([](AMazeRunner* R) { return !IsValid(R); });
	const int32 RemainingRunners = Orchestrator->ActiveRunners.Num();

	// Pure Basic levels have no spell artifacts — win when runners are gone.
	// Spell levels — win when all spell artifacts AND runners are gone.
	const bool bArtifactsDone = (TotalArtifacts == 0) ? true : (RemainingArtifacts == 0);

	if (bArtifactsDone && RemainingRunners == 0)
	{
		GetWorldTimerManager().ClearTimer(WinCheckTimerHandle);
		OnLevelWon();
	}
}

void AGameLevelManager::OnLevelWon()
{
	CurrentState = EGameLevelState::LevelWon;

	UE_LOG(LogTemp, Log, TEXT("[LevelManager] === LEVEL %d COMPLETE! ==="), CurrentLevelIndex + 1);
	if (LevelWonSound)
	{
		UGameplayStatics::PlaySound2D(this, LevelWonSound);
	}
	OnLevelWonDelegate.Broadcast(CurrentLevelIndex);

	GetWorldTimerManager().SetTimer(
		TransitionTimerHandle,
		this,
		&AGameLevelManager::TransitionToNextLevel,
		LevelTransitionDelay,
		false);
}

void AGameLevelManager::TransitionToNextLevel()
{
	// Advance past any disabled manual levels; procedural levels are always enabled
	do
	{
		CurrentLevelIndex++;
	}
	while (LevelConfigs.IsValidIndex(CurrentLevelIndex) && !LevelConfigs[CurrentLevelIndex].bEnabled);

	UE_LOG(LogTemp, Log, TEXT("[LevelManager] Transitioning to Level %d (Wave %d)..."),
		CurrentLevelIndex + 1, GetCurrentWaveNumber());
	StartCountdown();
}

// ── Procedural Config & Wave Queries ──────────────────────────────────────────

FLevelConfig AGameLevelManager::GetConfigForLevel(int32 LevelIndex) const
{
	if (LevelConfigs.IsValidIndex(LevelIndex))
		return LevelConfigs[LevelIndex];

	// Procedural generation for waves 6+ (any index beyond the manual array)
	const int32 ProceduralIndex = LevelIndex - LevelConfigs.Num(); // 0-based within procedural section
	const int32 WaveOffset  = ProceduralIndex / 3; // which procedural wave (0-based)
	const int32 LevelInWave = ProceduralIndex % 3; // 0, 1, or 2

	FLevelConfig Config;
	Config.bEnabled               = true;
	Config.bAllowAllArtifactTypes = true; // wave 6+: all types in round-robin
	Config.NumArtifacts           = ProceduralConfig.BaseArtifacts
	                                + WaveOffset  * ProceduralConfig.ArtifactsPerWaveGrowth
	                                + LevelInWave * ProceduralConfig.ArtifactsPerLevelGrowth;
	Config.NumRunners             = ProceduralConfig.BaseRunners
	                                + FMath::CeilToInt(WaveOffset * ProceduralConfig.RunnersPerWaveGrowth);
	Config.CountdownSeconds       = ProceduralConfig.CountdownSeconds;
	Config.SeedOffset             = LevelIndex * 7; // prime multiplier for varied seeds
	return Config;
}

int32 AGameLevelManager::GetCurrentWaveNumber() const
{
	return CurrentLevelIndex / 3 + 1;
}

int32 AGameLevelManager::GetCurrentLevelInWave() const
{
	return CurrentLevelIndex % 3 + 1;
}

// ── Loss Condition ────────────────────────────────────────────────────────────

void AGameLevelManager::OnGameLost()
{
	if (CurrentState != EGameLevelState::Running) return;

	CurrentState = EGameLevelState::GameOver;

	// Stop all timers — nothing should run while the lose screen is up
	GetWorldTimerManager().ClearTimer(WinCheckTimerHandle);
	GetWorldTimerManager().ClearTimer(TransitionTimerHandle);
	GetWorldTimerManager().ClearTimer(CountdownTimerHandle);

	UE_LOG(LogTemp, Log, TEXT("[LevelManager] === GAME LOST on Wave %d! Waiting for player to restart. ==="),
		GetCurrentWaveNumber());
	if (GameLostSound)
	{
		UGameplayStatics::PlaySound2D(this, GameLostSound);
	}

	// Freeze player input
	if (PlayerCharacter)
		PlayerCharacter->bInputFrozen = true;

	// Clear player inventory
	if (PlayerCharacter && PlayerCharacter->StorageComponent)
	{
		PlayerCharacter->StorageComponent->ClearAllSlots();
		PlayerCharacter->StorageComponent->CurrentWaveIndex = 0;
	}

	// Destroy all active runners
	for (AMazeRunner* Runner : Orchestrator->ActiveRunners)
	{
		if (IsValid(Runner)) Runner->Destroy();
	}
	Orchestrator->ActiveRunners.Empty();

	// Clear artifacts from the world
	if (Orchestrator->ArtifactManager)
		Orchestrator->ArtifactManager->ResetForNextLevel();

	// Broadcast — Blueprint shows WBP_Lose, waits for the Play Again button
	OnGameLostDelegate.Broadcast();
}

void AGameLevelManager::RestartGame()
{
	UE_LOG(LogTemp, Log, TEXT("[LevelManager] Restarting from Level 1..."));

	// Clear player inventory and reset upgrade tracking
	if (PlayerCharacter)
		PlayerCharacter->ResetForNewGame();

	// Reset to the first enabled level
	CurrentLevelIndex = 0;
	while (LevelConfigs.IsValidIndex(CurrentLevelIndex) && !LevelConfigs[CurrentLevelIndex].bEnabled)
		CurrentLevelIndex++;

	StartCountdown(); // unfreezes input once countdown completes
}
