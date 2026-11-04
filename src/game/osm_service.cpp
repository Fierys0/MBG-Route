#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <windows.h>
#include <wininet.h>
#else
#ifdef HAVE_CURL
#include <curl/curl.h>
#endif
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <fcntl.h>
#endif

#include "osm_service.hpp"
#include "fumbo.hpp"
#include <chrono>
#include <cmath>
#include <sstream>
#include <algorithm>


#if !defined(_WIN32) && defined(HAVE_CURL)
static size_t CurlWriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total = size * nmemb;
    std::string* s = static_cast<std::string*>(userp);
    s->append(static_cast<char*>(contents), total);
    return total;
}
#endif

OSMService& OSMService::Instance() {
    static OSMService s_instance;
    return s_instance;
}

OSMService::OSMService() {
    InitPresets();
    InitPondokLabuStreets();
}

OSMService::~OSMService() {
    Cleanup();
}

void OSMService::Init() {
    // Default to Pondok Labu, Jakarta Selatan (matches assets/map.jpg)
    SetLocationPreset(0);
    TraceLog(LOG_INFO, "[OSMService] Initialized for %s (%s)",
             m_currentLocation.name.c_str(), m_currentLocation.district.c_str());
}

void OSMService::Cleanup() {
    m_routeCache.clear();
}

void OSMService::InitPresets() {
    m_presets.clear();

    // Preset 0: Pondok Labu, Jakarta Selatan (User's authentic map image!)
    {
        OSMLocationPreset p;
        p.id = "pondok_labu";
        p.name = "Pondok Labu, Jakarta Selatan";
        p.district = "Kec. Cilandak, Jaksel";
        p.centerLat = -6.30877;
        p.centerLon = 106.79785;
        // Bounding box for ~2.0 km x 1.4 km map
        p.minLat = -6.31680;
        p.maxLat = -6.30120;
        p.minLon = 106.78650;
        p.maxLon = 106.80920;
        p.depotName = "SPPG Dapur Pusat MBG (Kampus UPNVJ)";
        p.schoolNames = {
            "SDN Pondok Labu 01 Pagi",
            "SMPN 85 Jakarta (Pondok Labu)",
            "SMAN 34 Jakarta",
            "SMA Al-Izhar Pondok Labu",
            "Sekolah Charitas Pondok Labu",
            "SDN Pondok Labu 11 Pagi",
            "SMAN 66 Jakarta (Pondok Labu)",
            "SDN Pondok Labu 02 Pagi",
            "Komplek SDN Pondok Labu 04/09/10",
            "SMP Negeri 96 Jakarta",
            "SMK Negeri 41 Jakarta",
            "SMP Darussalam H. Ipin",
            "Al Jabr Islamic School",
            "SDN Pondok Labu 12 Pagi"
        };
        m_presets.push_back(p);
    }

    // Preset 1: Monas / Gambir, Jakarta Pusat
    {
        OSMLocationPreset p;
        p.id = "monas_jakpus";
        p.name = "Monas & Gambir, Jakarta Pusat";
        p.district = "Kec. Gambir, Jakpus";
        p.centerLat = -6.17540;
        p.centerLon = 106.82717;
        p.minLat = -6.18300;
        p.maxLat = -6.16700;
        p.minLon = 106.81700;
        p.maxLon = 106.83700;
        p.depotName = "SPPG Dapur Pusat Gambir (Monas)";
        p.schoolNames = {
            "SDN Gambir 01 Pagi",
            "SMPN 1 Jakarta",
            "SMA Santa Ursula",
            "SDN Kenari 01 Pagi",
            "SDN Cideng 02 Pagi"
        };
        m_presets.push_back(p);
    }

    // Preset 2: ITB / Dago, Bandung
    {
        OSMLocationPreset p;
        p.id = "dago_bandung";
        p.name = "Dago & ITB, Kota Bandung";
        p.district = "Coblong, Bandung";
        p.centerLat = -6.89150;
        p.centerLon = 107.61070;
        p.minLat = -6.89900;
        p.maxLat = -6.88400;
        p.minLon = 107.60100;
        p.maxLon = 107.62000;
        p.depotName = "SPPG Dapur Pusat Coblong Dago";
        p.schoolNames = {
            "SDN Dago 01 Bandung",
            "SMPN 1 Bandung",
            "SMA Negeri 3 Bandung",
            "SD Santo Yusup Bandung",
            "SD Salman Al-Farisi"
        };
        m_presets.push_back(p);
    }

    // Preset 3: Blok M / Kebayoran Baru, Jakarta Selatan
    {
        OSMLocationPreset p;
        p.id = "kebayoran_jaksel";
        p.name = "Kebayoran Baru & Blok M, Jakarta Selatan";
        p.district = "Kebayoran Baru, Jaksel";
        p.centerLat = -6.24400;
        p.centerLon = 106.80000;
        p.minLat = -6.25200;
        p.maxLat = -6.23600;
        p.minLon = 106.79000;
        p.maxLon = 106.81000;
        p.depotName = "SPPG Dapur Pusat Kebayoran";
        p.schoolNames = {
            "SDN Gunung 01 Pagi",
            "SMPN 11 Jakarta",
            "SMA Negeri 6 Jakarta",
            "SMA Negeri 70 Jakarta",
            "SD Tarakanita 1"
        };
        m_presets.push_back(p);
    }
}

