#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "../Maze/Maze.h"
#include "../Conversion/CubeToSphere.h"
#include "MazeArtifactManager.generated.h"

class AArtifact;
class UMaze;
class ACubeToSphere;

UCLASS()
class GAME_API AMazeArtifactManager : public AActor
{
    GENERATED_BODY()

public:
    virtual void BeginPlay() override;

    // Configurable properties
    UPROPERTY(EditAnywhere, Category="Artifacts")
    TSubclassOf<AArtifact> ArtifactClass;

    // Number of artifacts to spawn in the maze
    UPROPERTY(EditAnywhere, Category="Artifacts")
    int32 NumArtifacts = 7;

    UPROPERTY(EditAnywhere, Category="Artifacts")
    UMaze* Maze;

    UPROPERTY(EditAnywhere, Category="Artifacts")
    ACubeToSphere* SphereActor;

    UPROPERTY(EditAnywhere, Category="Artifacts")
    FMazeNode PlayerStartCell;

    UPROPERTY(EditAnywhere, Category="Artifacts")
    AActor* PlayerPawn;

    UPROPERTY(EditAnywhere, Category="Artifacts")
    AActor* AIPawn;

    // Minimum distance (in cells) from the player or AI starting position to any spawned artifact
    UPROPERTY(EditAnywhere, Category="Artifacts")
    int32 SpawnSafetyRadius = 4;

private:
  TArray<FMazeNode> UsedCells; // To track which cells have already been used for artifact spawning
};