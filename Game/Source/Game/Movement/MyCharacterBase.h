#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "InputAction.h"
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

	// We override CalcCamera to implement a stable overhead sphere-surface camera.
	// This completely bypasses any attached CameraComponent, giving us full control
	// over the view matrix every frame.
	virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult) override;

public:
	// =========================================================================
	// Public API
	// =========================================================================

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

protected:
	// =========================================================================
	// Internal helpers
	// =========================================================================

	// Finds Blueprint-owned components (capsule, mesh, camera).
	void CacheCharacterComponents();

	// ---- Input handlers (bound to Enhanced Input actions) ----
	void HandleMoveForwardInput();
	void HandleMoveBackwardInput();
	void HandleMoveLeftInput();
	void HandleMoveRightInput();

	// Core input path: given a desired world-space tangent direction (W/A/S/D
	// already resolved to world space), resolve it to a maze direction and either
	// attempt the move immediately or queue it if a tween is in progress.
	void HandleMoveInputFromWorldDirection(const FVector& DesiredWorldDir);

	// Returns the centre of the sphere actor in world space.
	FVector GetBasisSphereCenterWorld() const;

	// Returns the outward surface normal at the player's current cell.
	FVector GetCurrentCellSurfaceNormal() const;

	// Returns the "screen up" direction in world space.
	// This is derived from CameraUpHint (the direction the player last moved),
	// projected onto the current cell's tangent plane.  It is the direction
	// that appears as "up on screen" to the player.
	FVector GetCameraScreenUpInWorld() const;

	// Given a desired world-space tangent direction, find the maze direction
	// (N/E/S/W) whose 3D neighbour cell is closest to that direction.
	// This works correctly on all six faces including the poles because it
	// uses actual 3D world positions of neighbour cells, never face-local axes.
	EMazeDir ResolveMazeDirectionFromWorldVector(const FVector& DesiredWorldDir) const;

	// Wall check via maze cell flags.
	bool IsOpen(const FMazeCell& Cell, EMazeDir Dir) const;

	// Try to take one step in the given maze direction.
	// Returns true if the move started; false if a wall blocked it.
	bool TryMoveInMazeDirection(EMazeDir Dir);

	// Immediately snap the character transform to the current (Face, X, Y) cell.
	void SnapCharacterToCurrentCell();

	// Brute-force search for the closest maze cell to a world position.
	bool FindClosestMazeCellToWorldLocation(
		const FVector& WorldPos,
		int32& OutFace, int32& OutX, int32& OutY) const;

	// ---- Smooth arc movement tween ----

	// Start a great-circle arc tween from OldFace/X/Y to NewFace/X/Y.
	void StartMoveTween(
		int32 OldFace, int32 OldX, int32 OldY,
		int32 NewFace, int32 NewX, int32 NewY);

	// Advance the active tween each Tick.
	void UpdateMoveTween(float DeltaSeconds);

	// ---- Cell transform helpers ----

	// Build the full world transform the character should have when resting on
	// the given cell (position above cell centre + rotation aligned to surface).
	UFUNCTION(BlueprintCallable, Category = "Maze", meta = (DisplayName = "Build Character Transform For Cell"))
	FTransform BuildPlacedWorldTransformForCell(int32 InFace, int32 InX, int32 InY) const;

	// Build a right-handed, surface-aligned basis for a cell:
	//   OutUp      = outward sphere normal (surface "up")
	//   OutForward = face-local North (decreasing Y in grid), always derived
	//                from actual neighbour cell positions — robust at poles.
	//   OutRight   = face-local East (increasing X in grid)
	UFUNCTION(BlueprintCallable, Category = "Maze", meta = (DisplayName = "Get Sphere Aligned Basis For Cell"))
	bool GetSphereAlignedBasisForCell(
		int32 InFace, int32 InX, int32 InY,
		FVector& OutForward, FVector& OutRight, FVector& OutUp) const;

	// Draw debug axes for a cell (green = forward, red = right, blue = up).
	UFUNCTION(BlueprintCallable, Category = "Debug")
	void DrawCellBasisDebug(
		int32 InFace, int32 InX, int32 InY,
		float Length = 150.f, float Duration = 5.f) const;

protected:
	// =========================================================================
	// Components
	// =========================================================================

	UPROPERTY(Transient)
	TObjectPtr<UCapsuleComponent> CapsuleComp = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> PawnMeshComp = nullptr;

	// The Blueprint may place a camera component on the character.
	// CalcCamera now overrides everything, so this is cached but not actively
	// used for the view matrix.  It can still be useful for Blueprint effects.
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
	// Logical 2D position (the "ghost player" state)
	// =========================================================================
	// These three values are the ground truth for WHERE the player IS in the
	// maze.  Everything else (3D position, rotation, camera) is derived from
	// them via sphere geometry.

	UPROPERTY(BlueprintReadOnly, Category = "Maze")
	int32 Face = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Maze")
	int32 X = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Maze")
	int32 Y = 0;

	// =========================================================================
	// Tuning parameters
	// =========================================================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze")
	float StepHeightOffset = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze")
	int32 StartFace = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze")
	int32 StartX = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze")
	int32 StartY = 0;

	// How long (seconds) a single cell-to-cell step animation takes.
	// 0.15 gives a snappy but smooth feel.  Increase for slower, deliberate movement.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Movement")
	float StepTweenDuration = 0.15f;

	// How fast the character turns to face the movement direction, in degrees
	// per second.  The position tween always takes StepTweenDuration regardless;
	// rotation runs independently at this rate so a 90° turn takes
	// 90 / CharacterTurnSpeed seconds.
	// 360 °/s  →  90° turn in 0.25 s  (recommended starting value)
	// 540 °/s  →  90° turn in 0.17 s  (snappier)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Movement")
	float CharacterTurnSpeed = 360.f;

	// Distance from the character to the camera (along the sphere surface normal).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Camera")
	float CameraArmLength = 900.f;

	// Vertical field of view for the overhead camera.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze|Camera")
	float CameraFOV = 70.f;

	// =========================================================================
	// Input assets
	// =========================================================================

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> GridInputContext = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> IA_North = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> IA_South = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> IA_West = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> IA_East = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bAutoLogCurrentMazeFace = true;

