#include "Orchestrator.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "AI/Navigator.h"
#include "Conversion/CubeToSphere.h"
#include "Maze/Maze.h"
#include "Kismet/GameplayStatics.h"
#include "Components/CapsuleComponent.h"

#include "Engine/TextRenderActor.h"
#include "Components/TextRenderComponent.h"

AOrchestrator::AOrchestrator()
{
	PrimaryActorTick.bCanEverTick = true;

	USceneComponent *Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	WallHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("WallHISM"));
	WallHISM->SetupAttachment(Root);
	WallHISM->SetCollisionProfileName(TEXT("BlockAll"));
	WallHISM->SetMobility(EComponentMobility::Movable);
}

void AOrchestrator::OnConstruction(const FTransform &Transform)
{
	Super::OnConstruction(Transform);

	// Useful for editor iteration
	Rebuild();
}

void AOrchestrator::BeginPlay()
{
    Super::BeginPlay();

    // Make sure we have a SphereActor even when it's not a child
    if (!SphereActor)
    {
        SphereActor = Cast<ACubeToSphere>(
            UGameplayStatics::GetActorOfClass(GetWorld(), ACubeToSphere::StaticClass()));
    }

    // Make sure Maze exists and sphere surface is built for PIEF
    Rebuild();
}
static float GetPawnCapsuleHalfHeight(const APawn* P)
{
	if (!P) return 0.f;

	const UCapsuleComponent* Cap = P->FindComponentByClass<UCapsuleComponent>();
	if (!Cap) return 0.f;

	return Cap->GetScaledCapsuleHalfHeight();
}



void AOrchestrator::ResolveSphereFromChild()
{
	if (SphereActor)
		return;

	TArray<UChildActorComponent *> ChildComps;
	GetComponents<UChildActorComponent>(ChildComps);

	// Prefer the one named exactly "sphereCild"
	for (UChildActorComponent *CAC : ChildComps)
	{
		if (!CAC)
			continue;

		if (CAC->GetFName() == FName(TEXT("sphereChild")))
		{
			SphereActor = Cast<ACubeToSphere>(CAC->GetChildActor());
			if (SphereActor)
				return;
		}
	}

	// Fallback: if there's only one child actor and it's a CubeToSphere, use it
	for (UChildActorComponent *CAC : ChildComps)
	{
		if (!CAC)
			continue;

		if (ACubeToSphere *AsSphere = Cast<ACubeToSphere>(CAC->GetChildActor()))
		{
			SphereActor = AsSphere;
			return;
		}
	}
}
void AOrchestrator::RandomizeSeedNow()
{
    Seed = FMath::Rand();
    Rebuild();
}
void AOrchestrator::Rebuild()
{
	UE_LOG(LogTemp, Warning, TEXT("ORCH Rebuild  bShowCellLabels=%d  CellsPerFace=%d  Sphere=%s"),
		bShowCellLabels ? 1 : 0,
		CellsPerFace,
		*GetNameSafe(SphereActor)
	);
    CellsPerFace = FMath::Max(2, CellsPerFace);
    Resolution = CellsPerFace + 1;

    if (bRandomizeSeed)
    {
        Seed = FMath::Rand();
    }

    EnsureMazeGenerated();

	ResolveSphereFromChild();

	if (SphereActor)
	{
		SphereActor->SetRadius(SphereRadius);
		SphereActor->SetResolution(Resolution);
		SphereActor->BuildSurface();
	}

	BuildWallsFromMaze();


	// Optional heavy debug: spawn labels
	if (bShowCellLabels)
	{
		BuildCellLabels();
	}
	else
	{
		ClearCellLabels();
	}
}

void AOrchestrator::EnsureMazeGenerated()
{
	if (!Maze)
	{
		Maze = NewObject<UMaze>(this);
	}

	// Prefer sphere’s current resolution if it exists
	const int32 N = FMath::Max(2, CellsPerFace);

	Maze->CellsPerFace = N;
	Maze->Seed = Seed;
	Maze->Generate();
}

static FORCEINLINE int32 CountOpenSides(const FMazeCell &C)
{
	return (C.OpenN ? 1 : 0) + (C.OpenE ? 1 : 0) + (C.OpenS ? 1 : 0) + (C.OpenW ? 1 : 0);
}

