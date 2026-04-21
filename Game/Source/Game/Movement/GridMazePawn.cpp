#include "GridMazePawn.h"

#include "Components/StaticMeshComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"

#include "Kismet/GameplayStatics.h"
#include "../Orchestrator.h"

#include "Components/CapsuleComponent.h"
#include "GameFramework/FloatingPawnMovement.h"

#include "../Conversion/CubeToSphere.h"
#include "../Maze/Maze.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"


#include "../Artifact/Artifact.h"
#include "Components/SphereComponent.h"
#include "InputCoreTypes.h"
#include "GameFramework/PlayerController.h"

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

void AGridMazePawn::RefreshAfterMazeRebuild()
{
	bStepTweenActive = false;
	StepTweenElapsed = 0.f;

	AOrchestrator* Orch = nullptr;
	if (GetWorld())
	{
		Orch = Cast<AOrchestrator>(UGameplayStatics::GetActorOfClass(GetWorld(), AOrchestrator::StaticClass()));
	}

	if (Orch)
	{
		Orchestrator = Orch;
		Sphere = Orch->SphereActor;
		Maze   = Orch->GetMaze();
	}

	UE_LOG(LogTemp, Warning,
		TEXT("RefreshAfterMazeRebuild START Orch=%s Sphere=%s Maze=%s"),
		*GetNameSafe(Orchestrator),
		*GetNameSafe(Sphere),
		*GetNameSafe(Maze));

	if (!Sphere || !Maze)
	{
		UE_LOG(LogTemp, Error, TEXT("RefreshAfterMazeRebuild FAIL missing Sphere or Maze"));
		return;
	}

	FTransform SpawnT;
	const float HalfHeight = Capsule ? Capsule->GetUnscaledCapsuleHalfHeight() : 88.f;

	if (Orchestrator && Orchestrator->GetRandomSpawnTransform(SpawnT, HalfHeight))
	{
		int32 NewFace = 0;
		int32 NewX = 0;
		int32 NewY = 0;

		if (FindNearestCellToWorld(SpawnT.GetLocation(), NewFace, NewX, NewY))
		{
			Face = NewFace;
			X = NewX;
			Y = NewY;

			UE_LOG(LogTemp, Warning,
				TEXT("RefreshAfterMazeRebuild picked random valid cell Face=%d X=%d Y=%d"),
				Face, X, Y);
		}
		else
		{
			Face = StartFace;
			X = StartX;
			Y = StartY;

			UE_LOG(LogTemp, Warning,
				TEXT("RefreshAfterMazeRebuild fallback FindNearest failed StartFace=%d StartX=%d StartY=%d"),
				Face, X, Y);
		}
	}
	else
	{
		Face = StartFace;
		X = StartX;
		Y = StartY;

		UE_LOG(LogTemp, Warning,
			TEXT("RefreshAfterMazeRebuild fallback no spawn transform StartFace=%d StartX=%d StartY=%d"),
			Face, X, Y);
	}

	SnapToCell();
	UpdateCameraToSphereCenter();
	DumpCurrentFaceAscii();

	if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		if (PC->GetPawn() != this)
		{
			PC->Possess(this);
		}

		PC->SetViewTargetWithBlend(this, 0.0f);
	}

	UE_LOG(LogTemp, Warning,
		TEXT("RefreshAfterMazeRebuild END Face=%d X=%d Y=%d"),
		Face, X, Y);
}

void AGridMazePawn::BeginPlay()
{
	Super::BeginPlay();

	InventoryArtifacts.SetNumZeroed(MaxArtifacts);

	RefreshAfterMazeRebuild();

	UE_LOG(LogTemp, Warning, TEXT("Pawn BeginPlay complete"));
}

