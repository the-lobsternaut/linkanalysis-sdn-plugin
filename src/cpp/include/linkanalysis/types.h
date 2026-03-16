#ifndef LINKANALYSIS_TYPES_H
#define LINKANALYSIS_TYPES_H

/**
 * Link Analysis (LINKANALYSIS) Plugin Types
 * ============================================
 *
 * Relationship mapping and network graph analysis for OSINT.
 * Connects entities (people, organizations, IPs, domains, vessels, aircraft)
 * and identifies patterns: clusters, central nodes, hidden connections.
 *
 * Output: $LNK FlatBuffer-aligned binary records
 *
 * Graph model:
 *   Nodes (entities) + Edges (relationships) in adjacency records
 */

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <cmath>

namespace linkanalysis {

static constexpr char LNK_FILE_ID[4] = {'$', 'L', 'N', 'K'};
static constexpr uint32_t LNK_VERSION = 1;

enum class EntityType : uint8_t {
    PERSON       = 0,
    ORGANIZATION = 1,
    VESSEL       = 2,   // Maritime (links to $AIS)
    AIRCRAFT     = 3,   // Aviation (links to $ADB)
    DOMAIN_NAME  = 4,   // Network (links to $NET)
    IP_ADDRESS   = 5,
    PHONE        = 6,
    EMAIL        = 7,
    SOCIAL_ACCT  = 8,   // Social media (links to $SOC)
    LOCATION     = 9,
    DOCUMENT     = 10,
    FINANCIAL    = 11,  // Bank account, crypto wallet
    SATELLITE    = 12,  // Space object (links to $OEM)
};

enum class RelationType : uint8_t {
    UNKNOWN       = 0,
    OWNS          = 1,   // Ownership
    OPERATES      = 2,   // Operational control
    FUNDS         = 3,   // Financial relationship
    COMMUNICATES  = 4,   // Communication link
    CO_LOCATED    = 5,   // Same location
    CO_TRAVELED   = 6,   // Traveled together
    REGISTERED_TO = 7,   // Domain/vessel/aircraft registered to
    ASSOCIATED    = 8,   // Generic association
    SANCTIONED    = 9,   // Under sanctions
    ALIAS_OF      = 10,  // Same entity, different name
    SUBORDINATE   = 11,  // Hierarchical relationship
    TRANSACTED    = 12,  // Financial transaction
};

enum class Confidence : uint8_t {
    UNVERIFIED = 0,  // 0-25% — automated/speculative
    LOW        = 1,  // 25-50%
    MEDIUM     = 2,  // 50-75%
    HIGH       = 3,  // 75-95%
    CONFIRMED  = 4,  // 95-100% — multiple independent sources
};

#pragma pack(push, 1)

struct LNKHeader {
    char     magic[4];
    uint32_t version;
    uint32_t source;
    uint32_t count;
};
static_assert(sizeof(LNKHeader) == 16, "LNKHeader must be 16 bytes");

/// Node record — entity in the graph
struct EntityRecord {
    uint64_t entity_id;        // Unique entity identifier
    char     name[48];         // Entity name/label
    char     alt_name[24];     // Alias / alternative name
    uint8_t  entity_type;      // EntityType enum
    uint8_t  confidence;       // Confidence enum
    double   lat_deg;          // Location (NAN if unknown)
    double   lon_deg;          // Location (NAN if unknown)
    double   first_seen;       // First observation epoch
    double   last_seen;        // Last observation epoch
    char     source_ref[24];   // Source reference (e.g., "$AIS:273123456")
    uint16_t edge_count;       // Number of connected edges
    uint8_t  sanctioned;       // 0=no, 1=yes
    uint8_t  reserved[5];
};

/// Edge record — relationship between entities
struct EdgeRecord {
    uint64_t from_id;          // Source entity ID
    uint64_t to_id;            // Target entity ID
    uint8_t  relation_type;    // RelationType enum
    uint8_t  confidence;       // Confidence enum
    uint8_t  bidirectional;    // 0=directed, 1=bidirectional
    uint8_t  reserved1;
    double   first_seen;       // When relationship first observed
    double   last_seen;        // Most recent observation
    float    strength;         // Relationship strength [0-1]
    char     evidence[32];     // Brief evidence description
    uint8_t  reserved2[4];
};

#pragma pack(pop)

// ============================================================================
// Serialization (entities + edges in one buffer)
// ============================================================================

struct LinkGraph {
    std::vector<EntityRecord> entities;
    std::vector<EdgeRecord> edges;
};

inline std::vector<uint8_t> serialize(const LinkGraph& graph) {
    // Layout: LNKHeader + entity_count(u32) + edge_count(u32) + entities + edges
    uint32_t nE = static_cast<uint32_t>(graph.entities.size());
    uint32_t nR = static_cast<uint32_t>(graph.edges.size());
    size_t size = sizeof(LNKHeader) + 8 + nE * sizeof(EntityRecord) + nR * sizeof(EdgeRecord);
    std::vector<uint8_t> buf(size);

    LNKHeader hdr;
    std::memcpy(hdr.magic, LNK_FILE_ID, 4);
    hdr.version = LNK_VERSION;
    hdr.source = 0;
    hdr.count = nE + nR;
    std::memcpy(buf.data(), &hdr, sizeof(LNKHeader));

    size_t off = sizeof(LNKHeader);
    std::memcpy(buf.data() + off, &nE, 4); off += 4;
    std::memcpy(buf.data() + off, &nR, 4); off += 4;

    if (nE > 0) { std::memcpy(buf.data() + off, graph.entities.data(), nE * sizeof(EntityRecord)); off += nE * sizeof(EntityRecord); }
    if (nR > 0) { std::memcpy(buf.data() + off, graph.edges.data(), nR * sizeof(EdgeRecord)); }

    return buf;
}

inline bool deserialize(const uint8_t* data, size_t len,
                         LNKHeader& hdr, LinkGraph& graph) {
    if (len < sizeof(LNKHeader) + 8) return false;
    std::memcpy(&hdr, data, sizeof(LNKHeader));
    if (std::memcmp(hdr.magic, LNK_FILE_ID, 4) != 0) return false;

    size_t off = sizeof(LNKHeader);
    uint32_t nE, nR;
    std::memcpy(&nE, data + off, 4); off += 4;
    std::memcpy(&nR, data + off, 4); off += 4;

    if (len < off + nE * sizeof(EntityRecord) + nR * sizeof(EdgeRecord)) return false;

    graph.entities.resize(nE);
    if (nE > 0) { std::memcpy(graph.entities.data(), data + off, nE * sizeof(EntityRecord)); off += nE * sizeof(EntityRecord); }
    graph.edges.resize(nR);
    if (nR > 0) { std::memcpy(graph.edges.data(), data + off, nR * sizeof(EdgeRecord)); }

    return true;
}

/// Find entities connected to a given entity
inline std::vector<uint64_t> findConnected(const LinkGraph& graph, uint64_t entityId) {
    std::vector<uint64_t> out;
    for (const auto& e : graph.edges) {
        if (e.from_id == entityId) out.push_back(e.to_id);
        if (e.to_id == entityId) out.push_back(e.from_id);
    }
    return out;
}

}  // namespace linkanalysis

#endif
