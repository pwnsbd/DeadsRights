#include "MazeNavigator.h"
#include "../Maze/Maze.h"
#include "../Conversion/CubeToSphere.h"
#include "Kismet/KismetMathLibrary.h"

// // initializes the data and visuals
// void UMazeNavigator::Init(UMaze *InMaze, ACubeToSphere *InSphere)
// {
//     Maze = InMaze;
//     Sphere = InSphere;
// }

// brute forces to find the closest cell (optimize later using math instead of loops)
FMazeNode UMazeNavigator::WorldToNode(FVector WorldPos) const
{
    if (!Sphere || !Maze)
        return FMazeNode(0, 0, 0);

    //     float BestDistSq = FLT_MAX;
    //     FMazeNode BestNode(0, 0, 0);

    // finds distance of every cell
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

// uses the helper function from maze to get the neighbors of the node that are open and traversable (not walls)
TArray<FMazeNode> UMazeNavigator::GetNeighbors(const FMazeNode &Node) const
{
    // get neighbors of current node
    return Maze->GetTraversableNeighbors(Node);
}

// bool UMazeNavigator::FindPath(FVector StartPos, FVector EndPos, TArray<FVector> &OutPath)
// {
//     if (!Maze || !Sphere)
//         return false;

// convert world positions to grid nodes
FMazeNode StartNode = WorldToNode(StartPos);
FMazeNode EndNode = WorldToNode(EndPos);

// --- ADD THESE LOGS HERE ---
UE_LOG(LogTemp, Warning, TEXT("Inside FindPath - StartNode -> Face: %d | X: %d | Y: %d"), StartNode.Face, StartNode.X, StartNode.Y);
UE_LOG(LogTemp, Warning, TEXT("Inside FindPath - EndNode   -> Face: %d | X: %d | Y: %d"), EndNode.Face, EndNode.X, EndNode.Y);
// ---------------------------

//     if (StartNode == EndNode)
//         return false; // already at destination

// initialize A* data structures
TArray<FMazeNode> OpenSet;

//     // initialize A* data structures: estimated cost
//     TMap<FMazeNode, float> CostSoFar;
//     CostSoFar.Add(StartNode, 0.0f);

//     // initialize A* data structures: parent map
//     TMap<FMazeNode, FMazeNode> CameFrom;

// initialize starting node
OpenSet.HeapPush(StartNode, [&](const FMazeNode &A, const FMazeNode &B)
                 { 
        float CostA = CostSoFar.Contains(A) ? CostSoFar[A] + FVector::Dist(Sphere->GetCellCenterWorld(A.Face, A.X, A.Y), EndPos) : FLT_MAX;
        float CostB = CostSoFar.Contains(B) ? CostSoFar[B] + FVector::Dist(Sphere->GetCellCenterWorld(B.Face, B.X, B.Y), EndPos) : FLT_MAX;
        return CostA < CostB; });

// A* main loop
while (OpenSet.Num() > 0)
{
    // get node with lowest estimated total cost
    FMazeNode CurrentNode;
    OpenSet.HeapPop(CurrentNode, [&](const FMazeNode &A, const FMazeNode &B)
                    {
            float CostA = CostSoFar.Contains(A) ? CostSoFar[A] + FVector::Dist(Sphere->GetCellCenterWorld(A.Face, A.X, A.Y), EndPos) : FLT_MAX;
            float CostB = CostSoFar.Contains(B) ? CostSoFar[B] + FVector::Dist(Sphere->GetCellCenterWorld(B.Face, B.X, B.Y), EndPos) : FLT_MAX;
            return CostA < CostB; });

    // base case: destination reached
    if (CurrentNode == EndNode)
    {
        // build path by backtracking through CameFrom
        FMazeNode step = EndNode;
        while (!(step == StartNode))
        {
            OutPath.Add(Sphere->GetCellCenterWorld(step.Face, step.X, step.Y));
            if (CameFrom.Contains(step))
            {
                step = CameFrom[step];
            }
            else
                break; // prevent infinite loop
        }

        //             // add start and reverse path
        //             OutPath.Add(Sphere->GetCellCenterWorld(StartNode.Face, StartNode.X, StartNode.Y));
        //             Algo::Reverse(OutPath);
        //             return true;
        //         }

        // get neighbors of current node
        TArray<FMazeNode> Neighbors = GetNeighbors(CurrentNode);

        if (CurrentNode == StartNode)
        {
            UE_LOG(LogTemp, Warning, TEXT("Start Node has %d traversable neighbors."), Neighbors.Num());
        }

        //         // explore neighbors
        //         for (const FMazeNode &Next : Neighbors)
        //         {
        //             float newCost = CostSoFar[CurrentNode] + 1.0f; // assuming uniform cost for moving to a neighbor

        //             if (!CostSoFar.Contains(Next) || newCost < CostSoFar[Next])
        //             {
        //                 CostSoFar.Add(Next, newCost);
        //                 CameFrom.Add(Next, CurrentNode);

        // add to the open set if not already there
        if (!OpenSet.Contains(Next))
        {
            OpenSet.HeapPush(Next, [&](const FMazeNode &A, const FMazeNode &B)
                             {
                        float CostA = CostSoFar[A] + FVector::Dist(Sphere->GetCellCenterWorld(A.Face, A.X, A.Y), EndPos);
                        float CostB = CostSoFar[B] + FVector::Dist(Sphere->GetCellCenterWorld(B.Face, B.X, B.Y), EndPos);
                        return CostA < CostB; });
        }
    }
}
}

//     return false; // no path found
// }