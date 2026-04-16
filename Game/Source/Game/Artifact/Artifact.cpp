#include "Artifact.h"
#include "../Maze/Maze.h"
#include "../Conversion/CubeToSphere.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "../AI/MazeNavigator.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "DrawDebugHelpers.h"
#include "../Movement/MyCharacterBase.h"
#include "../AI/MazeRunner.h"
#include "Particles/ParticleSystemComponent.h"
#include "../Orchestrator.h"

// ============================================================
// Initialization
// ============================================================
AArtifact::AArtifact()
{
    PrimaryActorTick.bCanEverTick = true;

    MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComponent"));
    RootComponent = MeshComponent;

    static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere"));
    if (SphereMesh.Succeeded())
    {
        MeshComponent->SetStaticMesh(SphereMesh.Object);
    }

    MeshComponent->SetWorldScale3D(FVector(SphereRadius / 50.f));
    MeshComponent->SetSimulatePhysics(false);
    MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);

    PickupTrigger = CreateDefaultSubobject<USphereComponent>(TEXT("PickupTrigger"));
    PickupTrigger->InitSphereRadius(35.f);
    PickupTrigger->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    PickupTrigger->SetCollisionResponseToAllChannels(ECR_Ignore);
    PickupTrigger->SetupAttachment(RootComponent);
    PickupTrigger->OnComponentBeginOverlap.AddDynamic(this, &AArtifact::OnOverlapBegin);
}

void AArtifact::BeginPlay()
{
    Super::BeginPlay();
    InitialLocation = GetActorLocation();
    CurrentCharges = MaxCharges;
}

// ============================================================
// Core State & Logic
// ============================================================
void AArtifact::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    if (!bIsCarried)
    {
        AccumulatedTime += DeltaTime;
        FRotator Rot = GetActorRotation();
        Rot.Yaw += RotationSpeed * DeltaTime;
        SetActorRotation(Rot);

        FVector FloatDir = FVector::UpVector;
        if (SphereActor)
        {
            FloatDir = (InitialLocation - SphereActor->GetActorLocation()).GetSafeNormal();
            if (FloatDir.IsNearlyZero())
            {
                FloatDir = FVector::UpVector;
            }
        }

        const FVector Loc = InitialLocation + FloatDir * (FMath::Sin(AccumulatedTime * FloatSpeed) * FloatAmplitude);
        SetActorLocation(Loc);
    }
}

void AArtifact::SpawnAtRandomCell()
{
    if (!Maze || !SphereActor)
        return;

    const int32 Face = FMath::RandRange(0, 5);
    const int32 X = FMath::RandRange(0, Maze->CellsPerFace - 1);
    const int32 Y = FMath::RandRange(0, Maze->CellsPerFace - 1);

    CurrentCell = FMazeNode(Face, X, Y);

    const FVector CellCenter = SphereActor->GetCellCenterWorld(Face, X, Y);
    const FVector SphereCenter = SphereActor->GetActorLocation();
    const FVector UpDir = (CellCenter - SphereCenter).GetSafeNormal();

    const FVector SpawnLoc = CellCenter + UpDir * IdleSurfaceOffset;
    const FRotator SpawnRot = FRotationMatrix::MakeFromZ(UpDir).Rotator();

    SetActorLocation(SpawnLoc);
    SetActorRotation(SpawnRot);
    InitialLocation = SpawnLoc;
}

