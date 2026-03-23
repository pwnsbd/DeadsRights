#include "MazeRunner.h"
#include "Components/StaticMeshComponent.h"
#include "../Conversion/CubeToSphere.h"

/**
 * desc : Default constructor. Initializes the mesh component and sets default scale.
 * args : None
 * result: None
 */
AMazeRunner::AMazeRunner()
{
	PrimaryActorTick.bCanEverTick = true;
	MeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComp"));
	SetRootComponent(MeshComp);

	// Force a smaller scale so the runner fits cleanly inside the maze corridors
	MeshComp->SetWorldScale3D(FVector(0.5f, 0.5f, 0.5f));
}

/**
 * desc : Hands the calculated local-space path to the AI so it can begin moving.
 * args :
 * - NewLocalPath: Array of waypoints relative to the sphere's local space (center 0,0,0).
 * - InSphereActor: Pointer to the spherical planet so the runner can track it.
 * result: None
 */
void AMazeRunner::SetPath(const TArray<FVector> &NewLocalPath, ACubeToSphere *InSphereActor)
{
	PathToFollow = NewLocalPath;
	TargetSphere = InSphereActor;
	CurrentTargetIndex = 0;

	if (PathToFollow.Num() > 0 && TargetSphere)
	{
		bIsMoving = true;
	}
}

/**
 * desc : Called every frame. Handles local space movement, spherical surface alignment, and waypoint tracking.
 * args : DeltaTime - The time elapsed since the last frame.
 * result: None
 */
void AMazeRunner::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bIsMoving || !TargetSphere || !PathToFollow.IsValidIndex(CurrentTargetIndex))
		return;

	// 1. PURE LOCAL SPACE: Get location relative to the attached sphere
	FVector CurrentLocal = GetRootComponent()->GetRelativeLocation();

	// 2. THE HOVER FIX: Waypoints are exactly on the floor, so we offset the target UP by 17 units.
	FVector BaseTarget = PathToFollow[CurrentTargetIndex];
	FVector UpDir = BaseTarget.GetSafeNormal();
	FVector TargetLocal = BaseTarget + (UpDir * 17.0f);

	// 3. WAYPOINT ARRIVAL CHECK (Tight 2.0f tolerance for exact center alignment)
	if (FVector::Dist(CurrentLocal, TargetLocal) < 2.0f)
	{
		CurrentTargetIndex++;

		// If there are no waypoints left, stop moving and alert the Orchestrator
		if (!PathToFollow.IsValidIndex(CurrentTargetIndex))
		{
			bIsMoving = false;
			OnPathCompleted.Broadcast();
			return;
		}

		// Recalculate target immediately to maintain smooth continuous motion
		BaseTarget = PathToFollow[CurrentTargetIndex];
		UpDir = BaseTarget.GetSafeNormal();
		TargetLocal = BaseTarget + (UpDir * 17.0f);
	}

	// 4. MOVEMENT & SPHERICAL PROJECTION
	// Interp linearly, then multiply by the hover radius to push the path back out to the sphere's curvature.
	FVector NewLocal = FMath::VInterpConstantTo(CurrentLocal, TargetLocal, DeltaTime, MovementSpeed);

	float HoverRadius = TargetLocal.Size();
	NewLocal = NewLocal.GetSafeNormal() * HoverRadius;

	SetActorRelativeLocation(NewLocal);

	// 5. SURFACE ALIGNMENT
	// Rotates the mesh so 'Up' faces away from the sphere center, and 'Forward' faces the next waypoint.
	FVector CurrentUp = NewLocal.GetSafeNormal();
	FVector ForwardDir = (TargetLocal - NewLocal).GetSafeNormal();

	if (ForwardDir.SizeSquared() > 0.001f)
	{
		FRotator NewRot = FRotationMatrix::MakeFromXZ(ForwardDir, CurrentUp).Rotator();
		SetActorRelativeRotation(NewRot);
	}
}
