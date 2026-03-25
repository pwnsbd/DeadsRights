#include "CubeToSphere.h"

#include "ProceduralMeshComponent.h"
#include "KismetProceduralMeshLibrary.h"

/**
 * desc : Default constructor. Creates procedural mesh component, sets root, and initializes face rotations.
 * args : None
 * result: None
 */
ACubeToSphere::ACubeToSphere()
{
	PrimaryActorTick.bCanEverTick = false;

	Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);

	CreateFaceRotations();
}

/**
 * desc : Editor/runtime construction hook. Rebuilds the sphere surface so changes reflect in editor.
 * args : Transform - current actor transform during construction.
 * result: None
 */
void ACubeToSphere::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	BuildSurface();
}

/**
 * desc : Precomputes 6 face rotation matrices so the same grid can be reused per face.
 * args : None
 * result: None
 */
void ACubeToSphere::CreateFaceRotations()
{
	if (FaceRotations.Num() != 6)
	{
		FaceRotations.SetNum(6);
	}

	// Equator faces
	FaceRotations[0] = FRotationMatrix::MakeFromZY(FVector(1, 0, 0), FVector(0, 0, 1)).Rotator();  // Front
	FaceRotations[1] = FRotationMatrix::MakeFromZY(FVector(0, 1, 0), FVector(0, 0, 1)).Rotator();  // Right
	FaceRotations[2] = FRotationMatrix::MakeFromZY(FVector(-1, 0, 0), FVector(0, 0, 1)).Rotator(); // Back
	FaceRotations[3] = FRotationMatrix::MakeFromZY(FVector(0, -1, 0), FVector(0, 0, 1)).Rotator(); // Left

	// Pole faces
	FaceRotations[4] = FRotationMatrix::MakeFromZY(FVector(0, 0, 1), FVector(-1, 0, 0)).Rotator(); // Top
	FaceRotations[5] = FRotationMatrix::MakeFromZY(FVector(0, 0, -1), FVector(1, 0, 0)).Rotator(); // Bottom
}

/**
 * desc : Builds (or rebuilds) the full cube->sphere mesh:
 *        - clamps resolution
 *        - creates face rotations
 *        - creates shared face grid
 *        - generates per-face sphere vertices
 *        - creates mesh sections (1 per face)
 * args : None
 * result: None
 */
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

/**
 * desc : Builds the base welded grid (verts/triangles/uvs) for one face.
 *        Also recenters the grid in local face space for consistent rotation.
 * args : None
 * result: None
 */
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

	// Center/offset the plane in local space before face rotation.
	const float Half = (Resolution - 1) * GridSpacing * 0.5f;

	for (FVector& V : FaceGridVertsLocal)
	{
		V += FVector(0.f, 0.f, Half);
	}

	VerticesPerSection = FaceGridVertsLocal.Num();
}

/**
 * desc : Generates projected sphere vertices for a given face index, using FaceGridVertsLocal.
 * args :
 *   - FaceIndex: face [0..5]
 *   - OutVerts: output array of LOCAL-space sphere vertices for this face
 * result: None
 */
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

/**
 * desc : Creates (or updates) a procedural mesh section for one face using the provided vertices.
 *        Normals are derived by normalizing each vertex direction from origin.
 * args :
 *   - FaceIndex: section index [0..5]
 *   - FaceVerts: LOCAL-space sphere vertices for this face
 * result: None
 */
