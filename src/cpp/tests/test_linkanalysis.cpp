#include "linkanalysis/types.h"
#include <iostream>
#include <cassert>
#include <cstring>
#include <cmath>
#include <numeric>
#include <algorithm>
using namespace linkanalysis;

// Declare graph algorithm types and functions from linkanalysis.cpp
namespace linkanalysis {
    struct Edge { int to; double weight; };
    class Graph {
    public:
        int n_nodes;
        std::vector<std::vector<Edge>> adj;
        bool directed;
        Graph(int n, bool directed = false);
        void addEdge(int from, int to, double weight = 1.0);
        double degree(int node) const;
        double totalWeight() const;
        static Graph fromLinkGraph(const LinkGraph& lg);
    };
    std::vector<double> pagerank(const Graph& graph, double damping, int max_iter, double tol);
    std::vector<int> louvainCommunities(const Graph& graph);
    double modularity(const Graph& graph, const std::vector<int>& communities);
    struct DijkstraResult { std::vector<double> dist; std::vector<int> pred; };
    DijkstraResult dijkstra(const Graph& graph, int source);
    std::vector<int> shortestPath(const DijkstraResult& result, int target);
    std::vector<double> betweennessCentrality(const Graph& graph);
    std::vector<double> degreeCentrality(const Graph& graph);
}

void testSerialization() {
    LinkGraph graph;
    EntityRecord e1{}; e1.entity_id = 1; std::strncpy(e1.name, "Shell Corp A", 47);
    e1.entity_type = static_cast<uint8_t>(EntityType::ORGANIZATION);
    e1.confidence = static_cast<uint8_t>(Confidence::HIGH);
    graph.entities.push_back(e1);

    EntityRecord e2{}; e2.entity_id = 2; std::strncpy(e2.name, "Vessel IVAN", 47);
    e2.entity_type = static_cast<uint8_t>(EntityType::VESSEL);
    std::strncpy(e2.source_ref, "$AIS:273123456", 23);
    graph.entities.push_back(e2);

    EntityRecord e3{}; e3.entity_id = 3; std::strncpy(e3.name, "John Doe", 47);
    e3.entity_type = static_cast<uint8_t>(EntityType::PERSON);
    e3.sanctioned = 1;
    graph.entities.push_back(e3);

    EdgeRecord r1{}; r1.from_id = 3; r1.to_id = 1;
    r1.relation_type = static_cast<uint8_t>(RelationType::OWNS);
    r1.strength = 0.95f;
    graph.edges.push_back(r1);

    EdgeRecord r2{}; r2.from_id = 1; r2.to_id = 2;
    r2.relation_type = static_cast<uint8_t>(RelationType::OPERATES);
    r2.strength = 0.8f;
    graph.edges.push_back(r2);

    auto buf = serialize(graph);
    assert(std::memcmp(buf.data(), "$LNK", 4) == 0);
    LNKHeader hdr; LinkGraph dec;
    assert(deserialize(buf.data(), buf.size(), hdr, dec));
    assert(dec.entities.size() == 3);
    assert(dec.edges.size() == 2);

    auto connected = findConnected(graph, 1);
    assert(connected.size() == 2);

    std::cout << "  Serialization + graph ✓\n";
}

// ============================================================================
// Zachary's Karate Club Graph (34 nodes, 78 edges)
// Reference: [5] Zachary (1977)
// ============================================================================

Graph buildKarateClub() {
    Graph g(34, false);
    // Edge list from Zachary's paper (0-indexed)
    int edges[][2] = {
        {0,1},{0,2},{0,3},{0,4},{0,5},{0,6},{0,7},{0,8},{0,10},{0,11},
        {0,12},{0,13},{0,17},{0,19},{0,21},{0,31},
        {1,2},{1,3},{1,7},{1,13},{1,17},{1,19},{1,21},{1,30},
        {2,3},{2,7},{2,8},{2,9},{2,13},{2,27},{2,28},{2,32},
        {3,7},{3,12},{3,13},
        {4,6},{4,10},
        {5,6},{5,10},{5,16},
        {6,16},
        {8,30},{8,32},{8,33},
        {9,33},
        {13,33},
        {14,32},{14,33},
        {15,32},{15,33},
        {18,32},{18,33},
        {19,33},
        {20,32},{20,33},
        {22,32},{22,33},
        {23,25},{23,27},{23,29},{23,32},{23,33},
        {24,25},{24,27},{24,31},
        {25,31},
        {26,29},{26,33},
        {27,33},
        {28,31},{28,33},
        {29,32},{29,33},
        {30,32},{30,33},
        {31,32},{31,33},
        {32,33},
    };

    for (const auto& e : edges) {
        g.addEdge(e[0], e[1]);
    }
    return g;
}