/*
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
*/
void AGridMazePawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UE_LOG(LogTemp, Warning,
		TEXT("SetupInput START Pawn=%s Controller=%s InputComp=%s"),
		*GetNameSafe(this),
		*GetNameSafe(GetController()),
		*GetNameSafe(PlayerInputComponent));

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("SetupInput PlayerController=%s LocalPlayer=%s"),
			*GetNameSafe(PC),
			PC->GetLocalPlayer() ? TEXT("VALID") : TEXT("NULL"));

		if (ULocalPlayer* LP = PC->GetLocalPlayer())
		{
			if (UEnhancedInputLocalPlayerSubsystem* Subsys = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
			{
				UE_LOG(LogTemp, Warning,
					TEXT("SetupInput EnhancedInput subsystem VALID. Context=%s"),
					*GetNameSafe(GridInputContext));

				if (GridInputContext)
				{
					Subsys->ClearAllMappings();
					Subsys->AddMappingContext(GridInputContext, 0);

					UE_LOG(LogTemp, Warning,
						TEXT("SetupInput Added mapping context %s"),
						*GetNameSafe(GridInputContext));
				}
				else
				{
					UE_LOG(LogTemp, Error, TEXT("SetupInput GridInputContext is NULL"));
				}
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("SetupInput EnhancedInput subsystem is NULL"));
			}
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("SetupInput Controller is not an APlayerController"));
	}

	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("SetupInput EnhancedInputComponent VALID. North=%s South=%s West=%s East=%s"),
			*GetNameSafe(IA_North),
			*GetNameSafe(IA_South),
			*GetNameSafe(IA_West),
			*GetNameSafe(IA_East));

		if (IA_North && IA_South && IA_West && IA_East)
		{
			EIC->BindAction(IA_North, ETriggerEvent::Started, this, &AGridMazePawn::StepNorth);
			EIC->BindAction(IA_South, ETriggerEvent::Started, this, &AGridMazePawn::StepSouth);
			EIC->BindAction(IA_West,  ETriggerEvent::Started, this, &AGridMazePawn::StepWest);
			EIC->BindAction(IA_East,  ETriggerEvent::Started, this, &AGridMazePawn::StepEast);

			UE_LOG(LogTemp, Warning, TEXT("SetupInput Actions bound successfully"));
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("SetupInput one or more InputActions are NULL"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("SetupInput PlayerInputComponent is not UEnhancedInputComponent"));
	}

	PlayerInputComponent->BindKey(EKeys::One,   IE_Pressed, this, &AGridMazePawn::UseArtifactSlot1);
	PlayerInputComponent->BindKey(EKeys::Two,   IE_Pressed, this, &AGridMazePawn::UseArtifactSlot2);
	PlayerInputComponent->BindKey(EKeys::Three, IE_Pressed, this, &AGridMazePawn::UseArtifactSlot3);
	PlayerInputComponent->BindKey(EKeys::Four,  IE_Pressed, this, &AGridMazePawn::UseArtifactSlot4);

	PlayerInputComponent->BindKey(EKeys::NumPadOne,   IE_Pressed, this, &AGridMazePawn::UseArtifactSlot1);
	PlayerInputComponent->BindKey(EKeys::NumPadTwo,   IE_Pressed, this, &AGridMazePawn::UseArtifactSlot2);
	PlayerInputComponent->BindKey(EKeys::NumPadThree, IE_Pressed, this, &AGridMazePawn::UseArtifactSlot3);
	PlayerInputComponent->BindKey(EKeys::NumPadFour,  IE_Pressed, this, &AGridMazePawn::UseArtifactSlot4);

	UE_LOG(LogTemp, Warning, TEXT("SetupInput END"));
}


// relative inputs
EMazeDir AGridMazePawn::GetScreenRelativeDir(const FVector& ScreenVectorWorld) const
{
	if (!Sphere)
	{
		return EMazeDir::N;
	}

	const FVector SphereCenterWorld = GetBasisSphereCenterWorld();
	const FVector PawnWorld = GetActorLocation();

	const FVector SurfaceNormal = (PawnWorld - SphereCenterWorld).GetSafeNormal();
	if (SurfaceNormal.IsNearlyZero())
	{
		return EMazeDir::N;
	}

	FVector TangentVector = FVector::VectorPlaneProject(ScreenVectorWorld, SurfaceNormal).GetSafeNormal();
	if (TangentVector.IsNearlyZero())
	{
		return EMazeDir::N;
	}

	const FMazeNode CurrentNode(Face, X, Y);
	return Sphere->GetDirectionFromVector(TangentVector, CurrentNode);
}



//god save me

int32 AGridMazePawn::GetPoleWedge(int32 InX, int32 InY) const
{
	if (!Maze)
	{
		return 0;
	}

	const int32 N = FMath::Max(1, Maze->CellsPerFace);
	const int32 Max = N - 1;

	const bool bUnderYX = (InY > InX);
	const bool bUnderYMaxMinusX = (InY > (Max - InX));

	if (!bUnderYX && !bUnderYMaxMinusX) return 0;
	if (!bUnderYX &&  bUnderYMaxMinusX) return 1;
	if ( bUnderYX &&  bUnderYMaxMinusX) return 2;
	return 3;
}

