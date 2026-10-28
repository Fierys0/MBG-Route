#include "pathfinder_state.hpp"
#include "rlgl.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <map>
#include <chrono>

// Google Maps Leg Colors (Each school destination has its own unique vibrant route color)
static const Color LEG_COLORS[] = {
    {26, 115, 232, 255},  // Stop 1: Google Maps Vibrant Blue (#1A73E8)
    {15, 157, 88, 255},   // Stop 2: Google Maps Emerald Green (#0F9D58)
    {242, 153, 0, 255},   // Stop 3: Google Maps Amber/Orange (#F29900)
    {147, 52, 232, 255},  // Stop 4: Google Maps Royal Purple (#9334E8)
    {229, 37, 146, 255},  // Stop 5: Google Maps Pink/Magenta (#E52592)
    {0, 180, 216, 255}    // Stop 6: Google Maps Cyan/Teal
};

static const Color LEG_DARK_COLORS[] = {
    {11, 87, 208, 255},
    {10, 110, 60, 255},
    {190, 115, 0, 255},
    {110, 35, 180, 255},
    {170, 20, 100, 255},
    {0, 130, 160, 255}
};

static const char* LEG_COLOR_NAMES[] = {
    "Biru", "Hijau", "Oranye", "Ungu", "Pink", "Cyan"
};

// UI Button Helper
struct StateButton {
    Rectangle rect;
    const char* label;
    bool active;

    bool IsClicked() const {
        Vector2 mouse = GetMousePosition();
        return CheckCollisionPointRec(mouse, rect) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    }

    void Draw(Font font, Color baseCol, Color hoverCol, Color activeCol, Color textCol) const {
        Vector2 mouse = GetMousePosition();
        bool hovered = CheckCollisionPointRec(mouse, rect);
        Color col = active ? activeCol : (hovered ? hoverCol : baseCol);

        DrawRectangleRounded(rect, 0.28f, 4, col);
        DrawRectangleRoundedLinesEx(rect, 0.28f, 4, 1.5f, Fade(BLACK, 0.4f));

        float fontSize = 12.0f;
        float spacing = 0.5f;
        float textW = (font.texture.id > 0) ? MeasureTextEx(font, label, fontSize, spacing).x : (float)MeasureText(label, (int)fontSize);
        float tx = rect.x + (rect.width - textW) * 0.5f;
        float ty = rect.y + (rect.height - fontSize) * 0.5f - 1.0f;
        if (font.texture.id > 0) {
            DrawTextEx(font, label, (Vector2){tx, ty}, fontSize, spacing, textCol);
        } else {
            DrawText(label, (int)tx, (int)ty, (int)fontSize, textCol);
        }
    }
};

PathfinderState::PathfinderState() {
    memset(&m_truckModel, 0, sizeof(Model));
    memset(&m_schoolModel, 0, sizeof(Model));
}

PathfinderState::~PathfinderState() {
    Cleanup();
}

void PathfinderState::LoadFonts() {
    // Collect standard character codepoints: Basic ASCII + Latin-1 + Navigation symbols supported by Montserrat
    std::vector<int> codepoints;
    for (int i = 32; i < 127; i++) codepoints.push_back(i); // ASCII 32-126
    for (int i = 160; i < 256; i++) codepoints.push_back(i); // Latin-1
    codepoints.push_back(0x2022); // • (Bullet)
    codepoints.push_back(0x2190); // ← (Left)
    codepoints.push_back(0x2191); // ↑ (Up)
    codepoints.push_back(0x2192); // → (Right)
    codepoints.push_back(0x2193); // ↓ (Down)
    codepoints.push_back(0x25B6); // ▶ (Play / Target)
    codepoints.push_back(0x25C0); // ◀ (Reverse)
    codepoints.push_back(0x25B2); // ▲ (Up triangle)
    codepoints.push_back(0x25BC); // ▼ (Down triangle)

    auto loadSingleFont = [&](const std::string& path, int fontSize) -> Font {
        // 1. Try loading from asset pack first
        for (const auto& pack : Fumbo::Assets::GetAssetPacks()) {
            if (pack && pack->IsLoaded() && pack->HasAsset(path)) {
                std::vector<uint8_t> data = pack->LoadAsset(path);
                if (!data.empty()) {
                    const char* ext = GetFileExtension(path.c_str());
                    Font f = LoadFontFromMemory(ext, data.data(), (int)data.size(), fontSize, codepoints.data(), (int)codepoints.size());
                    if (f.texture.id > 0) {
                        SetTextureFilter(f.texture, TEXTURE_FILTER_BILINEAR);
                        TraceLog(LOG_INFO, "[Montserrat] Loaded %s from pack (size=%d, glyphs=%d)", path.c_str(), fontSize, f.glyphCount);
                        return f;
                    }
                }
            }
        }
        // 2. Try loading from raw filesystem
        if (FileExists(path.c_str())) {
            Font f = LoadFontEx(path.c_str(), fontSize, codepoints.data(), (int)codepoints.size());
            if (f.texture.id > 0) {
                SetTextureFilter(f.texture, TEXTURE_FILTER_BILINEAR);
                TraceLog(LOG_INFO, "[Montserrat] Loaded %s from file (size=%d, glyphs=%d)", path.c_str(), fontSize, f.glyphCount);
                return f;
            }
        }
        // 3. Fallback to Fumbo default font loader
        Font f = Fumbo::Assets::LoadFont(path, fontSize);
        if (f.texture.id > 0) SetTextureFilter(f.texture, TEXTURE_FILTER_BILINEAR);
        return f;
    };

    m_fontBold = loadSingleFont("assets/fonts/Montserrat-Bold.ttf", 40);
    m_fontSemiBold = loadSingleFont("assets/fonts/Montserrat-SemiBold.ttf", 34);
    m_fontRegular = loadSingleFont("assets/fonts/Montserrat-Medium.ttf", 28);
    if (m_fontRegular.texture.id == 0) {
        m_fontRegular = loadSingleFont("assets/fonts/Montserrat-Regular.ttf", 28);
    }

    m_fontsLoaded = (m_fontRegular.texture.id > 0 || m_fontSemiBold.texture.id > 0 || m_fontBold.texture.id > 0);
    TraceLog(LOG_INFO, "Montserrat Fonts Loaded successfully (Bold=%d, SemiBold=%d, Regular=%d)",
             m_fontBold.texture.id, m_fontSemiBold.texture.id, m_fontRegular.texture.id);
}

void PathfinderState::UnloadFonts() {
    if (m_fontBold.texture.id > 0) {
        UnloadFont(m_fontBold);
        m_fontBold = {0};
    }
    if (m_fontSemiBold.texture.id > 0) {
        UnloadFont(m_fontSemiBold);
        m_fontSemiBold = {0};
    }
    if (m_fontRegular.texture.id > 0) {
        UnloadFont(m_fontRegular);
        m_fontRegular = {0};
    }
    m_fontsLoaded = false;
}

void PathfinderState::DrawTextCustom(const char* text, float x, float y, float size, Color color, FontStyle style) const {
    Font f = m_fontRegular;
    if (style == FONT_BOLD && m_fontBold.texture.id > 0) f = m_fontBold;
    else if (style == FONT_SEMIBOLD && m_fontSemiBold.texture.id > 0) f = m_fontSemiBold;
    else if (m_fontRegular.texture.id > 0) f = m_fontRegular;
    else if (m_fontSemiBold.texture.id > 0) f = m_fontSemiBold;

    if (f.texture.id > 0) {
        float spacing = 0.5f;
        DrawTextEx(f, text, (Vector2){x, y}, size, spacing, color);
    } else {
        DrawText(text, (int)x, (int)y, (int)size, color);
    }
}

float PathfinderState::MeasureTextCustom(const char* text, float size, FontStyle style) const {
    Font f = m_fontRegular;
    if (style == FONT_BOLD && m_fontBold.texture.id > 0) f = m_fontBold;
    else if (style == FONT_SEMIBOLD && m_fontSemiBold.texture.id > 0) f = m_fontSemiBold;
    else if (m_fontRegular.texture.id > 0) f = m_fontRegular;
    else if (m_fontSemiBold.texture.id > 0) f = m_fontSemiBold;

    if (f.texture.id > 0) {
        float spacing = 0.5f;
        return MeasureTextEx(f, text, size, spacing).x;
    } else {
        return (float)MeasureText(text, (int)size);
    }
}

void PathfinderState::Init() {
    TraceLog(LOG_INFO, "Initializing PathfinderState with MBG Routing Algorithm & Montserrat Fonts...");

    // 0. Load Montserrat Typography from assets/fonts
    LoadFonts();

    // 1. Generate 3D procedural world using Ray Casting against map image
    bool ok = m_world.GenerateFromImage("assets/map.jpg", 6);
    if (!ok) {
        TraceLog(LOG_ERROR, "Failed to generate world from assets/map.jpg");
    }

    // 2. Load 3D GLTF models
    m_truckModel = LoadModel("assets/cartoony_style_delivery_truck_low_poly/scene.gltf");
    m_truckLoaded = (m_truckModel.meshCount > 0);

    m_schoolModel = LoadModel("assets/low_poly_school/scene.gltf");
    m_schoolLoaded = (m_schoolModel.meshCount > 0);

    TraceLog(LOG_INFO, "Models loaded: Truck=%d meshes, School=%d meshes",
             m_truckModel.meshCount, m_schoolModel.meshCount);

    // 3. Initialize 3D Camera for Google Maps navigation mode
    m_camera.position = (Vector3){0.0f, 25.0f, 25.0f};
    m_camera.target = (Vector3){0.0f, 0.0f, 0.0f};
    m_camera.up = (Vector3){0.0f, 1.0f, 0.0f};
    m_camera.fovy = 50.0f;
    m_camera.projection = CAMERA_PERSPECTIVE;
    m_camMode = APP_CAM_FOLLOW_CAR;

    // 4. Randomize initial positions & solve MBG multi-school routing
    RandomizePositions();

    m_isDriving = true;
}