void AArtifact::PickUp(AActor *NewCarrier)
{
    if (!NewCarrier || bIsCarried)
        return;

    if (AMyCharacterBase *Char = Cast<AMyCharacterBase>(NewCarrier))
    {
        if (!Char->AddArtifactToInventory(this))
            return;
    }

    bIsCarried = true;
    Carrier = NewCarrier;

    if (MeshComponent)
    {
        MeshComponent->SetSimulatePhysics(false);
        MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }

    AttachToActor(NewCarrier, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
}

void AArtifact::Drop(FVector DropLocation)
{
    bIsCarried = false;
    Carrier = nullptr;

    DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
    MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    MeshComponent->SetSimulatePhysics(true);
    SetActorLocation(DropLocation);
    InitialLocation = DropLocation;
}

void AArtifact::OnOverlapBegin(UPrimitiveComponent *OverlappedComp, AActor *OtherActor, UPrimitiveComponent *OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult &SweepResult)
{
    // Pickup is handled by AMazeArtifactManager using maze-cell alignment.
}

// ============================================================
// Ability Routing
// ============================================================
void AArtifact::ActivateAbility()
{
    if (!Carrier || !SphereActor || CurrentCharges <= 0)
        return;

    FMazeNode PlayerCell = SphereActor->WorldToMazeCell(Carrier->GetActorLocation());
    FVector Forward = Carrier->GetActorForwardVector();
    EMazeDir Dir = SphereActor->GetDirectionFromVector(Forward, PlayerCell);

    ActivateAbilityFromNode(PlayerCell, Dir);
    CurrentCharges--;

    if (CurrentCharges <= 0)
    {
        ArtifactType = EArtifactType::None;
        MeshComponent->SetMaterial(0, nullptr);
    }
}

void AArtifact::ActivateAbilityFromNode(const FMazeNode &StartNode, EMazeDir Direction)
{
    if (!Maze)
        return;

    switch (ArtifactType)
    {
    case EArtifactType::Beam:
        FireBeam(StartNode, Direction);
        break;
    case EArtifactType::PhaseWalk:
        ActivatePhaseWalk();
        break;
    case EArtifactType::PathFinder:
        ActivatePathFinder();
        break;
    case EArtifactType::Barrier:
        ActivateBarrier(StartNode, Direction);
        break;
    case EArtifactType::AoEBomb:
        ActivateAoEBomb();
        break;
    default:
        break;
    }
}

// ============================================================
// Ability: 3D Beam
// ============================================================
void AArtifact::ClearActiveBeam()
{
    GetWorldTimerManager().ClearTimer(BeamPropagationTimerHandle);
    GetWorldTimerManager().ClearTimer(BeamCleanupTimerHandle);

    for (UParticleSystemComponent *VFX : SpawnedBeamEffects)
    {
        if (IsValid(VFX))
        {
            VFX->DestroyComponent();
        }
    }
    SpawnedBeamEffects.Empty();
    ActiveBeamPoints.Empty();
}

void AArtifact::FireBeam(const FMazeNode &StartNode, EMazeDir Direction)
{
    if (!Carrier || !SphereActor)
        return;

    ClearActiveBeam();

    FVector SphereCenter = SphereActor->GetActorLocation();
    FVector StartPos = Carrier->GetActorLocation();

    FVector UpDir = (StartPos - SphereCenter).GetSafeNormal();
    FVector PlayerForward = Carrier->GetActorForwardVector().GetSafeNormal();
    FVector RightAxis = FVector::CrossProduct(UpDir, PlayerForward).GetSafeNormal();

    float PlanetRadius = FVector::Dist(SphereCenter, StartPos);
    float DistanceBetweenFirePillars = 150.0f;
    float DegreesPerStep = (DistanceBetweenFirePillars / PlanetRadius) * (180.0f / PI);

    CurrentBeamSpawnIndex = 0;
    CurrentCleanupIndex = 0;

    for (int32 i = 0; i < BeamDistance; i++)
    {
        FVector PointDir = UpDir.RotateAngleAxis(DegreesPerStep * i, RightAxis);
        FVector PointPos = SphereCenter + (PointDir * PlanetRadius);
        PointPos -= (PointDir * 50.0f);
        ActiveBeamPoints.Add(PointPos);
    }

    GetWorldTimerManager().SetTimer(BeamPropagationTimerHandle, this, &AArtifact::SpawnNextBeamSegment, BeamPropagationSpeed, true, 0.0f);
}

void AArtifact::SpawnNextBeamSegment()
{
    if (CurrentBeamSpawnIndex >= ActiveBeamPoints.Num() || !SphereActor)
    {
        GetWorldTimerManager().ClearTimer(BeamPropagationTimerHandle);
        return;
    }

    FVector PointPos = ActiveBeamPoints[CurrentBeamSpawnIndex];
    FVector SphereCenter = SphereActor->GetActorLocation();

    if (BeamNodeVFX)
    {
        FVector UpDir = (PointPos - SphereCenter).GetSafeNormal();
        FVector ForwardDir = FVector::ForwardVector;

        if (CurrentBeamSpawnIndex < ActiveBeamPoints.Num() - 1)
        {
            ForwardDir = (ActiveBeamPoints[CurrentBeamSpawnIndex + 1] - PointPos).GetSafeNormal();
        }
        else if (CurrentBeamSpawnIndex > 0)
        {
            ForwardDir = (PointPos - ActiveBeamPoints[CurrentBeamSpawnIndex - 1]).GetSafeNormal();
        }

        FRotator VFXRotation = FRotationMatrix::MakeFromZX(UpDir, ForwardDir).Rotator();
        UParticleSystemComponent *SpawnedVFX = UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), BeamNodeVFX, PointPos, VFXRotation, FVector(1.0f));

        if (SpawnedVFX)
            SpawnedBeamEffects.Add(SpawnedVFX);
    }

    for (TActorIterator<AMazeRunner> It(GetWorld()); It; ++It)
    {
        AMazeRunner *Runner = *It;
        if (IsValid(Runner) && FVector::Dist(Runner->GetActorLocation(), PointPos) < 150.0f)
        {
            Runner->Die();
        }
    }

    if (CurrentBeamSpawnIndex == 0)
    {
        GetWorldTimerManager().SetTimer(BeamCleanupTimerHandle, this, &AArtifact::CleanupNextBeamSegment, BeamPropagationSpeed, true, BeamDuration);
    }

    CurrentBeamSpawnIndex++;
}

