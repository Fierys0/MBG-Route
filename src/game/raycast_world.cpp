#include "raycast_world.hpp"
#include "fumbo.hpp"
#include "rlgl.h"
#include <queue>
#include <algorithm>
#include <cstring>
#include <cmath>

static const float GROUND_Y = 0.0f;
static const float SHADOW_Y = 0.01f; // Just above ground for contact AO
static const float ROAD_Y   = 0.02f;
static const float RIVER_Y  = 0.015f;

// User specified color references
// jalan = #cdd1dc (205, 209, 220)
// gedung = #f7f3f2 (247, 243, 242)
static const Color BLD_BASE_COLOR = {247, 243, 242, 255};
static const Color ROAD_COLOR     = {205, 209, 220, 255};
static const Color RIVER_COLOR    = {66, 153, 225, 255};
static const Color GROUND_COLOR   = {226, 228, 235, 255};

// Helper: Calculate shaded color combining directional sunlight and ambient occlusion
static inline Color CalcShadedColor(Color base, float lightFactor, float aoFactor) {
    float totalFactor = lightFactor * aoFactor;
    return (Color){
        (unsigned char)std::clamp((int)(base.r * totalFactor), 0, 255),
        (unsigned char)std::clamp((int)(base.g * totalFactor), 0, 255),
        (unsigned char)std::clamp((int)(base.b * totalFactor), 0, 255),
        255
    };
}

// Push a double-sided quad with per-vertex colors for Ambient Occlusion
static void PushQuadAO(std::vector<float>& verts, std::vector<float>& normals, std::vector<float>& uvs,
                       std::vector<unsigned char>& colors,
                       Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3,
                       Vector3 normal,
                       Color c0, Color c1, Color c2, Color c3) {
    // Triangle 1 (p0, p1, p2)
    verts.push_back(p0.x); verts.push_back(p0.y); verts.push_back(p0.z);
    verts.push_back(p1.x); verts.push_back(p1.y); verts.push_back(p1.z);
    verts.push_back(p2.x); verts.push_back(p2.y); verts.push_back(p2.z);

    colors.push_back(c0.r); colors.push_back(c0.g); colors.push_back(c0.b); colors.push_back(c0.a);
    colors.push_back(c1.r); colors.push_back(c1.g); colors.push_back(c1.b); colors.push_back(c1.a);
    colors.push_back(c2.r); colors.push_back(c2.g); colors.push_back(c2.b); colors.push_back(c2.a);

    // Triangle 2 (p0, p2, p3)
    verts.push_back(p0.x); verts.push_back(p0.y); verts.push_back(p0.z);
    verts.push_back(p2.x); verts.push_back(p2.y); verts.push_back(p2.z);
    verts.push_back(p3.x); verts.push_back(p3.y); verts.push_back(p3.z);

    colors.push_back(c0.r); colors.push_back(c0.g); colors.push_back(c0.b); colors.push_back(c0.a);
    colors.push_back(c2.r); colors.push_back(c2.g); colors.push_back(c2.b); colors.push_back(c2.a);
    colors.push_back(c3.r); colors.push_back(c3.g); colors.push_back(c3.b); colors.push_back(c3.a);

    // Back face (reverse winding for 100% two-sided robustness)
    verts.push_back(p0.x); verts.push_back(p0.y); verts.push_back(p0.z);
    verts.push_back(p2.x); verts.push_back(p2.y); verts.push_back(p2.z);
    verts.push_back(p1.x); verts.push_back(p1.y); verts.push_back(p1.z);

    colors.push_back(c0.r); colors.push_back(c0.g); colors.push_back(c0.b); colors.push_back(c0.a);
    colors.push_back(c2.r); colors.push_back(c2.g); colors.push_back(c2.b); colors.push_back(c2.a);
    colors.push_back(c1.r); colors.push_back(c1.g); colors.push_back(c1.b); colors.push_back(c1.a);

    verts.push_back(p0.x); verts.push_back(p0.y); verts.push_back(p0.z);
    verts.push_back(p3.x); verts.push_back(p3.y); verts.push_back(p3.z);
    verts.push_back(p2.x); verts.push_back(p2.y); verts.push_back(p2.z);

    colors.push_back(c0.r); colors.push_back(c0.g); colors.push_back(c0.b); colors.push_back(c0.a);
    colors.push_back(c3.r); colors.push_back(c3.g); colors.push_back(c3.b); colors.push_back(c3.a);
    colors.push_back(c2.r); colors.push_back(c2.g); colors.push_back(c2.b); colors.push_back(c2.a);

    for (int i = 0; i < 12; i++) {
        normals.push_back(normal.x);
        normals.push_back(normal.y);
        normals.push_back(normal.z);
        uvs.push_back(0.0f);
        uvs.push_back(0.0f);
    }
}