void PathfinderState::Cleanup() {
    UnloadRouteModels();
    UnloadFonts();

    if (m_truckLoaded) {
        UnloadModel(m_truckModel);
        m_truckLoaded = false;
    }
    if (m_schoolLoaded) {
        UnloadModel(m_schoolModel);
        m_schoolLoaded = false;
    }
    m_world.Cleanup();
}

void PathfinderState::UnloadRouteModels() {
    for (auto& leg : m_routeLegs) {
        if (leg.modelsLoaded) {
            m_pathfinder.UnloadPathModel(leg.pathModel);
            m_pathfinder.UnloadPathModel(leg.borderModel);
            leg.modelsLoaded = false;
        }
    }
    m_routeLegs.clear();
}

// =============================================================================
// Pick multiple school destinations on the connected road network
// and initialize SPPG (Dapur Pusat) and schools with Time Windows
// =============================================================================
void PathfinderState::RandomizePositions() {
    // 1. Pick SPPG (Satuan Pelayanan Pemenuhan Gizi / Central Kitchen) location
    m_carStartGridPos = m_world.GetRandomRoadPosition();
    m_carCurrentPos = m_world.GridToWorld(m_carStartGridPos.x, m_carStartGridPos.y, 0.05f);

    // Clamp school count to [3, MAX_SCHOOLS]
    if (m_numSchools < 3) m_numSchools = 3;
    if (m_numSchools > MAX_SCHOOLS) m_numSchools = MAX_SCHOOLS;

    // 2. Pick distinct schools on connected roads with good spatial distribution
    std::vector<Vector2> pickedSchoolGridCoords;
    int attempts = 0;

    while ((int)pickedSchoolGridCoords.size() < m_numSchools && attempts < 400) {
        attempts++;
        Vector2 cand = m_world.GetRandomRoadPosition();

        if (!m_world.AreConnected(m_carStartGridPos, cand)) continue;
        if (Vector2Distance(m_carStartGridPos, cand) < 28.0f) continue;

        bool tooCloseToOther = false;
        for (const auto& existing : pickedSchoolGridCoords) {
            if (Vector2Distance(existing, cand) < 26.0f) {
                tooCloseToOther = true;
                break;
            }
        }
        if (!tooCloseToOther) {
            pickedSchoolGridCoords.push_back(cand);
        }
    }

    // Fallback if strict spacing didn't find enough
    while ((int)pickedSchoolGridCoords.size() < m_numSchools) {
        Vector2 cand = m_world.GetRandomRoadPosition();
        if (m_world.AreConnected(m_carStartGridPos, cand)) {
            pickedSchoolGridCoords.push_back(cand);
        }
    }

    // 3. Assemble schools with realistic delivery Time Windows and unload times
    m_schools.clear();
    for (int i = 0; i < (int)pickedSchoolGridCoords.size(); i++) {
        Vector2 gPos = pickedSchoolGridCoords[i];

        SchoolDestination sch;
        sch.id = i + 1;
        sch.name = TextFormat("SDN 0%d", i + 1);
        sch.gridPos = gPos;
        sch.worldPos = m_world.GridToWorld(gPos.x, gPos.y, 0.0f);

        // Realistic delivery time windows in minutes:
        // Lunch delivery window between 10:00 - 12:30 (simulated in minutes)
        float estDist = Vector2Distance(m_carStartGridPos, gPos);
        int baseMins = std::max(15, (int)(estDist * 0.45f));
        sch.window.start = std::max(10, baseMins - 10);
        sch.window.end   = baseMins + 75 + i * 25; // Achievable window
        sch.unloadTime   = 5; // 5 menit bongkar muatan makanan bergizi
        sch.color        = LEG_COLORS[i % 6];
        sch.darkColor    = LEG_DARK_COLORS[i % 6];
        sch.reached      = false;

        m_schools.push_back(sch);
    }

    // 4. Run MBG Routing Algorithm (Graph + Dijkstra + DP Modified / Baseline)
    OptimizeAndBuildRoutes();

    m_isDriving = true;
    m_allDelivered = false;
    m_isUnloading = false;
    m_unloadTimer = 0.0f;
    m_arrivalTimer = 0.0f;
    m_currentLegIndex = 0;
    m_legDriveProgress = 0.0f;

    // Orient camera and car towards first waypoint
    if (!m_routeLegs.empty() && m_routeLegs[0].path.worldWaypoints.size() >= 2) {
        Vector3 diff = Vector3Subtract(m_routeLegs[0].path.worldWaypoints[1], m_routeLegs[0].path.worldWaypoints[0]);
        m_carHeading = atan2f(diff.x, diff.z);
        Vector3 offset = {-sinf(m_carHeading) * 16.0f, 10.0f, -cosf(m_carHeading) * 16.0f};
        m_camera.position = Vector3Add(m_carCurrentPos, offset);
        m_camera.target = Vector3Add(m_carCurrentPos, (Vector3){0, 1.2f, 0});
    }
}

// =============================================================================
// Optimize routes using the MBG Routing Algorithm from `mbg_routing`
// Uses Graph, Dijkstra, and DPModified / DPBaseline (TSPTW with Food Freshness)
// =============================================================================
void PathfinderState::OptimizeAndBuildRoutes() {
    UnloadRouteModels();

    m_totalRouteDistance = 0.0f;
    m_totalNodesExplored = 0;
    m_totalExecutionTimeMs = 0.0;

    int n = (int)m_schools.size();
    if (n == 0) return;

    // Build list of all locations:
    // Location 0 = SPPG (Dapur Pusat / Car Start)
    // Locations 1..n = Schools
    struct LocInfo {
        int id; // 0 for SPPG, 1..n for schools
        Vector2 gridPos;
    };

    std::vector<LocInfo> locations;
    locations.push_back({0, m_carStartGridPos});
    for (int i = 0; i < n; i++) {
        locations.push_back({m_schools[i].id, m_schools[i].gridPos});
    }

    int totalLocs = (int)locations.size();

    // Cache computed road paths and edge weights for Graph g
    std::map<std::pair<int, int>, PathResult> pathCache;
    Graph g;

    for (int i = 0; i < totalLocs; i++) {
        for (int j = i + 1; j < totalLocs; j++) {
            PathResult p = m_pathfinder.FindShortestPath(m_world, locations[i].gridPos, locations[j].gridPos, m_gridPathAlgo);
            pathCache[{locations[i].id, locations[j].id}] = p;

            // Travel time in simulation minutes: 1 world distance unit ~ 0.4 minute
            int weight = std::max(1, (int)roundf(p.totalDistanceWorld * 0.40f));
            g.addEdge(locations[i].id, locations[j].id, weight, false);
        }
    }

    // Convert m_schools to mbg_routing's School structs
    std::vector<School> routingSchools;
    for (int i = 0; i < n; i++) {
        School s;
        s.id = m_schools[i].id;
        s.name = m_schools[i].name;
        s.window = m_schools[i].window;
        s.unload_time = m_schools[i].unloadTime;
        routingSchools.push_back(s);
    }

    // Execute the selected MBG routing algorithm from mbg_routing!
    auto t0 = std::chrono::high_resolution_clock::now();
    std::vector<int> optimalRoute;

    if (m_routingAlgorithm == ROUTING_DP_MODIFIED) {
        DPModifiedResult res = dpModified(g, 0, routingSchools, m_cookTime);
        auto t1 = std::chrono::high_resolution_clock::now();
        m_optimizationTimeUs = std::chrono::duration<double, std::micro>(t1 - t0).count();

        m_routeFeasible = res.feasible;
        m_totalCostMinutes = res.total_cost;
        m_statesExplored = res.states_explored;
        optimalRoute = res.route;
    } else {
        DPResult res = dpBaseline(g, 0, routingSchools, m_cookTime);
        auto t1 = std::chrono::high_resolution_clock::now();
        m_optimizationTimeUs = std::chrono::duration<double, std::micro>(t1 - t0).count();

        m_routeFeasible = res.feasible;
        m_totalCostMinutes = res.total_cost;
        m_statesExplored = (1 << n) * MAX_TIME; // Theoretical state space
        optimalRoute = res.route;
    }

    // Fallback if algorithm returned empty route (e.g. strict window violation)
    if (optimalRoute.empty()) {
        m_routeFeasible = false;
        for (int i = 0; i < n; i++) {
            optimalRoute.push_back(m_schools[i].id);
        }
    }

    m_optimalSequence = optimalRoute;

    // Reorder m_schools according to optimalSequence
    std::vector<SchoolDestination> orderedSchools;
    for (int schoolId : optimalRoute) {
        for (const auto& sch : m_schools) {
            if (sch.id == schoolId) {
                orderedSchools.push_back(sch);
                break;
            }
        }
    }
    m_schools = orderedSchools;

    // Update colors and build optimization log
    m_optimizationLog = "MBG: SPPG (Dapur)";
    for (size_t i = 0; i < m_schools.size(); i++) {
        m_schools[i].color = LEG_COLORS[i % 6];
        m_schools[i].darkColor = LEG_DARK_COLORS[i % 6];
        m_schools[i].reached = false;
        m_optimizationLog += TextFormat(" → [%d] %s (%s)", (int)i + 1, m_schools[i].name.c_str(), LEG_COLOR_NAMES[i % 6]);
    }

    // Build the routes and 3D colored ribbon meshes for each leg
    int prevId = 0; // SPPG
    for (size_t i = 0; i < m_schools.size(); i++) {
        int curId = m_schools[i].id;

        RouteLeg leg;
        leg.legIndex = (int)i;
        leg.targetSchoolIndex = (int)i;
        leg.destinationName = m_schools[i].name;
        leg.color = m_schools[i].color;
        leg.isReturnToDepot = false;

        // Retrieve road path from cache or compute
        std::pair<int, int> key1 = {prevId, curId};
        std::pair<int, int> key2 = {curId, prevId};

        if (pathCache.count(key1)) {
            leg.path = pathCache[key1];
        } else if (pathCache.count(key2)) {
            leg.path = pathCache[key2];
            std::reverse(leg.path.gridPath.begin(), leg.path.gridPath.end());
            std::reverse(leg.path.worldWaypoints.begin(), leg.path.worldWaypoints.end());
        } else {
            Vector2 startPos = (prevId == 0) ? m_carStartGridPos : m_schools[i - 1].gridPos;
            leg.path = m_pathfinder.FindShortestPath(m_world, startPos, m_schools[i].gridPos, m_gridPathAlgo);
        }

        m_totalRouteDistance += leg.path.totalDistanceWorld;
        m_totalNodesExplored += leg.path.nodesExplored;
        m_totalExecutionTimeMs += leg.path.executionTimeMs;

        if (leg.path.found && leg.path.gridPath.size() >= 2) {
            // 1. Crisp white underlay border
            leg.borderModel = m_pathfinder.BuildPathMeshModel(m_world, leg.path.gridPath, 0.70f, 0.08f);
            // 2. Core colored ribbon for this school leg!
            leg.pathModel = m_pathfinder.BuildPathMeshModel(m_world, leg.path.gridPath, 0.48f, 0.14f);
            leg.modelsLoaded = true;
        }

        m_routeLegs.push_back(leg);
        prevId = curId;
    }

    // =========================================================================
    // FINAL STEP: Mobil kembali ke titik awal (SPPG / Dapur Pusat)!
    // Setelah semua sekolah dikunjungi, truk kembali ke pangkalan
    // =========================================================================
    if (!m_schools.empty()) {
        int lastSchoolId = m_schools.back().id;
        RouteLeg returnLeg;
        returnLeg.legIndex = (int)m_schools.size();
        returnLeg.targetSchoolIndex = -1;
        returnLeg.destinationName = "SPPG (Dapur Pusat)";
        returnLeg.color = (Color){255, 185, 0, 255}; // Vibrant Amber/Gold matching SPPG
        returnLeg.isReturnToDepot = true;

        std::pair<int, int> key1 = {lastSchoolId, 0};
        std::pair<int, int> key2 = {0, lastSchoolId};

        if (pathCache.count(key1)) {
            returnLeg.path = pathCache[key1];
        } else if (pathCache.count(key2)) {
            returnLeg.path = pathCache[key2];
            std::reverse(returnLeg.path.gridPath.begin(), returnLeg.path.gridPath.end());
            std::reverse(returnLeg.path.worldWaypoints.begin(), returnLeg.path.worldWaypoints.end());
        } else {
            returnLeg.path = m_pathfinder.FindShortestPath(m_world, m_schools.back().gridPos, m_carStartGridPos, m_gridPathAlgo);
        }

        m_totalRouteDistance += returnLeg.path.totalDistanceWorld;
        m_totalNodesExplored += returnLeg.path.nodesExplored;
        m_totalExecutionTimeMs += returnLeg.path.executionTimeMs;

        if (returnLeg.path.found && returnLeg.path.gridPath.size() >= 2) {
            returnLeg.borderModel = m_pathfinder.BuildPathMeshModel(m_world, returnLeg.path.gridPath, 0.70f, 0.08f);
            returnLeg.pathModel = m_pathfinder.BuildPathMeshModel(m_world, returnLeg.path.gridPath, 0.48f, 0.14f);
            returnLeg.modelsLoaded = true;
        }

        m_routeLegs.push_back(returnLeg);
        m_optimizationLog += " → [Akhir] SPPG (Kembali ke Titik Awal)";
    }

    UpdateNavigationStatus();
}

