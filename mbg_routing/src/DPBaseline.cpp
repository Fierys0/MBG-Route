#include "DPBaseline.h"
#include <algorithm>

DPResult dpBaseline(const Graph& g, int sppg, const std::vector<School>& schools, int cook_time) {
    int n = schools.size();
    int full_mask = (1 << n) - 1;

    std::vector<int> locations;
    locations.push_back(sppg);
    for (const auto& s : schools) {
        locations.push_back(s.id);
    }

    int num_locs = locations.size();
    std::vector<std::vector<int>> dist(num_locs, std::vector<int>(num_locs, INF));

    for (int i = 0; i < num_locs; ++i) {
        auto result = dijkstra(g, locations[i]);
        for (int j = 0; j < num_locs; ++j) {
            dist[i][j] = result.dist[locations[j]];
        }
    }

    std::vector<std::vector<std::vector<int>>> dp(
        num_locs,
        std::vector<std::vector<int>>(1 << n, std::vector<int>(MAX_TIME + 1, INF))
    );
    std::vector<std::vector<std::vector<std::pair<int, int>>>> parent(
        num_locs,
        std::vector<std::vector<std::pair<int, int>>>(1 << n, std::vector<std::pair<int, int>>(MAX_TIME + 1, {-1, -1}))
    );

    dp[0][0][cook_time] = 0;

    for (int mask = 0; mask <= full_mask; ++mask) {
        for (int pos = 0; pos < num_locs; ++pos) {
            for (int time = cook_time; time <= MAX_TIME; ++time) {
                if (dp[pos][mask][time] == INF) continue;

                for (int next = 0; next < n; ++next) {
                    if (mask & (1 << next)) continue;

                    int next_pos = next + 1;
                    int travel = dist[pos][next_pos];
                    if (travel == INF) continue;

                    int arrival = time + travel;
                    int food_age = arrival - cook_time;
                    if (food_age > MAX_TIME) continue;

                    if (arrival < schools[next].window.start) {
                        arrival = schools[next].window.start;
                    }
                    if (arrival > schools[next].window.end) continue;

                    int new_time = arrival + schools[next].unload_time;
                    int new_mask = mask | (1 << next);
                    int new_cost = dp[pos][mask][time] + travel;

                    if (new_cost < dp[next_pos][new_mask][new_time]) {
                        dp[next_pos][new_mask][new_time] = new_cost;
                        parent[next_pos][new_mask][new_time] = {pos, time};
                    }
                }
            }
        }
    }

    DPResult result;
    result.total_cost = INF;
    result.feasible = false;

    int best_pos = -1;
    int best_time = -1;

    for (int pos = 0; pos < num_locs; ++pos) {
        for (int time = cook_time; time <= MAX_TIME; ++time) {
            if (dp[pos][full_mask][time] < result.total_cost) {
                result.total_cost = dp[pos][full_mask][time];
                result.feasible = true;
                best_pos = pos;
                best_time = time;
            }
        }
    }

    if (result.feasible && best_pos > 0) {
        int cur_pos = best_pos;
        int cur_mask = full_mask;
        int cur_time = best_time;

        while (cur_pos > 0 && cur_mask > 0) {
            result.route.push_back(schools[cur_pos - 1].id);
            auto [p_pos, p_time] = parent[cur_pos][cur_mask][cur_time];
            cur_mask ^= (1 << (cur_pos - 1));
            cur_pos = p_pos;
            cur_time = p_time;
        }
        std::reverse(result.route.begin(), result.route.end());
    }

    return result;
}