// Push a standard double-sided quad without per-vertex colors
static void PushQuad(std::vector<float>& verts, std::vector<float>& normals, std::vector<float>& uvs,
                     Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, Vector3 normal) {
    verts.push_back(p0.x); verts.push_back(p0.y); verts.push_back(p0.z);
    verts.push_back(p1.x); verts.push_back(p1.y); verts.push_back(p1.z);
    verts.push_back(p2.x); verts.push_back(p2.y); verts.push_back(p2.z);

    verts.push_back(p0.x); verts.push_back(p0.y); verts.push_back(p0.z);
    verts.push_back(p2.x); verts.push_back(p2.y); verts.push_back(p2.z);
    verts.push_back(p3.x); verts.push_back(p3.y); verts.push_back(p3.z);

    verts.push_back(p0.x); verts.push_back(p0.y); verts.push_back(p0.z);
    verts.push_back(p2.x); verts.push_back(p2.y); verts.push_back(p2.z);
    verts.push_back(p1.x); verts.push_back(p1.y); verts.push_back(p1.z);

    verts.push_back(p0.x); verts.push_back(p0.y); verts.push_back(p0.z);
    verts.push_back(p3.x); verts.push_back(p3.y); verts.push_back(p3.z);
    verts.push_back(p2.x); verts.push_back(p2.y); verts.push_back(p2.z);

    for (int i = 0; i < 12; i++) {
        normals.push_back(normal.x);
        normals.push_back(normal.y);
        normals.push_back(normal.z);
        uvs.push_back(0.0f);
        uvs.push_back(0.0f);
    }
}

static Model CreateModelFromBuffers(const std::vector<float>& verts,
                                    const std::vector<float>& normals,
                                    const std::vector<float>& uvs,
                                    const std::vector<unsigned char>& colors = {}) {
    Mesh mesh = {0};
    if (verts.empty()) return LoadModelFromMesh(mesh);

    mesh.triangleCount = (int)(verts.size() / 9);
    mesh.vertexCount = (int)(verts.size() / 3);

    mesh.vertices = (float*)MemAlloc(sizeof(float) * verts.size());
    mesh.normals = (float*)MemAlloc(sizeof(float) * normals.size());
    mesh.texcoords = (float*)MemAlloc(sizeof(float) * uvs.size());

    memcpy(mesh.vertices, verts.data(), sizeof(float) * verts.size());
    memcpy(mesh.normals, normals.data(), sizeof(float) * normals.size());
    memcpy(mesh.texcoords, uvs.data(), sizeof(float) * uvs.size());

    if (!colors.empty()) {
        mesh.colors = (unsigned char*)MemAlloc(sizeof(unsigned char) * colors.size());
        memcpy(mesh.colors, colors.data(), sizeof(unsigned char) * colors.size());
    }

    UploadMesh(&mesh, false);
    return LoadModelFromMesh(mesh);
}

RaycastWorld::RaycastWorld() {
    memset(&m_groundModel, 0, sizeof(Model));
    memset(&m_roadModel, 0, sizeof(Model));
    memset(&m_riverModel, 0, sizeof(Model));
    memset(&m_shadowModel, 0, sizeof(Model));
    memset(&m_buildingModel, 0, sizeof(Model));
    memset(&m_mapTexture, 0, sizeof(Texture2D));
}

RaycastWorld::~RaycastWorld() {
    Cleanup();
}

void RaycastWorld::Cleanup() {
    if (m_modelsLoaded) {
        if (m_groundModel.meshCount > 0) UnloadModel(m_groundModel);
        if (m_roadModel.meshCount > 0) UnloadModel(m_roadModel);
        if (m_riverModel.meshCount > 0) UnloadModel(m_riverModel);
        if (m_shadowModel.meshCount > 0) UnloadModel(m_shadowModel);
        if (m_buildingModel.meshCount > 0) UnloadModel(m_buildingModel);
        m_modelsLoaded = false;
    }
    if (m_textureLoaded) {
        UnloadTexture(m_mapTexture);
        m_textureLoaded = false;
    }
    m_grid.clear();
    m_heights.clear();
    m_connectedComponent.clear();
    m_mainRoadCells.clear();
    m_buildingLots.clear();
}

// Precise color classifier based on user's color definitions:
// jalan = #cdd1dc (205, 209, 220) atau sekitarnya
// gedung = #f7f3f2 (247, 243, 242) atau sekitarnya
// sungai = #c2dde6 (182, 218, 234)
MapTileType RaycastWorld::ClassifyPixelColor(Color c) const {
    float r = (float)c.r;
    float g = (float)c.g;
    float b = (float)c.b;

    // 1. Sungai berwarna biru (#c2dde6 / dominant blue)
    if (b > 195.0f && (b - r > 18.0f) && (g - r > 8.0f)) {
        return TILE_RIVER;
    }

    // 2. Distance to exact road color #cdd1dc (205, 209, 220)
    float dr = r - 205.0f;
    float dg = g - 209.0f;
    float db = b - 220.0f;
    float distRoad = sqrtf(dr * dr + dg * dg + db * db);

    float meanB = (r + g + b) / 3.0f;
    float maxC = fmaxf(fmaxf(r, g), b);
    float minC = fminf(fminf(r, g), b);
    float diff = maxC - minC;

    // Road check: close to #cdd1dc OR road grey tones (between 90 and 228 brightness, low saturation)
    if (distRoad < 42.0f || (meanB >= 90.0f && meanB < 228.0f && diff < 26.0f)) {
        return TILE_ROAD;
    }

    // Default: Gedung / Bangunan (#f7f3f2 / white city blocks)
    return TILE_BUILDING;
}