void testPageRank() {
    auto g = buildKarateClub();

    auto pr = pagerank(g, 0.85, 100, 1e-8);
    assert(pr.size() == 34);

    // PageRank should sum to ~1.0
    double sum = 0;
    for (double r : pr) sum += r;
    assert(std::abs(sum - 1.0) < 1e-4);

    // Node 33 (administrator) and node 0 (instructor) should have
    // highest PageRank — they are the most connected nodes
    // Node 33 has degree 17, node 0 has degree 16
    int max_node = static_cast<int>(
        std::max_element(pr.begin(), pr.end()) - pr.begin());
    assert(max_node == 33 || max_node == 0);

    // Known: node 33 should have PR > node 0 (more connections to high-degree nodes)
    assert(pr[33] > 0.05);  // Should be roughly 0.07-0.10
    assert(pr[0] > 0.05);

    // Low-degree peripheral nodes should have much lower PageRank
    assert(pr[11] < pr[0]);  // Node 11 has degree 1

    std::cout << "  PageRank ✓ (top=" << max_node
              << ", PR[33]=" << pr[33]
              << ", PR[0]=" << pr[0] << ")\n";
}

void testLouvainCommunities() {
    auto g = buildKarateClub();

    auto communities = louvainCommunities(g);
    assert(communities.size() == 34);

    // Count unique communities
    std::set<int> unique(communities.begin(), communities.end());
    int n_communities = static_cast<int>(unique.size());

    // Zachary's club splits into 2 main factions (instructor vs admin)
    // Louvain typically finds 2-4 communities
    assert(n_communities >= 2 && n_communities <= 6);

    // Modularity should be positive (better than random)
    double Q = modularity(g, communities);
    assert(Q > 0.2);  // Known modularity for karate club is ~0.38-0.42

    // Key constraint: nodes 0 and 33 should be in different communities
    // (they are the leaders of the two factions)
    assert(communities[0] != communities[33]);

    std::cout << "  Louvain communities ✓ (k=" << n_communities
              << ", Q=" << Q
              << ", c[0]=" << communities[0]
              << ", c[33]=" << communities[33] << ")\n";
}

void testDijkstra() {
    auto g = buildKarateClub();

    // Shortest path from node 0 to node 33
    auto result = dijkstra(g, 0);

    // Distance from 0 to 33 should be 2 (0→2→33, or 0→8→33, etc.)
    // Actually node 0 connects to 31, and 31 connects to 33
    assert(result.dist[33] <= 3);
    assert(result.dist[0] == 0);

    // All nodes should be reachable (connected graph)
    for (int i = 0; i < 34; i++) {
        assert(result.dist[i] < 100);
    }

    // Reconstruct path
    auto path = shortestPath(result, 33);
    assert(!path.empty());
    assert(path.front() == 0);
    assert(path.back() == 33);
    assert(path.size() >= 2);

    std::cout << "  Dijkstra ✓ (dist[0→33]=" << result.dist[33]
              << ", path length=" << path.size() << ")\n";
}

void testBetweenness() {
    auto g = buildKarateClub();

    auto bc = betweennessCentrality(g);
    assert(bc.size() == 34);

    // Node 0 and 33 should have high betweenness (bridge between factions)
    // Node 0 (instructor) is known to have highest betweenness in the network
    int max_bc_node = static_cast<int>(
        std::max_element(bc.begin(), bc.end()) - bc.begin());

    // Top betweenness nodes should be 0, 33, 2, or 32 (known from literature)
    assert(max_bc_node == 0 || max_bc_node == 33 ||
           max_bc_node == 2 || max_bc_node == 32);

    // Peripheral nodes (degree 1) should have zero betweenness
    assert(bc[11] < 1.0);  // Node 11 has degree 1, minimal betweenness

    std::cout << "  Betweenness centrality ✓ (max at node " << max_bc_node
              << ", BC=" << bc[max_bc_node]
              << ", BC[11]=" << bc[11] << ")\n";
}

void testDegreeCentrality() {
    auto g = buildKarateClub();

    auto dc = degreeCentrality(g);
    assert(dc.size() == 34);

    // Node 33 has degree 17 → dc = 17/33 ≈ 0.515
    // Node 0 has degree 16 → dc = 16/33 ≈ 0.485
    assert(dc[33] > 0.4);
    assert(dc[0] > 0.4);

    // Node 11 has degree 1 → dc = 1/33 ≈ 0.030
    assert(dc[11] < 0.1);

    std::cout << "  Degree centrality ✓ (dc[33]=" << dc[33]
              << ", dc[0]=" << dc[0] << ")\n";
}

void testSimpleGraph() {
    // Triangle graph: 3 nodes, all connected
    Graph g(3, false);
    g.addEdge(0, 1); g.addEdge(1, 2); g.addEdge(0, 2);

    auto pr = pagerank(g, 0.85, 100, 1e-8);
    // All nodes equivalent → equal PageRank
    assert(std::abs(pr[0] - pr[1]) < 0.01);
    assert(std::abs(pr[1] - pr[2]) < 0.01);
    assert(std::abs(pr[0] - 1.0/3.0) < 0.01);

    // Dijkstra: all distances should be 1
    auto result = dijkstra(g, 0);
    assert(result.dist[1] == 1.0);
    assert(result.dist[2] == 1.0);

    // Communities: all in same community
    auto comm = louvainCommunities(g);
    assert(comm[0] == comm[1] && comm[1] == comm[2]);

    std::cout << "  Simple graph ✓\n";
}

int main() {
    std::cout << "=== linkanalysis-sdn-plugin tests ===\n";
    testSerialization();
    testSimpleGraph();
    testPageRank();
    testLouvainCommunities();
    testDijkstra();
    testBetweenness();
    testDegreeCentrality();
    std::cout << "All LINKANALYSIS tests passed.\n";
    return 0;
}
