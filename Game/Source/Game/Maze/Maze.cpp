#include "Maze.h" // <-- your new header
#include "Math/UnrealMathUtility.h"

// ------------------------------------------------------------
// Public API
// ------------------------------------------------------------

void UMaze::Generate()
{
	// 1) Validate inputs + allocate storage
	CellsPerFace = FMath::Max(1, CellsPerFace);

	const int32 Total = 6 * CellsPerFace * CellsPerFace;
	// Cells.SetNum(Total);
	Cells.SetNumZeroed(Total);

	// 2) Reset all cells to "all walls, not visited"
	ResetCells();

	// 3) Carve a DFS maze on each face independently
	for (int32 Face = 0; Face < 6; ++Face)
	{
		CarveDFSSingleFace(Face);
	}

	// 4) Open a few corridors between faces (so the maze becomes connected across faces)
	StitchFaces();
}

bool UMaze::IsValid(int32 Face, int32 X, int32 Y) const
{
	return (Face >= 0 && Face < 6 &&
			X >= 0 && X < CellsPerFace &&
			Y >= 0 && Y < CellsPerFace);
}

const FMazeCell &UMaze::GetCell(int32 Face, int32 X, int32 Y) const
{
	static FMazeCell Empty;

	if (!IsValid(Face, X, Y))
	{
		return Empty;
	}

	const int32 I = Index(Face, X, Y);
	if (!Cells.IsValidIndex(I))
	{
		return Empty;
	}

	return Cells[I];
}

// ------------------------------------------------------------
// Internal helpers
// ------------------------------------------------------------

void UMaze::ResetCells()
{
	for (FMazeCell &C : Cells)
	{
		C.bVisited = false;
		C.OpenN = false;
		C.OpenE = false;
		C.OpenS = false;
		C.OpenW = false;
	}
}

void UMaze::OpenBetween(int32 Face, int32 X1, int32 Y1,
						int32 X2, int32 Y2,
						EMazeDir DirFromAtoB)
{
	FMazeCell &A = Cells[Index(Face, X1, Y1)];
	FMazeCell &B = Cells[Index(Face, X2, Y2)];

	switch (DirFromAtoB)
	{
	case EMazeDir::N:
		A.OpenN = true;
		B.OpenS = true;
		break;
	case EMazeDir::S:
		A.OpenS = true;
		B.OpenN = true;
		break;
	case EMazeDir::E:
		A.OpenE = true;
		B.OpenW = true;
		break;
	case EMazeDir::W:
		A.OpenW = true;
		B.OpenE = true;
		break;
	}
}

// ------------------------------------------------------------
// DFS maze carving per-face
// ------------------------------------------------------------

void UMaze::CarveDFSSingleFace(int32 TargetFace)
{
	if (Cells.Num() == 0)
	{
		return;
	}

	// Separate deterministic RNG per face (same Seed => stable results)
	FRandomStream FaceRNG(Seed + TargetFace);

	// Random start cell on this face
	const int32 StartX = FaceRNG.RandRange(0, CellsPerFace - 1);
	const int32 StartY = FaceRNG.RandRange(0, CellsPerFace - 1);

	// DFS stack stores (X,Y). Face is fixed here.
	struct FStackCell
	{
		int32 X, Y;
	};
	TArray<FStackCell> Stack;
	Stack.Reserve(CellsPerFace * CellsPerFace);

	Cells[Index(TargetFace, StartX, StartY)].bVisited = true;
	Stack.Add({StartX, StartY});

	// Candidate neighbor (direction + neighbor coordinates)
	struct FCandidate
	{
		EMazeDir Dir;
		int32 NX;
		int32 NY;
	};

	while (Stack.Num() > 0)
	{
		const FStackCell Curr = Stack.Last();
		const int32 CX = Curr.X;
		const int32 CY = Curr.Y;

		TArray<FCandidate> Candidates;
		Candidates.Reserve(4);

		auto TryAdd = [&](EMazeDir Dir, int32 NX, int32 NY)
		{
			if (!IsValid(TargetFace, NX, NY))
			{
				return;
			}

			if (!Cells[Index(TargetFace, NX, NY)].bVisited)
			{
				Candidates.Add({Dir, NX, NY});
			}
		};

		// 4-neighborhood on the same face
		TryAdd(EMazeDir::N, CX, CY - 1);
		TryAdd(EMazeDir::S, CX, CY + 1);
		TryAdd(EMazeDir::E, CX + 1, CY);
		TryAdd(EMazeDir::W, CX - 1, CY);

		// Dead end => backtrack
		if (Candidates.Num() == 0)
		{
			Stack.Pop();
			continue;
		}

		// Pick a random unvisited neighbor
		const FCandidate &Pick = Candidates[FaceRNG.RandRange(0, Candidates.Num() - 1)];

		// Carve corridor between current and chosen neighbor
		OpenBetween(TargetFace, CX, CY, Pick.NX, Pick.NY, Pick.Dir);

		// Visit and continue DFS
		Cells[Index(TargetFace, Pick.NX, Pick.NY)].bVisited = true;
		Stack.Add({Pick.NX, Pick.NY});
	}
}

