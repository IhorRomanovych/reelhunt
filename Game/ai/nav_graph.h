// Game/ai/nav_graph.h - the level's AI navigation graph (mapAI.grf) in scene coordinates.
//
// Hunters walk between graph nodes: goals and patrols name nodes ('attak1', 'wilson') and paths
// ('AIPath03'); hunting runs A* from the node nearest the hunter to the node nearest the target.
//
// Each link carries a permission, the GRF link type: 3 = AISCRIPT_GRAPHLINK_ALLOW_EVERYTHING,
// 0 = AISCRIPT_GRAPHLINK_ALLOW_NOTHING (Jury_Turf: 7414 open, 28 closed - the links through its
// locked gates and the chute door). Closed links are not walked. GraphModifyConnections(entity,
// mode) changes the links that pass across a door or trigger (setLinksNear).
#pragma once
#include "engine_types.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "Assets/grf.h"
#include "Game/level_frame.h"
#include "Math/vector.h"

namespace Game {

class NavGraph {
public:
    struct Node {
        Math::Vector3 position{0.0f, 0.0f, 0.0f}; // scene frame, at floor level
        float32 radius = 0.3f;
        int32 area = -1;
        std::string label;                        // lower case, may be empty
        std::vector<int32> links;
        std::vector<uint8> linkOpen; // parallel to links: 1 = may be walked
    };

    NavGraph() = default;
    NavGraph(const Assets::GrfData& grf, const LevelFrame& frame);

    [[nodiscard]] bool empty() const noexcept { return nodes_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return nodes_.size(); }
    [[nodiscard]] const Node& node(int32 i) const { return nodes_[static_cast<std::size_t>(i)]; }

    // A node by its script name (label or id), case-insensitive; -1 when absent.
    [[nodiscard]] int32 find(const std::string& name) const;
    // The nodes of a named path ('AIPath03'), empty when absent.
    [[nodiscard]] std::vector<int32> path(const std::string& name) const;
    [[nodiscard]] int32 areaIndex(const std::string& name) const;
    [[nodiscard]] const std::string& areaName(int32 area) const;

    // The node closest to `p`, preferring nodes on the same floor (within `maxRise` metres
    // vertically); -1 when the graph is empty.
    [[nodiscard]] int32 nearest(const Math::Vector3& p, float32 maxRise = 2.5f) const;
    // The closest same-floor node `clear` accepts (a free line from `p`: no wall between), trying
    // the `tries` nearest; else nearest().
    [[nodiscard]] int32 nearestClear(const Math::Vector3& p, const std::function<bool(const Math::Vector3&)>& clear,
                                     std::size_t tries = 8, float32 maxRise = 2.5f) const;
    // The route over the links, inclusive of both ends, as the exe plans it: breadth first from
    // `from` (0x4046d0: hop counts, a FIFO, each node's links in order, stopping on reaching `to`),
    // then back from `to` through the first link (in its order) to a node one hop nearer whose link
    // on is open (0x4049c0) - the fewest links, not the shortest way. When `to` cannot be reached it
    // is empty, or with `closest` (ours) the route to the reachable node nearest to `to`.
    [[nodiscard]] std::vector<int32> route(int32 from, int32 to, bool closest = false) const;
    // Nodes within `radius` of node `around` reachable over links (for wander-searching).
    [[nodiscard]] std::vector<int32> within(int32 around, float32 radius) const;
    // Breadth first from `from` over open links: `from` and the nodes within `radius` of `centre`,
    // at most `cap`, each with its link depth (`from` 0) - a ped's nearby list (CNavigation +0x50
    // nodes / +0x150 depths, built by 0x563fd0).
    [[nodiscard]] std::vector<std::pair<int32, int32>> ranked(int32 from, const Math::Vector3& centre, float32 radius,
                                                              std::size_t cap) const;

    // Opens or closes every link whose segment passes within `reach` of `centre` (horizontally,
    // within 3 m of its height); returns how many directed links that covered.
    std::size_t setLinksNear(const Math::Vector3& centre, float32 reach, bool open);
    [[nodiscard]] std::size_t closedLinks() const;

private:
    std::vector<Node> nodes_;
    std::vector<std::pair<std::string, std::vector<int32>>> paths_;
    std::vector<std::string> areas_;
};

} // namespace Game
