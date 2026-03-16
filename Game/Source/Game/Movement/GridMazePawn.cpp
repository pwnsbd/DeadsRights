#include "Components/StaticMeshComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"

#include "GridMazePawn.h"

#include "Kismet/GameplayStatics.h"
#include "Orchestrator.h"

#include "Components/CapsuleComponent.h"
#include "GameFramework/FloatingPawnMovement.h"

#include "../Conversion/CubeToSphere.h"
#include "../Maze/Maze.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"

AGridMazePawn::AGridMazePawn()
{
	PrimaryActorTick.bCanEverTick = true;

	Capsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Capsule"));
	SetRootComponent(Capsule);

	Capsule->InitCapsuleSize(34.f, 88.f);
	Capsule->SetCollisionProfileName(TEXT("Pawn"));

	Movement = CreateDefaultSubobject<UFloatingPawnMovement>(TEXT("Movement"));
	Movement->SetUpdatedComponent(Capsule);

	PawnMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PawnMesh"));
	PawnMesh->SetupAttachment(Capsule);
	PawnMesh->SetRelativeLocation(FVector(0.f, 0.f, 50.f));

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(Capsule);
	SpringArm->TargetArmLength = 900.f;
	SpringArm->bDoCollisionTest = false;
	SpringArm->bUsePawnControlRotation = false;
	SpringArm->bInheritPitch = false;
	SpringArm->bInheritYaw = false;
	SpringArm->bInheritRoll = false;
	SpringArm->SetUsingAbsoluteRotation(true);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
	Camera->bUsePawnControlRotation = false;
	Camera->SetActive(true);

	AutoPossessPlayer = EAutoReceiveInput::Player0;
}

void AGridMazePawn::BeginPlay()
{
	Super::BeginPlay();

	AOrchestrator* Orch = nullptr;
	if (GetWorld())
	{
		Orch = Cast<AOrchestrator>(UGameplayStatics::GetActorOfClass(GetWorld(), AOrchestrator::StaticClass()));
	}

	if (Orch)
	{
		Orchestrator = Orch;
		if (!Sphere) Sphere = Orch->SphereActor;
		if (!Maze)   Maze = Orch->GetMaze();
	}

	if (Sphere && Maze)
	{
		FTransform SpawnT;
		const float HalfHeight = Capsule ? Capsule->GetUnscaledCapsuleHalfHeight() : 88.f;

		if (Orch && Orch->GetRandomSpawnTransform(SpawnT, HalfHeight))
		{
			int32 NewFace = 0, NewX = 0, NewY = 0;
			FindNearestCellToWorld(SpawnT.GetLocation(), NewFace, NewX, NewY);
			Face = NewFace;
			X = NewX;
			Y = NewY;
		}
		else
		{
			Face = StartFace;
			X = StartX;
			Y = StartY;
		}

		SnapToCell();
		DumpCurrentFaceAscii();
	}

	UE_LOG(LogTemp, Warning, TEXT("Pawn BeginPlay Sphere=%s Maze=%s Face=%d X=%d Y=%d"),
		*GetNameSafe(Sphere), *GetNameSafe(Maze), Face, X, Y);

	UpdateCameraToSphereCenter();

	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (PC->GetPawn() != this)
		{
			PC->Possess(this);
		}

		PC->SetViewTargetWithBlend(this, 0.0f);
	}
}

void AGridMazePawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (ULocalPlayer* LP = PC->GetLocalPlayer())
		{
			if (UEnhancedInputLocalPlayerSubsystem* Subsys = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
			{
				if (GridInputContext)
				{
					Subsys->ClearAllMappings();
					Subsys->AddMappingContext(GridInputContext, 0);
				}
			}
		}
	}

	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		if (IA_North && IA_South && IA_West && IA_East)
		{
			EIC->BindAction(IA_North, ETriggerEvent::Started, this, &AGridMazePawn::StepNorth);
			EIC->BindAction(IA_South, ETriggerEvent::Started, this, &AGridMazePawn::StepSouth);
			EIC->BindAction(IA_West,  ETriggerEvent::Started, this, &AGridMazePawn::StepWest);
			EIC->BindAction(IA_East,  ETriggerEvent::Started, this, &AGridMazePawn::StepEast);
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("SetupInput called. Context=%s North=%s"),
		*GetNameSafe(GridInputContext), *GetNameSafe(IA_North));
}

