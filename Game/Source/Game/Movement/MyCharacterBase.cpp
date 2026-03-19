#include "MyCharacterBase.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "UObject/ConstructorHelpers.h"
#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"
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

	// Fall back to the existing input assets so Blueprint sub-classes that have
	// not yet set their overrides still work out of the box.
	static ConstructorHelpers::FObjectFinder<UInputMappingContext> GridInputContextFinder(
		TEXT("/Game/IMC_Controller.IMC_Controller"));
	static ConstructorHelpers::FObjectFinder<UInputAction> NorthFinder(TEXT("/Game/IA_GridNorth.IA_GridNorth"));
	static ConstructorHelpers::FObjectFinder<UInputAction> SouthFinder(TEXT("/Game/IA_GridSouth.IA_GridSouth"));
	static ConstructorHelpers::FObjectFinder<UInputAction> WestFinder (TEXT("/Game/IA_GridWest.IA_GridWest"));
	static ConstructorHelpers::FObjectFinder<UInputAction> EastFinder (TEXT("/Game/IA_GridEast.IA_GridEast"));

	if (!GridInputContext && GridInputContextFinder.Succeeded()) GridInputContext = GridInputContextFinder.Object;
	if (!IA_North && NorthFinder.Succeeded()) IA_North = NorthFinder.Object;
	if (!IA_South && SouthFinder.Succeeded()) IA_South = SouthFinder.Object;
	if (!IA_West  && WestFinder.Succeeded()) IA_West  = WestFinder .Object;
	if (!IA_East  && EastFinder .Succeeded()) IA_East  = EastFinder .Object;
}

// =============================================================================
// CacheCharacterComponents
// =============================================================================

void AMyCharacterBase::CacheCharacterComponents()
{
	CapsuleComp  = FindComponentByClass<UCapsuleComponent>();
	CameraComp   = FindComponentByClass<UCameraComponent>();

	TArray<UStaticMeshComponent*> Meshes;
	GetComponents<UStaticMeshComponent>(Meshes);
	PawnMeshComp = Meshes.Num() > 0 ? Meshes[0] : nullptr;

	// -------------------------------------------------------------------------
	// Disable physical collision between the capsule and wall meshes.
	//
	// WHY: The wall geometry on a spherified cube is curved and thin.  Unreal's
	// physics engine struggles to resolve capsule collisions against such shapes
	// reliably — the character gets pushed in random directions or tunnels through.
	//
	// Instead, we gate all movement through the maze logical data (IsOpen checks).
	// The capsule keeps QueryOnly so it still interacts with triggers/overlaps.
	// -------------------------------------------------------------------------
	if (CapsuleComp)
	{
		CapsuleComp->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		CapsuleComp->SetCollisionResponseToChannel(ECC_WorldStatic,  ECR_Ignore);
		CapsuleComp->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Ignore);
	}
}

// =============================================================================
// BeginPlay / Tick / Input setup
// =============================================================================

void AMyCharacterBase::BeginPlay()
{
	Super::BeginPlay();
	CacheCharacterComponents();
	RefreshAfterMazeRebuild();
}

void AMyCharacterBase::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateMoveTween(DeltaSeconds);
}

void AMyCharacterBase::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (ULocalPlayer* LP = PC->GetLocalPlayer())
		{
			if (UEnhancedInputLocalPlayerSubsystem* Sub = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
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
		if (IA_North) EIC->BindAction(IA_North, ETriggerEvent::Started, this, &AMyCharacterBase::HandleMoveForwardInput);
		if (IA_South) EIC->BindAction(IA_South, ETriggerEvent::Started, this, &AMyCharacterBase::HandleMoveBackwardInput);
		if (IA_West)  EIC->BindAction(IA_West,  ETriggerEvent::Started, this, &AMyCharacterBase::HandleMoveLeftInput);
		if (IA_East)  EIC->BindAction(IA_East,  ETriggerEvent::Started, this, &AMyCharacterBase::HandleMoveRightInput);
	}
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
	// Reset movement state on every rebuild so we start clean.
	bMoveTweenActive  = false;
	MoveTweenAlpha    = 0.f;
	bMoveQueued       = false;
	CameraUpHint      = FVector::ZeroVector;
	bCameraQuatInit   = false; // force CalcCamera to snap on the next frame
	bCharFixedPosInit = false; // recompute fixed position from the new start cell

	CacheCharacterComponents();

	// Auto-locate the orchestrator if not yet wired.
	if (!Orchestrator && GetWorld())
	{
		Orchestrator = Cast<AOrchestrator>(
			UGameplayStatics::GetActorOfClass(GetWorld(), AOrchestrator::StaticClass()));
	}

	if (Orchestrator)
	{
		InitializeMazeReferences(Orchestrator, Orchestrator->GetSphereActor(), Orchestrator->GetMaze());
	}

	if (!Sphere || !Maze)
	{
		UE_LOG(LogTemp, Error, TEXT("MyCharacterBase: missing Sphere or Maze after refresh"));
		return;
	}

	// Choose spawn cell.
	const float CapsuleHH = CapsuleComp ? CapsuleComp->GetScaledCapsuleHalfHeight() : 88.f;
	FTransform SpawnTransform;

	bool bFoundCell = false;
	if (Orchestrator && Orchestrator->GetRandomSpawnTransform(SpawnTransform, CapsuleHH))
	{
		int32 SF = 0, SX = 0, SY = 0;
		if (FindClosestMazeCellToWorldLocation(SpawnTransform.GetLocation(), SF, SX, SY))
		{
			Face = SF; X = SX; Y = SY;
			bFoundCell = true;
		}
	}

	if (!bFoundCell)
	{
		Face = StartFace; X = StartX; Y = StartY;
	}

	SnapCharacterToCurrentCell();

	// Initialise CameraUpHint to the face-local North direction so the camera
	// has a sensible "screen up" direction on the very first frame.
	{
		FVector Fwd, Right, Up;
		if (GetSphereAlignedBasisForCell(Face, X, Y, Fwd, Right, Up))
		{
			CameraUpHint = Fwd; // face-local North = initial "screen up"
		}
	}

	if (bAutoLogCurrentMazeFace) DumpCurrentMazeFaceAscii();

	if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		if (PC->GetPawn() != this) PC->Possess(this);
		PC->SetViewTargetWithBlend(this, 0.f);
	}
}