void PathfinderState::UpdateNavigationStatus() {
    if (m_routeLegs.empty() || m_currentLegIndex >= (int)m_routeLegs.size()) {
        m_turnInstruction = "Selesai!";
        m_distanceToCurrentTarget = 0.0f;
        m_totalDistanceRemaining = 0.0f;
        return;
    }

    if (m_isUnloading) {
        m_turnInstruction = TextFormat("Bongkar Muatan MBG (%s)", m_schools[m_currentLegIndex].name.c_str());
        return;
    }

    const auto& curLeg = m_routeLegs[m_currentLegIndex];
    if (!curLeg.path.found || curLeg.path.worldWaypoints.size() < 2) {
        m_turnInstruction = "Mencari Rute...";
        m_distanceToCurrentTarget = 0.0f;
        return;
    }

    // Distance remaining on current leg
    int curIdx = (int)m_legDriveProgress;
    float distLeg = 0.0f;
    if (curIdx < (int)curLeg.path.worldWaypoints.size()) {
        distLeg += Vector3Distance(m_carCurrentPos, curLeg.path.worldWaypoints[curIdx]);
        for (size_t i = curIdx + 1; i < curLeg.path.worldWaypoints.size(); i++) {
            distLeg += Vector3Distance(curLeg.path.worldWaypoints[i - 1], curLeg.path.worldWaypoints[i]);
        }
    }
    m_distanceToCurrentTarget = distLeg;

    // Total distance across all remaining unvisited legs
    float totalRem = distLeg;
    for (size_t l = m_currentLegIndex + 1; l < m_routeLegs.size(); l++) {
        totalRem += m_routeLegs[l].path.totalDistanceWorld;
    }
    m_totalDistanceRemaining = totalRem;

    // Detect turn instruction along upcoming waypoints
    int lookAhead = curIdx + 1;
    if (lookAhead + 1 < (int)curLeg.path.worldWaypoints.size()) {
        Vector3 v1 = Vector3Subtract(curLeg.path.worldWaypoints[lookAhead], curLeg.path.worldWaypoints[curIdx]);
        Vector3 v2 = Vector3Subtract(curLeg.path.worldWaypoints[lookAhead + 1], curLeg.path.worldWaypoints[lookAhead]);

        float cross = v1.x * v2.z - v1.z * v2.x;
        if (curLeg.isReturnToDepot) {
            if (cross > 0.12f) m_turnInstruction = "Belok Kanan (Kembali ke SPPG)";
            else if (cross < -0.12f) m_turnInstruction = "Belok Kiri (Kembali ke SPPG)";
            else m_turnInstruction = "Lurus Terus (Kembali ke SPPG)";
        } else {
            if (cross > 0.12f) m_turnInstruction = "Belok Kanan";
            else if (cross < -0.12f) m_turnInstruction = "Belok Kiri";
            else m_turnInstruction = "Lurus Terus";
        }
    } else {
        if (curLeg.isReturnToDepot) {
            m_turnInstruction = "Mendekati Titik Awal SPPG";
        } else {
            m_turnInstruction = TextFormat("Mendekati %s", curLeg.destinationName.c_str());
        }
    }
}

void PathfinderState::UpdateCarDriving(float dt) {
    if (!m_isDriving || m_routeLegs.empty() || m_currentLegIndex >= (int)m_routeLegs.size()) {
        return;
    }

    // Handle unloading simulation at school
    if (m_isUnloading) {
        m_unloadTimer -= dt;
        if (m_unloadTimer <= 0.0f) {
            m_isUnloading = false;
            // Transition to next leg (could be next school OR return to SPPG!)
            if (m_currentLegIndex + 1 < (int)m_routeLegs.size()) {
                m_currentLegIndex++;
                m_legDriveProgress = 0.0f;
                if (m_routeLegs[m_currentLegIndex].isReturnToDepot) {
                    TraceLog(LOG_INFO, "Semua sekolah selesai! Mobil sedang kembali ke titik awal (SPPG)...");
                } else {
                    TraceLog(LOG_INFO, "Unloading done! Now driving to School %d...", m_currentLegIndex + 1);
                }
            } else {
                m_allDelivered = true;
                m_distanceToCurrentTarget = 0.0f;
                m_totalDistanceRemaining = 0.0f;
            }
        }
        UpdateNavigationStatus();
        return;
    }

    const auto& curLeg = m_routeLegs[m_currentLegIndex];
    if (!curLeg.path.found || curLeg.path.worldWaypoints.size() < 2) return;

    int curIdx = (int)m_legDriveProgress;
    int nextIdx = curIdx + 1;

    // Check if car reached current destination
    if (nextIdx >= (int)curLeg.path.worldWaypoints.size()) {
        m_carCurrentPos = curLeg.path.worldWaypoints.back();

        if (curLeg.isReturnToDepot) {
            // ARRIVED BACK AT STARTING POINT (SPPG)!
            m_allDelivered = true;
            m_distanceToCurrentTarget = 0.0f;
            m_totalDistanceRemaining = 0.0f;
            m_arrivalTimer += dt;
            if (m_arrivalTimer >= 4.0f) {
                RandomizePositions(); // Automatically starts next delivery mission!
            }
            UpdateNavigationStatus();
            return;
        }

        // Reached a school destination: mark reached & start meal unloading
        if (curLeg.targetSchoolIndex >= 0 && curLeg.targetSchoolIndex < (int)m_schools.size()) {
            m_schools[curLeg.targetSchoolIndex].reached = true;
        }

        m_isUnloading = true;
        m_unloadTimer = 1.5f;

        UpdateNavigationStatus();
        return;
    }

    Vector3 pA = curLeg.path.worldWaypoints[curIdx];
    Vector3 pB = curLeg.path.worldWaypoints[nextIdx];
    float segDist = Vector3Distance(pA, pB);
    if (segDist < 0.001f) segDist = 0.001f;

    float step = (m_driveSpeed * dt) / segDist;
    m_legDriveProgress += step;

    curIdx = (int)m_legDriveProgress;
    nextIdx = curIdx + 1;

    if (nextIdx < (int)curLeg.path.worldWaypoints.size()) {
        float frac = m_legDriveProgress - curIdx;
        pA = curLeg.path.worldWaypoints[curIdx];
        pB = curLeg.path.worldWaypoints[nextIdx];
        m_carCurrentPos = Vector3Lerp(pA, pB, frac);

        Vector3 diff = Vector3Subtract(pB, pA);
        float targetAngle = atan2f(diff.x, diff.z);

        // Smooth heading turn
        float angleDiff = targetAngle - m_carHeading;
        while (angleDiff > PI) angleDiff -= 2.0f * PI;
        while (angleDiff < -PI) angleDiff += 2.0f * PI;
        m_carHeading += angleDiff * 0.18f;
    } else {
        m_carCurrentPos = curLeg.path.worldWaypoints.back();
    }

    UpdateNavigationStatus();
}

