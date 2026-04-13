#include "Artifact.h"
#include "../Maze/Maze.h"
#include "../Conversion/CubeToSphere.h"

#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "../AI/MazeNavigator.h"
// #include "EnemyPawn.h"

#include "Components/StaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "DrawDebugHelpers.h"
#include "../Movement/MyCharacterBase.h"

#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "../AI/MazeRunner.h"
#include "Particles/ParticleSystemComponent.h"

// Sets default values
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

// Called when the game starts or when spawned
void AArtifact::BeginPlay()
{
    Super::BeginPlay();
    InitialLocation = GetActorLocation();
    CurrentCharges = MaxCharges;
}

// Called every frame, handles idle animation when not carried
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

        const FVector Loc =
            InitialLocation +
            FloatDir * (FMath::Sin(AccumulatedTime * FloatSpeed) * FloatAmplitude);

        SetActorLocation(Loc);
    }
}

// Spawns the artifact at a random cell on the sphere
void AArtifact::SpawnAtRandomCell()
{
    if (!Maze || !SphereActor)
    {
        return;
    }

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

// Handles pickup logic, attaching the artifact to the carrier
void AArtifact::PickUp(AActor *NewCarrier)
{
    if (!NewCarrier || bIsCarried)
    {
        return;
    }
    UE_LOG(LogTemp, Warning, TEXT("PickUp called with carrier class = %s"), *GetNameSafe(NewCarrier->GetClass()));

    UE_LOG(LogTemp, Warning, TEXT("Trying GridMazePawn cast"));

    if (AMyCharacterBase *Char = Cast<AMyCharacterBase>(NewCarrier))
    {
        if (!Char->AddArtifactToInventory(this))
        {
            UE_LOG(LogTemp, Warning, TEXT("Artifact pickup failed because inventory is full"));
            return;
        }

        UE_LOG(LogTemp, Warning, TEXT("Artifact routed into GridMazePawn inventory"));
        return;
    }

    else
    {
        UE_LOG(LogTemp, Warning, TEXT("Carrier is NOT AGridMazePawn, using fallback attach path"));
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

// Handles dropping the artifact, re-enabling physics and collision
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

// Activates the artifact's ability based on the player's current cell and facing direction
void AArtifact::ActivateAbility()
{
    if (!Carrier || !SphereActor)
        return;

    // Check if we have charges left before activating
    if (CurrentCharges <= 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("Artifact out of charges"));
        return;
    }

    // Get the player's current cell and facing direction
    FMazeNode PlayerCell = SphereActor->WorldToMazeCell(Carrier->GetActorLocation());
    FVector Forward = Carrier->GetActorForwardVector();
    EMazeDir Dir = SphereActor->GetDirectionFromVector(Forward, PlayerCell);

    /* turned off temp for compiling
    // Activate the ability from the player's current cell and direction
    ActivateAbilityFromNode(PlayerCell, Dir);

    // Decrement AFTER successful use
    bool bSuccess = ActivateAbilityFromNode(PlayerCell, Dir);
    if (bSuccess)
    {
        CurrentCharges--;
    }*/

    ActivateAbilityFromNode(PlayerCell, Dir);
    CurrentCharges--;

    UE_LOG(LogTemp, Log, TEXT("Charges remaining: %d"), CurrentCharges);

    if (CurrentCharges <= 0)
    {
        UE_LOG(LogTemp, Log, TEXT("Artifact depleted"));

        ArtifactType = EArtifactType::None;

        // Optional: change color to indicate empty
        MeshComponent->SetMaterial(0, nullptr);
    }
}

void AArtifact::FireBeam2(const FMazeNode &StartNode, EMazeDir Direction)
{
    // Safety check first
    if (!Carrier || !SphereActor)
        return;

    // 1. BULLETPROOF OVERRIDE: Instantly sweep up any old timers or fire so the gun NEVER jams!
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

    // 2. Setup the new 3D arc
    FVector SphereCenter = SphereActor->GetActorLocation();
    FVector StartPos = Carrier->GetActorLocation();

    FVector UpDir = (StartPos - SphereCenter).GetSafeNormal();
    FVector PlayerForward = Carrier->GetActorForwardVector().GetSafeNormal();
    FVector RightAxis = FVector::CrossProduct(UpDir, PlayerForward).GetSafeNormal();

    float PlanetRadius = FVector::Dist(SphereCenter, StartPos);
    float DistanceBetweenFirePillars = 150.0f;
    float DegreesPerStep = (DistanceBetweenFirePillars / PlanetRadius) * (180.0f / PI);

    // Reset tracking indexes
    CurrentBeamSpawnIndex = 0;
    CurrentCleanupIndex = 0;

    // 3. Pre-calculate all the points
    for (int32 i = 0; i < BeamDistance; i++)
    {
        FVector PointDir = UpDir.RotateAngleAxis(DegreesPerStep * i, RightAxis);
        FVector PointPos = SphereCenter + (PointDir * PlanetRadius);
        PointPos -= (PointDir * 50.0f);
        ActiveBeamPoints.Add(PointPos);
    }

    // 4. Fire!
    GetWorldTimerManager().SetTimer(BeamPropagationTimerHandle, this, &AArtifact::SpawnNextBeamSegment, BeamPropagationSpeed, true, 0.0f);
}

void AArtifact::CleanupNextBeamSegment()
{
    // If we have cleaned up all the fire, just stop the timer
    if (CurrentCleanupIndex >= SpawnedBeamEffects.Num())
    {
        GetWorldTimerManager().ClearTimer(BeamCleanupTimerHandle);
        SpawnedBeamEffects.Empty();
        ActiveBeamPoints.Empty();
        return; // We no longer need bIsBeamActive = false!
    }

    // Destroy the oldest piece of fire
    if (SpawnedBeamEffects[CurrentCleanupIndex])
    {
        SpawnedBeamEffects[CurrentCleanupIndex]->DestroyComponent();
    }

    CurrentCleanupIndex++;
}

void AArtifact::SpawnNextBeamSegment()
{
    // If we reached the end of the line, stop spawning
    if (CurrentBeamSpawnIndex >= ActiveBeamPoints.Num() || !SphereActor)
    {
        GetWorldTimerManager().ClearTimer(BeamPropagationTimerHandle);
        return;
    }

    FVector PointPos = ActiveBeamPoints[CurrentBeamSpawnIndex];
    FVector SphereCenter = SphereActor->GetActorLocation();

    // 1. SPAWN THE VISUAL EFFECT
    if (BeamNodeVFX)
    {
        FVector UpDir = (PointPos - SphereCenter).GetSafeNormal();
        FVector ForwardDir = FVector::ForwardVector; // Fallback

        // Look at the next point to figure out which way to rotate the particle!
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

    // 2. CHECK FOR KILLS (Only in the current blast radius!)
    for (TActorIterator<AMazeRunner> It(GetWorld()); It; ++It)
    {
        AMazeRunner *Runner = *It;
        // Keep the 150.0f blast radius so the fire easily catches them
        if (IsValid(Runner) && FVector::Dist(Runner->GetActorLocation(), PointPos) < 150.0f)
        {
            Runner->Die();
        }
    }

    // 3. START THE CLEANUP WAVE (Only runs once on the very first cell spawn)
    if (CurrentBeamSpawnIndex == 0)
    {
        GetWorldTimerManager().SetTimer(BeamCleanupTimerHandle, this, &AArtifact::CleanupNextBeamSegment, BeamPropagationSpeed, true, BeamDuration);
    }

    CurrentBeamSpawnIndex++;
}

void AArtifact::FireBeam(const FMazeNode &StartNode, EMazeDir Direction)
{
    // 1. Get the curving path of cells
    TArray<FMazeNode> BeamCells = Maze->GetCellsInLine(StartNode, Direction, BeamDistance, true);
    TArray<FVector> BeamPoints;

    // 2. THE ANIMATION: Spawn a visual effect at every cell and aim it forward
    for (int32 i = 0; i < BeamCells.Num(); i++)
    {
        FVector NodeLocation = GetWorldPositionFromNode(BeamCells[i]);
        BeamPoints.Add(NodeLocation);

        if (BeamNodeVFX)
        {
            // Point the particle "UP" away from the planet core
            FVector UpDir = (NodeLocation - SphereActor->GetActorLocation()).GetSafeNormal();
            FRotator VFXRotation = FRotationMatrix::MakeFromZ(UpDir).Rotator();

            // FIX 1: If there is a next cell, rotate the particle to face it!
            if (i < BeamCells.Num() - 1)
            {
                FVector NextLoc = GetWorldPositionFromNode(BeamCells[i + 1]);
                FVector ForwardDir = (NextLoc - NodeLocation).GetSafeNormal();

                // Creates a rotation pointing Forward, with its "Up" matching the sphere
                VFXRotation = FRotationMatrix::MakeFromXZ(ForwardDir, UpDir).Rotator();
            }

            UParticleSystemComponent *SpawnedVFX = UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), BeamNodeVFX, NodeLocation, VFXRotation, FVector(1.0f));

            // Save it so we can delete it later
            if (SpawnedVFX)
                SpawnedBeamEffects.Add(SpawnedVFX);
        }
    }

    // FIX 2: Schedule the cleanup timer to destroy the fire!
    GetWorldTimerManager().SetTimer(BeamCleanupTimerHandle, this, &AArtifact::CleanupBeam, BeamDuration, false);

    DrawBeamVisual(BeamPoints);

    // 3. THE MATH: Find all AIs and check if they are in the blast zone
    int32 KillCount = 0;
    for (TActorIterator<AMazeRunner> It(GetWorld()); It; ++It)
    {
        AMazeRunner *Runner = *It;
        if (!IsValid(Runner))
            continue;

        FVector AILoc = Runner->GetActorLocation();
        bool bHit = false;

        // FIX 3: Distance check! If the AI is within 150 units of ANY fire pillar, they die.
        for (const FVector &Point : BeamPoints)
        {
            if (FVector::Dist(AILoc, Point) < 150.0f)
            {
                bHit = true;
                break;
            }
        }

        if (bHit)
        {
            UE_LOG(LogTemp, Warning, TEXT("AI CAUGHT IN BEAM BLAST! Executing death sequence."));
            Runner->Die();
            KillCount++;
        }
    }

    if (KillCount > 0 && GEngine)
    {
        GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Red, FString::Printf(TEXT("Beam destroyed %d enemies!"), KillCount));
    }
}

