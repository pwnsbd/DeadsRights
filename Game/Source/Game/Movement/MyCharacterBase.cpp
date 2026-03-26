#include "MyCharacterBase.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "UObject/ConstructorHelpers.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"

#include "Orchestrator.h"
#include "../Conversion/CubeToSphere.h"
#include "../Maze/Maze.h"

// =============================================================================
// Anonymous helpers
// =============================================================================

namespace
{
	const TCHAR* DirName(EMazeDir Dir)
	{
		switch (Dir)
		{
		case EMazeDir::N: return TEXT("N");
		case EMazeDir::E: return TEXT("E");
		case EMazeDir::S: return TEXT("S");
		case EMazeDir::W: return TEXT("W");
		}
		return TEXT("?");
	}
}

// =============================================================================
// Constructor
// =============================================================================

AMyCharacterBase::AMyCharacterBase()
{
	PrimaryActorTick.bCanEverTick = true;
	AutoPossessPlayer = EAutoReceiveInput::Disabled;

	static ConstructorHelpers::FObjectFinder<UInputMappingContext> IMCFinder(
		TEXT("/Game/IMC_Controller.IMC_Controller"));
	static ConstructorHelpers::FObjectFinder<UInputAction> MoveFinder(
		TEXT("/Game/IA_Move.IA_Move"));
	static ConstructorHelpers::FObjectFinder<UInputAction> LookFinder(
		TEXT("/Game/IA_Look.IA_Look"));

	if (!GridInputContext && IMCFinder.Succeeded())  GridInputContext = IMCFinder.Object;
	if (!IA_Move       && MoveFinder.Succeeded())   IA_Move          = MoveFinder.Object;
	if (!IA_Look       && LookFinder.Succeeded())   IA_Look          = LookFinder.Object;
}

// =============================================================================
// CacheCharacterComponents
// =============================================================================

void AMyCharacterBase::CacheCharacterComponents()
{
	CapsuleComp = FindComponentByClass<UCapsuleComponent>();
	CameraComp  = FindComponentByClass<UCameraComponent>();

	TArray<UStaticMeshComponent*> Meshes;
	GetComponents<UStaticMeshComponent>(Meshes);
	PawnMeshComp = Meshes.Num() > 0 ? Meshes[0] : nullptr;

	// Disable physics-driven collision so walls don't push the character.
	// All wall blocking is handled through logical IsOpen() checks instead.
	if (CapsuleComp)
	{
		CapsuleComp->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		CapsuleComp->SetCollisionResponseToChannel(ECC_WorldStatic,  ECR_Ignore);
		CapsuleComp->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Ignore);
	}
}

// =============================================================================
// BeginPlay
// =============================================================================

void AMyCharacterBase::BeginPlay()
{
	Super::BeginPlay();
	CacheCharacterComponents();
	RefreshAfterMazeRebuild();
}

// =============================================================================
// Tick
// =============================================================================

void AMyCharacterBase::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateTween(DeltaSeconds);
	UpdateCamera(DeltaSeconds);
}

// =============================================================================
// SetupPlayerInputComponent
// =============================================================================

void AMyCharacterBase::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (ULocalPlayer* LP = PC->GetLocalPlayer())
		{
			if (UEnhancedInputLocalPlayerSubsystem* Sub =
				LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
			{
				if (GridInputContext)
				{
					Sub->ClearAllMappings();
					Sub->AddMappingContext(GridInputContext, 0);
				}
			}
		}
	}

	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		if (IA_Move)
		{
			// Triggered fires every frame while held → smooth hold-to-move.
			// Completed clears the queue so the character stops cleanly on release.
			EIC->BindAction(IA_Move, ETriggerEvent::Triggered,  this, &AMyCharacterBase::HandleMoveInput);
			EIC->BindAction(IA_Move, ETriggerEvent::Completed,  this, &AMyCharacterBase::HandleMoveReleased);
		}

		// Mouse look fires every frame while the mouse is moving.
		if (IA_Look)
			EIC->BindAction(IA_Look, ETriggerEvent::Triggered, this, &AMyCharacterBase::HandleLookInput);
	}
}

// =============================================================================
// CalcCamera  —  reads pre-computed state from UpdateCamera()
// =============================================================================

