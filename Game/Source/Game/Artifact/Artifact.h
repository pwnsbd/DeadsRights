#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "../Maze/MazeTypes.h"
#include "Artifact.generated.h"

class UMaze;
class ACubeToSphere;
class UStaticMeshComponent;

// Enum defining different artifact types and their abilities
UENUM(BlueprintType)
enum class EArtifactType : uint8
{
  None UMETA(DisplayName = "None"),
  Beam UMETA(DisplayName = "Beam Projector"),
  Shield UMETA(DisplayName = "Shield Generator"),
  Teleport UMETA(DisplayName = "Teleporter"),
  Vision UMETA(DisplayName = "Vision Enhancer")
};

/**
 * AArtifact
 * Job: Represent collectible artifacts in the maze that grant abilities
 * - Can be picked up by player or AI
 * - Each artifact has a type that determines its ability
 * - Beam artifact: Fires a beam that traverses 20 maze cells
 */
UCLASS()
class GAME_API AArtifact : public AActor
{
  GENERATED_BODY()

public:
  AArtifact();

  virtual void BeginPlay() override;
  virtual void Tick(float DeltaTime) override;

  // ---- Components ----
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Artifact")
  UStaticMeshComponent *MeshComponent;

  // ---- Properties ----
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact")
  EArtifactType ArtifactType = EArtifactType::Beam;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact")
  float SphereRadius = 50.f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact")
  bool bIsCarried = false;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact")
  AActor *Carrier = nullptr;

  // ---- Maze Integration ----
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Maze")
  UMaze *Maze = nullptr;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Maze")
  ACubeToSphere *SphereActor = nullptr;

  // Current maze position of the artifact
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Maze")
  FMazeNode CurrentCell;

  // ---- Interaction ----
  UFUNCTION(BlueprintCallable, Category = "Artifact")
  void PickUp(AActor *NewCarrier);

  UFUNCTION(BlueprintCallable, Category = "Artifact")
  void Drop(FVector DropLocation);

  // ---- Abilities ----
  // Activate the artifact's ability
  UFUNCTION(BlueprintCallable, Category = "Artifact|Ability")
  void ActivateAbility(const FMazeNode &StartNode, EMazeDir Direction);

  // Beam-specific ability parameters
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Ability|Beam")
  int32 BeamDistance = 20;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Ability|Beam")
  float BeamWidth = 10.f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Ability|Beam")
  float BeamDuration = 2.f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Ability|Beam")
  FLinearColor BeamColor = FLinearColor::Blue;

protected:
  // Beam ability implementation
  void FireBeam(const FMazeNode &StartNode, EMazeDir Direction);

  // Helper: Get world position from maze node
  FVector GetWorldPositionFromNode(const FMazeNode &Node) const;

  // Visual feedback for beam
  void DrawBeamVisual(const TArray<FVector> &BeamPoints);

  // Rotation animation for idle artifact
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Visual")
  float RotationSpeed = 50.f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Visual")
  float FloatSpeed = 1.f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Visual")
  float FloatAmplitude = 10.f;

private:
  float AccumulatedTime = 0.f;
  FVector InitialLocation;
};
