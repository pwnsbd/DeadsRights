#include "MazeNavigator.h"
#include "../Maze/Maze.h"
#include "../Conversion/CubeToSphere.h"
#include "Kismet/KismetMathLibrary.h"
#include "Algo/Reverse.h"

// =========================================================================
// Initialization
// =========================================================================

void UMazeNavigator::Init(UMaze *InMaze, ACubeToSphere *InSphere)
{
    Maze = InMaze;
    Sphere = InSphere;
}

// =========================================================================
// Coordinate Translation
// =========================================================================

FMazeNode UMazeNavigator::WorldToNode(FVector WorldPos) const
{
    if (!Sphere || !Maze)
    {
        return FMazeNode(0, 0, 0);
    }

    float BestDistSq = FLT_MAX;
    FMazeNode BestNode(0, 0, 0);

    // TODO: Optimize mathematically later. For now, brute force distance check.
    for (int32 Face = 0; Face < 6; Face++)
    {
        for (int32 X = 0; X < Maze->CellsPerFace; X++)
        {
            for (int32 Y = 0; Y < Maze->CellsPerFace; Y++)
            {
                FVector Center = Sphere->GetCellCenterWorld(Face, X, Y);
                float DistSq = FVector::DistSquared(Center, WorldPos);

                if (DistSq < BestDistSq)
                {
                    BestDistSq = DistSq;
                    BestNode = FMazeNode(Face, X, Y);
                }
            }
        }
    }
    return BestNode;
}

TArray<FMazeNode> UMazeNavigator::GetNeighbors(const FMazeNode &Node) const
{
    return Maze->GetTraversableNeighbors(Node);
}

// =========================================================================
// A* Pathfinding Algorithm
// =========================================================================

bool UMazeNavigator::FindPath(FVector StartPos, FVector EndPos, TArray<FVector> &OutPath, FVector ThreatPos, float ThreatRadius)
{
    if (!Maze || !Sphere)
    {
        return false;
    }

    // 1. Convert world positions to logical grid nodes
    FMazeNode StartNode = WorldToNode(StartPos);
    FMazeNode EndNode = WorldToNode(EndPos);

    if (StartNode == EndNode)
    {
        return false; // already at destination
    }

    // 2. Initialize A* Data Structures
    TArray<FMazeNode> OpenSet;
    TMap<FMazeNode, float> CostSoFar;
    TMap<FMazeNode, FMazeNode> CameFrom;

    CostSoFar.Add(StartNode, 0.0f);

    // ---------------------------------------------------------------------
    // REFACTOR: Extract Method (Lambda)
    // We define the Priority checking logic ONCE here, and reuse it everywhere.
    // Priority = (Cost from Start) + (Distance to End)
    // ---------------------------------------------------------------------
    auto CalculatePriority = [&](const FMazeNode &Node) -> float
    {
        float Cost = CostSoFar.Contains(Node) ? CostSoFar[Node] : FLT_MAX;
        float Heuristic = FVector::Dist(Sphere->GetCellCenterWorld(Node.Face, Node.X, Node.Y), EndPos);
        return Cost + Heuristic;
    };

    auto CompareNodes = [&](const FMazeNode &A, const FMazeNode &B)
    {
        return CalculatePriority(A) < CalculatePriority(B);
    };
    // ---------------------------------------------------------------------

    // 3. Begin A* Loop
    OpenSet.HeapPush(StartNode, CompareNodes);

    // A* main loop
    while (OpenSet.Num() > 0)
    {
        // Pop the node with the lowest estimated total cost
        FMazeNode CurrentNode;
        OpenSet.HeapPop(CurrentNode, CompareNodes);

        // 4. Base Case: Destination Reached! Reconstruct the path.
        if (CurrentNode == EndNode)
        {
            FMazeNode step = EndNode;

            while (!(step == StartNode))
            {
                OutPath.Add(Sphere->GetCellCenterWorld(step.Face, step.X, step.Y));

                if (CameFrom.Contains(step))
                {
                    step = CameFrom[step];
                }
                else
                {
                    break; // Failsafe to prevent infinite loops
                }
            }

            // Add the starting position and flip the array so it goes Start -> End
            OutPath.Add(Sphere->GetCellCenterWorld(StartNode.Face, StartNode.X, StartNode.Y));
            Algo::Reverse(OutPath);
            return true;
        }

        // 5. Explore valid neighbors
        TArray<FMazeNode> Neighbors = GetNeighbors(CurrentNode);

        for (const FMazeNode &Next : Neighbors)
        {

            float StepCost = 1.0f;

            // IN MAZENAVIGATOR.CPP (Inside the For-Loop of FindPath):

            // --- FEATURE 1: BALANCED THREAT PATHFINDING ---
            // --- FEATURE 1: BALANCED THREAT PATHFINDING ---
            if (ThreatRadius > 0.0f)
            {
                FVector NodeLoc = Sphere->GetCellCenterWorld(Next.Face, Next.X, Next.Y);
                float DistToThreat = FVector::Dist(NodeLoc, ThreatPos);

                if (DistToThreat < ThreatRadius)
                {
                    // Linear penalty is better than Exponential for preventing freezes
                    StepCost += (ThreatRadius - DistToThreat) * 0.5f;
                }
            }

            float newCost = CostSoFar[CurrentNode] + StepCost; // Uniform cost for grid movement

            // If we haven't visited this neighbor, or we found a shorter path to it
            if (!CostSoFar.Contains(Next) || newCost < CostSoFar[Next])
            {
                CostSoFar.Add(Next, newCost);
                CameFrom.Add(Next, CurrentNode);

                if (!OpenSet.Contains(Next))
                {
                    OpenSet.HeapPush(Next, CompareNodes);
                }
            }
        }
    }

    return false; // OpenSet is empty, destination is unreachable
}
