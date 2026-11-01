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
    std::vector<bool> waypointInTraffic;    // Per-waypoint traffic flag (true = red zone)
    float totalDistanceWorld = 0.0f;
    float trafficDistanceWorld = 0.0f;      // Total distance through traffic congestion
    float trafficDelayMinutes = 0.0f;       // Simulated additional delay in minutes
    int nodesExplored = 0;
    double executionTimeMs = 0.0;
};

class Pathfinder {
public:
    Pathfinder();
    ~Pathfinder();

    // Find shortest path considering blocked cells (road closures) and traffic zones
    PathResult FindShortestPath(const RaycastWorld& world, Vector2 start, Vector2 goal, AlgorithmType algo,
                                const std::vector<Vector2>& blockedCells = {},
                                const std::vector<Vector2>& trafficCells = {});

    // Build 3D mesh model with vertex coloring for traffic segments (Red = traffic, baseColor = clear)
    // When isBlockedStyle is true, renders an alternating hazard-striped ribbon (original blocked route)
    Model BuildPathMeshModel(const RaycastWorld& world, const std::vector<Vector2>& path,
                             Color baseColor = WHITE,
                             const std::vector<bool>& inTraffic = {},
                             float ribbonWidth = 0.5f, float elevationY = 0.12f, bool isBorder = false,
                             bool isBlockedStyle = false);

    // Unload path model
    void UnloadPathModel(Model& model);

private:
    float HeuristicOctile(float x1, float y1, float x2, float y2) const;
};