void OSMService::InitPondokLabuStreets() {
    m_streetDatabase.clear();

    // Real OpenStreetMap road spatial bounds in Pondok Labu, Jakarta Selatan
    // Lat range: -6.3168 to -6.3012 (South to North)
    // Lon range: 106.7865 to 106.8092 (West to East)

    auto addStreet = [this](const std::string& name, double minLat, double maxLat, double minLon, double maxLon, int speed) {
        StreetSegment seg;
        seg.name = name;
        seg.minLat = minLat;
        seg.maxLat = maxLat;
        seg.minLon = minLon;
        seg.maxLon = maxLon;
        seg.speedLimitKmh = speed;
        m_streetDatabase.push_back(seg);
    };

    // Major Arterials & Collectors in Pondok Labu
    addStreet("Jl. Pondok Labu Raya",    -6.3160, -6.3020, 106.7950, 106.8010, 45);
    addStreet("Jl. Pinang Raya",         -6.3100, -6.3060, 106.7930, 106.7995, 35);
    addStreet("Jl. Pinang II",           -6.3095, -6.3075, 106.7960, 106.7985, 30);
    addStreet("Jl. Swakarya Bawah",      -6.3065, -6.3040, 106.7970, 106.8005, 30);
    addStreet("Jl. Swakarya",            -6.3060, -6.3035, 106.7960, 106.7990, 30);
    addStreet("Jl. Haji Ipin",           -6.3135, -6.3090, 106.7870, 106.7950, 35);
    addStreet("Jl. Margasatwa Barat",    -6.3168, -6.3080, 106.8020, 106.8092, 45);
    addStreet("Jl. Andara Raya",         -6.3160, -6.3090, 106.7990, 106.8060, 40);
    addStreet("Jl. Fatmawati Raya",      -6.3050, -6.3012, 106.7865, 106.7960, 50);
    addStreet("Jl. Sekolah",             -6.3085, -6.3065, 106.7915, 106.7940, 25);
    addStreet("Gang Sekolah",            -6.3080, -6.3070, 106.7915, 106.7930, 20);
    addStreet("Jl. Haji Kamang",         -6.3120, -6.3080, 106.7910, 106.7960, 30);
    addStreet("Jl. Pangkalan Jati Raya", -6.3165, -6.3110, 106.7865, 106.7920, 40);
    addStreet("Jl. Cinere Raya",         -6.3168, -6.3120, 106.7870, 106.7930, 45);
    addStreet("Jl. RS Fatmawati",        -6.3040, -6.3012, 106.7900, 106.7980, 45);
}

