#pragma once
#include <vector>
#include <map>
#include <set>
#include <queue>
#include <limits>
#include <string>

constexpr int INF = std::numeric_limits<int>::max();
constexpr int MAX_SCHOOLS = 5;
constexpr int MAX_TIME = 240;

struct TimeWindow {
    int start;
    int end;
};

struct School {
    int id;
    std::string name;
    TimeWindow window;
    int unload_time;
};

struct State {
    int pos;
    int mask;
    int time;
    int cost;

    bool operator<(const State& other) const {
        return cost > other.cost;
    }
};
