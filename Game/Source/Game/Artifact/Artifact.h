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

UENUM(BlueprintType)
enum class EArtifactType : uint8
{
    None UMETA(DisplayName = "None"),
    Beam UMETA(DisplayName = "Beam"),
    PhaseWalk UMETA(DisplayName = "Phase Walk"),
    PathFinder UMETA(DisplayName = "Path Finder"),
    Barrier UMETA(DisplayName = "Barrier")
};

UCLASS()
class GAME_API AArtifact : public AActor
{
    GENERATED_BODY()

public:
    AArtifact();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    // ==========================================
    // COMPONENTS & CORE
    // ==========================================
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Artifact")
    UStaticMeshComponent *MeshComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Artifact")
    USphereComponent *PickupTrigger;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact")
    EArtifactType ArtifactType = EArtifactType::Beam;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact")
    float SphereRadius = 50.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact")
    bool bIsCarried = false;

    UPROPERTY(BlueprintReadOnly, Category = "Artifact")
    AActor *Carrier = nullptr;

    // ==========================================
    // INVENTORY & CHARGES
    // ==========================================
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Ability")
    int32 MaxCharges = 100;

    UPROPERTY(BlueprintReadOnly, Category = "Artifact|Ability")
    int32 CurrentCharges;

    // ==========================================
    // MAZE INTEGRATION
    // ==========================================
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Maze")
    UMaze *Maze = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Maze")
    ACubeToSphere *SphereActor = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Maze")
    FMazeNode CurrentCell;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Maze")
    AActor *AIPawn = nullptr;

    UPROPERTY()
    UMazeNavigator *Navigator;

    // ==========================================
    // VISUALS & ANIMATION
    // ==========================================
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Test")
    float IdleSurfaceOffset = 35.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Test")
    FVector CarriedHatOffset = FVector(0.f, 0.f, 110.f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Test")
    FVector CarriedHatScale = FVector(0.35f, 0.35f, 0.35f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Visual")
    float RotationSpeed = 50.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Visual")
    float FloatSpeed = 1.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Visual")
    float FloatAmplitude = 10.f;

    UFUNCTION(BlueprintCallable, Category = "Artifact|Visual")
    void ApplyDebugVisuals();

    // ==========================================
    // FUNCTIONS
    // ==========================================
    UFUNCTION(BlueprintCallable, Category = "Artifact|Spawn")
    void SpawnAtRandomCell();

    UFUNCTION(BlueprintCallable, Category = "Artifact")
    void PickUp(AActor *NewCarrier);

    UFUNCTION(BlueprintCallable, Category = "Artifact")
    void Drop(FVector DropLocation);

    UFUNCTION(BlueprintCallable, Category = "Artifact|Ability")
    void ActivateAbility();

    UFUNCTION(BlueprintCallable, Category = "Artifact|Ability")
    void ActivateAbilityFromNode(const FMazeNode &StartNode, EMazeDir Direction);

protected:
    // ==========================================
    // ABILITY: BEAM
    // ==========================================
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Ability|Beam")
    int32 BeamDistance = 20;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Ability|Beam")
    float BeamPropagationSpeed = 0.04f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Ability|Beam")
    float BeamDuration = 2.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Ability|Beam")
    class UParticleSystem *BeamNodeVFX;

    void FireBeam(const FMazeNode &StartNode, EMazeDir Direction);
    void SpawnNextBeamSegment();
    void CleanupNextBeamSegment();
    void ClearActiveBeam(); // Replaces duplicate cleanup code

    TArray<FVector> ActiveBeamPoints;
    UPROPERTY()
    TArray<class UParticleSystemComponent *> SpawnedBeamEffects;

    int32 CurrentBeamSpawnIndex = 0;
    int32 CurrentCleanupIndex = 0;
    FTimerHandle BeamPropagationTimerHandle;
    FTimerHandle BeamCleanupTimerHandle;

    // ==========================================
    // ABILITY: PHASE WALK
    // ==========================================
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Ability|PhaseWalk")
    float PhaseDuration = 6.f;

    // The visual aura that wraps around the player
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Ability|PhaseWalk")
    class UParticleSystem *PhaseWalkVFX;

    UPROPERTY()
    class UParticleSystemComponent *ActivePhaseVFX;

    void ActivatePhaseWalk();
    void EndPhaseWalk();
    FTimerHandle PhaseTimer;

    // ==========================================
    // ABILITY: PATH FINDER
    // ==========================================
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Ability|PathFinder")
    float PathDuration = 10.f;

    void ActivatePathFinder();

    // ==========================================
    // ABILITY: BARRIER
    // ==========================================
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Ability|Barrier")
    TSubclassOf<AActor> BarrierWallClass;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Ability|Barrier")
    float BarrierDuration = 8.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Ability|Barrier")
    int32 BarrierRadius = 7;

    void ActivateBarrier();
    void DestroyBarrier();

    UPROPERTY()
    TArray<AActor *> BarrierWalls;
    FTimerHandle BarrierTimer;

private:
    float AccumulatedTime = 0.f;
    FVector InitialLocation;

    UFUNCTION()
    void OnOverlapBegin(UPrimitiveComponent *OverlappedComp, AActor *OtherActor, UPrimitiveComponent *OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult &SweepResult);
};
