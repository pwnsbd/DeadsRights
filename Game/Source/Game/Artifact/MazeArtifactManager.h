#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "../Maze/Maze.h"
#include "../Conversion/CubeToSphere.h"
#include "../Artifact/Artifact.h"
#include "MazeArtifactManager.generated.h"

class UMaze;
class ACubeToSphere;

UCLASS()
class GAME_API AMazeArtifactManager : public AActor
{
    GENERATED_BODY()

public:
    AMazeArtifactManager();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

    UFUNCTION(BlueprintCallable, Category="Artifacts")
    void SpawnArtifacts();

    UFUNCTION(BlueprintCallable, Category="Artifacts")
    void ClearArtifacts();

    /** Clears all artifacts and resets internal state so SpawnArtifacts() can be called again. */
    UFUNCTION(BlueprintCallable, Category="Artifacts")
    void ResetForNextLevel();

    /** Returns how many artifact actors are still alive in the world (not yet collected or escaped). */
    UFUNCTION(BlueprintPure, Category="Artifacts")
    int32 GetRemainingArtifactCount() const;

    /** When true, Tick will NOT auto-spawn artifacts — GameLevelManager owns spawning.
     *  Defaults to true so placed instances never auto-spawn; LevelManager sets this
     *  explicitly before calling SpawnArtifacts(). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Artifacts")
    bool bManagedExternally = true;

    /** Read-only access to spawned artifacts — used by Orchestrator for AI targeting. */
    const TArray<TObjectPtr<AArtifact>>& GetSpawnedArtifacts() const { return SpawnedArtifacts; }

    UPROPERTY(EditAnywhere, Category="Artifacts")
    TSubclassOf<AArtifact> ArtifactClass;

    UPROPERTY(EditAnywhere, Category="Artifacts")
    int32 NumArtifacts = 7;

    UPROPERTY(EditAnywhere, Category="Artifacts")
    UMaze* Maze = nullptr;

    UPROPERTY(EditAnywhere, Category="Artifacts")
    ACubeToSphere* SphereActor = nullptr;

    UPROPERTY(EditAnywhere, Category="Artifacts")
    FMazeNode PlayerStartCell;

    UPROPERTY(EditAnywhere, Category="Artifacts")
    AActor* PlayerPawn = nullptr;

    UPROPERTY(EditAnywhere, Category="Artifacts")
    AActor* AIPawn = nullptr;

    UPROPERTY(EditAnywhere, Category="Artifacts")
    int32 SpawnSafetyRadius = 4;

    /** Set by GameLevelManager before SpawnArtifacts(). The powered type placed in the first slot.
     *  EArtifactType::None means all artifacts this level are basic (no ability).
     *  Ignored when bAllowAllArtifactTypes is true. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Artifacts")
    EArtifactType IntroducedArtifactType = EArtifactType::None;

    /** When true, all four artifact types spawn in round-robin order (Wave 5+ behaviour). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Artifacts")
    bool bAllowAllArtifactTypes = false;

private:
    void ResolveReferences();
    bool IsCellUsed(const FMazeNode& Cell) const;
    bool bHasSpawnedArtifacts = false;

private:
    TArray<FMazeNode> UsedCells;

    UPROPERTY()
    TArray<TObjectPtr<AArtifact>> SpawnedArtifacts;
};