bool RaycastWorld::GenerateFromImage(const char* imagePath, int downscaleFactor) {
    Cleanup();

    Image img = Fumbo::Assets::LoadImage(imagePath);
    if (img.data == nullptr) {
        img = ::LoadImage(imagePath);
    }
    if (img.data == nullptr) {
        TraceLog(LOG_ERROR, "Failed to load map image: %s", imagePath);
        return false;
    }

    m_mapTexture = LoadTextureFromImage(img);
    m_textureLoaded = true;

    m_gridWidth = img.width / downscaleFactor;
    m_gridHeight = img.height / downscaleFactor;
    m_cellWorldSize = 1.0f;

    int totalCells = m_gridWidth * m_gridHeight;
    m_grid.resize(totalCells, TILE_BUILDING);
    m_heights.resize(totalCells, 0.0f);

    Color* pixels = LoadImageColors(img);

    // =========================================================================
    // Ray Casting Downward:
    // Cast vertical sampling rays down through the 2D image plane to detect
    // tile classification and compute 3D procedural building heights
    // =========================================================================
    for (int gy = 0; gy < m_gridHeight; gy++) {
        for (int gx = 0; gx < m_gridWidth; gx++) {
            int counts[TILE_COUNT] = {0};

            for (int dy = 0; dy < downscaleFactor; dy++) {
                for (int dx = 0; dx < downscaleFactor; dx++) {
                    int px = gx * downscaleFactor + dx;
                    int py = gy * downscaleFactor + dy;
                    if (px < img.width && py < img.height) {
                        Color c = pixels[py * img.width + px];
                        MapTileType t = ClassifyPixelColor(c);
                        counts[t]++;
                    }
                }
            }

            // Road sensitivity: preserve road connectivity
            MapTileType bestType = TILE_BUILDING;
            if (counts[TILE_RIVER] >= (downscaleFactor * downscaleFactor) / 3) {
                bestType = TILE_RIVER;
            } else if (counts[TILE_ROAD] >= (downscaleFactor * downscaleFactor) / 4) {
                bestType = TILE_ROAD;
            } else {
                bestType = TILE_BUILDING;
            }

            int idx = gy * m_gridWidth + gx;
            m_grid[idx] = bestType;
            m_heights[idx] = 0.0f;
        }
    }

    UnloadImageColors(pixels);
    UnloadImage(img);

    // Analyze road connectivity to ensure shortest path algorithms always succeed
    AnalyzeRoadConnectivity();

    // Group adjacent building cells into varied lots (different widths and heights)
    PartitionBuildingLots();

    // Procedurally generate 3D meshes with baked ambient occlusion
    BuildProceduralMeshes();

    TraceLog(LOG_INFO, "3D World built: %dx%d cells, %d roads, %d building lots",
             m_gridWidth, m_gridHeight, (int)m_mainRoadCells.size(), (int)m_buildingLots.size());
    return true;
}

void RaycastWorld::AnalyzeRoadConnectivity() {
    int totalCells = m_gridWidth * m_gridHeight;
    m_connectedComponent.assign(totalCells, -1);
    m_mainRoadCells.clear();

    std::vector<std::vector<Vector2>> components;
    int compId = 0;

    for (int y = 0; y < m_gridHeight; y++) {
        for (int x = 0; x < m_gridWidth; x++) {
            int idx = y * m_gridWidth + x;
            if (m_grid[idx] == TILE_ROAD && m_connectedComponent[idx] == -1) {
                std::vector<Vector2> currentComp;
                std::queue<std::pair<int, int>> q;

                q.push({x, y});
                m_connectedComponent[idx] = compId;

                while (!q.empty()) {
                    auto [cx, cy] = q.front();
                    q.pop();
                    currentComp.push_back({(float)cx, (float)cy});

                    const int dx[] = {-1, 1, 0, 0, -1, 1, -1, 1};
                    const int dy[] = {0, 0, -1, 1, -1, -1, 1, 1};

                    for (int d = 0; d < 8; d++) {
                        int nx = cx + dx[d];
                        int ny = cy + dy[d];
                        if (IsValidGrid(nx, ny)) {
                            int nIdx = ny * m_gridWidth + nx;
                            if (m_grid[nIdx] == TILE_ROAD && m_connectedComponent[nIdx] == -1) {
                                m_connectedComponent[nIdx] = compId;
                                q.push({nx, ny});
                            }
                        }
                    }
                }

                components.push_back(currentComp);
                compId++;
            }
        }
    }

    // Find the largest connected road network
    size_t maxRoads = 0;
    int bestComp = -1;
    for (size_t i = 0; i < components.size(); i++) {
        if (components[i].size() > maxRoads) {
            maxRoads = components[i].size();
            bestComp = (int)i;
        }
    }

    if (bestComp != -1) {
        m_mainRoadCells = components[bestComp];
    }
}