void AArtifact::CleanupNextBeamSegment()
{
    if (CurrentCleanupIndex >= SpawnedBeamEffects.Num())
    {
        GetWorldTimerManager().ClearTimer(BeamCleanupTimerHandle);
        SpawnedBeamEffects.Empty();
        ActiveBeamPoints.Empty();
        return;
    }

    if (SpawnedBeamEffects[CurrentCleanupIndex])
    {
        SpawnedBeamEffects[CurrentCleanupIndex]->DestroyComponent();
    }

    CurrentCleanupIndex++;
}

// ============================================================
// Ability: Phase Walk
// ============================================================
void AArtifact::ActivatePhaseWalk()
{
    if (!Carrier)
        return;

    // 1. Tell the player's movement logic to ignore walls mathematically
    if (AMyCharacterBase *Char = Cast<AMyCharacterBase>(Carrier))
    {
        Char->bIsPhasing = true;
    }

    // 2. Spawn the visual ghost aura
    if (PhaseWalkVFX)
    {
        ActivePhaseVFX = UGameplayStatics::SpawnEmitterAttached(
            PhaseWalkVFX,
            Carrier->GetRootComponent(),
            NAME_None,
            FVector::ZeroVector,
            FRotator::ZeroRotator,
            EAttachLocation::SnapToTarget);
    }

    GetWorldTimerManager().SetTimer(PhaseTimer, this, &AArtifact::EndPhaseWalk, PhaseDuration, false);
}

void AArtifact::EndPhaseWalk()
{
    if (!Carrier)
        return;

    // 1. Tell the player's movement logic to respect walls again
    if (AMyCharacterBase *Char = Cast<AMyCharacterBase>(Carrier))
    {
        Char->bIsPhasing = false;
    }

    // 2. Turn off the ghost aura
    if (ActivePhaseVFX)
    {
        ActivePhaseVFX->DestroyComponent();
        ActivePhaseVFX = nullptr;
    }
}

