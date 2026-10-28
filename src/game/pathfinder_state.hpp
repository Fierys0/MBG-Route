#pragma once

#include "fumbo.hpp"
#include "raycast_world.hpp"
#include "pathfinder.hpp"
#include <string>
#include <vector>

enum AppCameraMode {
    APP_CAM_FOLLOW_CAR = 0,
    APP_CAM_FREE,
    APP_CAM_FOLLOW_SCHOOL
};

// =============================================================================
// School Destination Definition
// =============================================================================
struct SchoolDestination {
    int id;                   // 1-based index (1, 2, 3...)
    std::string name;         // "Sekolah 1", "Sekolah 2", etc.
    Vector2 gridPos;
    Vector3 worldPos;
    Color color;              // Visual color matching its route leg
    Color darkColor;
    bool reached = false;
};

// =============================================================================
// Route Leg (Path segment connecting Car -> School 1 -> School 2 -> School 3)
// Each leg has its own distinct color and 3D ribbon model
// =============================================================================
struct RouteLeg {
    int legIndex;             // 0, 1, 2...
    int targetSchoolIndex;    // index into m_schools array
    Color color;              // Unique color for this leg (Blue, Green, Orange, etc.)
    PathResult path;
    Model pathModel;          // Core colored ribbon
    Model borderModel;        // White border underlay
    bool modelsLoaded = false;
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
    // Minimal 3 sekolah sesuai permintaan, bisa diubah ke 4, 5, dll.
    // =========================================================================
    static const int DEFAULT_NUM_SCHOOLS = 3;
    int m_numSchools = DEFAULT_NUM_SCHOOLS; // <<-- UBAH VARIABEL INI (misal: 3, 4, 5)

    // Core systems
    RaycastWorld m_world;
    Pathfinder m_pathfinder;
    AlgorithmType m_algorithm = ALGO_ASTAR;

    // 3D Models
    Model m_truckModel;
    Model m_schoolModel;
    bool m_truckLoaded = false;
    bool m_schoolLoaded = false;

    // Vehicle state
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

    // UI state
    bool m_showMinimap = true;

    // Internal actions
    void RandomizePositions();
    void OptimizeAndBuildRoutes();
    void UnloadRouteModels();
    void UpdateCarDriving(float dt);
    void UpdateNavigationStatus();
    void UpdateCameraControls();
    void DrawUI();
};
