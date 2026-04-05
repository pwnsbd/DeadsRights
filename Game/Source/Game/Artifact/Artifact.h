#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "../Maze/MazeTypes.h"
#include "Artifact.generated.h"

class UMaze;
class ACubeToSphere;
class UStaticMeshComponent;
class USphereComponent;
class UMazeNavigator;
class UCapsuleComponent;
//class AEnemyPawn;

// Initalized artifact types
UENUM(BlueprintType)
enum class EArtifactType : uint8
{
    None          UMETA(DisplayName = "None"), // Basic artifact
    Beam          UMETA(DisplayName = "Beam"), //Fires beam to destroy AI
    PhaseWalk     UMETA(DisplayName = "Phase Walk"),       // walk through walls
    PathFinder    UMETA(DisplayName = "Path Finder"),      // shortest path to nearest artifact
    Barrier       UMETA(DisplayName = "Barrier")          // trap area
};

UCLASS()
class GAME_API AArtifact : public AActor
{
    GENERATED_BODY()

public:
    AArtifact();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;

    // ---------- Components ----------
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Artifact")
    UStaticMeshComponent* MeshComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Artifact")
    USphereComponent* PickupTrigger;

    // ---------- Core Properties ----------
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Artifact")
    EArtifactType ArtifactType = EArtifactType::Beam;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Artifact")
    float SphereRadius = 50.f; // Mesh property for artifact size, also used for pickup radius

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Artifact")
    bool bIsCarried = false; // Whether the artifact is currently being carried by the player

    UPROPERTY(BlueprintReadOnly, Category="Artifact")
    AActor* Carrier = nullptr; // The actor currently carrying the artifact (e.g., the player)

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Artifact|Test")
    float IdleSurfaceOffset = 35.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Artifact|Test")
    FVector CarriedHatOffset = FVector(0.f, 0.f, 110.f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Artifact|Test")
    FVector CarriedHatScale = FVector(0.35f, 0.35f, 0.35f);

    // ---------- Maze Integration ----------
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Artifact|Maze")
    UMaze* Maze = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Artifact|Maze")
    ACubeToSphere* SphereActor = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Artifact|Maze")
    FMazeNode CurrentCell; // The maze cell where the artifact is currently located (updated on spawn and pickup)

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Artifact|Maze")
    AActor* AIPawn = nullptr;

    // ---------- Spawning ----------
    UFUNCTION(BlueprintCallable, Category="Artifact|Spawn")
    void SpawnAtRandomCell();

    // ---------- Interaction ----------
    UFUNCTION(BlueprintCallable, Category="Artifact")
    void PickUp(AActor* NewCarrier);

    UFUNCTION(BlueprintCallable, Category="Artifact")
    void Drop(FVector DropLocation);

    // ---------- Ability ----------
    UFUNCTION(BlueprintCallable, Category="Artifact|Ability")
    void ActivateAbility(); // Auto-detects player cell + direction

    UFUNCTION(BlueprintCallable, Category="Artifact|Ability")
    void ActivateAbilityFromNode(const FMazeNode& StartNode, EMazeDir Direction); // More direct control, used for testing and potential future AI use

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Artifact|Ability")
    int32 MaxCharges = 2; // Maximum number of times the artifact can be used before depletion

    UPROPERTY(BlueprintReadOnly, Category="Artifact|Ability")
    int32 CurrentCharges;

    // Beam parameters
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Artifact|Ability|Beam")
    int32 BeamDistance = 20;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Artifact|Ability|Beam")
    float BeamWidth = 10.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Artifact|Ability|Beam")
    float BeamDuration = 2.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Artifact|Ability|Beam")
    FLinearColor BeamColor = FLinearColor::Blue;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Artifact|Ability|Barrier")
    TSubclassOf<AActor> BarrierWallClass;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Artifact|Ability|PhaseWalk")
    float PhaseDuration = 6.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Artifact|Ability|PathFinder")
    float PathDuration = 10.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Artifact|Ability|Barrier")
    float BarrierDuration = 8.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Artifact|Ability|Barrier")
    int32 BarrierRadius = 7;

protected:
    void FireBeam(const FMazeNode& StartNode, EMazeDir Direction); // Core logic for firing the beam, called by ActivateAbilityFromNode

    // Phase Walk parameters
    void ActivatePhaseWalk();
    void EndPhaseWalk();
    FTimerHandle PhaseTimer;
 

    //Path Finder parameters
    void ActivatePathFinder();
    

    UPROPERTY()
    UMazeNavigator* Navigator;

    //Barrier Spell
    void ActivateBarrier();
    void DestroyBarrier();

    UPROPERTY()
    TArray<AActor*> BarrierWalls;

    FTimerHandle BarrierTimer;



    FVector GetWorldPositionFromNode(const FMazeNode& Node) const; // Helper to convert maze cell to world position for visual effects
    void DrawBeamVisual(const TArray<FVector>& BeamPoints); // Drawing actual beam effect (using debug lines for simplicity)

    // Overlap event for pickup
    UFUNCTION()
    void OnOverlapBegin(
        UPrimitiveComponent* OverlappedComp,
        AActor* OtherActor,
        UPrimitiveComponent* OtherComp,
        int32 OtherBodyIndex,
        bool bFromSweep,
        const FHitResult& SweepResult
    );

    // Idle animation
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Artifact|Visual")
    float RotationSpeed = 50.f;

    // Floating animation parameters
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Artifact|Visual")
    float FloatSpeed = 1.f;

    // Amplitude of vertical floating motion when idle
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Artifact|Visual")
    float FloatAmplitude = 10.f;

private:
    float AccumulatedTime = 0.f; // Used for floating animation timing
    FVector InitialLocation; // Used for floating animation reference
};