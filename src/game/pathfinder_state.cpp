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

    void Draw(Color baseCol, Color hoverCol, Color activeCol, Color textCol) const {
        Vector2 mouse = GetMousePosition();
        bool hovered = CheckCollisionPointRec(mouse, rect);
        Color col = active ? activeCol : (hovered ? hoverCol : baseCol);

        DrawRectangleRounded(rect, 0.28f, 4, col);
        DrawRectangleRoundedLinesEx(rect, 0.28f, 4, 1.5f, Fade(BLACK, 0.4f));

        int fontSize = 13;
        int textW = MeasureText(label, fontSize);
        float tx = rect.x + (rect.width - textW) * 0.5f;
        float ty = rect.y + (rect.height - fontSize) * 0.5f;
        DrawText(label, (int)tx, (int)ty, fontSize, textCol);
    }
};

PathfinderState::PathfinderState() {
    memset(&m_truckModel, 0, sizeof(Model));
    memset(&m_schoolModel, 0, sizeof(Model));
}

PathfinderState::~PathfinderState() {
    Cleanup();
}

void PathfinderState::Init() {
    TraceLog(LOG_INFO, "Initializing PathfinderState with MBG Routing Algorithm...");

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
        m_optimizationLog += TextFormat(" ➔ [%d] %s (%s)", (int)i + 1, m_schools[i].name.c_str(), LEG_COLOR_NAMES[i % 6]);
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
        m_optimizationLog += " ➔ [Akhir] SPPG (Kembali ke Titik Awal)";
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
    if (IsKeyPressed(KEY_M)) m_showMinimap = !m_showMinimap;

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
        DrawText("✓ MISI MBG SELESAI!", navCardX + 22, navCardY + 20, 22, WHITE);
        DrawText(TextFormat("Seluruh %d sekolah terlayani & mobil kembali ke titik awal (SPPG)!", (int)m_schools.size()),
                 navCardX + 22, navCardY + 50, 13, (Color){220, 255, 230, 255});
        DrawText("Memulai misi baru dalam 4 detik...", navCardX + 22, navCardY + 70, 12, (Color){200, 240, 210, 255});
    } else if (isReturningToDepot) {
        // Return to depot header
        DrawText("⌂", navCardX + 22, navCardY + 24, 30, WHITE);
        DrawText(m_turnInstruction.c_str(), navCardX + 74, navCardY + 12, 19, WHITE);
        DrawText("Langkah Akhir: Kembali ke Titik Awal (SPPG)", navCardX + 74, navCardY + 38, 14, WHITE);

        float metersCurrent = m_distanceToCurrentTarget * 4.0f;
        int etaSeconds = (int)(m_distanceToCurrentTarget / m_driveSpeed);
        DrawText(TextFormat("Sisa Jarak ke SPPG: %.0f m (%d dtk) • Menuju Pangkalan", metersCurrent, etaSeconds),
                 navCardX + 74, navCardY + 62, 13, (Color){255, 255, 180, 255});
        DrawText("✓ Pengiriman makanan bergizi di seluruh sekolah telah selesai",
                 navCardX + 74, navCardY + 82, 12, (Color){255, 245, 220, 255});

        DrawCircle(navCardX + navCardW - 20, navCardY + 20, 6, (Color){255, 230, 0, 255});
        DrawText("KEMBALI", navCardX + navCardW - 74, navCardY + 16, 9, BLACK);
    } else {
        // Navigation icon
        const char* iconStr = "↑";
        if (m_turnInstruction == "Belok Kanan") iconStr = "↱";
        else if (m_turnInstruction == "Belok Kiri") iconStr = "↰";
        else if (m_isUnloading) iconStr = "🍱";
        DrawText(iconStr, navCardX + 22, navCardY + 24, 30, WHITE);

        // Turn instruction
        DrawText(m_turnInstruction.c_str(), navCardX + 74, navCardY + 12, 20, WHITE);

        // Destination with school step indicator
        int targetId = m_currentLegIndex + 1;
        const char* colName = (m_currentLegIndex < 6) ? LEG_COLOR_NAMES[m_currentLegIndex] : "Warna";
        const char* schName = (m_currentLegIndex < (int)m_schools.size()) ? m_schools[m_currentLegIndex].name.c_str() : "Sekolah";

        DrawText(TextFormat("Tujuan: %s (Stop %d dari %d • Jalur %s)", schName, targetId, (int)m_schools.size(), colName),
                 navCardX + 74, navCardY + 38, 14, WHITE);

        // Distance & ETA
        float metersCurrent = m_distanceToCurrentTarget * 4.0f;
        float metersTotal   = m_totalDistanceRemaining * 4.0f;
        int etaSeconds = (int)(m_distanceToCurrentTarget / m_driveSpeed);

        DrawText(TextFormat("Target: %.0f m (%d dtk) • Total Sisa: %.0f m", metersCurrent, etaSeconds, metersTotal),
                 navCardX + 74, navCardY + 62, 13, (Color){255, 255, 180, 255});

        // Time window indicator
        if (m_currentLegIndex < (int)m_schools.size()) {
            const auto& w = m_schools[m_currentLegIndex].window;
            DrawText(TextFormat("Window Pengantaran: [%d - %d menit] • Bongkar: 5m", w.start, w.end),
                     navCardX + 74, navCardY + 82, 12, (Color){230, 245, 255, 255});
        }

        // Pulsing status
        DrawCircle(navCardX + navCardW - 20, navCardY + 20, 6, m_isUnloading ? ORANGE : RED);
        DrawText(m_isUnloading ? "BONGKAR" : "AUTO", navCardX + navCardW - 68, navCardY + 16, 9, WHITE);
    }

    // =========================================================================
    // 2. Control Toolbar (Top Right / Middle)
    // =========================================================================
    int toolX = navCardX + navCardW + 14;
    int toolY = 14;

    bool isModified = (m_routingAlgorithm == ROUTING_DP_MODIFIED);
    StateButton btnMod      = {{(float)toolX, (float)toolY, 145, 38}, "DP Modified [TAB]", isModified};
    StateButton btnBaseAlgo = {{(float)toolX + 152, (float)toolY, 140, 38}, "DP Baseline", !isModified};
    StateButton btnRandomize= {{(float)toolX + 298, (float)toolY, 120, 38}, "Acak Rute [R]", false};
    StateButton btnDrive    = {{(float)toolX + 424, (float)toolY, 115, 38}, m_isDriving ? "Jeda [SPACE]" : "Auto Jalan", m_isDriving};

    // Buttons to adjust number of schools live! (3 to 5)
    StateButton btnMinusSchool = {{(float)toolX + 545, (float)toolY, 36, 38}, "[-]", false};
    StateButton btnSchoolCount = {{(float)toolX + 586, (float)toolY, 90, 38}, TextFormat("%d Sekolah", m_numSchools), true};
    StateButton btnPlusSchool  = {{(float)toolX + 681, (float)toolY, 36, 38}, "[+]", false};

    StateButton btnCam = {{(float)toolX + 724, (float)toolY, 115, 38},
                          (m_camMode == APP_CAM_FOLLOW_CAR) ? "Cam: Truk [C]" : "Cam: Sek [S]",
                          m_camMode == APP_CAM_FOLLOW_CAR};

    Color btnBase = {240, 244, 250, 255};
    Color btnHover = {215, 230, 250, 255};

    btnMod.Draw(btnBase, btnHover, LEG_COLORS[0], isModified ? WHITE : BLACK);
    btnBaseAlgo.Draw(btnBase, btnHover, LEG_COLORS[2], !isModified ? WHITE : BLACK);
    btnRandomize.Draw(btnBase, btnHover, LEG_COLORS[1], BLACK);
    btnDrive.Draw(btnBase, btnHover, {230, 60, 60, 255}, m_isDriving ? WHITE : BLACK);
    btnMinusSchool.Draw(btnBase, btnHover, {200, 200, 210, 255}, BLACK);
    btnSchoolCount.Draw(btnBase, btnHover, Fade(LEG_COLORS[1], 0.85f), WHITE);
    btnPlusSchool.Draw(btnBase, btnHover, {200, 200, 210, 255}, BLACK);
    btnCam.Draw(btnBase, btnHover, LEG_DARK_COLORS[0], WHITE);

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

    DrawText("Sistem Routing MBG (Makan Bergizi Gratis)", cardX + 16, cardY + 12, 15, WHITE);
    DrawLine(cardX + 16, cardY + 32, cardX + cardW - 16, cardY + 32, Fade(WHITE, 0.2f));

    // Algorithm & Feasibility badge
    const char* algoName = isModified ? "DP-Modified (Optimized)" : "DP-Baseline (Dijkstra+DP)";
    Color algoCol = isModified ? (Color){100, 210, 255, 255} : (Color){255, 200, 100, 255};
    DrawText(TextFormat("Algoritma: %s", algoName), cardX + 16, cardY + 38, 12, algoCol);

    const char* feasStr = m_routeFeasible ? "✓ RUTE LAYAK (Feasible)" : "✗ MELEBIHI WINDOW WAKTU";
    Color feasCol = m_routeFeasible ? (Color){100, 240, 130, 255} : (Color){255, 80, 80, 255};
    DrawText(feasStr, cardX + cardW - MeasureText(feasStr, 12) - 16, cardY + 38, 12, feasCol);

    // Sequence header
    DrawText("Urutan Kunjungan Optimal:", cardX + 16, cardY + 58, 12, LIGHTGRAY);

    // SPPG Central Kitchen start badge
    DrawRectangleRounded((Rectangle){(float)cardX + 16, (float)cardY + 76, 48, 18}, 0.3f, 4, (Color){255, 180, 0, 255});
    DrawText("SPPG", cardX + 22, cardY + 78, 11, BLACK);
    DrawText("Dapur Pusat MBG (Titik Awal)", cardX + 70, cardY + 78, 12, (Color){255, 220, 140, 255});

    int rowY = cardY + 100;
    for (size_t i = 0; i < m_schools.size(); i++) {
        const auto& sch = m_schools[i];
        bool isCurrent = ((int)i == m_currentLegIndex && !m_allDelivered);
        bool isDone = sch.reached;

        // Colored badge
        DrawRectangleRounded((Rectangle){(float)cardX + 16, (float)rowY, 24, 18}, 0.3f, 4, sch.color);
        DrawText(TextFormat("%d", (int)i + 1), cardX + 24, rowY + 2, 12, WHITE);

        // Status string
        const char* statusStr = isDone ? "✓ Selesai" : (isCurrent ? (m_isUnloading ? "🍱 Bongkar" : "▶ Menuju") : "Menunggu");
        Color statusColor = isDone ? (Color){100, 240, 130, 255} : (isCurrent ? YELLOW : (Color){180, 190, 205, 255});

        float legMeters = (i < m_routeLegs.size()) ? (m_routeLegs[i].path.totalDistanceWorld * 4.0f) : 0.0f;
        DrawText(TextFormat("%s • [%d-%dm] • %.0fm • %s", sch.name.c_str(), sch.window.start, sch.window.end, legMeters, statusStr),
                 cardX + 46, rowY + 2, 12, statusColor);

        rowY += 21;
    }

    // Final Step (Kembali ke SPPG)
    bool isReturnLegCurrent = isReturningToDepot && !m_allDelivered;
    const char* returnStatusStr = m_allDelivered ? "✓ Tiba di SPPG" : (isReturnLegCurrent ? "▶ Sedang Kembali" : "Menunggu");
    Color returnStatusCol = m_allDelivered ? (Color){100, 240, 130, 255} : (isReturnLegCurrent ? (Color){255, 220, 120, 255} : (Color){180, 190, 205, 255});

    float returnMeters = (!m_routeLegs.empty() && m_routeLegs.back().isReturnToDepot) ?
                         (m_routeLegs.back().path.totalDistanceWorld * 4.0f) : 0.0f;

    DrawRectangleRounded((Rectangle){(float)cardX + 16, (float)rowY, 24, 18}, 0.3f, 4, (Color){255, 180, 0, 255});
    DrawText("⌂", cardX + 22, rowY + 2, 12, BLACK);
    DrawText(TextFormat("Kembali ke SPPG (Titik Awal) • %.0fm • %s", returnMeters, returnStatusStr),
             cardX + 46, rowY + 2, 12, returnStatusCol);

    // Performance & Metrics summary
    DrawLine(cardX + 16, cardY + cardH - 58, cardX + cardW - 16, cardY + cardH - 58, Fade(WHITE, 0.15f));

    DrawText(TextFormat("Total Cost: %d menit • Total Jarak: %.1f m", m_totalCostMinutes, m_totalRouteDistance * 4.0f),
             cardX + 16, cardY + cardH - 46, 12, WHITE);

    DrawText(TextFormat("States Explored: %d • Runtime: %.1f µs", m_statesExplored, m_optimizationTimeUs),
             cardX + 16, cardY + cardH - 26, 12, (Color){140, 230, 255, 255});

    // =========================================================================
    // 4. Real-Time 2D Minimap Overlay (Google Maps Style)
    // Shows all legs color-coded on the map
    // =========================================================================
    if (m_showMinimap && m_world.GetMapTexture().id != 0) {
        Texture2D mapTex = m_world.GetMapTexture();
        int mmW = 310;
        int mmH = (int)(mmW * ((float)m_world.GetGridHeight() / m_world.GetGridWidth()));
        int mmX = screenW - mmW - 16;
        int mmY = screenH - mmH - 16;

        DrawRectangleRounded((Rectangle){(float)mmX - 4, (float)mmY - 26, (float)mmW + 8, (float)mmH + 30}, 0.05f, 4, Fade(BLACK, 0.88f));
        DrawRectangleRoundedLinesEx((Rectangle){(float)mmX - 4, (float)mmY - 26, (float)mmW + 8, (float)mmH + 30}, 0.05f, 4, 1.5f, Fade(WHITE, 0.3f));
        DrawText("Peta Distribusi Rute MBG", mmX + 6, mmY - 20, 13, WHITE);

        // Map Image Texture
        DrawTexturePro(mapTex,
                       (Rectangle){0.0f, 0.0f, (float)mapTex.width, (float)mapTex.height},
                       (Rectangle){(float)mmX, (float)mmY, (float)mmW, (float)mmH},
                       (Vector2){0, 0}, 0.0f, WHITE);

        // Draw each colored route leg on the minimap
        for (const auto& leg : m_routeLegs) {
            if (!leg.path.found || leg.path.gridPath.size() < 2) continue;

            // White underlay border
            for (size_t i = 1; i < leg.path.gridPath.size(); i++) {
                float x1 = mmX + (leg.path.gridPath[i - 1].x / m_world.GetGridWidth()) * mmW;
                float y1 = mmY + (leg.path.gridPath[i - 1].y / m_world.GetGridHeight()) * mmH;
                float x2 = mmX + (leg.path.gridPath[i].x / m_world.GetGridWidth()) * mmW;
                float y2 = mmY + (leg.path.gridPath[i].y / m_world.GetGridHeight()) * mmH;
                DrawLineEx((Vector2){x1, y1}, (Vector2){x2, y2}, 4.5f, WHITE);
            }
            // Leg colored line
            for (size_t i = 1; i < leg.path.gridPath.size(); i++) {
                float x1 = mmX + (leg.path.gridPath[i - 1].x / m_world.GetGridWidth()) * mmW;
                float y1 = mmY + (leg.path.gridPath[i - 1].y / m_world.GetGridHeight()) * mmH;
                float x2 = mmX + (leg.path.gridPath[i].x / m_world.GetGridWidth()) * mmW;
                float y2 = mmY + (leg.path.gridPath[i].y / m_world.GetGridHeight()) * mmH;
                DrawLineEx((Vector2){x1, y1}, (Vector2){x2, y2}, 3.0f, leg.color);
            }
        }

        // Draw SPPG Pin (Dapur Pusat) on minimap
        float sppgMMX = mmX + (m_carStartGridPos.x / m_world.GetGridWidth()) * mmW;
        float sppgMMY = mmY + (m_carStartGridPos.y / m_world.GetGridHeight()) * mmH;
        DrawCircle((int)sppgMMX, (int)sppgMMY, 8, (Color){255, 180, 0, 255});
        DrawCircle((int)sppgMMX, (int)sppgMMY, 4, WHITE);
        DrawText("SPPG", (int)sppgMMX - 10, (int)sppgMMY - 14, 9, (Color){255, 230, 100, 255});

        // Draw all school pins on minimap
        for (size_t i = 0; i < m_schools.size(); i++) {
            const auto& sch = m_schools[i];
            float schMMX = mmX + (sch.gridPos.x / m_world.GetGridWidth()) * mmW;
            float schMMY = mmY + (sch.gridPos.y / m_world.GetGridHeight()) * mmH;

            DrawCircle((int)schMMX, (int)schMMY, 7, sch.color);
            DrawCircle((int)schMMX, (int)schMMY, 3, WHITE);
            DrawText(TextFormat("%d", (int)i + 1), (int)schMMX - 2, (int)schMMY - 12, 10, WHITE);
        }

        // Draw car current position on minimap
        Vector2 curCarGrid = m_world.WorldToGrid(m_carCurrentPos);
        float carMMX = mmX + (curCarGrid.x / m_world.GetGridWidth()) * mmW;
        float carMMY = mmY + (curCarGrid.y / m_world.GetGridHeight()) * mmH;

        DrawCircle((int)carMMX, (int)carMMY, 8 + 3 * sinf(m_pulseTimer * 5.0f), Fade(curHeaderBg, 0.5f));
        DrawCircle((int)carMMX, (int)carMMY, 5, WHITE);
        DrawCircle((int)carMMX, (int)carMMY, 3, RED);
    }

    // Top Right FPS & Engine info
    Fumbo::Instance().DrawFPS(screenW - 105, 16);
}
