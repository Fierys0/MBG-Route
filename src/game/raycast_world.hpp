#pragma once

#include "raylib.h"
#include "raymath.h"
#include <vector>
#include <cmath>
#include <cstdint>

// Tile types classified from map image
enum MapTileType : uint8_t {
    TILE_GRASS = 0,
    TILE_ROAD,        // jalan: #cdd1dc (abu-abu)
    TILE_BUILDING,    // gedung: #f7f3f2 (putih)
    TILE_RIVER,       // sungai: biru (#4299e1)
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

// Procedural 3D City generated via Ray Casting against the map image
class RaycastWorld {
public:
    RaycastWorld();
    ~RaycastWorld();

    // Generate 3D world by casting rays against map image
    bool GenerateFromImage(const char* imagePath, int downscaleFactor = 6);
    void Cleanup();

    // 3D Rendering with baked vertex ambient occlusion & directional lighting
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

    // =========================================================================
    // Procedural 3D Models with Maximum Optimization:
    // 1. Ground Plane: neutral terrain floor
    // 2. Road Model: road network (#cdd1dc)
    // 3. River Model: water blue (#4299e1)
    // 4. Ground Contact AO: soft ambient occlusion shadow skirts under buildings
    // 5. Building Model: 3D structures with baked vertex ambient occlusion
    // =========================================================================
    Model m_groundModel;
    Model m_roadModel;
    Model m_riverModel;
    Model m_shadowModel;
    Model m_buildingModel;
    bool m_modelsLoaded = false;

    Texture2D m_mapTexture;
    bool m_textureLoaded = false;

    struct BuildingLot {
        int x, y, w, h;
        float height;
        float inset;
        int style;          // Architectural archetype style (1-12)
        bool hasPenthouse;
    };
    std::vector<BuildingLot> m_buildingLots;

    // Internal ray casting and classification helpers
    MapTileType ClassifyPixelColor(Color c) const;
    void AnalyzeRoadConnectivity();
    void PartitionBuildingLots();
    void BuildProceduralMeshes();
};
