#include "Orchestrator.h"
#include "Components/SceneComponent.h"
#include "AI/MazeNavigator.h"
#include "AI/MazeRunner.h"
#include "Conversion/CubeToSphere.h"
#include "Maze/Maze.h"
#include "DrawDebugHelpers.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Artifact/MazeArtifactManager.h"
#include "Kismet/GameplayStatics.h"


namespace
{
	static FVector SlerpDirOnSphere(const FVector &A, const FVector &B, float Alpha)
	{
		const FVector SA = A.GetSafeNormal();
		const FVector SB = B.GetSafeNormal();

		if (SA.IsNearlyZero() || SB.IsNearlyZero())
		{
			return FVector::ZeroVector;
		}

		const FQuat Arc = FQuat::FindBetweenNormals(SA, SB);
		return FQuat::Slerp(FQuat::Identity, Arc, Alpha).RotateVector(SA).GetSafeNormal();
	}

	static void AppendQuad(
		int32 I0, int32 I1, int32 I2, int32 I3,
		TArray<int32> &Triangles,
		bool bFlip = false)
	{
		if (!bFlip)
		{
			Triangles.Add(I0); Triangles.Add(I2); Triangles.Add(I1);
			Triangles.Add(I1); Triangles.Add(I2); Triangles.Add(I3);
		}
		else
		{
			Triangles.Add(I0); Triangles.Add(I1); Triangles.Add(I2);
			Triangles.Add(I1); Triangles.Add(I3); Triangles.Add(I2);
		}
	}
}

/**
 * desc : Default constructor. Creates root + wall/path instanced mesh components and sets basic collision rules.
 * args : None
 * result: None
 */
AOrchestrator::AOrchestrator()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent *Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	WallHISM = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("WallHISM"));
	WallHISM->SetupAttachment(Root);
	WallHISM->SetCollisionProfileName(TEXT("BlockAll"));
	WallHISM->SetMobility(EComponentMobility::Movable);

	WallHISM->SetCanEverAffectNavigation(false);

	CornerHISM = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("CornerHISM"));
	CornerHISM->SetupAttachment(Root);
	CornerHISM->SetCollisionProfileName(TEXT("BlockAll"));
	CornerHISM->SetMobility(EComponentMobility::Movable);
	CornerHISM->SetCanEverAffectNavigation(false);

	WallProcMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("WallProcMesh"));
	WallProcMesh->SetupAttachment(Root);
	WallProcMesh->SetCollisionProfileName(TEXT("BlockAll"));
	WallProcMesh->SetMobility(EComponentMobility::Movable);
	WallProcMesh->SetCanEverAffectNavigation(false);

	if (CornerHISM)
	{
		CornerHISM->ClearInstances();
		CornerHISM->SetVisibility(true);

		if (CornerMesh)
		{
			CornerHISM->SetStaticMesh(CornerMesh);
		}

		if (CornerMaterial)
		{
			CornerHISM->SetMaterial(0, CornerMaterial);
		}
		else if (WallMaterial)
		{
			CornerHISM->SetMaterial(0, WallMaterial);
		}
	}

	PathHISM = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("PathHISM"));
	PathHISM->SetupAttachment(Root);
	PathHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PathHISM->SetMobility(EComponentMobility::Movable);

	PathHISM->SetCanEverAffectNavigation(false);
}

/**
 * desc : Editor/runtime construction hook. Rebuilds the maze when placed/edited in the editor.
 * args : Transform - current actor transform during construction.
 * result: None
 */
void AOrchestrator::OnConstruction(const FTransform &Transform)
{
	Super::OnConstruction(Transform);
	Rebuild();
}

/**
 * desc : Searches attached ChildActorComponents and assigns SphereActor if a CubeToSphere child is found.
 * args : None
 * result: None
 */
