/**
 * @file cuckoo_orient.hpp
 * @brief Orient edges of a 2-choice cuckoo graph to give each vertex in-degree <= 1.
 *
 * The core primitive used by shock_hash for per-bucket placement: given
 * a small graph where each edge represents a key and each vertex
 * represents a slot, orient every edge toward one of its two endpoints
 * such that each vertex receives at most one edge. This is equivalent
 * to saying the graph is a "pseudoforest" (each connected component
 * has at most one cycle).
 *
 * Algorithm (two-phase):
 *   1. Peeling: repeatedly remove degree-1 vertices and their incident
 *      edges, assigning each such edge to its degree-1 endpoint.
 *   2. Cycle handling: if edges remain after peeling, every remaining
 *      vertex must have degree exactly 2 (pseudoforest property).
 *      Orient the remaining cycles by walking each cycle and assigning
 *      each edge to one of its endpoints consistently.
 *
 * If peeling exposes a vertex of degree >= 3 before its edges are
 * removed, the graph is not a pseudoforest and orientation fails.
 * The caller (shock_hash seed search) retries with a new seed.
 *
 * Complexity: O(V + E). Typical V = E = BucketSize ~ 64.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace maph::detail {

// Each edge is a (p1, p2) slot pair. assignment[i] = 0 means edge i is
// assigned to p1 (key uses hash 0); 1 means assigned to p2 (hash 1).
struct cuckoo_orient_result {
    std::vector<uint8_t> assignment;  // one byte per edge: 0 or 1
};

[[nodiscard]] inline std::optional<cuckoo_orient_result>
cuckoo_orient(const std::vector<std::pair<uint32_t, uint32_t>>& edges,
              size_t num_vertices) {
    size_t n = edges.size();
    if (n == 0) {
        return cuckoo_orient_result{{}};
    }

    // Adjacency: vertex -> list of (edge_index, other_vertex).
    std::vector<std::vector<std::pair<uint32_t, uint32_t>>> adj(num_vertices);
    for (size_t i = 0; i < n; ++i) {
        uint32_t a = edges[i].first;
        uint32_t b = edges[i].second;
        if (a >= num_vertices || b >= num_vertices) return std::nullopt;
        adj[a].push_back({static_cast<uint32_t>(i), b});
        if (a != b) {
            adj[b].push_back({static_cast<uint32_t>(i), a});
        }
    }

    std::vector<uint8_t> assignment(n, 0xFF);

    // Self-loops: an edge (v, v) can be assigned to v iff v has no other
    // edges that would conflict. For simplicity we orient self-loops
    // to their single endpoint; collision will be caught by a later check.
    for (size_t i = 0; i < n; ++i) {
        if (edges[i].first == edges[i].second) {
            assignment[i] = 0;
        }
    }

    // Peeling: find degree-1 vertices and peel their edges.
    std::vector<uint32_t> queue;
    queue.reserve(num_vertices);
    for (uint32_t v = 0; v < num_vertices; ++v) {
        if (adj[v].size() == 1) queue.push_back(v);
    }

    auto remove_edge_from = [&](uint32_t vertex, uint32_t edge_idx) {
        auto& list = adj[vertex];
        for (size_t i = 0; i < list.size(); ++i) {
            if (list[i].first == edge_idx) {
                list[i] = list.back();
                list.pop_back();
                return;
            }
        }
    };

    while (!queue.empty()) {
        uint32_t v = queue.back();
        queue.pop_back();
        if (adj[v].size() != 1) continue;

        auto [edge_idx, other] = adj[v][0];
        if (assignment[edge_idx] != 0xFF) {
            adj[v].clear();
            continue;
        }

        // Assign this edge to v.
        assignment[edge_idx] = (edges[edge_idx].first == v) ? 0 : 1;
        adj[v].clear();
        remove_edge_from(other, edge_idx);
        if (adj[other].size() == 1) queue.push_back(other);
    }

    // Remaining: either empty, or disjoint cycles. Any vertex with
    // degree != 0 must have degree exactly 2 (pseudoforest).
    for (uint32_t v = 0; v < num_vertices; ++v) {
        if (!adj[v].empty() && adj[v].size() != 2) {
            return std::nullopt;  // not a pseudoforest
        }
    }

    // Orient cycles by walking them. Pick any starting vertex on a cycle
    // and assign each edge to the vertex it arrives at.
    for (uint32_t v = 0; v < num_vertices; ++v) {
        while (!adj[v].empty()) {
            auto [edge_idx, other] = adj[v][0];
            if (assignment[edge_idx] != 0xFF) {
                // Already oriented (from another walk direction); drop
                // this half of the edge from v's list and continue.
                adj[v].erase(adj[v].begin());
                continue;
            }
            // Assign to the "other" endpoint so v has no incoming edge
            // from this step; cycle walk continues from other.
            assignment[edge_idx] = (edges[edge_idx].first == other) ? 0 : 1;
            adj[v].erase(adj[v].begin());
            remove_edge_from(other, edge_idx);
        }
    }

    // Final sanity: every edge assigned, no collisions (in-degree <= 1).
    std::vector<int32_t> owner(num_vertices, -1);
    for (size_t i = 0; i < n; ++i) {
        if (assignment[i] == 0xFF) return std::nullopt;
        uint32_t slot = (assignment[i] == 0) ? edges[i].first : edges[i].second;
        if (owner[slot] != -1) return std::nullopt;
        owner[slot] = static_cast<int32_t>(i);
    }

    return cuckoo_orient_result{std::move(assignment)};
}

} // namespace maph::detail
