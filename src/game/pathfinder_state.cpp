#include "pathfinder_state.hpp"
#include "rlgl.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <cmath>

// Google Maps Colors
static const Color GMAPS_BLUE        = {26, 115, 232, 255};   // #1A73E8
static const Color GMAPS_DEEP_BLUE   = {11, 87, 208, 255};    // #0B57D0
static const Color GMAPS_HEADER_GREEN= {15, 157, 88, 255};    // #0F9D58
static const Color GMAPS_RED_PIN     = {234, 67, 53, 255};    // #EA4335
static const Color GMAPS_WHITE       = {255, 255, 255, 255};

// Helper button collision & draw
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

        int fontSize = 14;
        int textW = MeasureText(label, fontSize);
        float tx = rect.x + (rect.width - textW) * 0.5f;
        float ty = rect.y + (rect.height - fontSize) * 0.5f;
        DrawText(label, (int)tx, (int)ty, fontSize, textCol);
    }
};

PathfinderState::PathfinderState() {
    memset(&m_truckModel, 0, sizeof(Model));
    memset(&m_schoolModel, 0, sizeof(Model));
    memset(&m_pathModel, 0, sizeof(Model));
    memset(&m_pathBorderModel, 0, sizeof(Model));
}

PathfinderState::~PathfinderState() {
    Cleanup();
}

void PathfinderState::Init() {
    TraceLog(LOG_INFO, "Initializing PathfinderState with Google Maps navigation & auto-drive...");

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
    m_camMode = APP_CAM_FOLLOW_CAR; // Default to Google Maps turn-by-turn follow view!

    // 4. Randomize initial positions on connected road network & find shortest path
    RandomizePositions();

    // 5. Automatically enable auto-drive!
    m_isDriving = true;
}

