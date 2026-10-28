#include "Dijkstra.h"
#include <queue>
#include <algorithm>

DijkstraResult dijkstra(const Graph& g, int source) {
    DijkstraResult result;
    result.success = false;

    for (const auto& [node, neighbors] : g.adj) {
        result.dist[node] = INF;
        result.prev[node] = -1;
    }

    result.dist[source] = 0;

    std::priority_queue<std::pair<int, int>,
                        std::vector<std::pair<int, int>>,
                        std::greater<>> pq;
    pq.push({0, source});

    while (!pq.empty()) {
        auto [d, u] = pq.top();
        pq.pop();

        if (result.dist[u] < d) continue;

        for (const auto& [v, w] : g.adj.at(u)) {
            if (g.isBlocked(u, v)) continue;

            int new_dist = result.dist[u] + w;
            if (new_dist < result.dist[v]) {
                result.dist[v] = new_dist;
                result.prev[v] = u;
                pq.push({new_dist, v});
            }
        }
    }

    result.success = true;
    return result;
}

std::vector<int> reconstructPath(const std::map<int, int>& prev, int source, int target) {
    std::vector<int> path;
    int current = target;

    while (current != -1) {
        path.push_back(current);
        if (current == source) break;

        auto it = prev.find(current);
        if (it == prev.end() || it->second == -1) {
            return {};
        }
        current = it->second;
    }

    std::reverse(path.begin(), path.end());
    return path;
}