void RaycastWorld::PartitionBuildingLots() {
    m_buildingLots.clear();
    std::vector<bool> assigned(m_gridWidth * m_gridHeight, false);

    // Group adjacent building cells into diverse lots (different widths & depths: 4x3, 3x3, 3x2, 2x3, 2x2, 2x1, 1x2, 1x1)
    // Larger footprints represent realistic city compounds, commercial plazas, and skyscrapers
    for (int y = 0; y < m_gridHeight; y++) {
        for (int x = 0; x < m_gridWidth; x++) {
            int idx = y * m_gridWidth + x;
            if (m_grid[idx] != TILE_BUILDING || assigned[idx]) {
                continue;
            }

            unsigned int seed = (unsigned int)(x * 73856093 ^ y * 19349663);
            int pref = seed % 100;

            const int candidates[8][2] = {
                {4, 3}, {3, 3}, {3, 2}, {2, 3}, {2, 2}, {2, 1}, {1, 2}, {1, 1}
            };
            int candStart = (pref < 12) ? 0 : (pref < 25) ? 2 : (pref < 55) ? 4 : (pref < 80) ? 5 : 7;

            int bestW = 1, bestH = 1;
            for (int c = candStart; c < 8; c++) {
                int lw = candidates[c][0];
                int lh = candidates[c][1];
                if (x + lw <= m_gridWidth && y + lh <= m_gridHeight) {
                    bool canFit = true;
                    for (int dy = 0; dy < lh && canFit; dy++) {
                        for (int dx = 0; dx < lw; dx++) {
                            int nIdx = (y + dy) * m_gridWidth + (x + dx);
                            if (m_grid[nIdx] != TILE_BUILDING || assigned[nIdx]) {
                                canFit = false;
                                break;
                            }
                        }
                    }
                    if (canFit) {
                        bestW = lw;
                        bestH = lh;
                        break;
                    }
                }
            }

            for (int dy = 0; dy < bestH; dy++) {
                for (int dx = 0; dx < bestW; dx++) {
                    assigned[(y + dy) * m_gridWidth + (x + dx)] = true;
                }
            }

            // Procedural diverse height based on district zone and lot size
            // Creates 12-15 distinct architectural building categories
            float zone = (sinf((float)x * 0.045f) * cosf((float)y * 0.045f) + 1.0f) * 0.5f;
            float rnd = (float)(seed % 1000) / 1000.0f;
            float height = 0.8f;
            int style = 0;
            bool hasPenthouse = false;

            int area = bestW * bestH;
            if (area >= 9) {
                // Grand Commercial Plazas / Supertall Headquarters (Category 1, 2, 3)
                if (zone > 0.65f) {
                    height = 4.8f + rnd * 2.4f; // 4.8m - 7.2m Landmark Skyscraper
                    style = 1;
                    hasPenthouse = true;
                } else {
                    height = 1.4f + rnd * 1.2f; // Shopping Mall / Exhibition Center
                    style = 2;
                }
            } else if (area >= 6) {
                // Wide Corporate Towers / Large Office Blocks (Category 4, 5, 6)
                if (zone > 0.60f) {
                    height = 3.8f + rnd * 2.2f; // 3.8m - 6.0m Office Tower
                    style = 3;
                    hasPenthouse = true;
                } else if (zone > 0.35f) {
                    height = 2.4f + rnd * 1.4f; // Mid-rise Business Complex
                    style = 4;
                    if (height > 3.2f) hasPenthouse = true;
                } else {
                    height = 1.2f + rnd * 0.8f; // Large Residential / School Compound
                    style = 5;
                }
            } else if (area >= 4) {
                // Medium High-Rise / Apartment Blocks (Category 7, 8, 9)
                if (zone > 0.70f) {
                    height = 3.2f + rnd * 1.8f; // 3.2m - 5.0m Apartment Tower
                    style = 6;
                    hasPenthouse = true;
                } else if (zone > 0.40f) {
                    height = 2.0f + rnd * 1.2f; // 4-story Commercial Building
                    style = 7;
                } else {
                    height = 1.1f + rnd * 0.7f; // Low-rise Condominium
                    style = 8;
                }
            } else if (area >= 2) {
                // Street Commercial / Two-story Shopfronts (Category 10, 11, 12)
                if (zone > 0.75f && rnd > 0.65f) {
                    height = 2.8f + rnd * 1.4f; // Slender Mid-rise Tower
                    style = 9;
                    hasPenthouse = true;
                } else {
                    height = 1.2f + rnd * 1.0f; // 2-3 Story Shophouse (Ruko)
                    style = 10;
                }
            } else {
                // 1x1 Compact Lots: Suburban Houses & Neighborhood Stores (Category 13, 14, 15)
                if (rnd > 0.92f && zone > 0.75f) {
                    height = 3.0f + rnd * 1.5f; // Slender Modern Spire
                    style = 11;
                    hasPenthouse = true;
                } else if (rnd > 0.50f) {
                    height = 1.1f + rnd * 0.6f; // 2-Story Residential Villa
                    style = 12;
                } else {
                    height = 0.75f + rnd * 0.5f; // Single-Story House
                    style = 13;
                }
            }

            // Setback margin ensuring buildings NEVER clip into roads or vehicles
            float inset = m_cellWorldSize * (0.14f + 0.05f * rnd);

            BuildingLot lot;
            lot.x = x;
            lot.y = y;
            lot.w = bestW;
            lot.h = bestH;
            lot.height = height;
            lot.inset = inset;
            lot.style = style;
            lot.hasPenthouse = hasPenthouse;

            m_buildingLots.push_back(lot);

            // Record height for all cells in lot
            for (int dy = 0; dy < bestH; dy++) {
                for (int dx = 0; dx < bestW; dx++) {
                    m_heights[(y + dy) * m_gridWidth + (x + dx)] = height;
                }
            }
        }
    }
}