void AMyCharacterBase::CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult)
{
	OutResult.Location              = CamWorldPos;
	OutResult.Rotation              = CamQuat.Rotator();
	OutResult.FOV                   = CameraFOV;
	OutResult.bConstrainAspectRatio = false;
}

// =============================================================================
// InitializeMazeReferences / RefreshAfterMazeRebuild
// =============================================================================

void AMyCharacterBase::InitializeMazeReferences(
	AOrchestrator* InOrchestrator, ACubeToSphere* InSphere, UMaze* InMaze)
{
	Orchestrator = InOrchestrator;
	Sphere       = InSphere;
	Maze         = InMaze;
}

void AMyCharacterBase::RefreshAfterMazeRebuild()
{
	// Reset all runtime state so a rebuild starts clean.
	bTweenActive = false;
	TweenAlpha   = 0.f;
	bMoveQueued  = false;
	bCamInit     = false;

	CacheCharacterComponents();

	// Auto-locate orchestrator if not wired by Blueprint.
	if (!Orchestrator && GetWorld())
	{
		Orchestrator = Cast<AOrchestrator>(
			UGameplayStatics::GetActorOfClass(GetWorld(), AOrchestrator::StaticClass()));
	}

	if (Orchestrator)
	{
		InitializeMazeReferences(
			Orchestrator, Orchestrator->SphereActor, Orchestrator->GetMaze());
	}

	if (!Sphere || !Maze)
	{
		UE_LOG(LogTemp, Error, TEXT("MyCharacterBase: missing Sphere or Maze after refresh"));
		return;
	}

	// Pick starting cell — prefer a random open cell from the orchestrator.
	bool bFoundCell = false;
	if (Orchestrator)
	{
		const float CapsuleHH = CapsuleComp ? CapsuleComp->GetScaledCapsuleHalfHeight() : 88.f;
		FTransform SpawnTransform;
		if (Orchestrator->GetRandomSpawnTransform(SpawnTransform, CapsuleHH))
		{
			int32 SF, SX, SY;
			if (FindClosestCell(SpawnTransform.GetLocation(), SF, SX, SY))
			{
				Face = SF; X = SX; Y = SY;
				bFoundCell = true;
			}
		}
	}

	if (!bFoundCell)
	{
		Face = StartFace; X = StartX; Y = StartY;
	}

	SnapToCurrentCell();

	// Seed the camera follow direction so it has a valid value before the first step.
	{
		const FVector SurfNorm = (GetActorLocation() - GetSphereCenter()).GetSafeNormal();
		FVector Seed = FVector::VectorPlaneProject(GetActorQuat().GetAxisX(), SurfNorm).GetSafeNormal();
		if (Seed.IsNearlyZero())
			Seed = FVector::VectorPlaneProject(FVector::ForwardVector, SurfNorm).GetSafeNormal();
		if (!Seed.IsNearlyZero())
			CamFollowDir = Seed;
	}

	if (bAutoLogCurrentMazeFace) DumpCurrentMazeFaceAscii();

	// Possess, set view target, and capture the mouse for GTA-style look.
	if (UWorld* W = GetWorld())
	{
		if (APlayerController* PC = W->GetFirstPlayerController())
		{
			if (PC->GetPawn() != this) PC->Possess(this);
			PC->SetViewTargetWithBlend(this, 0.f);
			PC->SetInputMode(FInputModeGameOnly());
			PC->SetShowMouseCursor(false);
		}
	}
}

// =============================================================================
// PlaceOnCell
// =============================================================================

FTransform AMyCharacterBase::PlaceOnCell(int32 InFace, int32 InX, int32 InY)
{
	Face = InFace; X = InX; Y = InY;
	SnapToCurrentCell();
	return GetActorTransform();
}

// =============================================================================
// Input handler
// =============================================================================