// FIX 2: The function that puts the fire out
void AArtifact::CleanupBeam()
{
    for (UParticleSystemComponent *VFX : SpawnedBeamEffects)
    {
        if (IsValid(VFX))
        {
            VFX->DestroyComponent(); // Kills the looping particle effect
        }
    }
    SpawnedBeamEffects.Empty();

    bIsBeamActive = false;
}

// More direct ability activation, used for testing and potential future AI use
void AArtifact::ActivateAbilityFromNode(const FMazeNode &StartNode, EMazeDir Direction)
{
    if (!Maze)
    {
        UE_LOG(LogTemp, Warning, TEXT("ActivateAbilityFromNode failed because Maze is null"));
        return;
    }

    switch (ArtifactType)
    {
    case EArtifactType::Beam:
        UE_LOG(LogTemp, Warning, TEXT("[ARTIFACT] RED / BEAM used from Face=%d X=%d Y=%d"), StartNode.Face, StartNode.X, StartNode.Y);
        // FireBeam(StartNode, Direction);
        FireBeam2(StartNode, Direction);
        break;

    case EArtifactType::PhaseWalk:
        UE_LOG(LogTemp, Warning, TEXT("[ARTIFACT] GREEN / PHASE WALK used from Face=%d X=%d Y=%d"), StartNode.Face, StartNode.X, StartNode.Y);
        break;

    case EArtifactType::PathFinder:
        UE_LOG(LogTemp, Warning, TEXT("[ARTIFACT] YELLOW / PATH FINDER used from Face=%d X=%d Y=%d"), StartNode.Face, StartNode.X, StartNode.Y);
        break;

    case EArtifactType::Barrier:
        UE_LOG(LogTemp, Warning, TEXT("[ARTIFACT] BLUE / BARRIER used from Face=%d X=%d Y=%d"), StartNode.Face, StartNode.X, StartNode.Y);
        break;

    default:
        UE_LOG(LogTemp, Warning, TEXT("[ARTIFACT] NONE used"));
        break;
    }
}

