#include "Orchestrator.h"
#include "Components/SceneComponent.h"
#include "AI/MazeNavigator.h"
#include "Conversion/CubeToSphere.h"
#include "Maze/Maze.h"
#include "DrawDebugHelpers.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Components/CapsuleComponent.h"
#include "Movement/GridMazePawn.h"
#include "Movement/MyCharacterBase.h"
/**
 * desc : Default constructor. Creates root + wall/path instanced mesh components and sets basic collision rules.
 * args : None
 * result: None
 */
AOrchestrator::AOrchestrator()
{
	PrimaryActorTick.bCanEverTick = true;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	WallHISM = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("WallHISM"));
	WallHISM->SetupAttachment(Root);
	WallHISM->SetCollisionProfileName(TEXT("BlockAll"));
	WallHISM->SetMobility(EComponentMobility::Movable);

	PathHISM = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("PathHISM"));
	PathHISM->SetupAttachment(Root);
	PathHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PathHISM->SetMobility(EComponentMobility::Movable);
}

/**
 * desc : Editor/runtime construction hook. Rebuilds the maze when placed/edited in the editor.
 * args : Transform - current actor transform during construction.
 * result: None
 */
void AOrchestrator::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Rebuild();
}


static float GetPawnCapsuleHalfHeight(const APawn* P)
{
	if (!P) return 0.f;

	const UCapsuleComponent* Cap = P->FindComponentByClass<UCapsuleComponent>();
	if (!Cap) return 0.f;

	return Cap->GetScaledCapsuleHalfHeight();
}


/**
 * desc : Searches attached ChildActorComponents and assigns SphereActor if a CubeToSphere child is found.
 * args : None
 * result: None
 */

void AOrchestrator::ResolveSphereFromChild()
{
	SphereActor = nullptr;

	TArray<UChildActorComponent*> ChildComps;
	GetComponents<UChildActorComponent>(ChildComps);

	for (UChildActorComponent* CAC : ChildComps)
	{
		if (CAC && CAC->GetChildActor() == nullptr)
		{
			CAC->CreateChildActor();
		}

		if (CAC && CAC->GetChildActor())
		{
			SphereActor = Cast<ACubeToSphere>(CAC->GetChildActor());
			if (SphereActor)
			{
				return;
			}
		}
	}
}
void AOrchestrator::RandomizeSeedNow()
{
    Seed = FMath::Rand();
    Rebuild();
}

/**
 * desc : Full pipeline rebuild:
 *        - resolves SphereActor from child actor component
 *        - locks Resolution = CellsPerFace + 1
 *        - builds sphere surface
 *        - generates maze data
 *        - builds wall instances
 *        - initializes Navigator
 *        - runs A* debug/path visualization
 * args : None
 * result: None
 */
void AOrchestrator::Rebuild()
{
	ResolveSphereFromChild();

	if (!SphereActor)
		return;

	Resolution = CellsPerFace + 1;

	SphereActor->SetRadius(SphereRadius);
	SphereActor->SetResolution(Resolution);
	SphereActor->BuildSurface();

	EnsureMazeGenerated();
	BuildWallsFromMaze();

	if (!Navigator)
	{
		Navigator = NewObject<UMazeNavigator>(this);
	}
	Navigator->Init(Maze, SphereActor);

	Astar();

	if (UWorld* World = GetWorld())
	{
		AMyCharacterBase* Character = SpawnedPawn;
		if (!Character)
		{
			Character = Cast<AMyCharacterBase>(
				UGameplayStatics::GetActorOfClass(World, AMyCharacterBase::StaticClass())
			);
		}

		if (Character)
		{
			Character->RefreshAfterMazeRebuild();
		}
		else if (AGridMazePawn* Pawn = Cast<AGridMazePawn>(
			UGameplayStatics::GetActorOfClass(World, AGridMazePawn::StaticClass())
		))
		{
			Pawn->RefreshAfterMazeRebuild();
		}
	}
}