void AMyCharacterBase::HandleMoveInput(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>(); // X = right (D/A), Y = forward (W/S)
	if (Axis.IsNearlyZero()) return;

	const FVector CharPos    = GetActorLocation();
	const FVector SurfNormal = (CharPos - GetSphereCenter()).GetSafeNormal();

	// Screen directions come from the camera's own orientation quaternion.
	// CamQuat is built with MakeFromXZ(CamFwd, SurfaceUp), so:
	//   GetAxisX() = camera forward  = what W should move toward
	//   GetAxisY() = camera right    = what D should move toward
	// Both are projected to the tangent plane so they stay on the sphere surface.
	FVector ScreenFwd   = FVector::VectorPlaneProject(CamQuat.GetAxisX(), SurfNormal).GetSafeNormal();
	FVector ScreenRight = FVector::VectorPlaneProject(CamQuat.GetAxisY(), SurfNormal).GetSafeNormal();

	// Fallback if camera hasn't initialised yet.
	if (!bCamInit || ScreenFwd.IsNearlyZero())
	{
		ScreenFwd   = FVector::VectorPlaneProject(CamFollowDir,                        SurfNormal).GetSafeNormal();
		ScreenRight = FVector::VectorPlaneProject(FVector::CrossProduct(SurfNormal, ScreenFwd), SurfNormal).GetSafeNormal();
	}

	// Dominant axis so exactly one direction fires per press.
	FVector MoveDir;
	if (FMath::Abs(Axis.Y) >= FMath::Abs(Axis.X))
		MoveDir = ScreenFwd * FMath::Sign(Axis.Y);
	else
		MoveDir = ScreenRight * FMath::Sign(Axis.X);

	// Snap camera to nearest 90° cardinal so diagonal yaw never causes ambiguous
	// direction resolution.  Snap target is computed relative to character forward.
	{
		const FVector CharFwd   = FVector::VectorPlaneProject(
		                              GetActorQuat().GetAxisX(), SurfNormal).GetSafeNormal();
		const FVector CharRight = FVector::CrossProduct(SurfNormal, CharFwd).GetSafeNormal();

		const FVector Cardinals[4] = { CharFwd, CharRight, -CharFwd, -CharRight };
		float   BestDot = -2.f;
		FVector BestDir = CharFwd;
		for (const FVector& C : Cardinals)
		{
			const float D = FVector::DotProduct(CamFollowDir, C);
			if (D > BestDot) { BestDot = D; BestDir = C; }
		}
		CamSnapTarget = BestDir;
		bCamSnapping  = true;
	}

	TryMove(ResolveDir(MoveDir));
}

// =============================================================================
// HandleMoveReleased  —  key up: cancel any pending queued step
// =============================================================================

void AMyCharacterBase::HandleMoveReleased(const FInputActionValue& /*Value*/)
{
	bMoveQueued = false;
}

// =============================================================================
// HandleLookInput  —  mouse orbit
// =============================================================================

void AMyCharacterBase::HandleLookInput(const FInputActionValue& Value)
{
	const FVector2D Delta = Value.Get<FVector2D>(); // X = horizontal, Y = vertical
	if (Delta.IsNearlyZero()) return;

	// Mouse overrides any in-progress cardinal snap.
	bCamSnapping = false;

	const FVector SurfNorm = (GetActorLocation() - GetSphereCenter()).GetSafeNormal();

	// --- Horizontal: orbit CamFollowDir around the surface normal ---
	// Negative sign: mouse right → camera sweeps clockwise from above → character
	// appears to turn left (same convention as GTA / most TPS games).
	if (FMath::Abs(Delta.X) > KINDA_SMALL_NUMBER)
	{
		const FQuat   YawQ        = FQuat(SurfNorm,
		                                  FMath::DegreesToRadians(-Delta.X * MouseSensitivity));
		const FVector Rotated     = YawQ.RotateVector(CamFollowDir);
		const FVector Reprojected = FVector::VectorPlaneProject(Rotated, SurfNorm).GetSafeNormal();
		if (!Reprojected.IsNearlyZero())
			CamFollowDir = Reprojected;
	}

	// --- Vertical: adjust elevation angle ---
	// Mouse up (positive Delta.Y) raises the camera by default; flip with bInvertMouseY.
	if (FMath::Abs(Delta.Y) > KINDA_SMALL_NUMBER)
	{
		const float Sign  = bInvertMouseY ? 1.f : -1.f;
		CameraPitchAngle  = FMath::Clamp(
		                        CameraPitchAngle + Sign * Delta.Y * MouseSensitivity,
		                        5.f, 85.f);
	}
}

// =============================================================================
// TryMove
// =============================================================================

