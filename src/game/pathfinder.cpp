#include "pathfinder.hpp"
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <cmath>
#include <chrono>
#include <algorithm>
#include <cstring>

struct PQNode {
    int x;
    int y;
    float gCost;
    float fCost;

    bool operator>(const PQNode& other) const {
        return fCost > other.fCost;
    }
};

struct CoordHash {
    size_t operator()(const std::pair<int, int>& p) const {
        return std::hash<long long>()(((long long)p.first << 32) | (unsigned int)p.second);
    }
};

Pathfinder::Pathfinder() {}

Pathfinder::~Pathfinder() {}

float Pathfinder::HeuristicOctile(float x1, float y1, float x2, float y2) const {
    float dx = fabsf(x2 - x1);
    float dy = fabsf(y2 - y1);
    return dx + dy + (1.41421356f - 2.0f) * fminf(dx, dy);
}

PathResult Pathfinder::FindShortestPath(const RaycastWorld& world, Vector2 start, Vector2 goal, AlgorithmType algo,
                                        const std::vector<Vector2>& blockedCells,
                                        const std::vector<Vector2>& trafficCells) {
    PathResult result;
    result.algorithm = algo;
    result.found = false;

    int sx = (int)roundf(start.x);
    int sy = (int)roundf(start.y);
    int gx = (int)roundf(goal.x);
    int gy = (int)roundf(goal.y);

    if (!world.IsValidGrid(sx, sy) || !world.IsValidGrid(gx, gy)) {
        return result;
    }
    if (!world.IsWalkable(sx, sy) || !world.IsWalkable(gx, gy)) {
        return result;
    }

    // Fast lookup sets for blocked cells and traffic congestion
    using PII = std::pair<int, int>;
    std::unordered_set<PII, CoordHash> blockedSet;
    for (const auto& b : blockedCells) {
        int bx = (int)roundf(b.x);
        int by = (int)roundf(b.y);
        if ((bx == sx && by == sy) || (bx == gx && by == gy)) continue; // don't block start/goal itself
        blockedSet.insert({bx, by});
    }

    std::unordered_set<PII, CoordHash> trafficSet;
    for (const auto& t : trafficCells) {
        trafficSet.insert({(int)roundf(t.x), (int)roundf(t.y)});
    }

    auto startTime = std::chrono::high_resolution_clock::now();

    std::priority_queue<PQNode, std::vector<PQNode>, std::greater<PQNode>> openList;
    std::unordered_map<PII, float, CoordHash> gScore;
    std::unordered_map<PII, PII, CoordHash> cameFrom;

    gScore[{sx, sy}] = 0.0f;
    float startH = (algo == ALGO_ASTAR) ? HeuristicOctile((float)sx, (float)sy, (float)gx, (float)gy) : 0.0f;
    openList.push({sx, sy, 0.0f, startH});

    const int dx[8] = {-1, 1, 0, 0, -1, 1, -1, 1};
    const int dy[8] = {0, 0, -1, 1, -1, -1, 1, 1};
    const float moveCost[8] = {1.0f, 1.0f, 1.0f, 1.0f, 1.41421356f, 1.41421356f, 1.41421356f, 1.41421356f};

    int nodesExplored = 0;
    int maxSearchNodes = world.GetGridWidth() * world.GetGridHeight();

    while (!openList.empty() && nodesExplored < maxSearchNodes) {
        PQNode current = openList.top();
        openList.pop();
        nodesExplored++;

        if (current.x == gx && current.y == gy) {
            // Path found! Reconstruct
            std::vector<Vector2> path;
            PII currCoord = {gx, gy};
            while (cameFrom.count(currCoord)) {
                path.push_back({(float)currCoord.first, (float)currCoord.second});
                currCoord = cameFrom[currCoord];
            }
            path.push_back({(float)sx, (float)sy});
            std::reverse(path.begin(), path.end());

            result.found = true;
            result.gridPath = path;

            // Compute world waypoints, distance, and traffic congestion
            result.worldWaypoints.clear();
            result.waypointInTraffic.clear();
            float totalDist = 0.0f;
            float trafficDist = 0.0f;

            for (size_t i = 0; i < path.size(); i++) {
                Vector3 wp = world.GridToWorld(path[i].x, path[i].y, 0.05f);
                result.worldWaypoints.push_back(wp);

                bool isTf = trafficSet.count({(int)roundf(path[i].x), (int)roundf(path[i].y)});
                result.waypointInTraffic.push_back(isTf);

                if (i > 0) {
                    float d = Vector3Distance(result.worldWaypoints[i - 1], wp);
                    totalDist += d;
                    if (isTf) {
                        trafficDist += d;
                    }
                }
            }
            result.totalDistanceWorld = totalDist;
            result.trafficDistanceWorld = trafficDist;
            // Realistic traffic delay: traffic distance unit ~ 0.85 minutes delay
            result.trafficDelayMinutes = trafficDist * 0.85f;
            break;
        }

        PII currCoord = {current.x, current.y};
        if (current.gCost > gScore[currCoord] + 0.001f) {
            continue; // stale entry in priority queue
        }

        for (int i = 0; i < 8; i++) {
            int nx = current.x + dx[i];
            int ny = current.y + dy[i];

            if (!world.IsValidGrid(nx, ny) || !world.IsWalkable(nx, ny)) {
                continue;
            }

            // Road Block / Constraint check (Tent / Closed road)
            if (blockedSet.count({nx, ny})) {
                continue;
            }

            // Diagonal corner-cutting check
            if (dx[i] != 0 && dy[i] != 0) {
                if (!world.IsWalkable(current.x + dx[i], current.y) ||
                    !world.IsWalkable(current.x, current.y + dy[i]) ||
                    blockedSet.count({current.x + dx[i], current.y}) ||
                    blockedSet.count({current.x, current.y + dy[i]})) {
                    continue; // do not cut through building or blocked corners
                }
            }

            // Traffic congestion penalty: +3.5x cost so pathfinder considers detours if available
            float costMult = trafficSet.count({nx, ny}) ? 3.5f : 1.0f;
            float newG = current.gCost + moveCost[i] * costMult;
            PII nextCoord = {nx, ny};

            if (!gScore.count(nextCoord) || newG < gScore[nextCoord] - 0.001f) {
                gScore[nextCoord] = newG;
                cameFrom[nextCoord] = currCoord;

                float fCost = newG;
                if (algo == ALGO_ASTAR) {
                    fCost += HeuristicOctile((float)nx, (float)ny, (float)gx, (float)gy);
                }
                openList.push({nx, ny, newG, fCost});
            }
        }
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed = endTime - startTime;

    result.nodesExplored = nodesExplored;
    result.executionTimeMs = elapsed.count();
    return result;
}

Model Pathfinder::BuildPathMeshModel(const RaycastWorld& world, const std::vector<Vector2>& path,
                                    Color baseColor, const std::vector<bool>& inTraffic,
                                    float ribbonWidth, float elevationY, bool isBorder,
                                    bool isBlockedStyle) {
    std::vector<float> verts, normals, uvs;
    std::vector<unsigned char> colors;

    if (path.size() < 2) {
        Mesh mesh = {0};
        return LoadModelFromMesh(mesh);
    }

    float halfW = ribbonWidth * 0.5f;

    for (size_t i = 0; i < path.size() - 1; i++) {
        Vector3 a = world.GridToWorld(path[i].x, path[i].y, elevationY);
        Vector3 b = world.GridToWorld(path[i + 1].x, path[i + 1].y, elevationY);

        Vector3 dir = Vector3Subtract(b, a);
        dir.y = 0.0f;
        float len = Vector3Length(dir);
        if (len < 0.001f) continue;
        dir = Vector3Scale(dir, 1.0f / len);

        Vector3 perp = {-dir.z, 0.0f, dir.x};
        perp = Vector3Scale(perp, halfW);

        Vector3 p0 = Vector3Add(a, perp);
        Vector3 p1 = Vector3Subtract(a, perp);
        Vector3 p2 = Vector3Subtract(b, perp);
        Vector3 p3 = Vector3Add(b, perp);

        // Segment coloring
        bool segTraffic = (i < inTraffic.size() && inTraffic[i]);
        Color segColor;
        if (isBlockedStyle) {
            // Hazard stripe pattern: alternating Red and Yellow/White every 2 segments
            bool stripe = ((i / 2) % 2 == 0);
            segColor = stripe ? (Color){230, 45, 45, 220} : (Color){255, 235, 70, 200};
        } else if (isBorder) {
            segColor = segTraffic ? (Color){180, 25, 25, 240} : (Color){255, 255, 255, 240};
        } else {
            segColor = segTraffic ? (Color){230, 40, 40, 255} : baseColor;
        }

        // Front face
        verts.push_back(p0.x); verts.push_back(p0.y); verts.push_back(p0.z);
        verts.push_back(p1.x); verts.push_back(p1.y); verts.push_back(p1.z);
        verts.push_back(p2.x); verts.push_back(p2.y); verts.push_back(p2.z);

        verts.push_back(p0.x); verts.push_back(p0.y); verts.push_back(p0.z);
        verts.push_back(p2.x); verts.push_back(p2.y); verts.push_back(p2.z);
        verts.push_back(p3.x); verts.push_back(p3.y); verts.push_back(p3.z);

        // Back face (reverse winding)
        verts.push_back(p0.x); verts.push_back(p0.y); verts.push_back(p0.z);
        verts.push_back(p2.x); verts.push_back(p2.y); verts.push_back(p2.z);
        verts.push_back(p1.x); verts.push_back(p1.y); verts.push_back(p1.z);

        verts.push_back(p0.x); verts.push_back(p0.y); verts.push_back(p0.z);
        verts.push_back(p3.x); verts.push_back(p3.y); verts.push_back(p3.z);
        verts.push_back(p2.x); verts.push_back(p2.y); verts.push_back(p2.z);

        for (int k = 0; k < 12; k++) {
            normals.push_back(0.0f); normals.push_back(1.0f); normals.push_back(0.0f);
            uvs.push_back(0.0f); uvs.push_back(0.0f);
            colors.push_back(segColor.r);
            colors.push_back(segColor.g);
            colors.push_back(segColor.b);
            colors.push_back(segColor.a);
        }
    }

    Mesh mesh = {0};
    if (verts.empty()) return LoadModelFromMesh(mesh);

    mesh.triangleCount = (int)(verts.size() / 9);
    mesh.vertexCount = (int)(verts.size() / 3);

    mesh.vertices = (float*)MemAlloc(sizeof(float) * verts.size());
    mesh.normals = (float*)MemAlloc(sizeof(float) * normals.size());
    mesh.texcoords = (float*)MemAlloc(sizeof(float) * uvs.size());
    mesh.colors = (unsigned char*)MemAlloc(sizeof(unsigned char) * colors.size());

    memcpy(mesh.vertices, verts.data(), sizeof(float) * verts.size());
    memcpy(mesh.normals, normals.data(), sizeof(float) * normals.size());
    memcpy(mesh.texcoords, uvs.data(), sizeof(float) * uvs.size());
    memcpy(mesh.colors, colors.data(), sizeof(unsigned char) * colors.size());

    UploadMesh(&mesh, false);
    return LoadModelFromMesh(mesh);
}

void Pathfinder::UnloadPathModel(Model& model) {
    if (model.meshCount > 0) {
        UnloadModel(model);
        memset(&model, 0, sizeof(Model));
    }
}