EMazeDir AGridMazePawn::RemapPoleInput(EMazeDir BaseDir) const
{
	if ((Face != 4 && Face != 5) || !Maze)
	{
		return BaseDir;
	}

	const int32 Wedge = GetPoleWedge(X, Y);

	// Row = wedge 0..3
	// Col = BaseDir (N=0, E=1, S=2, W=3)
	//
	// Face 4:
	//   W0: N E S W
	//   W1: E S W N
	//   W2: S W N E
	//   W3: W N E S
	//
	// Face 5:
	//   W0: N E S W
	//   W1: W N E S
	//   W2: S W N E
	//   W3: E S W N

	static const EMazeDir Face4Map[4][4] =
	{
		{ EMazeDir::N, EMazeDir::E, EMazeDir::S, EMazeDir::W }, // wedge 0
		{ EMazeDir::E, EMazeDir::S, EMazeDir::W, EMazeDir::N }, // wedge 1
		{ EMazeDir::S, EMazeDir::W, EMazeDir::N, EMazeDir::E }, // wedge 2
		{ EMazeDir::W, EMazeDir::N, EMazeDir::E, EMazeDir::S }  // wedge 3
	};

	static const EMazeDir Face5Map[4][4] =
	{
		{ EMazeDir::S, EMazeDir::W, EMazeDir::N, EMazeDir::E }, // wedge 0  keep
		{ EMazeDir::W, EMazeDir::N, EMazeDir::E, EMazeDir::S }, // wedge 1  keep
		{ EMazeDir::N, EMazeDir::E, EMazeDir::S, EMazeDir::W }, // wedge 2  FIXED
		{ EMazeDir::W, EMazeDir::N, EMazeDir::E, EMazeDir::S }  // wedge 3  try flipped
	};
	const int32 DirIndex = (int32)BaseDir;
	const EMazeDir OutDir = (Face == 4)
		? Face4Map[Wedge][DirIndex]
		: Face5Map[Wedge][DirIndex];

	UE_LOG(LogTemp, Warning,
		TEXT("PoleRemap Face=%d X=%d Y=%d Wedge=%d BaseDir=%d OutDir=%d"),
		Face, X, Y, Wedge, DirIndex, (int32)OutDir);

	return OutDir;
}
///-0--------------------------

void AGridMazePawn::StepNorth()
{
	EMazeDir Dir = EMazeDir::N;

	if (Face == 4 || Face == 5)
	{
		Dir = RemapPoleInput(EMazeDir::N);
		UE_LOG(LogTemp, Warning, TEXT("INPUT StepNorth pole remap -> Dir=%d"), (int32)Dir);
	}
	else
	{
		Dir = GetScreenRelativeDir(Camera ? Camera->GetUpVector() : FVector::ForwardVector);
		UE_LOG(LogTemp, Warning, TEXT("INPUT StepNorth screen-up -> Dir=%d"), (int32)Dir);
	}

	TryStep(Dir);
}

void AGridMazePawn::StepSouth()
{
	EMazeDir Dir = EMazeDir::S;

	if (Face == 4 || Face == 5)
	{
		Dir = RemapPoleInput(EMazeDir::S);
		UE_LOG(LogTemp, Warning, TEXT("INPUT StepSouth pole remap -> Dir=%d"), (int32)Dir);
	}
	else
	{
		Dir = GetScreenRelativeDir(Camera ? -Camera->GetUpVector() : FVector::BackwardVector);
		UE_LOG(LogTemp, Warning, TEXT("INPUT StepSouth screen-down -> Dir=%d"), (int32)Dir);
	}

	TryStep(Dir);
}

void AGridMazePawn::StepWest()
{
	EMazeDir Dir = EMazeDir::W;

	if (Face == 4 || Face == 5)
	{
		Dir = RemapPoleInput(EMazeDir::W);
		UE_LOG(LogTemp, Warning, TEXT("INPUT StepWest pole remap -> Dir=%d"), (int32)Dir);
	}
	else
	{
		Dir = GetScreenRelativeDir(Camera ? -Camera->GetRightVector() : -FVector::RightVector);
		UE_LOG(LogTemp, Warning, TEXT("INPUT StepWest screen-left -> Dir=%d"), (int32)Dir);
	}

	TryStep(Dir);
}