bool AMyCharacterBase::TryMove(EMazeDir Dir)
{
	// Queue one move while a tween is in progress.
	if (bTweenActive)
	{
		bMoveQueued = true;
		QueuedDir   = Dir;
		return false;
	}

	if (!Maze || !Sphere) return false;

	// Wall check.
	const FMazeCell& Cell = Maze->GetCell(Face, X, Y);
	if (!IsOpen(Cell, Dir)) return false;

	// Locate neighbour (handles face transitions).
	const FMazeNode NeighborNode = Maze->GetNeighborCell(FMazeNode(Face, X, Y), Dir, true);
	if (!Maze->IsValid(NeighborNode.Face, NeighborNode.X, NeighborNode.Y)) return false;

	const int32 NewFace = NeighborNode.Face;
	const int32 NewX    = NeighborNode.X;
	const int32 NewY    = NeighborNode.Y;

	StartTween(Face, X, Y, NewFace, NewX, NewY);

	// Update logical position immediately so queued moves resolve correctly.
	Face = NewFace; X = NewX; Y = NewY;
	return true;
}

// =============================================================================
// StartTween
// =============================================================================

void AMyCharacterBase::StartTween(
	int32 OldFace, int32 OldX, int32 OldY,
	int32 NewFace, int32 NewX, int32 NewY)
{
	TweenSphereCenter = GetSphereCenter();

	const FVector OldSurfPos = GetCellSurfacePos(OldFace, OldX, OldY);
	const FVector NewSurfPos = GetCellSurfacePos(NewFace, NewX, NewY);

	TweenFromNormal = (OldSurfPos - TweenSphereCenter).GetSafeNormal();
	TweenToNormal   = (NewSurfPos - TweenSphereCenter).GetSafeNormal();

	// Radius from sphere centre to where the character's feet stand.
	TweenRadius = FVector::Dist(TweenSphereCenter, GetCharStandPos(OldFace, OldX, OldY));

	// Tangential direction of this step: project ToNormal onto the tangent plane
	// at FromNormal.  This is the "forward" direction the character faces during
	// the step.
	const float Dot = FVector::DotProduct(TweenToNormal, TweenFromNormal);
	TweenMoveDir = (TweenToNormal - Dot * TweenFromNormal).GetSafeNormal();

	TweenAlpha   = 0.f;
	bTweenActive = true;
}

// =============================================================================
// UpdateTween
// =============================================================================

void AMyCharacterBase::UpdateTween(float DeltaSeconds)
{
	if (!bTweenActive) return;

	TweenAlpha = FMath::Min(TweenAlpha + DeltaSeconds / FMath::Max(StepDuration, 0.001f), 1.f);

	// Ease-in/out so starts and stops feel snappy but not jarring.
	const float SmoothAlpha = FMath::InterpEaseInOut(0.f, 1.f, TweenAlpha, 2.f);

	// Spherical arc: rotate FromNormal toward ToNormal by SmoothAlpha.
	const FQuat  ArcQuat       = FQuat::FindBetweenNormals(TweenFromNormal, TweenToNormal);
	const FVector CurrentNormal = FQuat::Slerp(FQuat::Identity, ArcQuat, SmoothAlpha)
	                              .RotateVector(TweenFromNormal);

	// Character always stands at TweenRadius above the sphere centre along the current normal.
	SetActorLocation(TweenSphereCenter + CurrentNormal * TweenRadius);

	// Character rotation: Up = surface normal, Forward = step direction.
	const FVector CharUp  = CurrentNormal;
	const FVector CharFwd = FVector::VectorPlaneProject(TweenMoveDir, CharUp).GetSafeNormal();

	if (!CharFwd.IsNearlyZero())
	{
		SetActorRotation(FRotationMatrix::MakeFromXZ(CharFwd, CharUp).ToQuat());
	}

	if (TweenAlpha >= 1.f)
	{
		FinishTween();
	}
}

// =============================================================================
// FinishTween
// =============================================================================

void AMyCharacterBase::FinishTween()
{
	bTweenActive = false;

	// Parallel-transport the camera orbit direction from the old surface normal to
	// the new one.  This keeps the player's sense of screen direction consistent
	// across face seams: whatever felt like "right" before the step still feels
	// like "right" after it.
	if (!CamFollowDir.IsNearlyZero()
	    && !TweenFromNormal.IsNearlyZero()
	    && !TweenToNormal.IsNearlyZero())
	{
		const FQuat   Arc         = FQuat::FindBetweenNormals(TweenFromNormal, TweenToNormal);
		const FVector Transported = FVector::VectorPlaneProject(
		                                Arc.RotateVector(CamFollowDir),
		                                TweenToNormal).GetSafeNormal();
		if (!Transported.IsNearlyZero())
			CamFollowDir = Transported;
	}

	SnapToCurrentCell(); // Face/X/Y already point to the new cell

	if (bAutoLogCurrentMazeFace) DumpCurrentMazeFaceAscii();

	if (bMoveQueued)
	{
		bMoveQueued = false;
		TryMove(QueuedDir);
	}
}