void OSMService::SetLocationPreset(int presetIndex) {
    if (presetIndex < 0 || presetIndex >= (int)m_presets.size()) return;
    m_currentLocation = m_presets[presetIndex];
    m_routeCache.clear();
    TraceLog(LOG_INFO, "[OSMService] Switched location to %s", m_currentLocation.name.c_str());
}

Vector2 OSMService::GridToGeo(Vector2 gridPos, int gridW, int gridH) const {
    if (gridW <= 0) gridW = 1;
    if (gridH <= 0) gridH = 1;

    float normX = std::clamp(gridPos.x / (float)gridW, 0.0f, 1.0f);
    float normY = std::clamp(gridPos.y / (float)gridH, 0.0f, 1.0f);

    // X: West to East (Longitude)
    double lon = m_currentLocation.minLon + normX * (m_currentLocation.maxLon - m_currentLocation.minLon);
    // Y: North to South (Latitude: North is maxLat, South is minLat)
    double lat = m_currentLocation.maxLat - normY * (m_currentLocation.maxLat - m_currentLocation.minLat);

    return {(float)lon, (float)lat};
}

Vector2 OSMService::GeoToGrid(Vector2 geoPos, int gridW, int gridH) const {
    double lon = geoPos.x;
    double lat = geoPos.y;

    double dLon = m_currentLocation.maxLon - m_currentLocation.minLon;
    double dLat = m_currentLocation.maxLat - m_currentLocation.minLat;

    float normX = (dLon > 1e-6) ? (float)((lon - m_currentLocation.minLon) / dLon) : 0.5f;
    float normY = (dLat > 1e-6) ? (float)((m_currentLocation.maxLat - lat) / dLat) : 0.5f;

    return {normX * gridW, normY * gridH};
}

std::string OSMService::GetDepotName() const {
    return m_currentLocation.depotName;
}

std::string OSMService::GetSchoolName(int schoolIndex) const {
    if (m_currentLocation.schoolNames.empty()) {
        return TextFormat("Sekolah %d", schoolIndex + 1);
    }
    int idx = schoolIndex % (int)m_currentLocation.schoolNames.size();
    return m_currentLocation.schoolNames[idx];
}

std::string OSMService::ResolveStreetName(Vector2 gridPos, int gridW, int gridH) const {
    Vector2 geo = GridToGeo(gridPos, gridW, gridH);
    double lon = geo.x;
    double lat = geo.y;

    // 1. Search in defined street segments
    for (const auto& seg : m_streetDatabase) {
        if (lat >= seg.minLat && lat <= seg.maxLat &&
            lon >= seg.minLon && lon <= seg.maxLon) {
            return seg.name;
        }
    }

    // 2. Spatial quadrant heuristic based on Pondok Labu landmarks
    if (lat > -6.3050) return "Jl. Fatmawati Raya";
    if (lon < 106.7920) return "Jl. Haji Ipin";
    if (lon > 106.8030) return "Jl. Margasatwa Barat";
    if (lat < -6.3120) return "Jl. Andara Raya";
    if (lat < -6.3080) return "Jl. Pinang Raya";

    return "Jl. Pondok Labu Raya";
}

std::string OSMService::ExtractJsonString(const std::string& json, const std::string& key, size_t startPos) const {
    std::string needle = "\"" + key + "\":\"";
    size_t pos = json.find(needle, startPos);
    if (pos == std::string::npos) return "";
    pos += needle.size();
    size_t end = json.find("\"", pos);
    if (end == std::string::npos) return "";
    return json.substr(pos, end - pos);
}

double OSMService::ExtractJsonDouble(const std::string& json, const std::string& key, size_t startPos) const {
    std::string needle = "\"" + key + "\":";
    size_t pos = json.find(needle, startPos);
    if (pos == std::string::npos) return 0.0;
    pos += needle.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    size_t end = pos;
    while (end < json.size() && (isdigit(json[end]) || json[end] == '.' || json[end] == '-')) end++;
    try {
        return std::stod(json.substr(pos, end - pos));
    } catch (...) {
        return 0.0;
    }
}

