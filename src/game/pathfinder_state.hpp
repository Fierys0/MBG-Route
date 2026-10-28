#pragma once

#include "fumbo.hpp"
#include "raycast_world.hpp"
#include "pathfinder.hpp"
#include "Common.h"
#include "Graph.h"
#include "Dijkstra.h"
#include "DPBaseline.h"
#include "DPModified.h"
#include <string>
#include <vector>

enum AppCameraMode {
    APP_CAM_FOLLOW_CAR = 0,
    APP_CAM_FREE,
    APP_CAM_FOLLOW_SCHOOL
};

enum MBGRoutingAlgorithm {
    ROUTING_DP_MODIFIED = 0,   // DP Modified (Optimized Dominance Pruning & Branch-and-Bound)
    ROUTING_DP_BASELINE        // DP Baseline (Dijkstra + Dynamic Programming)
};

// =============================================================================
// School Destination Definition for MBG (Makan Bergizi Gratis)
// =============================================================================
struct SchoolDestination {
    int id;                   // 1-based index (1, 2, 3...)
    std::string name;         // "SDN 01", "SDN 02", etc.
    Vector2 gridPos;
    Vector3 worldPos;
    TimeWindow window;        // [start, end] in minutes (jam pengantaran makanan)
    int unloadTime = 5;       // Waktu bongkar muatan makanan bergizi (menit)
    Color color;              // Visual color matching its route leg
    Color darkColor;
    bool reached = false;
};

// =============================================================================
// Route Leg (Path segment connecting SPPG -> School 1 -> School 2 -> School 3)
// Each leg has its own distinct color and 3D ribbon model
// =============================================================================
struct RouteLeg {
    int legIndex;             // 0, 1, 2...
    int targetSchoolIndex;    // index into m_schools array (-1 for return to SPPG)
    std::string destinationName; // Name of destination
    Color color;              // Unique color for this leg (Blue, Green, Orange, etc.)
    PathResult path;
    Model pathModel;          // Core colored ribbon
    Model borderModel;        // White border underlay
    bool modelsLoaded = false;
    bool isReturnToDepot = false; // True if this leg is returning to SPPG (Titik Awal)!
};

class PathfinderState : public IGameState {
public:
    PathfinderState();
    ~PathfinderState() override;

    void Init() override;
    void Cleanup() override;
    void Update() override;
    void DrawClean() override;
    void DrawDirty() override;

private:
    // =========================================================================
    // MULTI-SCHOOL CONFIGURATION: Jumlah sekolah tujuan (bisa diubah!)
    // Minimal 3 sekolah sesuai permintaan, default 3, max 5 (MAX_SCHOOLS di Common.h)
    // =========================================================================
    static const int DEFAULT_NUM_SCHOOLS = 3;
    int m_numSchools = DEFAULT_NUM_SCHOOLS; // <<-- UBAH VARIABEL INI (misal: 3, 4, 5)

    // MBG Routing Algorithm Configuration
    MBGRoutingAlgorithm m_routingAlgorithm = ROUTING_DP_MODIFIED;
    int m_cookTime = 0;                     // Waktu selesai memasak di SPPG (0 menit / 07:00)
    bool m_routeFeasible = true;            // Kelayakan rute terhadap time window & freshness (240m)
    int m_totalCostMinutes = 0;             // Total cost rute dalam menit
    int m_statesExplored = 0;               // Jumlah states dieksplorasi (pada DP Modified)
    double m_optimizationTimeUs = 0.0;      // Waktu eksekusi optimasi MBG (µs)
    std::vector<int> m_optimalSequence;     // Urutan id sekolah hasil optimasi

    // Core systems
    RaycastWorld m_world;
    Pathfinder m_pathfinder;
    AlgorithmType m_gridPathAlgo = ALGO_ASTAR;

    // 3D Models
    Model m_truckModel;
    Model m_schoolModel;
    bool m_truckLoaded = false;
    bool m_schoolLoaded = false;

    // Vehicle state (SPPG Central Kitchen & Truck)
    Vector2 m_carStartGridPos = {0, 0};
    Vector3 m_carCurrentPos = {0, 0, 0};
    float m_carHeading = 0.0f;

    // Multi-School & Multi-Leg Route
    std::vector<SchoolDestination> m_schools;
    std::vector<RouteLeg> m_routeLegs;
    int m_currentLegIndex = 0;         // Which leg the car is currently driving
    float m_legDriveProgress = 0.0f;   // Waypoint index progress along current leg
    float m_driveSpeed = 10.0f;        // World units per second
    bool m_isDriving = true;           // Auto-drive active
    bool m_allDelivered = false;       // All schools visited
    float m_arrivalTimer = 0.0f;       // Timer after completing all schools
    float m_pulseTimer = 0.0f;

    // Unloading simulation at school
    bool m_isUnloading = false;
    float m_unloadTimer = 0.0f;

    // Navigation metrics
    float m_distanceToCurrentTarget = 0.0f;
    float m_totalDistanceRemaining = 0.0f;
    float m_totalRouteDistance = 0.0f;
    std::string m_turnInstruction = "Lurus Terus";
    std::string m_optimizationLog = "";
    int m_totalNodesExplored = 0;
    double m_totalExecutionTimeMs = 0.0;

    // Camera
    Camera3D m_camera;
    AppCameraMode m_camMode = APP_CAM_FOLLOW_CAR;
    int m_camSchoolTargetIdx = 0;

    // Lidar ray casting visualization
    bool m_showLidar = true;
    std::vector<RayHitResult> m_lidarHits;

    // UI state & Map View
    bool m_showMinimap = true;
    bool m_mapFullscreen = false;
    float m_minimapWidth = 320.0f; // Resizable minimap width (240px - 650px)
    float m_mapZoom = 1.0f;        // Fullscreen zoom factor (1.0x - 5.0x)
    Vector2 m_mapPan = {0, 0};     // Fullscreen pan offset
    bool m_isPanningMap = false;
    Vector2 m_panStartMouse = {0, 0};
    Vector2 m_panStartOffset = {0, 0};

    // Typography: Montserrat Font Family from assets/fonts
    enum FontStyle {
        FONT_REGULAR = 0,
        FONT_SEMIBOLD,
        FONT_BOLD
    };
    Font m_fontBold = {0};
    Font m_fontSemiBold = {0};
    Font m_fontRegular = {0};
    bool m_fontsLoaded = false;

    void LoadFonts();
    void UnloadFonts();
    void DrawTextCustom(const char* text, float x, float y, float size, Color color, FontStyle style = FONT_REGULAR) const;
    float MeasureTextCustom(const char* text, float size, FontStyle style = FONT_REGULAR) const;

    // Internal actions
    void RandomizePositions();
    void OptimizeAndBuildRoutes();
    void UnloadRouteModels();
    void UpdateCarDriving(float dt);
    void UpdateNavigationStatus();
    void UpdateCameraControls();
    void DrawUI();
    void DrawMinimap(int screenW, int screenH);
    void DrawFullscreenMap(int screenW, int screenH);
};