// =============================================================================
// SnapToCurrentCell
// =============================================================================

void AMyCharacterBase::SnapToCurrentCell()
{
	if (!Sphere) return;

	SetActorLocation(GetCharStandPos(Face, X, Y));

	// Preserve character forward, but re-align Up to the new surface normal.
	const FVector SurfNormal = (GetCellSurfacePos(Face, X, Y) - GetSphereCenter()).GetSafeNormal();
	FVector CharFwd = FVector::VectorPlaneProject(GetActorQuat().GetAxisX(), SurfNormal).GetSafeNormal();

	if (CharFwd.IsNearlyZero())
	{
		// No valid facing yet — use world forward as a seed.
		CharFwd = FVector::VectorPlaneProject(FVector::ForwardVector, SurfNormal).GetSafeNormal();
	}

	if (!CharFwd.IsNearlyZero())
	{
		SetActorRotation(FRotationMatrix::MakeFromXZ(CharFwd, SurfNormal).ToQuat());
	}
}

// =============================================================================
// ResolveDir
// =============================================================================
//
// Given a world-space direction on the tangent plane, return the maze direction
// (N/E/S/W) whose 3D neighbour cell lies closest to that direction.
// Uses actual cell world positions — no face-local rotation tables — so it works
// identically on all 6 faces including the poles.

EMazeDir AMyCharacterBase::ResolveDir(const FVector& WorldDir) const
{
	if (!Sphere || !Maze) return EMazeDir::N;

	const FVector CellCenter = GetCellSurfacePos(Face, X, Y);
	const FVector SurfNormal = (CellCenter - GetSphereCenter()).GetSafeNormal();

	// Project input direction onto the tangent plane.
	const FVector TangDir = FVector::VectorPlaneProject(WorldDir, SurfNormal).GetSafeNormal();
	if (TangDir.IsNearlyZero()) return EMazeDir::N;

	float    BestDot = -2.f;
	EMazeDir BestDir = EMazeDir::N;

	for (EMazeDir Dir : { EMazeDir::N, EMazeDir::E, EMazeDir::S, EMazeDir::W })
	{
		const FMazeNode Neighbor = Maze->GetNeighborCell(FMazeNode(Face, X, Y), Dir, true);
		if (!Maze->IsValid(Neighbor.Face, Neighbor.X, Neighbor.Y)) continue;

		const FVector NeighborPos = GetCellSurfacePos(Neighbor.Face, Neighbor.X, Neighbor.Y);
		const FVector ToNeighbor  = (NeighborPos - CellCenter).GetSafeNormal();

		const float Dot = FVector::DotProduct(TangDir, ToNeighbor);
		if (Dot > BestDot)
		{
			BestDot = Dot;
			BestDir = Dir;
		}
	}

	return BestDir;
}

// =============================================================================
// IsOpen
// =============================================================================

bool AMyCharacterBase::IsOpen(const FMazeCell& Cell, EMazeDir Dir) const
{
	switch (Dir)
	{
	case EMazeDir::N: return Cell.OpenN;
	case EMazeDir::E: return Cell.OpenE;
	case EMazeDir::S: return Cell.OpenS;
	case EMazeDir::W: return Cell.OpenW;
	}
	return false;
}

// =============================================================================
// Sphere helpers
// =============================================================================

FVector AMyCharacterBase::GetSphereCenter() const
{
	// Use Orchestrator location — the fixed rotation pivot.
	// Sphere (child actor) drifts when the Orchestrator rotates.
	if (Orchestrator) return Orchestrator->GetActorLocation();
	if (Sphere)       return Sphere->GetActorLocation();
	return FVector::ZeroVector;
}

FVector AMyCharacterBase::GetCellSurfacePos(int32 InFace, int32 InX, int32 InY) const
{
	if (Sphere) return Sphere->GetCellCenterWorld(InFace, InX, InY);
	return FVector::ZeroVector;
}