std::string OSMService::HttpGet(const std::string& url, int timeoutMs) {
#ifdef _WIN32
    HINTERNET hInternet = InternetOpenA("MBGRouteApp/1.0 (contact@mbgroute.org)", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInternet) {
        m_isOnline = false;
        return "";
    }

    // Fast timeouts so background or synchronous queries never stall UI
    DWORD timeout = (DWORD)timeoutMs;
    InternetSetOptionA(hInternet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionA(hInternet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

    const char* headers = "User-Agent: MBGRouteApp/1.0 (contact@mbgroute.org)\r\nReferer: https://www.openstreetmap.org\r\n";
    HINTERNET hUrl = InternetOpenUrlA(hInternet, url.c_str(), headers, -1, INTERNET_FLAG_RELOAD | INTERNET_FLAG_DONT_CACHE, 0);
    if (!hUrl) {
        InternetCloseHandle(hInternet);
        m_isOnline = false;
        return "";
    }

    std::string result;
    char buffer[4096];
    DWORD bytesRead = 0;
    while (InternetReadFile(hUrl, buffer, sizeof(buffer) - 1, &bytesRead) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        result.append(buffer, bytesRead);
    }

    InternetCloseHandle(hUrl);
    InternetCloseHandle(hInternet);
    m_isOnline = true;
    return result;
#elif defined(HAVE_CURL)
    CURL* curl = curl_easy_init();
    if (!curl) {
        m_isOnline = false;
        return "";
    }
    std::string response;
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "User-Agent: MBGRouteApp/1.0 (contact@mbgroute.org)");
    headers = curl_slist_append(headers, "Referer: https://www.openstreetmap.org");
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, (long)timeoutMs);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, (long)timeoutMs);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (res == CURLE_OK) {
        m_isOnline = true;
        return response;
    } else {
        m_isOnline = false;
        return "";
    }
#elif defined(__linux__) || defined(__unix__) || defined(__APPLE__)
    if (url.rfind("http://", 0) != 0) {
        m_isOnline = false;
        return "";
    }
    std::string withoutProto = url.substr(7);
    size_t slashPos = withoutProto.find('/');
    std::string host = (slashPos != std::string::npos) ? withoutProto.substr(0, slashPos) : withoutProto;
    std::string path = (slashPos != std::string::npos) ? withoutProto.substr(slashPos) : "/";
    std::string port = "80";
    size_t colonPos = host.find(':');
    if (colonPos != std::string::npos) {
        port = host.substr(colonPos + 1);
        host = host.substr(0, colonPos);
    }

    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0 || !res) {
        m_isOnline = false;
        return "";
    }

    int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) {
        freeaddrinfo(res);
        m_isOnline = false;
        return "";
    }

    struct timeval tv{};
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));

    if (connect(sock, res->ai_addr, res->ai_addrlen) < 0) {
        close(sock);
        freeaddrinfo(res);
        m_isOnline = false;
        return "";
    }
    freeaddrinfo(res);

    std::ostringstream req;
    req << "GET " << path << " HTTP/1.1\r\n"
        << "Host: " << host << "\r\n"
        << "User-Agent: MBGRouteApp/1.0 (contact@mbgroute.org)\r\n"
        << "Referer: https://www.openstreetmap.org\r\n"
        << "Connection: close\r\n"
        << "Accept: */*\r\n\r\n";
    std::string reqStr = req.str();
    if (send(sock, reqStr.c_str(), reqStr.size(), 0) <= 0) {
        close(sock);
        m_isOnline = false;
        return "";
    }

    std::string response;
    char buffer[4096];
    ssize_t bytes = 0;
    while ((bytes = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytes] = '\0';
        response.append(buffer, bytes);
    }
    close(sock);

    size_t headerEnd = response.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        m_isOnline = false;
        return "";
    }
    m_isOnline = true;
    return response.substr(headerEnd + 4);
#else
    m_isOnline = false;
    return "";
#endif
}