void PathfinderState::UpdateCameraControls() {
    if (m_camMode == APP_CAM_FREE) {
        UpdateCamera(&m_camera, CAMERA_FREE);
    } else if (m_camMode == APP_CAM_FOLLOW_CAR) {
        // Google Maps 3D Turn-By-Turn Navigation Camera
        Vector3 offset = {-sinf(m_carHeading) * 14.0f, 9.5f, -cosf(m_carHeading) * 14.0f};
        Vector3 desiredCamPos = Vector3Add(m_carCurrentPos, offset);
        m_camera.position = Vector3Lerp(m_camera.position, desiredCamPos, 0.12f);

        Vector3 desiredTarget = Vector3Add(m_carCurrentPos, (Vector3){sinf(m_carHeading) * 5.0f, 0.5f, cosf(m_carHeading) * 5.0f});
        m_camera.target = Vector3Lerp(m_camera.target, desiredTarget, 0.14f);
    } else if (m_camMode == APP_CAM_FOLLOW_SCHOOL) {
        if (!m_schools.empty()) {
            Vector3 schPos = m_schools[m_camSchoolTargetIdx % m_schools.size()].worldPos;
            Vector3 desiredCamPos = Vector3Add(schPos, (Vector3){0, 22.0f, 22.0f});
            m_camera.position = Vector3Lerp(m_camera.position, desiredCamPos, 0.08f);
            m_camera.target = Vector3Lerp(m_camera.target, schPos, 0.1f);
        }
    }
}

void PathfinderState::Update() {
    float dt = GetFrameTime();
    m_pulseTimer += dt;

    // Hotkeys
    if (IsKeyPressed(KEY_TAB)) {
        // Toggle MBG Routing Algorithm between DP Modified and DP Baseline
        m_routingAlgorithm = (m_routingAlgorithm == ROUTING_DP_MODIFIED) ? ROUTING_DP_BASELINE : ROUTING_DP_MODIFIED;
        OptimizeAndBuildRoutes();
    }
    if (IsKeyPressed(KEY_R)) {
        RandomizePositions();
    }
    if (IsKeyPressed(KEY_SPACE)) {
        m_isDriving = !m_isDriving;
    }
    if (IsKeyPressed(KEY_C)) m_camMode = APP_CAM_FOLLOW_CAR;
    if (IsKeyPressed(KEY_S)) {
        m_camMode = APP_CAM_FOLLOW_SCHOOL;
        m_camSchoolTargetIdx = (m_camSchoolTargetIdx + 1) % m_schools.size();
    }
    if (IsKeyPressed(KEY_F)) m_camMode = APP_CAM_FREE;
    if (IsKeyPressed(KEY_L)) m_showLidar = !m_showLidar;
    if (IsKeyPressed(KEY_M)) {
        m_mapFullscreen = !m_mapFullscreen;
        if (!m_mapFullscreen) {
            m_mapZoom = 1.0f;
            m_mapPan = {0, 0};
        }
    }
    if (IsKeyPressed(KEY_ESCAPE) && m_mapFullscreen) {
        m_mapFullscreen = false;
        m_mapZoom = 1.0f;
        m_mapPan = {0, 0};
    }
    if (IsKeyPressed(KEY_H)) m_showMinimap = !m_showMinimap;

    // Adjust school count via keyboard '+' and '-' (3 to 5)
    if (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD)) {
        if (m_numSchools < MAX_SCHOOLS) {
            m_numSchools++;
            RandomizePositions();
        }
    }
    if (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT)) {
        if (m_numSchools > 3) {
            m_numSchools--;
            RandomizePositions();
        }
    }

    // Vehicle Driving & Navigation Status
    UpdateCarDriving(dt);

    // Cast LIDAR rays around vehicle
    if (m_showLidar) {
        m_lidarHits = m_world.CastLidarRays(m_carCurrentPos, m_carHeading, 24, 25.0f);
    }

    // Camera follow controls
    UpdateCameraControls();
}

void PathfinderState::DrawClean() {
    ClearBackground((Color){195, 225, 255, 255});
}

void PathfinderState::DrawDirty() {
    rlClearScreenBuffers();
    ClearBackground((Color){195, 225, 255, 255});

    // =========================================================================
    // 3D Scene Rendering
    // =========================================================================
    BeginMode3D(m_camera);
    rlDisableBackfaceCulling();

    // 1. Procedural 3D City with baked Ambient Occlusion
    m_world.Draw3D(m_showLidar);

    // 2. Draw Multi-School Colored Route Lines (Each leg has its own distinct color!)
    for (size_t i = 0; i < m_routeLegs.size(); i++) {
        const auto& leg = m_routeLegs[i];
        if (!leg.modelsLoaded) continue;

        // Crisp white underlay border
        DrawModel(leg.borderModel, {0, 0, 0}, 1.0f, (Color){255, 255, 255, 240});

        // Core ribbon in this school's unique color (Blue, Green, Orange, etc.)
        Color ribbonCol = leg.color;
        if (leg.targetSchoolIndex < m_currentLegIndex) {
            // Already visited legs are slightly softened
            ribbonCol = Fade(leg.color, 0.65f);
        }
        DrawModel(leg.pathModel, {0, 0, 0}, 1.0f, ribbonCol);
    }

    // 3. Draw SPPG (Dapur Pusat / Kitchen) 3D Marker Pin
    Vector3 sppgWorld = m_world.GridToWorld(m_carStartGridPos.x, m_carStartGridPos.y, 0.0f);
    DrawCircle3D(Vector3Add(sppgWorld, (Vector3){0, 0.06f, 0}), 2.2f, (Vector3){1, 0, 0}, 90.0f, Fade((Color){255, 190, 0, 255}, 0.5f));
    DrawCylinder(Vector3Add(sppgWorld, (Vector3){0, 1.0f, 0}), 0.2f, 0.2f, 2.0f, 12, (Color){240, 160, 0, 255});
    DrawSphere(Vector3Add(sppgWorld, (Vector3){0, 3.2f, 0}), 0.85f, (Color){255, 190, 0, 255});
    DrawSphere(Vector3Add(sppgWorld, (Vector3){0, 3.2f, 0}), 0.40f, WHITE);

    // 4. Draw Delivery Truck Model (Mobil MBG)
    // +180 degrees so truck headlights face forward in movement direction
    float truckScale = 0.022f;
    Vector3 truckRenderPos = m_carCurrentPos;
    DrawModelEx(m_truckModel, truckRenderPos, (Vector3){0, 1, 0}, (m_carHeading * RAD2DEG) + 180.0f,
                (Vector3){truckScale, truckScale, truckScale}, WHITE);

    // Halo ring under the truck in current leg color
    Color currentHaloColor = (!m_routeLegs.empty() && m_currentLegIndex < (int)m_routeLegs.size()) ?
                             m_routeLegs[m_currentLegIndex].color : LEG_COLORS[0];
    float pulseRadius = 0.40f + 0.12f * sinf(m_pulseTimer * 4.0f);
    DrawCircle3D(Vector3Add(truckRenderPos, (Vector3){0, 0.08f, 0}), pulseRadius, (Vector3){1, 0, 0}, 90.0f, Fade(currentHaloColor, 0.45f));
    DrawCircle3D(Vector3Add(truckRenderPos, (Vector3){0, 0.09f, 0}), 0.18f, (Vector3){1, 0, 0}, 90.0f, currentHaloColor);

    // 5. Draw School Models & Pin Markers for ALL schools (1, 2, 3...)
    float schoolScale = 0.0028f;
    for (size_t i = 0; i < m_schools.size(); i++) {
        const auto& sch = m_schools[i];
        Vector3 schRenderPos = sch.worldPos;

        // Draw 3D School Model
        DrawModelEx(m_schoolModel, schRenderPos, (Vector3){0, 1, 0}, 0.0f,
                    (Vector3){schoolScale, schoolScale, schoolScale}, WHITE);

        // Google Maps 3D Pin Drop Marker in this school's matching color!
        float pinHoverY = 4.2f + 0.3f * sinf(m_pulseTimer * 3.0f + (float)i * 1.5f);
        Vector3 pinHeadPos = Vector3Add(schRenderPos, (Vector3){0, pinHoverY, 0});
        Vector3 pinTipPos  = Vector3Add(schRenderPos, (Vector3){0, pinHoverY - 1.2f, 0});

        Color pinCol = sch.reached ? (Color){100, 230, 120, 255} : sch.color;

        // Pin head sphere & white center
        DrawSphere(pinHeadPos, 0.90f, pinCol);
        DrawSphere(pinHeadPos, 0.38f, WHITE);
        // Pointed cone
        DrawCylinder(pinTipPos, 0.1f, 0.75f, 1.2f, 12, pinCol);

        // Ground target halo ring
        float ringRadius = 2.0f + 0.4f * sinf(m_pulseTimer * 3.0f + (float)i);
        DrawCircle3D(Vector3Add(schRenderPos, (Vector3){0, 0.05f, 0}), ringRadius, (Vector3){1, 0, 0}, 90.0f, Fade(pinCol, 0.35f));
    }

    // 6. Draw 3D LIDAR sensor rays cast from vehicle
    if (m_showLidar) {
        Vector3 sensorOrigin = Vector3Add(m_carCurrentPos, (Vector3){0, 0.4f, 0});
        for (const auto& hit : m_lidarHits) {
            Color rayCol = hit.hit ? (Color){240, 50, 50, 220} : (Color){60, 220, 80, 160};
            DrawLine3D(sensorOrigin, hit.point, rayCol);
            if (hit.hit) {
                DrawSphere(hit.point, 0.14f, RED);
            }
        }
    }

    rlEnableBackfaceCulling();
    EndMode3D();

    // 2D UI & Google Maps HUD Overlay
    DrawUI();
}