// ============================================================
// Ability: Path Finder
// ============================================================
void AArtifact::ActivatePathFinder()
{
    if (!Carrier || !SphereActor)
        return;

    // --- NEW: GRAB LIVE PLAYER COORDINATES ---
    AMyCharacterBase *PlayerChar = Cast<AMyCharacterBase>(Carrier);
    if (!PlayerChar)
        return;

    AOrchestrator *Orch = Cast<AOrchestrator>(UGameplayStatics::GetActorOfClass(GetWorld(), AOrchestrator::StaticClass()));
    if (!Orch || !Orch->Navigator)
        return;

    // Calculate the perfect floor center using the Player's LIVE Face, X, and Y!
    FVector TrueStartPos = SphereActor->GetCellCenterWorld(PlayerChar->Face, PlayerChar->X, PlayerChar->Y);
    // -----------------------------------------

    AArtifact *Closest = nullptr;
    float BestDist = FLT_MAX;

    // Find the closest valid artifact on the map
    for (TActorIterator<AArtifact> It(GetWorld()); It; ++It)
    {
        AArtifact *Art = *It;
        if (Art == this || Art->bIsCarried || Art->IsHidden())
            continue;

        float Dist = FVector::Dist(TrueStartPos, Art->GetActorLocation());
        if (Dist < BestDist)
        {
            BestDist = Dist;
            Closest = Art;
        }
    }

    if (!Closest)
        return;

    CleanupPathFinder();

    // 3. Find the path using the exact floor coordinates
    if (Orch->Navigator->FindPath(TrueStartPos, Closest->GetActorLocation(), ActivePathPoints))
    {
        FVector SphereCenter = SphereActor->GetActorLocation();

        for (const FVector &PointPos : ActivePathPoints)
        {
            FVector UpDir = (PointPos - SphereCenter).GetSafeNormal();
            FRotator Rot = FRotationMatrix::MakeFromZ(UpDir).Rotator();

            // Slightly elevate the VFX so it hovers above the ground and doesn't clip into the floor!
            FVector ElevatedPos = PointPos + (UpDir * 20.f);

            if (PathFinderVFX)
            {
                UParticleSystemComponent *SpawnedVFX = UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), PathFinderVFX, ElevatedPos, Rot, FVector(1.0f));
                if (SpawnedVFX)
                {
                    SpawnedPathEffects.Add(SpawnedVFX);
                }
            }
            else
            {
                DrawDebugSphere(GetWorld(), ElevatedPos, 30.f, 12, FColor::Yellow, false, PathDuration);
            }
        }

        GetWorldTimerManager().SetTimer(PathCleanupTimer, this, &AArtifact::CleanupPathFinder, PathDuration, false);
    }
}

void AArtifact::CleanupPathFinder()
{
    // Instantly destroy all particles and empty the visual array
    for (UParticleSystemComponent *VFX : SpawnedPathEffects)
    {
        if (IsValid(VFX))
        {
            VFX->DestroyComponent();
        }
    }
    SpawnedPathEffects.Empty();
    ActivePathPoints.Empty();
}

