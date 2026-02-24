#include "GridMazePawn.h"

#include "Components/CapsuleComponent.h"
#include "GameFramework/FloatingPawnMovement.h"

#include "../Conversion/CubeToSphere.h"
#include "../Maze/Maze.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"

AGridMazePawn::AGridMazePawn()
{
	PrimaryActorTick.bCanEverTick = false;

	Capsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Capsule"));
	SetRootComponent(Capsule);

	Movement = CreateDefaultSubobject<UFloatingPawnMovement>(TEXT("Movement"));
	AutoPossessPlayer = EAutoReceiveInput::Player0;
}

void AGridMazePawn::BeginPlay()
{
	Super::BeginPlay();

	Face = StartFace;
	X = StartX;
	Y = StartY;

	if (Sphere)
	{
		const FVector P = GetCellWorld(Face, X, Y);
		SetActorLocation(P);
	}
}

void AGridMazePawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (ULocalPlayer* LP = PC->GetLocalPlayer())
		{
			if (UEnhancedInputLocalPlayerSubsystem* Subsys =
				LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
			{
				if (!GridInputContext) return;

				Subsys->ClearAllMappings();
				Subsys->AddMappingContext(GridInputContext, 0);
			}
		}
	}

	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		if (!IA_North || !IA_South || !IA_West || !IA_East) return;

		EIC->BindAction(IA_North, ETriggerEvent::Triggered, this, &AGridMazePawn::StepNorth);
		EIC->BindAction(IA_South, ETriggerEvent::Triggered, this, &AGridMazePawn::StepSouth);
		EIC->BindAction(IA_West,  ETriggerEvent::Triggered, this, &AGridMazePawn::StepWest);
		EIC->BindAction(IA_East,  ETriggerEvent::Triggered, this, &AGridMazePawn::StepEast);
	}
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

FVector AGridMazePawn::GetCellWorld(int32 InFace, int32 InX, int32 InY) const
{
	FVector P = Sphere->GetCellCenterWorld(InFace, InX, InY);

	const FVector Center = Sphere->GetActorLocation();
	const FVector Normal = (P - Center).GetSafeNormal();
	P += Normal * StepHeightOffset;

	return P;
}

FVector AGridMazePawn::GetNorthTangentWorld() const
{
	const FVector Center = Sphere->GetActorLocation();

	const FVector P = Sphere->GetCellCenterWorld(Face, X, Y);
	const FVector N = (P - Center).GetSafeNormal();

	const FVector P2 = Sphere->GetCellCenterWorld(Face, X, FMath::Max(0, Y - 1));
	const FVector D = (P2 - P).GetSafeNormal();

	FVector T = D - FVector::DotProduct(D, N) * N;
	return T.GetSafeNormal();
}

FVector AGridMazePawn::GetEastTangentWorld() const
{
	const FVector Center = Sphere->GetActorLocation();

	const FVector P = Sphere->GetCellCenterWorld(Face, X, Y);
	const FVector N = (P - Center).GetSafeNormal();

	const FVector P2 = Sphere->GetCellCenterWorld(Face, FMath::Min(Sphere->GetCellsPerFace() - 1, X + 1), Y);
	const FVector D = (P2 - P).GetSafeNormal();

	FVector T = D - FVector::DotProduct(D, N) * N;
	return T.GetSafeNormal();
}

bool AGridMazePawn::FindNearestCellToWorld(const FVector& WorldPos, int32& OutFace, int32& OutX, int32& OutY) const
{
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

bool AGridMazePawn::TryStep(EMazeDir Dir)
{
	if (!Sphere || !Maze) return false;

	const FMazeCell& Cell = Maze->GetCell(Face, X, Y);
	if (!IsOpen(Cell, Dir)) return false;

	const FVector P = Sphere->GetCellCenterWorld(Face, X, Y);

	FVector Tangent;
	if (Dir == EMazeDir::N) Tangent = GetNorthTangentWorld();
	if (Dir == EMazeDir::S) Tangent = -GetNorthTangentWorld();
	if (Dir == EMazeDir::E) Tangent = GetEastTangentWorld();
	if (Dir == EMazeDir::W) Tangent = -GetEastTangentWorld();

	const FVector Predicted = P + Tangent * StepPredictDistance;

	int32 NewFace, NewX, NewY;
	FindNearestCellToWorld(Predicted, NewFace, NewX, NewY);

	Face = NewFace;
	X = NewX;
	Y = NewY;

	const FVector Target = GetCellWorld(Face, X, Y);

	SetActorLocation(Target);

	const FVector Center = Sphere->GetActorLocation();
	const FVector Normal = (Target - Center).GetSafeNormal();
	SetActorRotation(Normal.ToOrientationRotator());

	return true;
}