void PathfinderState::DrawUI() {
    int screenW = GetScreenWidth();
    int screenH = GetScreenHeight();

    // =========================================================================
    // 1. Google Maps Navigation Header (Turn-by-Turn Card)
    // Dynamic color matching the current target school or return leg!
    // =========================================================================
    int navCardX = 16;
    int navCardY = 14;
    int navCardW = 420;
    int navCardH = m_allDelivered ? 96 : 116;

    bool isReturningToDepot = (!m_routeLegs.empty() && m_currentLegIndex < (int)m_routeLegs.size() && m_routeLegs[m_currentLegIndex].isReturnToDepot);

    Color curHeaderBg = m_allDelivered ? (Color){24, 134, 75, 250} :
                        (m_isUnloading ? (Color){210, 120, 10, 250} :
                        (isReturningToDepot ? (Color){220, 155, 0, 250} :
                        ((!m_routeLegs.empty() && m_currentLegIndex < (int)m_routeLegs.size()) ?
                         Fade(m_routeLegs[m_currentLegIndex].color, 0.96f) : (Color){26, 115, 232, 250})));

    DrawRectangleRounded((Rectangle){(float)navCardX, (float)navCardY, (float)navCardW, (float)navCardH}, 0.12f, 6, curHeaderBg);
    DrawRectangleRoundedLinesEx((Rectangle){(float)navCardX, (float)navCardY, (float)navCardW, (float)navCardH}, 0.12f, 6, 2.0f, Fade(WHITE, 0.45f));

    if (m_allDelivered) {
        DrawTextCustom("MISI MBG SELESAI!", navCardX + 22, navCardY + 18, 20, WHITE, FONT_BOLD);
        DrawTextCustom(TextFormat("Seluruh %d sekolah terlayani & mobil kembali ke titik awal (SPPG)!", (int)m_schools.size()),
                       navCardX + 22, navCardY + 48, 12.5f, (Color){220, 255, 230, 255}, FONT_REGULAR);
        DrawTextCustom("Memulai misi baru dalam 4 detik...", navCardX + 22, navCardY + 68, 11.5f, (Color){200, 240, 210, 255}, FONT_REGULAR);
    } else if (isReturningToDepot) {
        // Return to depot header
        const char* retIcon = "↑";
        if (m_turnInstruction == "Belok Kanan") retIcon = "→";
        else if (m_turnInstruction == "Belok Kiri") retIcon = "←";
        DrawTextCustom(retIcon, navCardX + 26, navCardY + 18, 28, WHITE, FONT_BOLD);
        DrawTextCustom(m_turnInstruction.c_str(), navCardX + 70, navCardY + 12, 18, WHITE, FONT_BOLD);
        DrawTextCustom("Langkah Akhir: Kembali ke Titik Awal (SPPG)", navCardX + 70, navCardY + 36, 13, WHITE, FONT_SEMIBOLD);

        float metersCurrent = m_distanceToCurrentTarget * 4.0f;
        int etaSeconds = (int)(m_distanceToCurrentTarget / m_driveSpeed);
        DrawTextCustom(TextFormat("Sisa Jarak ke SPPG: %.0f m (%d dtk) • Menuju Pangkalan", metersCurrent, etaSeconds),
                       navCardX + 70, navCardY + 58, 12, (Color){255, 255, 180, 255}, FONT_REGULAR);
        DrawTextCustom("Pengiriman makanan bergizi di seluruh sekolah telah selesai",
                       navCardX + 70, navCardY + 78, 11, (Color){255, 245, 220, 255}, FONT_REGULAR);

        DrawCircle(navCardX + navCardW - 20, navCardY + 20, 6, (Color){255, 230, 0, 255});
        DrawTextCustom("KEMBALI", navCardX + navCardW - 74, navCardY + 15, 9, BLACK, FONT_BOLD);
    } else {
        // Navigation icon
        const char* iconStr = "↑";
        if (m_turnInstruction == "Belok Kanan") iconStr = "→";
        else if (m_turnInstruction == "Belok Kiri") iconStr = "←";
        else if (m_isUnloading) iconStr = "▶";
        DrawTextCustom(iconStr, navCardX + 26, navCardY + 18, 28, WHITE, FONT_BOLD);

        // Turn instruction
        DrawTextCustom(m_turnInstruction.c_str(), navCardX + 70, navCardY + 12, 18, WHITE, FONT_BOLD);

        // Destination with school step indicator
        int targetId = m_currentLegIndex + 1;
        const char* colName = (m_currentLegIndex < 6) ? LEG_COLOR_NAMES[m_currentLegIndex] : "Warna";
        const char* schName = (m_currentLegIndex < (int)m_schools.size()) ? m_schools[m_currentLegIndex].name.c_str() : "Sekolah";

        DrawTextCustom(TextFormat("Tujuan: %s (Stop %d dari %d • Jalur %s)", schName, targetId, (int)m_schools.size(), colName),
                       navCardX + 70, navCardY + 36, 13, WHITE, FONT_SEMIBOLD);

        // Distance & ETA
        float metersCurrent = m_distanceToCurrentTarget * 4.0f;
        float metersTotal   = m_totalDistanceRemaining * 4.0f;
        int etaSeconds = (int)(m_distanceToCurrentTarget / m_driveSpeed);

        DrawTextCustom(TextFormat("Target: %.0f m (%d dtk) • Total Sisa: %.0f m", metersCurrent, etaSeconds, metersTotal),
                       navCardX + 70, navCardY + 58, 12, (Color){255, 255, 180, 255}, FONT_REGULAR);

        // Time window indicator
        if (m_currentLegIndex < (int)m_schools.size()) {
            const auto& w = m_schools[m_currentLegIndex].window;
            DrawTextCustom(TextFormat("Window Pengantaran: [%d - %d menit] • Bongkar: 5m", w.start, w.end),
                           navCardX + 70, navCardY + 78, 11, (Color){230, 245, 255, 255}, FONT_REGULAR);
        }

        // Pulsing status
        DrawCircle(navCardX + navCardW - 20, navCardY + 20, 6, m_isUnloading ? ORANGE : RED);
        DrawTextCustom(m_isUnloading ? "BONGKAR" : "AUTO", navCardX + navCardW - 68, navCardY + 15, 9, WHITE, FONT_BOLD);
    }

    // =========================================================================
    // 2. Control Toolbar (Top Right / Middle)
    // =========================================================================
    int toolX = navCardX + navCardW + 14;
    int toolY = 14;

    bool isModified = (m_routingAlgorithm == ROUTING_DP_MODIFIED);
    StateButton btnMod      = {{(float)toolX, (float)toolY, 138, 36}, "DP Modified [TAB]", isModified};
    StateButton btnBaseAlgo = {{(float)toolX + 144, (float)toolY, 120, 36}, "DP Baseline", !isModified};
    StateButton btnRandomize= {{(float)toolX + 270, (float)toolY, 110, 36}, "Acak Rute [R]", false};
    StateButton btnDrive    = {{(float)toolX + 386, (float)toolY, 105, 36}, m_isDriving ? "Jeda [SPACE]" : "Auto Jalan", m_isDriving};
    StateButton btnMap      = {{(float)toolX + 497, (float)toolY, 135, 36}, m_mapFullscreen ? "Tutup Peta [M]" : "Peta Penuh [M]", m_mapFullscreen};

    // Row 2 of controls (Schools count + Camera toggle)
    int row2Y = toolY + 42;
    StateButton btnMinusSchool = {{(float)toolX, (float)row2Y, 34, 32}, "[-]", false};
    StateButton btnSchoolCount = {{(float)toolX + 38, (float)row2Y, 86, 32}, TextFormat("%d Sekolah", m_numSchools), true};
    StateButton btnPlusSchool  = {{(float)toolX + 128, (float)row2Y, 34, 32}, "[+]", false};

    StateButton btnCam = {{(float)toolX + 168, (float)row2Y, 115, 32},
                          (m_camMode == APP_CAM_FOLLOW_CAR) ? "Cam: Truk [C]" : "Cam: Sek [S]",
                          m_camMode == APP_CAM_FOLLOW_CAR};

    Color btnBase = {240, 244, 250, 255};
    Color btnHover = {215, 230, 250, 255};

    btnMod.Draw(m_fontSemiBold, btnBase, btnHover, LEG_COLORS[0], isModified ? WHITE : BLACK);
    btnBaseAlgo.Draw(m_fontSemiBold, btnBase, btnHover, LEG_COLORS[2], !isModified ? WHITE : BLACK);
    btnRandomize.Draw(m_fontSemiBold, btnBase, btnHover, LEG_COLORS[1], BLACK);
    btnDrive.Draw(m_fontSemiBold, btnBase, btnHover, {230, 60, 60, 255}, m_isDriving ? WHITE : BLACK);
    btnMap.Draw(m_fontSemiBold, btnBase, btnHover, (Color){30, 144, 255, 255}, m_mapFullscreen ? WHITE : BLACK);

    btnMinusSchool.Draw(m_fontBold, btnBase, btnHover, {200, 200, 210, 255}, BLACK);
    btnSchoolCount.Draw(m_fontSemiBold, btnBase, btnHover, Fade(LEG_COLORS[1], 0.85f), WHITE);
    btnPlusSchool.Draw(m_fontBold, btnBase, btnHover, {200, 200, 210, 255}, BLACK);
    btnCam.Draw(m_fontSemiBold, btnBase, btnHover, LEG_DARK_COLORS[0], WHITE);

    // Toolbar interactions
    if (btnMod.IsClicked() && m_routingAlgorithm != ROUTING_DP_MODIFIED) {
        m_routingAlgorithm = ROUTING_DP_MODIFIED;
        OptimizeAndBuildRoutes();
    }
    if (btnBaseAlgo.IsClicked() && m_routingAlgorithm != ROUTING_DP_BASELINE) {
        m_routingAlgorithm = ROUTING_DP_BASELINE;
        OptimizeAndBuildRoutes();
    }
    if (btnRandomize.IsClicked()) {
        RandomizePositions();
    }
    if (btnDrive.IsClicked()) {
        m_isDriving = !m_isDriving;
    }
    if (btnMap.IsClicked()) {
        m_mapFullscreen = !m_mapFullscreen;
        if (!m_mapFullscreen) {
            m_mapZoom = 1.0f;
            m_mapPan = {0, 0};
        }
    }
    if (btnMinusSchool.IsClicked() && m_numSchools > 3) {
        m_numSchools--;
        RandomizePositions();
    }
    if (btnPlusSchool.IsClicked() && m_numSchools < MAX_SCHOOLS) {
        m_numSchools++;
        RandomizePositions();
    }
    if (btnCam.IsClicked()) {
        m_camMode = (m_camMode == APP_CAM_FOLLOW_CAR) ? APP_CAM_FOLLOW_SCHOOL : APP_CAM_FOLLOW_CAR;
    }

    // =========================================================================
    // 3. MBG Routing Optimization Card (Left Side)
    // Displays the optimized sequence, Time Windows, and algorithm metrics
    // =========================================================================
    int cardX = 16;
    int cardY = navCardY + navCardH + 12;
    int cardW = 420;
    int cardH = 295;

    DrawRectangleRounded((Rectangle){(float)cardX, (float)cardY, (float)cardW, (float)cardH}, 0.08f, 6, Fade((Color){20, 25, 35, 255}, 0.92f));
    DrawRectangleRoundedLinesEx((Rectangle){(float)cardX, (float)cardY, (float)cardW, (float)cardH}, 0.08f, 6, 1.5f, Fade(WHITE, 0.25f));

    DrawTextCustom("Sistem Routing MBG (Makan Bergizi Gratis)", cardX + 16, cardY + 12, 14, WHITE, FONT_BOLD);
    DrawLine(cardX + 16, cardY + 32, cardX + cardW - 16, cardY + 32, Fade(WHITE, 0.2f));

    // Algorithm & Feasibility badge
    const char* algoName = isModified ? "DP-Modified (Optimized)" : "DP-Baseline (Dijkstra+DP)";
    Color algoCol = isModified ? (Color){100, 210, 255, 255} : (Color){255, 200, 100, 255};
    DrawTextCustom(TextFormat("Algoritma: %s", algoName), cardX + 16, cardY + 37, 11.5f, algoCol, FONT_SEMIBOLD);

    const char* feasStr = m_routeFeasible ? "RUTE LAYAK (Feasible)" : "MELEBIHI WINDOW WAKTU";
    Color feasCol = m_routeFeasible ? (Color){100, 240, 130, 255} : (Color){255, 80, 80, 255};
    DrawTextCustom(feasStr, cardX + cardW - MeasureTextCustom(feasStr, 11.5f, FONT_BOLD) - 16, cardY + 37, 11.5f, feasCol, FONT_BOLD);

    // Sequence header
    DrawTextCustom("Urutan Kunjungan Optimal:", cardX + 16, cardY + 57, 11.5f, LIGHTGRAY, FONT_SEMIBOLD);

    // SPPG Central Kitchen start badge
    DrawRectangleRounded((Rectangle){(float)cardX + 16, (float)cardY + 76, 48, 18}, 0.3f, 4, (Color){255, 180, 0, 255});
    DrawTextCustom("SPPG", cardX + 22, cardY + 78, 10.5f, BLACK, FONT_BOLD);
    DrawTextCustom("Dapur Pusat MBG (Titik Awal)", cardX + 70, cardY + 78, 11.5f, (Color){255, 220, 140, 255}, FONT_REGULAR);

    int rowY = cardY + 100;
    for (size_t i = 0; i < m_schools.size(); i++) {
        const auto& sch = m_schools[i];
        bool isCurrent = ((int)i == m_currentLegIndex && !m_allDelivered);
        bool isDone = sch.reached;

        // Colored badge
        DrawRectangleRounded((Rectangle){(float)cardX + 16, (float)rowY, 24, 18}, 0.3f, 4, sch.color);
        DrawTextCustom(TextFormat("%d", (int)i + 1), cardX + 24, rowY + 2, 11, WHITE, FONT_BOLD);

        // Status string
        const char* statusStr = isDone ? "Selesai" : (isCurrent ? (m_isUnloading ? "Bongkar" : "▶ Menuju") : "Menunggu");
        Color statusColor = isDone ? (Color){100, 240, 130, 255} : (isCurrent ? YELLOW : (Color){180, 190, 205, 255});

        float legMeters = (i < m_routeLegs.size()) ? (m_routeLegs[i].path.totalDistanceWorld * 4.0f) : 0.0f;
        DrawTextCustom(TextFormat("%s • [%d-%dm] • %.0fm • %s", sch.name.c_str(), sch.window.start, sch.window.end, legMeters, statusStr),
                       cardX + 46, rowY + 2, 11.5f, statusColor, FONT_REGULAR);

        rowY += 21;
    }

    // Final Step (Kembali ke SPPG)
    bool isReturnLegCurrent = isReturningToDepot && !m_allDelivered;
    const char* returnStatusStr = m_allDelivered ? "Tiba di SPPG" : (isReturnLegCurrent ? "▶ Sedang Kembali" : "Menunggu");
    Color returnStatusCol = m_allDelivered ? (Color){100, 240, 130, 255} : (isReturnLegCurrent ? (Color){255, 220, 120, 255} : (Color){180, 190, 205, 255});

    float returnMeters = (!m_routeLegs.empty() && m_routeLegs.back().isReturnToDepot) ?
                         (m_routeLegs.back().path.totalDistanceWorld * 4.0f) : 0.0f;

    DrawRectangleRounded((Rectangle){(float)cardX + 16, (float)rowY, 24, 18}, 0.3f, 4, (Color){255, 180, 0, 255});
    DrawTextCustom("0", cardX + 24, rowY + 2, 11, BLACK, FONT_BOLD);
    DrawTextCustom(TextFormat("Kembali ke SPPG (Titik Awal) • %.0fm • %s", returnMeters, returnStatusStr),
                   cardX + 46, rowY + 2, 11.5f, returnStatusCol, FONT_REGULAR);

    // Performance & Metrics summary
    DrawLine(cardX + 16, cardY + cardH - 58, cardX + cardW - 16, cardY + cardH - 58, Fade(WHITE, 0.15f));

    DrawTextCustom(TextFormat("Total Cost: %d menit • Total Jarak: %.1f m", m_totalCostMinutes, m_totalRouteDistance * 4.0f),
                   cardX + 16, cardY + cardH - 46, 11.5f, WHITE, FONT_SEMIBOLD);

    DrawTextCustom(TextFormat("States Explored: %d • Runtime: %.1f µs", m_statesExplored, m_optimizationTimeUs),
                   cardX + 16, cardY + cardH - 26, 11.5f, (Color){140, 230, 255, 255}, FONT_REGULAR);

    // =========================================================================
    // 4. Real-Time 2D Minimap & Fullscreen Map Overlay
    // =========================================================================
    if (m_showMinimap && !m_mapFullscreen) {
        DrawMinimap(screenW, screenH);
    }

    if (m_mapFullscreen) {
        DrawFullscreenMap(screenW, screenH);
    }

    // Top Right FPS & Engine info
    Fumbo::Instance().DrawFPS(screenW - 105, 16);
}