FVector AMyCharacterBase::GetCharStandPos(int32 InFace, int32 InX, int32 InY) const
{
	const FVector SurfPos   = GetCellSurfacePos(InFace, InX, InY);
	const FVector SphCenter = GetSphereCenter();
	const FVector Normal    = (SurfPos - SphCenter).GetSafeNormal();
	return SurfPos + Normal * StepHeightOffset;
}

// =============================================================================
// UpdateCamera
// =============================================================================
//
// DESIGN
// ------
// The camera sits behind and above the character in a classic third-person arc.
//
// Position lag    — FMath::VInterpTo smooths the world position each frame.
// Rotation lag    — FQuat::Slerp with a short-arc guard prevents flipping.
// Screen-up       — Surface normal projected perpendicular to the look direction.
//                   This eliminates roll at the poles without any special cases.

void AMyCharacterBase::UpdateCamera(float DeltaSeconds)
{
	if (!Sphere) return;

	const FVector CharPos   = GetActorLocation();
	const FVector SphCenter = GetSphereCenter();
	const FVector SurfNorm  = (CharPos - SphCenter).GetSafeNormal();

	// ---- Target position: behind and above character ----
	const float PitchRad  = FMath::DegreesToRadians(FMath::Clamp(CameraPitchAngle, 0.f, 89.f));
	const float HorizDist = CameraArmLength * FMath::Cos(PitchRad);
	const float VertDist  = CameraArmLength * FMath::Sin(PitchRad);

	// ---- Cardinal snap: smoothly rotate CamFollowDir toward the nearest 90° ----
	if (bCamSnapping && !CamSnapTarget.IsNearlyZero())
	{
		const FVector SnapDir = FVector::VectorPlaneProject(CamSnapTarget, SurfNorm).GetSafeNormal();
		if (!SnapDir.IsNearlyZero())
		{
			// Angle remaining between current direction and target.
			const float AngleRad = FMath::Acos(
			    FMath::Clamp(FVector::DotProduct(CamFollowDir, SnapDir), -1.f, 1.f));
			const float StepRad  = FMath::DegreesToRadians(CameraSnapSpeed) * DeltaSeconds;
			const float T        = (AngleRad > KINDA_SMALL_NUMBER)
			                       ? FMath::Min(StepRad / AngleRad, 1.f)
			                       : 1.f;

			const FQuat Arc = FQuat::FindBetweenNormals(CamFollowDir, SnapDir);
			CamFollowDir = FVector::VectorPlaneProject(
			    FQuat::Slerp(FQuat::Identity, Arc, T).RotateVector(CamFollowDir),
			    SurfNorm).GetSafeNormal();

			if (T >= 1.f)
			{
				CamFollowDir = SnapDir;
				bCamSnapping = false;
			}
		}
	}

	// Camera offset direction — updated by snap above or freely by mouse.
	FVector FollowDir = FVector::VectorPlaneProject(CamFollowDir, SurfNorm).GetSafeNormal();
	if (FollowDir.IsNearlyZero())
		FollowDir = FVector::VectorPlaneProject(FVector::ForwardVector, SurfNorm).GetSafeNormal();

	const FVector TargetCamPos  = CharPos - FollowDir * HorizDist + SurfNorm * VertDist;
	const FVector LookTarget    = CharPos + SurfNorm * CameraLookAtHeight;

	// ---- Target orientation ----
	const FVector CamFwd = (LookTarget - TargetCamPos).GetSafeNormal();

	// Screen-up: surface normal projected perpendicular to the look direction.
	FVector DesiredUp = FVector::VectorPlaneProject(SurfNorm, CamFwd).GetSafeNormal();
	if (DesiredUp.IsNearlyZero())
		DesiredUp = FVector::VectorPlaneProject(FVector::UpVector, CamFwd).GetSafeNormal();

	const FQuat TargetQuat = FRotationMatrix::MakeFromXZ(CamFwd, DesiredUp).ToQuat();

	// ---- Snap on first frame, interpolate afterwards ----
	if (!bCamInit)
	{
		CamWorldPos = TargetCamPos;
		CamQuat     = TargetQuat;
		bCamInit    = true;
		return;
	}

	// Smooth position.
	CamWorldPos = FMath::VInterpTo(CamWorldPos, TargetCamPos, DeltaSeconds, CameraPositionLag);

	// Short-arc guard: negate TargetQuat if it is on the far hemisphere.
	const FQuat AlignedTarget = ((CamQuat | TargetQuat) >= 0.f)
	                            ? TargetQuat
	                            : FQuat(-TargetQuat.X, -TargetQuat.Y,
	                                    -TargetQuat.Z, -TargetQuat.W);

	const float RotAlpha = FMath::Clamp(DeltaSeconds * CameraRotationLag, 0.f, 1.f);
	CamQuat = FQuat::Slerp(CamQuat, AlignedTarget, RotAlpha);
	CamQuat.Normalize();

	if (bShowCameraDebug)
	{
		// Yaw: signed angle between CamFollowDir and character forward on the tangent plane.
		// 0° = camera directly behind, ±180° = camera directly in front.
		const FVector CharFwd = FVector::VectorPlaneProject(
		                            GetActorQuat().GetAxisX(), SurfNorm).GetSafeNormal();
		const float   CosYaw  = FMath::Clamp(FVector::DotProduct(FollowDir, CharFwd), -1.f, 1.f);
		const float   YawSign = FVector::DotProduct(
		                            FVector::CrossProduct(FollowDir, CharFwd), SurfNorm) >= 0.f
		                        ? 1.f : -1.f;
		const float   YawDeg  = YawSign * FMath::RadiansToDegrees(FMath::Acos(CosYaw));

		const FString Msg = FString::Printf(
		    TEXT("Cam Pitch: %.1f°   Cam Yaw: %.1f°"), CameraPitchAngle, YawDeg);

		if (GEngine)
			GEngine->AddOnScreenDebugMessage(42, 0.f, FColor::Cyan, Msg);
	}
}

