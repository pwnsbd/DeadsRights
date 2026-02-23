#include "CubeToSphere.h"

#include "ProceduralMeshComponent.h"
#include "KismetProceduralMeshLibrary.h"

ACubeToSphere::ACubeToSphere()
{
	PrimaryActorTick.bCanEverTick = false;

	Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	CreateFaceRotations();
}

void ACubeToSphere::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	BuildSurface();
}

void ACubeToSphere::Build()
{
	// Backwards compatibility
	BuildSurface();
}

void ACubeToSphere::CreateFaceRotations()
{
	// Your current rotation set (can be corrected later if needed)
	if (FaceRotations.Num() != 6)
	{
		FaceRotations.SetNum(6);
		FaceRotations[0] = FRotator( 90, 0,   0); //front 
		FaceRotations[1] = FRotator(0, 0,   90); // right
		FaceRotations[2] = FRotator(270, 0,   0); // back
		FaceRotations[3] = FRotator(  0, 0,   -90); // left  
		FaceRotations[4] = FRotator(  0, 0,  0);  // top
		FaceRotations[5] = FRotator(  180, 0, 0); // bottom
	}
}

void ACubeToSphere::BuildSurface()
{
	if (!Mesh) return;

	Resolution = FMath::Max(2, Resolution);
	CreateFaceRotations();

	FaceSphereVerts.SetNum(6);

	CreateFaceGrid();

	Mesh->ClearAllMeshSections();

	for (int32 Face = 0; Face < 6; ++Face)
	{
		TArray<FVector> FaceVerts;
		VertsPerFace(Face, FaceVerts);

		FaceSphereVerts[Face] = FaceVerts;

		BuildFaceSection(Face, FaceVerts);
	}
}

void ACubeToSphere::CreateFaceGrid()
{
	FaceGridVertsLocal.Reset();
	FaceTriangles.Reset();
	FaceUVs.Reset();

	UKismetProceduralMeshLibrary::CreateGridMeshWelded(
		Resolution, Resolution,
		FaceTriangles,
		FaceGridVertsLocal,
		FaceUVs,
		GridSpacing
	);

	// Offset the plane so the grid is centered/positioned consistently before face rotation.
	const float Half = (Resolution - 1) * GridSpacing * 0.5f;

	for (FVector& V : FaceGridVertsLocal)
	{
		V += FVector(0.f, 0.f, Half);
	}

	VerticesPerSection = FaceGridVertsLocal.Num();
}

void ACubeToSphere::VertsPerFace(int32 FaceIndex, TArray<FVector>& OutVerts) const
{
	OutVerts.Reset(FaceGridVertsLocal.Num());

	const FRotator FaceRot = FaceRotations.IsValidIndex(FaceIndex)
		? FaceRotations[FaceIndex]
		: FRotator::ZeroRotator;

	for (const FVector& VLocal : FaceGridVertsLocal)
	{
		// Rotate face into cube-space
		FVector V = FaceRot.RotateVector(VLocal);

		// Project cube -> sphere
		V.Normalize(0.0001f);
		V *= Radius;

		OutVerts.Add(V);
	}
}

void ACubeToSphere::BuildFaceSection(int32 FaceIndex, const TArray<FVector>& FaceVerts)
{
	TArray<FVector> EmptyNormals;
	TArray<FColor> EmptyColors;
	TArray<FProcMeshTangent> EmptyTangents;

	if (CustomMaterial)
	{
		Mesh->SetMaterial(FaceIndex, CustomMaterial);
	}

	Mesh->CreateMeshSection(
		FaceIndex,
		FaceVerts,
		FaceTriangles,
		EmptyNormals,
		FaceUVs,
		EmptyColors,
		EmptyTangents,
		true
	);
}

FVector ACubeToSphere::GetCellCenterLocal(int32 Face, int32 CellX, int32 CellY) const
{
	const int32 CellsPerFace = FMath::Max(1, Resolution - 1);

	Face  = FMath::Clamp(Face, 0, 5);
	CellX = FMath::Clamp(CellX, 0, CellsPerFace - 1);
	CellY = FMath::Clamp(CellY, 0, CellsPerFace - 1);

	const float Half = (Resolution - 1) * GridSpacing * 0.5f;

	FVector P;
	P.X = (CellX + 0.5f) * GridSpacing;
	P.Y = (CellY + 0.5f) * GridSpacing;
	P.Z = 0.f;

	P += FVector(0.f, 0.f, Half);

	const FRotator FaceRot = FaceRotations.IsValidIndex(Face) ? FaceRotations[Face] : FRotator::ZeroRotator;
	P = FaceRot.RotateVector(P);

	return P.GetSafeNormal(0.0001f) * Radius;
}

FVector ACubeToSphere::GetCellCenterWorld(int32 Face, int32 CellX, int32 CellY) const
{
	return GetActorTransform().TransformPosition(GetCellCenterLocal(Face, CellX, CellY));
}