void PathfinderState::DrawMinimap(int screenW, int screenH) {
    if (m_world.GetMapTexture().id == 0) return;

    Texture2D mapTex = m_world.GetMapTexture();
    float mmW = m_minimapWidth;
    float aspect = (float)m_world.GetGridHeight() / (float)m_world.GetGridWidth();
    float mmH = mmW * aspect;
    float mmX = (float)screenW - mmW - 16.0f;
    float mmY = (float)screenH - mmH - 16.0f;

    // Interactive resizing via mouse wheel hover over minimap
    Vector2 mouse = GetMousePosition();
    Rectangle mmBox = {mmX - 6.0f, mmY - 30.0f, mmW + 12.0f, mmH + 36.0f};
    if (CheckCollisionPointRec(mouse, mmBox)) {
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) {
            m_minimapWidth = std::clamp(m_minimapWidth + wheel * 30.0f, 240.0f, 650.0f);
            mmW = m_minimapWidth;
            mmH = mmW * aspect;
            mmX = (float)screenW - mmW - 16.0f;
            mmY = (float)screenH - mmH - 16.0f;
        }
    }

    // Outer card container
    DrawRectangleRounded((Rectangle){mmX - 4.0f, mmY - 28.0f, mmW + 8.0f, mmH + 34.0f}, 0.04f, 4, Fade(BLACK, 0.90f));
    DrawRectangleRoundedLinesEx((Rectangle){mmX - 4.0f, mmY - 28.0f, mmW + 8.0f, mmH + 34.0f}, 0.04f, 4, 1.5f, Fade(WHITE, 0.35f));

    // Header label
    DrawTextCustom("Peta MBG (Scroll: Resize)", mmX + 6.0f, mmY - 22.0f, 11.5f, WHITE, FONT_SEMIBOLD);

    // Header control buttons: [-], [+], and [Fullscreen / Full]
    Rectangle btnMinusRect = {mmX + mmW - 80.0f, mmY - 25.0f, 22.0f, 18.0f};
    Rectangle btnPlusRect  = {mmX + mmW - 54.0f, mmY - 25.0f, 22.0f, 18.0f};
    Rectangle btnFullRect  = {mmX + mmW - 28.0f, mmY - 25.0f, 28.0f, 18.0f};

    bool hoverMinus = CheckCollisionPointRec(mouse, btnMinusRect);
    bool hoverPlus  = CheckCollisionPointRec(mouse, btnPlusRect);
    bool hoverFull  = CheckCollisionPointRec(mouse, btnFullRect);

    DrawRectangleRounded(btnMinusRect, 0.25f, 2, hoverMinus ? (Color){100, 115, 140, 255} : (Color){50, 60, 75, 255});
    DrawTextCustom("-", btnMinusRect.x + 8, btnMinusRect.y + 1, 12, WHITE, FONT_BOLD);

    DrawRectangleRounded(btnPlusRect, 0.25f, 2, hoverPlus ? (Color){100, 115, 140, 255} : (Color){50, 60, 75, 255});
    DrawTextCustom("+", btnPlusRect.x + 6, btnPlusRect.y + 1, 12, WHITE, FONT_BOLD);

    DrawRectangleRounded(btnFullRect, 0.25f, 2, hoverFull ? (Color){30, 144, 255, 255} : (Color){20, 95, 190, 255});
    DrawTextCustom("Full", btnFullRect.x + 4, btnFullRect.y + 2, 9.5f, WHITE, FONT_BOLD);

    if (hoverMinus && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        m_minimapWidth = std::clamp(m_minimapWidth - 40.0f, 240.0f, 650.0f);
    }
    if (hoverPlus && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        m_minimapWidth = std::clamp(m_minimapWidth + 40.0f, 240.0f, 650.0f);
    }
    if (hoverFull && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        m_mapFullscreen = true;
    }

    // Map Image Texture
    DrawTexturePro(mapTex,
                   (Rectangle){0.0f, 0.0f, (float)mapTex.width, (float)mapTex.height},
                   (Rectangle){mmX, mmY, mmW, mmH},
                   (Vector2){0, 0}, 0.0f, WHITE);

    // Draw each colored route leg on the minimap
    for (const auto& leg : m_routeLegs) {
        if (!leg.path.found || leg.path.gridPath.size() < 2) continue;

        // White underlay border
        for (size_t i = 1; i < leg.path.gridPath.size(); i++) {
            float x1 = mmX + (leg.path.gridPath[i - 1].x / (float)m_world.GetGridWidth()) * mmW;
            float y1 = mmY + (leg.path.gridPath[i - 1].y / (float)m_world.GetGridHeight()) * mmH;
            float x2 = mmX + (leg.path.gridPath[i].x / (float)m_world.GetGridWidth()) * mmW;
            float y2 = mmY + (leg.path.gridPath[i].y / (float)m_world.GetGridHeight()) * mmH;
            DrawLineEx((Vector2){x1, y1}, (Vector2){x2, y2}, 4.0f, WHITE);
        }
        // Leg colored line
        for (size_t i = 1; i < leg.path.gridPath.size(); i++) {
            float x1 = mmX + (leg.path.gridPath[i - 1].x / (float)m_world.GetGridWidth()) * mmW;
            float y1 = mmY + (leg.path.gridPath[i - 1].y / (float)m_world.GetGridHeight()) * mmH;
            float x2 = mmX + (leg.path.gridPath[i].x / (float)m_world.GetGridWidth()) * mmW;
            float y2 = mmY + (leg.path.gridPath[i].y / (float)m_world.GetGridHeight()) * mmH;
            DrawLineEx((Vector2){x1, y1}, (Vector2){x2, y2}, 2.6f, leg.color);
        }
    }

    // Draw SPPG Pin (Dapur Pusat) on minimap
    float sppgMMX = mmX + (m_carStartGridPos.x / (float)m_world.GetGridWidth()) * mmW;
    float sppgMMY = mmY + (m_carStartGridPos.y / (float)m_world.GetGridHeight()) * mmH;
    DrawCircle((int)sppgMMX, (int)sppgMMY, 8, (Color){255, 180, 0, 255});
    DrawCircle((int)sppgMMX, (int)sppgMMY, 4, WHITE);
    DrawTextCustom("SPPG", (int)sppgMMX - 12, (int)sppgMMY - 14, 9, (Color){255, 230, 100, 255}, FONT_BOLD);

    // Draw all school pins on minimap
    for (size_t i = 0; i < m_schools.size(); i++) {
        const auto& sch = m_schools[i];
        float schMMX = mmX + (sch.gridPos.x / (float)m_world.GetGridWidth()) * mmW;
        float schMMY = mmY + (sch.gridPos.y / (float)m_world.GetGridHeight()) * mmH;

        DrawCircle((int)schMMX, (int)schMMY, 7, sch.color);
        DrawCircle((int)schMMX, (int)schMMY, 3, WHITE);
        DrawTextCustom(TextFormat("%d", (int)i + 1), (int)schMMX - 3, (int)schMMY - 12, 10, WHITE, FONT_BOLD);
    }

    // Draw car current position on minimap
    Vector2 curCarGrid = m_world.WorldToGrid(m_carCurrentPos);
    float carMMX = mmX + (curCarGrid.x / (float)m_world.GetGridWidth()) * mmW;
    float carMMY = mmY + (curCarGrid.y / (float)m_world.GetGridHeight()) * mmH;

    DrawCircle((int)carMMX, (int)carMMY, 8 + 3 * sinf(m_pulseTimer * 5.0f), Fade(RED, 0.5f));
    DrawCircle((int)carMMX, (int)carMMY, 5, WHITE);
    DrawCircle((int)carMMX, (int)carMMY, 3, RED);
}