// ------------------------------------------------------------
// Face stitching (open corridors between cube faces)
// Cube layout:
//        [4]
//    [3][0][1][2]
//        [5]
// ------------------------------------------------------------

void UMaze::StitchFaces()
{
	FRandomStream StitchRNG(Seed + 1000);
	const int32 MinSpacing = FMath::Max(3, CellsPerFace / 4);

	auto RandOffset = [&](int32 MaxInclusive)
	{ return StitchRNG.RandRange(0, MaxInclusive); };
	auto SpreadPos = [&](int32 i) -> int32
	{
		int32 P = i * (CellsPerFace - 1) / (CorridorsPerBorder + 1);
		P += RandOffset(MinSpacing / 2);
		return FMath::Clamp(P, 0, CellsPerFace - 1);
	};

	auto OpenBorder = [&](int32 F1, int32 X1, int32 Y1, EMazeDir D1, int32 F2, int32 X2, int32 Y2, EMazeDir D2)
	{
		FMazeCell &A = Cells[Index(F1, X1, Y1)];
		FMazeCell &B = Cells[Index(F2, X2, Y2)];

		if (D1 == EMazeDir::N)
			A.OpenN = true;
		else if (D1 == EMazeDir::S)
			A.OpenS = true;
		else if (D1 == EMazeDir::E)
			A.OpenE = true;
		else if (D1 == EMazeDir::W)
			A.OpenW = true;
		if (D2 == EMazeDir::N)
			B.OpenN = true;
		else if (D2 == EMazeDir::S)
			B.OpenS = true;
		else if (D2 == EMazeDir::E)
			B.OpenE = true;
		else if (D2 == EMazeDir::W)
			B.OpenW = true;
	};

	// Middle ring (0-1-2-3-0)
	for (int32 i = 0; i < CorridorsPerBorder; ++i)
	{
		int32 pos = SpreadPos(i);
		OpenBorder(0, CellsPerFace - 1, pos, EMazeDir::E, 1, 0, pos, EMazeDir::W);
		OpenBorder(1, CellsPerFace - 1, pos, EMazeDir::E, 2, 0, pos, EMazeDir::W);
		OpenBorder(2, CellsPerFace - 1, pos, EMazeDir::E, 3, 0, pos, EMazeDir::W);
		OpenBorder(3, CellsPerFace - 1, pos, EMazeDir::E, 0, 0, pos, EMazeDir::W);
	}

	// Top face connections (4)
	for (int32 i = 0; i < CorridorsPerBorder; ++i)
	{
		int32 pos = SpreadPos(i);
		OpenBorder(0, pos, CellsPerFace - 1, EMazeDir::S, 4, pos, 0, EMazeDir::N);
		OpenBorder(1, pos, CellsPerFace - 1, EMazeDir::S, 4, CellsPerFace - 1, pos, EMazeDir::E);
		OpenBorder(2, pos, CellsPerFace - 1, EMazeDir::S, 4, CellsPerFace - 1 - pos, CellsPerFace - 1, EMazeDir::S);
		OpenBorder(3, pos, CellsPerFace - 1, EMazeDir::S, 4, 0, CellsPerFace - 1 - pos, EMazeDir::W);
	}

	// Bottom face connections (5)
	for (int32 i = 0; i < CorridorsPerBorder; ++i)
	{
		int32 pos = SpreadPos(i);
		OpenBorder(0, pos, 0, EMazeDir::N, 5, pos, CellsPerFace - 1, EMazeDir::S);
		OpenBorder(1, pos, 0, EMazeDir::N, 5, CellsPerFace - 1, CellsPerFace - 1 - pos, EMazeDir::E);
		OpenBorder(2, pos, 0, EMazeDir::N, 5, CellsPerFace - 1 - pos, 0, EMazeDir::N);
		OpenBorder(3, pos, 0, EMazeDir::N, 5, 0, pos, EMazeDir::W);
	}
}