void AGridMazePawn::StepNorth() { TryStep(EMazeDir::N); }
void AGridMazePawn::StepSouth() { TryStep(EMazeDir::S); }
void AGridMazePawn::StepWest()  { TryStep(EMazeDir::W); }
void AGridMazePawn::StepEast()  { TryStep(EMazeDir::E); }

bool AGridMazePawn::IsOpen(const FMazeCell& Cell, EMazeDir Dir) const
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

/*bool AGridMazePawn::MapAcrossEdge(
	int32 InFace,
	int32 InX,
	int32 InY,
	EMazeDir Dir,
	int32 N,
	int32& OutFace,
	int32& OutX,
	int32& OutY
) const
{
	OutFace = InFace;
	OutX = InX;
	OutY = InY;

	if (N <= 0) return false;

	// Middle ring faces 0 1 2 3
	// Top face 4
	// Bottom face 5
	// This mapping matches UMaze::StitchFaces

	if (InFace >= 0 && InFace <= 3)
	{
		if (Dir == EMazeDir::E && InX == N - 1)
		{
			OutFace = (InFace + 1) % 4;
			OutX = 0;
			OutY = InY;
			return true;
		}

		if (Dir == EMazeDir::W && InX == 0)
		{
			OutFace = (InFace + 3) % 4;
			OutX = N - 1;
			OutY = InY;
			return true;
		}

		if (Dir == EMazeDir::N && InY == 0)
		{
			if (InFace == 0) { OutFace = 4; OutX = InX;       OutY = N - 1;       return true; }
			if (InFace == 1) { OutFace = 4; OutX = 0;         OutY = InX;           return true; }
			if (InFace == 2) { OutFace = 4; OutX = N - 1 - InX; OutY = 0;           return true; }
			if (InFace == 3) { OutFace = 4; OutX = N - 1;     OutY = N - 1 - InX;   return true; }
		}

		if (Dir == EMazeDir::S && InY == N - 1)
		{
			if (InFace == 0) { OutFace = 5; OutX = InX;       OutY = 0;             return true; }
			if (InFace == 1) { OutFace = 5; OutX = N - 1;     OutY = InX;           return true; }
			if (InFace == 2) { OutFace = 5; OutX = N - 1 - InX; OutY = N - 1;       return true; }
			if (InFace == 3) { OutFace = 5; OutX = 0;         OutY = N - 1 - InX;   return true; }
		}

		return false;
	}

	if (InFace == 4)
	{
		if (Dir == EMazeDir::S && InY == N - 1)
		{
			OutFace = 0;
			OutX = InX;
			OutY = 0;
			return true;
		}

		if (Dir == EMazeDir::W && InX == 0)
		{
			OutFace = 1;
			OutX = InY;
			OutY = 0;
			return true;
		}

		if (Dir == EMazeDir::N && InY == 0)
		{
			OutFace = 2;
			OutX = N - 1 - InX;
			OutY = 0;
			return true;
		}

		if (Dir == EMazeDir::E && InX == N - 1)
		{
			OutFace = 3;
			OutX = N - 1 - InY;
			OutY = 0;
			return true;
		}

		return false;
	}

	if (InFace == 5)
	{
		if (Dir == EMazeDir::N && InY == 0)
		{
			OutFace = 0;
			OutX = InX;
			OutY = N - 1;
			return true;
		}

		if (Dir == EMazeDir::E && InX == N - 1)
		{
			OutFace = 1;
			OutX = InY;
			OutY = N - 1;
			return true;
		}

		if (Dir == EMazeDir::S && InY == N - 1)
		{
			OutFace = 2;
			OutX = N - 1 - InX;
			OutY = N - 1;
			return true;
		}

		if (Dir == EMazeDir::W && InX == 0)
		{
			OutFace = 3;
			OutX = N - 1 - InY;
			OutY = N - 1;
			return true;
		}

		return false;
	}

	return false;
}*/

FVector AGridMazePawn::GetBasisSphereCenterWorld() const
{
	const FTransform BasisXform = Orchestrator
		? Orchestrator->GetActorTransform()
		: Sphere->GetActorTransform();

	return BasisXform.TransformPosition(FVector::ZeroVector);
}