/**
 * desc : Ensures Maze object exists and regenerates maze data using current CellsPerFace + Seed.
 * args : None
 * result: None
 */
void AOrchestrator::EnsureMazeGenerated()
{
	if (!Maze)
	{
		Maze = NewObject<UMaze>(this);
	}

	Maze->CellsPerFace = CellsPerFace;
	Maze->Seed = Seed;
	Maze->Generate();
}

static FORCEINLINE int32 CountOpenSides(const FMazeCell& C)
{
	return (C.OpenN ? 1 : 0) + (C.OpenE ? 1 : 0) + (C.OpenS ? 1 : 0) + (C.OpenW ? 1 : 0);
}

/**
 * desc : Checks if a given cell meets spawn requirements (at least MinOpenSides open directions).
 * args :
 *   - Face, X, Y: cell indices
 *   - MinOpenSides: minimum number of open sides required
 * result: True if spawnable; otherwise False.
 */
bool AOrchestrator::IsCellSpawnable(int32 Face, int32 X, int32 Y, int32 MinOpenSides) const
{
	if (!Maze)
		return false;

	const FMazeCell& C = Maze->GetCell(Face, X, Y);
	return CountOpenSides(C) >= MinOpenSides;
}

/**
 * desc : Randomly searches for a spawnable cell within MaxTries attempts.
 * args :
 *   - OutFace, OutX, OutY: returned cell indices if found
 *   - MinOpenSides: minimum open sides required
 *   - MaxTries: maximum random attempts
 * result: True if a cell was found; otherwise False.
 */
bool AOrchestrator::FindRandomSpawnCell(int32& OutFace, int32& OutX, int32& OutY,
	int32 MinOpenSides, int32 MaxTries) const
{
	if (!Maze)
		return false;

	const int32 N = Maze->CellsPerFace;
	if (N <= 0)
		return false;

	for (int32 Try = 0; Try < MaxTries; ++Try)
	{
		const int32 Face = FMath::RandRange(0, 5);
		const int32 X = FMath::RandRange(0, N - 1);
		const int32 Y = FMath::RandRange(0, N - 1);

		if (IsCellSpawnable(Face, X, Y, MinOpenSides))
		{
			OutFace = Face;
			OutX = X;
			OutY = Y;
			return true;
		}
	}

	return false;
}


void AOrchestrator::RotateMazeToCell(
    int32 FromFace, int32 FromX, int32 FromY,
    int32 ToFace,   int32 ToX,   int32 ToY,
    float Duration
)
{
    if (!SphereActor)
    {
        return;
    }

    const FVector FromLocal = SphereActor->GetCellCenterLocal(FromFace, FromX, FromY).GetSafeNormal();
    const FVector ToLocal   = SphereActor->GetCellCenterLocal(ToFace,   ToX,   ToY).GetSafeNormal();

    if (FromLocal.IsNearlyZero() || ToLocal.IsNearlyZero())
    {
        return;
    }

    const FQuat Delta = FQuat::FindBetweenNormals(ToLocal, FromLocal);

    RotateStart = GetActorQuat();
    RotateMidTarget = RotateStart;
    RotateTarget = Delta * RotateStart;
    RotatePrimaryAxisWorld = FVector::UpVector;
    RotatePrimaryAngleRadians = 0.f;

    RotateElapsed = 0.f;
    RotateDuration = FMath::Max(0.001f, Duration);
    bUseSettledRollRotation = false;
    bRotatingMaze = true;
}