void AGridMazePawn::StepEast()
{
	EMazeDir Dir = EMazeDir::E;

	if (Face == 4 || Face == 5)
	{
		Dir = RemapPoleInput(EMazeDir::E);
		UE_LOG(LogTemp, Warning, TEXT("INPUT StepEast pole remap -> Dir=%d"), (int32)Dir);
	}
	else
	{
		Dir = GetScreenRelativeDir(Camera ? Camera->GetRightVector() : FVector::RightVector);
		UE_LOG(LogTemp, Warning, TEXT("INPUT StepEast screen-right -> Dir=%d"), (int32)Dir);
	}

	TryStep(Dir);
}
//

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
	UE_LOG(LogTemp, Warning,
		TEXT("TryStep START Dir=%d Face=%d X=%d Y=%d Sphere=%s Maze=%s StepTweenActive=%s"),
		(int32)Dir,
		Face,
		X,
		Y,
		*GetNameSafe(Sphere),
		*GetNameSafe(Maze),
		bStepTweenActive ? TEXT("true") : TEXT("false"));
	
	if (!Sphere)
	{
		UE_LOG(LogTemp, Error, TEXT("TryStep FAIL Sphere is NULL"));
		return false;
	}

	if (!Maze)
	{
		UE_LOG(LogTemp, Error, TEXT("TryStep FAIL Maze is NULL"));
		return false;
	}

	if (bStepTweenActive)
	{
		UE_LOG(LogTemp, Warning, TEXT("TryStep BLOCKED step tween still active"));
		return false;
	}

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
InventoryLogTimer += DeltaSeconds;

if (InventoryLogTimer >= 2.0f)
{
	InventoryLogTimer = 0.f;
	LogInventoryState(TEXT("Periodic"));
}

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

    const FVector SphereCenterWorld = GetBasisSphereCenterWorld();
    const FVector PawnWorld        = GetActorLocation();

    const FVector OutwardNormal = (PawnWorld - SphereCenterWorld).GetSafeNormal();
    if (OutwardNormal.IsNearlyZero())
    {
        return;
    }

    const FVector InwardNormal = -OutwardNormal;   // ← THIS IS THE KEY CHANGE

    // Keep your stable up vector (prevents camera roll flipping)
    FVector StableUp = FVector::VectorPlaneProject(FVector::UpVector, OutwardNormal).GetSafeNormal();
    if (StableUp.IsNearlyZero())
    {
        StableUp = FVector::VectorPlaneProject(GetActorForwardVector(), OutwardNormal).GetSafeNormal();
    }
    if (StableUp.IsNearlyZero())
    {
        StableUp = FVector::ForwardVector;
    }

    // Now SpringArm Forward = inward → camera gets pushed OUTWARD along the radial line
    const FRotator SpringArmRot = FRotationMatrix::MakeFromXZ(InwardNormal, StableUp).Rotator();

    SpringArm->SetWorldRotation(SpringArmRot);

    // Optional: make camera look slightly down at the pawn (classic 3rd-person feel)
    // Camera->SetWorldRotation( FRotator(SpringArmRot.Pitch + 5.0f, SpringArmRot.Yaw, 0.0f) );
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


int32 AGridMazePawn::FindFirstEmptyArtifactSlot() const
{
	for (int32 i = 0; i < InventoryArtifacts.Num(); ++i)
	{
		if (!IsValid(InventoryArtifacts[i]))
		{
			return i;
		}
	}

	return INDEX_NONE;
}

int32 AGridMazePawn::GetArtifactCount() const
{
	int32 Count = 0;

	for (AArtifact* Artifact : InventoryArtifacts)
	{
		if (IsValid(Artifact))
		{
			++Count;
		}
	}

	return Count;
}

void AGridMazePawn::UpdateArtifactCarryVisuals()
{
	if (!GetRootComponent())
	{
		return;
	}

	for (int32 i = 0; i < InventoryArtifacts.Num(); ++i)
	{
		AArtifact* Artifact = InventoryArtifacts[i];
		if (!IsValid(Artifact))
		{
			continue;
		}

		Artifact->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);

		const float SideOffset = (i - 1.5f) * ArtifactCarrySpacing;
		const FVector SlotOffset = ArtifactCarryBaseOffset + FVector(0.f, SideOffset, 0.f);

		Artifact->SetActorRelativeLocation(SlotOffset);
		Artifact->SetActorRelativeRotation(FRotator::ZeroRotator);
		Artifact->SetActorRelativeScale3D(ArtifactCarryScale);
	}
}