// =============================================================================
// CalcCamera  —  Stable overhead sphere-surface camera
// =============================================================================
//
// DESIGN
// ------
// The camera sits directly above the character along the sphere surface normal
// at CameraArmLength distance.  It always looks straight down at the character.
//
// The hard problem is "screen up": what world direction appears at the top of
// the screen?  A naive choice (world +Z) spins at the poles.  Tracking the
// sphere actor's forward vector fails after it is rolled.
//
// Solution: CameraUpHint — the tangent direction the player LAST MOVED IN.
//   • Initialised to face-local North on spawn.
//   • Updated to the actual movement direction on every successful step.
//   • Projected onto the current cell's tangent plane each frame so it is
//     always truly tangential (no radial component).
//
// Result: "screen up" always matches where the player is heading.  The camera
// rotates smoothly as the player turns corners, and never flips or spins
// across face seams — because the hint is purely 3D world-space geometry.

void AMyCharacterBase::CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult)
{
	const FVector CharPos    = GetActorLocation();
	const FVector SphCenter  = GetBasisSphereCenterWorld();
	const FVector SurfNormal = (CharPos - SphCenter).GetSafeNormal();

	// ----- Camera position -----
	// Sit directly above the character along the outward sphere normal.
	const FVector CamPos = CharPos + SurfNormal * CameraArmLength;

	// CamForward: the direction the camera looks (from cam toward character).
	// This equals -SurfNormal — the camera always stares straight at the surface.
	const FVector CamForward = -SurfNormal;

	// ----- Desired screen-up direction -----
	// CameraUpHint is a world-space tangent vector set to the player's last
	// movement direction.  Because it is tangent to the sphere it is already
	// perpendicular to SurfNormal (= -CamForward), so the plane-projection
	// below is mathematically a no-op — but we keep it for numerical safety.
	FVector DesiredUp = FVector::ZeroVector;

	if (!CameraUpHint.IsNearlyZero())
	{
		DesiredUp = FVector::VectorPlaneProject(CameraUpHint, CamForward).GetSafeNormal();
	}

	// Fallback A: use the previous camera frame's own "up" projected into
	// the new camera plane.  This is far more stable than world-up because it
	// carries the camera's last valid orientation forward in time — the camera
	// will just drift very slowly rather than jumping.
	if (DesiredUp.IsNearlyZero() && bCameraQuatInit)
	{
		const FVector PrevUp = CameraQuat.GetUpVector(); // local Z of last frame
		DesiredUp = FVector::VectorPlaneProject(PrevUp, CamForward).GetSafeNormal();
	}

	// Fallback B: world axes — only reached if the camera has never been
	// initialised yet (very first frame) and CameraUpHint is not set.
	if (DesiredUp.IsNearlyZero())
	{
		DesiredUp = FVector::VectorPlaneProject(FVector::UpVector, CamForward).GetSafeNormal();
	}
	if (DesiredUp.IsNearlyZero())
	{
		DesiredUp = FVector::VectorPlaneProject(FVector::ForwardVector, CamForward).GetSafeNormal();
	}
	if (DesiredUp.IsNearlyZero())
	{
		// Absolute last resort — pick any axis not collinear with CamForward.
		DesiredUp = FVector::VectorPlaneProject(FVector::RightVector, CamForward).GetSafeNormal();
	}

	// ----- Build the desired camera quaternion -----
	// MakeFromXZ: local-X = look direction, local-Z = screen-up.
	// We work with quaternions throughout to avoid gimbal lock.
	// (FRotator at Pitch = ±90° is singular; FQuat never is.)
	if (!DesiredUp.IsNearlyZero())
	{
		const FQuat TargetQuat = FRotationMatrix::MakeFromXZ(CamForward, DesiredUp).ToQuat();

		if (!bCameraQuatInit)
		{
			// First frame: snap directly to the correct orientation so there is
			// no "swoop in" from the identity quaternion.
			CameraQuat      = TargetQuat;
			bCameraQuatInit = true;
		}
		else
		{
			// Subsequent frames: slerp toward the target.
			//
			// WHY THIS FIXES THE FLIP
			// ───────────────────────
			// When MakeFromXZ is fed inputs that cross a mathematical
			// discontinuity (e.g. Pitch sweeping through ±90° at the poles),
			// the resulting quaternion can jump to its antipode — a 180° snap.
			// Quaternion slerp always takes the SHORTEST arc, but only if we
			// first force the two quaternions into the same hemisphere by
			// negating the target when their dot product is negative.
			//
			// Speed = 8 /s  →  half-life ≈ 0.09 s  →  snappy yet smooth.
			// Even if TargetQuat jumps 180° in one frame, CameraQuat will
			// only rotate ~8° on a 60 Hz frame — imperceptible.

			// Ensure we take the short arc (dot < 0 means antipodal).
			const FQuat AlignedTarget = ((CameraQuat | TargetQuat) >= 0.f)
			                            ? TargetQuat
			                            : FQuat(-TargetQuat.X, -TargetQuat.Y,
			                                    -TargetQuat.Z, -TargetQuat.W);

			const float Alpha = FMath::Clamp(DeltaTime * 8.f, 0.f, 1.f);
			CameraQuat = FQuat::Slerp(CameraQuat, AlignedTarget, Alpha);
			CameraQuat.Normalize();
		}
	}

	// ----- Write view info -----
	OutResult.Location              = CamPos;
	OutResult.Rotation              = CameraQuat.Rotator();
	OutResult.FOV                   = CameraFOV;
	OutResult.bConstrainAspectRatio = false;
}

