#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MazeRunner.generated.h"

class UStaticMeshComponent;
class ACubeToSphere; // <--- 1. Tells the header this class exists!

// 1. ADD THIS DELEGATE MACRO RIGHT HERE (Above the UCLASS!)
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPathCompletedSignature);

UCLASS()
class GAME_API AMazeRunner : public AActor
{
	GENERATED_BODY()

public:
	AMazeRunner();
	virtual void Tick(float DeltaTime) override;

	// // Hands the calculated A* path to the AI so it can begin moving
	// void SetPath(const TArray<FVector> &NewPath, FVector InSphereCenter);

	// 2. Update the SetPath signature
	void SetPath(const TArray<FVector> &NewLocalPath, ACubeToSphere *InSphereActor);

	// 2. ADD THE EVENT DISPATCHER
	UPROPERTY(BlueprintAssignable)
	FOnPathCompletedSignature OnPathCompleted;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UStaticMeshComponent *MeshComp;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
	float MovementSpeed = 300.0f;

private:
	TArray<FVector> PathToFollow;
	int32 CurrentTargetIndex = 0;
	bool bIsMoving = false;

	// 3. Replace SphereCenter with a pointer to the actual sphere
	ACubeToSphere *TargetSphere = nullptr;
};
