#pragma once

#include "fumbo.hpp"
#include "raycast_world.hpp"
#include "pathfinder.hpp"
#include <string>

enum AppCameraMode {
    APP_CAM_FOLLOW_CAR = 0,
    APP_CAM_FREE,
    APP_CAM_FOLLOW_SCHOOL
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
    // Core systems
    RaycastWorld m_world;
    Pathfinder m_pathfinder;
    PathResult m_currentPath;
    AlgorithmType m_algorithm = ALGO_ASTAR;

    // 3D Models
    Model m_truckModel;
    Model m_schoolModel;
    Model m_pathModel;        // Google Maps blue core line
    Model m_pathBorderModel;  // Border outline underlay
    bool m_truckLoaded = false;
    bool m_schoolLoaded = false;
    bool m_pathModelLoaded = false;
    bool m_pathBorderLoaded = false;

    // Positions (Grid & World)
    Vector2 m_carGridPos = {0, 0};
    Vector2 m_schoolGridPos = {0, 0};
    Vector3 m_carCurrentPos = {0, 0, 0};
    Vector3 m_schoolPos = {0, 0, 0};
    float m_carHeading = 0.0f;

    // Google Maps Auto-Driving Navigation
    bool m_isDriving = true;      // Auto drive on by default!
    float m_driveProgress = 0.0f; // progress index along waypoints
    float m_driveSpeed = 9.5f;    // world units per second
    float m_distanceRemaining = 0.0f;
    std::string m_turnInstruction = "Lurus Terus";
    bool m_arrived = false;
    float m_arrivalTimer = 0.0f;
    float m_pulseTimer = 0.0f;

    // Camera
    Camera3D m_camera;
    AppCameraMode m_camMode = APP_CAM_FOLLOW_CAR;

    // Lidar ray casting visualization
    bool m_showLidar = true;
    std::vector<RayHitResult> m_lidarHits;

    // UI state
    bool m_showMinimap = true;

    // Internal actions
    void RecomputePath();
    void RandomizePositions();
    void UpdateCarDriving(float dt);
    void UpdateNavigationStatus();
    void UpdateCameraControls();
    void DrawUI();
};