// =========================================================================
// BuildProceduralMeshes:
//   1. Ground Model: Solid terrain base (Ground color #e2e4ea)
//   2. Road Model: Distinct road network (#cdd1dc)
//   3. River Model: Water blue (#4299e1)
//   4. Shadow Model: Soft ground contact Ambient Occlusion under buildings
//   5. Building Model: 3D buildings with baked vertex Ambient Occlusion!
// =========================================================================
void RaycastWorld::BuildProceduralMeshes() {
    float s = m_cellWorldSize;
    float totalW = m_gridWidth * s;
    float totalH = m_gridHeight * s;
    float halfW = totalW * 0.5f;
    float halfH = totalH * 0.5f;

    // =====================================================================
    // 1. Solid Ground Plane across the entire world (no sky holes!)
    // =====================================================================
    {
        std::vector<float> gndV, gndN, gndU;
        float pad = 20.0f;
        Vector3 gp0 = {-halfW - pad, GROUND_Y, -halfH - pad};
        Vector3 gp1 = { halfW + pad, GROUND_Y, -halfH - pad};
        Vector3 gp2 = { halfW + pad, GROUND_Y,  halfH + pad};
        Vector3 gp3 = {-halfW - pad, GROUND_Y,  halfH + pad};
        PushQuad(gndV, gndN, gndU, gp0, gp1, gp2, gp3, {0, 1, 0});
        m_groundModel = CreateModelFromBuffers(gndV, gndN, gndU);
    }

    // =====================================================================
    // 2. Procedural Road and River Surfaces
    // =====================================================================
    {
        std::vector<float> roadV, roadN, roadU;
        std::vector<float> riverV, riverN, riverU;

        for (int y = 0; y < m_gridHeight; y++) {
            for (int x = 0; x < m_gridWidth; x++) {
                MapTileType type = GetTile(x, y);
                float wx = (x - m_gridWidth / 2.0f) * s;
                float wz = (y - m_gridHeight / 2.0f) * s;

                if (type == TILE_ROAD) {
                    Vector3 p0 = {wx,     ROAD_Y, wz};
                    Vector3 p1 = {wx + s, ROAD_Y, wz};
                    Vector3 p2 = {wx + s, ROAD_Y, wz + s};
                    Vector3 p3 = {wx,     ROAD_Y, wz + s};
                    PushQuad(roadV, roadN, roadU, p0, p1, p2, p3, {0, 1, 0});
                } else if (type == TILE_RIVER) {
                    Vector3 p0 = {wx,     RIVER_Y, wz};
                    Vector3 p1 = {wx + s, RIVER_Y, wz};
                    Vector3 p2 = {wx + s, RIVER_Y, wz + s};
                    Vector3 p3 = {wx,     RIVER_Y, wz + s};
                    PushQuad(riverV, riverN, riverU, p0, p1, p2, p3, {0, 1, 0});
                }
            }
        }

        m_roadModel = CreateModelFromBuffers(roadV, roadN, roadU);
        m_riverModel = CreateModelFromBuffers(riverV, riverN, riverU);
    }

    // =====================================================================
    // 3. Ground Contact Ambient Occlusion (Soft Drop Shadows under buildings)
    //    Darkens the ground right around building footprints with smooth falloff
    // =====================================================================
    {
        std::vector<float> shdV, shdN, shdU;
        std::vector<unsigned char> shdC;

        for (const auto& lot : m_buildingLots) {
            float wx0 = (lot.x - m_gridWidth / 2.0f) * s + lot.inset;
            float wz0 = (lot.y - m_gridHeight / 2.0f) * s + lot.inset;
            float wx1 = (lot.x + lot.w - m_gridWidth / 2.0f) * s - lot.inset;
            float wz1 = (lot.y + lot.h - m_gridHeight / 2.0f) * s - lot.inset;

            float padAO = 0.22f * s; // Soft AO shadow margin
            float sx0 = wx0 - padAO;
            float sz0 = wz0 - padAO;
            float sx1 = wx1 + padAO;
            float sz1 = wz1 + padAO;

            // Soft contact shadow color
            Color cShadow = {40, 45, 60, 130};
            PushQuadAO(shdV, shdN, shdU, shdC,
                       {sx0, SHADOW_Y, sz0}, {sx1, SHADOW_Y, sz0},
                       {sx1, SHADOW_Y, sz1}, {sx0, SHADOW_Y, sz1},
                       {0, 1, 0},
                       cShadow, cShadow, cShadow, cShadow);
        }

        m_shadowModel = CreateModelFromBuffers(shdV, shdN, shdU, shdC);
    }

    // =====================================================================
    // 4. Procedural 3D Buildings with Baked Vertex Ambient Occlusion!
    //    - Vertical gradient: Dark contact AO at street level -> bright sky top
    //    - Directional sunlight shading: South/East sunlit, North/West in shade
    //    - Roofs: User's exact #f7f3f2 pristine white
    // =====================================================================
    {
        std::vector<float> bldV, bldN, bldU;
        std::vector<unsigned char> bldC;

        // Sunlight light factors by wall direction (Sun from South-East)
        const float LIGHT_ROOF  = 1.00f;
        const float LIGHT_SOUTH = 0.92f;
        const float LIGHT_EAST  = 0.86f;
        const float LIGHT_WEST  = 0.77f;
        const float LIGHT_NORTH = 0.68f;

        // Ambient Occlusion factors
        const float AO_ROOF_OPEN = 1.00f;
        const float AO_TOP_WALL  = 0.94f; // High elevation: open sky ambient
        const float AO_BASE_WALL = 0.65f; // Street level: deep corner contact shadow

        for (const auto& lot : m_buildingLots) {
            float wx0 = (lot.x - m_gridWidth / 2.0f) * s;
            float wz0 = (lot.y - m_gridHeight / 2.0f) * s;
            float wx1 = (lot.x + lot.w - m_gridWidth / 2.0f) * s;
            float wz1 = (lot.y + lot.h - m_gridHeight / 2.0f) * s;

            float bx0 = wx0 + lot.inset;
            float bx1 = wx1 - lot.inset;
            float bz0 = wz0 + lot.inset;
            float bz1 = wz1 - lot.inset;
            float bh = lot.height;

            if (bx1 <= bx0 || bz1 <= bz0) continue;

            // -------------------------------------------------------------
            // Top Roof Face: User exact pristine color #f7f3f2
            // -------------------------------------------------------------
            Color cRoof = CalcShadedColor(BLD_BASE_COLOR, LIGHT_ROOF, AO_ROOF_OPEN);
            Vector3 r0 = {bx0, GROUND_Y + bh, bz0};
            Vector3 r1 = {bx1, GROUND_Y + bh, bz0};
            Vector3 r2 = {bx1, GROUND_Y + bh, bz1};
            Vector3 r3 = {bx0, GROUND_Y + bh, bz1};
            PushQuadAO(bldV, bldN, bldU, bldC, r0, r1, r2, r3, {0, 1, 0}, cRoof, cRoof, cRoof, cRoof);

            // -------------------------------------------------------------
            // South Wall: Sunlit, vertical AO gradient from base to top
            // -------------------------------------------------------------
            Color cSouthBase = CalcShadedColor(BLD_BASE_COLOR, LIGHT_SOUTH, AO_BASE_WALL);
            Color cSouthTop  = CalcShadedColor(BLD_BASE_COLOR, LIGHT_SOUTH, AO_TOP_WALL);
            Vector3 sf0 = {bx0, GROUND_Y, bz1}, sf1 = {bx1, GROUND_Y, bz1};
            Vector3 sf2 = {bx1, GROUND_Y + bh, bz1}, sf3 = {bx0, GROUND_Y + bh, bz1};
            PushQuadAO(bldV, bldN, bldU, bldC, sf0, sf1, sf2, sf3, {0, 0, 1},
                       cSouthBase, cSouthBase, cSouthTop, cSouthTop);

            // -------------------------------------------------------------
            // North Wall: Shaded side, vertical AO gradient
            // -------------------------------------------------------------
            Color cNorthBase = CalcShadedColor(BLD_BASE_COLOR, LIGHT_NORTH, AO_BASE_WALL * 0.95f);
            Color cNorthTop  = CalcShadedColor(BLD_BASE_COLOR, LIGHT_NORTH, AO_TOP_WALL);
            Vector3 nb0 = {bx1, GROUND_Y, bz0}, nb1 = {bx0, GROUND_Y, bz0};
            Vector3 nb2 = {bx0, GROUND_Y + bh, bz0}, nb3 = {bx1, GROUND_Y + bh, bz0};
            PushQuadAO(bldV, bldN, bldU, bldC, nb0, nb1, nb2, nb3, {0, 0, -1},
                       cNorthBase, cNorthBase, cNorthTop, cNorthTop);

            // -------------------------------------------------------------
            // East Wall: Morning sunlit, vertical AO gradient
            // -------------------------------------------------------------
            Color cEastBase = CalcShadedColor(BLD_BASE_COLOR, LIGHT_EAST, AO_BASE_WALL);
            Color cEastTop  = CalcShadedColor(BLD_BASE_COLOR, LIGHT_EAST, AO_TOP_WALL);
            Vector3 er0 = {bx1, GROUND_Y, bz1}, er1 = {bx1, GROUND_Y, bz0};
            Vector3 er2 = {bx1, GROUND_Y + bh, bz0}, er3 = {bx1, GROUND_Y + bh, bz1};
            PushQuadAO(bldV, bldN, bldU, bldC, er0, er1, er2, er3, {1, 0, 0},
                       cEastBase, cEastBase, cEastTop, cEastTop);

            // -------------------------------------------------------------
            // West Wall: Grazing light, vertical AO gradient
            // -------------------------------------------------------------
            Color cWestBase = CalcShadedColor(BLD_BASE_COLOR, LIGHT_WEST, AO_BASE_WALL);
            Color cWestTop  = CalcShadedColor(BLD_BASE_COLOR, LIGHT_WEST, AO_TOP_WALL);
            Vector3 wl0 = {bx0, GROUND_Y, bz0}, wl1 = {bx0, GROUND_Y, bz1};
            Vector3 wl2 = {bx0, GROUND_Y + bh, bz1}, wl3 = {bx0, GROUND_Y + bh, bz0};
            PushQuadAO(bldV, bldN, bldU, bldC, wl0, wl1, wl2, wl3, {-1, 0, 0},
                       cWestBase, cWestBase, cWestTop, cWestTop);

            // -------------------------------------------------------------
            // Architectural Rooftop Penthouse for tall buildings
            // -------------------------------------------------------------
            if (lot.hasPenthouse && (bx1 - bx0 > 0.6f) && (bz1 - bz0 > 0.6f)) {
                float px0 = bx0 + (bx1 - bx0) * 0.22f;
                float px1 = bx1 - (bx1 - bx0) * 0.22f;
                float pz0 = bz0 + (bz1 - bz0) * 0.22f;
                float pz1 = bz1 - (bz1 - bz0) * 0.22f;
                float ph = GROUND_Y + bh + 0.45f;
                float base = GROUND_Y + bh;

                Color cPenthRoof = CalcShadedColor(BLD_BASE_COLOR, 0.98f, 0.98f);
                Color cPenthSouth = CalcShadedColor(BLD_BASE_COLOR, LIGHT_SOUTH, 0.92f);
                Color cPenthNorth = CalcShadedColor(BLD_BASE_COLOR, LIGHT_NORTH, 0.88f);
                Color cPenthEast  = CalcShadedColor(BLD_BASE_COLOR, LIGHT_EAST, 0.90f);
                Color cPenthWest  = CalcShadedColor(BLD_BASE_COLOR, LIGHT_WEST, 0.85f);

                // Penthouse Roof
                PushQuadAO(bldV, bldN, bldU, bldC,
                           {px0, ph, pz0}, {px1, ph, pz0}, {px1, ph, pz1}, {px0, ph, pz1},
                           {0, 1, 0}, cPenthRoof, cPenthRoof, cPenthRoof, cPenthRoof);
                // Penthouse South
                PushQuadAO(bldV, bldN, bldU, bldC,
                           {px0, base, pz1}, {px1, base, pz1}, {px1, ph, pz1}, {px0, ph, pz1},
                           {0, 0, 1}, cPenthSouth, cPenthSouth, cPenthSouth, cPenthSouth);
                // Penthouse North
                PushQuadAO(bldV, bldN, bldU, bldC,
                           {px1, base, pz0}, {px0, base, pz0}, {px0, ph, pz0}, {px1, ph, pz0},
                           {0, 0, -1}, cPenthNorth, cPenthNorth, cPenthNorth, cPenthNorth);
                // Penthouse East
                PushQuadAO(bldV, bldN, bldU, bldC,
                           {px1, base, pz1}, {px1, base, pz0}, {px1, ph, pz0}, {px1, ph, pz1},
                           {1, 0, 0}, cPenthEast, cPenthEast, cPenthEast, cPenthEast);
                // Penthouse West
                PushQuadAO(bldV, bldN, bldU, bldC,
                           {px0, base, pz0}, {px0, base, pz1}, {px0, ph, pz1}, {px0, ph, pz0},
                           {-1, 0, 0}, cPenthWest, cPenthWest, cPenthWest, cPenthWest);
            }
        }

        m_buildingModel = CreateModelFromBuffers(bldV, bldN, bldU, bldC);

        int totalVerts = (int)(bldV.size() / 3);
        TraceLog(LOG_INFO, "3D Buildings compiled: %d lots, %d vertices with baked Ambient Occlusion",
                 (int)m_buildingLots.size(), totalVerts);
    }

    m_modelsLoaded = true;
}

