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

int greedyUpperBound(const Graph& g, int sppg, const std::vector<School>& schools, int cook_time, std::vector<int>& greedy_route) {
    int current = sppg;
    int total = 0;
    int time = cook_time;
    std::set<int> unvisited;
    for (size_t i = 0; i < schools.size(); ++i) {
        unvisited.insert((int)i);
    }
    greedy_route.clear();

    while (!unvisited.empty()) {
        int best_next = -1;
        int best_dist = INF;

        auto result = dijkstra(g, current);
        if (!result.success) {
            greedy_route.clear();
            return INF;
        }

        for (int next : unvisited) {
            auto it = result.dist.find(schools[next].id);
            if (it == result.dist.end() || it->second == INF) continue;
            int d = it->second;

            int arrival = time + d;
            int food_age = arrival - cook_time;
            if (food_age <= MAX_TIME && arrival <= schools[next].window.end) {
                if (d < best_dist) {
                    best_dist = d;
                    best_next = next;
                }
            }
        }

        if (best_next == -1 || best_dist == INF) {
            greedy_route.clear();
            return INF;
        }

        total += best_dist;
        int arrival = std::max(time + best_dist, schools[best_next].window.start);
        time = arrival + schools[best_next].unload_time;
        current = schools[best_next].id;
        greedy_route.push_back(schools[best_next].id);
        unvisited.erase(best_next);
    }

    return total;
}

DPModifiedResult dpModified(const Graph& g, int sppg, const std::vector<School>& schools, int cook_time) {
    int n = (int)schools.size();
    int full_mask = (1 << n) - 1;

    DominanceMap dominance_map;
    std::unordered_map<int, int> dp;

    std::vector<int> best_route;
    int best_cost = greedyUpperBound(g, sppg, schools, cook_time, best_route);

    int initial_key = (0 * 1000 + 0) * 1000 + cook_time;
    dp[initial_key] = 0;
    updateDominanceMap(0, 0, cook_time, 0, dominance_map);

    struct SearchState {
        int pos;
        int mask;
        int time;
        int cost;
        std::vector<int> path;

        bool operator<(const SearchState& other) const {
            return cost > other.cost;
        }
    };

    std::priority_queue<SearchState> frontier;
    frontier.push({0, 0, cook_time, 0, {}});

    int states_explored = 0;

    while (!frontier.empty()) {
        SearchState current = frontier.top();
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
            if (!result.success) continue;

            auto it = result.dist.find(schools[next].id);
            if (it == result.dist.end() || it->second == INF) continue;

            int travel = it->second;
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
            int new_pos = next + 1;

            if (new_cost >= best_cost) continue;

            int new_key = (new_pos * 1000 + new_mask) * 1000 + new_time;
            int existing = dp.count(new_key) ? dp[new_key] : INF;

            if (new_cost < existing) {
                dp[new_key] = new_cost;
                updateDominanceMap(new_pos, new_mask, new_time, new_cost, dominance_map);

                SearchState new_state;
                new_state.pos = new_pos;
                new_state.mask = new_mask;
                new_state.time = new_time;
                new_state.cost = new_cost;
                new_state.path = current.path;
                new_state.path.push_back(schools[next].id);
                frontier.push(new_state);

                if (new_mask == full_mask && new_cost < best_cost) {
                    best_cost = new_cost;
                    best_route = new_state.path;
                }
            }
        }
    }

    DPModifiedResult result;
    result.total_cost = best_cost;
    result.route = best_route;
    result.feasible = (best_cost < INF && !best_route.empty());
    result.states_explored = states_explored;

    return result;
}
