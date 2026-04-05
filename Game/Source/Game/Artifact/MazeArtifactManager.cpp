#include "MazeArtifactManager.h"
#include "Artifact.h"

#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

#include "../Maze/Maze.h"
#include "../Conversion/CubeToSphere.h"
#include "../Orchestrator.h"
#include "../AI/MazeRunner.h"

namespace
{
    bool IsWithinCellRadius(const FMazeNode& A, const FMazeNode& B, int32 Radius)
    {
        if (A.Face < 0 || B.Face < 0)
        {
            return false;
        }

        if (A.Face != B.Face)
        {
            return false;
        }

        const int32 DX = FMath::Abs(A.X - B.X);
        const int32 DY = FMath::Abs(A.Y - B.Y);
        return DX <= Radius && DY <= Radius;
    }
}

AMazeArtifactManager::AMazeArtifactManager()
{
    PrimaryActorTick.bCanEverTick = true;
}

void AMazeArtifactManager::BeginPlay()
{
    Super::BeginPlay();

    ResolveReferences();
    SpawnArtifacts();
}

void AMazeArtifactManager::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    ResolveReferences();

    if (!SphereActor || !PlayerPawn)
    {
        return;
    }

    const FMazeNode PlayerNode = SphereActor->WorldToMazeCell(PlayerPawn->GetActorLocation());

    for (AArtifact* Artifact : SpawnedArtifacts)
    {
        if (!IsValid(Artifact))
        {
            continue;
        }

        if (Artifact->bIsCarried)
        {
            continue;
        }

        if (Artifact->CurrentCell == PlayerNode)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("Artifact pickup by cell match  Face=%d X=%d Y=%d"),
                PlayerNode.Face, PlayerNode.X, PlayerNode.Y);

            Artifact->PickUp(PlayerPawn);
            break;
        }
    }
}

void AMazeArtifactManager::ResolveReferences()
{
    if ((!Maze || !SphereActor) && GetWorld())
    {
        AOrchestrator* Orch = Cast<AOrchestrator>(
            UGameplayStatics::GetActorOfClass(GetWorld(), AOrchestrator::StaticClass()));

        if (Orch)
        {
            if (!Maze)
            {
                Maze = Orch->GetMaze();
            }

            if (!SphereActor)
            {
                SphereActor = Orch->SphereActor;
            }
        }
    }

    if (!PlayerPawn && GetWorld())
    {
        PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
    }

    if (!AIPawn && GetWorld())
    {
        AIPawn = Cast<AActor>(
            UGameplayStatics::GetActorOfClass(GetWorld(), AMazeRunner::StaticClass()));
    }
}

bool AMazeArtifactManager::IsCellUsed(const FMazeNode& Cell) const
{
    for (const FMazeNode& Used : UsedCells)
    {
        if (Used == Cell)
        {
            return true;
        }
    }

    return false;
}

void AMazeArtifactManager::ClearArtifacts()
{
    for (AArtifact* Artifact : SpawnedArtifacts)
    {
        if (IsValid(Artifact))
        {
            Artifact->Destroy();
        }
    }

    SpawnedArtifacts.Empty();
    UsedCells.Empty();
}

void AMazeArtifactManager::SpawnArtifacts()
{
    ClearArtifacts();
    ResolveReferences();

    if (!ArtifactClass || !Maze || !SphereActor)
    {
        UE_LOG(LogTemp, Warning, TEXT("SpawnArtifacts failed  missing ArtifactClass, Maze, or SphereActor"));
        return;
    }

    const FMazeNode PlayerNode = PlayerPawn
        ? SphereActor->WorldToMazeCell(PlayerPawn->GetActorLocation())
        : FMazeNode(-1, -1, -1);

    const FMazeNode AINode = AIPawn
        ? SphereActor->WorldToMazeCell(AIPawn->GetActorLocation())
        : FMazeNode(-1, -1, -1);

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    for (int32 i = 0; i < NumArtifacts; ++i)
    {
        FMazeNode SpawnCell(-1, -1, -1);
        bool bFoundValidCell = false;

        for (int32 Attempt = 0; Attempt < 200; ++Attempt)
        {
            const int32 Face = FMath::RandRange(0, 5);
            const int32 X = FMath::RandRange(0, Maze->CellsPerFace - 1);
            const int32 Y = FMath::RandRange(0, Maze->CellsPerFace - 1);

            const FMazeNode Candidate(Face, X, Y);

            if (IsCellUsed(Candidate))
            {
                continue;
            }

            if (IsWithinCellRadius(Candidate, PlayerNode, SpawnSafetyRadius))
            {
                continue;
            }

            if (IsWithinCellRadius(Candidate, AINode, SpawnSafetyRadius))
            {
                continue;
            }

            SpawnCell = Candidate;
            bFoundValidCell = true;
            break;
        }

        if (!bFoundValidCell)
        {
            continue;
        }

        const FVector CellCenter = SphereActor->GetCellCenterWorld(
            SpawnCell.Face,
            SpawnCell.X,
            SpawnCell.Y);

        const FVector SphereCenter = SphereActor->GetActorLocation();
        const FVector UpDir = (CellCenter - SphereCenter).GetSafeNormal();
        const FVector SpawnLocation = CellCenter + UpDir * 35.f;
        const FRotator SpawnRotation = FRotationMatrix::MakeFromZ(UpDir).Rotator();

        AArtifact* NewArtifact = GetWorld()->SpawnActor<AArtifact>(
            ArtifactClass,
            SpawnLocation,
            SpawnRotation,
            SpawnParams);

        if (!NewArtifact)
        {
            continue;
        }

        NewArtifact->Maze = Maze;
        NewArtifact->SphereActor = SphereActor;
        NewArtifact->AIPawn = AIPawn;
        NewArtifact->CurrentCell = SpawnCell;

        switch (i)
        {
        case 0: NewArtifact->ArtifactType = EArtifactType::Beam; break;
        case 1: NewArtifact->ArtifactType = EArtifactType::PhaseWalk; break;
        case 2: NewArtifact->ArtifactType = EArtifactType::PathFinder; break;
        case 3: NewArtifact->ArtifactType = EArtifactType::Barrier; break;
        default: NewArtifact->ArtifactType = EArtifactType::Beam; break;
        }

        SpawnedArtifacts.Add(NewArtifact);
        UsedCells.Add(SpawnCell);

        UE_LOG(LogTemp, Warning,
            TEXT("Spawned Artifact  Face=%d X=%d Y=%d"),
            SpawnCell.Face, SpawnCell.X, SpawnCell.Y);
    }
}