OSMLegNavigation OSMService::QueryRoute(Vector2 startGrid, Vector2 endGrid, int gridW, int gridH,
                                        const std::string& fromName, const std::string& toName) {
    std::string cacheKey = TextFormat("%d,%d->%d,%d", (int)startGrid.x, (int)startGrid.y, (int)endGrid.x, (int)endGrid.y);
    if (m_routeCache.count(cacheKey)) {
        return m_routeCache[cacheKey];
    }

    OSMLegNavigation legNav;
    legNav.originName = fromName;
    legNav.destinationName = toName;

    Vector2 startGeo = GridToGeo(startGrid, gridW, gridH);
    Vector2 endGeo = GridToGeo(endGrid, gridW, gridH);

    // OpenStreetMap official OSRM Routing Machine endpoint
    std::string url = TextFormat("http://router.project-osrm.org/route/v1/driving/%.6f,%.6f;%%20%.6f,%.6f?overview=false&steps=true",
                                 startGeo.x, startGeo.y, endGeo.x, endGeo.y);

    auto t0 = std::chrono::high_resolution_clock::now();
    std::string json = HttpGet(url, 2000);
    auto t1 = std::chrono::high_resolution_clock::now();
    m_lastLatencyMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    m_queryCount++;

    if (!json.empty() && json.find("\"code\":\"Ok\"") != std::string::npos) {
        legNav.apiSuccess = true;
        legNav.totalDistanceKm = (float)(ExtractJsonDouble(json, "distance") / 1000.0);
        legNav.estimatedMinutes = (float)(ExtractJsonDouble(json, "duration") / 60.0);

        // Parse maneuvers and steps
        size_t pos = 0;
        while ((pos = json.find("\"maneuver\":", pos)) != std::string::npos) {
            std::string name = ExtractJsonString(json, "name", pos);
            std::string mtype = ExtractJsonString(json, "type", pos);
            std::string mod = ExtractJsonString(json, "modifier", pos);
            double dist = ExtractJsonDouble(json, "distance", pos);
            double dur = ExtractJsonDouble(json, "duration", pos);

            if (name.empty()) {
                name = ResolveStreetName(startGrid, gridW, gridH);
            }

            OSMStep step;
            step.streetName = name;
            step.maneuverType = mtype;
            step.modifier = mod;
            step.distanceMeters = (float)dist;
            step.durationSeconds = (float)dur;

            // Formulate natural Indonesian turn instructions
            if (mtype == "depart") {
                step.instruction = "Berangkat dari " + fromName;
                step.icon = "^";
            } else if (mtype == "arrive") {
                step.instruction = "Tiba di " + toName;
                step.icon = "[TIBA]";
            } else if (mod == "left" || mod == "sharp left") {
                step.instruction = "Belok Kiri ke " + name;
                step.icon = "<-";
            } else if (mod == "right" || mod == "sharp right") {
                step.instruction = "Belok Kanan ke " + name;
                step.icon = "->";
            } else if (mod == "slight left") {
                step.instruction = "Serong Kiri ke " + name;
                step.icon = "<-";
            } else if (mod == "slight right") {
                step.instruction = "Serong Kanan ke " + name;
                step.icon = "->";
            } else if (mod == "uturn") {
                step.instruction = "Putar Balik (U-Turn) di " + name;
                step.icon = "U";
            } else {
                step.instruction = "Lurus Terus di " + name;
                step.icon = "^";
            }

            legNav.steps.push_back(step);
            pos += 12;
        }
    } else {
        // Fallback: Synthesize realistic steps from local Pondok Labu OSM database
        legNav.apiSuccess = false;
        std::string startStreet = ResolveStreetName(startGrid, gridW, gridH);
        std::string endStreet = ResolveStreetName(endGrid, gridW, gridH);

        float estDistKm = Vector2Distance(startGrid, endGrid) * 0.035f;
        legNav.totalDistanceKm = estDistKm;
        legNav.estimatedMinutes = estDistKm * 2.2f;

        OSMStep s1;
        s1.streetName = startStreet;
        s1.instruction = "Berangkat dari " + fromName + " ke " + startStreet;
        s1.icon = "^";
        s1.distanceMeters = 80.0f;
        legNav.steps.push_back(s1);

        if (startStreet != endStreet) {
            OSMStep s2;
            s2.streetName = endStreet;
            s2.instruction = "Belok Kanan ke " + endStreet;
            s2.icon = "->";
            s2.distanceMeters = estDistKm * 600.0f;
            legNav.steps.push_back(s2);
        }

        OSMStep s3;
        s3.streetName = endStreet;
        s3.instruction = "Tiba di " + toName;
        s3.icon = "[TIBA]";
        s3.distanceMeters = 30.0f;
        legNav.steps.push_back(s3);
    }

    m_routeCache[cacheKey] = legNav;
    return legNav;
}