// AI helper function to get traversable neighbors of a node in the maze graph
TArray<FMazeNode> UMaze::GetTraversableNeighbors(const FMazeNode &Node) const
{
	TArray<FMazeNode> Neighbors; // Contains all valid neighboring nodes that can be traversed to from the input node

	// Safety check – invalid nodes return empty neighbor list
	if (!IsValid(Node.Face, Node.X, Node.Y))
		return Neighbors;

	const FMazeCell &Cell = GetCell(Node.Face, Node.X, Node.Y); // get the cell data for the input node
	const int32 Max = CellsPerFace - 1;

	// Attempt movement in each direction if there's an open corridor - Helper function
	auto TryAddNeighbor = [&](EMazeDir Dir)
	{
		// Calculate potential neighbor's coordinates based on direction
		int32 nf = Node.Face;
		int32 nx = Node.X;
		int32 ny = Node.Y;

		// Attempts movement within same face first
		switch (Dir)
		{
		case EMazeDir::N:
			ny -= 1;
			break;
		case EMazeDir::S:
			ny += 1;
			break;
		case EMazeDir::E:
			nx += 1;
			break;
		case EMazeDir::W:
			nx -= 1;
			break;
		}

		// If still inside bounds → simple same-face move, and add to vector
		if (IsValid(nf, nx, ny))
		{
			Neighbors.Add(FMazeNode(nf, nx, ny));
			return;
		}

		// Else, If we reach here, we crossed a face boundary and must handle transition logic

		// Middle ring (0-3)
		if (Node.Face >= 0 && Node.Face <= 3)
		{
			if (Dir == EMazeDir::E && Node.X == Max)
			{
				Neighbors.Add(FMazeNode((Node.Face + 1) % 4, 0, Node.Y));
				return;
			}
			if (Dir == EMazeDir::W && Node.X == 0)
			{
				Neighbors.Add(FMazeNode((Node.Face + 3) % 4, Max, Node.Y));
				return;
			}

			// Top edge -> Connects to Face 4
			if (Dir == EMazeDir::S && Node.Y == Max)
			{
				if (Node.Face == 0)
				{
					Neighbors.Add(FMazeNode(4, Node.X, 0));
					return;
				}
				if (Node.Face == 1)
				{
					Neighbors.Add(FMazeNode(4, Max, Node.X));
					return;
				}
				if (Node.Face == 2)
				{
					Neighbors.Add(FMazeNode(4, Max - Node.X, Max));
					return;
				}
				if (Node.Face == 3)
				{
					Neighbors.Add(FMazeNode(4, 0, Max - Node.X));
					return;
				}
			}

			// Bottom edge -> Connects to Face 5
			if (Dir == EMazeDir::N && Node.Y == 0)
			{
				if (Node.Face == 0)
				{
					Neighbors.Add(FMazeNode(5, Node.X, Max));
					return;
				}
				if (Node.Face == 1)
				{
					Neighbors.Add(FMazeNode(5, Max, Max - Node.X));
					return;
				}
				if (Node.Face == 2)
				{
					Neighbors.Add(FMazeNode(5, Max - Node.X, 0));
					return;
				}
				if (Node.Face == 3)
				{
					Neighbors.Add(FMazeNode(5, 0, Node.X));
					return;
				}
			}
		}

		// Face 4 (Top)
		if (Node.Face == 4)
		{
			if (Dir == EMazeDir::N && Node.Y == 0)
			{
				Neighbors.Add(FMazeNode(0, Node.X, Max));
				return;
			}
			if (Dir == EMazeDir::E && Node.X == Max)
			{
				Neighbors.Add(FMazeNode(1, Node.Y, Max));
				return;
			}
			if (Dir == EMazeDir::S && Node.Y == Max)
			{
				Neighbors.Add(FMazeNode(2, Max - Node.X, Max));
				return;
			}
			if (Dir == EMazeDir::W && Node.X == 0)
			{
				Neighbors.Add(FMazeNode(3, Max - Node.Y, Max));
				return;
			}
		}

		// Face 5 (Bottom)
		if (Node.Face == 5)
		{
			if (Dir == EMazeDir::S && Node.Y == Max)
			{
				Neighbors.Add(FMazeNode(0, Node.X, 0));
				return;
			}
			if (Dir == EMazeDir::E && Node.X == Max)
			{
				Neighbors.Add(FMazeNode(1, Max - Node.Y, 0));
				return;
			}
			if (Dir == EMazeDir::N && Node.Y == 0)
			{
				Neighbors.Add(FMazeNode(2, Max - Node.X, 0));
				return;
			}
			if (Dir == EMazeDir::W && Node.X == 0)
			{
				Neighbors.Add(FMazeNode(3, Node.Y, 0));
				return;
			}
		}
	};

	// Only attempt directions that are open corridors
	if (Cell.OpenN)
		TryAddNeighbor(EMazeDir::N);
	if (Cell.OpenS)
		TryAddNeighbor(EMazeDir::S);
	if (Cell.OpenE)
		TryAddNeighbor(EMazeDir::E);
	if (Cell.OpenW)
		TryAddNeighbor(EMazeDir::W);

	return Neighbors;
}
