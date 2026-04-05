#include "Artifact.h"
#include "../Maze/Maze.h"
#include "../Conversion/CubeToSphere.h"

#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "../AI/MazeNavigator.h"
//#include "EnemyPawn.h"

#include "Components/StaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "DrawDebugHelpers.h"

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
void AArtifact::PickUp(AActor* NewCarrier)
{
    if (!NewCarrier || bIsCarried)
    {
        return;
    }

    USceneComponent* CarrierRoot = NewCarrier->GetRootComponent();
    if (!CarrierRoot)
    {
        return;
    }

    bIsCarried = true;
    Carrier = NewCarrier;

    PickupTrigger->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    MeshComponent->SetSimulatePhysics(false);
    MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    AttachToComponent(
        CarrierRoot,
        FAttachmentTransformRules::SnapToTargetNotIncludingScale);

    SetActorRelativeLocation(CarriedHatOffset);
    SetActorRelativeRotation(FRotator::ZeroRotator);
    SetActorRelativeScale3D(CarriedHatScale);

    if (SphereActor)
    {
        CurrentCell = SphereActor->WorldToMazeCell(NewCarrier->GetActorLocation());
    }

    UE_LOG(LogTemp, Warning, TEXT("Artifact picked up and attached as test hat"));
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

// More direct ability activation, used for testing and potential future AI use
void AArtifact::ActivateAbilityFromNode(const FMazeNode& StartNode, EMazeDir Direction)
{
    if (!Maze) return;

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
    }
}

// Core logic for firing the beam, called by ActivateAbilityFromNode
void AArtifact::FireBeam(const FMazeNode& StartNode, EMazeDir Direction)
{
    TArray<FMazeNode> BeamCells = Maze->GetCellsInLine(StartNode, Direction, BeamDistance, true);

    TArray<FVector> BeamPoints;

    for (const FMazeNode& Node : BeamCells)
    {
        BeamPoints.Add(GetWorldPositionFromNode(Node));
    }

    DrawBeamVisual(BeamPoints);

    /*
    AActor* AI =
    UGameplayStatics::GetActorOfClass(GetWorld(), AEnemyPawn::StaticClass());

    if (AI)
    {
        FMazeNode AINode =
            SphereActor->WorldToMazeCell(AI->GetActorLocation());

        if (BeamCells.Contains(AINode))
        {
            UE_LOG(LogTemp, Warning, TEXT("AI HIT BY BEAM"));
        }
    }*/

    AActor* AI = AIPawn;

    if (AI)
    {
        FMazeNode AINode = SphereActor->WorldToMazeCell(AI->GetActorLocation());

        if (BeamCells.Contains(AINode))
        {
            UE_LOG(LogTemp, Warning, TEXT("AI HIT BY BEAM"));
        }
    }
}

// Helper to convert world position to maze cell for visual effects
FVector AArtifact::GetWorldPositionFromNode(const FMazeNode& Node) const
{
    if (!SphereActor)
        return FVector::ZeroVector;

    return SphereActor->GetCellCenterWorld(Node.Face, Node.X, Node.Y);
}

// Drawing actual beam effect (using debug lines for simplicity)
void AArtifact::DrawBeamVisual(const TArray<FVector>& BeamPoints)
{
    if (BeamPoints.Num() < 2) return;

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
            BeamWidth
        );
    }
}

// Overlap event for pickup
void AArtifact::OnOverlapBegin(
    UPrimitiveComponent* OverlappedComp,
    AActor* OtherActor,
    UPrimitiveComponent* OtherComp,
    int32 OtherBodyIndex,
    bool bFromSweep,
    const FHitResult& SweepResult)
{
    // Intentionally disabled for now.
    // Pickup is handled by AMazeArtifactManager using maze-cell alignment.
}


// Phase Walk logic
void AArtifact::ActivatePhaseWalk()
{
    if (!Carrier) return;

    UCapsuleComponent* Capsule =
        Carrier->FindComponentByClass<UCapsuleComponent>();

    if (!Capsule) return;

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
        5
    );

    GetWorldTimerManager().SetTimer(
        PhaseTimer,
        this,
        &AArtifact::EndPhaseWalk,
        PhaseDuration,
        false
    );
}

// Ends the phase walk effect, re-enabling collision and ensuring the player is in a valid cell
void AArtifact::EndPhaseWalk()
{
    if (!Carrier || !SphereActor) return;

    UCapsuleComponent* Capsule =
        Carrier->FindComponentByClass<UCapsuleComponent>();

    if (!Capsule) return;

    Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

    FMazeNode Node = SphereActor->WorldToMazeCell(Carrier->GetActorLocation());

    FVector SafePos = SphereActor->GetCellCenterWorld(
        Node.Face,
        Node.X,
        Node.Y
    );

    Carrier->SetActorLocation(SafePos);
}

// Path Finder logic, finds the nearest artifact and draws a path to it
void AArtifact::ActivatePathFinder()
{
    if (!Carrier || !Navigator) return;

    FVector PlayerPos = Carrier->GetActorLocation();

    AArtifact* Closest = nullptr;
    float BestDist = FLT_MAX;

    for (TActorIterator<AArtifact> It(GetWorld()); It; ++It)
    {
        if (*It == this) continue;

        float Dist = FVector::Dist(PlayerPos, It->GetActorLocation());

        if (Dist < BestDist)
        {
            BestDist = Dist;
            Closest = *It;
        }
    }

    if (!Closest) return;

    TArray<FVector> Path;

    if (Navigator->FindPath(PlayerPos, Closest->GetActorLocation(), Path))
    {
        for (int32 i = 0; i < Path.Num() - 1; i++)
        {
            DrawDebugLine(
                GetWorld(),
                Path[i],
                Path[i+1],
                FColor::Green,
                false,
                PathDuration,
                0,
                12.f
            );

            DrawDebugSphere(
                GetWorld(),
                Path[i],
                25,
                12,
                FColor::Green,
                false,
                PathDuration
            );
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

            AActor* Wall = GetWorld()->SpawnActor<AActor>(
                BarrierWallClass,
                Pos,
                FRotator::ZeroRotator
            );

            Wall->SetActorLocation(Pos);
            BarrierWalls.Add(Wall);
        }
    }

    FVector DebugCenter = SphereActor->GetCellCenterWorld(
        PlayerNode.Face,
        PlayerNode.X,
        PlayerNode.Y
    );

    DrawDebugBox(
        GetWorld(),
        DebugCenter,
        FVector(40),
        FColor::Red,
        false,
        BarrierDuration,
        0,
        5
    );

    
    GetWorldTimerManager().SetTimer(
        BarrierTimer,
        this,
        &AArtifact::DestroyBarrier,
        BarrierDuration,
        false
    );
}

// Destroys all barrier walls when the effect ends
void AArtifact::DestroyBarrier()
{
    for (AActor* Wall : BarrierWalls)
    {
        if (Wall) Wall->Destroy();
    }

    BarrierWalls.Empty();
}