void AOrchestrator::ResolveSphereFromChild()
{
	SphereActor = nullptr;

	TArray<UChildActorComponent *> ChildComps;
	GetComponents<UChildActorComponent>(ChildComps);

	for (UChildActorComponent *CAC : ChildComps)
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

/**
 * desc : Full pipeline rebuild:
 * - resolves SphereActor from child actor component
 * - locks Resolution = CellsPerFace + 1
 * - builds sphere surface
 * - generates maze data
 * - builds wall instances
 * - initializes Navigator
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

static FORCEINLINE int32 CountOpenSides(const FMazeCell &C)
{
	return (C.OpenN ? 1 : 0) + (C.OpenE ? 1 : 0) + (C.OpenS ? 1 : 0) + (C.OpenW ? 1 : 0);
}

/**
 * desc : Checks if a given cell meets spawn requirements (at least MinOpenSides open directions).
 * args :
 * - Face, X, Y: cell indices
 * - MinOpenSides: minimum number of open sides required
 * result: True if spawnable; otherwise False.
 */
bool AOrchestrator::IsCellSpawnable(int32 Face, int32 X, int32 Y, int32 MinOpenSides) const
{
	if (!Maze)
		return false;

	const FMazeCell &C = Maze->GetCell(Face, X, Y);
	return CountOpenSides(C) >= MinOpenSides;
}

/**
 * desc : Randomly searches for a spawnable cell within MaxTries attempts.
 * args :
 * - OutFace, OutX, OutY: returned cell indices if found
 * - MinOpenSides: minimum open sides required
 * - MaxTries: maximum random attempts
 * - PointSeed: Additional offset to ensure varied locations.
 * result: True if a cell was found; otherwise False.
 */
bool AOrchestrator::FindRandomSpawnCell(int32 &OutFace, int32 &OutX, int32 &OutY, int32 MinOpenSides, int32 MaxTries, int32 PointSeed) const
{
	if (!Maze)
		return false;

	const int32 N = Maze->CellsPerFace;
	if (N <= 0)
		return false;

	// LOCK THE RANDOMNESS TO YOUR MAZE SEED
	FRandomStream Stream(Seed + PointSeed);

	for (int32 Try = 0; Try < MaxTries; ++Try)
	{
		const int32 Face = Stream.RandRange(0, 5);
		const int32 X = Stream.RandRange(0, N - 1);
		const int32 Y = Stream.RandRange(0, N - 1);

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

/**
 * desc : Finds a random maze cell that is "open enough" and returns a spawn transform
 * aligned to the sphere surface + corridor direction.
 * args :
 * - OutTransform: returned spawn transform (rotation aligns to surface + hallway).
 * - CapsuleHalfHeight: character capsule half height used to offset spawn above surface.
 * - MinOpenSides: minimum number of open sides required for a cell to be spawnable.
 * - MaxTries: maximum random attempts before failing.
 * - PointSeed: Offset seed to ensure multiple calls generate different locations.
 * result: True if a valid spawn cell was found; otherwise False (OutTransform becomes Identity).
 */
bool AOrchestrator::GetRandomSpawnTransform(FTransform &OutTransform, float CapsuleHalfHeight, int32 MinOpenSides, int32 MaxTries, int32 PointSeed) const
{
	if (!SphereActor || !Maze)
		return false;

	int32 Face, X, Y;
	// Pass the PointSeed down into the math
	if (!FindRandomSpawnCell(Face, X, Y, MinOpenSides, MaxTries, PointSeed))
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

bool AOrchestrator::GetWallSegmentCentersWorld(
	int32 Face,
	int32 X,
	int32 Y,
	EMazeDir Dir,
	FVector& OutBaseCenter,
	FVector& OutTopCenter) const
{
	OutBaseCenter = FVector::ZeroVector;
	OutTopCenter  = FVector::ZeroVector;

	if (!SphereActor)
	{
		return false;
	}

	FVector EdgeA, EdgeB;
	if (!SphereActor->GetCellWallEdgeWorld(Face, X, Y, Dir, EdgeA, EdgeB))
	{
		return false;
	}

	const FVector BaseCenter = (EdgeA + EdgeB) * 0.5f;
	const FVector SphereCenter = SphereActor->GetActorLocation();
	const FVector UpDir = (BaseCenter - SphereCenter).GetSafeNormal();

	if (UpDir.IsNearlyZero())
	{
		return false;
	}

	OutBaseCenter = BaseCenter + UpDir * WallSurfaceOffset;
	OutTopCenter  = OutBaseCenter + UpDir * WallHeight;
	return true;
}

bool AOrchestrator::GetWallSegmentFrameWorld(
	int32 Face,
	int32 X,
	int32 Y,
	EMazeDir Dir,
	FVector& OutBaseCenter,
	FVector& OutTopCenter,
	FVector& OutUpDir,
	FVector& OutRightDir,
	FVector& OutForwardDir) const
{
	OutBaseCenter = FVector::ZeroVector;
	OutTopCenter  = FVector::ZeroVector;
	OutUpDir      = FVector::ZeroVector;
	OutRightDir   = FVector::ZeroVector;
	OutForwardDir = FVector::ZeroVector;

	if (!SphereActor)
	{
		return false;
	}

	FVector EdgeA, EdgeB;
	if (!SphereActor->GetCellWallEdgeWorld(Face, X, Y, Dir, EdgeA, EdgeB))
	{
		return false;
	}

	const FVector BaseCenter = (EdgeA + EdgeB) * 0.5f;
	const FVector SphereCenter = SphereActor->GetActorLocation();

	const FVector UpDir = (BaseCenter - SphereCenter).GetSafeNormal();
	if (UpDir.IsNearlyZero())
	{
		return false;
	}

	FVector ForwardDir = (EdgeB - EdgeA).GetSafeNormal();
	if (ForwardDir.IsNearlyZero())
	{
		return false;
	}

	ForwardDir = FVector::VectorPlaneProject(ForwardDir, UpDir).GetSafeNormal();
	if (ForwardDir.IsNearlyZero())
	{
		return false;
	}

	const FVector RightDir = FVector::CrossProduct(UpDir, ForwardDir).GetSafeNormal();

	OutBaseCenter = BaseCenter + UpDir * WallSurfaceOffset;
	OutTopCenter  = OutBaseCenter + UpDir * WallHeight;
	OutUpDir      = UpDir;
	OutRightDir   = RightDir;
	OutForwardDir = ForwardDir;
	return true;
}



bool AOrchestrator::GetWallSegmentTransformWorld(
	int32 Face,
	int32 X,
	int32 Y,
	EMazeDir Dir,
	FTransform& OutTransform) const
{
	OutTransform = FTransform::Identity;

	if (!SphereActor || !WallMesh)
	{
		return false;
	}

	FVector EdgeA, EdgeB;
	if (!SphereActor->GetCellWallEdgeWorld(Face, X, Y, Dir, EdgeA, EdgeB))
	{
		return false;
	}

	const FVector Edge = EdgeB - EdgeA;
	const float EdgeLen = Edge.Size();

	if (EdgeLen <= 0.1f || WallMeshBaseLength <= 0.1f)
	{
		return false;
	}

	const FVector Mid = (EdgeA + EdgeB) * 0.5f;
	const FVector SphereCenter = SphereActor->GetActorLocation();
	const FVector UpDir = (Mid - SphereCenter).GetSafeNormal();

	if (UpDir.IsNearlyZero())
	{
		return false;
	}

	FVector ForwardDir = Edge / EdgeLen;
	ForwardDir = FVector::VectorPlaneProject(ForwardDir, UpDir).GetSafeNormal();

	if (ForwardDir.IsNearlyZero())
	{
		return false;
	}

	const FQuat Rot = FRotationMatrix::MakeFromXZ(ForwardDir, UpDir).ToQuat();
	const FVector Loc = Mid + UpDir * (WallHeight * 0.5f + WallSurfaceOffset);

	const FVector Scale(
		EdgeLen / WallMeshBaseLength,
		WallThickness / WallMeshBaseLength,
		WallHeight / WallMeshBaseLength
	);

	OutTransform = FTransform(Rot, Loc, Scale);
	return true;
}

/**
 * desc : Converts Maze logical walls into physical instanced mesh wall segments on the sphere surface.
 * args : None
 * result: None
 */
void AOrchestrator::AppendCurvedWallEdge(
	const FVector &LocalA,
	const FVector &LocalB,
	TArray<FVector> &Vertices,
	TArray<int32> &Triangles,
	TArray<FVector> &Normals,
	TArray<FVector2D> &UVs,
	TArray<FProcMeshTangent> &Tangents) const
{
	if (!SphereActor)
	{
		return;
	}

	const int32 NumSlices = FMath::Max(1, WallArcSubdivisions);
	const float Radius = SphereActor->GetRadius();
	const float HalfThickness = WallThickness * 0.5f;

	for (int32 Slice = 0; Slice < NumSlices; ++Slice)
	{
		const float T0 = (float)Slice / (float)NumSlices;
		const float T1 = (float)(Slice + 1) / (float)NumSlices;

		const FVector Dir0 = SlerpDirOnSphere(LocalA, LocalB, T0);
		const FVector Dir1 = SlerpDirOnSphere(LocalA, LocalB, T1);
		if (Dir0.IsNearlyZero() || Dir1.IsNearlyZero())
		{
			continue;
		}

		const FVector P0 = Dir0 * Radius;
		const FVector P1 = Dir1 * Radius;

		FVector Forward = (P1 - P0).GetSafeNormal();
		if (Forward.IsNearlyZero())
		{
			continue;
		}

		const FVector Up0 = Dir0;
		const FVector Up1 = Dir1;
		FVector Right0 = FVector::CrossProduct(Up0, Forward).GetSafeNormal();
		FVector Right1 = FVector::CrossProduct(Up1, Forward).GetSafeNormal();

		if (Right0.IsNearlyZero() || Right1.IsNearlyZero())
		{
			continue;
		}

		const FVector Base0 = P0 + Up0 * WallSurfaceOffset;
		const FVector Base1 = P1 + Up1 * WallSurfaceOffset;

		const FVector B0L = Base0 - Right0 * HalfThickness;
		const FVector B0R = Base0 + Right0 * HalfThickness;
		const FVector T0L = B0L + Up0 * WallHeight;
		const FVector T0R = B0R + Up0 * WallHeight;

		const FVector B1L = Base1 - Right1 * HalfThickness;
		const FVector B1R = Base1 + Right1 * HalfThickness;
		const FVector T1L = B1L + Up1 * WallHeight;
		const FVector T1R = B1R + Up1 * WallHeight;

		const int32 BaseIndex = Vertices.Num();
		Vertices.Append({ B0L, B0R, T0L, T0R, B1L, B1R, T1L, T1R });

		const float U0 = T0;
		const float U1 = T1;

		Normals.Add(B0L.GetSafeNormal()); UVs.Add(FVector2D(U0, 0.0f)); Tangents.Add(FProcMeshTangent(Forward, false));
		Normals.Add(B0R.GetSafeNormal()); UVs.Add(FVector2D(U0, 0.0f)); Tangents.Add(FProcMeshTangent(Forward, false));
		Normals.Add(T0L.GetSafeNormal()); UVs.Add(FVector2D(U0, 1.0f)); Tangents.Add(FProcMeshTangent(Forward, false));
		Normals.Add(T0R.GetSafeNormal()); UVs.Add(FVector2D(U0, 1.0f)); Tangents.Add(FProcMeshTangent(Forward, false));

		Normals.Add(B1L.GetSafeNormal()); UVs.Add(FVector2D(U1, 0.0f)); Tangents.Add(FProcMeshTangent(Forward, false));
		Normals.Add(B1R.GetSafeNormal()); UVs.Add(FVector2D(U1, 0.0f)); Tangents.Add(FProcMeshTangent(Forward, false));
		Normals.Add(T1L.GetSafeNormal()); UVs.Add(FVector2D(U1, 1.0f)); Tangents.Add(FProcMeshTangent(Forward, false));
		Normals.Add(T1R.GetSafeNormal()); UVs.Add(FVector2D(U1, 1.0f)); Tangents.Add(FProcMeshTangent(Forward, false));

		// outside face
		AppendQuad(BaseIndex + 1, BaseIndex + 5, BaseIndex + 3, BaseIndex + 7, Triangles, false);
		// inside face
		AppendQuad(BaseIndex + 4, BaseIndex + 0, BaseIndex + 6, BaseIndex + 2, Triangles, false);
		// top face
		AppendQuad(BaseIndex + 2, BaseIndex + 3, BaseIndex + 6, BaseIndex + 7, Triangles, false);
		// start cap
		if (Slice == 0)
		{
			AppendQuad(BaseIndex + 0, BaseIndex + 1, BaseIndex + 2, BaseIndex + 3, Triangles, false);
		}
		// end cap
		if (Slice == NumSlices - 1)
		{
			AppendQuad(BaseIndex + 5, BaseIndex + 4, BaseIndex + 7, BaseIndex + 6, Triangles, false);
		}
	}
}

namespace
{
	struct FCornerAccum
	{
		FVector Pos = FVector::ZeroVector;
		TArray<FVector> Dirs;
	};

	FString MakeCornerKey(const FVector& P)
	{
		const FVector Q = P.GridSnap(5.0f);
		return FString::Printf(TEXT("%.1f_%.1f_%.1f"), Q.X, Q.Y, Q.Z);
	}

	bool HasNonCollinearPair(const TArray<FVector>& Dirs)
	{
		for (int32 i = 0; i < Dirs.Num(); ++i)
		{
			for (int32 j = i + 1; j < Dirs.Num(); ++j)
			{
				const float Dot = FMath::Abs(FVector::DotProduct(
					Dirs[i].GetSafeNormal(),
					Dirs[j].GetSafeNormal()));

				if (Dot < 0.95f)
				{
					return true;
				}
			}
		}
		return false;
	}
}
/**
 * desc : Converts Maze logical walls into physical wall geometry on the sphere surface.
 * args : None
 * result: None
 */
void AOrchestrator::BuildWallsFromMaze()
{

	if (CornerHISM)
	{
		CornerHISM->ClearInstances();
		CornerHISM->SetVisibility(true);

		if (CornerMesh)
		{
			CornerHISM->SetStaticMesh(CornerMesh);
		}

		if (CornerMaterial)
		{
			CornerHISM->SetMaterial(0, CornerMaterial);
		}
		else if (WallMaterial)
		{
			CornerHISM->SetMaterial(0, WallMaterial);
		}
	}

	if (!SphereActor || !Maze)
		return;

	if (WallHISM)
	{
		WallHISM->ClearInstances();
		WallHISM->SetVisibility(!bUseProceduralWalls);
	}

	if (WallProcMesh)
	{
		WallProcMesh->ClearAllMeshSections();
		WallProcMesh->SetVisibility(bUseProceduralWalls);
	}

	if (!bUseProceduralWalls)
	{
		if (!WallHISM || !WallMesh)
			return;

		WallHISM->SetStaticMesh(WallMesh);
		if (!WallHISM->GetStaticMesh())
			return;
		
		if (WallMaterial)
		{
			WallHISM->SetMaterial(0, WallMaterial);
		}
		
		const int32 N = Maze->CellsPerFace;
		if (N <= 0)
			return;

		auto AddWallFromEdgeLocal = [&](const FVector &A, const FVector &B)
		{
			if (A.ContainsNaN() || B.ContainsNaN())
				return;

			const FVector Edge = (B - A);
			const float EdgeLen = Edge.Size();
			if (EdgeLen <= 0.1f || !FMath::IsFinite(EdgeLen) || WallMeshBaseLength <= 0.1f)
				return;

			const FVector Mid = (A + B) * 0.5f;
			const FVector Up = Mid.GetSafeNormal();
			if (Up.IsNearlyZero() || Up.ContainsNaN())
				return;

			const FVector Fwd = Edge / EdgeLen;
			if (Fwd.ContainsNaN() || FMath::Abs(FVector::DotProduct(Fwd, Up)) > 0.99f)
				return;

			FQuat Rot = FRotationMatrix::MakeFromXZ(Fwd, Up).ToQuat();
			if (Rot.ContainsNaN() || !Rot.IsNormalized())
				return;

			const FVector Loc = Mid + Up * (WallHeight * 0.5f + WallSurfaceOffset);
			const FVector Scale(
				FMath::Clamp(EdgeLen / WallMeshBaseLength, 0.01f, 1000.0f),
				FMath::Clamp(WallThickness / WallMeshBaseLength, 0.01f, 1000.0f),
				FMath::Clamp(WallHeight / WallMeshBaseLength, 0.01f, 1000.0f));

			FTransform InstanceTransform;
			InstanceTransform.SetComponents(Rot, Loc, Scale);
			if (InstanceTransform.IsValid() && WallHISM)
			{
				WallHISM->AddInstance(InstanceTransform);
			}
		};

		auto IsOpen = [&](const FMazeCell &C, EMazeDir Dir) -> bool
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
		for (int32 Y = 0; Y < N; ++Y)
		for (int32 X = 0; X < N; ++X)
		{
			const FMazeCell &Cell = Maze->GetCell(Face, X, Y);
			FVector A, B;
			if (!IsOpen(Cell, EMazeDir::E) && SphereActor->GetCellWallEdgeLocal(Face, X, Y, EMazeDir::E, A, B)) AddWallFromEdgeLocal(A, B);
			if (!IsOpen(Cell, EMazeDir::S) && SphereActor->GetCellWallEdgeLocal(Face, X, Y, EMazeDir::S, A, B)) AddWallFromEdgeLocal(A, B);
			if (X == 0 && !IsOpen(Cell, EMazeDir::W) && SphereActor->GetCellWallEdgeLocal(Face, X, Y, EMazeDir::W, A, B)) AddWallFromEdgeLocal(A, B);
			if (Y == 0 && !IsOpen(Cell, EMazeDir::N) && SphereActor->GetCellWallEdgeLocal(Face, X, Y, EMazeDir::N, A, B)) AddWallFromEdgeLocal(A, B);
		}

		return;
	}

	if (!WallProcMesh)
	{
		return;
	}

	const int32 N = Maze->CellsPerFace;
	if (N <= 0)
	{
		return;
	}

	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FProcMeshTangent> Tangents;
	TArray<FColor> Colors;

	TMap<FString, FCornerAccum> CornerMap;

	auto AddCornerSample = [&](const FVector& P, const FVector& AlongEdge)
	{
		const FString Key = MakeCornerKey(P);
		FCornerAccum* Found = CornerMap.Find(Key);

		if (!Found)
		{
			FCornerAccum NewCorner;
			NewCorner.Pos = P;
			NewCorner.Dirs.Add(AlongEdge.GetSafeNormal());
			CornerMap.Add(Key, NewCorner);
		}
		else
		{
			Found->Dirs.Add(AlongEdge.GetSafeNormal());
		}

	};

	Vertices.Reserve(N * N * 6 * 8);
	Triangles.Reserve(N * N * 6 * 18);

	auto IsOpen = [&](const FMazeCell &C, EMazeDir Dir) -> bool
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
	for (int32 Y = 0; Y < N; ++Y)
	for (int32 X = 0; X < N; ++X)
	{
		const FMazeCell &Cell = Maze->GetCell(Face, X, Y);
		FVector A, B;

		if (!IsOpen(Cell, EMazeDir::E) && SphereActor->GetCellWallEdgeLocal(Face, X, Y, EMazeDir::E, A, B))
		{
			AppendCurvedWallEdge(A, B, Vertices, Triangles, Normals, UVs, Tangents);
			const FVector EdgeDir = (B - A).GetSafeNormal();
			AddCornerSample(A, EdgeDir);
			AddCornerSample(B, EdgeDir);
		}

		if (!IsOpen(Cell, EMazeDir::S) && SphereActor->GetCellWallEdgeLocal(Face, X, Y, EMazeDir::S, A, B))
		{
			AppendCurvedWallEdge(A, B, Vertices, Triangles, Normals, UVs, Tangents);

			const FVector EdgeDir = (B - A).GetSafeNormal();
			AddCornerSample(A, EdgeDir);
			AddCornerSample(B, EdgeDir);
		}

		if (X == 0 && !IsOpen(Cell, EMazeDir::W) && SphereActor->GetCellWallEdgeLocal(Face, X, Y, EMazeDir::W, A, B))
		{
			AppendCurvedWallEdge(A, B, Vertices, Triangles, Normals, UVs, Tangents);

			const FVector EdgeDir = (B - A).GetSafeNormal();
			AddCornerSample(A, EdgeDir);
			AddCornerSample(B, EdgeDir);
		}

		if (Y == 0 && !IsOpen(Cell, EMazeDir::N) && SphereActor->GetCellWallEdgeLocal(Face, X, Y, EMazeDir::N, A, B))
		{
			AppendCurvedWallEdge(A, B, Vertices, Triangles, Normals, UVs, Tangents);

			const FVector EdgeDir = (B - A).GetSafeNormal();
			AddCornerSample(A, EdgeDir);
			AddCornerSample(B, EdgeDir);
		}
	}

	Colors.Init(FColor::White, Vertices.Num());

	UE_LOG(LogTemp, Warning, TEXT("CornerMap Size: %d"), CornerMap.Num());
	WallProcMesh->CreateMeshSection(0, Vertices, Triangles, Normals, UVs, Colors, Tangents, true);

	if (CornerHISM && CornerMesh && CornerHISM->GetStaticMesh())
	{
		const float CornerHeight = WallHeight + CornerHeightExtra;
		const float CornerDiameterLocal = WallThickness * 1.0f;

		const float CornerMeshBaseDiameter = 100.0f;
		const float CornerMeshBaseHeight   = 100.0f;

		const float CornerScaleXY = FMath::Max(0.01f, CornerDiameterLocal / CornerMeshBaseDiameter);
		const float CornerScaleZ  = FMath::Max(0.01f, CornerHeight / CornerMeshBaseHeight);

		for (const TPair<FString, FCornerAccum>& Pair : CornerMap)
		{
			const FCornerAccum& Corner = Pair.Value;


			if (Corner.Dirs.Num() < 2)
			{
				continue;
			}

			if (!HasNonCollinearPair(Corner.Dirs))
			{
				continue;
			}

			const FVector Up = Corner.Pos.GetSafeNormal();
			if (Up.IsNearlyZero())
				continue;

			const FVector Loc = Corner.Pos + Up * (CornerHeight * 0.5f + WallSurfaceOffset);
			const FQuat BaseRot = FRotationMatrix::MakeFromZ(Up).ToQuat();
			const FQuat FlipRot = FQuat(FVector::RightVector, PI);
			const FQuat Rot = BaseRot * FlipRot;

			FTransform T;
			T.SetComponents(Rot, Loc, FVector(CornerScaleXY, CornerScaleXY, CornerScaleZ));

			if (T.IsValid())
			{
				CornerHISM->AddInstance(T);
			}
		}
	}

	if (WallMaterial)
	{
		WallProcMesh->SetMaterial(0, WallMaterial);
	}
	else if (WallMesh && WallMesh->GetStaticMaterials().Num() > 0)
	{
		if (UMaterialInterface *FallbackMat = WallMesh->GetStaticMaterials()[0].MaterialInterface)
		{
			WallProcMesh->SetMaterial(0, FallbackMat);
		}
	}
}

/**
 * desc : Called when the game starts. Spawns the initial Start Marker and idle AI Runner.
 * args : None
 * result: None
 */
void AOrchestrator::BeginPlay()
{
    Super::BeginPlay();

	EnsureMazeGenerated();

    if (ArtifactManagerClass && !ArtifactManager && SphereActor && Maze)
    {
        FActorSpawnParameters ArtifactSpawnParams;
        ArtifactSpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        ArtifactSpawnParams.Owner = this;

        ArtifactManager = GetWorld()->SpawnActor<AMazeArtifactManager>(
            ArtifactManagerClass,
            GetActorLocation(),
            FRotator::ZeroRotator,
            ArtifactSpawnParams);

        if (ArtifactManager)
        {
            ArtifactManager->Maze = Maze;
            ArtifactManager->SphereActor = SphereActor;
            ArtifactManager->PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);

            UE_LOG(LogTemp, Warning, TEXT("Spawned Artifact Manager from Orchestrator"));
        }
    }

    if (!SphereActor || !MazeRunnerClass || !MarkerMesh)
        return;

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    FTransform StartTransform;
    if (GetRandomSpawnTransform(StartTransform, 15.0f, 1, 5000, 1))
    {
        StartMarkerRef = GetWorld()->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), StartTransform, SpawnParams);
        if (StartMarkerRef)
        {
            StartMarkerRef->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
            StartMarkerRef->AttachToActor(SphereActor, FAttachmentTransformRules::KeepWorldTransform);
            StartMarkerRef->GetStaticMeshComponent()->SetStaticMesh(MarkerMesh);
            if (StartMaterial)
                StartMarkerRef->GetStaticMeshComponent()->SetMaterial(0, StartMaterial);
            StartMarkerRef->SetActorScale3D(FVector(0.25f));
        }

        ActiveRunner = GetWorld()->SpawnActor<AMazeRunner>(MazeRunnerClass, StartTransform, SpawnParams);
        if (ActiveRunner)
        {
            ActiveRunner->AttachToActor(SphereActor, FAttachmentTransformRules::KeepWorldTransform);
            ActiveRunner->OnPathCompleted.AddDynamic(this, &AOrchestrator::OnRunnerReachedArtifact);

            if (ArtifactManager)
            {
                ArtifactManager->AIPawn = ActiveRunner;
            }
        }
    }
}
/**
 * desc : Triggers the spawn of new artifacts on the maze and wakes up the AI to hunt them.
 * args : None
 * result: None
 */
void AOrchestrator::TriggerNextRun()
{
	RuntimeSeedOffset += 100;
	if (!SphereActor || !MarkerMesh || !ActiveRunner)
		return;

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	for (int32 i = 0; i < NumArtifactsToSpawn; ++i)
	{
		FTransform ArtTransform;
		if (GetRandomSpawnTransform(ArtTransform, 15.0f, 1, 5000, 2 + RuntimeSeedOffset + i))
		{
			AStaticMeshActor *Artifact = GetWorld()->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), ArtTransform, SpawnParams);
			if (Artifact)
			{
				Artifact->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
				Artifact->AttachToActor(SphereActor, FAttachmentTransformRules::KeepWorldTransform);
				Artifact->GetStaticMeshComponent()->SetStaticMesh(MarkerMesh);
				if (EndMaterial)
					Artifact->GetStaticMeshComponent()->SetMaterial(0, EndMaterial);
				Artifact->SetActorScale3D(FVector(0.25f));
				ActiveArtifacts.Add(Artifact);
			}
		}
	}

	// If the Runner is idle (has no target), wake it up to hunt the new item!
	if (!CurrentTargetArtifact)
	{
		OnRunnerReachedArtifact();
	}
}