FVector AGridMazePawn::BuildPlacedWorldLocationForCell(int32 InFace, int32 InX, int32 InY) const
{
	if (!Sphere)
	{
		return GetActorLocation();
	}

	const FVector CellCenterWorld = Sphere->GetCellCenterWorld(InFace, InX, InY);
	const FVector SphereCenterWorld = GetBasisSphereCenterWorld();
	const FVector UpDir = (CellCenterWorld - SphereCenterWorld).GetSafeNormal();

	const float HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 0.f;
	return CellCenterWorld + UpDir * (HalfHeight + 2.f + StepHeightOffset);
}

FRotator AGridMazePawn::BuildPlacedWorldRotationForCell(int32 InFace, int32 InX, int32 InY) const
{
	if (!Sphere)
	{
		return GetActorRotation();
	}

	const FVector CellCenterWorld = Sphere->GetCellCenterWorld(InFace, InX, InY);
	const FVector SphereCenterWorld = GetBasisSphereCenterWorld();
	const FVector UpDir = (CellCenterWorld - SphereCenterWorld).GetSafeNormal();

	return FRotationMatrix::MakeFromZ(UpDir).Rotator();
}

void AGridMazePawn::BeginStepTween(int32 OldFace, int32 OldX, int32 OldY, int32 NewFace, int32 NewX, int32 NewY)
{
	bStepTweenActive = true;
	StepTweenElapsed = 0.f;

	StepTweenSphereCenter = GetBasisSphereCenterWorld();

	StepTweenStartLocation = BuildPlacedWorldLocationForCell(OldFace, OldX, OldY);
	StepTweenTargetLocation = BuildPlacedWorldLocationForCell(NewFace, NewX, NewY);

	StepTweenStartRotation = BuildPlacedWorldRotationForCell(OldFace, OldX, OldY);
	StepTweenTargetRotation = BuildPlacedWorldRotationForCell(NewFace, NewX, NewY);
}

void AGridMazePawn::UpdateStepTween(float DeltaSeconds)
{
	if (!bStepTweenActive)
	{
		return;
	}

	StepTweenElapsed += DeltaSeconds;

	const float Alpha = FMath::Clamp(StepTweenElapsed / FMath::Max(0.001f, StepTweenDuration), 0.f, 1.f);
	const float SmoothAlpha = FMath::InterpEaseInOut(0.f, 1.f, Alpha, 2.0f);

	// Move along the sphere shell instead of straight-line drifting through space
	const FVector StartFromCenter = StepTweenStartLocation - StepTweenSphereCenter;
	const FVector TargetFromCenter = StepTweenTargetLocation - StepTweenSphereCenter;

	const FVector StartDir = StartFromCenter.GetSafeNormal();
	const FVector TargetDir = TargetFromCenter.GetSafeNormal();

	const float StartRadius = StartFromCenter.Size();
	const float TargetRadius = TargetFromCenter.Size();
	const float Radius = FMath::Lerp(StartRadius, TargetRadius, SmoothAlpha);

	const FVector Dir = FQuat::Slerp(
		FQuat::FindBetweenNormals(FVector::ForwardVector, StartDir),
		FQuat::FindBetweenNormals(FVector::ForwardVector, TargetDir),
		SmoothAlpha
	).RotateVector(FVector::ForwardVector).GetSafeNormal();

	const FVector NewLocation = StepTweenSphereCenter + Dir * Radius;
	const FRotator NewRotation = FMath::Lerp(StepTweenStartRotation, StepTweenTargetRotation, SmoothAlpha);

	SetActorLocationAndRotation(
		NewLocation,
		NewRotation,
		false,
		nullptr,
		ETeleportType::TeleportPhysics
	);

	if (Alpha >= 1.f)
	{
		bStepTweenActive = false;
		SnapToCell();
	}
}