bool ACubeToSphere::GetCellWallEdgeLocal(int32 Face, int32 CellX, int32 CellY, EMazeDir Dir,
                                         FVector& OutA, FVector& OutB) const
{
	// Requires surface to be built at least once
	if (!FaceSphereVerts.IsValidIndex(Face)) return false;

	const TArray<FVector>& FV = FaceSphereVerts[Face];
	if (FV.Num() != Resolution * Resolution) return false;

	const int32 N = GetCellsPerFace();
	if (CellX < 0 || CellX >= N || CellY < 0 || CellY >= N) return false;

	// Cell corners in the vertex grid
	const FVector V00 = FV[VertIndex(CellX,     CellY    )];
	const FVector V10 = FV[VertIndex(CellX + 1, CellY    )];
	const FVector V01 = FV[VertIndex(CellX,     CellY + 1)];
	const FVector V11 = FV[VertIndex(CellX + 1, CellY + 1)];

	// Edge endpoints by direction
	switch (Dir)
	{
		case EMazeDir::N: OutA = V00; OutB = V10; return true;
		case EMazeDir::S: OutA = V01; OutB = V11; return true;
		case EMazeDir::W: OutA = V00; OutB = V01; return true;
		case EMazeDir::E: OutA = V10; OutB = V11; return true;
	}

	return false;
}

bool ACubeToSphere::GetCellWallEdgeWorld(int32 Face, int32 CellX, int32 CellY, EMazeDir Dir,
                                         FVector& OutA, FVector& OutB) const
{
	FVector ALocal, BLocal;
	if (!GetCellWallEdgeLocal(Face, CellX, CellY, Dir, ALocal, BLocal))
	{
		return false;
	}

	OutA = GetActorTransform().TransformPosition(ALocal);
	OutB = GetActorTransform().TransformPosition(BLocal);
	return true;
}

FMazeNode ACubeToSphere::WorldToMazeCell(FVector WorldPosition) const
{
	// Convert world position to local space
	FVector LocalPos = GetActorTransform().InverseTransformPosition(WorldPosition);

	// Project the local position onto the sphere surface
	FVector NormalizedPos = LocalPos.GetSafeNormal(0.0001f) * Radius;

	const int32 CellsPerFace = GetCellsPerFace();

	// Find the closest face and cell
	int32 BestFace = 0;
	int32 BestX = 0;
	int32 BestY = 0;
	float BestDistance = FLT_MAX;

	// Check all faces and cells to find the closest match
	for (int32 Face = 0; Face < 6; ++Face)
	{
		for (int32 X = 0; X < CellsPerFace; ++X)
		{
			for (int32 Y = 0; Y < CellsPerFace; ++Y)
			{
				FVector CellCenter = GetCellCenterLocal(Face, X, Y);
				float Distance = FVector::DistSquared(NormalizedPos, CellCenter);

				if (Distance < BestDistance)
				{
					BestDistance = Distance;
					BestFace = Face;
					BestX = X;
					BestY = Y;
				}
			}
		}
	}

	return FMazeNode(BestFace, BestX, BestY);
}

EMazeDir ACubeToSphere::GetDirectionFromVector(FVector ForwardVector, const FMazeNode& CurrentCell) const
{
	// Convert forward vector to local space
	FVector LocalForward = GetActorTransform().InverseTransformVector(ForwardVector).GetSafeNormal();

	// Get the current cell center in local space
	FVector CellCenter = GetCellCenterLocal(CurrentCell.Face, CurrentCell.X, CurrentCell.Y);

	// Get the cell centers of neighbors in each direction
	const int32 CellsPerFace = GetCellsPerFace();

	// Sample positions in each direction (approximate)
	FVector NorthPos = CellCenter;
	FVector EastPos = CellCenter;
	FVector SouthPos = CellCenter;
	FVector WestPos = CellCenter;

	// Use the face rotation to determine local directions
	if (CurrentCell.Face >= 0 && CurrentCell.Face < 6)
	{
		const FRotator FaceRot = FaceRotations[CurrentCell.Face];
		
		// Get approximat	e direction vectors in face-local space
		FVector LocalN = FaceRot.RotateVector(FVector(0, -1, 0));
		FVector LocalE = FaceRot.RotateVector(FVector(1, 0, 0));
		FVector LocalS = FaceRot.RotateVector(FVector(0, 1, 0));
		FVector LocalW = FaceRot.RotateVector(FVector(-1, 0, 0));

		// Calculate dot products to find best matching direction
		float DotN = FVector::DotProduct(LocalForward, LocalN);
		float DotE = FVector::DotProduct(LocalForward, LocalE);
		float DotS = FVector::DotProduct(LocalForward, LocalS);
		float DotW = FVector::DotProduct(LocalForward, LocalW);

		// Find the direction with the highest dot product
		float MaxDot = FMath::Max(FMath::Max(DotN, DotE), FMath::Max(DotS, DotW));

		if (MaxDot == DotN) return EMazeDir::N;
		if (MaxDot == DotE) return EMazeDir::E;
		if (MaxDot == DotS) return EMazeDir::S;
		if (MaxDot == DotW) return EMazeDir::W;
	}

	// Default to North if something goes wrong
	return EMazeDir::N;
}