// =============================================================================
// GetBasisSphereCenterWorld
// =============================================================================

FVector AMyCharacterBase::GetBasisSphereCenterWorld() const
{
	if (Sphere)   return Sphere->GetActorTransform().GetLocation();
	if (Orchestrator) return Orchestrator->GetActorTransform().GetLocation();
	return FVector::ZeroVector;
}

// =============================================================================
// GetCurrentCellSurfaceNormal
// =============================================================================

FVector AMyCharacterBase::GetCurrentCellSurfaceNormal() const
{
	if (!Sphere) return FVector::UpVector;
	const FVector CellCenter = Sphere->GetCellCenterWorld(Face, X, Y);
	return (CellCenter - GetBasisSphereCenterWorld()).GetSafeNormal();
}

// =============================================================================
// GetCameraScreenUpInWorld
// =============================================================================
// Returns the tangential "screen up" direction for the current frame.
// This is CameraUpHint projected onto the current cell tangent plane.

FVector AMyCharacterBase::GetCameraScreenUpInWorld() const
{
	const FVector SurfNormal = GetCurrentCellSurfaceNormal();

	if (!CameraUpHint.IsNearlyZero() && !SurfNormal.IsNearlyZero())
	{
		const FVector Projected = FVector::VectorPlaneProject(CameraUpHint, SurfNormal).GetSafeNormal();
		if (!Projected.IsNearlyZero()) return Projected;
	}

	// Fallback: use the face-local North direction (always well-defined due to
	// the neighbour-cell basis computation in GetSphereAlignedBasisForCell).
	FVector Fwd, Right, Up;
	if (GetSphereAlignedBasisForCell(Face, X, Y, Fwd, Right, Up))
	{
		return Fwd;
	}

	return FVector::ForwardVector;
}

// =============================================================================
// GetSphereAlignedBasisForCell
// =============================================================================
//
// WHY NEIGHBOUR CELLS INSTEAD OF THE SPHERE ACTOR'S AXES
// -------------------------------------------------------
// The intuitive approach — project Sphere->GetActorForwardVector() onto the
// cell normal — breaks at the poles.  After RotateMazeAgainstMoveDirection rolls
// the sphere, the actor's world-forward may end up nearly parallel to the pole
// normal, giving a near-zero projection and an undefined "Forward" direction.
//
// Instead we sample the world positions of the two neighbours (X+1 and Y+1 on
// the same face) and subtract the current cell centre.  Neighbour cells are
// always physically close to the current cell regardless of how the sphere has
// been rotated, so the resulting vectors are always well-conditioned.
// This eliminates every pole singularity in one shot.

bool AMyCharacterBase::GetSphereAlignedBasisForCell(
	int32 InFace, int32 InX, int32 InY,
	FVector& OutForward, FVector& OutRight, FVector& OutUp) const
{
	OutForward = FVector::ForwardVector;
	OutRight   = FVector::RightVector;
	OutUp      = FVector::UpVector;

	if (!Sphere) return false;

	const FVector CellCenter   = Sphere->GetCellCenterWorld(InFace, InX, InY);
	const FVector SphereCenter = GetBasisSphereCenterWorld();

	OutUp = (CellCenter - SphereCenter).GetSafeNormal();
	if (OutUp.IsNearlyZero()) return false;

	const int32 CPF = Sphere->GetCellsPerFace();

	// ----- East direction  (+X in face grid) -----
	FVector EastRaw;
	if (InX + 1 < CPF)
		EastRaw = Sphere->GetCellCenterWorld(InFace, InX + 1, InY) - CellCenter;
	else if (InX - 1 >= 0)
		EastRaw = CellCenter - Sphere->GetCellCenterWorld(InFace, InX - 1, InY);
	else
		EastRaw = Sphere->GetActorRightVector(); // CPF == 1 edge case

	// ----- South direction (+Y in face grid) -----
	// We derive South first, then negate it to get North (= OutForward).
	FVector SouthRaw;
	if (InY + 1 < CPF)
		SouthRaw = Sphere->GetCellCenterWorld(InFace, InX, InY + 1) - CellCenter;
	else if (InY - 1 >= 0)
		SouthRaw = CellCenter - Sphere->GetCellCenterWorld(InFace, InX, InY - 1);
	else
		SouthRaw = Sphere->GetActorForwardVector();

	// Project onto the tangent plane (strips the radial sphere-curvature component).
	OutRight           = FVector::VectorPlaneProject(EastRaw,  OutUp).GetSafeNormal();
	const FVector SouthDir = FVector::VectorPlaneProject(SouthRaw, OutUp).GetSafeNormal();
	OutForward = -SouthDir; // North = −South

	// Final orthonormalization: keep OutUp authoritative, re-derive the other two.
	if (!OutForward.IsNearlyZero() && !OutRight.IsNearlyZero())
	{
		OutRight   = FVector::CrossProduct(OutUp, OutForward).GetSafeNormal();
		OutForward = FVector::CrossProduct(OutRight, OutUp).GetSafeNormal();
	}
	else if (!OutRight.IsNearlyZero())
	{
		OutForward = FVector::CrossProduct(OutRight, OutUp).GetSafeNormal();
	}
	else if (!OutForward.IsNearlyZero())
	{
		OutRight = FVector::CrossProduct(OutUp, OutForward).GetSafeNormal();
	}
	else
	{
		return false; // Both degenerate — face has 1 cell and sphere axes are also parallel.
	}

	return true;
}

// =============================================================================
// Input handlers
// =============================================================================
//
// W/A/S/D map to screen-up / screen-down / screen-left / screen-right.
//
// "Screen up" is CameraUpHint (tangential, world-space).
// "Screen right" is CrossProduct(ScreenUp, SurfaceNormal).
//
// Cross product derivation:
//   CamForward = -SurfNormal  (camera looks down at sphere)
//   ScreenRight = cross(CamForward, CamUp) = cross(-SurfNormal, ScreenUp)
//                = cross(ScreenUp, SurfNormal)   [by anti-commutativity]
//
// All directions are then passed through ResolveMazeDirectionFromWorldVector
// which picks the N/E/S/W maze direction whose 3D neighbour best matches.
// This is pure 3D geometry — zero face-local coordinate math, zero pole issues.