// Helper to convert world position to maze cell for visual effects
FVector AArtifact::GetWorldPositionFromNode(const FMazeNode &Node) const
{
    if (!SphereActor)
        return FVector::ZeroVector;

    return SphereActor->GetCellCenterWorld(Node.Face, Node.X, Node.Y);
}

// Drawing actual beam effect (using debug lines for simplicity)
void AArtifact::DrawBeamVisual(const TArray<FVector> &BeamPoints)
{
    if (BeamPoints.Num() < 2)
        return;

    FColor DebugColor = BeamColor.ToFColor(true);

    for (int32 i = 0; i < BeamPoints.Num() - 1; ++i)
    {
        DrawDebugLine(
            GetWorld(),
            BeamPoints[i],
            BeamPoints[i + 1],
            DebugColor,
            false,
            BeamDuration,
            0,
            BeamWidth);
    }
}

// Overlap event for pickup
void AArtifact::OnOverlapBegin(
    UPrimitiveComponent *OverlappedComp,
    AActor *OtherActor,
    UPrimitiveComponent *OtherComp,
    int32 OtherBodyIndex,
    bool bFromSweep,
    const FHitResult &SweepResult)
{
    // Intentionally disabled for now.
    // Pickup is handled by AMazeArtifactManager using maze-cell alignment.
}

