#pragma once

#include "raylib.h"
#include "raymath.h"
#include <vector>
#include <cmath>
#include <cstdint>

// Tile types classified from map image
enum MapTileType : uint8_t {
    TILE_GRASS = 0,
    TILE_ROAD,        // abu-abu (gray)
    TILE_BUILDING,    // putih (white)
    TILE_RIVER,       // biru (blue)
    TILE_COUNT
};

// Represents a 3D ray hit result
struct RayHitResult {
    bool hit;
    Vector3 point;
    Vector3 normal;
    float distance;
    MapTileType tileType;
};

// =========================================================================
// GPU Instanced Building Prototype
// A single mesh shape reused at many positions via DrawMeshInstanced()
// =========================================================================
struct BuildingPrototype {
    Mesh mesh;
    Material material;
    std::vector<Matrix> transforms; // Per-instance world transforms
    bool uploaded = false;
};

// Max number of distinct building prototypes (different w x d x h combos)
static const int MAX_BUILDING_PROTOTYPES = 16;

// Procedural 3D City generated via Ray Casting against the map image
class RaycastWorld {
public:
    RaycastWorld();
    ~RaycastWorld();

    // Generate 3D world by casting rays against map image
    bool GenerateFromImage(const char* imagePath, int downscaleFactor = 6);
    void Cleanup();

    // 3D Rendering
    void Draw3D(bool showLidarRays);

    // Coordinate conversions between grid and 3D world
    Vector3 GridToWorld(float gx, float gy, float yOffset = 0.0f) const;
    Vector2 WorldToGrid(Vector3 worldPos) const;

    // Grid queries
    bool IsValidGrid(int gx, int gy) const;
    MapTileType GetTile(int gx, int gy) const;
    bool IsWalkable(int gx, int gy) const;
    float GetBuildingHeight(int gx, int gy) const;

    // Ray casting queries in 3D
    RayHitResult CastRay3D(Ray ray, float maxDistance = 200.0f) const;
    // Cast lidar rays from an origin (e.g. car position)
    std::vector<RayHitResult> CastLidarRays(Vector3 origin, float headingAngleRad, int rayCount = 16, float maxDist = 20.0f) const;

    // Pick random connected road positions
    Vector2 GetRandomRoadPosition() const;
    bool AreConnected(Vector2 a, Vector2 b) const;

    // Dimensions
    int GetGridWidth() const { return m_gridWidth; }
    int GetGridHeight() const { return m_gridHeight; }
    float GetCellSize() const { return m_cellWorldSize; }
    Texture2D GetMapTexture() const { return m_mapTexture; }

private:
    int m_gridWidth = 0;
    int m_gridHeight = 0;
    float m_cellWorldSize = 0.5f;

    std::vector<MapTileType> m_grid;
    std::vector<float> m_heights;          // 3D heights for buildings
    std::vector<int> m_connectedComponent; // Component ID per cell
    std::vector<Vector2> m_mainRoadCells;  // Cells in the largest connected road network

    // 3D Models / Meshes generated procedurally (flat surfaces only)
    Model m_groundModel;
    Model m_roadModel;
    Model m_grassModel;
    Model m_riverModel;
    bool m_flatModelsLoaded = false;

    // GPU Instanced Building Prototypes (~15 unique shapes, thousands of instances)
    std::vector<BuildingPrototype> m_buildingPrototypes;
    bool m_buildingsLoaded = false;

    Texture2D m_mapTexture;
    bool m_textureLoaded = false;

    struct BuildingLot {
        int x, y, w, h;
        float height;
        float inset;
        bool hasPenthouse;
    };
    std::vector<BuildingLot> m_buildingLots;

    // Internal ray casting and classification helpers
    MapTileType ClassifyPixelColor(Color c) const;
    void AnalyzeRoadConnectivity();
    void PartitionBuildingLots();
    void BuildFlatSurfaceMeshes();
    void BuildInstancedBuildings();
};
