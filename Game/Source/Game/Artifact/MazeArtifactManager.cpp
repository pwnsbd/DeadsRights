#include "MazeArtifactManager.h"
#include "Artifact.h"
#include "Engine/World.h"
#include "Maze/Maze.h"
#include "Conversion/CubeToSphere.h"

bool IsWithinCellRadius(const FMazeNode& A, const FMazeNode& B, int32 Radius)
{
    int32 DX = FMath::Abs(A.X - B.X);
    int32 DY = FMath::Abs(A.Y - B.Y);

    if (A.Face != B.Face)
        return false; // keep simple for now

    return (DX <= Radius && DY <= Radius);
}

void AMazeArtifactManager::BeginPlay()
{
    Super::BeginPlay();

    if (!ArtifactClass || !Maze || !SphereActor)
        return;

    UsedCells.Empty();
    Navigator = NewObject<UMazeNavigator>(this);

    // Spawn artifacts at random valid cells
    for (int32 i = 0; i < NumArtifacts; i++)
    {
        FMazeNode SpawnCell;

        bool bFoundValidCell = false;
        int32 MaxAttempts = 100;

        // Try to find a valid cell for this artifact
        for (int32 Attempt = 0; Attempt < MaxAttempts; Attempt++)
        {
            int32 Face = FMath::RandRange(0, 5);
            int32 X = FMath::RandRange(0, Maze->CellsPerFace - 1);
            int32 Y = FMath::RandRange(0, Maze->CellsPerFace - 1);

            FMazeNode Candidate(Face, X, Y);

            // Check if this cell is already used
            FMazeNode PlayerNode = SphereActor->WorldToMazeCell(PlayerPawn->GetActorLocation());
            FMazeNode AINode = SphereActor->WorldToMazeCell(AIPawn->GetActorLocation());

            if (IsWithinCellRadius(Candidate, PlayerNode, SpawnSafetyRadius))
                continue;

            if (IsWithinCellRadius(Candidate, AINode, SpawnSafetyRadius))
                continue;

            SpawnCell = Candidate;
            bFoundValidCell = true;
            break;
        }

        if (!bFoundValidCell)
            continue;
        
        // Mark this cell as used for future iterations
        UsedCells.Add(SpawnCell);

        // Spawn the artifact actor
        AArtifact* NewArtifact = GetWorld()->SpawnActor<AArtifact>(ArtifactClass);

        if (!NewArtifact)
            continue;

        NewArtifact->Maze = Maze;
        NewArtifact->SphereActor = SphereActor;
        NewArtifact->CurrentCell = SpawnCell;

        FVector SpawnLocation = SphereActor->GetCellCenterWorld(
            SpawnCell.Face,
            SpawnCell.X,
            SpawnCell.Y);

        // Set the artifact's location to the center of the assigned cell
        NewArtifact->SetActorLocation(SpawnLocation);

        // Assign magic artifacts first
        switch (i)
        {
        case 0: NewArtifact->ArtifactType = EArtifactType::Beam; break;
        case 1: NewArtifact->ArtifactType = EArtifactType::PhaseWalk; break;
        case 2: NewArtifact->ArtifactType = EArtifactType::PathFinder; break;
        case 3: NewArtifact->ArtifactType = EArtifactType::Barrier; break;
        default: NewArtifact->ArtifactType = EArtifactType::Beam; break;
        }
    }
}