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
    memset(&m_tentModel, 0, sizeof(Model));
}

PathfinderState::~PathfinderState() {
    Cleanup();
}

void PathfinderState::LoadFonts() {
    // Collect standard character codepoints: Basic ASCII + Latin-1 + Navigation symbols supported by Montserrat
    std::vector<int> codepoints;
    for (int i = 32; i < 127; i++) codepoints.push_back(i); // ASCII 32-126
    for (int i = 160; i < 256; i++) codepoints.push_back(i); // Latin-1
    codepoints.push_back(0x2022); // | (Bullet)
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
    OSMService::Instance().Init();
    m_depotName = OSMService::Instance().GetDepotName();

    // 1. Generate 3D procedural world from authentic OpenStreetMap road vectors
    const char* osmMapFile = "assets/osm_pondok_labu_hd.png";
    const char* osmBinFile = "assets/osm_roads_pondok_labu.bin";
    bool worldLoaded = false;

    if (FileExists(osmMapFile) && FileExists(osmBinFile)) {
        worldLoaded = m_world.GenerateFromOSM(osmMapFile, osmBinFile, 4);
        if (worldLoaded) {
            m_osmMapLoaded = true;
            TraceLog(LOG_INFO, "[OSM] 3D world successfully generated from real OpenStreetMap road vectors!");
        }
    }

    if (!worldLoaded) {
        TraceLog(LOG_INFO, "Fallback: Generating 3D world from assets/map.jpg...");
        m_world.GenerateFromImage("assets/map.jpg", 6);
        m_osmMapLoaded = false;
    }

    // 2. Load 3D GLTF models
    m_truckModel = LoadModel("assets/cartoony_style_delivery_truck_low_poly/scene.gltf");
    m_truckLoaded = (m_truckModel.meshCount > 0);

    m_schoolModel = LoadModel("assets/low_poly_school/scene.gltf");
    m_schoolLoaded = (m_schoolModel.meshCount > 0);

    m_tentModel = LoadModel("assets/low_poly_camping_tent_3/scene.gltf");
    m_tentLoaded = (m_tentModel.meshCount > 0);

    TraceLog(LOG_INFO, "Models loaded: Truck=%d meshes, School=%d meshes, Tent=%d meshes",
             m_truckModel.meshCount, m_schoolModel.meshCount, m_tentModel.meshCount);

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

    if (m_osmMapLoaded && m_osmMapTexture.id > 0) {
        UnloadTexture(m_osmMapTexture);
        m_osmMapLoaded = false;
    }

    if (m_truckLoaded) {
        UnloadModel(m_truckModel);
        m_truckLoaded = false;
    }
    if (m_schoolLoaded) {
        UnloadModel(m_schoolModel);
        m_schoolLoaded = false;
    }
    if (m_tentLoaded) {
        UnloadModel(m_tentModel);
        m_tentLoaded = false;
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
        if (leg.origModelLoaded) {
            m_pathfinder.UnloadPathModel(leg.origBlockedModel);
            leg.origModelLoaded = false;
        }
    }
    m_routeLegs.clear();
}

bool PathfinderState::IsGridInTraffic(Vector2 gPos) const {
    if (!m_trafficEnabled) return false;
    for (const auto& tz : m_trafficZones) {
        if (Vector2Distance(gPos, tz.centerGrid) <= tz.radiusGrid) {
            return true;
        }
    }
    return false;
}

void PathfinderState::GenerateObstaclesAndTraffic() {
    m_obstacles.clear();
    m_blockedRoadCells.clear();
    m_trafficZones.clear();
    m_trafficRoadCells.clear();

    if (m_schools.empty()) return;

    // 1. Generate Traffic Congestion Zones (Area Pasar & Simpang Sibuk)
    Vector2 schoolMid = m_schools[0].gridPos;
    if (m_schools.size() >= 2) {
        schoolMid = Vector2Scale(Vector2Add(m_schools[0].gridPos, m_schools[1].gridPos), 0.5f);
    }

    Vector2 tz1Center = m_world.GetRandomRoadPosition();
    for (int attempts = 0; attempts < 150; attempts++) {
        Vector2 cand = m_world.GetRandomRoadPosition();
        if (m_world.AreConnected(m_carStartGridPos, cand) && Vector2Distance(cand, schoolMid) < 32.0f) {
            tz1Center = cand;
            break;
        }
    }

    TrafficZone tz1;
    tz1.id = 1;
    tz1.name = "Kawasan Pasar & Pertokoan (Macet)";
    tz1.centerGrid = tz1Center;
    tz1.centerWorld = m_world.GridToWorld(tz1Center.x, tz1Center.y, 0.0f);
    tz1.radiusGrid = 10.0f;
    m_trafficZones.push_back(tz1);

    Vector2 tz2Center = m_world.GetRandomRoadPosition();
    for (int attempts = 0; attempts < 150; attempts++) {
        Vector2 cand = m_world.GetRandomRoadPosition();
        if (m_world.AreConnected(m_carStartGridPos, cand) && Vector2Distance(cand, tz1Center) > 28.0f) {
            tz2Center = cand;
            break;
        }
    }

    TrafficZone tz2;
    tz2.id = 2;
    tz2.name = "Simpang Padat Jam Sibuk";
    tz2.centerGrid = tz2Center;
    tz2.centerWorld = m_world.GridToWorld(tz2Center.x, tz2Center.y, 0.0f);
    tz2.radiusGrid = 9.0f;
    m_trafficZones.push_back(tz2);

    // Populate m_trafficRoadCells
    for (int y = 0; y < m_world.GetGridHeight(); y++) {
        for (int x = 0; x < m_world.GetGridWidth(); x++) {
            if (m_world.IsWalkable(x, y)) {
                Vector2 gp = {(float)x, (float)y};
                if (Vector2Distance(gp, tz1.centerGrid) <= tz1.radiusGrid ||
                    Vector2Distance(gp, tz2.centerGrid) <= tz2.radiusGrid) {
                    m_trafficRoadCells.push_back(gp);
                }
            }
        }
    }

    // 2. Generate Road Obstacles (Tenda Acara Warga / Hajatan di Jalan)
    // Find candidate along baseline shortest path between SPPG and School 1
    PathResult directP = m_pathfinder.FindShortestPath(m_world, m_carStartGridPos, m_schools[0].gridPos, ALGO_ASTAR);
    if (directP.found && directP.gridPath.size() > 14) {
        for (int offset = 0; offset < 8; offset++) {
            size_t idx = (directP.gridPath.size() * (35 + offset * 5)) / 100;
            Vector2 cand = directP.gridPath[idx];

            Vector2 pPrev = (idx > 0) ? directP.gridPath[idx - 1] : directP.gridPath[idx];
            Vector2 pNext = (idx + 1 < directP.gridPath.size()) ? directP.gridPath[idx + 1] : directP.gridPath[idx];
            Vector2 roadDir = Vector2Normalize(Vector2Subtract(pNext, pPrev));
            Vector2 perp = {-roadDir.y, roadDir.x};

            std::vector<Vector2> blockCandidate = {cand};
            Vector2 perpN1 = {roundf(cand.x + perp.x), roundf(cand.y + perp.y)};
            Vector2 perpN2 = {roundf(cand.x - perp.x), roundf(cand.y - perp.y)};
            if (m_world.IsWalkable((int)perpN1.x, (int)perpN1.y)) {
                blockCandidate.push_back(perpN1);
            }
            if (m_world.IsWalkable((int)perpN2.x, (int)perpN2.y)) {
                blockCandidate.push_back(perpN2);
            }

            // Verify detour exists for all schools
            bool allReachable = true;
            for (const auto& sch : m_schools) {
                PathResult tp = m_pathfinder.FindShortestPath(m_world, m_carStartGridPos, sch.gridPos, ALGO_ASTAR, blockCandidate);
                if (!tp.found) {
                    allReachable = false;
                    break;
                }
            }

            if (allReachable) {
                float rotDeg = atan2f(roadDir.x, roadDir.y) * RAD2DEG;

                RoadObstacle obs1;
                obs1.id = 1;
                obs1.type = OBSTACLE_TENT;
                obs1.name = "Tenda Acara Warga (Hajatan)";
                obs1.gridPos = cand;
                obs1.worldPos = m_world.GridToWorld(cand.x, cand.y, 0.02f);
                obs1.rotationDeg = rotDeg;
                m_obstacles.push_back(obs1);

                for (const auto& bCell : blockCandidate) {
                    m_blockedRoadCells.push_back(bCell);
                }
                break;
            }
        }
    }

    // Obstacle 2: Road work construction
    if (m_schools.size() >= 2) {
        PathResult p2 = m_pathfinder.FindShortestPath(m_world, m_schools[0].gridPos, m_schools.back().gridPos, ALGO_ASTAR, m_blockedRoadCells);
        if (p2.found && p2.gridPath.size() > 14) {
            size_t idx2 = (p2.gridPath.size() * 55) / 100;
            Vector2 cand2 = p2.gridPath[idx2];

            std::vector<Vector2> testBlocked = m_blockedRoadCells;
            testBlocked.push_back(cand2);

            bool allReachable = true;
            for (const auto& sch : m_schools) {
                PathResult tp = m_pathfinder.FindShortestPath(m_world, m_carStartGridPos, sch.gridPos, ALGO_ASTAR, testBlocked);
                if (!tp.found) {
                    allReachable = false;
                    break;
                }
            }

            if (allReachable) {
                RoadObstacle obs2;
                obs2.id = 2;
                obs2.type = OBSTACLE_CONSTRUCTION;
                obs2.name = "Proyek Drainase & Jalan";
                obs2.gridPos = cand2;
                obs2.worldPos = m_world.GridToWorld(cand2.x, cand2.y, 0.0f);
                m_obstacles.push_back(obs2);

                m_blockedRoadCells.push_back(cand2);
            }
        }
    }
}

// =============================================================================
// Pick multiple school destinations on the connected road network
// and initialize SPPG (Dapur Pusat) and schools with Time Windows
// =============================================================================
void PathfinderState::SwitchMapMode(bool useOSM) {
    if (useOSM == m_world.IsGeneratedFromOSM()) {
        return;
    }

    bool success = false;
    if (useOSM) {
        const char* osmMapFile = "assets/osm_pondok_labu_hd.png";
        const char* osmBinFile = "assets/osm_roads_pondok_labu.bin";
        if (FileExists(osmMapFile) && FileExists(osmBinFile)) {
            success = m_world.GenerateFromOSM(osmMapFile, osmBinFile, 4);
            if (success) {
                m_osmMapLoaded = true;
                if (!OSMService::Instance().IsOSMViewEnabled()) {
                    OSMService::Instance().ToggleOSMView();
                }
            }
        }
    } else {
        success = m_world.GenerateFromImage("assets/map.jpg", 6);
        if (success) {
            m_osmMapLoaded = false;
            if (OSMService::Instance().IsOSMViewEnabled()) {
                OSMService::Instance().ToggleOSMView();
            }
        }
    }

    if (success) {
        RandomizePositions();
        TraceLog(LOG_INFO, "Switched map mode: %s", useOSM ? "OpenStreetMap (Real Roads)" : "Offline JPG");
    }
}