void RaycastWorld::Draw3D(bool showLidarRays) {
    if (!m_modelsLoaded) return;

    // Double-sided rendering ensures all faces are visible from every camera angle
    rlDisableBackfaceCulling();

    // 1. Solid Ground Floor (#e2e4ea neutral terrain)
    if (m_groundModel.meshCount > 0 && m_groundModel.meshes[0].vertexCount > 0)
        DrawModel(m_groundModel, {0, 0, 0}, 1.0f, GROUND_COLOR);

    // 2. Sungai: water blue (#4299e1)
    if (m_riverModel.meshCount > 0 && m_riverModel.meshes[0].vertexCount > 0)
        DrawModel(m_riverModel, {0, 0, 0}, 1.0f, RIVER_COLOR);

    // 3. Jalan: distinct road shade #cdd1dc (user's exact road color)
    if (m_roadModel.meshCount > 0 && m_roadModel.meshes[0].vertexCount > 0)
        DrawModel(m_roadModel, {0, 0, 0}, 1.0f, ROAD_COLOR);

    // 4. Ground Contact Ambient Occlusion (Soft shadows under buildings)
    if (m_shadowModel.meshCount > 0 && m_shadowModel.meshes[0].vertexCount > 0)
        DrawModel(m_shadowModel, {0, 0, 0}, 1.0f, WHITE);

    // 5. Gedung: #f7f3f2 with baked vertex Ambient Occlusion & Directional Shading
    // Using WHITE multiplies vertex colors directly (zero shader cost, max optimization!)
    if (m_buildingModel.meshCount > 0 && m_buildingModel.meshes[0].vertexCount > 0)
        DrawModel(m_buildingModel, {0, 0, 0}, 1.0f, WHITE);

    rlEnableBackfaceCulling();
}

