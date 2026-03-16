#include "linkanalysis/types.h"
#include <iostream>
#include <cassert>
#include <cstring>
#include <cmath>
using namespace linkanalysis;
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
    assert(connected.size() == 2); // John Doe + Vessel

    std::cout << "  Serialization + graph ✓\n";
}
int main() { std::cout << "=== linkanalysis tests ===\n"; testSerialization();
    std::cout << "All LINKANALYSIS tests passed.\n"; return 0; }
