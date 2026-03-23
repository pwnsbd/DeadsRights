#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "../Maze/MazeTypes.h"
#include "MyCharacterBase.generated.h"

class AOrchestrator;
class ACubeToSphere;
class UMaze;
class UCapsuleComponent;
class UStaticMeshComponent;
class UCameraComponent;
class UInputComponent;

/**
 * AMyCharacterBase
 *
 * Gravity Walker — the character physically travels along the sphere surface.
 * The sphere never rotates; the character moves from cell to cell along a
 * great-circle arc, always standing upright relative to the surface normal.
 *
 * Controls are camera-relative: W = camera forward, D = camera right.
 * The third-person camera sits behind and above the character with smooth lag.
 */
UCLASS()
class GAME_API AMyCharacterBase : public APawn
{
	GENERATED_BODY()

public:
	AMyCharacterBase();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult) override;

	// =========================================================================
	// Public API
	// =========================================================================
public:
	UFUNCTION(BlueprintCallable, Category = "Maze Setup")
	void InitializeMazeReferences(AOrchestrator* InOrchestrator, ACubeToSphere* InSphere, UMaze* InMaze);

	UFUNCTION(BlueprintCallable, Category = "Maze Setup")
	void RefreshAfterMazeRebuild();

	UFUNCTION(BlueprintCallable, Category = "Maze Spawn", meta = (DisplayName = "Place Character On Cell"))
	FTransform PlaceOnCell(int32 InFace, int32 InX, int32 InY);

	UFUNCTION(BlueprintCallable, Category = "Debug")
	void DumpCurrentMazeFaceAscii() const;

	UFUNCTION(BlueprintCallable, Category = "Debug")
	void DumpMazeFaceAscii(int32 FaceToDump) const;

	UFUNCTION(BlueprintCallable, Category = "Debug")
	void DumpAllMazeFacesAscii() const;

	// =========================================================================
	// Internal helpers
	// =========================================================================
protected:
	void CacheCharacterComponents();

	// ---- Input ----
	void HandleMoveInput(const FInputActionValue& Value);
	void HandleMoveReleased(const FInputActionValue& Value);
	void HandleLookInput(const FInputActionValue& Value);

	// ---- Core movement ----
	// Attempt a step in the given maze direction; returns false if blocked by wall.
	bool TryMove(EMazeDir Dir);

	// Begin a smooth arc tween from old cell to new cell.
	void StartTween(int32 OldFace, int32 OldX, int32 OldY,
	                int32 NewFace, int32 NewX, int32 NewY);

	// Advance the tween each tick.
	void UpdateTween(float DeltaSeconds);

	// Called when tween reaches alpha=1; snaps to final position and fires queue.
	void FinishTween();

	// Snap character to current Face/X/Y immediately (no tween).
	void SnapToCurrentCell();

	// ---- Direction resolution ----
	// Given a world-space tangent direction, return the maze direction (N/E/S/W)
	// whose 3D neighbour cell is geometrically closest to that direction.
	// Works correctly on every face including the poles.
	EMazeDir ResolveDir(const FVector& WorldDir) const;

	// ---- Wall check ----
	bool IsOpen(const FMazeCell& Cell, EMazeDir Dir) const;

	// ---- Sphere helpers ----
	// World-space pivot point of the sphere (Orchestrator actor location).
	FVector GetSphereCenter() const;

	// World-space position of a cell's surface centre (ON the sphere, not above).
	FVector GetCellSurfacePos(int32 InFace, int32 InX, int32 InY) const;

	// World-space position where the character stands above a cell.
	FVector GetCharStandPos(int32 InFace, int32 InX, int32 InY) const;

	// ---- Camera ----
	// Updates CamWorldPos and CamQuat each tick (called from Tick, read by CalcCamera).
	void UpdateCamera(float DeltaSeconds);

	// ---- Misc ----
	bool FindClosestCell(const FVector& WorldPos, int32& OutFace, int32& OutX, int32& OutY) const;

	// =========================================================================
	// Components (cached from Blueprint)
	// =========================================================================
protected:
	UPROPERTY(Transient)
	TObjectPtr<UCapsuleComponent> CapsuleComp = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> PawnMeshComp = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UCameraComponent> CameraComp = nullptr;

	// =========================================================================
	// Runtime references
	// =========================================================================
	UPROPERTY(BlueprintReadOnly, Category = "Maze")
	TObjectPtr<AOrchestrator> Orchestrator = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Maze")
	TObjectPtr<ACubeToSphere> Sphere = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Maze")
	TObjectPtr<UMaze> Maze = nullptr;

	// =========================================================================
	// Logical cell position  (ground truth of WHERE the player is)
	// =========================================================================
	UPROPERTY(BlueprintReadOnly, Category = "Maze")
	int32 Face = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Maze")
	int32 X = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Maze")
	int32 Y = 0;

	// =========================================================================
	// Tuning — Movement
	// =========================================================================
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze")
	float StepHeightOffset = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze")
	int32 StartFace = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze")
	int32 StartX = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze")
	int32 StartY = 0;

	// Duration of a single cell-to-cell step in seconds.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Movement")
	float StepDuration = 0.18f;

	// =========================================================================
	// Tuning — Camera
	// =========================================================================

	// Distance from the character to the camera.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Camera")
	float CameraArmLength = 800.f;

	// Elevation above the tangent plane in degrees (0 = level, 90 = straight above).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Camera")
	float CameraPitchAngle = 60.f;

	// Height above the character's feet the camera looks at (prevents clipping).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Camera")
	float CameraLookAtHeight = 50.f;

	// How fast the camera position catches up to the target (higher = tighter).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Camera")
	float CameraPositionLag = 8.f;

	// How fast the camera orientation catches up (higher = snappier).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Camera")
	float CameraRotationLag = 10.f;

	// Vertical field of view.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Camera")
	float CameraFOV = 70.f;

	// =========================================================================
	// Tuning — Input
	// =========================================================================
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> GridInputContext = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> IA_Move = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> IA_Look = nullptr;

	// Degrees of camera rotation per mouse pixel.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Camera")
	float MouseSensitivity = 0.2f;

	// Set true to invert mouse Y (camera pitches down when mouse moves up).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Camera")
	bool bInvertMouseY = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bAutoLogCurrentMazeFace = false;

	// =========================================================================
	// Private state
	// =========================================================================
private:
	// ---- Arc tween ----
	bool    bTweenActive      = false;
	float   TweenAlpha        = 0.f;

	FVector TweenSphereCenter = FVector::ZeroVector;
	FVector TweenFromNormal   = FVector::ZeroVector; // outward normal at start cell
	FVector TweenToNormal     = FVector::ZeroVector; // outward normal at end cell
	float   TweenRadius       = 0.f;                 // dist from sphere centre to char feet
	FVector TweenMoveDir      = FVector::ZeroVector; // tangential direction of this step (world)

	// ---- Camera state (updated in Tick, read in CalcCamera) ----
	FVector CamWorldPos   = FVector::ZeroVector;
	FQuat   CamQuat       = FQuat::Identity;
	bool    bCamInit      = false;

	// Last direction the character actually stepped in (world-space, tangential).
	// Camera uses this so it never swings when A/D rotates the character in place.
	FVector CamFollowDir  = FVector::ZeroVector;

	// ---- Input queue ----
	// One move may be queued while a tween is in progress. It fires the moment
	// the tween completes, making the movement feel responsive.
	bool     bMoveQueued = false;
	EMazeDir QueuedDir   = EMazeDir::N;
};