bool AGridMazePawn::TryStep(EMazeDir Dir)
{
	if (!Sphere || !Maze) return false;
	if (bStepTweenActive) return false;

	const int32 N = FMath::Max(1, Maze->CellsPerFace);

	UE_LOG(LogTemp, Warning, TEXT("TryStep Face=%d X=%d Y=%d Dir=%d"), Face, X, Y, (int32)Dir);

	const FMazeCell& Cell = Maze->GetCell(Face, X, Y);
	UE_LOG(LogTemp, Warning, TEXT("Open N=%d E=%d S=%d W=%d"),
		Cell.OpenN ? 1 : 0,
		Cell.OpenE ? 1 : 0,
		Cell.OpenS ? 1 : 0,
		Cell.OpenW ? 1 : 0);

	auto PrintFaceNow = [&]()
	{
		DumpCurrentFaceAscii();
	};

	if (!IsOpen(Cell, Dir))
	{
		UE_LOG(LogTemp, Warning, TEXT("Blocked by wall"));
		PrintFaceNow();
		return false;
	}

	int32 NewFace = Face;
	int32 NewX = X;
	int32 NewY = Y;

	if (Dir == EMazeDir::N) NewY--;
	if (Dir == EMazeDir::S) NewY++;
	if (Dir == EMazeDir::W) NewX--;
	if (Dir == EMazeDir::E) NewX++;

	if (NewX < 0 || NewX >= N || NewY < 0 || NewY >= N)
	{
		const FMazeNode CurrentNode(Face, X, Y);
		FMazeNode OutNode;

		if (!Maze->TryFaceTransition(CurrentNode, Dir, OutNode))
		{
			UE_LOG(LogTemp, Warning, TEXT("Out of bounds but no seam mapping found"));
			return false;
		}

		NewFace = OutNode.Face;
		NewX = OutNode.X;
		NewY = OutNode.Y;
	}

	const int32 OldFace = Face;
	const int32 OldX = X;
	const int32 OldY = Y;

	Face = NewFace;
	X = NewX;
	Y = NewY;

	BeginStepTween(OldFace, OldX, OldY, Face, X, Y);

	PrintFaceNow();
	return true;
}

void AGridMazePawn::SnapToCell()
{
	if (!Sphere) return;

	const FVector NewLocation = BuildPlacedWorldLocationForCell(Face, X, Y);
	const FRotator NewRotation = BuildPlacedWorldRotationForCell(Face, X, Y);

	SetActorLocationAndRotation(
		NewLocation,
		NewRotation,
		false,
		nullptr,
		ETeleportType::TeleportPhysics
	);

	const FVector PawnLoc = GetActorLocation();
	const FVector SphereCenterWorld = GetBasisSphereCenterWorld();

	UE_LOG(LogTemp, Warning,
		TEXT("Pawn World Pos: X=%.2f Y=%.2f Z=%.2f"),
		PawnLoc.X, PawnLoc.Y, PawnLoc.Z);

	UE_LOG(LogTemp, Warning,
		TEXT("Sphere Center: X=%.2f Y=%.2f Z=%.2f"),
		SphereCenterWorld.X, SphereCenterWorld.Y, SphereCenterWorld.Z);

	const float DistFromCenter = FVector::Dist(SphereCenterWorld, PawnLoc);
	const float SphereRadius = Sphere->GetRadius();

	UE_LOG(LogTemp, Warning,
		TEXT("DistFromCenter=%.2f  SphereRadius=%.2f  Delta=%.2f"),
		DistFromCenter, SphereRadius, DistFromCenter - SphereRadius);
}

bool AGridMazePawn::FindNearestCellToWorld(const FVector& WorldPos, int32& OutFace, int32& OutX, int32& OutY) const
{
	if (!Sphere) return false;

	const int32 N = Sphere->GetCellsPerFace();

	float BestD2 = TNumericLimits<float>::Max();
	int32 BestF = 0;
	int32 BestX = 0;
	int32 BestY = 0;

	for (int32 F = 0; F < 6; F++)
	{
		for (int32 cy = 0; cy < N; cy++)
		{
			for (int32 cx = 0; cx < N; cx++)
			{
				const FVector C = Sphere->GetCellCenterWorld(F, cx, cy);
				const float D2 = FVector::DistSquared(C, WorldPos);
				if (D2 < BestD2)
				{
					BestD2 = D2;
					BestF = F;
					BestX = cx;
					BestY = cy;
				}
			}
		}
	}

	OutFace = BestF;
	OutX = BestX;
	OutY = BestY;
	return true;
}

void AGridMazePawn::DumpCurrentFaceAscii() const
{
	DumpFaceAscii(Face);
}