// ============================================================
// Ability: Barrier
// ============================================================
void AArtifact::ActivateBarrier(const FMazeNode &StartNode, EMazeDir Direction)
{
    if (!Carrier || !SphereActor || !BarrierWallClass || !Maze)
        return;

    PendingBarrierSpawns.Empty();
    FMazeNode CurrentNode = StartNode;

    // Look ahead cell-by-cell
    for (int32 i = 0; i < BarrierLength; i++)
    {
        FMazeNode NextNode = Maze->GetNeighborCell(CurrentNode, Direction, true);

        // Stop if we hit the edge of a face or invalid grid
        if (!Maze->IsValid(NextNode.Face, NextNode.X, NextNode.Y))
            break;

        // Get the world centers of both cells
        FVector PosA = SphereActor->GetCellCenterWorld(CurrentNode.Face, CurrentNode.X, CurrentNode.Y);
        FVector PosB = SphereActor->GetCellCenterWorld(NextNode.Face, NextNode.X, NextNode.Y);

        // 1. Find the exact edge (seam) between the two cells
        FVector EdgeCenter = (PosA + PosB) * 0.5f;

        // 2. Orient the wall so it blocks the path
        FVector UpDir = (EdgeCenter - SphereActor->GetActorLocation()).GetSafeNormal(); // Keeps it flat on the planet
        FVector ForwardDir = (PosB - PosA).GetSafeNormal();                             // The direction of travel

        // The wall must stretch perpendicular (Left/Right) to the travel direction
        FVector RightDir = FVector::CrossProduct(UpDir, ForwardDir).GetSafeNormal();

        // Build a rotation where the Wall's length matches RightDir, and its top faces Up
        FRotator WallRot = FRotationMatrix::MakeFromZX(UpDir, RightDir).Rotator();

        // Save this perfect edge transform to our queue
        PendingBarrierSpawns.Add(FTransform(WallRot, EdgeCenter, FVector(1.0f)));

        CurrentNode = NextNode;
    }

    if (PendingBarrierSpawns.Num() > 0)
    {
        // Propagate forward like the beam! (0.05 seconds per step)
        GetWorldTimerManager().SetTimer(BarrierPropagationTimerHandle, this, &AArtifact::SpawnNextBarrierSegment, 0.05f, true, 0.0f);
    }
}

void AArtifact::SpawnNextBarrierSegment()
{
    // If the queue is empty, the wall is finished! Start the destruction countdown.
    if (PendingBarrierSpawns.Num() == 0)
    {
        GetWorldTimerManager().ClearTimer(BarrierPropagationTimerHandle);
        GetWorldTimerManager().SetTimer(BarrierTimer, this, &AArtifact::DestroyBarrier, BarrierDuration, false);
        return;
    }

    // Pop the first calculated edge off the queue
    FTransform SpawnTransform = PendingBarrierSpawns[0];
    PendingBarrierSpawns.RemoveAt(0);

    // Spawn the physical wall exactly on the seam
    AActor *Wall = GetWorld()->SpawnActor<AActor>(BarrierWallClass, SpawnTransform.GetLocation(), SpawnTransform.GetRotation().Rotator());
    if (Wall)
    {
        BarrierWalls.Add(Wall);
    }
}

void AArtifact::DestroyBarrier()
{
    // Loop through all the physical walls we spawned and destroy them
    for (AActor *Wall : BarrierWalls)
    {
        if (IsValid(Wall))
        {
            Wall->Destroy();
        }
    }

    // Clear the arrays so it is perfectly clean for the next time you cast the spell!
    BarrierWalls.Empty();
    PendingBarrierSpawns.Empty();
}

// ============================================================
// Ability: AoE Bomb
// ============================================================
void AArtifact::ActivateAoEBomb()
{
    if (!Carrier || !SphereActor)
        return;

    // Lock in the center of the explosion
    CurrentAoERadius = 0.f;
    AoECenterPos = Carrier->GetActorLocation();

    FVector SphereCenter = SphereActor->GetActorLocation();
    AoEUpDir = (AoECenterPos - SphereCenter).GetSafeNormal();
    AoEForwardAxis = Carrier->GetActorForwardVector();

    // Ensure the axes are perfectly flat against the planet
    FVector RightAxis = FVector::CrossProduct(AoEUpDir, AoEForwardAxis).GetSafeNormal();
    AoEForwardAxis = FVector::CrossProduct(RightAxis, AoEUpDir).GetSafeNormal();

    // Start the outward shockwave timer!
    GetWorldTimerManager().SetTimer(AoEExpansionTimer, this, &AArtifact::ExpandAoE, AoEPropagationSpeed, true, 0.0f);
}