Vector3 RaycastWorld::GridToWorld(float gx, float gy, float yOffset) const {
    float x = (gx - m_gridWidth / 2.0f) * m_cellWorldSize;
    float z = (gy - m_gridHeight / 2.0f) * m_cellWorldSize;
    return {x, yOffset, z};
}

Vector2 RaycastWorld::WorldToGrid(Vector3 worldPos) const {
    float gx = (worldPos.x / m_cellWorldSize) + m_gridWidth / 2.0f;
    float gy = (worldPos.z / m_cellWorldSize) + m_gridHeight / 2.0f;
    return {gx, gy};
}

bool RaycastWorld::IsValidGrid(int gx, int gy) const {
    return (gx >= 0 && gx < m_gridWidth && gy >= 0 && gy < m_gridHeight);
}

MapTileType RaycastWorld::GetTile(int gx, int gy) const {
    if (!IsValidGrid(gx, gy)) return TILE_BUILDING;
    return m_grid[gy * m_gridWidth + gx];
}

bool RaycastWorld::IsWalkable(int gx, int gy) const {
    return (GetTile(gx, gy) == TILE_ROAD);
}

float RaycastWorld::GetBuildingHeight(int gx, int gy) const {
    if (!IsValidGrid(gx, gy)) return 0.0f;
    return m_heights[gy * m_gridWidth + gx];
}

