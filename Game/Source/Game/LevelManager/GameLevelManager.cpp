#include "../LevelManager/GameLevelManager.h"
#include "../Orchestrator.h"
#include "../Movement/MyCharacterBase.h"
#include "../Artifact/MazeArtifactManager.h"
#include "../AI/MazeRunner.h"
#include "../Inventory/ItemStorageComponent.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"

AGameLevelManager::AGameLevelManager()
{
	PrimaryActorTick.bCanEverTick = false;
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

	// Unfreeze player
	if (PlayerCharacter)
		PlayerCharacter->bInputFrozen = false;

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
	Orchestrator->ActiveRunners.RemoveAll([](AMazeRunner* R) { return !IsValid(R); });
	const int32 RemainingRunners = Orchestrator->ActiveRunners.Num();

	if (RemainingArtifacts == 0 && RemainingRunners == 0)
	{
		GetWorldTimerManager().ClearTimer(WinCheckTimerHandle);
		OnLevelWon();
	}
}

void AGameLevelManager::OnLevelWon()
{
	CurrentState = EGameLevelState::LevelWon;

	UE_LOG(LogTemp, Log, TEXT("[LevelManager] === LEVEL %d COMPLETE! ==="), CurrentLevelIndex + 1);
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

void AGameLevelManager::OnLostRestartCountdown()
{
	UE_LOG(LogTemp, Log, TEXT("[LevelManager] Restarting from Level 1..."));
	StartCountdown();
}

void AGameLevelManager::OnGameLost()
{
	if (CurrentState != EGameLevelState::Running) return;

	CurrentState = EGameLevelState::GameOver;
	GetWorldTimerManager().ClearTimer(WinCheckTimerHandle);
	UE_LOG(LogTemp, Log, TEXT("[LevelManager] === GAME LOST! AI stole an artifact. Resetting... ==="));

	// Clear player inventory
	if (PlayerCharacter && PlayerCharacter->StorageComponent)
	{
		PlayerCharacter->StorageComponent->ClearAllSlots();
		PlayerCharacter->StorageComponent->CurrentWaveIndex = 0;
	}

	// Clear all active runners and artifacts
	for (AMazeRunner* Runner : Orchestrator->ActiveRunners)
	{
		if (IsValid(Runner)) Runner->Destroy();
	}
	Orchestrator->ActiveRunners.Empty();

	if (Orchestrator->ArtifactManager)
		Orchestrator->ArtifactManager->ResetForNextLevel();

	// Broadcast so Blueprint can show a loss screen / play audio
	OnGameLostDelegate.Broadcast();

	// Restart from first enabled level after a short pause
	CurrentLevelIndex = 0;
	while (CurrentLevelIndex < LevelConfigs.Num() && !LevelConfigs[CurrentLevelIndex].bEnabled)
		CurrentLevelIndex++;
	GetWorldTimerManager().SetTimer(
		TransitionTimerHandle,
		this,
		&AGameLevelManager::OnLostRestartCountdown,
		2.0f,
		false);
}
