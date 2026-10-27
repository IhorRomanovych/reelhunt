// Game/ai/nav_graph.cpp - AI navigation graph. See nav_graph.h.
#include "Game/ai/nav_graph.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <queue>

namespace Game {

namespace {

std::string lower(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

NavGraph::NavGraph(const Assets::GrfData& grf, const LevelFrame& frame)
{
    nodes_.reserve(grf.nodes.size());
    for (const Assets::GrfNode& g : grf.nodes)
    {
        Node n;
        // GRF positions are level coordinates with Z up, like .inst placements.
        n.position = frame.placementOrigin(g.position);
        n.radius = g.radius;
        n.area = g.area;
        n.label = lower(g.label.empty() ? g.name : g.label);
        for (const Assets::GrfLink& l : g.links)
            if (l.node >= 0 && static_cast<std::size_t>(l.node) < grf.nodes.size())
            {
                n.links.push_back(l.node);
                n.linkOpen.push_back(l.type != 0 ? 1 : 0);
            }
        nodes_.push_back(std::move(n));
    }
    for (const Assets::GrfPath& p : grf.paths)
        paths_.emplace_back(lower(p.name), p.nodes);
    for (const std::string& a : grf.areas)
        areas_.push_back(lower(a));
}

int32 NavGraph::find(const std::string& name) const
{
    const std::string key = lower(name);
    for (std::size_t i = 0; i < nodes_.size(); ++i)
        if (nodes_[i].label == key)
            return static_cast<int32>(i);
    return -1;
}

std::vector<int32> NavGraph::path(const std::string& name) const
{
    const std::string key = lower(name);
    for (const auto& [n, nodes] : paths_)
        if (n == key)
            return nodes;
    return {};
}

int32 NavGraph::areaIndex(const std::string& name) const
{
    const std::string key = lower(name);
    for (std::size_t i = 0; i < areas_.size(); ++i)
        if (areas_[i] == key)
            return static_cast<int32>(i);
    return -1;
}

const std::string& NavGraph::areaName(int32 area) const
{
    static const std::string kNone;
    return area >= 0 && static_cast<std::size_t>(area) < areas_.size() ? areas_[static_cast<std::size_t>(area)] : kNone;
}

int32 NavGraph::nearest(const Math::Vector3& p, float32 maxRise) const
{
    int32 best = -1, bestAny = -1;
    float32 bestD = std::numeric_limits<float32>::max(), bestAnyD = bestD;
    for (std::size_t i = 0; i < nodes_.size(); ++i)
    {
        const Math::Vector3 d = nodes_[i].position - p;
        const float32 dist = d.x * d.x + d.z * d.z + d.y * d.y;
        if (dist < bestAnyD)
        {
            bestAnyD = dist;
            bestAny = static_cast<int32>(i);
        }
        if (std::fabs(d.y) <= maxRise && dist < bestD)
        {
            bestD = dist;
            best = static_cast<int32>(i);
        }
    }
    return best >= 0 ? best : bestAny;
}

int32 NavGraph::nearestClear(const Math::Vector3& p, const std::function<bool(const Math::Vector3&)>& clear,
                             std::size_t tries, float32 maxRise) const
{
    std::vector<std::pair<float32, int32>> near;
    for (std::size_t i = 0; i < nodes_.size(); ++i)
    {
        const Math::Vector3 d = nodes_[i].position - p;
        if (std::fabs(d.y) <= maxRise)
            near.push_back({d.x * d.x + d.y * d.y + d.z * d.z, static_cast<int32>(i)});
    }
    const std::size_t k = std::min(tries, near.size());
    std::partial_sort(near.begin(), near.begin() + static_cast<std::ptrdiff_t>(k), near.end());
    for (std::size_t i = 0; i < k; ++i)
        if (!clear || clear(nodes_[static_cast<std::size_t>(near[i].second)].position))
            return near[i].second;
    return nearest(p, maxRise);
}

std::vector<int32> NavGraph::route(int32 from, int32 to, bool closest) const
{
    const std::size_t n = nodes_.size();
    if (from < 0 || to < 0 || static_cast<std::size_t>(from) >= n || static_cast<std::size_t>(to) >= n)
        return {};
    if (from == to)
        return {from};
    constexpr uint32 kUnseen = 0xffffffffu;
    std::vector<uint32> hops(n, kUnseen);
    std::vector<int32> fifo{from};
    hops[static_cast<std::size_t>(from)] = 0;
    const Math::Vector3 goal = nodes_[static_cast<std::size_t>(to)].position;
    int32 nearestGoal = from; // the reached node closest to the goal (ours, when it cannot be reached)
    float32 nearestGoalD = (goal - nodes_[static_cast<std::size_t>(from)].position).length();
    bool reached = false;
    for (std::size_t head = 0; head < fifo.size() && !reached; ++head)
    {
        const int32 cur = fifo[head];
        const Node& c = nodes_[static_cast<std::size_t>(cur)];
        const uint32 next = hops[static_cast<std::size_t>(cur)] + 1;
        for (std::size_t k = 0; k < c.links.size(); ++k)
        {
            const int32 nb = c.links[k];
            if (!c.linkOpen[k] || next >= hops[static_cast<std::size_t>(nb)])
                continue;
            hops[static_cast<std::size_t>(nb)] = next;
            if (nb == to)
            {
                reached = true;
                break;
            }
            if (const float32 d = (goal - nodes_[static_cast<std::size_t>(nb)].position).length(); d < nearestGoalD)
            {
                nearestGoalD = d;
                nearestGoal = nb;
            }
            fifo.push_back(nb);
        }
    }
    int32 end = to;
    if (!reached)
    {
        if (!closest)
            return {};
        end = nearestGoal;
    }
    // Back from the end: the first of its links to a node one hop nearer, the link from there open.
    std::vector<int32> out{end};
    for (int32 cur = end; cur != from;)
    {
        const Node& c = nodes_[static_cast<std::size_t>(cur)];
        int32 back = -1;
        for (const int32 nb : c.links)
        {
            if (hops[static_cast<std::size_t>(nb)] == kUnseen || hops[static_cast<std::size_t>(nb)] + 1 != hops[static_cast<std::size_t>(cur)])
                continue;
            const Node& b = nodes_[static_cast<std::size_t>(nb)];
            for (std::size_t k = 0; k < b.links.size(); ++k)
                if (b.links[k] == cur && b.linkOpen[k])
                {
                    back = nb;
                    break;
                }
            if (back >= 0)
                break;
        }
        if (back < 0)
            return {};
        out.push_back(back);
        cur = back;
    }
    std::reverse(out.begin(), out.end());
    return out;
}

std::vector<std::pair<int32, int32>> NavGraph::ranked(int32 from, const Math::Vector3& centre, float32 radius,
                                                     std::size_t cap) const
{
    std::vector<std::pair<int32, int32>> out;
    if (from < 0 || static_cast<std::size_t>(from) >= nodes_.size() || cap == 0)
        return out;
    std::vector<bool> seen(nodes_.size(), false);
    seen[static_cast<std::size_t>(from)] = true;
    out.emplace_back(from, 0);
    for (std::size_t i = 0; i < out.size(); ++i)
    {
        const auto [cur, depth] = out[i];
        const Node& c = nodes_[static_cast<std::size_t>(cur)];
        for (std::size_t k = 0; k < c.links.size() && out.size() < cap; ++k)
        {
            const int32 nb = c.links[k];
            if (!c.linkOpen[k] || seen[static_cast<std::size_t>(nb)])
                continue;
            seen[static_cast<std::size_t>(nb)] = true; // (marked once looked at, near or not)
            if ((nodes_[static_cast<std::size_t>(nb)].position - centre).length() < radius)
                out.emplace_back(nb, depth + 1);
        }
    }
    return out;
}

std::vector<int32> NavGraph::within(int32 around, float32 radius) const
{
    std::vector<int32> out;
    if (around < 0 || static_cast<std::size_t>(around) >= nodes_.size())
        return out;
    const Math::Vector3 centre = nodes_[static_cast<std::size_t>(around)].position;
    std::vector<bool> seen(nodes_.size(), false);
    std::vector<int32> stack{around};
    seen[static_cast<std::size_t>(around)] = true;
    while (!stack.empty())
    {
        const int32 cur = stack.back();
        stack.pop_back();
        out.push_back(cur);
        const Node& c = nodes_[static_cast<std::size_t>(cur)];
        for (std::size_t k = 0; k < c.links.size(); ++k)
            if (const int32 nb = c.links[k];
                c.linkOpen[k] && !seen[static_cast<std::size_t>(nb)] && (nodes_[static_cast<std::size_t>(nb)].position - centre).length() <= radius)
            {
                seen[static_cast<std::size_t>(nb)] = true;
                stack.push_back(nb);
            }
    }
    return out;
}

std::size_t NavGraph::setLinksNear(const Math::Vector3& centre, float32 reach, bool open)
{
    std::size_t covered = 0;
    for (Node& n : nodes_)
        for (std::size_t k = 0; k < n.links.size(); ++k)
        {
            const Math::Vector3& a = n.position;
            const Math::Vector3& b = nodes_[static_cast<std::size_t>(n.links[k])].position;
            if (std::fabs((a.y + b.y) * 0.5f - centre.y) > 3.0f)
                continue;
            // Horizontal distance from the centre to the segment a-b.
            const float32 dx = b.x - a.x, dz = b.z - a.z;
            const float32 len2 = dx * dx + dz * dz;
            const float32 t = len2 > 1e-8f ? std::clamp(((centre.x - a.x) * dx + (centre.z - a.z) * dz) / len2, 0.0f, 1.0f) : 0.0f;
            const float32 px = a.x + dx * t - centre.x, pz = a.z + dz * t - centre.z;
            if (px * px + pz * pz > reach * reach)
                continue;
            n.linkOpen[k] = open ? 1 : 0;
            ++covered;
        }
    return covered;
}

std::size_t NavGraph::closedLinks() const
{
    std::size_t closed = 0;
    for (const Node& n : nodes_)
        closed += static_cast<std::size_t>(std::count(n.linkOpen.begin(), n.linkOpen.end(), uint8{0}));
    return closed;
}

} // namespace Game
