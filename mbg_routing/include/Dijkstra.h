#pragma once
#include "Graph.h"
#include <optional>

struct DijkstraResult {
    std::map<int, int> dist;
    std::map<int, int> prev;
    bool success;
};

DijkstraResult dijkstra(const Graph& g, int source);
std::vector<int> reconstructPath(const std::map<int, int>& prev, int source, int target);
