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
class USoundBase;

UENUM(BlueprintType)
enum class EArtifactType : uint8
{
    None    UMETA(DisplayName = "None"),
    Basic   UMETA(DisplayName = "Basic"),
    Beam    UMETA(DisplayName = "Beam"),
    PhaseWalk UMETA(DisplayName = "Phase Walk"),
    PathFinder UMETA(DisplayName = "Path Finder"),
    Barrier UMETA(DisplayName = "Barrier"),
    AoEBomb UMETA(DisplayName = "AoE Bomb")
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

    float UpgradePowerLevel();
    float UpgradeCooldown(float CurrentCooldown);

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

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Ability")
    float CooldownDuration = 6.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Upgrades")
    int32 CooldownUpgrades = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Upgrades")
    int32 MaxCooldownUpgrades = 5;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Upgrades")
    float CooldownReductionPerUpgrade = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Upgrades")
    float MinCooldownAfterUpgrades = 1.0f;

    float CalculateCooldownAtLevel(float BaseCooldown, int32 Level) const;

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

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Artifact|Visual")
    TMap<EArtifactType, UStaticMesh *> ArtifactMeshes;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Visual")
    FRotator MeshRotationOffset = FRotator::ZeroRotator;

    UFUNCTION(BlueprintCallable, Category = "Artifact|Visual")
    void UpdateMeshForType();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Visual")
    float RotationSpeed = 50.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Visual")
    float FloatSpeed = 1.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Visual")
    float FloatAmplitude = 10.f;

    UFUNCTION(BlueprintCallable, Category = "Artifact|Visual")
    void ApplyDebugVisuals();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Audio")
    USoundBase *PickupSound = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Audio")
    USoundBase *ActivationSound = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Audio")
    USoundBase *BeamActivationSound = nullptr;

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
    int32 BeamDistance = 5;

    // Beam level
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Ability|Beam")
    float BeamPowerLevel = 1.f;

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
    float PhaseDuration = 2.f;

    // Phase level
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Ability|PhaseWalk")
    float PhasePowerLevel = 1.f;

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
    float PathDuration = 3.f; // How long it stays visible AFTER it finishes drawing

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Ability|PathFinder")
    float PathPropagationSpeed = 0.05f; // How fast the snake draws itself (0.05s per step)

    // --- FIX: ADD THIS BACK IN! ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Ability|PathFinder")
    float PathPowerLevel = 1.f;
    // ------------------------------

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Ability|PathFinder")
    class UParticleSystem *PathFinderVFX;

    // --- NEW: ERROR FEEDBACK ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Ability|PathFinder")
    class UParticleSystem *PathErrorVFX; // Red smoke puff

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Ability|PathFinder")
    class USoundBase *ErrorSound; // Error buzz sound

    void ActivatePathFinder();
    void SpawnNextPathSegment(); // NEW: The growing logic
    void CleanupNextPathSegment();
    void CleanupPathFinder();

    FTimerHandle PathCleanupTimer;
    FTimerHandle PathPropagationTimerHandle; // NEW: Controls the drawing speed
    int32 CurrentPathSpawnIndex = 0;
    int32 CurrentPathCleanupIndex = 0;

    TArray<FVector> ActivePathPoints;

    UPROPERTY()
    TArray<class UParticleSystemComponent *> SpawnedPathEffects;

    // ==========================================
    // ABILITY: BARRIER
    // ==========================================
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Ability|Barrier")
    TSubclassOf<AActor> BarrierWallClass;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Ability|Barrier")
    float BarrierDuration = 8.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Ability|Barrier")
    int32 BarrierLength = 5;

    void ActivateBarrier(const FMazeNode &StartNode, EMazeDir Direction);
    void SpawnNextBarrierSegment();
    void DestroyBarrier();

    UPROPERTY()
    TArray<AActor *> BarrierWalls;

    FTimerHandle BarrierTimer;
    FTimerHandle BarrierPropagationTimerHandle;

    // Stores the exact mathematical edges between the cells!
    UPROPERTY()
    TArray<FTransform> PendingBarrierSpawns;

    // ==========================================
    // ABILITY: AOE BOMB
    // ==========================================
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Ability|AoE")
    float AoEMaxRadius = 100.f;

    // AoE level
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Ability|AoE")
    float AoEPowerLevel = 1.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Ability|AoE")
    float AoEPropagationSpeed = 0.08f; // 0.08 is slower than the beam's 0.04!

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Ability|AoE")
    float AoEExpansionStep = 80.f; // Distance between each ring of fire

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artifact|Ability|AoE")
    class UParticleSystem *AoEVFX;

    void ActivateAoEBomb();
    void ExpandAoE();
    void CleanupAoE();

    FTimerHandle AoEExpansionTimer;
    FTimerHandle AoECleanupTimer;

    UPROPERTY()
    TArray<class UParticleSystemComponent *> SpawnedAoEEffects;

    float CurrentAoERadius = 0.f;
    FVector AoECenterPos;
    FVector AoEUpDir;
    FVector AoEForwardAxis;

public:
    FVector InitialLocation;

private:
    float AccumulatedTime = 0.f;
    // FVector InitialLocation;

    UFUNCTION()
    void OnOverlapBegin(UPrimitiveComponent *OverlappedComp, AActor *OtherActor, UPrimitiveComponent *OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult &SweepResult);
};