// Phase Walk logic
void AArtifact::ActivatePhaseWalk()
{
    if (!Carrier)
        return;

    UCapsuleComponent *Capsule =
        Carrier->FindComponentByClass<UCapsuleComponent>();

    if (!Capsule)
        return;

    Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    DrawDebugSphere(
        GetWorld(),
        Carrier->GetActorLocation(),
        120,
        16,
        FColor::Purple,
        false,
        PhaseDuration,
        0,
        5);

    GetWorldTimerManager().SetTimer(
        PhaseTimer,
        this,
        &AArtifact::EndPhaseWalk,
        PhaseDuration,
        false);
}

// Ends the phase walk effect, re-enabling collision and ensuring the player is in a valid cell
void AArtifact::EndPhaseWalk()
{
    if (!Carrier || !SphereActor)
        return;

    UCapsuleComponent *Capsule =
        Carrier->FindComponentByClass<UCapsuleComponent>();

    if (!Capsule)
        return;

    Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

    FMazeNode Node = SphereActor->WorldToMazeCell(Carrier->GetActorLocation());

    FVector SafePos = SphereActor->GetCellCenterWorld(
        Node.Face,
        Node.X,
        Node.Y);

    Carrier->SetActorLocation(SafePos);
}

// Path Finder logic, finds the nearest artifact and draws a path to it
void AArtifact::ActivatePathFinder()
{
    if (!Carrier || !Navigator)
        return;

    FVector PlayerPos = Carrier->GetActorLocation();

    AArtifact *Closest = nullptr;
    float BestDist = FLT_MAX;

    for (TActorIterator<AArtifact> It(GetWorld()); It; ++It)
    {
        if (*It == this)
            continue;

        float Dist = FVector::Dist(PlayerPos, It->GetActorLocation());

        if (Dist < BestDist)
        {
            BestDist = Dist;
            Closest = *It;
        }
    }

    if (!Closest)
        return;

    TArray<FVector> Path;

    if (Navigator->FindPath(PlayerPos, Closest->GetActorLocation(), Path))
    {
        for (int32 i = 0; i < Path.Num() - 1; i++)
        {
            DrawDebugLine(
                GetWorld(),
                Path[i],
                Path[i + 1],
                FColor::Green,
                false,
                PathDuration,
                0,
                12.f);

            DrawDebugSphere(
                GetWorld(),
                Path[i],
                25,
                12,
                FColor::Green,
                false,
                PathDuration);
        }
    }
}