private:
	// =========================================================================
	// Smooth arc tween state
	// =========================================================================
	// The character moves along a great-circle arc (sphere surface) rather than
	// a straight line (which would cut through the sphere).  See StartMoveTween
	// and UpdateMoveTween for the spherical interpolation math.

	bool  bMoveTweenActive = false;
	float MoveTweenAlpha   = 0.f;    // 0..1 raw progress

	// Rotation runs at its own speed, independent of the position tween.
	// Seconds elapsed since the current tween started, used to drive the
	// character turn at CharacterTurnSpeed °/s regardless of cell distance.
	float RotTweenElapsed  = 0.f;

	// =========================================================================
	// Sphere-rolling movement
	// =========================================================================
	// The character stands at a FIXED world position (CharFixedWorldPos).
	// Instead of tweening the character's position, the SPHERE rotates so the
	// next maze cell arrives under the character.  The net effect is the maze
	// ball rolling smoothly beneath a stationary player — centered on screen
	// every frame, no camera chasing required.
	//
	// WHY this works now (but sphere-rotation failed before):
	//   The old attempt used physics collision for walls.  Wall meshes are
	//   attached to the sphere, so when the sphere rolled the walls moved past
	//   the character's capsule — unreliable at curved seams.
	//   Now walls are blocked purely by maze data (IsOpen / TryFaceTransition),
	//   so the sphere can spin freely without any physics interaction.

	// World-space position where the character always stands.
	// Set once on first SnapCharacterToCurrentCell, then never changed.
	FVector CharFixedWorldPos  = FVector::ZeroVector;
	bool    bCharFixedPosInit  = false;

	// Sphere rotation at the START and TARGET of the current step tween.
	// TweenTargetSphereRot brings cell (Face,X,Y) to CharFixedWorldPos.
	FQuat TweenStartSphereRot  = FQuat::Identity;
	FQuat TweenTargetSphereRot = FQuat::Identity;

	// Movement direction in sphere-LOCAL space, stored so we can recompute
	// the world-space facing direction as the sphere rolls each frame.
	FVector TweenLocalMoveDir  = FVector::ZeroVector;

	// World-space cell centre positions at start and end of the tween.
	// The character sits ABOVE these at StepHeightOffset + capsule half-height.
	FVector TweenFromCellCenter = FVector::ZeroVector;
	FVector TweenToCellCenter   = FVector::ZeroVector;

	// Character world-space rotations at start and end of the tween.
	// Slerped each frame so the character smoothly turns to face the next cell.
	FQuat TweenFromRot = FQuat::Identity;
	FQuat TweenToRot   = FQuat::Identity;

	// Sphere centre cached at the moment the tween starts (the sphere does NOT
	// move during the tween in this approach).
	FVector TweenSphereCenter = FVector::ZeroVector;

	// =========================================================================
	// Stable camera heading
	// =========================================================================
	// CameraUpHint is a world-space tangent vector that represents "screen up"
	// — the direction that appears at the top of the player's screen.
	//
	// It is updated to the movement direction on every successful step.
	// Because it tracks where the player JUST moved, the camera stays oriented
	// in a way that feels natural: moving forward keeps forward at the top of
	// the screen, turning left rotates the camera left, etc.
	//
	// This vector is always kept in the tangent plane of the current cell
	// (projected in GetCameraScreenUpInWorld) so it never has a radial component.
	//
	// Initialized to the face-local North direction on spawn.
	FVector CameraUpHint = FVector::ZeroVector;

	// =========================================================================
	// Persistent camera quaternion  (anti-flip)
	// =========================================================================
	// CalcCamera builds a *desired* camera orientation each frame and slерps
	// this quaternion toward it.  Because the output is always a small angular
	// step from the previous frame, any mathematical discontinuity in the
	// desired orientation (e.g. gimbal-lock at the poles) can never produce a
	// visible snap — the slerp rate bounds how fast the camera can rotate.
	//
	// bCameraQuatInit starts false; on the very first CalcCamera call the
	// quaternion is snapped directly to the desired value so there is no
	// "swoop in" from Identity at startup.
	FQuat CameraQuat     = FQuat::Identity;
	bool  bCameraQuatInit = false;

	// =========================================================================
	// Input queue
	// =========================================================================
	// While a tween is playing the player can press a direction once.  The
	// queued move fires automatically the instant the tween completes.
	// This makes the movement feel responsive rather than requiring precise timing.

	bool     bMoveQueued    = false;
	EMazeDir QueuedDir      = EMazeDir::N;
	FVector  QueuedWorldDir = FVector::ZeroVector; // the tangent dir that produced QueuedDir
};
