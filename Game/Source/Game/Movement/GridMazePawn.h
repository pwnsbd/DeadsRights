#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "../Maze/MazeTypes.h"

#include "GridMazePawn.generated.h"

class ACubeToSphere;
class UMaze;
class UCapsuleComponent;
class UFloatingPawnMovement;

UCLASS()
class GAME_API AGridMazePawn : public APawn
{
	GENERATED_BODY()

public:
	AGridMazePawn();

protected:
	virtual void BeginPlay() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

private:
	void StepNorth();
	void StepSouth();
	void StepWest();
	void StepEast();

	bool TryStep(EMazeDir Dir);

	bool IsOpen(const FMazeCell& Cell, EMazeDir Dir) const;

	bool FindNearestCellToWorld(
		const FVector& WorldPos,
		int32& OutFace,
		int32& OutX,
		int32& OutY
	) const;

	FVector GetCellWorld(int32 Face, int32 X, int32 Y) const;

	FVector GetNorthTangentWorld() const;
	FVector GetEastTangentWorld() const;

private:
	UPROPERTY(VisibleAnywhere)
	UCapsuleComponent* Capsule = nullptr;

	UPROPERTY(VisibleAnywhere)
	UFloatingPawnMovement* Movement = nullptr;

public:
	UPROPERTY(EditAnywhere, Category="Grid")
	ACubeToSphere* Sphere = nullptr;

	UPROPERTY(EditAnywhere, Category="Grid")
	UMaze* Maze = nullptr;

	UPROPERTY(EditAnywhere, Category="Grid")
	float StepHeightOffset = 25.0f;

	UPROPERTY(EditAnywhere, Category="Grid")
	float StepPredictDistance = 15.0f;

	UPROPERTY(EditAnywhere, Category="Grid")
	int32 StartFace = 0;

	UPROPERTY(EditAnywhere, Category="Grid")
	int32 StartX = 0;

	UPROPERTY(EditAnywhere, Category="Grid")
	int32 StartY = 0;

    UPROPERTY(EditDefaultsOnly, Category="Input")
    UInputMappingContext* GridInputContext = nullptr;

    UPROPERTY(EditDefaultsOnly, Category="Input")
    UInputAction* IA_North = nullptr;

    UPROPERTY(EditDefaultsOnly, Category="Input")
    UInputAction* IA_South = nullptr;

    UPROPERTY(EditDefaultsOnly, Category="Input")
    UInputAction* IA_West = nullptr;

    UPROPERTY(EditDefaultsOnly, Category="Input")
    UInputAction* IA_East = nullptr;

private:
	UPROPERTY(VisibleAnywhere, Category="Grid")
	int32 Face = 0;

	UPROPERTY(VisibleAnywhere, Category="Grid")
	int32 X = 0;

	UPROPERTY(VisibleAnywhere, Category="Grid")
	int32 Y = 0;
};