void AMyCharacterBase::HandleMoveForwardInput()
{
	const FVector ScreenUp = GetCameraScreenUpInWorld();
	HandleMoveInputFromWorldDirection(ScreenUp);
}

void AMyCharacterBase::HandleMoveBackwardInput()
{
	const FVector ScreenUp = GetCameraScreenUpInWorld();
	HandleMoveInputFromWorldDirection(-ScreenUp);
}

void AMyCharacterBase::HandleMoveRightInput()
{
	const FVector SurfNormal = GetCurrentCellSurfaceNormal();
	const FVector ScreenUp   = GetCameraScreenUpInWorld();

	// ScreenRight derivation:
	// The camera matrix is built with MakeFromXZ(CamForward, CamUp) where
	// CamForward = -SurfNormal and CamUp = ScreenUp.
	// Inside MakeFromXZ, local-Y (screen right) = normalize(CamUp × CamForward)
	//                                            = normalize(ScreenUp × -SurfNormal)
	//                                            = normalize(SurfNormal × ScreenUp)
	// So the correct screen-right is SurfNormal × ScreenUp, NOT ScreenUp × SurfNormal.
	const FVector ScreenRight = FVector::CrossProduct(SurfNormal, ScreenUp).GetSafeNormal();
	HandleMoveInputFromWorldDirection(ScreenRight);
}

void AMyCharacterBase::HandleMoveLeftInput()
{
	const FVector SurfNormal  = GetCurrentCellSurfaceNormal();
	const FVector ScreenUp    = GetCameraScreenUpInWorld();
	const FVector ScreenRight = FVector::CrossProduct(SurfNormal, ScreenUp).GetSafeNormal();
	HandleMoveInputFromWorldDirection(-ScreenRight);
}

// =============================================================================
// HandleMoveInputFromWorldDirection
// =============================================================================

void AMyCharacterBase::HandleMoveInputFromWorldDirection(const FVector& DesiredWorldDir)
{
	if (!Sphere || !Maze) return;

	const EMazeDir Dir = ResolveMazeDirectionFromWorldVector(DesiredWorldDir);

	if (bMoveTweenActive)
	{
		// Queue the move — fires automatically when the current tween finishes.
		// Overwrite any previously queued move (most recent intent wins).
		bMoveQueued    = true;
		QueuedDir      = Dir;
		QueuedWorldDir = DesiredWorldDir;
	}
	else
	{
		TryMoveInMazeDirection(Dir);
	}
}

// =============================================================================
// ResolveMazeDirectionFromWorldVector
// =============================================================================
//
// Given a desired world-space tangent direction (e.g. "screen up"), find which
// of the four maze directions (N/E/S/W) has its 3D neighbour cell in the most
// similar direction.
//
// This is robust on all six faces including the poles because:
//   1. It uses Maze->GetNeighborCell to get all four neighbours, which already
//      handles face transitions internally.
//   2. It uses Sphere->GetCellCenterWorld to get their 3D positions — pure world
//      geometry, independent of face-local coordinate frames.
//   3. It never touches 2D face coordinates for direction resolution.

