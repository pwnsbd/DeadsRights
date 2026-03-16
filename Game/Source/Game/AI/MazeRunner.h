#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MazeRunner.generated.h"

class UStaticMeshComponent;

UCLASS()
class GAME_API AMazeRunner : public AActor
{
	GENERATED_BODY()

public:
	AMazeRunner();
	virtual void Tick(float DeltaTime) override;

	// Hands the calculated A* path to the AI so it can begin moving
	void SetPath(const TArray<FVector> &NewPath, FVector InSphereCenter);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UStaticMeshComponent *MeshComp;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
	float MovementSpeed = 300.0f;

private:
	TArray<FVector> PathToFollow;
	int32 CurrentTargetIndex = 0;
	FVector SphereCenter;
	bool bIsMoving = false;
};