void PathfinderState::DrawFullscreenMap(int screenW, int screenH) {
    if (m_world.GetMapTexture().id == 0) return;

    Texture2D mapTex = m_world.GetMapTexture();

    // 1. Dark semi-transparent backdrop over entire screen
    DrawRectangle(0, 0, screenW, screenH, Fade(BLACK, 0.88f));

    // 2. Fullscreen Modal Frame (centered with margin)
    int marginX = 28;
    int marginY = 18;
    int modalW = screenW - marginX * 2;
    int modalH = screenH - marginY * 2;
    int modalX = marginX;
    int modalY = marginY;

    // Modal Background & Border
    DrawRectangleRounded((Rectangle){(float)modalX, (float)modalY, (float)modalW, (float)modalH}, 0.02f, 6, (Color){18, 22, 32, 255});
    DrawRectangleRoundedLinesEx((Rectangle){(float)modalX, (float)modalY, (float)modalW, (float)modalH}, 0.02f, 6, 2.0f, (Color){45, 125, 235, 200});

    // 3. Header Bar
    DrawRectangle((float)modalX + 2, (float)modalY + 2, (float)modalW - 4, 48, (Color){28, 35, 50, 255});
    DrawLine(modalX + 16, modalY + 50, modalX + modalW - 16, modalY + 50, Fade(WHITE, 0.2f));

    DrawTextCustom("PETA DISTRIBUSI RUTE MBG (RESOLUSI PENUH)", modalX + 18, modalY + 11, 15, WHITE, FONT_BOLD);
    DrawTextCustom(TextFormat("Resolusi Asli: %dx%d • Zoom: %.1fx • Scroll: Zoom • Drag: Geser Peta",
                              mapTex.width, mapTex.height, m_mapZoom),
                   modalX + 18, modalY + 31, 11, (Color){160, 205, 250, 255}, FONT_REGULAR);

    // Header Buttons: Reset Zoom & Close
    Vector2 mouse = GetMousePosition();

    Rectangle btnResetRect = {(float)modalX + modalW - 250, (float)modalY + 10, 120, 30};
    Rectangle btnCloseRect = {(float)modalX + modalW - 120, (float)modalY + 10, 105, 30};

    bool hoverReset = CheckCollisionPointRec(mouse, btnResetRect);
    bool hoverClose = CheckCollisionPointRec(mouse, btnCloseRect);

    DrawRectangleRounded(btnResetRect, 0.25f, 4, hoverReset ? (Color){60, 80, 110, 255} : (Color){40, 50, 70, 255});
    DrawRectangleRoundedLinesEx(btnResetRect, 0.25f, 4, 1.2f, Fade(WHITE, 0.3f));
    DrawTextCustom("100% Reset Zoom", btnResetRect.x + 12, btnResetRect.y + 7, 11, WHITE, FONT_SEMIBOLD);

    DrawRectangleRounded(btnCloseRect, 0.25f, 4, hoverClose ? (Color){220, 50, 50, 255} : (Color){180, 40, 40, 255});
    DrawRectangleRoundedLinesEx(btnCloseRect, 0.25f, 4, 1.2f, Fade(WHITE, 0.4f));
    DrawTextCustom("Tutup [ESC/M]", btnCloseRect.x + 12, btnCloseRect.y + 7, 11, WHITE, FONT_BOLD);

    if (hoverReset && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        m_mapZoom = 1.0f;
        m_mapPan = {0, 0};
    }
    if (hoverClose && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        m_mapFullscreen = false;
        m_mapZoom = 1.0f;
        m_mapPan = {0, 0};
    }

    // 4. Map Viewport inside modal
    int viewX = modalX + 14;
    int viewY = modalY + 54;
    int viewW = modalW - 28;
    int viewH = modalH - 108; // Leaves space for bottom legend bar

    DrawRectangle(viewX, viewY, viewW, viewH, (Color){10, 12, 18, 255});

    // Compute base dimensions preserving map aspect ratio
    float mapAspect = (float)m_world.GetGridWidth() / (float)m_world.GetGridHeight();
    float viewAspect = (float)viewW / (float)viewH;
    float baseW, baseH;
    if (viewAspect > mapAspect) {
        baseH = (float)viewH;
        baseW = baseH * mapAspect;
    } else {
        baseW = (float)viewW;
        baseH = baseW / mapAspect;
    }

    float cx = (float)viewX + (float)viewW * 0.5f;
    float cy = (float)viewY + (float)viewH * 0.5f;
    float curW = baseW * m_mapZoom;
    float curH = baseH * m_mapZoom;
    float mapLeft = cx - curW * 0.5f + m_mapPan.x;
    float mapTop  = cy - curH * 0.5f + m_mapPan.y;

    // Viewport mouse interactions: Zoom & Pan
    Rectangle viewRec = {(float)viewX, (float)viewY, (float)viewW, (float)viewH};
    if (CheckCollisionPointRec(mouse, viewRec)) {
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) {
            float oldZoom = m_mapZoom;
            m_mapZoom = std::clamp(m_mapZoom + wheel * 0.25f, 1.0f, 6.0f);
            if (m_mapZoom <= 1.0f) {
                m_mapZoom = 1.0f;
                m_mapPan = {0, 0};
            } else {
                float ratio = m_mapZoom / oldZoom;
                m_mapPan.x = (m_mapPan.x - (mouse.x - cx)) * ratio + (mouse.x - cx);
                m_mapPan.y = (m_mapPan.y - (mouse.y - cy)) * ratio + (mouse.y - cy);
            }
        }

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
            m_isPanningMap = true;
            m_panStartMouse = mouse;
            m_panStartOffset = m_mapPan;
        }
    }

    if (m_isPanningMap) {
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) || IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
            m_mapPan.x = m_panStartOffset.x + (mouse.x - m_panStartMouse.x);
            m_mapPan.y = m_panStartOffset.y + (mouse.y - m_panStartMouse.y);
        } else {
            m_isPanningMap = false;
        }
    }

    // 5. Scissor mode: Clip map rendering inside viewport
    BeginScissorMode(viewX, viewY, viewW, viewH);

    // Draw Map Texture
    DrawTexturePro(mapTex,
                   (Rectangle){0.0f, 0.0f, (float)mapTex.width, (float)mapTex.height},
                   (Rectangle){mapLeft, mapTop, curW, curH},
                   (Vector2){0, 0}, 0.0f, WHITE);

    auto g2s = [&](Vector2 gp) -> Vector2 {
        return (Vector2){
            mapLeft + (gp.x / (float)m_world.GetGridWidth()) * curW,
            mapTop  + (gp.y / (float)m_world.GetGridHeight()) * curH
        };
    };

    // Draw Route Lines with dynamic thickness based on zoom
    float underWidth = std::clamp(6.0f * sqrtf(m_mapZoom), 5.0f, 14.0f);
    float coreWidth  = std::clamp(3.5f * sqrtf(m_mapZoom), 3.0f, 9.0f);

    for (const auto& leg : m_routeLegs) {
        if (!leg.path.found || leg.path.gridPath.size() < 2) continue;

        // White underlay border
        for (size_t i = 1; i < leg.path.gridPath.size(); i++) {
            Vector2 p1 = g2s(leg.path.gridPath[i - 1]);
            Vector2 p2 = g2s(leg.path.gridPath[i]);
            DrawLineEx(p1, p2, underWidth, WHITE);
        }

        // Core colored leg ribbon
        for (size_t i = 1; i < leg.path.gridPath.size(); i++) {
            Vector2 p1 = g2s(leg.path.gridPath[i - 1]);
            Vector2 p2 = g2s(leg.path.gridPath[i]);
            DrawLineEx(p1, p2, coreWidth, leg.color);
        }
    }

    // Draw SPPG Depot (Dapur Pusat) Pin & Label
    Vector2 sppgPos = g2s(m_carStartGridPos);
    DrawCircle((int)sppgPos.x, (int)sppgPos.y, 14, (Color){255, 180, 0, 255});
    DrawCircle((int)sppgPos.x, (int)sppgPos.y, 9, WHITE);
    DrawCircle((int)sppgPos.x, (int)sppgPos.y, 5, (Color){220, 140, 0, 255});

    const char* sppgLabel = "SPPG (Dapur Pusat MBG)";
    float sppgLblW = MeasureTextCustom(sppgLabel, 11, FONT_BOLD) + 14;
    DrawRectangleRounded((Rectangle){sppgPos.x - sppgLblW * 0.5f, sppgPos.y - 34, sppgLblW, 20}, 0.3f, 4, Fade(BLACK, 0.88f));
    DrawRectangleRoundedLinesEx((Rectangle){sppgPos.x - sppgLblW * 0.5f, sppgPos.y - 34, sppgLblW, 20}, 0.3f, 4, 1.2f, (Color){255, 190, 0, 255});
    DrawTextCustom(sppgLabel, sppgPos.x - sppgLblW * 0.5f + 7, sppgPos.y - 30, 11, (Color){255, 230, 100, 255}, FONT_BOLD);

    // Draw School Pins with full informational floating pills
    for (size_t i = 0; i < m_schools.size(); i++) {
        const auto& sch = m_schools[i];
        Vector2 schPos = g2s(sch.gridPos);

        DrawCircle((int)schPos.x, (int)schPos.y, 13, sch.color);
        DrawCircle((int)schPos.x, (int)schPos.y, 7, WHITE);
        DrawTextCustom(TextFormat("%d", (int)i + 1), schPos.x - 4, schPos.y - 7, 12, BLACK, FONT_BOLD);

        // Floating pill: Stop #, School Name, Window
        const char* infoText = TextFormat("Stop %d: %s [%d-%dm]", (int)i + 1, sch.name.c_str(), sch.window.start, sch.window.end);
        float sW = MeasureTextCustom(infoText, 10.5f, FONT_SEMIBOLD) + 14;
        DrawRectangleRounded((Rectangle){schPos.x - sW * 0.5f, schPos.y - 32, sW, 19}, 0.3f, 4, Fade(BLACK, 0.88f));
        DrawRectangleRoundedLinesEx((Rectangle){schPos.x - sW * 0.5f, schPos.y - 32, sW, 19}, 0.3f, 4, 1.2f, sch.color);
        DrawTextCustom(infoText, schPos.x - sW * 0.5f + 7, schPos.y - 29, 10.5f, WHITE, FONT_SEMIBOLD);
    }

    // Draw Delivery Car on map
    Vector2 curCarGrid = m_world.WorldToGrid(m_carCurrentPos);
    Vector2 carPos = g2s(curCarGrid);

    DrawCircle((int)carPos.x, (int)carPos.y, 16 + 5 * sinf(m_pulseTimer * 6.0f), Fade(RED, 0.4f));
    DrawCircle((int)carPos.x, (int)carPos.y, 10, WHITE);
    DrawCircle((int)carPos.x, (int)carPos.y, 6, RED);

    // Direction arrow
    Vector2 arrowTip = {
        carPos.x + sinf(m_carHeading) * 18.0f,
        carPos.y + cosf(m_carHeading) * 18.0f
    };
    DrawLineEx(carPos, arrowTip, 3.2f, (Color){255, 230, 0, 255});
    DrawCircle((int)arrowTip.x, (int)arrowTip.y, 3.5f, (Color){255, 230, 0, 255});

    EndScissorMode();

    // 6. Bottom Legend & Sequence Bar
    int barY = viewY + viewH + 6;
    int barH = modalH - (barY - modalY) - 8;
    DrawRectangleRounded((Rectangle){(float)viewX, (float)barY, (float)viewW, (float)barH}, 0.15f, 4, (Color){24, 30, 44, 255});
    DrawRectangleRoundedLinesEx((Rectangle){(float)viewX, (float)barY, (float)viewW, (float)barH}, 0.15f, 4, 1.0f, Fade(WHITE, 0.2f));

    DrawTextCustom("Urutan:", viewX + 14, barY + 8, 11, LIGHTGRAY, FONT_SEMIBOLD);

    int legX = viewX + 68;
    // SPPG badge
    DrawRectangleRounded((Rectangle){(float)legX, (float)barY + 6, 44, 18}, 0.3f, 4, (Color){255, 180, 0, 255});
    DrawTextCustom("SPPG", legX + 6, barY + 8, 9.5f, BLACK, FONT_BOLD);
    legX += 50;

    for (size_t i = 0; i < m_schools.size(); i++) {
        DrawTextCustom("→", legX, barY + 7, 12, WHITE, FONT_BOLD);
        legX += 16;

        const auto& sch = m_schools[i];
        DrawRectangleRounded((Rectangle){(float)legX, (float)barY + 6, 22, 18}, 0.3f, 4, sch.color);
        DrawTextCustom(TextFormat("%d", (int)i + 1), legX + 7, barY + 8, 10, WHITE, FONT_BOLD);
        legX += 28;

        DrawTextCustom(sch.name.c_str(), legX, barY + 8, 11, WHITE, FONT_REGULAR);
        legX += MeasureTextCustom(sch.name.c_str(), 11, FONT_REGULAR) + 12;
    }

    DrawTextCustom("→", legX, barY + 7, 12, WHITE, FONT_BOLD);
    legX += 16;
    DrawRectangleRounded((Rectangle){(float)legX, (float)barY + 6, 80, 18}, 0.3f, 4, (Color){255, 180, 0, 255});
    DrawTextCustom("Kembali SPPG", legX + 5, barY + 8, 9.5f, BLACK, FONT_BOLD);

    // Right summary metrics
    const char* sumStr = TextFormat("Total: %.1f m • Cost: %d mnt • %s",
                                    m_totalRouteDistance * 4.0f,
                                    m_totalCostMinutes,
                                    m_routeFeasible ? "Layak (Feasible)" : "Overdue");
    float sumW = MeasureTextCustom(sumStr, 11, FONT_SEMIBOLD);
    DrawTextCustom(sumStr, viewX + viewW - sumW - 16, barY + 8, 11, (Color){140, 230, 255, 255}, FONT_SEMIBOLD);
}
