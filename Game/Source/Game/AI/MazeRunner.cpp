#include "MazeRunner.h"
#include "Components/StaticMeshComponent.h"

AMazeRunner::AMazeRunner()
{
	PrimaryActorTick.bCanEverTick = true; // MUST be true so it can move every frame!

	MeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComp"));
	SetRootComponent(MeshComp);
}

void AMazeRunner::SetPath(const TArray<FVector> &NewPath, FVector InSphereCenter)
{
	PathToFollow = NewPath;
	SphereCenter = InSphereCenter;
	CurrentTargetIndex = 0; // Start at the beginning of the new path

	if (PathToFollow.Num() > 0)
	{
		bIsMoving = true;
	}
}

void AMazeRunner::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bIsMoving || !PathToFollow.IsValidIndex(CurrentTargetIndex))
	{
		return; // We reached the end of the maze!
	}

	FVector CurrentLoc = GetActorLocation();
	FVector TargetLoc = PathToFollow[CurrentTargetIndex];

	// 1. Check if we reached the target waypoint (within a small 10-unit threshold)
	if (FVector::Dist(CurrentLoc, TargetLoc) < 10.0f)
	{
		CurrentTargetIndex++; // Aim for the next point in the array!

		if (!PathToFollow.IsValidIndex(CurrentTargetIndex))
		{
			bIsMoving = false; // Maze solved!
			return;
		}
		TargetLoc = PathToFollow[CurrentTargetIndex];
	}

	// 2. Move smoothly toward the target
	FVector NewLoc = FMath::VInterpConstantTo(CurrentLoc, TargetLoc, DeltaTime, MovementSpeed);
	SetActorLocation(NewLoc);

	// 3. Align perfectly to the surface of the sphere
	FVector UpDir = (NewLoc - SphereCenter).GetSafeNormal();
	FVector ForwardDir = (TargetLoc - NewLoc).GetSafeNormal();

	// Prevent mathematically impossible rotations if looking straight up
	if (ForwardDir.SizeSquared() > 0.001f)
	{
		FRotator NewRot = FRotationMatrix::MakeFromXZ(ForwardDir, UpDir).Rotator();
		SetActorRotation(NewRot);
	}
}
