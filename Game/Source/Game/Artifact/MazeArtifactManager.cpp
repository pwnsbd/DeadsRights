#include "MazeArtifactManager.h"
#include "Artifact.h"
#include "Engine/World.h"

void AMazeArtifactManager::BeginPlay()
{
    Super::BeginPlay();

    if (!ArtifactClass || !Maze || !SphereActor)
        return;

    UsedCells.Empty();

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

            // Reject if same as player start
            if (Candidate == PlayerStartCell)
                continue;

            // Reject if already used
            if (UsedCells.Contains(Candidate))
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

        // First artifact is Beam
        if (i == 0)
        {
            NewArtifact->ArtifactType = EArtifactType::Beam;
        }
    }
}