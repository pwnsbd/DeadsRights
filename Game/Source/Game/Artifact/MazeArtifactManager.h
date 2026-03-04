#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
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
    int32 NumArtifacts = 3;

    UPROPERTY(EditAnywhere, Category="Artifacts")
    UMaze* Maze;

    UPROPERTY(EditAnywhere, Category="Artifacts")
    ACubeToSphere* SphereActor;

    UPROPERTY(EditAnywhere, Category="Artifacts")
    FMazeNode PlayerStartCell;

private:
  TArray<FMazeNode> UsedCells; // To track which cells have already been used for artifact spawning
};