/**
 * desc : Brain function bound to the AI Runner's delegate. Calculates shortest distance to
 * remaining artifacts, runs A* once, and dispatches the runner.
 * args : None
 * result: None
 */
void AOrchestrator::OnRunnerReachedArtifact()
{
	// 1. Destroy collected artifact
	if (CurrentTargetArtifact)
	{
		ActiveArtifacts.Remove(CurrentTargetArtifact);
		CurrentTargetArtifact->Destroy();
		CurrentTargetArtifact = nullptr;
	}

	// 2. Are we out of artifacts? Do nothing! Stand idle.
	if (ActiveArtifacts.IsEmpty())
	{
		return;
	}

	// 3. PERFORMANCE FIX: Find closest artifact using straight-line distance FIRST!
	AStaticMeshActor *BestArtifact = nullptr;
	float ClosestDistance = MAX_FLT; // Start with infinitely far away
	FVector StartLoc = ActiveRunner->GetActorLocation();

	for (AStaticMeshActor *Artifact : ActiveArtifacts)
	{
		// DistSquared is heavily optimized for CPUs because it skips calculating square roots!
		float Dist = FVector::DistSquared(StartLoc, Artifact->GetActorLocation());
		if (Dist < ClosestDistance)
		{
			ClosestDistance = Dist;
			BestArtifact = Artifact;
		}
	}

	// 4. ONLY RUN A* EXACTLY ONCE FOR THE WINNING ARTIFACT
	if (BestArtifact)
	{
		TArray<FVector> BestPath;

		// We only calculate the maze path for the closest item
		if (Navigator->FindPath(StartLoc, BestArtifact->GetActorLocation(), BestPath))
		{
			CurrentTargetArtifact = BestArtifact;

			TArray<FVector> LocalPath;
			FTransform SphereTransform = SphereActor->GetTransform();
			for (const FVector &WorldPoint : BestPath)
			{
				LocalPath.Add(SphereTransform.InverseTransformPosition(WorldPoint));
			}

			ActiveRunner->SetPath(LocalPath, SphereActor);
		}
	}
}