EMazeDir AMyCharacterBase::ResolveMazeDirectionFromWorldVector(const FVector& DesiredWorldDir) const
{
	if (!Sphere || !Maze) return EMazeDir::N;

	const FVector SurfNormal = GetCurrentCellSurfaceNormal();
	if (SurfNormal.IsNearlyZero()) return EMazeDir::N;

	// Project the desired direction onto the tangent plane so we compare apples
	// to apples when we dot-product against neighbour directions below.
	const FVector DesiredTangent = FVector::VectorPlaneProject(DesiredWorldDir, SurfNormal).GetSafeNormal();
	if (DesiredTangent.IsNearlyZero()) return EMazeDir::N;

	const FVector CurrentCenter = Sphere->GetCellCenterWorld(Face, X, Y);
	const FMazeNode CurrentNode(Face, X, Y);

	EMazeDir BestDir  = EMazeDir::N;
	float    BestDot  = -FLT_MAX;

	for (EMazeDir Dir : { EMazeDir::N, EMazeDir::E, EMazeDir::S, EMazeDir::W })
	{
		// GetNeighborCell with bIgnoreWalls=true: we want ALL four neighbours for
		// direction resolution, not just the open ones.  Wall filtering happens in
		// TryMoveInMazeDirection via IsOpen — not here.
		const FMazeNode Neighbour = Maze->GetNeighborCell(CurrentNode, Dir, /*bIgnoreWalls=*/true);
		if (!Maze->IsValid(Neighbour.Face, Neighbour.X, Neighbour.Y)) continue;

		const FVector NeighbourCenter = Sphere->GetCellCenterWorld(
			Neighbour.Face, Neighbour.X, Neighbour.Y);

		// Direction from current cell to this neighbour, projected onto tangent plane.
		const FVector CandidateDir = FVector::VectorPlaneProject(
			NeighbourCenter - CurrentCenter, SurfNormal).GetSafeNormal();

		if (CandidateDir.IsNearlyZero()) continue;

		const float Dot = FVector::DotProduct(DesiredTangent, CandidateDir);
		if (Dot > BestDot)
		{
			BestDot  = Dot;
			BestDir  = Dir;
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
// TryMoveInMazeDirection
// =============================================================================

bool AMyCharacterBase::TryMoveInMazeDirection(EMazeDir Dir)
{
	if (!Sphere || !Maze)                       return false;
	if (bMoveTweenActive)                       return false;

	const FMazeCell& Cell = Maze->GetCell(Face, X, Y);

	if (!IsOpen(Cell, Dir))
	{
		UE_LOG(LogTemp, Verbose, TEXT("MyChar: wall blocks %s from Face%d (%d,%d)"),
			DirName(Dir), Face, X, Y);
		return false;
	}

	const int32 CPF = FMath::Max(1, Maze->CellsPerFace);
	int32 NewFace = Face, NewX = X, NewY = Y;

	// Step within the same face first.
	switch (Dir)
	{
	case EMazeDir::N: --NewY; break;
	case EMazeDir::E: ++NewX; break;
	case EMazeDir::S: ++NewY; break;
	case EMazeDir::W: --NewX; break;
	}

	// If out of bounds, resolve the face transition using the existing seam table.
	if (NewX < 0 || NewX >= CPF || NewY < 0 || NewY >= CPF)
	{
		FMazeNode SeamNode;
		if (!Maze->TryFaceTransition(FMazeNode(Face, X, Y), Dir, SeamNode))
		{
			UE_LOG(LogTemp, Warning, TEXT("MyChar: TryFaceTransition failed for Dir=%s"), DirName(Dir));
			return false;
		}
		NewFace = SeamNode.Face;
		NewX    = SeamNode.X;
		NewY    = SeamNode.Y;
	}

	const int32 OldFace = Face, OldX = X, OldY = Y;
	Face = NewFace; X = NewX; Y = NewY;

	StartMoveTween(OldFace, OldX, OldY, Face, X, Y);

	UE_LOG(LogTemp, Verbose, TEXT("MyChar: move %s → Face%d (%d,%d)"),
		DirName(Dir), Face, X, Y);

	if (bAutoLogCurrentMazeFace) DumpCurrentMazeFaceAscii();

	return true;
}

// =============================================================================
// StartMoveTween
// =============================================================================
//
// Prepares all state for a smooth spherical arc tween from the old cell to the
// new cell.  The actual per-frame interpolation happens in UpdateMoveTween.

void AMyCharacterBase::StartMoveTween(
	int32 OldFace, int32 OldX, int32 OldY,
	int32 NewFace, int32 NewX, int32 NewY)
{
	// Safety: CharFixedWorldPos must be initialised before any tween can run.
	// RefreshAfterMazeRebuild → SnapCharacterToCurrentCell normally guarantees
	// this, but guard here in case of an unusual call order.
	if (!bCharFixedPosInit)
	{
		SnapCharacterToCurrentCell();
		if (!bCharFixedPosInit) return; // Sphere not ready — abort silently
	}

	TweenSphereCenter = GetBasisSphereCenterWorld();

	// World-space cell centres at the START of this tween (sphere has not moved yet).
	const FVector FromCell = Sphere->GetCellCenterWorld(OldFace, OldX, OldY);
	const FVector ToCell   = Sphere->GetCellCenterWorld(NewFace, NewX, NewY);

	const FVector FromNormal = (FromCell - TweenSphereCenter).GetSafeNormal();
	const FVector ToNormal   = (ToCell   - TweenSphereCenter).GetSafeNormal();

	// -------------------------------------------------------------------------
	// SPHERE ROTATION SETUP
	//
	// Goal: rotate the sphere so that the TARGET cell (ToCell) arrives at
	// CharFixedWorldPos — the fixed world point where the character stands.
	//
	// CharFixedWorldPos lies along the radial direction CharUp from the sphere
	// centre.  We need the sphere to rotate until ToNormal aligns with CharUp.
	//
	// The rotation is applied in WORLD space, so:
	//   TweenTargetSphereRot = WorldAlignRot * TweenStartSphereRot
	//
	// where WorldAlignRot = FindBetweenNormals(ToNormal, CharUp) — the minimum
	// world-space rotation that swings ToNormal onto CharUp.
	//
	// Proof:
	//   After applying TweenTargetSphereRot to sphere-local ToNormal_local:
	//   TweenTargetSphereRot * ToNormal_local
	//   = (WorldAlignRot * TweenStartSphereRot) * ToNormal_local
	//   = WorldAlignRot * (TweenStartSphereRot * ToNormal_local)
	//   = WorldAlignRot * ToNormal   [since sphere is at TweenStartSphereRot]
	//   = CharUp                     [by definition of WorldAlignRot]  ✓
	// -------------------------------------------------------------------------
	// Rotate the ORCHESTRATOR (not just the sphere child) so that WallHISM
	// and all other components attached to it move together with the geometry.
	TweenStartSphereRot  = Orchestrator->GetActorQuat();

	// CharUp: the fixed outward direction from sphere centre to CharFixedWorldPos.
	// (Initialised in SnapCharacterToCurrentCell on first use.)
	const FVector CharUp = (CharFixedWorldPos - TweenSphereCenter).GetSafeNormal();

	// Rotation in world space that swings the target-cell direction onto CharUp.
	const FQuat WorldAlignRot   = FQuat::FindBetweenNormals(ToNormal, CharUp);
	TweenTargetSphereRot        = WorldAlignRot * TweenStartSphereRot;

	// -------------------------------------------------------------------------
	// Character rotation at start and end of the tween.
	//
	// We want the character to smoothly turn to face the direction they are
	// walking.  The movement direction (in world space) is approximated by the
	// vector from the old cell centre to the new cell centre, then projected
	// onto the start/end tangent planes.
	//
	// MakeFromXZ(X, Z): X = local forward (where character faces), Z = local up (surface normal).
	// -------------------------------------------------------------------------
	const FVector MoveApprox = (ToCell - FromCell).GetSafeNormal(); // approx move direction

	const FVector StartFacingDir = FVector::VectorPlaneProject(MoveApprox, FromNormal).GetSafeNormal();
	const FVector EndFacingDir   = FVector::VectorPlaneProject(MoveApprox, ToNormal  ).GetSafeNormal();

	// If the movement vector is degenerate (cells are on top of each other, shouldn't
	// happen), fall back to the face-local North direction.
	FVector StartFwd = StartFacingDir;
	FVector EndFwd   = EndFacingDir;

	if (StartFwd.IsNearlyZero())
	{
		FVector TmpFwd, TmpRight, TmpUp;
		if (GetSphereAlignedBasisForCell(OldFace, OldX, OldY, TmpFwd, TmpRight, TmpUp))
			StartFwd = TmpFwd;
	}
	if (EndFwd.IsNearlyZero())
	{
		FVector TmpFwd, TmpRight, TmpUp;
		if (GetSphereAlignedBasisForCell(NewFace, NewX, NewY, TmpFwd, TmpRight, TmpUp))
			EndFwd = TmpFwd;
	}

	// -------------------------------------------------------------------------
	// CHARACTER ROTATION SETUP
	//
	// The character always stands at CharFixedWorldPos, so its "up" is the
	// fixed CharUp direction.  We only need to rotate it around that axis to
	// face the movement direction.
	//
	// TweenFromRot  = current actual rotation (so chained turns are seamless).
	// TweenToRot    = face EndFwd, upright on CharUp.
	//                 Recomputed each frame in UpdateMoveTween as the sphere
	//                 rolls, so the target tracks the maze geometry correctly.
	//
	// TweenLocalMoveDir = EndFwd converted to sphere-LOCAL space.
	//   Each frame: WorldFacing = CurrentSphereRot * TweenLocalMoveDir
	//   This automatically updates the target facing as the sphere rolls,
	//   making the character appear to turn relative to the maze surface.
	// -------------------------------------------------------------------------
	TweenFromRot = GetActorQuat();

	// EndFwd is world-space at TweenStartSphereRot; project onto CharUp plane.
	const FVector EndFacingOnCharPlane =
		FVector::VectorPlaneProject(EndFwd, CharUp).GetSafeNormal();
	const FVector InitialFacing = EndFacingOnCharPlane.IsNearlyZero() ? EndFwd : EndFacingOnCharPlane;
	TweenToRot = FRotationMatrix::MakeFromXZ(InitialFacing, CharUp).ToQuat();

	// Store movement direction in sphere-local space.
	TweenLocalMoveDir = TweenStartSphereRot.Inverse().RotateVector(EndFwd);

	RotTweenElapsed = 0.f;

	// CameraUpHint: same guard as before (only update when moving forward-ish).
	if (!StartFacingDir.IsNearlyZero())
	{
		const bool  HintEmpty  = CameraUpHint.IsNearlyZero();
		const float DotWithUp  = FVector::DotProduct(StartFacingDir, CameraUpHint);
		if (HintEmpty || DotWithUp >= 0.f)
			CameraUpHint = StartFacingDir;
	}

	MoveTweenAlpha   = 0.f;
	bMoveTweenActive = true;
}

// =============================================================================
// UpdateMoveTween  —  Spherical arc interpolation
// =============================================================================
//
// MATH OVERVIEW
// -------------
// Both cell positions (TweenFromCellCenter, TweenToCellCenter) lie on a sphere
// centred at TweenSphereCenter.  A straight lerp between them would arc inward
// through the sphere.  Instead we interpolate along the great-circle arc
// (the shortest path on the sphere surface) using quaternion slerp.
//
// Given:
//   O  = TweenSphereCenter
//   A  = TweenFromCellCenter - O   (vector from sphere centre to start point)
//   B  = TweenToCellCenter   - O   (vector from sphere centre to end point)
//   A_dir = normalize(A),  B_dir = normalize(B)
//
// The quaternion Q rotates A_dir onto B_dir (it is the minimum rotation between
// the two radial directions, i.e., the great-circle arc rotation).
//
// At time t ∈ [0,1]:
//   curr_dir = slerp(Identity, Q, t)  *  A_dir
//   curr_R   = lerp(|A|, |B|, t)       // handles small height differences
//   curr_pos = O + curr_dir * curr_R
//
// This guarantees the character glides along the sphere surface, never
// dipping below it or floating off it.

void AMyCharacterBase::UpdateMoveTween(float DeltaSeconds)
{
	if (!bMoveTweenActive) return;

	// -------------------------------------------------------------------------
	// SPHERE ROTATION  —  driven by StepTweenDuration
	//
	// The character stays at CharFixedWorldPos.  The sphere rotates so the
	// next cell arrives at that fixed point.  Slerping the sphere quaternion
	// gives a smooth "ball rolling" effect in all directions.
	// -------------------------------------------------------------------------
	MoveTweenAlpha += DeltaSeconds / FMath::Max(0.001f, StepTweenDuration);
	const float Alpha       = FMath::Clamp(MoveTweenAlpha, 0.f, 1.f);
	const float SmoothAlpha = FMath::InterpEaseInOut(0.f, 1.f, Alpha, 2.f);

	if (Orchestrator)
	{
		const FQuat NewSphereRot = FQuat::Slerp(TweenStartSphereRot, TweenTargetSphereRot, SmoothAlpha);
		Orchestrator->SetActorRotation(NewSphereRot);

		// -------------------------------------------------------------------------
		// CHARACTER ROTATION  —  driven by CharacterTurnSpeed °/s
		//
		// TweenToRot is recomputed each frame from TweenLocalMoveDir (sphere-local
		// movement direction) rotated by the sphere's CURRENT world rotation.
		// This makes the character face the movement direction relative to the maze
		// geometry below, which rotates naturally as the sphere rolls.
		//
		// Because the sphere rolls under the character, the world-space "facing
		// direction" changes continuously during the tween.  Tracking it live
		// means the character always ends up facing exactly the right direction
		// rather than overshooting due to the sphere having moved.
		// -------------------------------------------------------------------------
		if (!TweenLocalMoveDir.IsNearlyZero())
		{
			const FVector CharUp       = (CharFixedWorldPos - TweenSphereCenter).GetSafeNormal();
			const FVector WorldFacing  = NewSphereRot.RotateVector(TweenLocalMoveDir);
			const FVector TangentFacing = FVector::VectorPlaneProject(WorldFacing, CharUp).GetSafeNormal();
			if (!TangentFacing.IsNearlyZero())
				TweenToRot = FRotationMatrix::MakeFromXZ(TangentFacing, CharUp).ToQuat();
		}
	}

	RotTweenElapsed += DeltaSeconds;
	const float TotalAngleRad = TweenFromRot.AngularDistance(TweenToRot);

	float RotAlpha;
	if (TotalAngleRad < KINDA_SMALL_NUMBER)
	{
		RotAlpha = 1.f;
	}
	else
	{
		const float TurnDuration = TotalAngleRad
		                           / FMath::DegreesToRadians(FMath::Max(1.f, CharacterTurnSpeed));
		RotAlpha = FMath::Clamp(RotTweenElapsed / TurnDuration, 0.f, 1.f);
		RotAlpha = FMath::InterpEaseInOut(0.f, 1.f, RotAlpha, 2.f);
	}

	const FQuat AlignedToRot = ((TweenFromRot | TweenToRot) >= 0.f)
	                            ? TweenToRot
	                            : FQuat(-TweenToRot.X, -TweenToRot.Y,
	                                    -TweenToRot.Z, -TweenToRot.W);
	const FQuat NewCharRot = FQuat::Slerp(TweenFromRot, AlignedToRot, RotAlpha);

	// Character position is always the fixed world point.
	SetActorLocationAndRotation(CharFixedWorldPos, NewCharRot,
	                            false, nullptr, ETeleportType::TeleportPhysics);

	if (Alpha >= 1.f)
	{
		bMoveTweenActive = false;

		// Snap sphere to exact target (kills floating-point drift).
		if (Orchestrator) Orchestrator->SetActorRotation(TweenTargetSphereRot);

		// Character stays at the fixed point; only preserve rotation.
		SetActorLocation(CharFixedWorldPos, false, nullptr, ETeleportType::TeleportPhysics);

		// Update camera hint with the final world-space movement direction.
		if (Sphere && !TweenLocalMoveDir.IsNearlyZero())
		{
			const FVector CharUp       = (CharFixedWorldPos - TweenSphereCenter).GetSafeNormal();
			const FVector FinalFacing  = FVector::VectorPlaneProject(
			                                TweenTargetSphereRot.RotateVector(TweenLocalMoveDir),
			                                CharUp).GetSafeNormal();
			if (!FinalFacing.IsNearlyZero())
			{
				const bool  HintEmpty = CameraUpHint.IsNearlyZero();
				const float DotWithUp = FVector::DotProduct(FinalFacing, CameraUpHint);
				if (HintEmpty || DotWithUp >= 0.f)
					CameraUpHint = FinalFacing;
			}
		}

		if (bMoveQueued)
		{
			bMoveQueued = false;
			TryMoveInMazeDirection(QueuedDir);
		}
	}
}

// =============================================================================
// BuildPlacedWorldTransformForCell
// =============================================================================

FTransform AMyCharacterBase::BuildPlacedWorldTransformForCell(
	int32 InFace, int32 InX, int32 InY) const
{
	if (!Sphere) return FTransform(GetActorRotation(), GetActorLocation());

	const FVector CellCenter   = Sphere->GetCellCenterWorld(InFace, InX, InY);
	const FVector SphereCenter = GetBasisSphereCenterWorld();
	const FVector OutUp        = (CellCenter - SphereCenter).GetSafeNormal();

	if (OutUp.IsNearlyZero()) return FTransform(FRotator::ZeroRotator, CellCenter);

	const float HalfHeight = CapsuleComp ? CapsuleComp->GetScaledCapsuleHalfHeight() : 88.f;
	const FVector NewPos   = CellCenter + OutUp * (HalfHeight + 2.f + StepHeightOffset);

	FVector Fwd, Right, Up;
	if (!GetSphereAlignedBasisForCell(InFace, InX, InY, Fwd, Right, Up))
	{
		return FTransform(FRotator::ZeroRotator, NewPos);
	}

	// Character faces "screen up" (CameraUpHint) when standing still.
	// If the hint is set, use it for the forward direction.
	FVector FacingFwd = Fwd;
	if (!CameraUpHint.IsNearlyZero())
	{
		const FVector HintTangent = FVector::VectorPlaneProject(CameraUpHint, Up).GetSafeNormal();
		if (!HintTangent.IsNearlyZero()) FacingFwd = HintTangent;
	}

	const FRotator NewRot = FRotationMatrix::MakeFromXZ(FacingFwd, Up).Rotator();
	return FTransform(NewRot, NewPos);
}

// =============================================================================
// SnapCharacterToCurrentCell
// =============================================================================

void AMyCharacterBase::SnapCharacterToCurrentCell()
{
	if (!Sphere) return;

	if (!bCharFixedPosInit)
	{
		// -----------------------------------------------------------------------
		// First call (spawn / rebuild): establish CharFixedWorldPos from the
		// current cell's world position.  Everything else follows from here.
		//
		// The sphere is NOT moved — its current orientation defines where
		// cell (Face, X, Y) sits in the world, and that world position becomes
		// the permanent anchor point for the character.
		// -----------------------------------------------------------------------
		const FVector CellWorld = Sphere->GetCellCenterWorld(Face, X, Y);
		const FVector SphCenter = GetBasisSphereCenterWorld();
		const FVector Normal    = (CellWorld - SphCenter).GetSafeNormal();
		const float   HalfH     = CapsuleComp ? CapsuleComp->GetScaledCapsuleHalfHeight() : 88.f;

		CharFixedWorldPos      = CellWorld + Normal * (HalfH + 2.f + StepHeightOffset);
		TweenTargetSphereRot   = Orchestrator->GetActorQuat(); // current rot is already correct
		bCharFixedPosInit      = true;

		// Place character and set initial facing from BuildPlacedWorldTransformForCell.
		const FTransform T = BuildPlacedWorldTransformForCell(Face, X, Y);
		SetActorLocationAndRotation(CharFixedWorldPos, T.GetRotation(),
		                            false, nullptr, ETeleportType::TeleportPhysics);
		return;
	}

	// Subsequent calls (end of tween / teleport): snap sphere to the exact
	// target rotation and keep character at the fixed world point.
	// Character rotation is left untouched (the turn system owns it).
	Orchestrator->SetActorRotation(TweenTargetSphereRot);
	SetActorLocation(CharFixedWorldPos, false, nullptr, ETeleportType::TeleportPhysics);
}

// =============================================================================
// PlaceOnCell  (Blueprint-callable teleport)
// =============================================================================

FTransform AMyCharacterBase::PlaceOnCell(int32 InFace, int32 InX, int32 InY)
{
	bMoveTweenActive = false;
	MoveTweenAlpha   = 0.f;
	bMoveQueued      = false;

	Face = InFace; X = InX; Y = InY;

	const FTransform T = BuildPlacedWorldTransformForCell(Face, X, Y);
	SetActorLocationAndRotation(
		T.GetLocation(), T.Rotator(),
		false, nullptr, ETeleportType::TeleportPhysics);

	// Re-init the camera hint for the new cell.
	FVector Fwd, Right, Up;
	if (GetSphereAlignedBasisForCell(Face, X, Y, Fwd, Right, Up)) CameraUpHint = Fwd;

	DrawCellBasisDebug(Face, X, Y, 150.f, 10.f);
	return T;
}

// =============================================================================
// FindClosestMazeCellToWorldLocation
// =============================================================================

bool AMyCharacterBase::FindClosestMazeCellToWorldLocation(
	const FVector& WorldPos, int32& OutFace, int32& OutX, int32& OutY) const
{
	if (!Sphere) return false;

	const int32 CPF    = Sphere->GetCellsPerFace();
	float       BestD2 = TNumericLimits<float>::Max();

	for (int32 F = 0; F < 6; ++F)
	{
		for (int32 CY = 0; CY < CPF; ++CY)
		{
			for (int32 CX = 0; CX < CPF; ++CX)
			{
				const float D2 = FVector::DistSquared(
					Sphere->GetCellCenterWorld(F, CX, CY), WorldPos);
				if (D2 < BestD2)
				{
					BestD2   = D2;
					OutFace  = F;
					OutX     = CX;
					OutY     = CY;
				}
			}
		}
	}
	return true;
}

// =============================================================================
// Debug / ASCII dump
// =============================================================================

void AMyCharacterBase::DumpCurrentMazeFaceAscii() const { DumpMazeFaceAscii(Face); }
void AMyCharacterBase::DumpAllMazeFacesAscii()    const { for (int32 F = 0; F < 6; ++F) DumpMazeFaceAscii(F); }

void AMyCharacterBase::DumpMazeFaceAscii(int32 FaceToDump) const
{
	if (!Maze || !Maze->IsValid(FaceToDump, 0, 0))
	{
		UE_LOG(LogTemp, Warning, TEXT("DumpMazeFace: Face %d invalid"), FaceToDump);
		return;
	}

	const int32 CPF = FMath::Max(1, Maze->CellsPerFace);
	UE_LOG(LogTemp, Warning, TEXT("FACE %d  (player at Face=%d X=%d Y=%d)"), FaceToDump, Face, X, Y);

	for (int32 CY = 0; CY < CPF; ++CY)
	{
		FString TopLine;
		for (int32 CX = 0; CX < CPF; ++CX)
		{
			TopLine += TEXT("+");
			TopLine += Maze->GetCell(FaceToDump, CX, CY).OpenN ? TEXT("   ") : TEXT("---");
		}
		TopLine += TEXT("+");
		UE_LOG(LogTemp, Warning, TEXT("%s"), *TopLine);

		FString CellLine;
		for (int32 CX = 0; CX < CPF; ++CX)
		{
			CellLine += Maze->GetCell(FaceToDump, CX, CY).OpenW ? TEXT(" ") : TEXT("|");
			const bool bHere = (FaceToDump == Face && CX == X && CY == Y);
			CellLine += bHere ? TEXT(" P ") : TEXT("   ");
		}
		const FMazeCell& LastCell = Maze->GetCell(FaceToDump, CPF - 1, CY);
		CellLine += LastCell.OpenE ? TEXT(" ") : TEXT("|");
		UE_LOG(LogTemp, Warning, TEXT("%s"), *CellLine);
	}

	FString BottomLine;
	const int32 LastRow = CPF - 1;
	for (int32 CX = 0; CX < CPF; ++CX)
	{
		BottomLine += TEXT("+");
		BottomLine += Maze->GetCell(FaceToDump, CX, LastRow).OpenS ? TEXT("   ") : TEXT("---");
	}
	BottomLine += TEXT("+");
	UE_LOG(LogTemp, Warning, TEXT("%s"), *BottomLine);
	UE_LOG(LogTemp, Warning, TEXT("END FACE %d"), FaceToDump);
}

// =============================================================================
// DrawCellBasisDebug
// =============================================================================

void AMyCharacterBase::DrawCellBasisDebug(
	int32 InFace, int32 InX, int32 InY, float Length, float Duration) const
{
	if (!GetWorld() || !Sphere) return;

	FVector Fwd, Right, Up;
	if (!GetSphereAlignedBasisForCell(InFace, InX, InY, Fwd, Right, Up)) return;

	const FVector CellCenter = Sphere->GetCellCenterWorld(InFace, InX, InY);
	const FVector DrawStart  = CellCenter + Up * 20.f;

	DrawDebugSphere(GetWorld(), CellCenter,             12.f,   10, FColor::Yellow, false, Duration, 0, 2.f);
	DrawDebugLine  (GetWorld(), DrawStart, DrawStart + Fwd   * Length, FColor::Green,  false, Duration, 0, 3.f);
	DrawDebugLine  (GetWorld(), DrawStart, DrawStart + Right * Length, FColor::Red,    false, Duration, 0, 3.f);
	DrawDebugLine  (GetWorld(), DrawStart, DrawStart + Up    * Length, FColor::Blue,   false, Duration, 0, 3.f);
}
