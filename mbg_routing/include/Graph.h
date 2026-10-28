#pragma once
#include "Common.h"
#include <unordered_map>
#include <unordered_set>

class Graph {
public:
    std::unordered_map<int, std::vector<std::pair<int, int>>> adj;
    std::unordered_set<int> blocked_edges;

    void addEdge(int u, int v, int weight, bool directed = false);
    void blockEdge(int u, int v);
    void unblockEdge(int u, int v);
    bool isBlocked(int u, int v) const;
    std::vector<int> getNodes() const;
};