bool AOrchestrator::IsCellSpawnable(int32 Face, int32 X, int32 Y, int32 MinOpenSides) const
{
	if (!Maze)
		return false;

	const FMazeCell &C = Maze->GetCell(Face, X, Y);
	return CountOpenSides(C) >= MinOpenSides;
}

bool AOrchestrator::FindRandomSpawnCell(int32 &OutFace, int32 &OutX, int32 &OutY,
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
	const FVector SpawnLoc = CenterWorld + UpDir * (CapsuleHalfHeight + 2.f);
	const FRotator SpawnRot = FRotationMatrix::MakeFromZ(UpDir).Rotator();

	OutTransform = FTransform(SpawnRot, SpawnLoc, FVector::OneVector);
	return true;
}

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

	// Helps with maze and sphere being on the same center when we move sphere in view
	auto AddWallFromEdgeLocal = [&](const FVector &A, const FVector &B)
	{
		const FVector Edge = (B - A);
		const float EdgeLen = Edge.Size();
		if (EdgeLen <= KINDA_SMALL_NUMBER)
			return;

		const FVector Mid = (A + B) * 0.5f;

		// Local sphere center is (0,0,0) when child is identity
		const FVector Up = Mid.GetSafeNormal();
		const FVector Fwd = Edge / EdgeLen;

		const FQuat Rot = FRotationMatrix::MakeFromXZ(Fwd, Up).ToQuat();
		const FVector Loc = Mid + Up * (WallHeight * 0.5f + WallSurfaceOffset);

		const FVector Scale(
			EdgeLen / WallMeshBaseLength,
			WallThickness / WallMeshBaseLength,
			WallHeight / WallMeshBaseLength);

		// IMPORTANT: Add in component/local space (will follow orchestrator moves)
		WallHISM->AddInstance(FTransform(Rot, Loc, Scale));
	};

	auto IsOpen = [&](const FMazeCell &C, EMazeDir Dir) -> bool
	{
		switch (Dir)
		{
		case EMazeDir::N:
			return C.OpenN;
		case EMazeDir::E:
			return C.OpenE;
		case EMazeDir::S:
			return C.OpenS;
		case EMazeDir::W:
			return C.OpenW;
		}
		return false;
	};

	// Dedupe: per cell build E + S, plus borders W (X==0) and N (Y==0)
	for (int32 Face = 0; Face < 6; ++Face)
	{
		for (int32 Y = 0; Y < N; ++Y)
		{
			for (int32 X = 0; X < N; ++X)
			{
				const FMazeCell &Cell = Maze->GetCell(Face, X, Y);

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
void AOrchestrator::RotateMazeToCell(
	int32 FromFace, int32 FromX, int32 FromY,
	int32 ToFace,   int32 ToX,   int32 ToY,
	float Duration
)
{
	if (!SphereActor) return;

	const FVector FromLocal = SphereActor->GetCellCenterLocal(FromFace, FromX, FromY).GetSafeNormal();
	const FVector ToLocal   = SphereActor->GetCellCenterLocal(ToFace,   ToX,   ToY).GetSafeNormal();

	if (FromLocal.IsNearlyZero() || ToLocal.IsNearlyZero()) return;

	const FQuat Delta = FQuat::FindBetweenNormals(ToLocal, FromLocal);

	RotateStart = GetActorQuat();
	RotateTarget = Delta * RotateStart;

	RotateElapsed = 0.f;
	RotateDuration = FMath::Max(0.001f, Duration);
	bRotatingMaze = true;
}

void AOrchestrator::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bRotatingMaze) return;

	RotateElapsed += DeltaSeconds;

	const float Alpha = FMath::Clamp(RotateElapsed / RotateDuration, 0.f, 1.f);
	const FQuat NewQ = FQuat::Slerp(RotateStart, RotateTarget, Alpha).GetNormalized();

	SetActorRotation(NewQ);

	if (Alpha >= 1.f)
	{
		bRotatingMaze = false;
	}
}

void AOrchestrator::ClearCellLabels()
{

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(
			-1,
			2.0f,
			FColor::Yellow,
			FString::Printf(TEXT("ClearCellLabels RUNNING  Count=%d"), CellLabelActors.Num())
		);
	}		
    static const FName LabelTag(TEXT("DBG_CellLabel"));

    // 1. Destroy what we tracked
    for (ATextRenderActor* A : CellLabelActors)
    {
        if (IsValid(A))
        {
            A->Destroy();
        }
    }
    CellLabelActors.Reset();

    // 2. Destroy any leftovers by tag
    UWorld* World = GetWorld();
    if (!World)
        return;

    TArray<AActor*> Tagged;
    UGameplayStatics::GetAllActorsWithTag(World, LabelTag, Tagged);

    for (AActor* A : Tagged)
    {
        if (IsValid(A))
        {
            A->Destroy();
        }
    }
}

