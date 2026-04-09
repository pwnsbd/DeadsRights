#include "MazeArtifactManager.h"
#include "Artifact.h"

#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

#include "../Maze/Maze.h"
#include "../Conversion/CubeToSphere.h"
#include "../Orchestrator.h"
#include "../AI/MazeRunner.h"

#include "EngineUtils.h"

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

    UE_LOG(LogTemp, Warning, TEXT("ArtifactManager BeginPlay HIT"));
}

void AMazeArtifactManager::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    ResolveReferences();

    if (!bHasSpawnedArtifacts)
    {
        if (ArtifactClass && Maze && SphereActor)
        {
            UE_LOG(LogTemp, Warning, TEXT("All refs valid, spawning artifacts now"));
            SpawnArtifacts();
            bHasSpawnedArtifacts = true;
        }
        else
        {
            UE_LOG(LogTemp, Warning,
                TEXT("Waiting for refs  ArtifactClass=%s Maze=%s SphereActor=%s PlayerPawn=%s"),
                ArtifactClass ? TEXT("VALID") : TEXT("NULL"),
                Maze ? TEXT("VALID") : TEXT("NULL"),
                SphereActor ? TEXT("VALID") : TEXT("NULL"),
                PlayerPawn ? TEXT("VALID") : TEXT("NULL"));
        }

        return;
    }

    if (!PlayerPawn)
    {
        PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);

        if (PlayerPawn)
        {
            UE_LOG(LogTemp, Warning, TEXT("Resolved PlayerPawn"));
        }
    }

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
    // Find Orchestrator
    for (TActorIterator<AOrchestrator> It(GetWorld()); It; ++It)
    {
        AOrchestrator* Orch = *It;

        if (!Orch) continue;

        // Pull Maze
        if (!Maze && Orch->GetMaze())
        {
            Maze = Orch->GetMaze();
            UE_LOG(LogTemp, Warning, TEXT("Resolved Maze from Orchestrator"));
        }

        // Pull Sphere  
        if (!SphereActor && Orch->SphereActor)
        {
            SphereActor = Orch->SphereActor;
            UE_LOG(LogTemp, Warning, TEXT("Resolved SphereActor from Orchestrator"));
        }

        break;
    }

    // Fallback: find sphere directly in world
    if (!SphereActor)
    {
        for (TActorIterator<ACubeToSphere> It(GetWorld()); It; ++It)
        {
            SphereActor = *It;
            UE_LOG(LogTemp, Warning, TEXT("Found SphereActor directly in world"));
            break;
        }
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

    UE_LOG(LogTemp, Warning, TEXT("SpawnArtifacts START"));
    UE_LOG(LogTemp, Warning, TEXT("ArtifactClass=%s"), ArtifactClass ? TEXT("VALID") : TEXT("NULL"));
    UE_LOG(LogTemp, Warning, TEXT("Maze=%s"), Maze ? TEXT("VALID") : TEXT("NULL"));
    UE_LOG(LogTemp, Warning, TEXT("SphereActor=%s"), SphereActor ? TEXT("VALID") : TEXT("NULL"));
    UE_LOG(LogTemp, Warning, TEXT("PlayerPawn=%s"), PlayerPawn ? TEXT("VALID") : TEXT("NULL"));

    ClearArtifacts();
    ResolveReferences();

    if (!ArtifactClass || !Maze || !SphereActor)
    {
        UE_LOG(LogTemp, Warning, TEXT("SpawnArtifacts EARLY RETURN"));
        UE_LOG(LogTemp, Warning, TEXT("ArtifactClass=%s"), ArtifactClass ? TEXT("VALID") : TEXT("NULL"));
        UE_LOG(LogTemp, Warning, TEXT("Maze=%s"), Maze ? TEXT("VALID") : TEXT("NULL"));
        UE_LOG(LogTemp, Warning, TEXT("SphereActor=%s"), SphereActor ? TEXT("VALID") : TEXT("NULL"));
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

        switch (i % 4)
        {
        case 0: NewArtifact->ArtifactType = EArtifactType::Beam; break;
        case 1: NewArtifact->ArtifactType = EArtifactType::PhaseWalk; break;
        case 2: NewArtifact->ArtifactType = EArtifactType::PathFinder; break;
        case 3: NewArtifact->ArtifactType = EArtifactType::Barrier; break;
        default: NewArtifact->ArtifactType = EArtifactType::Beam; break;
        }

        NewArtifact->ApplyDebugVisuals();

        SpawnedArtifacts.Add(NewArtifact);
        UsedCells.Add(SpawnCell);

        UE_LOG(LogTemp, Warning,
            TEXT("Spawned Artifact  Face=%d X=%d Y=%d"),
            SpawnCell.Face, SpawnCell.X, SpawnCell.Y);
    }
}