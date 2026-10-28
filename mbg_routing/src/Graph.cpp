#include "Graph.h"

void Graph::addEdge(int u, int v, int weight, bool directed) {
    adj[u].push_back({v, weight});
    if (!directed) {
        adj[v].push_back({u, weight});
    }
}

void Graph::blockEdge(int u, int v) {
    blocked_edges.insert(u * 1000 + v);
    blocked_edges.insert(v * 1000 + u);
}

void Graph::unblockEdge(int u, int v) {
    blocked_edges.erase(u * 1000 + v);
    blocked_edges.erase(v * 1000 + u);
}

bool Graph::isBlocked(int u, int v) const {
    return blocked_edges.count(u * 1000 + v) > 0;
}

std::vector<int> Graph::getNodes() const {
    std::vector<int> nodes;
    for (const auto& [node, neighbors] : adj) {
        nodes.push_back(node);
    }
    return nodes;
}