void AGridMazePawn::DumpFaceAscii(int32 FaceToDump) const
{
	if (!Maze)
	{
		UE_LOG(LogTemp, Warning, TEXT("DumpFaceAscii skipped Maze is null"));
		return;
	}

	const int32 N = FMath::Max(1, Maze->CellsPerFace);

	UE_LOG(LogTemp, Warning, TEXT("MAZE FACE %d N %d Pawn Face %d X %d Y %d"),
		FaceToDump, N, Face, X, Y);

	auto TopWall = [&](int32 cx, int32 cy) -> const TCHAR*
	{
		const FMazeCell& C = Maze->GetCell(FaceToDump, cx, cy);
		return C.OpenN ? TEXT("   ") : TEXT("---");
	};

	auto LeftWall = [&](int32 cx, int32 cy) -> const TCHAR*
	{
		const FMazeCell& C = Maze->GetCell(FaceToDump, cx, cy);
		return C.OpenW ? TEXT(" ") : TEXT("|");
	};

	auto BottomWall = [&](int32 cx, int32 cy) -> const TCHAR*
	{
		const FMazeCell& C = Maze->GetCell(FaceToDump, cx, cy);
		return C.OpenS ? TEXT("   ") : TEXT("---");
	};

	for (int32 cy = 0; cy < N; ++cy)
	{
		{
			FString Line;
			for (int32 cx = 0; cx < N; ++cx)
			{
				Line += TEXT("+");
				Line += TopWall(cx, cy);
			}
			Line += TEXT("+");
			UE_LOG(LogTemp, Warning, TEXT("%s"), *Line);
		}

		{
			FString Line;
			for (int32 cx = 0; cx < N; ++cx)
			{
				Line += LeftWall(cx, cy);

				const bool bMarkPawn = (FaceToDump == Face && cx == X && cy == Y);
				Line += TEXT(" ");
				Line += (bMarkPawn ? TEXT("P") : TEXT(" "));
				Line += TEXT(" ");
			}

			const FMazeCell& Last = Maze->GetCell(FaceToDump, N - 1, cy);
			Line += (Last.OpenE ? TEXT(" ") : TEXT("|"));

			UE_LOG(LogTemp, Warning, TEXT("%s"), *Line);
		}
	}

	{
		FString Line;
		const int32 cy = N - 1;
		for (int32 cx = 0; cx < N; ++cx)
		{
			Line += TEXT("+");
			Line += BottomWall(cx, cy);
		}
		Line += TEXT("+");
		UE_LOG(LogTemp, Warning, TEXT("%s"), *Line);
	}

	UE_LOG(LogTemp, Warning, TEXT("END FACE %d"), FaceToDump);
}

void AGridMazePawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UpdateStepTween(DeltaSeconds);
	UpdateCameraToSphereCenter();
}

void AGridMazePawn::UpdateCameraToSphereCenter()
{
	if (!SpringArm || !Sphere)
	{
		return;
	}

	const FTransform BasisXform = Orchestrator
		? Orchestrator->GetActorTransform()
		: Sphere->GetActorTransform();

	const FVector SphereCenterWorld = BasisXform.TransformPosition(FVector::ZeroVector);
	const FVector PawnWorld = GetActorLocation();

	const FVector ToCenter = (PawnWorld - SphereCenterWorld).GetSafeNormal();
	if (ToCenter.IsNearlyZero())
	{
		return;
	}

	FVector StableUp = FVector::VectorPlaneProject(FVector::UpVector, ToCenter).GetSafeNormal();

	if (StableUp.IsNearlyZero())
	{
		StableUp = FVector::VectorPlaneProject(GetActorForwardVector(), ToCenter).GetSafeNormal();
	}

	if (StableUp.IsNearlyZero())
	{
		StableUp = FVector::ForwardVector;
	}

	const FRotator LookAtCenter = FRotationMatrix::MakeFromXZ(ToCenter, StableUp).Rotator();
	SpringArm->SetWorldRotation(LookAtCenter);
}

void AGridMazePawn::CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult)
{
	if (Camera && Camera->IsActive())
	{
		Camera->GetCameraView(DeltaTime, OutResult);
		return;
	}

	Super::CalcCamera(DeltaTime, OutResult);
}