// Activates a barrier around the player, creating temporary walls in adjacent cells
void AArtifact::ActivateBarrier()
{
    FMazeNode PlayerNode =
        SphereActor->WorldToMazeCell(Carrier->GetActorLocation());

    for (int32 x = -BarrierRadius; x <= BarrierRadius; x++)
    {
        for (int32 y = -BarrierRadius; y <= BarrierRadius; y++)
        {
            if (FMath::Abs(x) != BarrierRadius &&
                FMath::Abs(y) != BarrierRadius)
                continue;

            int32 Face = PlayerNode.Face;
            int32 NX = PlayerNode.X + x;
            int32 NY = PlayerNode.Y + y;

            if (NX < 0 || NX >= Maze->CellsPerFace || NY < 0 || NY >= Maze->CellsPerFace)
            {
                continue;
            }

            FVector Pos =
                SphereActor->GetCellCenterWorld(Face, NX, NY);

            AActor *Wall = GetWorld()->SpawnActor<AActor>(
                BarrierWallClass,
                Pos,
                FRotator::ZeroRotator);

            Wall->SetActorLocation(Pos);
            BarrierWalls.Add(Wall);
        }
    }

    FVector DebugCenter = SphereActor->GetCellCenterWorld(
        PlayerNode.Face,
        PlayerNode.X,
        PlayerNode.Y);

    DrawDebugBox(
        GetWorld(),
        DebugCenter,
        FVector(40),
        FColor::Red,
        false,
        BarrierDuration,
        0,
        5);

    GetWorldTimerManager().SetTimer(
        BarrierTimer,
        this,
        &AArtifact::DestroyBarrier,
        BarrierDuration,
        false);
}

// Destroys all barrier walls when the effect ends
void AArtifact::DestroyBarrier()
{
    for (AActor *Wall : BarrierWalls)
    {
        if (Wall)
            Wall->Destroy();
    }

    BarrierWalls.Empty();
}

void AArtifact::ApplyDebugVisuals()
{
    if (!MeshComponent)
    {
        return;
    }

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

    UMaterialInstanceDynamic *MID = MeshComponent->CreateAndSetMaterialInstanceDynamic(0);

    if (MID)
    {
        MID->SetVectorParameterValue(TEXT("BaseColor"), Color);
        MID->SetVectorParameterValue(TEXT("Color"), Color);
    }
}

// ============================================================
// Cleanup on Deletion / Wave Reset
// ============================================================
void AArtifact::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    Super::EndPlay(EndPlayReason);

    // 1. Destroy any orphaned fire particles left in the world!
    for (UParticleSystemComponent *VFX : SpawnedBeamEffects)
    {
        if (IsValid(VFX))
        {
            VFX->DestroyComponent();
        }
    }
    SpawnedBeamEffects.Empty();

    // 2. Safely cancel the traveling beam timers
    GetWorldTimerManager().ClearTimer(BeamCleanupTimerHandle);

    // (Optional) Clear the barrier walls if you use the Barrier ability!
    for (AActor *Wall : BarrierWalls)
    {
        if (IsValid(Wall))
            Wall->Destroy();
    }
    BarrierWalls.Empty();
}