// =============================================================================
// FindClosestCell
// =============================================================================

bool AMyCharacterBase::FindClosestCell(
	const FVector& WorldPos, int32& OutFace, int32& OutX, int32& OutY) const
{
	if (!Sphere || !Maze) return false;

	float BestDistSq = FLT_MAX;
	bool  bFound     = false;
	const int32 CPF  = Maze->CellsPerFace;

	for (int32 F = 0; F < 6; ++F)
	{
		for (int32 CX = 0; CX < CPF; ++CX)
		{
			for (int32 CY = 0; CY < CPF; ++CY)
			{
				const float DistSq = FVector::DistSquared(WorldPos,
				                     Sphere->GetCellCenterWorld(F, CX, CY));
				if (DistSq < BestDistSq)
				{
					BestDistSq = DistSq;
					OutFace = F; OutX = CX; OutY = CY;
					bFound = true;
				}
			}
		}
	}

	return bFound;
}

// =============================================================================
// Debug dump helpers
// =============================================================================

void AMyCharacterBase::DumpCurrentMazeFaceAscii() const
{
	DumpMazeFaceAscii(Face);
}

void AMyCharacterBase::DumpMazeFaceAscii(int32 FaceToDump) const
{
	if (!Maze) return;

	const int32 CPF = Maze->CellsPerFace;
	UE_LOG(LogTemp, Log, TEXT("=== Face %d ==="), FaceToDump);

	for (int32 Row = 0; Row < CPF; ++Row)
	{
		FString Line;
		for (int32 Col = 0; Col < CPF; ++Col)
		{
			const FMazeCell& Cell = Maze->GetCell(FaceToDump, Col, Row);

			bool bPlayer = (FaceToDump == Face && Col == X && Row == Y);
			TCHAR Ch = bPlayer ? TEXT('@') : TEXT('.');

			if (!Cell.OpenN) Ch = (bPlayer ? TEXT('@') : TEXT('#'));
			Line.AppendChar(Ch);
			Line.AppendChar(Cell.OpenE ? TEXT(' ') : TEXT('|'));
		}
		UE_LOG(LogTemp, Log, TEXT("%s"), *Line);

		// Draw South walls for this row.
		FString WallLine;
		for (int32 Col = 0; Col < CPF; ++Col)
		{
			const FMazeCell& Cell = Maze->GetCell(FaceToDump, Col, Row);
			WallLine.AppendChar(Cell.OpenS ? TEXT(' ') : TEXT('-'));
			WallLine.AppendChar(TEXT(' '));
		}
		UE_LOG(LogTemp, Log, TEXT("%s"), *WallLine);
	}
}

void AMyCharacterBase::DumpAllMazeFacesAscii() const
{
	for (int32 F = 0; F < 6; ++F)
	{
		DumpMazeFaceAscii(F);
	}
}