bool AGridMazePawn::AddArtifactToInventory(AArtifact* Artifact)
{
	if (!IsValid(Artifact))
	{
		UE_LOG(LogTemp, Warning, TEXT("AddArtifactToInventory FAIL invalid artifact"));
		return false;
	}

	if (InventoryArtifacts.Num() == 0)
	{
		InventoryArtifacts.SetNumZeroed(MaxArtifacts);
	}

	const int32 SlotIndex = FindFirstEmptyArtifactSlot();
	if (SlotIndex == INDEX_NONE)
	{
		UE_LOG(LogTemp, Warning, TEXT("Inventory full, could not pick up artifact"));
		LogInventoryState(TEXT("Pickup Failed Full"));
		return false;
	}

	InventoryArtifacts[SlotIndex] = Artifact;

	Artifact->bIsCarried = true;
	Artifact->Carrier = this;

	if (Artifact->PickupTrigger)
	{
		Artifact->PickupTrigger->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	if (Artifact->MeshComponent)
	{
		Artifact->MeshComponent->SetSimulatePhysics(false);
		Artifact->MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	UpdateArtifactCarryVisuals();

	UE_LOG(LogTemp, Warning,
		TEXT("Picked up artifact into slot %d  Type=%d  Count=%d"),
		SlotIndex + 1,
		(int32)Artifact->ArtifactType,
		GetArtifactCount());

		LogInventoryState(TEXT("After Pickup"));

	LogInventoryState(TEXT("After Pickup"));
	return true;
}

bool AGridMazePawn::UseArtifactInSlot(int32 SlotIndex)
{
	if (!InventoryArtifacts.IsValidIndex(SlotIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("UseArtifactInSlot FAIL invalid slot %d"), SlotIndex);
		LogInventoryState(TEXT("Use Failed Invalid Slot"));
		return false;
	}

	AArtifact* Artifact = InventoryArtifacts[SlotIndex];
	if (!IsValid(Artifact))
	{
		UE_LOG(LogTemp, Warning, TEXT("UseArtifactInSlot slot %d is empty"), SlotIndex + 1);
		LogInventoryState(TEXT("Use Failed Empty Slot"));
		return false;
	}

	UE_LOG(LogTemp, Warning,
		TEXT("Using artifact in slot %d  Type=%d"),
		SlotIndex + 1,
		(int32)Artifact->ArtifactType);

	LogInventoryState(TEXT("Before Use"));

	Artifact->ActivateAbility();

	LogInventoryState(TEXT("After Use"));

	if (!IsValid(Artifact) || Artifact->CurrentCharges <= 0)
	{
		if (IsValid(Artifact))
		{
			Artifact->Destroy();
		}

		InventoryArtifacts[SlotIndex] = nullptr;
		UpdateArtifactCarryVisuals();

		UE_LOG(LogTemp, Warning, TEXT("Artifact in slot %d depleted and removed"), SlotIndex + 1);
		LogInventoryState(TEXT("After Removal"));
	}

	LogInventoryState(TEXT("After Use"));
	return true;
}

void AGridMazePawn::UseArtifactSlot1()
{
	UseArtifactInSlot(0);
}

void AGridMazePawn::UseArtifactSlot2()
{
	UseArtifactInSlot(1);
}

void AGridMazePawn::UseArtifactSlot3()
{
	UseArtifactInSlot(2);
}

void AGridMazePawn::UseArtifactSlot4()
{
	UseArtifactInSlot(3);
}


void AGridMazePawn::LogInventoryState(const TCHAR* Context) const
{
	FString InventoryText = FString::Printf(TEXT("[Inventory] %s | "), Context);

	for (int32 i = 0; i < InventoryArtifacts.Num(); ++i)
	{
		const AArtifact* Artifact = InventoryArtifacts[i];

		if (!IsValid(Artifact))
		{
			InventoryText += FString::Printf(TEXT("Slot%d=Empty "), i + 1);
			continue;
		}

		FString TypeName = TEXT("None");

		switch (Artifact->ArtifactType)
		{
		case EArtifactType::Beam:       TypeName = TEXT("Red/Beam"); break;
		case EArtifactType::PhaseWalk:  TypeName = TEXT("Green/PhaseWalk"); break;
		case EArtifactType::PathFinder: TypeName = TEXT("Yellow/PathFinder"); break;
		case EArtifactType::Barrier:    TypeName = TEXT("Blue/Barrier"); break;
		default: break;
		}

		InventoryText += FString::Printf(
			TEXT("Slot%d=%s Charges=%d "),
			i + 1,
			*TypeName,
			Artifact->CurrentCharges);
	}

	UE_LOG(LogTemp, Warning, TEXT("%s"), *InventoryText);
}