void AOrchestrator::RotateMazeAgainstMoveDirection(
    int32 FromFace, int32 FromX, int32 FromY,
    int32 ToFace,   int32 ToX,   int32 ToY,
    const FVector& DesiredWorldMoveDirection,
    float Duration
)
{
    if (!SphereActor || bRotatingMaze)
    {
        return;
    }

    const FVector FromLocal = SphereActor->GetCellCenterLocal(FromFace, FromX, FromY).GetSafeNormal();
    const FVector ToLocal   = SphereActor->GetCellCenterLocal(ToFace,   ToX,   ToY).GetSafeNormal();

    if (FromLocal.IsNearlyZero() || ToLocal.IsNearlyZero())
    {
        return;
    }

    const float StepAngleRadians = FMath::Acos(FMath::Clamp(FVector::DotProduct(FromLocal, ToLocal), -1.f, 1.f));
    if (StepAngleRadians <= KINDA_SMALL_NUMBER)
    {
        return;
    }

    const FVector SphereCenterWorld = GetActorTransform().TransformPosition(FVector::ZeroVector);
    const FVector FromWorld = SphereActor->GetCellCenterWorld(FromFace, FromX, FromY);
    const FVector SurfaceNormal = (FromWorld - SphereCenterWorld).GetSafeNormal();
    const FVector DesiredTangent = FVector::VectorPlaneProject(DesiredWorldMoveDirection, SurfaceNormal).GetSafeNormal();
    const FVector RotationAxisWorld = FVector::CrossProduct(DesiredTangent, SurfaceNormal).GetSafeNormal();

    if (SurfaceNormal.IsNearlyZero() || DesiredTangent.IsNearlyZero() || RotationAxisWorld.IsNearlyZero())
    {
        RotateMazeToCell(FromFace, FromX, FromY, ToFace, ToX, ToY, Duration);
        return;
    }

    const FVector ToWorld = SphereActor->GetCellCenterWorld(ToFace, ToX, ToY);
    const FVector ToWorldDirection = (ToWorld - SphereCenterWorld).GetSafeNormal();
    if (ToWorldDirection.IsNearlyZero())
    {
        RotateMazeToCell(FromFace, FromX, FromY, ToFace, ToX, ToY, Duration);
        return;
    }

    const FQuat PrimaryDelta(RotationAxisWorld, StepAngleRadians);
    const FVector PrimaryTargetDirection = PrimaryDelta.RotateVector(ToWorldDirection).GetSafeNormal();
    if (PrimaryTargetDirection.IsNearlyZero())
    {
        RotateMazeToCell(FromFace, FromX, FromY, ToFace, ToX, ToY, Duration);
        return;
    }

    const float CorrectionAngleRadians = FMath::Acos(FMath::Clamp(FVector::DotProduct(PrimaryTargetDirection, SurfaceNormal), -1.f, 1.f));
    const FVector CorrectionAxisWorld = FVector::CrossProduct(PrimaryTargetDirection, SurfaceNormal).GetSafeNormal();

    RotateStart = GetActorQuat();
    RotateMidTarget = (PrimaryDelta * RotateStart).GetNormalized();
    RotateTarget = RotateMidTarget;
    RotatePrimaryAxisWorld = RotationAxisWorld;
    RotatePrimaryAngleRadians = StepAngleRadians;
    RotateElapsed = 0.f;
    RotateDuration = FMath::Max(0.001f, Duration);
    bUseSettledRollRotation = true;

    if (!CorrectionAxisWorld.IsNearlyZero() && CorrectionAngleRadians > KINDA_SMALL_NUMBER)
    {
        RotateTarget = (FQuat(CorrectionAxisWorld, CorrectionAngleRadians) * RotateMidTarget).GetNormalized();
    }

    bRotatingMaze = true;
}

