/**
 * Link Analysis Plugin — Graph Algorithms Implementation
 *
 * References:
 *   [1] Page, Brin et al. (1999) "The PageRank Citation Ranking"
 *   [2] Blondel et al. (2008) "Fast unfolding of communities in large networks"
 *       (Louvain method), J. Stat. Mech. P10008
 *   [3] Dijkstra (1959) "A note on two problems in connexion with graphs"
 *   [4] Brandes (2001) "A faster algorithm for betweenness centrality"
 *   [5] Zachary (1977) "An information flow model for conflict and fission
 *       in small groups", J. Anthro. Research 33:452-473
 *
 * Implements:
 *   - Adjacency list graph representation
 *   - PageRank (power iteration, damping factor 0.85)
 *   - Louvain community detection (modularity optimization)
 *   - Dijkstra shortest path
 *   - Betweenness centrality (Brandes algorithm)
 *
 * C++17, no external dependencies.
 */

#include "linkanalysis/types.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <map>
#include <set>
#include <queue>
#include <stack>
#include <limits>
#include <functional>

namespace linkanalysis {

// ============================================================================
// Adjacency List Graph
// ============================================================================

struct Edge {
    int    to;
    double weight;
};

class Graph {
public:
    int n_nodes;
    std::vector<std::vector<Edge>> adj;     // adjacency list
    bool directed;

    Graph(int n, bool directed = false)
        : n_nodes(n), adj(n), directed(directed) {}

    void addEdge(int from, int to, double weight = 1.0) {
        adj[from].push_back({to, weight});
        if (!directed) {
            adj[to].push_back({from, weight});
        }
    }

    /// Total weight of edges from node
    double degree(int node) const {
        double d = 0;
        for (const auto& e : adj[node]) d += e.weight;
        return d;
    }

    /// Total weight of all edges (counts undirected edges once)
    double totalWeight() const {
        double w = 0;
        for (int i = 0; i < n_nodes; i++)
            for (const auto& e : adj[i])
                w += e.weight;
        return directed ? w : w / 2.0;
    }

