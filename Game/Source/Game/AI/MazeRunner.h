#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "../Artifact/Artifact.h"
#include "MazeRunner.generated.h"

class USkeletalMeshComponent;
class ACubeToSphere;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPathCompletedSignature);

UENUM(BlueprintType)
enum class EAIState : uint8
{
	Idle,
	Hunting,
	Fleeing,
	Escaping,
	Casting
};

UCLASS()
class GAME_API AMazeRunner : public AActor
{
	GENERATED_BODY()

public:
	AMazeRunner();
	virtual void Tick(float DeltaTime) override;
	void SetPath(const TArray<FVector> &NewLocalPath, ACubeToSphere *InSphereActor);

	UPROPERTY(BlueprintAssignable)
	FOnPathCompletedSignature OnPathCompleted;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	EAIState CurrentState = EAIState::Hunting;

	UPROPERTY()
	AArtifact *MyTarget = nullptr;

	FTimerHandle EscapeTimerHandle;

	/** Called when hit by a weapon or beam */
	UFUNCTION(BlueprintCallable, Category = "AI|Health")
	void Die();

	/** Seconds remaining on the escape timer. Returns 0 if not currently escaping. */
	UFUNCTION(BlueprintPure, Category = "AI|Escape")
	float GetEscapeTimeRemaining() const;

	UFUNCTION(BlueprintPure, Category = "Movement")
	bool IsMoving() const { return bIsMoving; }

	void FinishEscape();
	virtual void NotifyActorBeginOverlap(AActor *OtherActor) override;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	USkeletalMeshComponent *MeshComp;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
	float MovementSpeed = 150.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
	float SurfaceOffset = 17.0f;

	virtual void BeginPlay() override;

	UPROPERTY()
	class UMaterialInstanceDynamic *DynamicMat;

	void SetAIColor(FLinearColor NewColor);

	/** Distance at which the AI begins to flee. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
	float FleeThreshold = 300.0f;

	/** Distance at which the AI feels safe enough to stop fleeing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
	float SafeThreshold = 500.0f;

	/** Radius used by the pathfinder to avoid the player. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
	float PathAvoidanceRadius = 400.0f;

private:
	TArray<FVector> PathToFollow;
	int32 CurrentTargetIndex = 0;
	bool bIsMoving = false;
	ACubeToSphere *TargetSphere = nullptr;
	// --- ADD THIS LINE TO FIX THE ERROR ---
	/** Tracks the player's last known location to dynamically update paths. */
	FVector LastPlayerPosForPath = FVector::ZeroVector;

	FTimerHandle RePathTimerHandle;
	/** * Finds the index of the node in the new path that is closest to our current
	 * physical location so we don't "snap" backwards when recalculating.
	 */
	int32 FindClosestPathIndex(const TArray<FVector> &NewPath);
};