void AOrchestrator::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    if (!bRotatingMaze) return;

    RotateElapsed += DeltaSeconds;

    const float Alpha = FMath::Clamp(RotateElapsed / RotateDuration, 0.f, 1.f);
    FQuat NewQ = RotateTarget;

    if (bUseSettledRollRotation)
    {
        const float SafePrimaryPhase = FMath::Clamp(RotatePrimaryPhasePortion, 0.05f, 0.95f);
        if (Alpha < SafePrimaryPhase)
        {
            const float PhaseAlpha = FMath::Clamp(Alpha / SafePrimaryPhase, 0.f, 1.f);
            const float SmoothPhaseAlpha = FMath::InterpEaseInOut(0.f, 1.f, PhaseAlpha, 2.0f);
            NewQ = (FQuat(RotatePrimaryAxisWorld, RotatePrimaryAngleRadians * SmoothPhaseAlpha) * RotateStart).GetNormalized();
        }
        else
        {
            const float PhaseAlpha = FMath::Clamp(
                (Alpha - SafePrimaryPhase) / FMath::Max(KINDA_SMALL_NUMBER, 1.f - SafePrimaryPhase),
                0.f,
                1.f);
            const float SmoothPhaseAlpha = FMath::InterpEaseInOut(0.f, 1.f, PhaseAlpha, 2.0f);
            NewQ = FQuat::Slerp(RotateMidTarget, RotateTarget, SmoothPhaseAlpha).GetNormalized();
        }
    }
    else
    {
        NewQ = FQuat::Slerp(RotateStart, RotateTarget, Alpha).GetNormalized();
    }

    SetActorRotation(NewQ);

    if (Alpha >= 1.f)
    {
        SetActorRotation(RotateTarget);
        bUseSettledRollRotation = false;
        bRotatingMaze = false;
    }
}

bool AOrchestrator::GetRandomSpawnTransform(FTransform &OutTransform,
											float CapsuleHalfHeight,
											int32 MinOpenSides,
											int32 MaxTries) const
{
	if (!SphereActor || !Maze)
		return false;

	int32 Face, X, Y;
	if (!FindRandomSpawnCell(Face, X, Y, MinOpenSides, MaxTries))
	{
		OutTransform = FTransform::Identity;
		return false;
	}

	const FVector CenterWorld = SphereActor->GetCellCenterWorld(Face, X, Y);
	const FVector SphereCenter = SphereActor->GetActorLocation();

	const FVector UpDir = (CenterWorld - SphereCenter).GetSafeNormal();

	FVector EdgeA, EdgeB;
	SphereActor->GetCellWallEdgeWorld(Face, X, Y, EMazeDir::W, EdgeA, EdgeB);

	const FVector ForwardDir = (EdgeA - EdgeB).GetSafeNormal();
	const FRotator SpawnRot = FRotationMatrix::MakeFromXZ(ForwardDir, UpDir).Rotator();

	const FVector SpawnLoc = CenterWorld + UpDir * (CapsuleHalfHeight + 2.f);

	OutTransform = FTransform(SpawnRot, SpawnLoc, FVector::OneVector);
	return true;
}

/**
 * desc : Converts Maze walls into instanced mesh wall segments on the sphere surface.
 * args : None
 * result: None
 */