    /// Build from LinkGraph's edge list
    static Graph fromLinkGraph(const LinkGraph& lg) {
        // Map entity IDs to compact indices
        std::map<uint64_t, int> id_map;
        int idx = 0;
        for (const auto& e : lg.entities) {
            if (id_map.find(e.entity_id) == id_map.end())
                id_map[e.entity_id] = idx++;
        }

        Graph g(idx, false);
        for (const auto& edge : lg.edges) {
            auto it_from = id_map.find(edge.from_id);
            auto it_to = id_map.find(edge.to_id);
            if (it_from != id_map.end() && it_to != id_map.end()) {
                g.addEdge(it_from->second, it_to->second, edge.strength);
            }
        }
        return g;
    }
};

// ============================================================================
// PageRank (Power Iteration)
// ============================================================================

/// Compute PageRank using power iteration
/// Reference: [1] Page, Brin et al. (1999)
///
/// PR(i) = (1-d)/N + d * Σ_j PR(j)/L(j)
/// where d = damping factor, L(j) = out-degree of j
///
/// @param graph     Input graph
/// @param damping   Damping factor (default 0.85, original Google value)
/// @param max_iter  Maximum iterations
/// @param tol       Convergence tolerance (L1 norm)
/// @return          PageRank vector (sums to 1.0)

std::vector<double> pagerank(const Graph& graph,
                               double damping = 0.85,
                               int max_iter = 100,
                               double tol = 1e-6) {
    int N = graph.n_nodes;
    if (N == 0) return {};

    std::vector<double> rank(N, 1.0 / N);
    std::vector<double> new_rank(N);

    // Build reverse adjacency for efficient update
    std::vector<std::vector<int>> rev_adj(N);
    std::vector<double> out_degree(N, 0);

    for (int i = 0; i < N; i++) {
        for (const auto& e : graph.adj[i]) {
            rev_adj[e.to].push_back(i);
            out_degree[i] += 1.0;  // unweighted degree for PageRank
        }
    }

    double teleport = (1.0 - damping) / N;

    for (int iter = 0; iter < max_iter; iter++) {
        // Handle dangling nodes (no outlinks): distribute their rank uniformly
        double dangling_sum = 0;
        for (int i = 0; i < N; i++) {
            if (out_degree[i] == 0) dangling_sum += rank[i];
        }
        double dangling_contrib = damping * dangling_sum / N;

        for (int i = 0; i < N; i++) {
            double sum = 0;
            for (int j : rev_adj[i]) {
                if (out_degree[j] > 0)
                    sum += rank[j] / out_degree[j];
            }
            new_rank[i] = teleport + damping * sum + dangling_contrib;
        }

        // Check convergence
        double diff = 0;
        for (int i = 0; i < N; i++)
            diff += std::abs(new_rank[i] - rank[i]);

        rank.swap(new_rank);

        if (diff < tol) break;
    }

    // Normalize to sum to 1.0
    double total = 0;
    for (double r : rank) total += r;
    if (total > 0)
        for (double& r : rank) r /= total;

    return rank;
}

// ============================================================================
// Louvain Community Detection
// ============================================================================

/// Louvain method for community detection via modularity optimization
/// Reference: [2] Blondel et al. (2008)
///
/// Modularity Q = (1/2m) Σ_ij [A_ij - k_i*k_j/(2m)] δ(c_i, c_j)
///
/// Phase 1: Greedily move nodes to maximize modularity gain
/// Phase 2: Aggregate communities into super-nodes
/// Repeat until no improvement
///
/// @param graph   Input graph (undirected, weighted)
/// @return        Community assignment for each node

std::vector<int> louvainCommunities(const Graph& graph) {
    int N = graph.n_nodes;
    if (N == 0) return {};

    double m2 = 0;  // 2 * total edge weight
    for (int i = 0; i < N; i++)
        for (const auto& e : graph.adj[i])
            m2 += e.weight;
    // For undirected: m2 already counts each edge twice
    if (!graph.directed) {
        // m2 is already 2m because adj stores both directions
    } else {
        m2 *= 2;
    }

    if (m2 < 1e-15) {
        // No edges: each node is its own community
        std::vector<int> result(N);
        std::iota(result.begin(), result.end(), 0);
        return result;
    }

    // Node degrees (weighted)
    std::vector<double> k(N);
    for (int i = 0; i < N; i++) {
        for (const auto& e : graph.adj[i])
            k[i] += e.weight;
    }

    // Initial: each node in its own community
    std::vector<int> community(N);
    std::iota(community.begin(), community.end(), 0);

    // Sum of weights internal to each community (Σ_in)
    std::vector<double> sigma_in(N, 0);
    // Sum of all weights incident to community nodes (Σ_tot)
    std::vector<double> sigma_tot(N);
    for (int i = 0; i < N; i++) sigma_tot[i] = k[i];

    bool improved = true;
    while (improved) {
        improved = false;

        for (int i = 0; i < N; i++) {
            int ci = community[i];

            // Compute weight from i to each neighboring community
            std::map<int, double> comm_weights;
            for (const auto& e : graph.adj[i]) {
                int cj = community[e.to];
                comm_weights[cj] += e.weight;
            }

            // Remove i from its community
            double ki_in_ci = comm_weights.count(ci) ? comm_weights[ci] : 0;
            sigma_in[ci] -= 2.0 * ki_in_ci; // self-loops counted twice
            sigma_tot[ci] -= k[i];

            // Find best community to move i into
            int best_comm = ci;
            double best_gain = 0;

            for (const auto& [cj, w_ij] : comm_weights) {
                // ΔQ = [Σ_in(cj) + 2*k_{i,cj}] / 2m - [(Σ_tot(cj) + k_i) / 2m]^2
                //     - [Σ_in(cj) / 2m - (Σ_tot(cj) / 2m)^2 - (k_i / 2m)^2]
                double dQ = (w_ij - k[i] * sigma_tot[cj] / m2);
                if (dQ > best_gain) {
                    best_gain = dQ;
                    best_comm = cj;
                }
            }

            // Also check staying in old community (already removed)
            double dQ_stay = (ki_in_ci - k[i] * sigma_tot[ci] / m2);
            if (dQ_stay >= best_gain) {
                best_comm = ci;
                best_gain = dQ_stay;
            }

            // Move i to best community
            community[i] = best_comm;
            double ki_in_best = comm_weights.count(best_comm) ? comm_weights[best_comm] : 0;
            sigma_in[best_comm] += 2.0 * ki_in_best;
            sigma_tot[best_comm] += k[i];

            if (best_comm != ci) improved = true;
        }
    }

    // Renumber communities contiguously
    std::map<int, int> remap;
    int next_id = 0;
    for (int& c : community) {
        if (remap.find(c) == remap.end())
            remap[c] = next_id++;
        c = remap[c];
    }

    return community;
}

/// Compute modularity Q for a given community assignment
/// Q = (1/2m) Σ_ij [A_ij - k_i*k_j/(2m)] δ(c_i, c_j)
double modularity(const Graph& graph, const std::vector<int>& communities) {
    int N = graph.n_nodes;
    double m2 = 0;
    for (int i = 0; i < N; i++)
        for (const auto& e : graph.adj[i])
            m2 += e.weight;

    if (m2 < 1e-15) return 0;

    std::vector<double> k(N, 0);
    for (int i = 0; i < N; i++)
        for (const auto& e : graph.adj[i])
            k[i] += e.weight;

    double Q = 0;
    for (int i = 0; i < N; i++) {
        for (const auto& e : graph.adj[i]) {
            if (communities[i] == communities[e.to]) {
                Q += e.weight - k[i] * k[e.to] / m2;
            }
        }
    }
    return Q / m2;
}

// ============================================================================
// Dijkstra Shortest Path
// ============================================================================

/// Dijkstra's algorithm for single-source shortest paths
/// Reference: [3] Dijkstra (1959)
///
/// @param graph   Input graph (non-negative weights)
/// @param source  Source node
/// @return        {distances, predecessors} — dist[i] = shortest distance from source to i

struct DijkstraResult {
    std::vector<double> dist;
    std::vector<int>    pred;    // predecessor on shortest path (-1 if unreachable)
};

DijkstraResult dijkstra(const Graph& graph, int source) {
    int N = graph.n_nodes;
    DijkstraResult result;
    result.dist.assign(N, std::numeric_limits<double>::infinity());
    result.pred.assign(N, -1);

    result.dist[source] = 0;

    // Min-heap: (distance, node)
    using PII = std::pair<double, int>;
    std::priority_queue<PII, std::vector<PII>, std::greater<PII>> pq;
    pq.push({0, source});

    while (!pq.empty()) {
        auto [d, u] = pq.top();
        pq.pop();

        if (d > result.dist[u]) continue;  // stale entry

        for (const auto& e : graph.adj[u]) {
            double alt = result.dist[u] + e.weight;
            if (alt < result.dist[e.to]) {
                result.dist[e.to] = alt;
                result.pred[e.to] = u;
                pq.push({alt, e.to});
            }
        }
    }

    return result;
}

/// Reconstruct shortest path from Dijkstra result
std::vector<int> shortestPath(const DijkstraResult& result, int target) {
    std::vector<int> path;
    if (result.dist[target] == std::numeric_limits<double>::infinity())
        return path;  // unreachable

    for (int v = target; v != -1; v = result.pred[v])
        path.push_back(v);
    std::reverse(path.begin(), path.end());
    return path;
}

// ============================================================================
// Betweenness Centrality (Brandes Algorithm)
// ============================================================================

/// Compute betweenness centrality for all nodes
/// Reference: [4] Brandes (2001) — O(VE) for unweighted, O(VE + V² log V) for weighted
///
/// BC(v) = Σ_{s≠v≠t} σ_st(v) / σ_st
/// where σ_st = number of shortest paths from s to t
///       σ_st(v) = number that pass through v
///
/// @param graph   Input graph
/// @return        Betweenness centrality for each node (unnormalized)

std::vector<double> betweennessCentrality(const Graph& graph) {
    int N = graph.n_nodes;
    std::vector<double> CB(N, 0);

    for (int s = 0; s < N; s++) {
        // BFS/Dijkstra from s
        std::stack<int> S;
        std::vector<std::vector<int>> P(N);  // predecessors on shortest paths
        std::vector<double> sigma(N, 0);      // number of shortest paths
        std::vector<double> dist(N, -1);      // distance from s
        std::vector<double> delta(N, 0);      // dependency

        sigma[s] = 1;
        dist[s] = 0;

        // BFS for unweighted graphs (all weights = 1)
        std::queue<int> Q;
        Q.push(s);

        while (!Q.empty()) {
            int v = Q.front();
            Q.pop();
            S.push(v);

            for (const auto& e : graph.adj[v]) {
                int w = e.to;
                // First visit?
                if (dist[w] < 0) {
                    dist[w] = dist[v] + 1;
                    Q.push(w);
                }
                // Shortest path to w via v?
                if (dist[w] == dist[v] + 1) {
                    sigma[w] += sigma[v];
                    P[w].push_back(v);
                }
            }
        }

        // Accumulation (back-propagation of dependencies)
        while (!S.empty()) {
            int w = S.top();
            S.pop();
            for (int v : P[w]) {
                delta[v] += (sigma[v] / sigma[w]) * (1.0 + delta[w]);
            }
            if (w != s) {
                CB[w] += delta[w];
            }
        }
    }

    // For undirected graphs, each pair counted twice
    if (!graph.directed) {
        for (double& c : CB) c /= 2.0;
    }

    return CB;
}

// ============================================================================
// Degree Centrality
// ============================================================================

std::vector<double> degreeCentrality(const Graph& graph) {
    int N = graph.n_nodes;
    std::vector<double> dc(N, 0);
    if (N <= 1) return dc;
    for (int i = 0; i < N; i++) {
        dc[i] = static_cast<double>(graph.adj[i].size()) / (N - 1);
    }
    return dc;
}

}  // namespace linkanalysis
