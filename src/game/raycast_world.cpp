#include "raycast_world.hpp"
#include "rlgl.h"
#include <queue>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <unordered_map>

static const float GROUND_Y = 0.0f;
static const float ROAD_Y   = 0.02f;
static const float RIVER_Y  = 0.015f;

// User specified color references
// jalan = #cdd1dc (205, 209, 220)
// gedung = #f7f3f2 (247, 243, 242)

static void PushQuad(std::vector<float>& verts, std::vector<float>& normals, std::vector<float>& uvs,
                     Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, Vector3 normal) {
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
                                    const std::vector<float>& uvs) {
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

    UploadMesh(&mesh, false);
    return LoadModelFromMesh(mesh);
}

RaycastWorld::RaycastWorld() {
    memset(&m_groundModel, 0, sizeof(Model));
    memset(&m_roadModel, 0, sizeof(Model));
    memset(&m_grassModel, 0, sizeof(Model));
    memset(&m_riverModel, 0, sizeof(Model));
    memset(&m_mapTexture, 0, sizeof(Texture2D));
}

RaycastWorld::~RaycastWorld() {
    Cleanup();
}

void RaycastWorld::Cleanup() {
    if (m_flatModelsLoaded) {
        if (m_groundModel.meshCount > 0) UnloadModel(m_groundModel);
        if (m_roadModel.meshCount > 0) UnloadModel(m_roadModel);
        if (m_grassModel.meshCount > 0) UnloadModel(m_grassModel);
        if (m_riverModel.meshCount > 0) UnloadModel(m_riverModel);
        m_flatModelsLoaded = false;
    }
    // Cleanup instanced building prototypes
    if (m_buildingsLoaded) {
        for (auto& proto : m_buildingPrototypes) {
            if (proto.uploaded) {
                UnloadMesh(proto.mesh);
                // Material is shared default, don't unload it multiple times
            }
            proto.transforms.clear();
            proto.uploaded = false;
        }
        m_buildingPrototypes.clear();
        m_buildingsLoaded = false;
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

    // 1. Sungai berwarna biru (#c2dde6 / #b5daea / dominant light blue)
    if (b > 200.0f && (b - r > 20.0f) && (g - r > 10.0f)) {
        return TILE_RIVER;
    }

    // 2. Distance to exact road color #cdd1dc (205, 209, 220)
    float dr = r - 205.0f;
    float dg = g - 209.0f;
    float db = b - 220.0f;
    float distRoad = sqrtf(dr * dr + dg * dg + db * db);

    // Distance to exact building color #f7f3f2 (247, 243, 242)
    float dbr = r - 247.0f;
    float dbg = g - 243.0f;
    float dbb = b - 242.0f;
    float distBld = sqrtf(dbr * dbr + dbg * dbg + dbb * dbb);

    float meanB = (r + g + b) / 3.0f;
    float maxC = fmaxf(fmaxf(r, g), b);
    float minC = fminf(fminf(r, g), b);
    float diff = maxC - minC;

    // Road check: close to #cdd1dc OR darker road lines (between 90 and 228 brightness, low saturation)
    if (distRoad < 40.0f || (meanB >= 90.0f && meanB < 228.0f && diff < 28.0f)) {
        return TILE_ROAD;
    }

    // Default: Gedung / Bangunan (#f7f3f2 / white city blocks)
    return TILE_BUILDING;
}

bool RaycastWorld::GenerateFromImage(const char* imagePath, int downscaleFactor) {
    Cleanup();

    Image img = LoadImage(imagePath);
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

    // Build flat surface meshes (ground, road, river) — still single merged model
    BuildFlatSurfaceMeshes();

    // Build GPU instanced building prototypes (~15 types, 1 draw call each!)
    BuildInstancedBuildings();

    TraceLog(LOG_INFO, "3D World successfully built: %dx%d cells, %d connected roads, %d building prototypes",
             m_gridWidth, m_gridHeight, (int)m_mainRoadCells.size(), (int)m_buildingPrototypes.size());
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

    // Group adjacent building cells into diverse lots (different widths & depths: 3x2, 2x3, 2x2, 2x1, 1x2, 1x1)
    for (int y = 0; y < m_gridHeight; y++) {
        for (int x = 0; x < m_gridWidth; x++) {
            int idx = y * m_gridWidth + x;
            if (m_grid[idx] != TILE_BUILDING || assigned[idx]) {
                continue;
            }

            unsigned int seed = (unsigned int)(x * 73856093 ^ y * 19349663);
            int pref = seed % 100;

            const int candidates[6][2] = {
                {3, 2}, {2, 3}, {2, 2}, {2, 1}, {1, 2}, {1, 1}
            };
            int candStart = (pref < 16) ? 0 : (pref < 42) ? 2 : (pref < 72) ? 3 : 5;

            int bestW = 1, bestH = 1;
            for (int c = candStart; c < 6; c++) {
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
            float zone = (sinf((float)x * 0.045f) * cosf((float)y * 0.045f) + 1.0f) * 0.5f;
            float rnd = (float)(seed % 1000) / 1000.0f;
            float height = 0.8f;
            bool hasPenthouse = false;

            if (bestW * bestH >= 4) {
                // Wide buildings (2x2, 3x2, 2x3)
                if (zone > 0.65f) {
                    // Skyscraper / Corporate Tower
                    height = 3.6f + rnd * 2.4f; // 3.6f to 6.0f tall!
                    hasPenthouse = true;
                } else if (zone > 0.35f) {
                    // Mid-rise Commercial Mall / Office
                    height = 2.0f + rnd * 1.5f; // 2.0f to 3.5f
                    if (height > 2.8f) hasPenthouse = true;
                } else {
                    // Large Residential Complex / School campus
                    height = 1.2f + rnd * 1.0f; // 1.2f to 2.2f
                }
            } else if (bestW * bestH >= 2) {
                // Medium buildings (2x1, 1x2)
                if (zone > 0.70f) {
                    height = 2.6f + rnd * 1.8f; // 2.6f to 4.4f
                    if (height > 3.4f) hasPenthouse = true;
                } else {
                    height = 1.3f + rnd * 1.2f; // 1.3f to 2.5f
                }
            } else {
                // 1x1 Compact Lots (Individual houses, shops)
                if (rnd > 0.90f && zone > 0.75f) {
                    height = 3.2f + rnd * 1.5f; // Occasional slender high spire
                    hasPenthouse = true;
                } else {
                    height = 0.75f + rnd * 0.75f; // 0.75f to 1.5f (low-rise houses)
                }
            }

            // Setback margin with slight variation per lot
            float inset = m_cellWorldSize * (0.12f + 0.06f * (rnd * 0.5f));

            BuildingLot lot;
            lot.x = x;
            lot.y = y;
            lot.w = bestW;
            lot.h = bestH;
            lot.height = height;
            lot.inset = inset;
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
// BuildFlatSurfaceMeshes: ground plane, roads, river
// These remain as single merged meshes (efficient enough for flat quads)
// =========================================================================
void RaycastWorld::BuildFlatSurfaceMeshes() {
    std::vector<float> gndVerts, gndNorms, gndUvs;
    std::vector<float> roadVerts, roadNorms, roadUvs;
    std::vector<float> riverVerts, riverNorms, riverUvs;

    float s = m_cellWorldSize;

    // 1. Solid Ground Plane across the entire world (no sky holes!)
    float totalW = m_gridWidth * s;
    float totalH = m_gridHeight * s;
    float halfW = totalW * 0.5f;
    float halfH = totalH * 0.5f;

    Vector3 gp0 = {-halfW, GROUND_Y, -halfH};
    Vector3 gp1 = { halfW, GROUND_Y, -halfH};
    Vector3 gp2 = { halfW, GROUND_Y,  halfH};
    Vector3 gp3 = {-halfW, GROUND_Y,  halfH};
    PushQuad(gndVerts, gndNorms, gndUvs, gp0, gp1, gp2, gp3, {0, 1, 0});

    // 2. Procedural Road and River Surfaces
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
                PushQuad(roadVerts, roadNorms, roadUvs, p0, p1, p2, p3, {0, 1, 0});
            } else if (type == TILE_RIVER) {
                Vector3 p0 = {wx,     RIVER_Y, wz};
                Vector3 p1 = {wx + s, RIVER_Y, wz};
                Vector3 p2 = {wx + s, RIVER_Y, wz + s};
                Vector3 p3 = {wx,     RIVER_Y, wz + s};
                PushQuad(riverVerts, riverNorms, riverUvs, p0, p1, p2, p3, {0, 1, 0});
            }
        }
    }

    m_groundModel = CreateModelFromBuffers(gndVerts, gndNorms, gndUvs);
    m_roadModel = CreateModelFromBuffers(roadVerts, roadNorms, roadUvs);
    m_riverModel = CreateModelFromBuffers(riverVerts, riverNorms, riverUvs);
    m_flatModelsLoaded = true;
}

// =========================================================================
// GPU INSTANCED RENDERING OPTIMIZATION
// =========================================================================
// Instead of baking ALL building geometry into one giant mesh (100K+ verts),
// we create ~15 prototype cube meshes with distinct (width, depth, height)
// and use DrawMeshInstanced() to render all instances of each prototype
// in a SINGLE draw call.
//
// Result: ~15 draw calls instead of 1 giant VBO = massively better FPS
// =========================================================================

// Hash key for building prototype: quantized (w, d, h, hasPenthouse)
struct ProtoKey {
    int qw, qd, qh;
    bool penthouse;

    bool operator==(const ProtoKey& o) const {
        return qw == o.qw && qd == o.qd && qh == o.qh && penthouse == o.penthouse;
    }
};

struct ProtoKeyHash {
    size_t operator()(const ProtoKey& k) const {
        size_t h = (size_t)k.qw * 73856093;
        h ^= (size_t)k.qd * 19349663;
        h ^= (size_t)k.qh * 83492791;
        h ^= k.penthouse ? 1 : 0;
        return h;
    }
};

// Quantize a float dimension into discrete buckets for prototype grouping
static int QuantizeDim(float val, float step) {
    return (int)(val / step + 0.5f);
}

void RaycastWorld::BuildInstancedBuildings() {
    m_buildingPrototypes.clear();

    float s = m_cellWorldSize;

    // Step 1: Quantize all building lots into prototype buckets
    // Width/depth quantized to 0.15 units, height to 0.4 units
    // This typically yields ~12-16 unique prototypes from thousands of lots
    const float DIM_STEP = 0.15f;
    const float HEIGHT_STEP = 0.4f;

    std::unordered_map<ProtoKey, int, ProtoKeyHash> protoMap;

    struct InstanceData {
        ProtoKey key;
        float cx, cz;   // Center position in world
        float actualW, actualD, actualH; // Actual dimensions for this lot
    };
    std::vector<InstanceData> instances;
    instances.reserve(m_buildingLots.size());

    for (const auto& lot : m_buildingLots) {
        float wx0 = (lot.x - m_gridWidth / 2.0f) * s;
        float wz0 = (lot.y - m_gridHeight / 2.0f) * s;
        float wx1 = (lot.x + lot.w - m_gridWidth / 2.0f) * s;
        float wz1 = (lot.y + lot.h - m_gridHeight / 2.0f) * s;

        float bx0 = wx0 + lot.inset;
        float bx1 = wx1 - lot.inset;
        float bz0 = wz0 + lot.inset;
        float bz1 = wz1 - lot.inset;

        float bw = bx1 - bx0;
        float bd = bz1 - bz0;
        float bh = lot.height;

        if (bw <= 0.05f || bd <= 0.05f || bh <= 0.05f) continue;

        ProtoKey key;
        key.qw = QuantizeDim(bw, DIM_STEP);
        key.qd = QuantizeDim(bd, DIM_STEP);
        key.qh = QuantizeDim(bh, HEIGHT_STEP);
        key.penthouse = lot.hasPenthouse && (bw > 0.6f) && (bd > 0.6f);

        // Ensure minimum quantized values
        if (key.qw < 1) key.qw = 1;
        if (key.qd < 1) key.qd = 1;
        if (key.qh < 1) key.qh = 1;

        float cx = (bx0 + bx1) * 0.5f;
        float cz = (bz0 + bz1) * 0.5f;

        InstanceData inst;
        inst.key = key;
        inst.cx = cx;
        inst.cz = cz;
        inst.actualW = bw;
        inst.actualD = bd;
        inst.actualH = bh;
        instances.push_back(inst);

        if (protoMap.find(key) == protoMap.end()) {
            protoMap[key] = (int)protoMap.size();
        }
    }

    TraceLog(LOG_INFO, "Building instancing: %d lots -> %d unique prototypes",
             (int)m_buildingLots.size(), (int)protoMap.size());

    // Step 2: Create one Mesh per unique prototype (unit cube scaled by prototype dims)
    m_buildingPrototypes.resize(protoMap.size());

    for (auto& [key, idx] : protoMap) {
        float pw = key.qw * DIM_STEP;
        float pd = key.qd * DIM_STEP;
        float ph = key.qh * HEIGHT_STEP;

        // Generate building mesh at origin: base cube + optional penthouse on top
        std::vector<float> verts, norms, uvs;
        float hw = pw * 0.5f;
        float hd = pd * 0.5f;

        // Main building body (centered at origin, bottom at y=0, top at y=ph)
        // Top roof
        PushQuad(verts, norms, uvs,
                 {-hw, ph, -hd}, {hw, ph, -hd}, {hw, ph, hd}, {-hw, ph, hd}, {0, 1, 0});
        // North wall
        PushQuad(verts, norms, uvs,
                 {hw, 0, -hd}, {-hw, 0, -hd}, {-hw, ph, -hd}, {hw, ph, -hd}, {0, 0, -1});
        // South wall
        PushQuad(verts, norms, uvs,
                 {-hw, 0, hd}, {hw, 0, hd}, {hw, ph, hd}, {-hw, ph, hd}, {0, 0, 1});
        // West wall
        PushQuad(verts, norms, uvs,
                 {-hw, 0, -hd}, {-hw, 0, hd}, {-hw, ph, hd}, {-hw, ph, -hd}, {-1, 0, 0});
        // East wall
        PushQuad(verts, norms, uvs,
                 {hw, 0, hd}, {hw, 0, -hd}, {hw, ph, -hd}, {hw, ph, hd}, {1, 0, 0});

        // Penthouse structure on top
        if (key.penthouse) {
            float px = hw * 0.56f;
            float pz = hd * 0.56f;
            float pTop = ph + 0.45f;

            PushQuad(verts, norms, uvs,
                     {-px, pTop, -pz}, {px, pTop, -pz}, {px, pTop, pz}, {-px, pTop, pz}, {0, 1, 0});
            PushQuad(verts, norms, uvs,
                     {px, ph, -pz}, {-px, ph, -pz}, {-px, pTop, -pz}, {px, pTop, -pz}, {0, 0, -1});
            PushQuad(verts, norms, uvs,
                     {-px, ph, pz}, {px, ph, pz}, {px, pTop, pz}, {-px, pTop, pz}, {0, 0, 1});
            PushQuad(verts, norms, uvs,
                     {-px, ph, -pz}, {-px, ph, pz}, {-px, pTop, pz}, {-px, pTop, -pz}, {-1, 0, 0});
            PushQuad(verts, norms, uvs,
                     {px, ph, pz}, {px, ph, -pz}, {px, pTop, -pz}, {px, pTop, pz}, {1, 0, 0});
        }

        // Build GPU mesh
        BuildingPrototype& proto = m_buildingPrototypes[idx];
        Mesh mesh = {0};
        mesh.triangleCount = (int)(verts.size() / 9);
        mesh.vertexCount = (int)(verts.size() / 3);
        mesh.vertices = (float*)MemAlloc(sizeof(float) * verts.size());
        mesh.normals = (float*)MemAlloc(sizeof(float) * norms.size());
        mesh.texcoords = (float*)MemAlloc(sizeof(float) * uvs.size());
        memcpy(mesh.vertices, verts.data(), sizeof(float) * verts.size());
        memcpy(mesh.normals, norms.data(), sizeof(float) * norms.size());
        memcpy(mesh.texcoords, uvs.data(), sizeof(float) * uvs.size());
        UploadMesh(&mesh, false);

        proto.mesh = mesh;
        proto.material = LoadMaterialDefault();
        // Gedung: user exact color #f7f3f2 (247, 243, 242)
        proto.material.maps[MATERIAL_MAP_DIFFUSE].color = (Color){247, 243, 242, 255};
        proto.uploaded = true;
    }

    // Step 3: Assign instance transforms to each prototype
    for (const auto& inst : instances) {
        auto it = protoMap.find(inst.key);
        if (it == protoMap.end()) continue;

        int protoIdx = it->second;

        // Each instance is placed at its world center, bottom at GROUND_Y
        // The mesh is already built centered at origin with base at y=0
        // So we just translate to world position
        Matrix transform = MatrixTranslate(inst.cx, GROUND_Y, inst.cz);

        m_buildingPrototypes[protoIdx].transforms.push_back(transform);
    }

    // Log instance counts
    for (size_t i = 0; i < m_buildingPrototypes.size(); i++) {
        TraceLog(LOG_INFO, "  Prototype %d: %d instances, %d verts/instance",
                 (int)i, (int)m_buildingPrototypes[i].transforms.size(),
                 m_buildingPrototypes[i].mesh.vertexCount);
    }

    m_buildingsLoaded = true;
}

void RaycastWorld::Draw3D(bool showLidarRays) {
    rlDisableBackfaceCulling();

    // 1. Solid Ground Floor: #cdd1dc (never transparent/sky void)
    if (m_flatModelsLoaded) {
        Color groundColor = {205, 209, 220, 255};
        if (m_groundModel.meshCount > 0 && m_groundModel.meshes[0].vertexCount > 0)
            DrawModel(m_groundModel, {0, 0, 0}, 1.0f, groundColor);

        // 2. Sungai: water blue (#4299e1)
        Color riverColor = {66, 153, 225, 255};
        if (m_riverModel.meshCount > 0 && m_riverModel.meshes[0].vertexCount > 0)
            DrawModel(m_riverModel, {0, 0, 0}, 1.0f, riverColor);

        // 3. Jalan: distinct road shade of #cdd1dc
        Color roadColor = {176, 181, 196, 255};
        if (m_roadModel.meshCount > 0 && m_roadModel.meshes[0].vertexCount > 0)
            DrawModel(m_roadModel, {0, 0, 0}, 1.0f, roadColor);
    }

    // 4. GPU Instanced Buildings: ~15 draw calls instead of thousands!
    if (m_buildingsLoaded) {
        for (auto& proto : m_buildingPrototypes) {
            if (!proto.uploaded || proto.transforms.empty()) continue;
            DrawMeshInstanced(proto.mesh, proto.material, proto.transforms.data(),
                              (int)proto.transforms.size());
        }
    }

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
