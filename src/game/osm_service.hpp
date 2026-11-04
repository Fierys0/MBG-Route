#pragma once

#include "raylib.h"
#include <string>
#include <vector>
#include <map>

// =============================================================================
// OpenStreetMap Turn-by-Turn Navigation Step
// =============================================================================
struct OSMStep {
    std::string streetName;      // e.g. "Jl. Pinang Raya"
    std::string instruction;     // e.g. "Belok Kanan ke Jl. Pinang Raya"
    std::string maneuverType;    // "depart", "turn", "new name", "arrive", "continue"
    std::string modifier;        // "left", "right", "straight", "slight left", "slight right", "uturn"
    std::string icon;            // "<-", "->", "^", "v", "[TIBA]"
    float distanceMeters = 0.0f; // Step distance in meters
    float durationSeconds = 0.0f;// Step duration in seconds
    Vector2 gridPos = {0, 0};    // Approximate grid location of maneuver
};

// =============================================================================
// OpenStreetMap Leg Navigation Details
// =============================================================================
struct OSMLegNavigation {
    bool apiSuccess = false;
    std::string originName;
    std::string destinationName;
    float totalDistanceKm = 0.0f;
    float estimatedMinutes = 0.0f;
    std::string summary;
    std::vector<OSMStep> steps;
};

// =============================================================================
// Real-world Location Preset
// =============================================================================
struct OSMLocationPreset {
    std::string id;
    std::string name;            // "Pondok Labu, Jakarta Selatan"
    std::string district;        // "Cilandak, Jaksel"
    double centerLat;
    double centerLon;
    double minLat;
    double maxLat;
    double minLon;
    double maxLon;
    std::string depotName;       // "SPPG Dapur Pusat Pondok Labu"
    std::vector<std::string> schoolNames;
};

// =============================================================================
// OpenStreetMap API & Real Navigation Service
// =============================================================================
class OSMService {
public:
    static OSMService& Instance();

    void Init();
    void Cleanup();

    // Geospatial coordinate transforms for the active region
    Vector2 GridToGeo(Vector2 gridPos, int gridW, int gridH) const;
    Vector2 GeoToGrid(Vector2 geoPos, int gridW, int gridH) const;

    // Location & Area management
    const OSMLocationPreset& GetCurrentLocation() const { return m_currentLocation; }
    void SetLocationPreset(int presetIndex);
    int GetPresetCount() const { return (int)m_presets.size(); }
    const OSMLocationPreset& GetPreset(int index) const { return m_presets[index]; }

    // Authentic school names and central kitchen for active region
    std::string GetDepotName() const;
    std::string GetSchoolName(int schoolIndex) const;

    // Street Name Resolution & Proximity Matching
    std::string ResolveStreetName(Vector2 gridPos, int gridW, int gridH) const;

    // Live OpenStreetMap OSRM Routing Query
    OSMLegNavigation QueryRoute(Vector2 startGrid, Vector2 endGrid, int gridW, int gridH,
                                const std::string& fromName, const std::string& toName);

    // Turn-by-Turn Maneuver Resolver during active driving
    void UpdateNavigationGuidance(Vector2 carGrid, int currentLegIdx, float legProgress,
                                  const std::vector<Vector2>& gridPath,
                                  const std::vector<Vector3>& worldWaypoints,
                                  bool isReturnToDepot, const std::string& destName,
                                  std::string& outInstruction, std::string& outCurrentStreet,
                                  std::string& outIcon, float& outDistToNextManeuver);

    // Live API status
    bool IsOnline() const { return m_isOnline; }
    int GetQueryCount() const { return m_queryCount; }
    double GetLastLatencyMs() const { return m_lastLatencyMs; }
    std::string GetStatusString() const;

    // OpenStreetMap Tile / Map View Mode
    bool IsOSMViewEnabled() const { return m_osmViewEnabled; }
    void ToggleOSMView() { m_osmViewEnabled = !m_osmViewEnabled; }

private:
    OSMService();
    ~OSMService();

    void InitPresets();
    void InitPondokLabuStreets();

    // HTTP Request via WinINet with fast timeout
    std::string HttpGet(const std::string& url, int timeoutMs = 2500);

    // Simple JSON helpers
    std::string ExtractJsonString(const std::string& json, const std::string& key, size_t startPos = 0) const;
    double ExtractJsonDouble(const std::string& json, const std::string& key, size_t startPos = 0) const;

    // Street definition for spatial indexing
    struct StreetSegment {
        std::string name;
        double minLat, maxLat, minLon, maxLon;
        int speedLimitKmh;
    };

    std::vector<OSMLocationPreset> m_presets;
    OSMLocationPreset m_currentLocation;
    std::vector<StreetSegment> m_streetDatabase;

    // Cache of queried OSRM routes: key is "startGx,startGy->endGx,endGy"
    std::map<std::string, OSMLegNavigation> m_routeCache;

    bool m_isOnline = true;
    int m_queryCount = 0;
    double m_lastLatencyMs = 0.0;
    bool m_osmViewEnabled = true;
};

