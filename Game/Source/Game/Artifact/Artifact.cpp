#include "Artifact.h"
#include "../Maze/Maze.h"
#include "../Conversion/CubeToSphere.h"

#include "Components/StaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "DrawDebugHelpers.h"

// Sets default values
AArtifact::AArtifact()
{
    PrimaryActorTick.bCanEverTick = true;

    // Mesh
    MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComponent"));
    RootComponent = MeshComponent;

    // Use a basic sphere mesh from the engine content
    static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere"));
    if (SphereMesh.Succeeded())
    {
        MeshComponent->SetStaticMesh(SphereMesh.Object);
    }

    // Set default material (can be overridden in editor)
    MeshComponent->SetWorldScale3D(FVector(SphereRadius / 50.f));
    MeshComponent->SetSimulatePhysics(false);
    MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

    // Pickup Trigger
    PickupTrigger = CreateDefaultSubobject<USphereComponent>(TEXT("PickupTrigger"));
    PickupTrigger->InitSphereRadius(100.f);
    PickupTrigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    PickupTrigger->SetCollisionResponseToAllChannels(ECR_Ignore);
    PickupTrigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
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

        FVector Loc = InitialLocation;
        Loc.Z += FMath::Sin(AccumulatedTime * FloatSpeed) * FloatAmplitude;
        SetActorLocation(Loc);
    }
}

// Spawns the artifact at a random cell on the sphere
void AArtifact::SpawnAtRandomCell()
{
    if (!Maze || !SphereActor)
        return;

    int32 Face = FMath::RandRange(0, 5);
    int32 X = FMath::RandRange(0, Maze->CellsPerFace - 1);
    int32 Y = FMath::RandRange(0, Maze->CellsPerFace - 1);

    CurrentCell = FMazeNode(Face, X, Y);

    FVector SpawnLoc = SphereActor->GetCellCenterWorld(Face, X, Y);
    SetActorLocation(SpawnLoc);

    InitialLocation = SpawnLoc;
}

// Handles pickup logic, attaching the artifact to the carrier
void AArtifact::PickUp(AActor* NewCarrier)
{
    if (!NewCarrier) return;

    bIsCarried = true;
    Carrier = NewCarrier;

    MeshComponent->SetSimulatePhysics(false);
    MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);

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

    // Activate the ability from the player's current cell and direction
    ActivateAbilityFromNode(PlayerCell, Dir);

    // Decrement AFTER successful use
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

        default:
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
    if (bIsCarried || !OtherActor)
        return;

    PickUp(OtherActor);
}