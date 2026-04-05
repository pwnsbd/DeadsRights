#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MazeRunner.generated.h"

class UStaticMeshComponent;
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
	class AStaticMeshActor *MyTarget = nullptr;

	FTimerHandle EscapeTimerHandle;

	void FinishEscape();
	virtual void NotifyActorBeginOverlap(AActor *OtherActor) override;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UStaticMeshComponent *MeshComp;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
	float MovementSpeed = 150.0f;

	virtual void BeginPlay() override;

	UPROPERTY()
	class UMaterialInstanceDynamic *DynamicMat;

	void SetAIColor(FLinearColor NewColor);

private:
	TArray<FVector> PathToFollow;
	int32 CurrentTargetIndex = 0;
	bool bIsMoving = false;
	ACubeToSphere *TargetSphere = nullptr;
	// --- ADD THIS LINE TO FIX THE ERROR ---
	/** Tracks the player's last known location to dynamically update paths. */
	FVector LastPlayerPosForPath = FVector::ZeroVector;
	/** * Finds the index of the node in the new path that is closest to our current
	 * physical location so we don't "snap" backwards when recalculating.
	 */
	int32 FindClosestPathIndex(const TArray<FVector> &NewPath);
};
