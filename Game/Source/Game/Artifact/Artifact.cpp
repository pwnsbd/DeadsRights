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
        ActivateBarrier();
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
    if (!Carrier || !SphereActor)
        return;

    // FIX: Tell the character's logical movement system to ignore walls!
    if (AMyCharacterBase *Char = Cast<AMyCharacterBase>(Carrier))
    {
        Char->bIsPhasing = true;
    }

    // Spawn the ghost aura effect and attach it to the player!
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
    if (!Carrier || !SphereActor)
        return;

    // FIX: Turn the wall collisions back on!
    if (AMyCharacterBase *Char = Cast<AMyCharacterBase>(Carrier))
    {
        Char->bIsPhasing = false;
    }

    // Put out the visual effect
    if (ActivePhaseVFX)
    {
        ActivePhaseVFX->DestroyComponent();
        ActivePhaseVFX = nullptr;
    }

    // Note: Because your character moves strictly from grid center to grid center,
    // they can NEVER accidentally end their phase walk stuck halfway inside a wall!
    // They will just safely materialize inside whichever grid cell they are standing in.
}

// ============================================================
// Ability: Path Finder
// ============================================================
void AArtifact::ActivatePathFinder()
{
    if (!Carrier)
        return;

    // 1. Safely grab the global Maze Navigator from the Orchestrator!
    AOrchestrator *Orch = Cast<AOrchestrator>(UGameplayStatics::GetActorOfClass(GetWorld(), AOrchestrator::StaticClass()));
    if (!Orch || !Orch->Navigator)
        return;

    UMazeNavigator *ActiveNavigator = Orch->Navigator;

    FVector PlayerPos = Carrier->GetActorLocation();
    AArtifact *Closest = nullptr;
    float BestDist = FLT_MAX;

    // 2. Find the closest artifact that is NOT in someone's inventory
    for (TActorIterator<AArtifact> It(GetWorld()); It; ++It)
    {
        AArtifact *Art = *It;

        // Ignore this exact artifact, and ignore any artifact that has already been picked up!
        if (Art == this || Art->bIsCarried || Art->IsHidden())
            continue;

        float Dist = FVector::Dist(PlayerPos, Art->GetActorLocation());
        if (Dist < BestDist)
        {
            BestDist = Dist;
            Closest = Art;
        }
    }

    if (!Closest)
    {
        UE_LOG(LogTemp, Warning, TEXT("PathFinder: No available artifacts left to find!"));
        return;
    }

    // 3. Calculate the A* Path
    TArray<FVector> Path;
    if (ActiveNavigator->FindPath(PlayerPos, Closest->GetActorLocation(), Path))
    {
        // Draw a glowing yellow trail of breadcrumbs to the target
        for (int32 i = 0; i < Path.Num() - 1; i++)
        {
            DrawDebugLine(GetWorld(), Path[i], Path[i + 1], FColor::Yellow, false, PathDuration, 0, 15.f);
            DrawDebugSphere(GetWorld(), Path[i], 30.f, 12, FColor::Yellow, false, PathDuration);
        }
    }
}

// ============================================================
// Ability: Barrier
// ============================================================
void AArtifact::ActivateBarrier()
{
    FMazeNode PlayerNode = SphereActor->WorldToMazeCell(Carrier->GetActorLocation());

    for (int32 x = -BarrierRadius; x <= BarrierRadius; x++)
    {
        for (int32 y = -BarrierRadius; y <= BarrierRadius; y++)
        {
            if (FMath::Abs(x) != BarrierRadius && FMath::Abs(y) != BarrierRadius)
                continue;

            int32 Face = PlayerNode.Face;
            int32 NX = PlayerNode.X + x;
            int32 NY = PlayerNode.Y + y;

            if (NX < 0 || NX >= Maze->CellsPerFace || NY < 0 || NY >= Maze->CellsPerFace)
                continue;

            FVector Pos = SphereActor->GetCellCenterWorld(Face, NX, NY);
            AActor *Wall = GetWorld()->SpawnActor<AActor>(BarrierWallClass, Pos, FRotator::ZeroRotator);

            Wall->SetActorLocation(Pos);
            BarrierWalls.Add(Wall);
        }
    }

    FVector DebugCenter = SphereActor->GetCellCenterWorld(PlayerNode.Face, PlayerNode.X, PlayerNode.Y);
    DrawDebugBox(GetWorld(), DebugCenter, FVector(40), FColor::Red, false, BarrierDuration, 0, 5);

    GetWorldTimerManager().SetTimer(BarrierTimer, this, &AArtifact::DestroyBarrier, BarrierDuration, false);
}

void AArtifact::DestroyBarrier()
{
    for (AActor *Wall : BarrierWalls)
    {
        if (Wall)
            Wall->Destroy();
    }
    BarrierWalls.Empty();
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

    for (AActor *Wall : BarrierWalls)
    {
        if (IsValid(Wall))
            Wall->Destroy();
    }
    BarrierWalls.Empty();
}
