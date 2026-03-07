#pragma once
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Components/StaticMeshComponent.h"

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
class AOrchestrator;

UCLASS()
class GAME_API AGridMazePawn : public APawn
{
	GENERATED_BODY()

public:
	AGridMazePawn();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult) override;

private:
	void StepNorth();
	void StepSouth();
	void StepWest();
	void StepEast();

	bool TryStep(EMazeDir Dir);

	void SnapToCell();

	bool IsOpen(const FMazeCell& Cell, EMazeDir Dir) const;

	bool FindNearestCellToWorld(
		const FVector& WorldPos,
		int32& OutFace,
		int32& OutX,
		int32& OutY
	) const;

	// Pure 2D movement rule
	// If the step goes out of bounds, this maps the coordinate across cube face seams
	/*bool MapAcrossEdge(
		int32 InFace,
		int32 InX,
		int32 InY,
		EMazeDir Dir,
		int32 N,
		int32& OutFace,
		int32& OutX,
		int32& OutY
	) const;*/

	void DumpFaceAscii(int32 FaceToDump) const;
	void DumpCurrentFaceAscii() const;

	void UpdateCameraToSphereCenter();

private:
	UPROPERTY(VisibleAnywhere)
	UCapsuleComponent* Capsule = nullptr;

	UPROPERTY(VisibleAnywhere)
	UFloatingPawnMovement* Movement = nullptr;

	UPROPERTY()
	AOrchestrator* Orchestrator = nullptr;

public:
	UPROPERTY(EditAnywhere, Category="Grid")
	ACubeToSphere* Sphere = nullptr;

	UPROPERTY(EditAnywhere, Category="Grid")
	UMaze* Maze = nullptr;

	UPROPERTY(EditAnywhere, Category="Grid")
	float StepHeightOffset = 25.0f;

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

	UPROPERTY(VisibleAnywhere)
	UStaticMeshComponent* PawnMesh;

	UPROPERTY(VisibleAnywhere)
	USpringArmComponent* SpringArm;

	UPROPERTY(VisibleAnywhere)
	UCameraComponent* Camera;

private:
	UPROPERTY(VisibleAnywhere, Category="Grid")
	int32 Face = 0;

	UPROPERTY(VisibleAnywhere, Category="Grid")
	int32 X = 0;

	UPROPERTY(VisibleAnywhere, Category="Grid")
	int32 Y = 0;
};