void AOrchestrator::BuildCellLabels()
{
    UE_LOG(LogTemp, Warning, TEXT("BuildCellLabels called  Sphere=%s  CellsPerFace=%d  Step=%d"),
        *GetNameSafe(SphereActor), CellsPerFace, LabelStep);

    ClearCellLabels();

    // If SphereActor got stale or was never set, try to resolve it again
    if (!IsValid(SphereActor))
    {
        ResolveSphereFromChild();
    }

    if (!IsValid(SphereActor))
    {
        UE_LOG(LogTemp, Warning, TEXT("BuildCellLabels early out. SphereActor invalid"));
        return;
    }

    UWorld* World = GetWorld();
    if (!World)
        return;

    const int32 N = FMath::Max(2, CellsPerFace);
    const int32 Step = FMath::Max(1, LabelStep);

    // IMPORTANT: cell centers are local to the SphereActor
    const FTransform SphereXform = SphereActor->GetActorTransform();
    const FVector SphereCenterWorld = SphereXform.TransformPosition(FVector::ZeroVector);

    static const FName LabelTag(TEXT("DBG_CellLabel"));

    auto SpawnOne = [&](int32 Face, int32 X, int32 Y)
    {
        const FVector CenterLocal = SphereActor->GetCellCenterLocal(Face, X, Y);
        FVector CenterWorld = SphereXform.TransformPosition(CenterLocal);

        // push outward so it sits on the surface
        FVector Up = (CenterWorld - SphereCenterWorld).GetSafeNormal();
        if (Up.IsNearlyZero())
        {
            Up = FVector::UpVector;
        }
        CenterWorld += Up * LabelSurfaceOffset;

        // TextRender faces its +X axis, so align +X to the outward normal
        const FRotator TextRot = FRotationMatrix::MakeFromX(Up).Rotator();

        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Params.Owner = this;

        ATextRenderActor* TextActor = World->SpawnActor<ATextRenderActor>(
            ATextRenderActor::StaticClass(),
            CenterWorld,
            TextRot,
            Params
        );

        if (!IsValid(TextActor))
            return;

        TextActor->Tags.Add(LabelTag);

        if (UTextRenderComponent* TR = TextActor->GetTextRender())
        {
            const int32 Index = Face * (N * N) + (Y * N) + X;

            TR->SetText(FText::FromString(FString::Printf(TEXT("%d"), Index)));
            TR->SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
            TR->SetVerticalAlignment(EVerticalTextAligment::EVRTA_TextCenter);
            TR->SetWorldSize(LabelWorldSize);
            TR->SetCollisionEnabled(ECollisionEnabled::NoCollision);

            // Helpful so it still shows when small
            TR->SetMobility(EComponentMobility::Movable);
        }

        CellLabelActors.Add(TextActor);
		#if WITH_EDITORONLY_DATA
		TextActor->SetFolderPath(FName(TEXT("Debug/CellLabels")));
		#endif
    };

    if (bLabelSingleFace)
    {
        const int32 Face = FMath::Clamp(LabelFace, 0, 5);
        for (int32 Y = 0; Y < N; Y += Step)
        {
            for (int32 X = 0; X < N; X += Step)
            {
                SpawnOne(Face, X, Y);
            }
        }
        return;
    }

    for (int32 Face = 0; Face < 6; ++Face)
    {
        for (int32 Y = 0; Y < N; Y += Step)
        {
            for (int32 X = 0; X < N; X += Step)
            {
                SpawnOne(Face, X, Y);
            }
        }
    }
}

