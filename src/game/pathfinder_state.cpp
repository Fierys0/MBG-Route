#include "pathfinder_state.hpp"
#include "rlgl.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <cmath>
#include <algorithm>
#include <numeric>

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
    TraceLog(LOG_INFO, "Initializing PathfinderState with Multi-School Optimization...");

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

    // 4. Randomize initial positions & optimize multi-school route
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
// and find the optimal sequence that minimizes total travel distance (TSP)
// =============================================================================
void PathfinderState::RandomizePositions() {
    m_carStartGridPos = m_world.GetRandomRoadPosition();
    m_carCurrentPos = m_world.GridToWorld(m_carStartGridPos.x, m_carStartGridPos.y, 0.05f);

    // Ensure m_numSchools is at least 3
    if (m_numSchools < 3) m_numSchools = 3;
    if (m_numSchools > 6) m_numSchools = 6;

    // Pick distinct schools on connected roads with good spatial distribution
    std::vector<Vector2> pickedSchoolGridCoords;
    int attempts = 0;

    while ((int)pickedSchoolGridCoords.size() < m_numSchools && attempts < 400) {
        attempts++;
        Vector2 cand = m_world.GetRandomRoadPosition();

        if (!m_world.AreConnected(m_carStartGridPos, cand)) continue;
        if (Vector2Distance(m_carStartGridPos, cand) < 30.0f) continue;

        bool tooCloseToOther = false;
        for (const auto& existing : pickedSchoolGridCoords) {
            if (Vector2Distance(existing, cand) < 28.0f) {
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

    // =========================================================================
    // Multi-Destination Route Optimization (Traveling Salesperson Sequence):
    // Evaluates visiting order permutations to find the minimal total travel distance!
    // Mobil -> Sekolah A -> Sekolah B -> Sekolah C
    // =========================================================================
    int n = (int)pickedSchoolGridCoords.size();
    std::vector<int> perm(n);
    std::iota(perm.begin(), perm.end(), 0);

    std::vector<int> bestPerm = perm;
    float bestTotalDist = 1e9f;

    // Fast Octile distance metric for permutation optimization
    auto EstimateDist = [&](Vector2 a, Vector2 b) -> float {
        float dx = fabsf(a.x - b.x);
        float dy = fabsf(a.y - b.y);
        return (dx + dy) + (1.41421356f - 2.0f) * fminf(dx, dy);
    };

    do {
        float cost = EstimateDist(m_carStartGridPos, pickedSchoolGridCoords[perm[0]]);
        for (int i = 0; i < n - 1; i++) {
            cost += EstimateDist(pickedSchoolGridCoords[perm[i]], pickedSchoolGridCoords[perm[i + 1]]);
        }
        if (cost < bestTotalDist) {
            bestTotalDist = cost;
            bestPerm = perm;
        }
    } while (std::next_permutation(perm.begin(), perm.end()));

    // Assemble the ordered schools list based on the optimal sequence
    m_schools.clear();
    m_optimizationLog = "Urutan Optimal: Mobil";

    for (int i = 0; i < n; i++) {
        int originalIdx = bestPerm[i];
        Vector2 gPos = pickedSchoolGridCoords[originalIdx];

        SchoolDestination sch;
        sch.id = i + 1;
        sch.name = TextFormat("Sekolah %d", sch.id);
        sch.gridPos = gPos;
        sch.worldPos = m_world.GridToWorld(gPos.x, gPos.y, 0.0f);
        sch.color = LEG_COLORS[i % 6];
        sch.darkColor = LEG_DARK_COLORS[i % 6];
        sch.reached = false;

        m_schools.push_back(sch);
        m_optimizationLog += TextFormat(" ➔ [%d] Sek. %d (%s)", i + 1, sch.id, LEG_COLOR_NAMES[i % 6]);
    }

    // Build the routes and 3D colored ribbon meshes for each leg
    OptimizeAndBuildRoutes();

    m_isDriving = true;
    m_allDelivered = false;
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
// Build each leg of the multi-school journey with its own distinct color ribbon
// =============================================================================
void PathfinderState::OptimizeAndBuildRoutes() {
    UnloadRouteModels();

    m_totalRouteDistance = 0.0f;
    m_totalNodesExplored = 0;
    m_totalExecutionTimeMs = 0.0;

    Vector2 currentStart = m_carStartGridPos;

    for (size_t i = 0; i < m_schools.size(); i++) {
        Vector2 goal = m_schools[i].gridPos;

        RouteLeg leg;
        leg.legIndex = (int)i;
        leg.targetSchoolIndex = (int)i;
        leg.color = m_schools[i].color;

        // Compute shortest path for this leg using selected algorithm
        leg.path = m_pathfinder.FindShortestPath(m_world, currentStart, goal, m_algorithm);

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
        currentStart = goal; // Next leg starts from this school
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
        if (cross > 0.12f) {
            m_turnInstruction = "Belok Kanan";
        } else if (cross < -0.12f) {
            m_turnInstruction = "Belok Kiri";
        } else {
            m_turnInstruction = "Lurus Terus";
        }
    } else {
        m_turnInstruction = TextFormat("Mendekati Sekolah %d", m_currentLegIndex + 1);
    }
}

void PathfinderState::UpdateCarDriving(float dt) {
    if (!m_isDriving || m_routeLegs.empty() || m_currentLegIndex >= (int)m_routeLegs.size()) {
        return;
    }

    const auto& curLeg = m_routeLegs[m_currentLegIndex];
    if (!curLeg.path.found || curLeg.path.worldWaypoints.size() < 2) return;

    int curIdx = (int)m_legDriveProgress;
    int nextIdx = curIdx + 1;

    // Check if car reached current school destination
    if (nextIdx >= (int)curLeg.path.worldWaypoints.size()) {
        m_carCurrentPos = curLeg.path.worldWaypoints.back();
        m_schools[m_currentLegIndex].reached = true;

        // Transition to next school leg!
        if (m_currentLegIndex + 1 < (int)m_routeLegs.size()) {
            m_currentLegIndex++;
            m_legDriveProgress = 0.0f;
            TraceLog(LOG_INFO, "Reached School %d! Now driving to School %d...",
                     m_currentLegIndex, m_currentLegIndex + 1);
        } else {
            // Reached ALL schools! Mission accomplished!
            m_allDelivered = true;
            m_distanceToCurrentTarget = 0.0f;
            m_totalDistanceRemaining = 0.0f;

            m_arrivalTimer += dt;
            if (m_arrivalTimer >= 4.0f) {
                RandomizePositions(); // Automatically starts next delivery mission!
            }
            return;
        }
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
        m_algorithm = (m_algorithm == ALGO_ASTAR) ? ALGO_DIJKSTRA : ALGO_ASTAR;
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

    // Adjust school count via keyboard '+' and '-'
    if (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD)) {
        if (m_numSchools < 6) {
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

    // Auto-drive car along current route leg
    UpdateCarDriving(dt);

    // Update active 3D LIDAR sensor rays cast from vehicle
    if (m_showLidar) {
        m_lidarHits = m_world.CastLidarRays(m_carCurrentPos, m_carHeading, 16, 18.0f);
    }

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

    // 3. Draw Delivery Truck Model (Mobil MBG)
    float truckScale = 0.022f;
    Vector3 truckRenderPos = m_carCurrentPos;
    DrawModelEx(m_truckModel, truckRenderPos, (Vector3){0, 1, 0}, m_carHeading * RAD2DEG,
                (Vector3){truckScale, truckScale, truckScale}, WHITE);

    // Halo ring under the truck in current leg color
    Color currentHaloColor = (!m_routeLegs.empty() && m_currentLegIndex < (int)m_routeLegs.size()) ?
                             m_routeLegs[m_currentLegIndex].color : LEG_COLORS[0];
    float pulseRadius = 0.40f + 0.12f * sinf(m_pulseTimer * 4.0f);
    DrawCircle3D(Vector3Add(truckRenderPos, (Vector3){0, 0.08f, 0}), pulseRadius, (Vector3){1, 0, 0}, 90.0f, Fade(currentHaloColor, 0.45f));
    DrawCircle3D(Vector3Add(truckRenderPos, (Vector3){0, 0.09f, 0}), 0.18f, (Vector3){1, 0, 0}, 90.0f, currentHaloColor);

    // 4. Draw School Models & Pin Markers for ALL schools (1, 2, 3...)
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

    // 5. Draw 3D LIDAR sensor rays cast from vehicle
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
    // Dynamic color matching the current target school!
    // =========================================================================
    int navCardX = 16;
    int navCardY = 14;
    int navCardW = 410;
    int navCardH = m_allDelivered ? 96 : 112;

    Color curHeaderBg = m_allDelivered ? (Color){24, 134, 75, 250} :
                        (!m_routeLegs.empty() && m_currentLegIndex < (int)m_routeLegs.size()) ?
                        Fade(m_routeLegs[m_currentLegIndex].color, 0.94f) : (Color){15, 157, 88, 250};

    DrawRectangleRounded((Rectangle){(float)navCardX, (float)navCardY, (float)navCardW, (float)navCardH}, 0.16f, 6, curHeaderBg);
    DrawRectangleRoundedLinesEx((Rectangle){(float)navCardX, (float)navCardY, (float)navCardW, (float)navCardH}, 0.16f, 6, 2.0f, Fade(WHITE, 0.7f));

    if (m_allDelivered) {
        DrawText("🎉", navCardX + 18, navCardY + 24, 38, WHITE);
        DrawText("Semua Sekolah Terkirim!", navCardX + 68, navCardY + 18, 22, WHITE);
        DrawText(TextFormat("Sukses mengantarkan ke %d sekolah! Rute selesai.", (int)m_schools.size()),
                 navCardX + 68, navCardY + 46, 14, (Color){220, 255, 230, 255});
        DrawText(TextFormat("Mengacak misi baru dalam %.1f dtk...", fmaxf(0.0f, 4.0f - m_arrivalTimer)),
                 navCardX + 68, navCardY + 68, 13, YELLOW);
    } else {
        const char* iconStr = "⬆";
        if (m_turnInstruction == "Belok Kanan") iconStr = "⮞";
        else if (m_turnInstruction == "Belok Kiri") iconStr = "⮜";

        DrawRectangleRounded((Rectangle){(float)navCardX + 14, (float)navCardY + 16, 52, 52}, 0.25f, 4, Fade(BLACK, 0.25f));
        DrawText(iconStr, navCardX + 26, navCardY + 24, 32, WHITE);

        // Turn instruction
        DrawText(m_turnInstruction.c_str(), navCardX + 78, navCardY + 14, 21, WHITE);

        // Destination with school step indicator
        int targetId = m_currentLegIndex + 1;
        const char* colName = (m_currentLegIndex < 6) ? LEG_COLOR_NAMES[m_currentLegIndex] : "Warna";
        DrawText(TextFormat("Menuju: Sekolah %d dari %d (Jalur %s)", targetId, (int)m_schools.size(), colName),
                 navCardX + 78, navCardY + 40, 15, WHITE);

        // Distance & ETA
        float metersCurrent = m_distanceToCurrentTarget * 4.0f;
        float metersTotal   = m_totalDistanceRemaining * 4.0f;
        int etaSeconds = (int)(m_distanceToCurrentTarget / m_driveSpeed);

        DrawText(TextFormat("Target Ini: %.0f m (%d dtk) • Total Sisa: %.0f m", metersCurrent, etaSeconds, metersTotal),
                 navCardX + 78, navCardY + 68, 13, (Color){255, 255, 180, 255});

        // Pulsing AUTO status
        DrawCircle(navCardX + navCardW - 22, navCardY + 22, 6, RED);
        DrawText("AUTO", navCardX + navCardW - 58, navCardY + 17, 10, WHITE);
    }

    // =========================================================================
    // 2. Control Toolbar (Top Right / Middle)
    // =========================================================================
    int toolX = navCardX + navCardW + 16;
    int toolY = 14;

    StateButton btnAStar     = {{(float)toolX, (float)toolY, 115, 38}, "A* Algo", m_algorithm == ALGO_ASTAR};
    StateButton btnDijkstra  = {{(float)toolX + 125, (float)toolY, 125, 38}, "Dijkstra", m_algorithm == ALGO_DIJKSTRA};
    StateButton btnRandomize = {{(float)toolX + 260, (float)toolY, 125, 38}, "Acak Rute [R]", false};
    StateButton btnDrive     = {{(float)toolX + 395, (float)toolY, 120, 38}, m_isDriving ? "Jeda [SPACE]" : "Auto Jalan", m_isDriving};

    // Buttons to adjust number of schools live!
    StateButton btnMinusSchool = {{(float)toolX + 525, (float)toolY, 40, 38}, "[-]", false};
    StateButton btnSchoolCount = {{(float)toolX + 570, (float)toolY, 100, 38}, TextFormat("%d Sekolah", m_numSchools), true};
    StateButton btnPlusSchool  = {{(float)toolX + 675, (float)toolY, 40, 38}, "[+]", false};

    StateButton btnCam = {{(float)toolX + 725, (float)toolY, 120, 38},
                          (m_camMode == APP_CAM_FOLLOW_CAR) ? "Cam: Truk" : "Cam: Sekolah",
                          m_camMode == APP_CAM_FOLLOW_CAR};

    Color btnBase = {240, 244, 250, 255};
    Color btnHover = {215, 230, 250, 255};

    btnAStar.Draw(btnBase, btnHover, LEG_COLORS[0], m_algorithm == ALGO_ASTAR ? WHITE : BLACK);
    btnDijkstra.Draw(btnBase, btnHover, LEG_COLORS[2], m_algorithm == ALGO_DIJKSTRA ? WHITE : BLACK);
    btnRandomize.Draw(btnBase, btnHover, LEG_COLORS[0], BLACK);
    btnDrive.Draw(btnBase, btnHover, {230, 60, 60, 255}, m_isDriving ? WHITE : BLACK);
    btnMinusSchool.Draw(btnBase, btnHover, {200, 200, 210, 255}, BLACK);
    btnSchoolCount.Draw(btnBase, btnHover, Fade(LEG_COLORS[1], 0.85f), WHITE);
    btnPlusSchool.Draw(btnBase, btnHover, {200, 200, 210, 255}, BLACK);
    btnCam.Draw(btnBase, btnHover, LEG_DARK_COLORS[0], WHITE);

    // Toolbar interactions
    if (btnAStar.IsClicked() && m_algorithm != ALGO_ASTAR) {
        m_algorithm = ALGO_ASTAR;
        OptimizeAndBuildRoutes();
    }
    if (btnDijkstra.IsClicked() && m_algorithm != ALGO_DIJKSTRA) {
        m_algorithm = ALGO_DIJKSTRA;
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
    if (btnPlusSchool.IsClicked() && m_numSchools < 6) {
        m_numSchools++;
        RandomizePositions();
    }
    if (btnCam.IsClicked()) {
        m_camMode = (m_camMode == APP_CAM_FOLLOW_CAR) ? APP_CAM_FOLLOW_SCHOOL : APP_CAM_FOLLOW_CAR;
    }

    // =========================================================================
    // 3. Multi-School TSP Route Optimization Card (Left Side)
    // Displays the optimized sequence, color coding, and algorithm metrics
    // =========================================================================
    int cardX = 16;
    int cardY = navCardY + navCardH + 12;
    int cardW = 380;
    int cardH = 220;

    DrawRectangleRounded((Rectangle){(float)cardX, (float)cardY, (float)cardW, (float)cardH}, 0.08f, 6, Fade((Color){20, 25, 35, 255}, 0.90f));
    DrawRectangleRoundedLinesEx((Rectangle){(float)cardX, (float)cardY, (float)cardW, (float)cardH}, 0.08f, 6, 1.5f, Fade(WHITE, 0.25f));

    DrawText("Optimasi Rute Multi-Sekolah (TSP)", cardX + 16, cardY + 12, 16, WHITE);
    DrawLine(cardX + 16, cardY + 34, cardX + cardW - 16, cardY + 34, Fade(WHITE, 0.2f));

    // Show sequence with colored bullet badges
    DrawText("Urutan Kunjungan Terbaik:", cardX + 16, cardY + 42, 13, LIGHTGRAY);

    int rowY = cardY + 62;
    for (size_t i = 0; i < m_schools.size(); i++) {
        const auto& sch = m_schools[i];
        bool isCurrent = ((int)i == m_currentLegIndex && !m_allDelivered);
        bool isDone = sch.reached;

        // Colored badge
        DrawRectangleRounded((Rectangle){(float)cardX + 16, (float)rowY, 22, 18}, 0.3f, 4, sch.color);
        DrawText(TextFormat("%d", sch.id), cardX + 23, rowY + 2, 12, WHITE);

        // Status string
        const char* statusStr = isDone ? "✓ Selesai" : (isCurrent ? "▶ Sedang Menuju" : "Menunggu");
        Color statusColor = isDone ? (Color){100, 240, 130, 255} : (isCurrent ? YELLOW : (Color){180, 190, 205, 255});

        float legMeters = (i < m_routeLegs.size()) ? (m_routeLegs[i].path.totalDistanceWorld * 4.0f) : 0.0f;
        DrawText(TextFormat("Stop %d: %s (%.0f m) • %s", sch.id, LEG_COLOR_NAMES[i % 6], legMeters, statusStr),
                 cardX + 44, rowY + 2, 12, statusColor);

        rowY += 22;
    }

    // Performance & Distance summary
    DrawLine(cardX + 16, rowY + 4, cardX + cardW - 16, rowY + 4, Fade(WHITE, 0.15f));
    rowY += 10;

    DrawText(TextFormat("Total Jarak Semua Rute: %.1f m", m_totalRouteDistance * 4.0f), cardX + 16, rowY, 12, WHITE);
    DrawText(TextFormat("Algoritma: %s • Waktu: %.2f ms", (m_algorithm == ALGO_ASTAR) ? "A*" : "Dijkstra", m_totalExecutionTimeMs),
             cardX + 16, rowY + 18, 12, (m_algorithm == ALGO_ASTAR) ? (Color){100, 235, 120, 255} : ORANGE);

    // =========================================================================
    // 4. Real-Time 2D Minimap Overlay (Google Maps Style)
    // Shows all legs color-coded on the map
    // =========================================================================
    if (m_showMinimap && m_world.GetMapTexture().id != 0) {
        Texture2D mapTex = m_world.GetMapTexture();
        int mmW = 300;
        int mmH = (int)(mmW * ((float)m_world.GetGridHeight() / m_world.GetGridWidth()));
        int mmX = screenW - mmW - 16;
        int mmY = screenH - mmH - 16;

        DrawRectangleRounded((Rectangle){(float)mmX - 4, (float)mmY - 26, (float)mmW + 8, (float)mmH + 30}, 0.05f, 4, Fade(BLACK, 0.88f));
        DrawRectangleRoundedLinesEx((Rectangle){(float)mmX - 4, (float)mmY - 26, (float)mmW + 8, (float)mmH + 30}, 0.05f, 4, 1.5f, Fade(WHITE, 0.3f));
        DrawText("Google Maps Multi-Stop Overview", mmX + 6, mmY - 20, 13, WHITE);

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

        // Draw all school pins on minimap
        for (const auto& sch : m_schools) {
            float schMMX = mmX + (sch.gridPos.x / m_world.GetGridWidth()) * mmW;
            float schMMY = mmY + (sch.gridPos.y / m_world.GetGridHeight()) * mmH;

            DrawCircle((int)schMMX, (int)schMMY, 7, sch.color);
            DrawCircle((int)schMMX, (int)schMMY, 3, WHITE);
            DrawText(TextFormat("%d", sch.id), (int)schMMX - 2, (int)schMMY - 12, 10, WHITE);
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