void PathfinderState::Cleanup() {
    if (m_pathModelLoaded) {
        m_pathfinder.UnloadPathModel(m_pathModel);
        m_pathModelLoaded = false;
    }
    if (m_pathBorderLoaded) {
        m_pathfinder.UnloadPathModel(m_pathBorderModel);
        m_pathBorderLoaded = false;
    }
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

void PathfinderState::RandomizePositions() {
    m_carGridPos = m_world.GetRandomRoadPosition();

    // Pick school from connected road network with decent distance
    int attempts = 0;
    do {
        m_schoolGridPos = m_world.GetRandomRoadPosition();
        attempts++;
    } while (attempts < 100 && (Vector2Distance(m_carGridPos, m_schoolGridPos) < 45.0f ||
                               !m_world.AreConnected(m_carGridPos, m_schoolGridPos)));

    m_carCurrentPos = m_world.GridToWorld(m_carGridPos.x, m_carGridPos.y, 0.05f);
    m_schoolPos = m_world.GridToWorld(m_schoolGridPos.x, m_schoolGridPos.y, 0.0f);

    m_isDriving = true; // Auto-drive starts immediately!
    m_driveProgress = 0.0f;
    m_arrived = false;
    m_arrivalTimer = 0.0f;

    RecomputePath();

    // Align camera immediately behind car
    if (m_camMode == APP_CAM_FOLLOW_CAR && m_currentPath.worldWaypoints.size() >= 2) {
        Vector3 diff = Vector3Subtract(m_currentPath.worldWaypoints[1], m_currentPath.worldWaypoints[0]);
        m_carHeading = atan2f(diff.x, diff.z);
        Vector3 offset = {-sinf(m_carHeading) * 16.0f, 10.0f, -cosf(m_carHeading) * 16.0f};
        m_camera.position = Vector3Add(m_carCurrentPos, offset);
        m_camera.target = Vector3Add(m_carCurrentPos, (Vector3){0, 1.2f, 0});
    }
}

void PathfinderState::RecomputePath() {
    if (m_pathModelLoaded) {
        m_pathfinder.UnloadPathModel(m_pathModel);
        m_pathModelLoaded = false;
    }
    if (m_pathBorderLoaded) {
        m_pathfinder.UnloadPathModel(m_pathBorderModel);
        m_pathBorderLoaded = false;
    }

    // Determine path start from current car grid position if car is already driving
    Vector2 startCoord = m_carGridPos;
    if (m_driveProgress > 0.0f && m_currentPath.found) {
        startCoord = m_world.WorldToGrid(m_carCurrentPos);
    }

    // Execute pathfinding (A* or Dijkstra)
    m_currentPath = m_pathfinder.FindShortestPath(m_world, startCoord, m_schoolGridPos, m_algorithm);

    if (m_currentPath.found && m_currentPath.gridPath.size() >= 2) {
        // Build Google Maps 3D Dual-layer Blue ribbon:
        // 1. Underlay border (Crisp white border)
        m_pathBorderModel = m_pathfinder.BuildPathMeshModel(m_world, m_currentPath.gridPath, 0.70f, 0.08f);
        m_pathBorderLoaded = true;

        // 2. Core Google Maps blue ribbon
        m_pathModel = m_pathfinder.BuildPathMeshModel(m_world, m_currentPath.gridPath, 0.48f, 0.14f);
        m_pathModelLoaded = true;

        // Orient car towards first waypoint
        if (m_currentPath.worldWaypoints.size() >= 2) {
            Vector3 diff = Vector3Subtract(m_currentPath.worldWaypoints[1], m_currentPath.worldWaypoints[0]);
            m_carHeading = atan2f(diff.x, diff.z);
        }
    }

    m_driveProgress = 0.0f;
    m_arrived = false;
    m_arrivalTimer = 0.0f;
    m_distanceRemaining = m_currentPath.totalDistanceWorld;
}

void PathfinderState::UpdateNavigationStatus() {
    if (!m_currentPath.found || m_currentPath.worldWaypoints.size() < 2) {
        m_turnInstruction = "Mencari Rute...";
        m_distanceRemaining = 0.0f;
        return;
    }

    int curIdx = (int)m_driveProgress;
    // Calculate remaining distance along path
    float distRem = 0.0f;
    if (curIdx < (int)m_currentPath.worldWaypoints.size()) {
        distRem += Vector3Distance(m_carCurrentPos, m_currentPath.worldWaypoints[curIdx]);
        for (size_t i = curIdx + 1; i < m_currentPath.worldWaypoints.size(); i++) {
            distRem += Vector3Distance(m_currentPath.worldWaypoints[i - 1], m_currentPath.worldWaypoints[i]);
        }
    }
    m_distanceRemaining = distRem;

    // Detect next turn angle along upcoming waypoints
    int lookAhead = curIdx + 1;
    if (lookAhead + 1 < (int)m_currentPath.worldWaypoints.size()) {
        Vector3 v1 = Vector3Subtract(m_currentPath.worldWaypoints[lookAhead], m_currentPath.worldWaypoints[curIdx]);
        Vector3 v2 = Vector3Subtract(m_currentPath.worldWaypoints[lookAhead + 1], m_currentPath.worldWaypoints[lookAhead]);

        // 2D cross product in XZ plane
        float cross = v1.x * v2.z - v1.z * v2.x;
        if (cross > 0.12f) {
            m_turnInstruction = "Belok Kanan";
        } else if (cross < -0.12f) {
            m_turnInstruction = "Belok Kiri";
        } else {
            m_turnInstruction = "Lurus Terus";
        }
    } else {
        m_turnInstruction = "Mendekati Sekolah";
    }
}

void PathfinderState::UpdateCarDriving(float dt) {
    if (!m_isDriving || !m_currentPath.found || m_currentPath.worldWaypoints.size() < 2) {
        return;
    }

    // Advance smoothly along waypoints
    int curIdx = (int)m_driveProgress;
    int nextIdx = curIdx + 1;

    if (nextIdx >= (int)m_currentPath.worldWaypoints.size()) {
        // Destination reached!
        m_carCurrentPos = m_currentPath.worldWaypoints.back();
        m_arrived = true;
        m_distanceRemaining = 0.0f;

        // Auto re-route after arrival celebration (3 seconds delay)
        m_arrivalTimer += dt;
        if (m_arrivalTimer >= 3.2f) {
            RandomizePositions(); // Automatically drives to a new destination!
        }
        return;
    }

    Vector3 pA = m_currentPath.worldWaypoints[curIdx];
    Vector3 pB = m_currentPath.worldWaypoints[nextIdx];
    float segDist = Vector3Distance(pA, pB);
    if (segDist < 0.001f) segDist = 0.001f;

    float step = (m_driveSpeed * dt) / segDist;
    m_driveProgress += step;

    curIdx = (int)m_driveProgress;
    nextIdx = curIdx + 1;

    if (nextIdx < (int)m_currentPath.worldWaypoints.size()) {
        float frac = m_driveProgress - curIdx;
        pA = m_currentPath.worldWaypoints[curIdx];
        pB = m_currentPath.worldWaypoints[nextIdx];
        m_carCurrentPos = Vector3Lerp(pA, pB, frac);

        Vector3 diff = Vector3Subtract(pB, pA);
        float targetAngle = atan2f(diff.x, diff.z);

        // Smooth heading turn without 360 wrap glitch
        float angleDiff = targetAngle - m_carHeading;
        while (angleDiff > PI) angleDiff -= 2.0f * PI;
        while (angleDiff < -PI) angleDiff += 2.0f * PI;
        m_carHeading += angleDiff * 0.18f;
    } else {
        m_carCurrentPos = m_currentPath.worldWaypoints.back();
        m_arrived = true;
        m_distanceRemaining = 0.0f;
    }

    UpdateNavigationStatus();
}

void PathfinderState::UpdateCameraControls() {
    if (m_camMode == APP_CAM_FREE) {
        UpdateCamera(&m_camera, CAMERA_FREE);
    } else if (m_camMode == APP_CAM_FOLLOW_CAR) {
        // Google Maps 3D Turn-By-Turn Navigation Camera:
        // Positioned behind the car at an elevated angle, looking forward along route
        Vector3 offset = {-sinf(m_carHeading) * 14.0f, 9.5f, -cosf(m_carHeading) * 14.0f};
        Vector3 desiredCamPos = Vector3Add(m_carCurrentPos, offset);
        m_camera.position = Vector3Lerp(m_camera.position, desiredCamPos, 0.12f);

        Vector3 desiredTarget = Vector3Add(m_carCurrentPos, (Vector3){sinf(m_carHeading) * 5.0f, 0.5f, cosf(m_carHeading) * 5.0f});
        m_camera.target = Vector3Lerp(m_camera.target, desiredTarget, 0.14f);
    } else if (m_camMode == APP_CAM_FOLLOW_SCHOOL) {
        Vector3 desiredCamPos = Vector3Add(m_schoolPos, (Vector3){0, 22.0f, 22.0f});
        m_camera.position = Vector3Lerp(m_camera.position, desiredCamPos, 0.08f);
        m_camera.target = Vector3Lerp(m_camera.target, m_schoolPos, 0.1f);
    }
}

void PathfinderState::Update() {
    float dt = GetFrameTime();
    m_pulseTimer += dt;

    // 1. Hotkeys
    if (IsKeyPressed(KEY_TAB)) {
        m_algorithm = (m_algorithm == ALGO_ASTAR) ? ALGO_DIJKSTRA : ALGO_ASTAR;
        RecomputePath();
    }
    if (IsKeyPressed(KEY_R)) {
        RandomizePositions();
    }
    if (IsKeyPressed(KEY_SPACE)) {
        if (m_currentPath.found) {
            m_isDriving = !m_isDriving;
            if (m_driveProgress >= (float)(m_currentPath.worldWaypoints.size() - 1)) {
                m_driveProgress = 0.0f;
                m_isDriving = true;
                m_arrived = false;
                m_arrivalTimer = 0.0f;
            }
        }
    }
    if (IsKeyPressed(KEY_C)) m_camMode = APP_CAM_FOLLOW_CAR;
    if (IsKeyPressed(KEY_S)) m_camMode = APP_CAM_FOLLOW_SCHOOL;
    if (IsKeyPressed(KEY_F)) m_camMode = APP_CAM_FREE;
    if (IsKeyPressed(KEY_L)) m_showLidar = !m_showLidar;
    if (IsKeyPressed(KEY_M)) m_showMinimap = !m_showMinimap;

    // 2. Auto-driving car along Google Maps blue line
    UpdateCarDriving(dt);

    // 3. Update active 3D LIDAR sensor rays cast from vehicle
    if (m_showLidar) {
        m_lidarHits = m_world.CastLidarRays(m_carCurrentPos, m_carHeading, 16, 18.0f);
    }

    // 4. Update Camera
    UpdateCameraControls();
}

void PathfinderState::DrawClean() {
    // Clean background layer (cached clear sky)
    ClearBackground((Color){195, 225, 255, 255});
}

void PathfinderState::DrawDirty() {
    // Clear depth buffer before 3D drawing
    rlClearScreenBuffers();
    ClearBackground((Color){195, 225, 255, 255});

    // =========================================================================
    // 3D Scene Rendering
    // =========================================================================
    BeginMode3D(m_camera);
    rlDisableBackfaceCulling();

    // 1. Draw procedurally generated 3D city (roads, buildings, river, ground)
    m_world.Draw3D(m_showLidar);

    // 2. Draw Google Maps Route (Dual layer: white border + vibrant blue core)
    if (m_currentPath.found && m_currentPath.gridPath.size() >= 2) {
        // Underlay border (Crisp white border)
        if (m_pathBorderLoaded) {
            DrawModel(m_pathBorderModel, {0, 0, 0}, 1.0f, (Color){255, 255, 255, 240});
        }
        // Core Google Maps Vibrant Blue line!
        if (m_pathModelLoaded) {
            DrawModel(m_pathModel, {0, 0, 0}, 1.0f, GMAPS_BLUE);
        }
    }

    // 3. Draw Delivery Truck Model (Mobil MBG)
    float truckScale = 0.022f; // Scaled down to fit comfortably in road lanes
    Vector3 truckRenderPos = m_carCurrentPos;
    DrawModelEx(m_truckModel, truckRenderPos, (Vector3){0, 1, 0}, m_carHeading * RAD2DEG,
                (Vector3){truckScale, truckScale, truckScale}, WHITE);

    // Google Maps Location Halo under the car
    float pulseRadius = 0.40f + 0.12f * sinf(m_pulseTimer * 4.0f);
    DrawCircle3D(Vector3Add(truckRenderPos, (Vector3){0, 0.08f, 0}), pulseRadius, (Vector3){1, 0, 0}, 90.0f, Fade(GMAPS_BLUE, 0.45f));
    DrawCircle3D(Vector3Add(truckRenderPos, (Vector3){0, 0.09f, 0}), 0.18f, (Vector3){1, 0, 0}, 90.0f, GMAPS_DEEP_BLUE);

    // 4. Draw School Model (Sekolah)
    float schoolScale = 0.0028f;
    Vector3 schoolRenderPos = m_schoolPos;
    DrawModelEx(m_schoolModel, schoolRenderPos, (Vector3){0, 1, 0}, 0.0f,
                (Vector3){schoolScale, schoolScale, schoolScale}, WHITE);

    // Google Maps Red Pin Drop Marker above the School
    float pinHoverY = 4.2f + 0.3f * sinf(m_pulseTimer * 3.0f);
    Vector3 pinHeadPos = Vector3Add(schoolRenderPos, (Vector3){0, pinHoverY, 0});
    Vector3 pinTipPos  = Vector3Add(schoolRenderPos, (Vector3){0, pinHoverY - 1.2f, 0});

    // Red Pin Sphere
    DrawSphere(pinHeadPos, 0.85f, GMAPS_RED_PIN);
    DrawSphere(pinHeadPos, 0.35f, WHITE); // White center dot like Google Maps pin
    // Cone pointing to school
    DrawCylinder(pinTipPos, 0.1f, 0.7f, 1.2f, 12, GMAPS_RED_PIN);
    // Pulsing circle at school ground
    DrawCircle3D(Vector3Add(schoolRenderPos, (Vector3){0, 0.05f, 0}), 2.0f + 0.4f * sinf(m_pulseTimer * 3.0f), (Vector3){1, 0, 0}, 90.0f, Fade(GMAPS_RED_PIN, 0.35f));

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

    // =========================================================================
    // 2D UI & Google Maps HUD Overlay
    // =========================================================================
    DrawUI();
}

void PathfinderState::DrawUI() {
    int screenW = GetScreenWidth();
    int screenH = GetScreenHeight();

    // =========================================================================
    // 1. Google Maps Navigation Header (Top Left / Turn-by-Turn Card)
    // =========================================================================
    int navCardX = 16;
    int navCardY = 14;
    int navCardW = 380;
    int navCardH = m_arrived ? 90 : 105;

    // Green Google Maps header background
    Color headerBg = m_arrived ? (Color){24, 134, 75, 250} : (Color){15, 157, 88, 250};
    DrawRectangleRounded((Rectangle){(float)navCardX, (float)navCardY, (float)navCardW, (float)navCardH}, 0.16f, 6, headerBg);
    DrawRectangleRoundedLinesEx((Rectangle){(float)navCardX, (float)navCardY, (float)navCardW, (float)navCardH}, 0.16f, 6, 2.0f, Fade(WHITE, 0.6f));

    if (m_arrived) {
        // Arrival celebration
        DrawText("🎉", navCardX + 18, navCardY + 22, 36, WHITE);
        DrawText("Sampai di Tujuan!", navCardX + 64, navCardY + 18, 22, WHITE);
        DrawText("Sekolah (Low Poly School) • Rute Selesai", navCardX + 64, navCardY + 46, 14, (Color){220, 255, 230, 255});
        DrawText(TextFormat("Mengacak rute baru dalam %.1f dtk...", fmaxf(0.0f, 3.2f - m_arrivalTimer)), navCardX + 64, navCardY + 66, 13, YELLOW);
    } else {
        // Turn icon indicator
        const char* iconStr = "⬆";
        if (m_turnInstruction == "Belok Kanan") iconStr = "⮞";
        else if (m_turnInstruction == "Belok Kiri") iconStr = "⮜";

        DrawRectangleRounded((Rectangle){(float)navCardX + 14, (float)navCardY + 14, 52, 52}, 0.25f, 4, Fade(BLACK, 0.2f));
        DrawText(iconStr, navCardX + 26, navCardY + 22, 32, WHITE);

        // Turn instruction
        DrawText(m_turnInstruction.c_str(), navCardX + 78, navCardY + 14, 21, WHITE);

        // Destination
        DrawText("Menuju: Sekolah (Low Poly School)", navCardX + 78, navCardY + 40, 14, (Color){225, 255, 235, 255});

        // Live Remaining Distance and ETA
        float remainingMeters = m_distanceRemaining * 4.0f; // scaled to meters
        int etaSeconds = (int)(m_distanceRemaining / m_driveSpeed);
        DrawText(TextFormat("Sisa Jarak: %.0f m  •  ETA: %d dtk  •  Kecepatan: 40 km/j", remainingMeters, etaSeconds),
                 navCardX + 78, navCardY + 68, 13, (Color){255, 255, 170, 255});

        // Pulsing Live status
        DrawCircle(navCardX + navCardW - 22, navCardY + 22, 6, RED);
        DrawText("AUTO", navCardX + navCardW - 58, navCardY + 17, 10, WHITE);
    }

    // =========================================================================
    // 2. Control Toolbar (Top Right / Middle)
    // =========================================================================
    int toolX = 412;
    int toolY = 14;

    StateButton btnAStar = {{(float)toolX, (float)toolY, 125, 38}, "A* Algorithm", m_algorithm == ALGO_ASTAR};
    StateButton btnDijkstra = {{(float)toolX + 135, (float)toolY, 140, 38}, "Dijkstra Algo", m_algorithm == ALGO_DIJKSTRA};
    StateButton btnRandomize = {{(float)toolX + 285, (float)toolY, 135, 38}, "Acak Rute [R]", false};
    StateButton btnDrive = {{(float)toolX + 430, (float)toolY, 130, 38}, m_isDriving ? "Jeda [SPACE]" : "Auto Jalan", m_isDriving};

    const char* camLabel = (m_camMode == APP_CAM_FOLLOW_CAR) ? "Cam: Navigasi [C]" :
                           (m_camMode == APP_CAM_FOLLOW_SCHOOL) ? "Cam: Sekolah [S]" : "Cam: Bebas [F]";
    StateButton btnCam = {{(float)toolX + 570, (float)toolY, 145, 38}, camLabel, m_camMode == APP_CAM_FOLLOW_CAR};
    StateButton btnMap = {{(float)toolX + 725, (float)toolY, 115, 38}, m_showMinimap ? "Map: ON" : "Map: OFF", m_showMinimap};

    Color btnBase = {240, 244, 250, 255};
    Color btnHover = {215, 230, 250, 255};
    Color btnActiveBlue = GMAPS_BLUE;
    Color btnActiveOrange = {240, 130, 20, 255};

    btnAStar.Draw(btnBase, btnHover, btnActiveBlue, m_algorithm == ALGO_ASTAR ? WHITE : BLACK);
    btnDijkstra.Draw(btnBase, btnHover, btnActiveOrange, m_algorithm == ALGO_DIJKSTRA ? WHITE : BLACK);
    btnRandomize.Draw(btnBase, btnHover, btnActiveBlue, BLACK);
    btnDrive.Draw(btnBase, btnHover, {230, 60, 60, 255}, m_isDriving ? WHITE : BLACK);
    btnCam.Draw(btnBase, btnHover, GMAPS_DEEP_BLUE, m_camMode == APP_CAM_FOLLOW_CAR ? WHITE : BLACK);
    btnMap.Draw(btnBase, btnHover, {80, 160, 220, 255}, BLACK);

    // Toolbar interactions
    if (btnAStar.IsClicked() && m_algorithm != ALGO_ASTAR) {
        m_algorithm = ALGO_ASTAR;
        RecomputePath();
    }
    if (btnDijkstra.IsClicked() && m_algorithm != ALGO_DIJKSTRA) {
        m_algorithm = ALGO_DIJKSTRA;
        RecomputePath();
    }
    if (btnRandomize.IsClicked()) {
        RandomizePositions();
    }
    if (btnDrive.IsClicked()) {
        if (m_currentPath.found) {
            m_isDriving = !m_isDriving;
            if (m_driveProgress >= (float)(m_currentPath.worldWaypoints.size() - 1)) {
                m_driveProgress = 0.0f;
                m_isDriving = true;
                m_arrived = false;
                m_arrivalTimer = 0.0f;
            }
        }
    }
    if (btnCam.IsClicked()) {
        m_camMode = (AppCameraMode)((m_camMode + 1) % 3);
        if (m_camMode == APP_CAM_FREE) {
            m_camera.position = (Vector3){0.0f, 75.0f, 75.0f};
            m_camera.target = (Vector3){0.0f, 0.0f, 0.0f};
        }
    }
    if (btnMap.IsClicked()) m_showMinimap = !m_showMinimap;

    // =========================================================================
    // 3. Stats & Algorithm Comparison Card (Left Side)
    // =========================================================================
    int cardX = 16;
    int cardY = navCardY + navCardH + 12;
    int cardW = 340;
    int cardH = 195;

    DrawRectangleRounded((Rectangle){(float)cardX, (float)cardY, (float)cardW, (float)cardH}, 0.08f, 6, Fade((Color){20, 25, 35, 255}, 0.88f));
    DrawRectangleRoundedLinesEx((Rectangle){(float)cardX, (float)cardY, (float)cardW, (float)cardH}, 0.08f, 6, 1.5f, Fade(WHITE, 0.2f));

    DrawText("Navigasi & Algoritma Rute", cardX + 16, cardY + 12, 16, WHITE);
    DrawLine(cardX + 16, cardY + 32, cardX + cardW - 16, cardY + 32, Fade(WHITE, 0.2f));

    const char* algoName = (m_algorithm == ALGO_ASTAR) ? "A* (Heuristic Guided)" : "Dijkstra (Uniform Cost)";
    Color algoColor = (m_algorithm == ALGO_ASTAR) ? (Color){80, 215, 120, 255} : (Color){255, 165, 45, 255};
    DrawText(TextFormat("Algoritma: %s", algoName), cardX + 16, cardY + 40, 14, algoColor);

    DrawText(TextFormat("Panjang Rute: %.1f m (%d waypoints)", m_currentPath.totalDistanceWorld * 4.0f, (int)m_currentPath.gridPath.size()),
             cardX + 16, cardY + 62, 13, LIGHTGRAY);

    DrawText(TextFormat("Node Dieksplorasi: %d nodes", m_currentPath.nodesExplored),
             cardX + 16, cardY + 84, 13, (m_algorithm == ALGO_ASTAR) ? (Color){100, 235, 120, 255} : ORANGE);

    DrawText(TextFormat("Waktu Hitung: %.3f ms", m_currentPath.executionTimeMs),
             cardX + 16, cardY + 106, 13, YELLOW);

    if (m_algorithm == ALGO_ASTAR) {
        DrawText("A* menggunakan estimasi jarak ke tujuan sehingga", cardX + 16, cardY + 130, 11, (Color){180, 220, 255, 255});
        DrawText("mencari rute lebih cepat dengan sedikit node.", cardX + 16, cardY + 145, 11, (Color){180, 220, 255, 255});
    } else {
        DrawText("Dijkstra mencari ke segala arah tanpa heuristik,", cardX + 16, cardY + 130, 11, (Color){255, 220, 180, 255});
        DrawText("sehingga menjelajahi lebih banyak node.", cardX + 16, cardY + 145, 11, (Color){255, 220, 180, 255});
    }

    DrawText("[TAB] Ganti Algoritma  [R] Acak Rute  [C] Cam Navigasi", cardX + 16, cardY + 170, 11, Fade(WHITE, 0.7f));

    // =========================================================================
    // 4. Real-Time 2D Minimap Overlay (Google Maps Style)
    // =========================================================================
    if (m_showMinimap && m_world.GetMapTexture().id != 0) {
        Texture2D mapTex = m_world.GetMapTexture();
        int mmW = 280;
        int mmH = (int)(mmW * ((float)m_world.GetGridHeight() / m_world.GetGridWidth()));
        int mmX = screenW - mmW - 16;
        int mmY = screenH - mmH - 16;

        // Minimap container & header
        DrawRectangleRounded((Rectangle){(float)mmX - 4, (float)mmY - 26, (float)mmW + 8, (float)mmH + 30}, 0.05f, 4, Fade(BLACK, 0.85f));
        DrawRectangleRoundedLinesEx((Rectangle){(float)mmX - 4, (float)mmY - 26, (float)mmW + 8, (float)mmH + 30}, 0.05f, 4, 1.5f, Fade(WHITE, 0.3f));
        DrawText("Google Maps Overview", mmX + 6, mmY - 20, 14, WHITE);

        // Map Image Texture
        DrawTexturePro(mapTex,
                       (Rectangle){0.0f, 0.0f, (float)mapTex.width, (float)mapTex.height},
                       (Rectangle){(float)mmX, (float)mmY, (float)mmW, (float)mmH},
                       (Vector2){0, 0}, 0.0f, WHITE);

        // Draw Google Maps Blue Route Line on minimap
        if (m_currentPath.found && m_currentPath.gridPath.size() >= 2) {
            // White underlay line
            for (size_t i = 1; i < m_currentPath.gridPath.size(); i++) {
                float x1 = mmX + (m_currentPath.gridPath[i - 1].x / m_world.GetGridWidth()) * mmW;
                float y1 = mmY + (m_currentPath.gridPath[i - 1].y / m_world.GetGridHeight()) * mmH;
                float x2 = mmX + (m_currentPath.gridPath[i].x / m_world.GetGridWidth()) * mmW;
                float y2 = mmY + (m_currentPath.gridPath[i].y / m_world.GetGridHeight()) * mmH;
                DrawLineEx((Vector2){x1, y1}, (Vector2){x2, y2}, 4.5f, WHITE);
            }
            // Blue route line
            for (size_t i = 1; i < m_currentPath.gridPath.size(); i++) {
                float x1 = mmX + (m_currentPath.gridPath[i - 1].x / m_world.GetGridWidth()) * mmW;
                float y1 = mmY + (m_currentPath.gridPath[i - 1].y / m_world.GetGridHeight()) * mmH;
                float x2 = mmX + (m_currentPath.gridPath[i].x / m_world.GetGridWidth()) * mmW;
                float y2 = mmY + (m_currentPath.gridPath[i].y / m_world.GetGridHeight()) * mmH;
                DrawLineEx((Vector2){x1, y1}, (Vector2){x2, y2}, 3.0f, GMAPS_BLUE);
            }
        }

        // Draw car current position on minimap
        Vector2 curCarGrid = m_world.WorldToGrid(m_carCurrentPos);
        float carMMX = mmX + (curCarGrid.x / m_world.GetGridWidth()) * mmW;
        float carMMY = mmY + (curCarGrid.y / m_world.GetGridHeight()) * mmH;

        // Pulsing radar ring
        DrawCircle((int)carMMX, (int)carMMY, 8 + 3 * sinf(m_pulseTimer * 5.0f), Fade(GMAPS_BLUE, 0.4f));
        DrawCircle((int)carMMX, (int)carMMY, 5, GMAPS_DEEP_BLUE);
        DrawCircle((int)carMMX, (int)carMMY, 2, WHITE);

        // Draw school destination pin on minimap
        float schMMX = mmX + (m_schoolGridPos.x / m_world.GetGridWidth()) * mmW;
        float schMMY = mmY + (m_schoolGridPos.y / m_world.GetGridHeight()) * mmH;
        DrawCircle((int)schMMX, (int)schMMY, 6, GMAPS_RED_PIN);
        DrawCircle((int)schMMX, (int)schMMY, 2, WHITE);
    }

    // Top Right FPS & Engine info
    Fumbo::Instance().DrawFPS(screenW - 105, 16);
}
