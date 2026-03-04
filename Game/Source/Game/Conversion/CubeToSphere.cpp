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

void ACubeToSphere::OnConstruction(const FTransform &Transform)
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
	if (FaceRotations.Num() != 6)
	{
		FaceRotations.SetNum(6);
	}

	// Lock the axes! Up is Z, Right is Y/X, Normal is Outward.
	// Equator Faces (0, 1, 2, 3)
	FaceRotations[0] = FRotationMatrix::MakeFromZY(FVector(1, 0, 0), FVector(0, 0, 1)).Rotator();  // Front
	FaceRotations[1] = FRotationMatrix::MakeFromZY(FVector(0, 1, 0), FVector(0, 0, 1)).Rotator();  // Right
	FaceRotations[2] = FRotationMatrix::MakeFromZY(FVector(-1, 0, 0), FVector(0, 0, 1)).Rotator(); // Back
	FaceRotations[3] = FRotationMatrix::MakeFromZY(FVector(0, -1, 0), FVector(0, 0, 1)).Rotator(); // Left

	// Pole Faces (4, 5)
	FaceRotations[4] = FRotationMatrix::MakeFromZY(FVector(0, 0, 1), FVector(-1, 0, 0)).Rotator(); // Top
	FaceRotations[5] = FRotationMatrix::MakeFromZY(FVector(0, 0, -1), FVector(1, 0, 0)).Rotator(); // Bottom
}

void ACubeToSphere::BuildSurface()
{
	if (!Mesh)
		return;

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
		GridSpacing);

	// Offset the plane so the grid is centered/positioned consistently before face rotation.
	const float Half = (Resolution - 1) * GridSpacing * 0.5f;

	for (FVector &V : FaceGridVertsLocal)
	{
		V += FVector(0.f, 0.f, Half);
	}

	VerticesPerSection = FaceGridVertsLocal.Num();
}

void ACubeToSphere::VertsPerFace(int32 FaceIndex, TArray<FVector> &OutVerts) const
{
	OutVerts.Reset(FaceGridVertsLocal.Num());

	const FRotator FaceRot = FaceRotations.IsValidIndex(FaceIndex)
								 ? FaceRotations[FaceIndex]
								 : FRotator::ZeroRotator;

	// Calculate the "radius" of the raw flat cube before it is spherified
	const float CubeHalfSize = (Resolution - 1) * GridSpacing * 0.5f;

	for (const FVector &VLocal : FaceGridVertsLocal)
	{
		// Rotate face into cube-space
		FVector V = FaceRot.RotateVector(VLocal);

		// Project cube -> sphere
		V.Normalize(0.0001f);
		V *= Radius;

		OutVerts.Add(V);
	}
}

void ACubeToSphere::BuildFaceSection(int32 FaceIndex, const TArray<FVector> &FaceVerts)
{
	// Normalize face vertices
	TArray<FVector> Normals;
	for (auto i : FaceVerts)
	{
		if (i.Normalize(0.0001f))
		{
			Normals.Add(i);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Not Normalized"));
		}
	}
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
		Normals,
		FaceUVs,
		EmptyColors,
		EmptyTangents,
		true);
}

FVector ACubeToSphere::GetCellCenterLocal(int32 Face, int32 CellX, int32 CellY) const
{
	// 1. Safety check to make sure the surface actually exists
	if (!FaceSphereVerts.IsValidIndex(Face))
	{
		return FVector::ZeroVector;
	}

	const TArray<FVector> &FV = FaceSphereVerts[Face];

	if (FV.Num() != Resolution * Resolution)
	{
		return FVector::ZeroVector;
	}

	const int32 N = GetCellsPerFace();
	if (CellX < 0 || CellX >= N || CellY < 0 || CellY >= N)
	{
		return FVector::ZeroVector;
	}

	// 2. Get the exact, spherified physical 3D corners of the cell
	const FVector V00 = FV[VertIndex(CellX, CellY)];
	const FVector V10 = FV[VertIndex(CellX + 1, CellY)];
	const FVector V01 = FV[VertIndex(CellX, CellY + 1)];
	const FVector V11 = FV[VertIndex(CellX + 1, CellY + 1)];

	// 3. Average them to find the exact dead-center of the rendered hallway
	FVector CenterPos = (V00 + V10 + V01 + V11) * 0.25f;

	// 4. Averaging pulls the center slightly underground, so push it back up to the surface radius!
	return CenterPos.GetSafeNormal() * Radius;
}

FVector ACubeToSphere::GetCellCenterWorld(int32 Face, int32 CellX, int32 CellY) const
{
	return GetActorTransform().TransformPosition(GetCellCenterLocal(Face, CellX, CellY));
}

// FVector ACubeToSphere::GetCellCenterLocal(int32 Face, int32 CellX, int32 CellY) const
// {
// 	if (!FaceSphereVerts.IsValidIndex(Face))
// 	{
// 		return FVector::ZeroVector;
// 	}

// 	const TArray<FVector> &FV = FaceSphereVerts[Face];

// 	// Get all 4 corners of the cell
// 	const FVector V00 = FV[VertIndex(CellX, CellY)];
// 	const FVector V10 = FV[VertIndex(CellX + 1, CellY)];
// 	const FVector V01 = FV[VertIndex(CellX, CellY + 1)];
// 	const FVector V11 = FV[VertIndex(CellX + 1, CellY + 1)];

// 	// Average them to find the exact dead-center of the hallway!
// 	return (V00 + V10 + V01 + V11) * 0.25f;
// }

bool ACubeToSphere::GetCellWallEdgeLocal(int32 Face, int32 CellX, int32 CellY, EMazeDir Dir,
										 FVector &OutA, FVector &OutB) const
{
	// Requires surface to be built at least once
	if (!FaceSphereVerts.IsValidIndex(Face))
		return false;

	const TArray<FVector> &FV = FaceSphereVerts[Face];
	if (FV.Num() != Resolution * Resolution)
		return false;

	const int32 N = GetCellsPerFace();
	if (CellX < 0 || CellX >= N || CellY < 0 || CellY >= N)
		return false;

	// Cell corners in the vertex grid
	const FVector V00 = FV[VertIndex(CellX, CellY)];
	const FVector V10 = FV[VertIndex(CellX + 1, CellY)];
	const FVector V01 = FV[VertIndex(CellX, CellY + 1)];
	const FVector V11 = FV[VertIndex(CellX + 1, CellY + 1)];

	// Edge endpoints by direction
	switch (Dir)
	{
	case EMazeDir::N:
		OutA = V00;
		OutB = V10;
		return true;
	case EMazeDir::S:
		OutA = V01;
		OutB = V11;
		return true;
	case EMazeDir::W:
		OutA = V00;
		OutB = V01;
		return true;
	case EMazeDir::E:
		OutA = V10;
		OutB = V11;
		return true;
	}

	return false;
}

bool ACubeToSphere::GetCellWallEdgeWorld(int32 Face, int32 CellX, int32 CellY, EMazeDir Dir,
										 FVector &OutA, FVector &OutB) const
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