void ACubeToSphere::BuildFaceSection(int32 FaceIndex, const TArray<FVector>& FaceVerts)
{
	TArray<FVector> Normals;
	Normals.Reserve(FaceVerts.Num());

	for (FVector V : FaceVerts)
	{
		if (V.Normalize(0.0001f))
		{
			Normals.Add(V);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Not Normalized"));
			Normals.Add(FVector::UpVector);
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

/**
 * desc : Returns the local-space center position of a maze cell on the sphere surface.
 *        Uses the four corner vertices of the cell, averages them, then re-projects to Radius.
 * args :
 *   - Face: cube face index [0..5]
 *   - CellX: cell x index [0..CellsPerFace-1]
 *   - CellY: cell y index [0..CellsPerFace-1]
 * result: Local-space center on the sphere surface, or ZeroVector if invalid.
 */
FVector ACubeToSphere::GetCellCenterLocal(int32 Face, int32 CellX, int32 CellY) const
{
	if (!FaceSphereVerts.IsValidIndex(Face))
	{
		return FVector::ZeroVector;
	}

	const TArray<FVector>& FV = FaceSphereVerts[Face];

	if (FV.Num() != Resolution * Resolution)
	{
		return FVector::ZeroVector;
	}

	const int32 N = GetCellsPerFace();
	if (CellX < 0 || CellX >= N || CellY < 0 || CellY >= N)
	{
		return FVector::ZeroVector;
	}

	const FVector V00 = FV[VertIndex(CellX,     CellY)];
	const FVector V10 = FV[VertIndex(CellX + 1, CellY)];
	const FVector V01 = FV[VertIndex(CellX,     CellY + 1)];
	const FVector V11 = FV[VertIndex(CellX + 1, CellY + 1)];

	FVector CenterPos = (V00 + V10 + V01 + V11) * 0.25f;

	return CenterPos.GetSafeNormal() * Radius;
}

/**
 * desc : Returns the world-space center position of a maze cell on the sphere surface.
 * args :
 *   - Face: cube face index [0..5]
 *   - CellX: cell x index
 *   - CellY: cell y index
 * result: World-space center on the sphere surface.
 */
FVector ACubeToSphere::GetCellCenterWorld(int32 Face, int32 CellX, int32 CellY) const
{
	return GetActorTransform().TransformPosition(GetCellCenterLocal(Face, CellX, CellY));
}

/**
 * desc : Returns local-space endpoints of a given cell wall edge (N/E/S/W) on the sphere.
 *        Contract: Other systems can build walls without knowing vertex indexing.
 * args :
 *   - Face, CellX, CellY: cell coordinate
 *   - Dir: requested wall direction (N/E/S/W)
 *   - OutA: endpoint A (local-space)
 *   - OutB: endpoint B (local-space)
 * result: True if endpoints were computed successfully; False if inputs/caches invalid.
 */
bool ACubeToSphere::GetCellWallEdgeLocal(int32 Face, int32 CellX, int32 CellY, EMazeDir Dir,
                                         FVector& OutA, FVector& OutB) const
{
	if (!FaceSphereVerts.IsValidIndex(Face))
		return false;

	const TArray<FVector>& FV = FaceSphereVerts[Face];
	if (FV.Num() != Resolution * Resolution)
		return false;

	const int32 N = GetCellsPerFace();
	if (CellX < 0 || CellX >= N || CellY < 0 || CellY >= N)
		return false;

	const FVector V00 = FV[VertIndex(CellX,     CellY)];
	const FVector V10 = FV[VertIndex(CellX + 1, CellY)];
	const FVector V01 = FV[VertIndex(CellX,     CellY + 1)];
	const FVector V11 = FV[VertIndex(CellX + 1, CellY + 1)];

	switch (Dir)
	{
	case EMazeDir::N: OutA = V00; OutB = V10; return true;
	case EMazeDir::S: OutA = V01; OutB = V11; return true;
	case EMazeDir::W: OutA = V00; OutB = V01; return true;
	case EMazeDir::E: OutA = V10; OutB = V11; return true;
	}

	return false;
}

/**
 * desc : Returns world-space endpoints of a given cell wall edge (N/E/S/W) on the sphere.
 * args :
 *   - Face, CellX, CellY: cell coordinate
 *   - Dir: requested wall direction (N/E/S/W)
 *   - OutA: endpoint A (world-space)
 *   - OutB: endpoint B (world-space)
 * result: True if endpoints were computed successfully; False otherwise.
 */
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