void AArtifact::ExpandAoE()
{
    if (!SphereActor || CurrentAoERadius > AoEMaxRadius)
    {
        // We reached max size! Stop expanding and schedule the cleanup.
        GetWorldTimerManager().ClearTimer(AoEExpansionTimer);
        GetWorldTimerManager().SetTimer(AoECleanupTimer, this, &AArtifact::CleanupAoE, 2.0f, false);
        return;
    }

    FVector SphereCenter = SphereActor->GetActorLocation();

    // Calculate how many particles we need to form a solid ring at this specific radius
    float Circumference = 2.0f * PI * CurrentAoERadius;
    int32 NumParticles = FMath::Max(1, FMath::RoundToInt(Circumference / 100.0f));

    for (int32 i = 0; i < NumParticles; i++)
    {
        float Angle = (360.0f / NumParticles) * i;
        FVector Dir = AoEForwardAxis.RotateAngleAxis(Angle, AoEUpDir);
        FVector Pos = AoECenterPos + (Dir * CurrentAoERadius);

        // Snap the ring to the sphere surface
        FVector PosUp = (Pos - SphereCenter).GetSafeNormal();
        Pos = SphereCenter + (PosUp * FVector::Dist(SphereCenter, AoECenterPos));

        if (AoEVFX)
        {
            FRotator Rot = FRotationMatrix::MakeFromZ(PosUp).Rotator();
            UParticleSystemComponent *VFX = UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), AoEVFX, Pos, Rot, FVector(1.0f));
            if (VFX)
                SpawnedAoEEffects.Add(VFX);
        }
    }

    // --- DAMAGE CHECK ---
    // Kill any AI caught in the current shockwave ring
    for (TActorIterator<AMazeRunner> It(GetWorld()); It; ++It)
    {
        if (IsValid(*It))
        {
            float Dist = FVector::Dist((*It)->GetActorLocation(), AoECenterPos);
            // If they are within the new outer edge, and outside the old inner edge, they get hit!
            if (Dist <= (CurrentAoERadius + 50.f) && Dist > (CurrentAoERadius - AoEExpansionStep))
            {
                (*It)->Die();
            }
        }
    }

    // Expand the radius for the next tick
    CurrentAoERadius += AoEExpansionStep;
}

void AArtifact::CleanupAoE()
{
    for (UParticleSystemComponent *VFX : SpawnedAoEEffects)
    {
        if (IsValid(VFX))
            VFX->DestroyComponent();
    }
    SpawnedAoEEffects.Empty();
}

// ============================================================
// Editor / Debug
// ============================================================
void AArtifact::ApplyDebugVisuals()
{
    if (!MeshComponent)
        return;

    FLinearColor Color = FLinearColor::White;
    switch (ArtifactType)
    {
    case EArtifactType::Beam:
        Color = FLinearColor::Red;
        break;
    case EArtifactType::PhaseWalk:
        Color = FLinearColor::Green;
        break;
    case EArtifactType::PathFinder:
        Color = FLinearColor::Yellow;
        break;
    case EArtifactType::Barrier:
        Color = FLinearColor::Blue;
        break;
    case EArtifactType::AoEBomb:
        Color = FLinearColor(1.0f, 0.25f, 0.0f);
        break;
    default:
        break;
    }

    if (UMaterialInstanceDynamic *MID = MeshComponent->CreateAndSetMaterialInstanceDynamic(0))
    {
        MID->SetVectorParameterValue(TEXT("BaseColor"), Color);
        MID->SetVectorParameterValue(TEXT("Color"), Color);
    }
}

// ============================================================
// Cleanup
// ============================================================
void AArtifact::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    Super::EndPlay(EndPlayReason);

    ClearActiveBeam();
    EndPhaseWalk();

    for (AActor *Wall : BarrierWalls)
    {
        if (IsValid(Wall))
            Wall->Destroy();
    }
    BarrierWalls.Empty();
}
