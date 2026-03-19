#include "MazeRunner.h"
#include "Components/StaticMeshComponent.h"
#include "../Conversion/CubeToSphere.h"

AMazeRunner::AMazeRunner()
{
	PrimaryActorTick.bCanEverTick = true;
	MeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComp"));
	SetRootComponent(MeshComp);
}

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

void AMazeRunner::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bIsMoving || !TargetSphere || !PathToFollow.IsValidIndex(CurrentTargetIndex))
		return;

	// 1. PURE LOCAL SPACE: Get location relative to the attached sphere
	FVector CurrentLocal = GetRootComponent()->GetRelativeLocation();

	// 2. THE HOVER FIX: Path nodes are exactly on the floor, but the runner spawns 17 units above it!
	FVector BaseTarget = PathToFollow[CurrentTargetIndex];
	FVector UpDir = BaseTarget.GetSafeNormal();
	FVector TargetLocal = BaseTarget + (UpDir * 17.0f);

	// 3. Check if we reached the waypoint
	if (FVector::Dist(CurrentLocal, TargetLocal) < 2.0f)
	{
		CurrentTargetIndex++;

		// If we finished the path, clean up and disappear
		if (!PathToFollow.IsValidIndex(CurrentTargetIndex))
		{
			bIsMoving = false;
			for (AActor *Marker : LinkedMarkers)
			{
				if (Marker)
					Marker->Destroy();
			}
			this->Destroy();
			return;
		}

		// Immediately grab the next target to keep movement smooth
		BaseTarget = PathToFollow[CurrentTargetIndex];
		UpDir = BaseTarget.GetSafeNormal();
		TargetLocal = BaseTarget + (UpDir * 17.0f);
	}

	// 4. THE CURVE FIX: Move smoothly, then push the step out to the sphere surface!
	FVector NewLocal = FMath::VInterpConstantTo(CurrentLocal, TargetLocal, DeltaTime, MovementSpeed);

	float HoverRadius = TargetLocal.Size();
	NewLocal = NewLocal.GetSafeNormal() * HoverRadius;

	SetActorRelativeLocation(NewLocal);

	// 5. Surface alignment in Local Space
	FVector CurrentUp = NewLocal.GetSafeNormal();
	FVector ForwardDir = (TargetLocal - NewLocal).GetSafeNormal();
	if (ForwardDir.SizeSquared() > 0.001f)
	{
		FRotator NewRot = FRotationMatrix::MakeFromXZ(ForwardDir, CurrentUp).Rotator();
		SetActorRelativeRotation(NewRot);
	}
}
