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
    bool IsWithinCellRadius(const FMazeNode &A, const FMazeNode &B, int32 Radius)
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
}

void AMazeArtifactManager::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    ResolveReferences();

    if (!bHasSpawnedArtifacts)
    {
        // LevelManager owns spawning when bManagedExternally — skip auto-spawn
        if (bManagedExternally)
            return;

        if (ArtifactClass && Maze && SphereActor)
        {
            SpawnArtifacts();
        }

        return;
    }

    if (!PlayerPawn)
    {
        PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
    }

    if (!SphereActor || !PlayerPawn)
    {
        return;
    }

    const FMazeNode PlayerNode = SphereActor->WorldToMazeCell(PlayerPawn->GetActorLocation());

    for (AArtifact *Artifact : SpawnedArtifacts)
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
        AOrchestrator *Orch = *It;

        if (!Orch)
            continue;

        // Pull Maze
        if (!Maze && Orch->GetMaze())
            Maze = Orch->GetMaze();

        if (!SphereActor && Orch->SphereActor)
            SphereActor = Orch->SphereActor;

        break;
    }

    // Fallback: find sphere directly in world
    if (!SphereActor)
    {
        for (TActorIterator<ACubeToSphere> It(GetWorld()); It; ++It)
        {
            SphereActor = *It;
            break;
        }
    }
}

bool AMazeArtifactManager::IsCellUsed(const FMazeNode &Cell) const
{
    for (const FMazeNode &Used : UsedCells)
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
    // We must loop backwards through the array when we are removing items from it
    for (int32 i = SpawnedArtifacts.Num() - 1; i >= 0; --i)
    {
        AArtifact *Artifact = SpawnedArtifacts[i];

        if (IsValid(Artifact))
        {
            // FIX: Only destroy the artifact if it is NOT in the player's inventory!
            if (!Artifact->bIsCarried)
            {
                Artifact->Destroy();
                SpawnedArtifacts.RemoveAt(i);
            }
        }
        else
        {
            // Clean up any empty slots just in case
            SpawnedArtifacts.RemoveAt(i);
        }
    }

    UsedCells.Empty();
}

void AMazeArtifactManager::ResetForNextLevel()
{
    ClearArtifacts();
    bHasSpawnedArtifacts = false;
    UE_LOG(LogTemp, Log, TEXT("[ArtifactManager] Reset for next level."));
}

int32 AMazeArtifactManager::GetRemainingArtifactCount() const
{
    int32 Count = 0;
    for (const AArtifact *Artifact : SpawnedArtifacts)
    {
        if (IsValid(Artifact) && !Artifact->bIsCarried)
            Count++;
    }
    return Count;
}

void AMazeArtifactManager::SpawnArtifacts()
{

    ClearArtifacts();
    ResolveReferences();

    const bool bUsingClassList = ArtifactClasses.Num() > 0;
    if (!bUsingClassList && !ArtifactClass)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ArtifactManager] SpawnArtifacts: no ArtifactClass or ArtifactClasses set"));
        return;
    }
    if (!Maze || !SphereActor)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ArtifactManager] SpawnArtifacts: missing refs (Maze=%s SphereActor=%s)"),
               Maze ? TEXT("OK") : TEXT("NULL"),
               SphereActor ? TEXT("OK") : TEXT("NULL"));
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

        TSubclassOf<AArtifact> ClassToSpawn = bUsingClassList
            ? ArtifactClasses[i % ArtifactClasses.Num()]
            : ArtifactClass;

        AArtifact *NewArtifact = GetWorld()->SpawnActor<AArtifact>(
            ClassToSpawn,
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

        if (!bUsingClassList)
        {
            EArtifactType TypeToAssign = EArtifactType::None;
            if (bAllowAllArtifactTypes)
            {
                switch (i % 5)
                {
                case 0: TypeToAssign = EArtifactType::Beam;      break;
                case 1: TypeToAssign = EArtifactType::PhaseWalk;  break;
                case 2: TypeToAssign = EArtifactType::PathFinder; break;
                default: TypeToAssign = EArtifactType::AoEBomb;  break;
                }
            }
            else if (IntroducedArtifactType != EArtifactType::None && i == 0)
            {
                TypeToAssign = IntroducedArtifactType;
            }
            NewArtifact->ArtifactType = TypeToAssign;
            NewArtifact->UpdateMeshForType();
        }

        NewArtifact->ApplyDebugVisuals();

        SpawnedArtifacts.Add(NewArtifact);
        UsedCells.Add(SpawnCell);

        UE_LOG(LogTemp, Warning,
               TEXT("Spawned Artifact  Face=%d X=%d Y=%d"),
               SpawnCell.Face, SpawnCell.X, SpawnCell.Y);
    }

    // Mark as spawned so Tick proceeds to pickup detection regardless of who called us
    bHasSpawnedArtifacts = true;
    UE_LOG(LogTemp, Log, TEXT("[ArtifactManager] Spawned %d artifacts."), SpawnedArtifacts.Num());
}