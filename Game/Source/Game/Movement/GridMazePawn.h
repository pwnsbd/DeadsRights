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
class AArtifact;

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

	EMazeDir GetScreenRelativeDir(const FVector& ScreenVectorWorld) const;
	bool TryStep(EMazeDir Dir);

	void SnapToCell();

	bool IsOpen(const FMazeCell& Cell, EMazeDir Dir) const;

	bool FindNearestCellToWorld(
		const FVector& WorldPos,
		int32& OutFace,
		int32& OutX,
		int32& OutY
	) const;

	void DumpFaceAscii(int32 FaceToDump) const;
	void DumpCurrentFaceAscii() const;

	void UpdateCameraToSphereCenter();

	// new center-to-center movement
	bool bStepTweenActive = false;
	float StepTweenElapsed = 0.f;

	UPROPERTY(EditAnywhere, Category="Grid|Movement")
	float StepTweenDuration = 0.12f;

	FVector StepTweenStartLocation = FVector::ZeroVector;
	FVector StepTweenTargetLocation = FVector::ZeroVector;
	FVector StepTweenSphereCenter = FVector::ZeroVector;

	FRotator StepTweenStartRotation = FRotator::ZeroRotator;
	FRotator StepTweenTargetRotation = FRotator::ZeroRotator;

	void BeginStepTween(int32 OldFace, int32 OldX, int32 OldY, int32 NewFace, int32 NewX, int32 NewY);
	void UpdateStepTween(float DeltaSeconds);
	FVector BuildPlacedWorldLocationForCell(int32 InFace, int32 InX, int32 InY) const;
	FRotator BuildPlacedWorldRotationForCell(int32 InFace, int32 InX, int32 InY) const;
	FVector GetBasisSphereCenterWorld() const;

private:
	UPROPERTY(VisibleAnywhere)
	UCapsuleComponent* Capsule = nullptr;

	UPROPERTY(VisibleAnywhere)
	UFloatingPawnMovement* Movement = nullptr;

	UPROPERTY()
	AOrchestrator* Orchestrator = nullptr;

public:
	UFUNCTION(BlueprintCallable, Category="Grid")
	void RefreshAfterMazeRebuild();

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




	public:
		UFUNCTION(BlueprintCallable, Category="Artifacts")
		bool AddArtifactToInventory(AArtifact* Artifact);

		UFUNCTION(BlueprintCallable, Category="Artifacts")
		bool UseArtifactInSlot(int32 SlotIndex);

		UFUNCTION(BlueprintPure, Category="Artifacts")
		int32 GetArtifactCount() const;

	private:
		void UseArtifactSlot1();
		void UseArtifactSlot2();
		void UseArtifactSlot3();
		void UseArtifactSlot4();

		void UpdateArtifactCarryVisuals();
		void LogInventoryState(const TCHAR* Context) const;
		int32 FindFirstEmptyArtifactSlot() const;
		
private:
	UPROPERTY(VisibleAnywhere, Category="Grid")
	int32 Face = 0;

	UPROPERTY(VisibleAnywhere, Category="Grid")
	int32 X = 0;

	UPROPERTY(VisibleAnywhere, Category="Grid")
	int32 Y = 0;

	UPROPERTY(EditAnywhere, Category="Artifacts")
	int32 MaxArtifacts = 4;

	UPROPERTY(EditAnywhere, Category="Artifacts")
	FVector ArtifactCarryBaseOffset = FVector(0.f, 0.f, 120.f);

	UPROPERTY(EditAnywhere, Category="Artifacts")
	float ArtifactCarrySpacing = 35.f;

	UPROPERTY(EditAnywhere, Category="Artifacts")
	FVector ArtifactCarryScale = FVector(0.35f, 0.35f, 0.35f);

	UPROPERTY(VisibleAnywhere, Category="Artifacts")
	TArray<TObjectPtr<AArtifact>> InventoryArtifacts;

	float InventoryLogTimer = 0.f;

	int32 GetPoleWedge(int32 InX, int32 InY) const;
	EMazeDir RemapPoleInput(EMazeDir BaseDir) const;
};