#include "Artifact.h"
#include "../Maze/Maze.h"
#include "../Conversion/CubeToSphere.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"

AArtifact::AArtifact()
{
  PrimaryActorTick.bCanEverTick = true;

  // Create the sphere mesh component
  MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComponent"));
  RootComponent = MeshComponent;

  // Set up sphere mesh (will use default engine sphere)
  static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere"));
  if (SphereMesh.Succeeded())
  {
    MeshComponent->SetStaticMesh(SphereMesh.Object);
  }

  // Set default scale based on radius
  MeshComponent->SetWorldScale3D(FVector(SphereRadius / 50.f));

  // Enable physics simulation when dropped
  MeshComponent->SetSimulatePhysics(false);
  MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
}

void AArtifact::BeginPlay()
{
  Super::BeginPlay();

  InitialLocation = GetActorLocation();
  AccumulatedTime = 0.f;
}

void AArtifact::Tick(float DeltaTime)
{
  Super::Tick(DeltaTime);

  // Idle animation when not carried
  if (!bIsCarried)
  {
    AccumulatedTime += DeltaTime;

    // Gentle rotation
    FRotator NewRotation = GetActorRotation();
    NewRotation.Yaw += RotationSpeed * DeltaTime;
    SetActorRotation(NewRotation);

    // Gentle floating motion
    FVector NewLocation = InitialLocation;
    NewLocation.Z += FMath::Sin(AccumulatedTime * FloatSpeed) * FloatAmplitude;
    SetActorLocation(NewLocation);
  }
}

void AArtifact::PickUp(AActor *NewCarrier)
{
  if (!NewCarrier)
  {
    return;
  }

  bIsCarried = true;
  Carrier = NewCarrier;

  // Disable physics and collision when carried
  MeshComponent->SetSimulatePhysics(false);
  MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);

  // Attach to carrier (can be customized based on socket/bone)
  AttachToActor(NewCarrier, FAttachmentTransformRules::SnapToTargetNotIncludingScale);

  UE_LOG(LogTemp, Log, TEXT("Artifact picked up by %s"), *NewCarrier->GetName());
}

void AArtifact::Drop(FVector DropLocation)
{
  bIsCarried = false;
  Carrier = nullptr;

  // Detach from carrier
  DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

  // Re-enable collision
  MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
  MeshComponent->SetSimulatePhysics(true);

  // Set new location
  SetActorLocation(DropLocation);
  InitialLocation = DropLocation;

  UE_LOG(LogTemp, Log, TEXT("Artifact dropped at location: %s"), *DropLocation.ToString());
}

void AArtifact::ActivateAbility(const FMazeNode &StartNode, EMazeDir Direction)
{
  if (!Maze || !SphereActor)
  {
    UE_LOG(LogTemp, Warning, TEXT("Artifact: Cannot activate ability - Maze or SphereActor is null"));
    return;
  }

  switch (ArtifactType)
  {
  case EArtifactType::Beam:
    FireBeam(StartNode, Direction);
    break;

  case EArtifactType::Shield:
    UE_LOG(LogTemp, Log, TEXT("Shield ability not yet implemented"));
    break;

  case EArtifactType::Teleport:
    UE_LOG(LogTemp, Log, TEXT("Teleport ability not yet implemented"));
    break;

  case EArtifactType::Vision:
    UE_LOG(LogTemp, Log, TEXT("Vision ability not yet implemented"));
    break;

  default:
    UE_LOG(LogTemp, Warning, TEXT("Unknown artifact type"));
    break;
  }
}

void AArtifact::FireBeam(const FMazeNode &StartNode, EMazeDir Direction)
{
  if (!Maze || !SphereActor)
  {
    return;
  }

  UE_LOG(LogTemp, Log, TEXT("Firing beam from Face:%d X:%d Y:%d in direction %d"),
         StartNode.Face, StartNode.X, StartNode.Y, (int32)Direction);

  // Get all cells in the beam path
  TArray<FMazeNode> BeamCells = Maze->GetCellsInLine(StartNode, Direction, BeamDistance, true);

  // Convert maze nodes to world positions
  TArray<FVector> BeamPoints;
  for (const FMazeNode &Cell : BeamCells)
  {
    FVector WorldPos = GetWorldPositionFromNode(Cell);
    BeamPoints.Add(WorldPos);
  }

  // Draw visual feedback
  if (BeamPoints.Num() > 0)
  {
    DrawBeamVisual(BeamPoints);
    UE_LOG(LogTemp, Log, TEXT("Beam traversed %d cells"), BeamPoints.Num());
  }
  else
  {
    UE_LOG(LogTemp, Warning, TEXT("Beam did not traverse any cells"));
  }
}

FVector AArtifact::GetWorldPositionFromNode(const FMazeNode &Node) const
{
  if (!SphereActor)
  {
    return FVector::ZeroVector;
  }

  // Use the CubeToSphere actor to convert maze coordinates to world position
  return SphereActor->GetCellCenterWorld(Node.Face, Node.X, Node.Y);
}

void AArtifact::DrawBeamVisual(const TArray<FVector> &BeamPoints)
{
  if (BeamPoints.Num() < 2 || !GetWorld())
  {
    return;
  }

  // Convert FLinearColor to FColor for DrawDebugLine
  FColor DebugColor = BeamColor.ToFColor(true);

  // Draw lines connecting all beam points
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

  // Draw sphere at each point for better visibility
  for (const FVector &Point : BeamPoints)
  {
    DrawDebugSphere(
        GetWorld(),
        Point,
        BeamWidth * 2.f,
        12,
        DebugColor,
        false,
        BeamDuration);
  }

  // Draw a larger sphere at the start point
  if (BeamPoints.Num() > 0)
  {
    DrawDebugSphere(
        GetWorld(),
        BeamPoints[0],
        BeamWidth * 3.f,
        12,
        FColor::Yellow,
        false,
        BeamDuration);
  }

  // Draw a larger sphere at the end point
  if (BeamPoints.Num() > 1)
  {
    DrawDebugSphere(
        GetWorld(),
        BeamPoints.Last(),
        BeamWidth * 3.f,
        12,
        FColor::Red,
        false,
        BeamDuration);
  }
}
