#include "DPBaseline.h"
#include "DPModified.h"
#include <iostream>
#include <chrono>

int main() {
    Graph g;
    g.addEdge(0, 1, 10);
    g.addEdge(0, 2, 15);
    g.addEdge(1, 2, 5);
    g.addEdge(1, 3, 20);
    g.addEdge(2, 3, 10);
    g.addEdge(3, 4, 15);
    g.addEdge(4, 5, 10);

    std::vector<School> schools = {
        {1, "SD 1", {30, 60}, 5},
        {2, "SD 2", {40, 70}, 5},
        {3, "SD 3", {50, 80}, 5},
        {4, "SD 4", {60, 90}, 5},
        {5, "SD 5", {70, 100}, 5}
    };

    int cook_time = 0;

    auto start1 = std::chrono::high_resolution_clock::now();
    auto result1 = dpBaseline(g, 0, schools, cook_time);
    auto end1 = std::chrono::high_resolution_clock::now();
    auto duration1 = std::chrono::duration_cast<std::chrono::microseconds>(end1 - start1);

    std::cout << "=== Baseline (Dijkstra + DP) ===" << std::endl;
    std::cout << "Total cost: " << result1.total_cost << std::endl;
    std::cout << "Feasible: " << (result1.feasible ? "Yes" : "No") << std::endl;
    std::cout << "Route: ";
    for (int id : result1.route) std::cout << id << " ";
    std::cout << std::endl;
    std::cout << "Time: " << duration1.count() << " microseconds" << std::endl;

    auto start2 = std::chrono::high_resolution_clock::now();
    auto result2 = dpModified(g, 0, schools, cook_time);
    auto end2 = std::chrono::high_resolution_clock::now();
    auto duration2 = std::chrono::duration_cast<std::chrono::microseconds>(end2 - start2);

    std::cout << "\n=== Modified (Optimized) ===" << std::endl;
    std::cout << "Total cost: " << result2.total_cost << std::endl;
    std::cout << "Feasible: " << (result2.feasible ? "Yes" : "No") << std::endl;
    std::cout << "Route: ";
    for (int id : result2.route) std::cout << id << " ";
    std::cout << std::endl;
    std::cout << "States explored: " << result2.states_explored << std::endl;
    std::cout << "Time: " << duration2.count() << " microseconds" << std::endl;

    return 0;
}
