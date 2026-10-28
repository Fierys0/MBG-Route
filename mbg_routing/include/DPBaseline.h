#pragma once
#include "Common.h"
#include "Graph.h"
#include "Dijkstra.h"

struct DPResult {
    int total_cost;
    std::vector<int> route;
    bool feasible;
};

DPResult dpBaseline(const Graph& g, int sppg, const std::vector<School>& schools, int cook_time);
