#pragma once

#include "raylib.h"
#include "raymath.h"
#include "raycast_world.hpp"
#include <vector>

enum AlgorithmType {
    ALGO_ASTAR = 0,
    ALGO_DIJKSTRA
};

struct PathResult {
    bool found = false;
    AlgorithmType algorithm = ALGO_ASTAR;
    std::vector<Vector2> gridPath;
    std::vector<Vector3> worldWaypoints;
    float totalDistanceWorld = 0.0f;
    int nodesExplored = 0;
    double executionTimeMs = 0.0;
};

class Pathfinder {
public:
    Pathfinder();
    ~Pathfinder();

    // Find shortest path using selected algorithm
    PathResult FindShortestPath(const RaycastWorld& world, Vector2 start, Vector2 goal, AlgorithmType algo);

    // Build 3D mesh model for path visualization
    Model BuildPathMeshModel(const RaycastWorld& world, const std::vector<Vector2>& path, float ribbonWidth = 0.5f, float elevationY = 0.12f);

    // Unload path model
    void UnloadPathModel(Model& model);

private:
    float HeuristicOctile(float x1, float y1, float x2, float y2) const;
};
