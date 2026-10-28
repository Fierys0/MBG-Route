#include "DPModified.h"
#include <unordered_map>
#include <map>

using DominanceMap = std::unordered_map<int, std::map<int, int>>;

bool isDominated(int pos, int mask, int time, int cost, const DominanceMap& dom_map) {
    int key = pos * 1000 + mask;
    auto it = dom_map.find(key);
    if (it == dom_map.end()) return false;

    auto& sorted_states = it->second;
    auto lower = sorted_states.upper_bound(time);
    if (lower == sorted_states.begin()) return false;

    --lower;
    if (lower->second <= cost) return true;

    return false;
}

void updateDominanceMap(int pos, int mask, int time, int cost, DominanceMap& dom_map) {
    int key = pos * 1000 + mask;
    auto& sorted_states = dom_map[key];

    auto it = sorted_states.lower_bound(time);
    while (it != sorted_states.end()) {
        if (it->second >= cost) {
            it = sorted_states.erase(it);
        } else {
            ++it;
        }
    }

    sorted_states[time] = cost;
}

int greedyUpperBound(const Graph& g, int sppg, const std::vector<School>& schools, int cook_time) {
    int current = sppg;
    int total = 0;
    int time = cook_time;
    std::set<int> unvisited;
    for (int i = 0; i < schools.size(); ++i) {
        unvisited.insert(i);
    }

    while (!unvisited.empty()) {
        int best_next = -1;
        int best_dist = INF;

        for (int next : unvisited) {
            auto result = dijkstra(g, current);
            int d = result.dist[schools[next].id];
            if (d < best_dist) {
                best_dist = d;
                best_next = next;
            }
        }

        total += best_dist;
        time += best_dist + schools[best_next].unload_time;
        current = schools[best_next].id;
        unvisited.erase(best_next);
    }

    return total;
}

DPModifiedResult dpModified(const Graph& g, int sppg, const std::vector<School>& schools, int cook_time) {
    int n = schools.size();
    int full_mask = (1 << n) - 1;

    DominanceMap dominance_map;
    std::unordered_map<int, int> dp;
    std::unordered_map<int, std::pair<int, int>> parent;

    int best_cost = greedyUpperBound(g, sppg, schools, cook_time);

    int initial_key = (0 * 1000 + 0) * 1000 + cook_time;
    dp[initial_key] = 0;
    updateDominanceMap(0, 0, cook_time, 0, dominance_map);

    std::priority_queue<State> frontier;
    frontier.push({0, 0, cook_time, 0});

    int states_explored = 0;

    while (!frontier.empty()) {
        State current = frontier.top();
        frontier.pop();
        states_explored++;

        if (current.cost >= best_cost) continue;

        if (isDominated(current.pos, current.mask, current.time, current.cost, dominance_map)) {
            continue;
        }

        for (int next = 0; next < n; ++next) {
            if (current.mask & (1 << next)) continue;

            int current_loc = (current.pos == 0) ? sppg : schools[current.pos - 1].id;
            auto result = dijkstra(g, current_loc);
            if (!result.success || result.dist[schools[next].id] == INF) continue;

            int travel = result.dist[schools[next].id];
            int arrival = current.time + travel;
            int food_age = arrival - cook_time;
            if (food_age > MAX_TIME) continue;

            if (arrival < schools[next].window.start) {
                arrival = schools[next].window.start;
            }
            if (arrival > schools[next].window.end) continue;

            int new_time = arrival + schools[next].unload_time;
            int new_mask = current.mask | (1 << next);
            int new_cost = current.cost + travel;

            if (new_cost >= best_cost) continue;

            int new_key = (next * 1000 + new_mask) * 1000 + new_time;
            int existing = dp.count(new_key) ? dp[new_key] : INF;

            if (new_cost < existing) {
                dp[new_key] = new_cost;
                parent[new_key] = {current.pos, current.mask};
                updateDominanceMap(next, new_mask, new_time, new_cost, dominance_map);

                State new_state;
                new_state.pos = next;
                new_state.mask = new_mask;
                new_state.time = new_time;
                new_state.cost = new_cost;
                frontier.push(new_state);

                if (new_mask == full_mask && new_cost < best_cost) {
                    best_cost = new_cost;
                }
            }
        }
    }

    DPModifiedResult result;
    result.total_cost = best_cost;
    result.feasible = (best_cost < INF);
    result.states_explored = states_explored;

    return result;
}