void PathfinderState::RandomizePositions() {
    // Clamp school count to [3, MAX_SCHOOLS]
    if (m_numSchools < 3) m_numSchools = 3;
    if (m_numSchools > MAX_SCHOOLS) m_numSchools = MAX_SCHOOLS;

    // Check if authentic OSM landmarks are available in this world
    const auto& osmLms = m_world.GetOSMLandmarks();
    bool useOsmLms = (m_world.IsGeneratedFromOSM() && osmLms.size() >= 4);

    if (useOsmLms) {
        // Depot is authentic Central Kitchen near UPNVJ Campus
        m_depotName = osmLms[0].name;
        m_carStartGridPos = osmLms[0].gridPos;
        m_carCurrentPos = m_world.GridToWorld(m_carStartGridPos.x, m_carStartGridPos.y, 0.05f);

        // Pick distinct authentic schools from OSM landmarks pool
        std::vector<int> availableIndices;
        for (int i = 1; i < (int)osmLms.size(); i++) {
            availableIndices.push_back(i);
        }
        // Shuffle to offer variety when user presses [R] (Acak Lokasi)
        for (size_t i = availableIndices.size() - 1; i > 0; i--) {
            size_t j = (size_t)GetRandomValue(0, (int)i);
            std::swap(availableIndices[i], availableIndices[j]);
        }

        m_schools.clear();
        for (int i = 0; i < m_numSchools && i < (int)availableIndices.size(); i++) {
            int lmIdx = availableIndices[i];
            Vector2 gPos = osmLms[lmIdx].gridPos;

            SchoolDestination sch;
            sch.id = i + 1;
            sch.name = osmLms[lmIdx].name;
            sch.streetName = m_world.GetStreetNameAt((int)gPos.x, (int)gPos.y);
            sch.gridPos = gPos;
            sch.worldPos = m_world.GridToWorld(gPos.x, gPos.y, 0.0f);

            float estDist = Vector2Distance(m_carStartGridPos, gPos);
            int baseMins = std::max(15, (int)(estDist * 0.45f));
            sch.window.start = std::max(10, baseMins - 10);
            sch.window.end   = baseMins + 75 + i * 25;
            sch.unloadTime   = 5;
            sch.color        = LEG_COLORS[i % 6];
            sch.darkColor    = LEG_DARK_COLORS[i % 6];
            sch.reached      = false;
            m_schools.push_back(sch);
        }
    } else {
        // 1. Pick SPPG location
        m_carStartGridPos = m_world.GetRandomRoadPosition();
        m_carCurrentPos = m_world.GridToWorld(m_carStartGridPos.x, m_carStartGridPos.y, 0.05f);
        m_depotName = OSMService::Instance().GetDepotName();

        // 2. Pick distinct schools on connected roads
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

        while ((int)pickedSchoolGridCoords.size() < m_numSchools) {
            Vector2 cand = m_world.GetRandomRoadPosition();
            if (m_world.AreConnected(m_carStartGridPos, cand)) {
                pickedSchoolGridCoords.push_back(cand);
            }
        }

        m_schools.clear();
        for (int i = 0; i < (int)pickedSchoolGridCoords.size(); i++) {
            Vector2 gPos = pickedSchoolGridCoords[i];

            SchoolDestination sch;
            sch.id = i + 1;
            sch.name = OSMService::Instance().GetSchoolName(i);
            sch.streetName = OSMService::Instance().ResolveStreetName(gPos, m_world.GetGridWidth(), m_world.GetGridHeight());
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
    }

    // 4. Generate Road Obstacles (Tenda) & Traffic Congestion Zones
    GenerateObstaclesAndTraffic();

    // Reset Realistic Delivery Clock (07:15:00 WIB = 26100 detik) & Speed
    m_simClockSeconds = 26100.0;
    m_carCurrentSpeedKmh = 45.0f;
    m_carTargetSpeedKmh = 45.0f;

    // 5. Run MBG Routing Algorithm (Graph + Dijkstra + DP Modified / Baseline)
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

    const auto& blocked = m_obstaclesEnabled ? m_blockedRoadCells : std::vector<Vector2>{};
    const auto& traffic = m_trafficEnabled ? m_trafficRoadCells : std::vector<Vector2>{};

    for (int i = 0; i < totalLocs; i++) {
        for (int j = i + 1; j < totalLocs; j++) {
            PathResult p = m_pathfinder.FindShortestPath(m_world, locations[i].gridPos, locations[j].gridPos, m_gridPathAlgo, blocked, traffic);
            pathCache[{locations[i].id, locations[j].id}] = p;

            // Travel time in simulation minutes: 1 world distance unit ~ 0.40 minute + traffic delay!
            float travelMins = p.totalDistanceWorld * 0.40f;
            if (m_trafficEnabled) {
                travelMins += p.trafficDelayMinutes;
            }
            int weight = std::max(1, (int)roundf(travelMins));
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
    m_optimizationLog += TextFormat(" -> [%d] %s (%s)", (int)i + 1, m_schools[i].name.c_str(), LEG_COLOR_NAMES[i]);
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

        Vector2 startPos = (prevId == 0) ? m_carStartGridPos : m_schools[i - 1].gridPos;
        Vector2 goalPos = m_schools[i].gridPos;
        std::string startName = (prevId == 0) ? m_depotName : m_schools[i - 1].name;
        std::string goalName = m_schools[i].name;
        leg.osmNav = OSMService::Instance().QueryRoute(startPos, goalPos, m_world.GetGridWidth(), m_world.GetGridHeight(), startName, goalName);

        // Retrieve road path from cache or compute
        std::pair<int, int> key1 = {prevId, curId};
        std::pair<int, int> key2 = {curId, prevId};

        if (pathCache.count(key1)) {
            leg.path = pathCache[key1];
        } else if (pathCache.count(key2)) {
            leg.path = pathCache[key2];
            std::reverse(leg.path.gridPath.begin(), leg.path.gridPath.end());
            std::reverse(leg.path.worldWaypoints.begin(), leg.path.worldWaypoints.end());
            std::reverse(leg.path.waypointInTraffic.begin(), leg.path.waypointInTraffic.end());
        } else {
            leg.path = m_pathfinder.FindShortestPath(m_world, startPos, goalPos, m_gridPathAlgo, blocked, traffic);
        }

        // Compute original intended path (WITHOUT road blockades/tents)
        PathResult origPath = m_pathfinder.FindShortestPath(m_world, startPos, goalPos, m_gridPathAlgo, {}, traffic);
        leg.originalPath = origPath;
        leg.originalDistanceKm = origPath.totalDistanceWorld * 0.045f;
        leg.detourDistanceKm = leg.path.totalDistanceWorld * 0.045f;

        // Detect if detour is taken
        bool isDifferent = (origPath.gridPath.size() != leg.path.gridPath.size());
        if (!isDifferent) {
            for (size_t k = 0; k < origPath.gridPath.size(); k++) {
                if (origPath.gridPath[k].x != leg.path.gridPath[k].x ||
                    origPath.gridPath[k].y != leg.path.gridPath[k].y) {
                    isDifferent = true;
                    break;
                }
            }
        }

        leg.hasDetour = (m_obstaclesEnabled && isDifferent);
        if (leg.hasDetour) {
            leg.extraDetourKm = std::max(0.0f, leg.detourDistanceKm - leg.originalDistanceKm);
            leg.extraDetourMinutes = leg.extraDetourKm * 2.5f;

            // Find branch point (where detour diverges from original path)
            size_t branchIdx = 0;
            while (branchIdx < leg.path.gridPath.size() && branchIdx < origPath.gridPath.size() &&
                   leg.path.gridPath[branchIdx].x == origPath.gridPath[branchIdx].x &&
                   leg.path.gridPath[branchIdx].y == origPath.gridPath[branchIdx].y) {
                branchIdx++;
            }
            if (branchIdx > 0 && branchIdx <= leg.path.gridPath.size()) {
                leg.detourBranchGrid = leg.path.gridPath[branchIdx - 1];
            }

            // Find merge point (where detour rejoins original path)
            int endDetour = (int)leg.path.gridPath.size() - 1;
            int endOrig = (int)origPath.gridPath.size() - 1;
            while (endDetour >= 0 && endOrig >= 0 &&
                   leg.path.gridPath[endDetour].x == origPath.gridPath[endOrig].x &&
                   leg.path.gridPath[endDetour].y == origPath.gridPath[endOrig].y) {
                endDetour--;
                endOrig--;
            }
            if (endDetour + 1 < (int)leg.path.gridPath.size()) {
                leg.detourMergeGrid = leg.path.gridPath[endDetour + 1];
            }

            // Build 3D hazard-striped ribbon for the original blocked route
            if (origPath.found && origPath.gridPath.size() >= 2) {
                leg.origBlockedModel = m_pathfinder.BuildPathMeshModel(
                    m_world, origPath.gridPath, (Color){230, 45, 45, 200}, {}, 0.40f, 0.09f, false, true);
                leg.origModelLoaded = true;
            }
        }

        m_totalRouteDistance += leg.path.totalDistanceWorld;
        m_totalNodesExplored += leg.path.nodesExplored;
        m_totalExecutionTimeMs += leg.path.executionTimeMs;

        if (leg.path.found && leg.path.gridPath.size() >= 2) {
            // 1. Crisp white underlay border
            leg.borderModel = m_pathfinder.BuildPathMeshModel(m_world, leg.path.gridPath, WHITE, leg.path.waypointInTraffic, 0.70f, 0.08f, true);
            // 2. Core colored ribbon for this school leg (traffic = RED, clear = leg.color)
            leg.pathModel = m_pathfinder.BuildPathMeshModel(m_world, leg.path.gridPath, leg.color, leg.path.waypointInTraffic, 0.48f, 0.14f, false);
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
        returnLeg.destinationName = m_depotName;
        returnLeg.color = (Color){255, 185, 0, 255}; // Vibrant Amber/Gold matching SPPG
        returnLeg.isReturnToDepot = true;

        Vector2 startPos = m_schools.back().gridPos;
        Vector2 goalPos = m_carStartGridPos;
        returnLeg.osmNav = OSMService::Instance().QueryRoute(startPos, goalPos, m_world.GetGridWidth(), m_world.GetGridHeight(), m_schools.back().name, m_depotName);

        std::pair<int, int> key1 = {lastSchoolId, 0};
        std::pair<int, int> key2 = {0, lastSchoolId};

        if (pathCache.count(key1)) {
            returnLeg.path = pathCache[key1];
        } else if (pathCache.count(key2)) {
            returnLeg.path = pathCache[key2];
            std::reverse(returnLeg.path.gridPath.begin(), returnLeg.path.gridPath.end());
            std::reverse(returnLeg.path.worldWaypoints.begin(), returnLeg.path.worldWaypoints.end());
            std::reverse(returnLeg.path.waypointInTraffic.begin(), returnLeg.path.waypointInTraffic.end());
        } else {
            returnLeg.path = m_pathfinder.FindShortestPath(m_world, startPos, goalPos, m_gridPathAlgo, blocked, traffic);
        }

        // Check detour on return leg
        PathResult origRetPath = m_pathfinder.FindShortestPath(m_world, startPos, goalPos, m_gridPathAlgo, {}, traffic);
        returnLeg.originalPath = origRetPath;
        returnLeg.originalDistanceKm = origRetPath.totalDistanceWorld * 0.045f;
        returnLeg.detourDistanceKm = returnLeg.path.totalDistanceWorld * 0.045f;

        bool retDiff = (origRetPath.gridPath.size() != returnLeg.path.gridPath.size());
        if (!retDiff) {
            for (size_t k = 0; k < origRetPath.gridPath.size(); k++) {
                if (origRetPath.gridPath[k].x != returnLeg.path.gridPath[k].x ||
                    origRetPath.gridPath[k].y != returnLeg.path.gridPath[k].y) {
                    retDiff = true;
                    break;
                }
            }
        }
        returnLeg.hasDetour = (m_obstaclesEnabled && retDiff);
        if (returnLeg.hasDetour) {
            returnLeg.extraDetourKm = std::max(0.0f, returnLeg.detourDistanceKm - returnLeg.originalDistanceKm);
            returnLeg.extraDetourMinutes = returnLeg.extraDetourKm * 2.5f;
            if (origRetPath.found && origRetPath.gridPath.size() >= 2) {
                returnLeg.origBlockedModel = m_pathfinder.BuildPathMeshModel(
                    m_world, origRetPath.gridPath, (Color){230, 45, 45, 200}, {}, 0.40f, 0.09f, false, true);
                returnLeg.origModelLoaded = true;
            }
        }

        m_totalRouteDistance += returnLeg.path.totalDistanceWorld;
        m_totalNodesExplored += returnLeg.path.nodesExplored;
        m_totalExecutionTimeMs += returnLeg.path.executionTimeMs;

        if (returnLeg.path.found && returnLeg.path.gridPath.size() >= 2) {
            returnLeg.borderModel = m_pathfinder.BuildPathMeshModel(m_world, returnLeg.path.gridPath, WHITE, returnLeg.path.waypointInTraffic, 0.70f, 0.08f, true);
            returnLeg.pathModel = m_pathfinder.BuildPathMeshModel(m_world, returnLeg.path.gridPath, returnLeg.color, returnLeg.path.waypointInTraffic, 0.48f, 0.14f, false);
            returnLeg.modelsLoaded = true;
        }

        m_routeLegs.push_back(returnLeg);
    m_optimizationLog += " -> [Akhir] SPPG (Kembali ke Titik Awal)";
    }

    // Preserve car driving progress on current leg if car was already moving
    if (m_currentLegIndex >= 0 && m_currentLegIndex < (int)m_routeLegs.size()) {
        const auto& leg = m_routeLegs[m_currentLegIndex];
        if (leg.path.found && !leg.path.worldWaypoints.empty()) {
            float bestDist = 1e9f;
            int bestIdx = 0;
            for (size_t w = 0; w < leg.path.worldWaypoints.size(); w++) {
                float d = Vector3Distance(m_carCurrentPos, leg.path.worldWaypoints[w]);
                if (d < bestDist) {
                    bestDist = d;
                    bestIdx = (int)w;
                }
            }
            m_legDriveProgress = (float)bestIdx;
        }
    }

    UpdateNavigationStatus();
}

void PathfinderState::UpdateNavigationStatus() {
    if (m_routeLegs.empty() || m_currentLegIndex >= (int)m_routeLegs.size()) {
        m_turnInstruction = "Selesai!";
        m_distanceToCurrentTarget = 0.0f;
        m_totalDistanceRemaining = 0.0f;
        m_realisticEtaMinutes = 0.0f;
        m_trafficDelayMinutes = 0.0f;
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
        m_realisticEtaMinutes = 0.0f;
        return;
    }

    // Distance remaining on current leg and traffic congestion distance
    int numWp = (int)curLeg.path.worldWaypoints.size();
    int curIdx = std::clamp((int)m_legDriveProgress, 0, numWp - 1);
    float distLeg = 0.0f;
    float trafficDistLeg = 0.0f;

    if (curIdx < numWp) {
        float d0 = Vector3Distance(m_carCurrentPos, curLeg.path.worldWaypoints[curIdx]);
        distLeg += d0;
        if (curIdx < (int)curLeg.path.waypointInTraffic.size() && curLeg.path.waypointInTraffic[curIdx]) {
            trafficDistLeg += d0;
        }
        for (size_t i = curIdx + 1; i < curLeg.path.worldWaypoints.size(); i++) {
            float seg = Vector3Distance(curLeg.path.worldWaypoints[i - 1], curLeg.path.worldWaypoints[i]);
            distLeg += seg;
            if (i < curLeg.path.waypointInTraffic.size() && curLeg.path.waypointInTraffic[i]) {
                trafficDistLeg += seg;
            }
        }
    }
    m_distanceToCurrentTarget = distLeg;

    // Realistic delivery ETA calculation in minutes
    // Free distance at ~45 km/h (12.5 m/s) + Traffic distance at ~12 km/h (3.3 m/s)
    float freeMeters = std::max(0.0f, (distLeg - trafficDistLeg) * 25.0f);
    float trafficMeters = trafficDistLeg * 25.0f;

    float freeTravelSecs = freeMeters / 12.5f;
    float trafficTravelSecs = trafficMeters / 3.3f;
    m_trafficDelayMinutes = (m_trafficEnabled ? (trafficTravelSecs - (trafficMeters / 12.5f)) : 0.0f) / 60.0f;

    float totalSecs = freeTravelSecs + (m_trafficEnabled ? trafficTravelSecs : (trafficMeters / 12.5f));
    m_realisticEtaMinutes = std::max(1.0f, ceilf(totalSecs / 60.0f));

    // Total distance across all remaining unvisited legs
    float totalRem = distLeg;
    for (size_t l = m_currentLegIndex + 1; l < m_routeLegs.size(); l++) {
        totalRem += m_routeLegs[l].path.totalDistanceWorld;
    }
    m_totalDistanceRemaining = totalRem;

    // OpenStreetMap Real Turn-by-Turn Guidance & Street Resolver
    Vector2 carGrid = m_world.WorldToGrid(m_carCurrentPos);
    OSMService::Instance().UpdateNavigationGuidance(
        carGrid, m_currentLegIndex, m_legDriveProgress,
        curLeg.path.gridPath, curLeg.path.worldWaypoints,
        curLeg.isReturnToDepot, curLeg.destinationName,
        m_turnInstruction, m_currentStreetName, m_turnIcon, m_distToNextManeuverMeters
    );
    return;
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

    dt = std::clamp(dt, 0.0f, 0.05f);

    // Advance realistic simulation clock (1 real second = 15 sim seconds * multiplier)
    m_simClockSeconds += (double)(dt * 15.0f * m_simSpeedMultiplier);

    // Detect if current car position is inside a traffic congestion zone
    Vector2 curCarGrid = m_world.WorldToGrid(m_carCurrentPos);
    m_isCarInTraffic = m_trafficEnabled && IsGridInTraffic(curCarGrid);

    // Realistic vehicle speed (km/h) simulation
    if (m_isUnloading) {
        m_carTargetSpeedKmh = 0.0f;
        m_speedStatusStr = "Bongkar Muatan (0 km/j)";
    } else if (m_isCarInTraffic) {
        m_carTargetSpeedKmh = 12.0f; // Macet padat merayap
        m_speedStatusStr = "Macet Parah (12 km/j)";
    } else {
        bool nearTurn = (m_turnInstruction.find("Belok") != std::string::npos);
        m_carTargetSpeedKmh = nearTurn ? 24.0f : 48.0f;
        m_speedStatusStr = nearTurn ? "Menikung (24 km/j)" : "Lancar (48 km/j)";
    }

    // Smooth speed interpolation (acceleration / deceleration) with clamped alpha
    float speedLerpFactor = std::clamp(3.5f * dt, 0.0f, 1.0f);
    m_carCurrentSpeedKmh = Lerp(m_carCurrentSpeedKmh, m_carTargetSpeedKmh, speedLerpFactor);
    m_carCurrentSpeedKmh = std::clamp(m_carCurrentSpeedKmh, 0.0f, 120.0f);

    // Dynamic movement speed scaled by km/h and simulation speed multiplier
    m_driveSpeed = std::max(0.0f, (m_carCurrentSpeedKmh / 45.0f) * 10.5f * m_simSpeedMultiplier);

    // Handle unloading simulation at school
    if (m_isUnloading) {
        m_unloadTimer -= dt * m_simSpeedMultiplier;
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
    int numWp = (int)curLeg.path.worldWaypoints.size();
    if (!curLeg.path.found || numWp < 2) return;

    m_legDriveProgress = std::max(0.0f, m_legDriveProgress);
    int curIdx = std::clamp((int)m_legDriveProgress, 0, numWp - 1);
    int nextIdx = curIdx + 1;

    // Check if car reached current destination
    if (nextIdx >= numWp) {
        m_carCurrentPos = curLeg.path.worldWaypoints.back();

        if (curLeg.isReturnToDepot) {
            // ARRIVED BACK AT STARTING POINT (SPPG)!
            m_allDelivered = true;
            m_distanceToCurrentTarget = 0.0f;
            m_totalDistanceRemaining = 0.0f;
            m_arrivalTimer += dt * m_simSpeedMultiplier;
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
    m_legDriveProgress = std::max(0.0f, m_legDriveProgress + step);

    curIdx = std::clamp((int)m_legDriveProgress, 0, numWp - 1);
    nextIdx = curIdx + 1;

    if (curIdx >= 0 && nextIdx < numWp) {
        float frac = std::clamp(m_legDriveProgress - (float)curIdx, 0.0f, 1.0f);
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
    if (dt > 0.05f) dt = 0.05f; // Protect against lag spike on initialization / window moving
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
    if (IsKeyPressed(KEY_N)) m_showItinerarySheet = !m_showItinerarySheet;
    if (IsKeyPressed(KEY_P)) m_showLocationModal = !m_showLocationModal;
    if (IsKeyPressed(KEY_V)) SwitchMapMode(!m_world.IsGeneratedFromOSM());
    if (IsKeyPressed(KEY_ESCAPE)) {
        if (m_showItinerarySheet) m_showItinerarySheet = false;
        else if (m_showLocationModal) m_showLocationModal = false;
        else if (m_mapFullscreen) {
            m_mapFullscreen = false;
            m_mapZoom = 1.0f;
            m_mapPan = {0, 0};
        }
    }
    if (IsKeyPressed(KEY_H)) m_showMinimap = !m_showMinimap;
    if (IsKeyPressed(KEY_O)) {
        m_obstaclesEnabled = !m_obstaclesEnabled;
        OptimizeAndBuildRoutes();
    }
    if (IsKeyPressed(KEY_T)) {
        m_trafficEnabled = !m_trafficEnabled;
        OptimizeAndBuildRoutes();
    }
    if (IsKeyPressed(KEY_B)) {
        m_showOriginalRoute = !m_showOriginalRoute;
    }
    if (IsKeyPressed(KEY_X)) {
        if (m_simSpeedMultiplier == 1.0f) m_simSpeedMultiplier = 2.0f;
        else if (m_simSpeedMultiplier == 2.0f) m_simSpeedMultiplier = 4.0f;
        else m_simSpeedMultiplier = 1.0f;
    }

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

    // 2. Draw Multi-School Route Lines (Traffic segments are colored RED!)
    // First, draw the Original Blocked Path (Jalur Semestinya) if enabled
    if (m_obstaclesEnabled && m_showOriginalRoute) {
        for (const auto& leg : m_routeLegs) {
            if (leg.hasDetour && leg.origModelLoaded) {
                DrawModel(leg.origBlockedModel, {0, 0, 0}, 1.0f, WHITE);
            }
        }
    }

    // Draw 3D Detour Branch Indicators where dynamic detour diverges from original path
    if (m_obstaclesEnabled) {
        for (const auto& leg : m_routeLegs) {
            if (!leg.hasDetour) continue;
            Vector3 branchW = m_world.GridToWorld(leg.detourBranchGrid.x, leg.detourBranchGrid.y, 0.08f);

            // Glowing green detour ring & marker on pavement
            float dPulse = 1.1f + 0.25f * sinf(m_pulseTimer * 5.0f);
            DrawCircle3D(branchW, dPulse, (Vector3){1, 0, 0}, 90.0f, Fade((Color){40, 225, 90, 255}, 0.55f));
            DrawSphere(Vector3Add(branchW, (Vector3){0, 1.2f, 0}), 0.28f, (Color){40, 225, 90, 255});
            DrawCylinder(Vector3Add(branchW, (Vector3){0, 0.5f, 0}), 0.06f, 0.06f, 1.0f, 6, WHITE);
        }
    }

    // Then draw the Active Dynamic Detour Routes
    for (size_t i = 0; i < m_routeLegs.size(); i++) {
        const auto& leg = m_routeLegs[i];
        if (!leg.modelsLoaded) continue;

        Color tint = (leg.targetSchoolIndex < m_currentLegIndex) ? Fade(WHITE, 0.60f) : WHITE;
        DrawModel(leg.borderModel, {0, 0, 0}, 1.0f, tint);
        DrawModel(leg.pathModel, {0, 0, 0}, 1.0f, tint);
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

    // 6. Draw 3D Road Obstacles (Tenda Acara Warga di Tengah Jalan & Perbaikan Jalan)
    if (m_obstaclesEnabled) {
        for (const auto& obs : m_obstacles) {
            if (obs.type == OBSTACLE_TENT && m_tentLoaded) {
                // Direction vectors of the road at the tent
                float rad = obs.rotationDeg * DEG2RAD;
                Vector3 roadFwd = {sinf(rad), 0, cosf(rad)};
                Vector3 roadPerp = {-cosf(rad), 0, sinf(rad)};

    // 1. Draw 3D International "No Entry / Road Closed" [X] decal on the asphalt road surface
                DrawCircle3D(Vector3Add(obs.worldPos, (Vector3){0, 0.05f, 0}), 1.6f, (Vector3){1, 0, 0}, 90.0f, Fade((Color){230, 35, 35, 255}, 0.75f));
                DrawCube(Vector3Add(obs.worldPos, (Vector3){0, 0.06f, 0}), 1.3f, 0.02f, 0.32f, WHITE);

                // 2. Draw 3D Tent Model centered directly in the middle of the road
                float tentScale = 0.24f;
                DrawModelEx(m_tentModel, obs.worldPos, (Vector3){0, 1, 0}, obs.rotationDeg,
                            (Vector3){tentScale, tentScale, tentScale}, WHITE);

                // 3. Barricade posts on left and right sides of the road (blocking passage past tent)
                Vector3 postL = Vector3Add(obs.worldPos, Vector3Scale(roadPerp, 0.70f));
                Vector3 postR = Vector3Subtract(obs.worldPos, Vector3Scale(roadPerp, 0.70f));
                DrawCylinder(postL, 0.08f, 0.08f, 0.70f, 8, (Color){240, 120, 0, 255});
                DrawCylinder(postR, 0.08f, 0.08f, 0.70f, 8, (Color){240, 120, 0, 255});
                DrawCylinder(postL, 0.09f, 0.09f, 0.20f, 8, WHITE);
                DrawCylinder(postR, 0.09f, 0.09f, 0.20f, 8, WHITE);

                // 4. Safety traffic cones in front and behind the tent along the blocked road
                Vector3 coneFront = Vector3Add(obs.worldPos, Vector3Scale(roadFwd, 1.30f));
                Vector3 coneBack  = Vector3Subtract(obs.worldPos, Vector3Scale(roadFwd, 1.30f));
                DrawCylinder(Vector3Add(coneFront, (Vector3){0, 0.16f, 0}), 0.04f, 0.15f, 0.32f, 8, (Color){255, 120, 0, 255});
                DrawCylinder(Vector3Add(coneBack, (Vector3){0, 0.16f, 0}), 0.04f, 0.15f, 0.32f, 8, (Color){255, 120, 0, 255});

                // 5. Flashing warning beacon light on top of the tent
                float beaconPulse = 0.6f + 0.4f * sinf(m_pulseTimer * 8.0f);
                DrawSphere(Vector3Add(obs.worldPos, (Vector3){0, 1.05f, 0}), 0.20f, Fade((Color){255, 180, 0, 255}, beaconPulse));
                DrawCircle3D(Vector3Add(obs.worldPos, (Vector3){0, 0.07f, 0}), 2.0f, (Vector3){1, 0, 0}, 90.0f, Fade(RED, 0.35f));
            } else {
                // Construction / Road work barricade
                DrawCube(Vector3Add(obs.worldPos, (Vector3){0, 0.25f, 0}), 0.95f, 0.5f, 0.25f, (Color){255, 120, 0, 255});
                DrawCubeWires(Vector3Add(obs.worldPos, (Vector3){0, 0.25f, 0}), 0.95f, 0.5f, 0.25f, BLACK);

                float beaconPulse = 0.6f + 0.4f * sinf(m_pulseTimer * 10.0f);
                DrawSphere(Vector3Add(obs.worldPos, (Vector3){0, 0.65f, 0}), 0.16f, Fade(RED, beaconPulse));
                DrawCircle3D(Vector3Add(obs.worldPos, (Vector3){0, 0.06f, 0}), 1.2f, (Vector3){1, 0, 0}, 90.0f, Fade(RED, 0.45f));
            }
        }
    }

    // 7. Draw 3D Traffic Congestion Indicators (Ground Halos)
    if (m_trafficEnabled) {
        for (const auto& tz : m_trafficZones) {
            float rPulse = tz.radiusGrid * 0.5f + 0.25f * sinf(m_pulseTimer * 3.0f);
            DrawCircle3D(Vector3Add(tz.centerWorld, (Vector3){0, 0.05f, 0}), rPulse, (Vector3){1, 0, 0}, 90.0f, Fade(RED, 0.18f));
            DrawCircle3D(Vector3Add(tz.centerWorld, (Vector3){0, 0.05f, 0}), 1.2f, (Vector3){1, 0, 0}, 90.0f, Fade(RED, 0.40f));
        }
    }

    // 8. Draw 3D LIDAR sensor rays cast from vehicle
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
    int navCardW = 430;
    int navCardH = m_allDelivered ? 100 : 158;

    bool isReturningToDepot = (!m_routeLegs.empty() && m_currentLegIndex < (int)m_routeLegs.size() && m_routeLegs[m_currentLegIndex].isReturnToDepot);

    Color curHeaderBg = m_allDelivered ? (Color){24, 134, 75, 250} :
                        (m_isUnloading ? (Color){210, 120, 10, 250} :
                        (isReturningToDepot ? (Color){220, 155, 0, 250} :
                        ((!m_routeLegs.empty() && m_currentLegIndex < (int)m_routeLegs.size()) ?
                         Fade(m_routeLegs[m_currentLegIndex].color, 0.96f) : (Color){26, 115, 232, 250})));

    DrawRectangleRounded((Rectangle){(float)navCardX, (float)navCardY, (float)navCardW, (float)navCardH}, 0.10f, 6, curHeaderBg);
    DrawRectangleRoundedLinesEx((Rectangle){(float)navCardX, (float)navCardY, (float)navCardW, (float)navCardH}, 0.10f, 6, 2.0f, Fade(WHITE, 0.45f));

    // Calculate realistic simulation clock:
    int clockSecs = (int)m_simClockSeconds % 86400;
    int curH = clockSecs / 3600;
    int curM = (clockSecs % 3600) / 60;
    int curS = clockSecs % 60;

    int arrSecs = (int)(m_simClockSeconds + m_realisticEtaMinutes * 60.0f) % 86400;
    int arrH = arrSecs / 3600;
    int arrM = (arrSecs % 3600) / 60;

    // Top status strip: Clock & Speedometer Badge
    DrawTextCustom(TextFormat("JAM: %02d:%02d:%02d WIB", curH, curM, curS), navCardX + 18, navCardY + 12, 12.0f, (Color){255, 250, 220, 255}, FONT_BOLD);
    std::string osmBadge = OSMService::Instance().GetStatusString();
    Color osmBadgeCol = OSMService::Instance().IsOnline() ? (Color){180, 255, 200, 255} : (Color){255, 220, 160, 255};
    DrawTextCustom(osmBadge.c_str(), navCardX + 155, navCardY + 13, 10.5f, osmBadgeCol, FONT_SEMIBOLD);

    // Speedometer badge
    int speedVal = (int)roundf(m_carCurrentSpeedKmh);
    Color speedBadgeBg = m_isUnloading ? (Color){230, 130, 20, 255} :
                         (m_isCarInTraffic ? (Color){220, 35, 35, 255} :
                         ((m_speedStatusStr.find("Menikung") != std::string::npos) ? (Color){200, 160, 20, 255} : (Color){34, 180, 80, 255}));
    const char* speedStatusShort = m_isUnloading ? "BONGKAR" : (m_isCarInTraffic ? "MACET" : ((m_speedStatusStr.find("Menikung") != std::string::npos) ? "TIKUNGAN" : "LANCAR"));
    const char* speedText = TextFormat("%d KM/J | %s", speedVal, speedStatusShort);
    float spdW = MeasureTextCustom(speedText, 10.5f, FONT_BOLD) + 16;
    DrawRectangleRounded((Rectangle){navCardX + navCardW - spdW - 14, (float)navCardY + 9, spdW, 20}, 0.35f, 4, speedBadgeBg);
    DrawTextCustom(speedText, navCardX + navCardW - spdW - 6, navCardY + 13, 10.5f, WHITE, FONT_BOLD);

    DrawLine(navCardX + 16, navCardY + 34, navCardX + navCardW - 16, navCardY + 34, Fade(WHITE, 0.25f));

    if (m_allDelivered) {
        DrawTextCustom("MISI PENGANTARAN SELESAI!", navCardX + 20, navCardY + 44, 17, WHITE, FONT_BOLD);
        DrawTextCustom(TextFormat("Seluruh %d sekolah terlayani & mobil kembali ke SPPG tepat waktu!", (int)m_schools.size()),
                       navCardX + 20, navCardY + 68, 11.5f, (Color){220, 255, 230, 255}, FONT_REGULAR);
    } else if (isReturningToDepot) {
        const char* retIcon = (m_turnInstruction.find("Kanan") != std::string::npos) ? "->" :
                              ((m_turnInstruction.find("Kiri") != std::string::npos) ? "<-" : "^");
        DrawTextCustom(retIcon, navCardX + 20, navCardY + 44, 26, WHITE, FONT_BOLD);
        DrawTextCustom(m_turnInstruction.c_str(), navCardX + 58, navCardY + 42, 16.5f, WHITE, FONT_BOLD);
        DrawTextCustom(TextFormat("Sedang di: %s", m_currentStreetName.c_str()), navCardX + 58, navCardY + 63, 11.5f, (Color){255, 245, 170, 255}, FONT_SEMIBOLD);
        DrawTextCustom(TextFormat("Langkah Akhir: Kembali ke %s", m_depotName.c_str()), navCardX + 58, navCardY + 81, 11.0f, WHITE, FONT_REGULAR);

        float metersCurrent = m_distanceToCurrentTarget * 25.0f;
        float kmCurrent = metersCurrent / 1000.0f;
        DrawTextCustom(TextFormat("Jarak: %.2f km (%.0f m) | ETA: %.0f Menit (Tiba ~%02d:%02d WIB)",
                                  kmCurrent, metersCurrent, m_realisticEtaMinutes, arrH, arrM),
                       navCardX + 58, navCardY + 99, 11.5f, (Color){255, 255, 180, 255}, FONT_REGULAR);

        if (m_isCarInTraffic) {
            DrawTextCustom("[!] Zona Macet: Kecepatan mobil turun ke 12 km/jam", navCardX + 58, navCardY + 119, 11.0f, (Color){255, 130, 130, 255}, FONT_BOLD);
        } else {
            DrawTextCustom("[OK] Menuju pangkalan dapur pusat SPPG", navCardX + 58, navCardY + 119, 11.0f, (Color){230, 255, 220, 255}, FONT_REGULAR);
        }
    } else {
        const char* iconStr = m_isUnloading ? ">" :
                              ((m_turnInstruction.find("Kanan") != std::string::npos) ? "->" :
                              ((m_turnInstruction.find("Kiri") != std::string::npos) ? "<-" : "^"));
        DrawTextCustom(iconStr, navCardX + 20, navCardY + 44, 26, WHITE, FONT_BOLD);
        DrawTextCustom(m_turnInstruction.c_str(), navCardX + 58, navCardY + 42, 16.5f, WHITE, FONT_BOLD);

        int targetId = m_currentLegIndex + 1;
        const char* colName = (m_currentLegIndex < 6) ? LEG_COLOR_NAMES[m_currentLegIndex] : "Warna";
        const char* schName = (m_currentLegIndex < (int)m_schools.size()) ? m_schools[m_currentLegIndex].name.c_str() : "Sekolah";

        DrawTextCustom(TextFormat("Sedang di: %s", m_currentStreetName.c_str()), navCardX + 58, navCardY + 63, 11.5f, (Color){255, 245, 170, 255}, FONT_SEMIBOLD);
        DrawTextCustom(TextFormat("Tujuan: %s (Stop %d dari %d | Jalur %s)", schName, targetId, (int)m_schools.size(), colName),
                       navCardX + 58, navCardY + 81, 11.0f, WHITE, FONT_REGULAR);

        float metersCurrent = m_distanceToCurrentTarget * 25.0f;
        float kmCurrent = metersCurrent / 1000.0f;
        DrawTextCustom(TextFormat("Jarak: %.2f km (%.0f m) | ETA: %.0f Menit (Tiba ~%02d:%02d WIB)",
                                  kmCurrent, metersCurrent, m_realisticEtaMinutes, arrH, arrM),
                       navCardX + 58, navCardY + 99, 11.5f, (Color){255, 255, 180, 255}, FONT_REGULAR);

        if (m_isCarInTraffic) {
            DrawTextCustom(TextFormat("[!] Zona Macet (+%.0f mnt delay) - Kecepatan 12 km/jam", m_trafficDelayMinutes),
                           navCardX + 58, navCardY + 119, 11.0f, (Color){255, 140, 140, 255}, FONT_BOLD);
        } else if (m_currentLegIndex < (int)m_schools.size()) {
            const auto& w = m_schools[m_currentLegIndex].window;
            DrawTextCustom(TextFormat("Window Pengantaran: [%d - %d mnt] | Bongkar: 5m", w.start, w.end),
                           navCardX + 58, navCardY + 119, 11.0f, (Color){230, 245, 255, 255}, FONT_REGULAR);
        }
    }

    // =========================================================================
    // 2. Control Toolbar (Top Right / Middle)
    // =========================================================================
    int toolX = navCardX + navCardW + 14;
    int toolY = 14;

    bool isModified = (m_routingAlgorithm == ROUTING_DP_MODIFIED);
    StateButton btnMod       = {{(float)toolX,        (float)toolY, 125, 36}, "DP Modified [TAB]", isModified};
    StateButton btnBaseAlgo  = {{(float)toolX + 130,  (float)toolY, 105, 36}, "DP Baseline",       !isModified};
    StateButton btnRandomize = {{(float)toolX + 240,  (float)toolY, 105, 36}, "Acak Rute [R]",     false};
    StateButton btnDrive     = {{(float)toolX + 350,  (float)toolY, 105, 36}, m_isDriving ? "Jeda [SPACE]" : "Auto Jalan", m_isDriving};
    StateButton btnItinerary = {{(float)toolX + 460,  (float)toolY, 105, 36}, "Rute OSM [N]",     m_showItinerarySheet};
    StateButton btnLocation  = {{(float)toolX + 570,  (float)toolY,  90, 36}, "Lokasi [P]",        m_showLocationModal};
    StateButton btnMap       = {{(float)toolX + 665,  (float)toolY,  85, 36}, m_mapFullscreen ? "Tutup [M]" : "Peta [M]", m_mapFullscreen};

    // Row 2 of controls (Obstacles, Traffic, Simulation Speed, School Count, Camera)
    int row2Y = toolY + 42;
    StateButton btnObstacle  = {{(float)toolX,        (float)row2Y, 125, 32}, m_obstaclesEnabled ? "Hambatan: ON [O]" : "Hambatan: OFF [O]", m_obstaclesEnabled};
    StateButton btnTraffic   = {{(float)toolX + 130,  (float)row2Y, 105, 32}, m_trafficEnabled ? "Macet: ON [T]" : "Macet: OFF [T]", m_trafficEnabled};
    StateButton btnSimSpeed  = {{(float)toolX + 240,  (float)row2Y, 105, 32}, TextFormat("Speed: %.0fx [X]", m_simSpeedMultiplier), m_simSpeedMultiplier > 1.0f};
    StateButton btnOrigRoute = {{(float)toolX + 350,  (float)row2Y, 105, 32}, m_showOriginalRoute ? "Jalur Asli [B]" : "Jalur Asli: OFF", m_showOriginalRoute};
    StateButton btnOSMView   = {{(float)toolX + 460,  (float)row2Y, 105, 32}, m_world.IsGeneratedFromOSM() ? "Peta: OSM [V]" : "Peta: JPG [V]", m_world.IsGeneratedFromOSM()};

    StateButton btnMinusSchool = {{(float)toolX + 570, (float)row2Y, 26, 32}, "[-]", false};
    StateButton btnSchoolCount = {{(float)toolX + 600, (float)row2Y, 60, 32}, TextFormat("%d Sek", m_numSchools), true};
    StateButton btnPlusSchool  = {{(float)toolX + 664, (float)row2Y, 26, 32}, "[+]", false};

    StateButton btnCam = {{(float)toolX + 694, (float)row2Y, 70, 32},
                          (m_camMode == APP_CAM_FOLLOW_CAR) ? "Truk [C]" : "Sek [S]",
                          m_camMode == APP_CAM_FOLLOW_CAR};

    Color btnBase = {240, 244, 250, 255};
    Color btnHover = {215, 230, 250, 255};

    btnMod.Draw(m_fontSemiBold, btnBase, btnHover, LEG_COLORS[0], isModified ? WHITE : BLACK);
    btnBaseAlgo.Draw(m_fontSemiBold, btnBase, btnHover, LEG_COLORS[2], !isModified ? WHITE : BLACK);
    btnRandomize.Draw(m_fontSemiBold, btnBase, btnHover, LEG_COLORS[1], BLACK);
    btnDrive.Draw(m_fontSemiBold, btnBase, btnHover, {230, 60, 60, 255}, m_isDriving ? WHITE : BLACK);
    btnItinerary.Draw(m_fontSemiBold, btnBase, btnHover, (Color){50, 160, 230, 255}, m_showItinerarySheet ? WHITE : BLACK);
    btnLocation.Draw(m_fontSemiBold, btnBase, btnHover, (Color){150, 90, 240, 255}, m_showLocationModal ? WHITE : BLACK);
    btnMap.Draw(m_fontSemiBold, btnBase, btnHover, (Color){30, 144, 255, 255}, m_mapFullscreen ? WHITE : BLACK);

    btnObstacle.Draw(m_fontSemiBold, btnBase, btnHover, (Color){240, 140, 20, 255}, m_obstaclesEnabled ? WHITE : BLACK);
    btnTraffic.Draw(m_fontSemiBold, btnBase, btnHover, (Color){230, 50, 50, 255}, m_trafficEnabled ? WHITE : BLACK);
    btnSimSpeed.Draw(m_fontSemiBold, btnBase, btnHover, (Color){40, 180, 80, 255}, m_simSpeedMultiplier > 1.0f ? WHITE : BLACK);
    btnOrigRoute.Draw(m_fontSemiBold, btnBase, btnHover, (Color){70, 180, 255, 255}, m_showOriginalRoute ? WHITE : BLACK);
    btnOSMView.Draw(m_fontSemiBold, btnBase, btnHover, (Color){0, 180, 140, 255}, m_world.IsGeneratedFromOSM() ? WHITE : BLACK);

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
    if (btnItinerary.IsClicked()) {
        m_showItinerarySheet = !m_showItinerarySheet;
    }
    if (btnLocation.IsClicked()) {
        m_showLocationModal = !m_showLocationModal;
    }
    if (btnMap.IsClicked()) {
        m_mapFullscreen = !m_mapFullscreen;
        if (!m_mapFullscreen) {
            m_mapZoom = 1.0f;
            m_mapPan = {0, 0};
        }
    }
    if (btnObstacle.IsClicked()) {
        m_obstaclesEnabled = !m_obstaclesEnabled;
        OptimizeAndBuildRoutes();
    }
    if (btnTraffic.IsClicked()) {
        m_trafficEnabled = !m_trafficEnabled;
        OptimizeAndBuildRoutes();
    }
    if (btnSimSpeed.IsClicked()) {
        if (m_simSpeedMultiplier == 1.0f) m_simSpeedMultiplier = 2.0f;
        else if (m_simSpeedMultiplier == 2.0f) m_simSpeedMultiplier = 4.0f;
        else m_simSpeedMultiplier = 1.0f;
    }
    if (btnOrigRoute.IsClicked()) {
        m_showOriginalRoute = !m_showOriginalRoute;
    }
    if (btnOSMView.IsClicked()) {
        SwitchMapMode(!m_world.IsGeneratedFromOSM());
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

    // Detour status banner below toolbar - show when road blockade is active
    if (m_obstaclesEnabled) {
        bool anyDetour = false;
        float extraKm = 0.0f;
        float extraMin = 0.0f;
        for (const auto& leg : m_routeLegs) {
            if (leg.hasDetour) {
                anyDetour = true;
                extraKm += leg.extraDetourKm;
                extraMin += leg.extraDetourMinutes;
            }
        }
        if (anyDetour) {
            int bannerY = row2Y + 38;
            float pulse = 0.7f + 0.3f * sinf(m_pulseTimer * 4.0f);
            DrawRectangleRounded(
                (Rectangle){(float)toolX, (float)bannerY, 620, 22}, 0.25f, 4,
                Fade((Color){255, 60, 30, 255}, pulse * 0.92f));
            const char* detourMsg = TextFormat(
                "[!] JALAN DIBLOKADE (Tenda Acara) | Detour Dinamis Aktif | Jalur dialihkan +%.2f km (+%.0f menit delay)",
                extraKm, extraMin);
            DrawTextCustom(detourMsg, toolX + 12, bannerY + 4, 10.5f, WHITE, FONT_BOLD);
        }
    }

    // =========================================================================
    // 3. MBG Routing Optimization Card (Left Side)
    // Displays the optimized sequence, Time Windows, and algorithm metrics
    // =========================================================================
    int cardX = 16;
    int cardY = navCardY + navCardH + 10;
    int cardW = 430;
    int cardH = 300;

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
    DrawTextCustom("Urutan Kunjungan Optimal:", cardX + 16, cardY + 56, 11.0f, LIGHTGRAY, FONT_SEMIBOLD);

    // SPPG Central Kitchen start badge
    DrawRectangleRounded((Rectangle){(float)cardX + 16, (float)cardY + 74, 48, 18}, 0.3f, 4, (Color){255, 180, 0, 255});
    DrawTextCustom("SPPG", cardX + 22, cardY + 76, 10.5f, BLACK, FONT_BOLD);
    DrawTextCustom("Dapur Pusat MBG (Titik Awal)", cardX + 70, cardY + 76, 11.0f, (Color){255, 220, 140, 255}, FONT_REGULAR);

    int rowY = cardY + 96;
    for (size_t i = 0; i < m_schools.size(); i++) {
        const auto& sch = m_schools[i];
        bool isCurrent = ((int)i == m_currentLegIndex && !m_allDelivered);
        bool isDone = sch.reached;

        // Colored badge
        DrawRectangleRounded((Rectangle){(float)cardX + 16, (float)rowY, 24, 18}, 0.3f, 4, sch.color);
        DrawTextCustom(TextFormat("%d", (int)i + 1), cardX + 24, rowY + 2, 11, WHITE, FONT_BOLD);

        // Status string
        const char* statusStr = isDone ? "Selesai" : (isCurrent ? (m_isUnloading ? "Bongkar" : "> Menuju") : "Menunggu");
        Color statusColor = isDone ? (Color){100, 240, 130, 255} : (isCurrent ? YELLOW : (Color){180, 190, 205, 255});

        float legMeters = (i < m_routeLegs.size()) ? (m_routeLegs[i].path.totalDistanceWorld * 25.0f) : 0.0f;
        DrawTextCustom(TextFormat("%s | [%d-%dm] | %.0fm | %s", sch.name.c_str(), sch.window.start, sch.window.end, legMeters, statusStr),
                       cardX + 46, rowY + 2, 11.0f, statusColor, FONT_REGULAR);

        rowY += 20;
    }

    // Final Step (Kembali ke SPPG)
    bool isReturnLegCurrent = isReturningToDepot && !m_allDelivered;
    const char* returnStatusStr = m_allDelivered ? "Tiba di SPPG" : (isReturnLegCurrent ? "> Sedang Kembali" : "Menunggu");
    Color returnStatusCol = m_allDelivered ? (Color){100, 240, 130, 255} : (isReturnLegCurrent ? (Color){255, 220, 120, 255} : (Color){180, 190, 205, 255});

    float returnMeters = (!m_routeLegs.empty() && m_routeLegs.back().isReturnToDepot) ?
                         (m_routeLegs.back().path.totalDistanceWorld * 25.0f) : 0.0f;

    DrawRectangleRounded((Rectangle){(float)cardX + 16, (float)rowY, 24, 18}, 0.3f, 4, (Color){255, 180, 0, 255});
    DrawTextCustom("0", cardX + 24, rowY + 2, 11, BLACK, FONT_BOLD);
    DrawTextCustom(TextFormat("Kembali ke SPPG (Titik Awal) | %.0fm | %s", returnMeters, returnStatusStr),
                   cardX + 46, rowY + 2, 11.0f, returnStatusCol, FONT_REGULAR);

    // Performance & Metrics summary
    DrawLine(cardX + 16, cardY + cardH - 56, cardX + cardW - 16, cardY + cardH - 56, Fade(WHITE, 0.15f));

    DrawTextCustom(TextFormat("Total Cost: %d menit | Jarak Total: %.2f km (%.0f m)",
                              m_totalCostMinutes, (m_totalRouteDistance * 25.0f) / 1000.0f, m_totalRouteDistance * 25.0f),
                   cardX + 16, cardY + cardH - 44, 11.0f, WHITE, FONT_SEMIBOLD);

    DrawTextCustom(TextFormat("States Explored: %d - Runtime: %.1f us - Detour: %s",
                              m_statesExplored, m_optimizationTimeUs, m_obstaclesEnabled ? "Aktif" : "Off"),
                   cardX + 16, cardY + cardH - 24, 10.5f, (Color){140, 230, 255, 255}, FONT_REGULAR);

    // =========================================================================
    // 4. Real-Time 2D Minimap & Fullscreen Map Overlay
    // =========================================================================
    if (m_showMinimap && !m_mapFullscreen) {
        DrawMinimap(screenW, screenH);
    }

    if (m_mapFullscreen) {
        DrawFullscreenMap(screenW, screenH);
    }

    if (m_showItinerarySheet) {
        DrawItinerarySheet(screenW, screenH);
    }

    if (m_showLocationModal) {
        DrawLocationModal(screenW, screenH);
    }

    // Top Right FPS & Engine info
    Fumbo::Instance().DrawFPS(screenW - 105, 16);
}

void PathfinderState::DrawMinimap(int screenW, int screenH) {
    Texture2D mapTex = m_world.GetMapTexture();
    bool useOSM = m_world.IsGeneratedFromOSM();
    if (mapTex.id == 0) return;
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
    DrawTextCustom(useOSM ? "Peta OpenStreetMap (Scroll: Zoom)" : "Peta MBG (Scroll: Zoom)", mmX + 6.0f, mmY - 22.0f, 11.5f, WHITE, FONT_SEMIBOLD);

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
    // First, draw original blocked path (Jalur Semestinya) in striped red if detour active
    if (m_obstaclesEnabled && m_showOriginalRoute) {
        for (const auto& leg : m_routeLegs) {
            if (!leg.hasDetour || leg.originalPath.gridPath.size() < 2) continue;
            for (size_t i = 1; i < leg.originalPath.gridPath.size(); i++) {
                float x1 = mmX + (leg.originalPath.gridPath[i-1].x / (float)m_world.GetGridWidth()) * mmW;
                float y1 = mmY + (leg.originalPath.gridPath[i-1].y / (float)m_world.GetGridHeight()) * mmH;
                float x2 = mmX + (leg.originalPath.gridPath[i].x / (float)m_world.GetGridWidth()) * mmW;
                float y2 = mmY + (leg.originalPath.gridPath[i].y / (float)m_world.GetGridHeight()) * mmH;
                // Alternating hazard stripe: red and yellow
                Color stripeCol = ((i / 2) % 2 == 0) ? (Color){230, 40, 40, 200} : (Color){255, 220, 50, 200};
                DrawLineEx({x1, y1}, {x2, y2}, 3.5f, stripeCol);
            }
        }
    }

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
        // Leg colored line (Congested segments drawn in RED!)
        for (size_t i = 1; i < leg.path.gridPath.size(); i++) {
            float x1 = mmX + (leg.path.gridPath[i - 1].x / (float)m_world.GetGridWidth()) * mmW;
            float y1 = mmY + (leg.path.gridPath[i - 1].y / (float)m_world.GetGridHeight()) * mmH;
            float x2 = mmX + (leg.path.gridPath[i].x / (float)m_world.GetGridWidth()) * mmW;
            float y2 = mmY + (leg.path.gridPath[i].y / (float)m_world.GetGridHeight()) * mmH;

            bool isTraffic = m_trafficEnabled && (i - 1 < leg.path.waypointInTraffic.size() && leg.path.waypointInTraffic[i - 1]);
            Color segCol = isTraffic ? (Color){235, 40, 40, 255} : leg.color;
            DrawLineEx((Vector2){x1, y1}, (Vector2){x2, y2}, isTraffic ? 3.4f : 2.6f, segCol);
        }

        // Draw detour branch point marker on minimap
        if (m_obstaclesEnabled && leg.hasDetour) {
            float bx = mmX + (leg.detourBranchGrid.x / (float)m_world.GetGridWidth()) * mmW;
            float by = mmY + (leg.detourBranchGrid.y / (float)m_world.GetGridHeight()) * mmH;
            DrawCircle((int)bx, (int)by, 5, (Color){40, 225, 90, 255});
            DrawCircle((int)bx, (int)by, 2, WHITE);
        }
    }

    // Draw Obstacles (Tenda / Perbaikan Jalan) on minimap
    if (m_obstaclesEnabled) {
        for (const auto& obs : m_obstacles) {
            float ox = mmX + (obs.gridPos.x / (float)m_world.GetGridWidth()) * mmW;
            float oy = mmY + (obs.gridPos.y / (float)m_world.GetGridHeight()) * mmH;
            // Pulsing red X marker
            float r = 7.0f + 2.0f * sinf(m_pulseTimer * 5.0f);
            DrawCircle((int)ox, (int)oy, (int)r, Fade((Color){220, 30, 30, 255}, 0.6f));
            DrawCircle((int)ox, (int)oy, 4, (Color){220, 30, 30, 255});
            DrawCircle((int)ox, (int)oy, 2, WHITE);
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
    Texture2D mapTex = m_world.GetMapTexture();
    bool useOSM = m_world.IsGeneratedFromOSM();
    if (mapTex.id == 0) return;

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

        const char* fsMainTitle = useOSM ? "PETA DIGITAL OPENSTREETMAP (PONDOK LABU)" : "PETA DISTRIBUSI RUTE MBG (RESOLUSI PENUH)";
    DrawTextCustom(fsMainTitle, modalX + 18, modalY + 11, 15, WHITE, FONT_BOLD);
    DrawTextCustom(TextFormat("Resolusi Asli: %dx%d | Zoom: %.1fx | Scroll: Zoom | Drag: Geser Peta",
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

    // Draw Traffic Zones (translucent red heat circles)
    if (m_trafficEnabled) {
        for (const auto& tz : m_trafficZones) {
            Vector2 sPos = g2s(tz.centerGrid);
            float screenR = (tz.radiusGrid / (float)m_world.GetGridWidth()) * curW;
            DrawCircle((int)sPos.x, (int)sPos.y, screenR, (Color){230, 40, 40, 40});
            DrawCircleLines((int)sPos.x, (int)sPos.y, screenR, (Color){230, 50, 50, 140});
        }
    }

    // Draw original blocked routes (Jalur Semestinya) with hazard stripe if detour is active
    if (m_obstaclesEnabled && m_showOriginalRoute) {
        for (const auto& leg : m_routeLegs) {
            if (!leg.hasDetour || leg.originalPath.gridPath.size() < 2) continue;
            for (size_t i = 1; i < leg.originalPath.gridPath.size(); i++) {
                Vector2 p1 = g2s(leg.originalPath.gridPath[i - 1]);
                Vector2 p2 = g2s(leg.originalPath.gridPath[i]);
                Color stripeCol = ((i / 2) % 2 == 0) ? (Color){230, 40, 40, 200} : (Color){255, 220, 50, 200};
                DrawLineEx(p1, p2, coreWidth * 0.85f, stripeCol);
            }
        }
    }

    for (const auto& leg : m_routeLegs) {
        if (!leg.path.found || leg.path.gridPath.size() < 2) continue;

        // Underlay border (White normally, dark red in traffic)
        for (size_t i = 1; i < leg.path.gridPath.size(); i++) {
            Vector2 p1 = g2s(leg.path.gridPath[i - 1]);
            Vector2 p2 = g2s(leg.path.gridPath[i]);
            bool inTraffic = (i < leg.path.waypointInTraffic.size()) ? leg.path.waypointInTraffic[i] : false;
            Color underCol = inTraffic ? (Color){160, 20, 20, 255} : WHITE;
            DrawLineEx(p1, p2, underWidth, underCol);
        }

        // Core colored leg ribbon (or bright Red if in traffic)
        for (size_t i = 1; i < leg.path.gridPath.size(); i++) {
            Vector2 p1 = g2s(leg.path.gridPath[i - 1]);
            Vector2 p2 = g2s(leg.path.gridPath[i]);
            bool inTraffic = (i < leg.path.waypointInTraffic.size()) ? leg.path.waypointInTraffic[i] : false;
            Color segColor = inTraffic ? (Color){235, 40, 40, 255} : leg.color;
            float segW = inTraffic ? coreWidth * 1.35f : coreWidth;
            DrawLineEx(p1, p2, segW, segColor);
        }

        // Draw detour branch marker on fullscreen map
        if (m_obstaclesEnabled && leg.hasDetour) {
            Vector2 bPt = g2s(leg.detourBranchGrid);
            DrawCircle((int)bPt.x, (int)bPt.y, 12 + 3 * sinf(m_pulseTimer * 5.0f), Fade((Color){40, 225, 90, 255}, 0.45f));
            DrawCircle((int)bPt.x, (int)bPt.y, 7, (Color){40, 225, 90, 255});
            DrawCircle((int)bPt.x, (int)bPt.y, 3, WHITE);
            const char* detourLabel = TextFormat("Detour +%.2fkm", leg.extraDetourKm);
            float dlW = MeasureTextCustom(detourLabel, 9.5f, FONT_SEMIBOLD) + 12;
            DrawRectangleRounded({bPt.x - dlW * 0.5f, bPt.y - 28, dlW, 18}, 0.3f, 4, Fade(BLACK, 0.88f));
            DrawRectangleRoundedLinesEx({bPt.x - dlW * 0.5f, bPt.y - 28, dlW, 18}, 0.3f, 4, 1.2f, (Color){40, 225, 90, 255});
            DrawTextCustom(detourLabel, bPt.x - dlW * 0.5f + 6, bPt.y - 25, 9.5f, (Color){180, 255, 200, 255}, FONT_SEMIBOLD);
        }
    }

    // Draw Obstacles (Tenda Acara Warga / Penutupan Jalan)
    if (m_obstaclesEnabled) {
        for (const auto& obs : m_obstacles) {
            Vector2 oPos = g2s(obs.gridPos);
            // Pulsing warning ring
            DrawCircle((int)oPos.x, (int)oPos.y, 14 + 4 * sinf(m_pulseTimer * 5.0f), Fade(RED, 0.35f));
            DrawCircle((int)oPos.x, (int)oPos.y, 10, (Color){220, 30, 30, 255});
            DrawCircle((int)oPos.x, (int)oPos.y, 6, WHITE);

            // Floating pill label
    const char* obsLabel = "[!] Tenda Acara (Tutup)";
            float obW = MeasureTextCustom(obsLabel, 10, FONT_BOLD) + 14;
            DrawRectangleRounded((Rectangle){oPos.x - obW * 0.5f, oPos.y - 30, obW, 19}, 0.3f, 4, Fade(BLACK, 0.88f));
            DrawRectangleRoundedLinesEx((Rectangle){oPos.x - obW * 0.5f, oPos.y - 30, obW, 19}, 0.3f, 4, 1.2f, (Color){255, 60, 60, 255});
            DrawTextCustom(obsLabel, oPos.x - obW * 0.5f + 7, oPos.y - 27, 10, (Color){255, 120, 120, 255}, FONT_BOLD);
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
        DrawTextCustom("->", legX, barY + 7, 12, WHITE, FONT_BOLD);
        legX += 16;

        const auto& sch = m_schools[i];
        DrawRectangleRounded((Rectangle){(float)legX, (float)barY + 6, 22, 18}, 0.3f, 4, sch.color);
        DrawTextCustom(TextFormat("%d", (int)i + 1), legX + 7, barY + 8, 10, WHITE, FONT_BOLD);
        legX += 28;

        DrawTextCustom(sch.name.c_str(), legX, barY + 8, 11, WHITE, FONT_REGULAR);
        legX += MeasureTextCustom(sch.name.c_str(), 11, FONT_REGULAR) + 12;
    }

        DrawTextCustom("->", legX, barY + 7, 12, WHITE, FONT_BOLD);
    legX += 16;
    DrawRectangleRounded((Rectangle){(float)legX, (float)barY + 6, 80, 18}, 0.3f, 4, (Color){255, 180, 0, 255});
    DrawTextCustom("Kembali SPPG", legX + 5, barY + 8, 9.5f, BLACK, FONT_BOLD);

    // Right summary metrics
    const char* sumStr = TextFormat("Jarak: %.2f km | ETA: %d mnt (Macet +%d mnt) | %s",
                                    m_totalRouteDistance * 0.045f,
                                    m_realisticEtaMinutes,
                                    m_trafficDelayMinutes,
                                    m_routeFeasible ? "Layak (Feasible)" : "Overdue");
    float sumW = MeasureTextCustom(sumStr, 11, FONT_SEMIBOLD);
    DrawTextCustom(sumStr, viewX + viewW - sumW - 16, barY + 8, 11, (Color){140, 230, 255, 255}, FONT_SEMIBOLD);
}

void PathfinderState::DrawItinerarySheet(int screenW, int screenH) {
    // Backdrop
    DrawRectangle(0, 0, screenW, screenH, Fade(BLACK, 0.65f));

    // Center dialog card
    int sheetW = 660;
    int sheetH = std::min(screenH - 80, 680);
    int sheetX = (screenW - sheetW) / 2;
    int sheetY = (screenH - sheetH) / 2;

    Vector2 mouse = GetMousePosition();

    // Modal background
    DrawRectangleRounded((Rectangle){(float)sheetX, (float)sheetY, (float)sheetW, (float)sheetH}, 0.03f, 6, (Color){22, 28, 42, 252});
    DrawRectangleRoundedLinesEx((Rectangle){(float)sheetX, (float)sheetY, (float)sheetW, (float)sheetH}, 0.03f, 6, 1.5f, Fade(WHITE, 0.35f));

    // Header
    DrawRectangleRounded((Rectangle){(float)sheetX, (float)sheetY, (float)sheetW, 58}, 0.03f, 6, (Color){15, 20, 32, 255});
    DrawTextCustom("Petunjuk Rute & Turn-by-Turn OpenStreetMap", sheetX + 20, sheetY + 12, 15.0f, WHITE, FONT_BOLD);

    const auto& loc = OSMService::Instance().GetCurrentLocation();
    std::string subHead = TextFormat("Wilayah: %s - %s", loc.name.c_str(), OSMService::Instance().GetStatusString().c_str());
    DrawTextCustom(subHead.c_str(), sheetX + 20, sheetY + 34, 11.0f, (Color){160, 210, 255, 255}, FONT_REGULAR);

    // Close button [X]
    Rectangle closeBtnRect = {(float)sheetX + sheetW - 90, (float)sheetY + 14, 76, 28};
    bool hoverClose = CheckCollisionPointRec(mouse, closeBtnRect);
    DrawRectangleRounded(closeBtnRect, 0.25f, 4, hoverClose ? (Color){220, 50, 50, 255} : (Color){70, 80, 100, 255});
    DrawTextCustom("Tutup [N]", closeBtnRect.x + 10, closeBtnRect.y + 7, 11.0f, WHITE, FONT_BOLD);
    if (hoverClose && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        m_showItinerarySheet = false;
        return;
    }

    // Scrollable content area
    int contentX = sheetX + 16;
    int contentY = sheetY + 68;
    int contentW = sheetW - 32;
    int contentH = sheetH - 120;

    // Handle mouse wheel scrolling
    static float s_scrollOffset = 0.0f;
    if (CheckCollisionPointRec(mouse, (Rectangle){(float)sheetX, (float)contentY, (float)sheetW, (float)contentH})) {
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) {
            s_scrollOffset -= wheel * 35.0f;
        }
    }

    BeginScissorMode(contentX, contentY, contentW, contentH);

    float curY = (float)contentY - s_scrollOffset;
    float totalContentHeight = 0.0f;

    if (m_routeLegs.empty()) {
        DrawTextCustom("Belum ada rute aktif. Tekan [Acak Rute] untuk menghasilkan rute baru.", contentX + 20, curY + 20, 12.0f, LIGHTGRAY, FONT_REGULAR);
        totalContentHeight += 60.0f;
    } else {
        for (size_t l = 0; l < m_routeLegs.size(); l++) {
            const auto& leg = m_routeLegs[l];

            // Leg header
            std::string legTitle = leg.isReturnToDepot ?
                TextFormat("Leg %d: %s -> %s (Kembali)", (int)l + 1, leg.osmNav.originName.c_str(), leg.osmNav.destinationName.c_str()) :
                TextFormat("Leg %d: %s -> %s", (int)l + 1, leg.osmNav.originName.c_str(), leg.osmNav.destinationName.c_str());

            Color legBg = Fade(leg.color, 0.22f);
            DrawRectangleRounded((Rectangle){(float)contentX, curY, (float)contentW, 30}, 0.2f, 4, legBg);
            DrawRectangleRoundedLinesEx((Rectangle){(float)contentX, curY, (float)contentW, 30}, 0.2f, 4, 1.0f, Fade(leg.color, 0.6f));

            // Small color strip
            DrawRectangleRounded((Rectangle){(float)contentX + 6, curY + 6, 10, 18}, 0.3f, 2, leg.color);
            DrawTextCustom(legTitle.c_str(), contentX + 24, curY + 7, 12.0f, WHITE, FONT_BOLD);

            std::string legMetrics = TextFormat("%.2f km - %.0f mnt", leg.osmNav.totalDistanceKm, leg.osmNav.estimatedMinutes);
            float metW = MeasureTextCustom(legMetrics.c_str(), 11.0f, FONT_SEMIBOLD);
            DrawTextCustom(legMetrics.c_str(), contentX + contentW - metW - 12, curY + 8, 11.0f, (Color){255, 240, 150, 255}, FONT_SEMIBOLD);

            curY += 36;
            totalContentHeight += 36;

            if (leg.osmNav.steps.empty()) {
                DrawTextCustom("  - Lurus mengikuti koridor jalan", contentX + 24, curY, 11.0f, GRAY, FONT_REGULAR);
                curY += 22;
                totalContentHeight += 22;
            } else {
                for (size_t s = 0; s < leg.osmNav.steps.size(); s++) {
                    const auto& step = leg.osmNav.steps[s];

                    // Step icon badge
                    DrawRectangleRounded((Rectangle){(float)contentX + 16, curY, 32, 20}, 0.25f, 2, (Color){40, 52, 75, 255});
                    DrawTextCustom(step.icon.c_str(), contentX + 19, curY + 3, 10.5f, (Color){100, 210, 255, 255}, FONT_BOLD);

                    // Step instruction
                    DrawTextCustom(step.instruction.c_str(), contentX + 54, curY + 3, 11.5f, WHITE, FONT_REGULAR);

                    // Step distance
                    std::string distStr = TextFormat("%.0f m", step.distanceMeters);
                    float dW = MeasureTextCustom(distStr.c_str(), 10.5f, FONT_REGULAR);
                    DrawTextCustom(distStr.c_str(), contentX + contentW - dW - 14, curY + 3, 10.5f, (Color){200, 215, 235, 255}, FONT_REGULAR);

                    curY += 24;
                    totalContentHeight += 24;
                }
            }

            curY += 10;
            totalContentHeight += 10;
        }
    }

    EndScissorMode();

    // Clamp scroll
    float maxScroll = std::max(0.0f, totalContentHeight - (float)contentH);
    s_scrollOffset = std::clamp(s_scrollOffset, 0.0f, maxScroll);

    // Scrollbar indicator
    if (maxScroll > 0.0f) {
        float barH = (float)contentH * ((float)contentH / totalContentHeight);
        float barY = (float)contentY + (s_scrollOffset / maxScroll) * ((float)contentH - barH);
        DrawRectangleRounded((Rectangle){(float)contentX + contentW - 4, barY, 4, barH}, 0.5f, 2, Fade(WHITE, 0.4f));
    }

    // Footer summary bar
    int footY = sheetY + sheetH - 46;
    DrawRectangleRounded((Rectangle){(float)sheetX, (float)footY, (float)sheetW, 46}, 0.03f, 6, (Color){15, 20, 32, 255});
    DrawLine(sheetX, footY, sheetX + sheetW, footY, Fade(WHITE, 0.15f));

    std::string footerText = TextFormat("Total: %d Leg Pengantaran - %.2f km - Ditenagai OSRM OpenStreetMap Engine",
                                        (int)m_routeLegs.size(), (m_totalRouteDistance * 25.0f) / 1000.0f);
    DrawTextCustom(footerText.c_str(), sheetX + 20, footY + 15, 11.0f, (Color){160, 220, 255, 255}, FONT_SEMIBOLD);

    DrawTextCustom("[ESC] Tutup", sheetX + sheetW - 90, footY + 15, 11.0f, LIGHTGRAY, FONT_REGULAR);
}

void PathfinderState::DrawLocationModal(int screenW, int screenH) {
    // Backdrop
    DrawRectangle(0, 0, screenW, screenH, Fade(BLACK, 0.65f));

    int modalW = 600;
    int modalH = 480;
    int modalX = (screenW - modalW) / 2;
    int modalY = (screenH - modalH) / 2;

    Vector2 mouse = GetMousePosition();

    // Dialog card
    DrawRectangleRounded((Rectangle){(float)modalX, (float)modalY, (float)modalW, (float)modalH}, 0.03f, 6, (Color){20, 26, 40, 252});
    DrawRectangleRoundedLinesEx((Rectangle){(float)modalX, (float)modalY, (float)modalW, (float)modalH}, 0.03f, 6, 1.5f, Fade(WHITE, 0.35f));

    // Header
    DrawRectangleRounded((Rectangle){(float)modalX, (float)modalY, (float)modalW, 56}, 0.03f, 6, (Color){14, 18, 30, 255});
    DrawTextCustom("Pilih Preset Wilayah OpenStreetMap", modalX + 20, modalY + 12, 15.0f, WHITE, FONT_BOLD);
    DrawTextCustom("Menyesuaikan geolokasi GPS, sekolah autentik, dan data jalan OSRM",
                   modalX + 20, modalY + 33, 11.0f, (Color){160, 210, 255, 255}, FONT_REGULAR);

    // Close button [X]
    Rectangle closeBtnRect = {(float)modalX + modalW - 90, (float)modalY + 14, 76, 28};
    bool hoverClose = CheckCollisionPointRec(mouse, closeBtnRect);
    DrawRectangleRounded(closeBtnRect, 0.25f, 4, hoverClose ? (Color){220, 50, 50, 255} : (Color){70, 80, 100, 255});
    DrawTextCustom("Tutup [P]", closeBtnRect.x + 10, closeBtnRect.y + 7, 11.0f, WHITE, FONT_BOLD);
    if (hoverClose && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        m_showLocationModal = false;
        return;
    }

    int cardY = modalY + 68;
    int presetCount = OSMService::Instance().GetPresetCount();
    const auto& currentLoc = OSMService::Instance().GetCurrentLocation();

    for (int i = 0; i < presetCount; i++) {
        const auto& preset = OSMService::Instance().GetPreset(i);
        bool isActive = (currentLoc.id == preset.id);

        Rectangle itemRect = {(float)modalX + 16, (float)cardY, (float)modalW - 32, 74};
        bool hoverItem = CheckCollisionPointRec(mouse, itemRect);

        Color itemBg = isActive ? (Color){30, 50, 80, 255} : (hoverItem ? (Color){28, 36, 56, 255} : (Color){22, 28, 44, 255});
        DrawRectangleRounded(itemRect, 0.12f, 4, itemBg);
        DrawRectangleRoundedLinesEx(itemRect, 0.12f, 4, 1.0f, isActive ? (Color){60, 160, 255, 255} : Fade(WHITE, 0.2f));

        // Location title
        DrawTextCustom(preset.name.c_str(), itemRect.x + 14, itemRect.y + 10, 13.0f, WHITE, FONT_BOLD);
        DrawTextCustom(TextFormat("Wilayah: %s - Dapur: %s", preset.district.c_str(), preset.depotName.c_str()),
                       itemRect.x + 14, itemRect.y + 29, 10.5f, (Color){200, 215, 235, 255}, FONT_REGULAR);

        // Coordinates & Schools count
        std::string infoStr = TextFormat("GPS: %.4f, %.4f | %d Sekolah Terdaftar",
                                         preset.centerLat, preset.centerLon, (int)preset.schoolNames.size());
        DrawTextCustom(infoStr.c_str(), itemRect.x + 14, itemRect.y + 48, 10.0f, (Color){140, 180, 220, 255}, FONT_REGULAR);

        // Action button
        Rectangle actionBtn = {itemRect.x + itemRect.width - 105, itemRect.y + 22, 92, 30};
        if (isActive) {
            DrawRectangleRounded(actionBtn, 0.25f, 3, (Color){34, 160, 75, 255});
            DrawTextCustom("[AKTIF]", actionBtn.x + 22, actionBtn.y + 8, 11.0f, WHITE, FONT_BOLD);
        } else {
            bool hoverBtn = CheckCollisionPointRec(mouse, actionBtn);
            DrawRectangleRounded(actionBtn, 0.25f, 3, hoverBtn ? (Color){50, 130, 230, 255} : (Color){35, 95, 175, 255});
            DrawTextCustom("Pilih", actionBtn.x + 30, actionBtn.y + 8, 11.0f, WHITE, FONT_SEMIBOLD);

            if ((hoverItem || hoverBtn) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                OSMService::Instance().SetLocationPreset(i);
                m_depotName = OSMService::Instance().GetDepotName();
                RandomizePositions();
                m_showLocationModal = false;
                return;
            }
        }

        cardY += 84;
    }

    // Footer info
    int footY = modalY + modalH - 46;
    DrawRectangleRounded((Rectangle){(float)modalX, (float)footY, (float)modalW, 46}, 0.03f, 6, (Color){14, 18, 30, 255});
    DrawLine(modalX, footY, modalX + modalW, footY, Fade(WHITE, 0.15f));

    DrawTextCustom("Peta raster 3D menggunakan citra Pondok Labu (assets/map.jpg)",
                   modalX + 20, footY + 16, 10.5f, (Color){255, 230, 150, 255}, FONT_REGULAR);
    DrawTextCustom("[ESC] Tutup", modalX + modalW - 90, footY + 16, 11.0f, LIGHTGRAY, FONT_REGULAR);
}