void AOrchestrator::BuildWallsFromMaze()
{
	if (!WallHISM || !SphereActor || !Maze)
		return;
	if (!WallMesh)
		return;

	WallHISM->SetStaticMesh(WallMesh);
	if (!WallHISM->GetStaticMesh())
		return;

	WallHISM->ClearInstances();

	const int32 N = Maze->CellsPerFace;
	if (N <= 0)
		return;

	auto AddWallFromEdgeLocal = [&](const FVector& A, const FVector& B)
	{
		const FVector Edge = (B - A);
		const float EdgeLen = Edge.Size();
		if (EdgeLen <= KINDA_SMALL_NUMBER)
			return;

		const FVector Mid = (A + B) * 0.5f;
		const FVector Up = Mid.GetSafeNormal();
		const FVector Fwd = Edge / EdgeLen;

		const FQuat Rot = FRotationMatrix::MakeFromXZ(Fwd, Up).ToQuat();
		const FVector Loc = Mid + Up * (WallHeight * 0.5f + WallSurfaceOffset);

		const FVector Scale(
			EdgeLen / WallMeshBaseLength,
			WallThickness / WallMeshBaseLength,
			WallHeight / WallMeshBaseLength);

		WallHISM->AddInstance(FTransform(Rot, Loc, Scale));
	};

	auto IsOpen = [&](const FMazeCell& C, EMazeDir Dir) -> bool
	{
		switch (Dir)
		{
		case EMazeDir::N: return C.OpenN;
		case EMazeDir::E: return C.OpenE;
		case EMazeDir::S: return C.OpenS;
		case EMazeDir::W: return C.OpenW;
		}
		return false;
	};

	for (int32 Face = 0; Face < 6; ++Face)
	{
		for (int32 Y = 0; Y < N; ++Y)
		{
			for (int32 X = 0; X < N; ++X)
			{
				const FMazeCell& Cell = Maze->GetCell(Face, X, Y);
				FVector A, B;

				if (!IsOpen(Cell, EMazeDir::E) && SphereActor->GetCellWallEdgeLocal(Face, X, Y, EMazeDir::E, A, B))
					AddWallFromEdgeLocal(A, B);

				if (!IsOpen(Cell, EMazeDir::S) && SphereActor->GetCellWallEdgeLocal(Face, X, Y, EMazeDir::S, A, B))
					AddWallFromEdgeLocal(A, B);

				if (X == 0 && !IsOpen(Cell, EMazeDir::W) && SphereActor->GetCellWallEdgeLocal(Face, X, Y, EMazeDir::W, A, B))
					AddWallFromEdgeLocal(A, B);

				if (Y == 0 && !IsOpen(Cell, EMazeDir::N) && SphereActor->GetCellWallEdgeLocal(Face, X, Y, EMazeDir::N, A, B))
					AddWallFromEdgeLocal(A, B);
			}
		}
	}
}

/**
 * desc : Debug/test pathfinding routine. Picks start/end points, runs Navigator->FindPath(),
 *        then draws results via instanced meshes or debug spheres.
 * args : None
 * result: None
 */
void AOrchestrator::Astar()
{
	if (!SphereActor)
	{
		UE_LOG(LogTemp, Error, TEXT("A* TEST FAILED: SphereActor is null!"));
		return;
	}

	FlushPersistentDebugLines(GetWorld());

	int32 MaxCell = SphereActor->GetCellsPerFace() - 1;

	FVector StartPos = SphereActor->GetCellCenterWorld(1, MaxCell / 2, MaxCell / 2);
	FVector EndPos   = SphereActor->GetCellCenterWorld(0, MaxCell / 2, MaxCell / 2);

	DrawDebugSphere(GetWorld(), StartPos, 30.0f, 12, FColor::Blue, true, 20.0f);
	DrawDebugSphere(GetWorld(), EndPos,   30.0f, 12, FColor::Red,  true, 20.0f);

	TArray<FVector> PathResult;

	if (Navigator != nullptr)
	{
		bool bFoundPath = Navigator->FindPath(StartPos, EndPos, PathResult);

		if (bFoundPath)
		{
			if (PathHISM && PathMesh)
			{
				PathHISM->SetStaticMesh(PathMesh);

				if (PathMaterial)
				{
					PathHISM->SetMaterial(0, PathMaterial);
				}
			}

			if (PathHISM && PathHISM->GetStaticMesh())
			{
				PathHISM->ClearInstances();

				for (const FVector& Point : PathResult)
				{
					FVector LocalPos = GetActorTransform().InverseTransformPosition(Point);
					FVector UpDir = LocalPos.GetSafeNormal();

					LocalPos += UpDir * 10.0f;

					FTransform InstanceTransform(FRotator::ZeroRotator, LocalPos, FVector(0.1f));
					PathHISM->AddInstance(InstanceTransform);
				}
			}
			else
			{
				for (const FVector& Point : PathResult)
				{
					DrawDebugSphere(GetWorld(), Point, 15.0f, 12, FColor::Green, true, 20.0f);
				}
			}
		}
	}
}