void OSMService::UpdateNavigationGuidance(Vector2 carGrid, int currentLegIdx, float legProgress,
                                          const std::vector<Vector2>& gridPath,
                                          const std::vector<Vector3>& worldWaypoints,
                                          bool isReturnToDepot, const std::string& destName,
                                          std::string& outInstruction, std::string& outCurrentStreet,
                                          std::string& outIcon, float& outDistToNextManeuver) {
    if (gridPath.empty() || worldWaypoints.empty()) {
        outInstruction = "Mencari Rute...";
        outCurrentStreet = "Pondok Labu";
        outIcon = "^";
        outDistToNextManeuver = 0.0f;
        return;
    }

    int curIdx = std::clamp((int)legProgress, 0, (int)gridPath.size() - 1);
    Vector2 curCell = gridPath[curIdx];

    // 1. Current street vehicle is driving on
    outCurrentStreet = ResolveStreetName(curCell, 410, 282);

    // 2. Check if near destination
    int remainingWaypoints = (int)gridPath.size() - 1 - curIdx;
    if (remainingWaypoints <= 3) {
        if (isReturnToDepot) {
            outInstruction = "Mendekati SPPG Dapur Pusat";
            outIcon = "[TIBA]";
        } else {
            outInstruction = "Mendekati " + destName;
            outIcon = "[TIBA]";
        }
        outDistToNextManeuver = remainingWaypoints * 12.0f;
        return;
    }

    // 3. Scan forward 4-10 waypoints to detect upcoming turn or road change
    int turnIdx = -1;
    float crossProduct = 0.0f;
    float distToTurnWorld = 0.0f;

    for (int i = curIdx + 1; i + 1 < (int)worldWaypoints.size() && (i - curIdx) <= 12; i++) {
        Vector3 v1 = Vector3Subtract(worldWaypoints[i], worldWaypoints[i - 1]);
        Vector3 v2 = Vector3Subtract(worldWaypoints[i + 1], worldWaypoints[i]);
        float cross = v1.x * v2.z - v1.z * v2.x;

        distToTurnWorld += Vector3Distance(worldWaypoints[i - 1], worldWaypoints[i]);

        if (fabsf(cross) > 0.16f) {
            turnIdx = i;
            crossProduct = cross;
            break;
        }
    }

    outDistToNextManeuver = distToTurnWorld * 15.0f; // in meters

    if (turnIdx != -1) {
        int safeGridIdx = std::clamp(turnIdx, 0, (int)gridPath.size() - 1);
        Vector2 targetCell = gridPath[safeGridIdx];
        std::string nextStreet = ResolveStreetName(targetCell, 410, 282);

        // Turn instruction
        if (crossProduct > 0.16f) {
            outIcon = "->";
            outInstruction = TextFormat("Belok Kanan ke %s", nextStreet.c_str());
        } else {
            outIcon = "<-";
            outInstruction = TextFormat("Belok Kiri ke %s", nextStreet.c_str());
        }
    } else {
        outIcon = "^";
        outInstruction = TextFormat("Lurus Terus di %s", outCurrentStreet.c_str());
        outDistToNextManeuver = remainingWaypoints * 14.0f;
    }
}

std::string OSMService::GetStatusString() const {
    if (m_isOnline) {
        return TextFormat("[OSM] OpenStreetMap: Online (%.0f ms)", m_lastLatencyMs);
    }
    return "[OSM] OpenStreetMap: Offline (Database Lokal)";
}