Vector2 RaycastWorld::GetRandomRoadPosition() const {
    if (m_mainRoadCells.empty()) return {0, 0};
    int r = GetRandomValue(0, (int)m_mainRoadCells.size() - 1);
    return m_mainRoadCells[r];
}

bool RaycastWorld::AreConnected(Vector2 a, Vector2 b) const {
    int idxA = (int)a.y * m_gridWidth + (int)a.x;
    int idxB = (int)b.y * m_gridWidth + (int)b.x;
    if (idxA < 0 || idxA >= (int)m_connectedComponent.size()) return false;
    if (idxB < 0 || idxB >= (int)m_connectedComponent.size()) return false;
    return (m_connectedComponent[idxA] != -1 && m_connectedComponent[idxA] == m_connectedComponent[idxB]);
}

RayHitResult RaycastWorld::CastRay3D(Ray ray, float maxDistance) const {
    RayHitResult result = {false, {0,0,0}, {0,1,0}, maxDistance, TILE_GRASS};

    float step = m_cellWorldSize * 0.5f;
    float t = 0.0f;

    while (t < maxDistance) {
        Vector3 curPos = Vector3Add(ray.position, Vector3Scale(ray.direction, t));
        Vector2 gPos = WorldToGrid(curPos);
        int gx = (int)floorf(gPos.x);
        int gy = (int)floorf(gPos.y);

        if (IsValidGrid(gx, gy)) {
            MapTileType type = GetTile(gx, gy);
            float topY = (type == TILE_BUILDING) ? (GROUND_Y + GetBuildingHeight(gx, gy)) :
                         (type == TILE_ROAD) ? ROAD_Y :
                         (type == TILE_RIVER) ? RIVER_Y : GROUND_Y;

            if (curPos.y <= topY) {
                result.hit = true;
                result.point = curPos;
                result.distance = t;
                result.tileType = type;
                result.normal = (type == TILE_BUILDING && curPos.y < topY - 0.1f) ? (Vector3){0, 0, 1} : (Vector3){0, 1, 0};
                return result;
            }
        }
        t += step;
    }

    return result;
}

std::vector<RayHitResult> RaycastWorld::CastLidarRays(Vector3 origin, float headingAngleRad, int rayCount, float maxDist) const {
    std::vector<RayHitResult> hits;
    hits.reserve(rayCount);

    origin.y = 0.45f;

    float angleStep = (2.0f * PI) / rayCount;
    for (int i = 0; i < rayCount; i++) {
        float angle = headingAngleRad + i * angleStep;
        Vector3 dir = {cosf(angle), 0.0f, sinf(angle)};
        Ray ray = {origin, dir};
        RayHitResult r = CastRay3D(ray, maxDist);
        hits.push_back(r);
    }

    return hits;
}
