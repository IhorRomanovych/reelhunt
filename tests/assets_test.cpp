#include "Assets/txd.h"
#include "Assets/dff.h"
#include "Assets/inst.h"
#include "Assets/world.h"
#include "Assets/col.h"
#include "Assets/ifp/ifp.h"
#include "Assets/librw_engine.h"
#include "Assets/mls.h"
#include "Assets/sfx_bank.h"
#include "Math/matrix.h"
#include "Math/quaternion.h"

#include "rw.h"  // librw: direct Skin/HAnimHierarchy inspection for the CHARPAK diagnostic

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "test_paths.h"

int main() {
    if (!TestPaths::haveGameData())
        return TestPaths::skipNoGameData();
    std::cerr << "assets_test: start\n";

    const std::string base =
        TestPaths::gameRoot() + "levels/asylum/";

    // ---- CHARPAK skeletal diagnostic: which character dffs carry Skin + HAnimHierarchy? ----
    {
        Assets::ensureLibrwEngine();  // registers skin+hanim plugins (see librw_engine.cpp)
        const std::string charpak =
            TestPaths::gameRoot() + "levels/GLOBAL/CHARPAK/";
        const char* names[] = {"hel_pc.dff", "pig_pc.dff", "cash_pc.dff", "bun_pc.dff", "trmp_pc.dff"};
        for (const char* nm : names) {
            rw::uint32 len = 0;
            rw::uint8* data = rw::getFileContents((charpak + nm).c_str(), &len);
            if (!data) { std::cerr << "charpak " << nm << ": OPEN FAIL\n"; continue; }
            rw::StreamMemory in;
            in.open(data, len);
            int clumps = 0, skinnedAtomics = 0, maxBones = 0, totalSkinnedVerts = 0, hierNodes = -1, numWeights = -1;
            const bool detail = (std::string(nm) == "bun_pc.dff");
            while (rw::findChunk(&in, rw::ID_CLUMP, nullptr, nullptr)) {
                rw::Clump* c = rw::Clump::streamRead(&in);
                if (!c) break;
                ++clumps;
                if (hierNodes < 0) {
                    if (rw::HAnimHierarchy* h = rw::HAnimHierarchy::get(c)) hierNodes = h->numNodes;
                }
                FORLIST(lnk, c->atomics) {
                    rw::Atomic* a = rw::Atomic::fromClump(lnk);
                    if (!a || !a->geometry) continue;
                    if (rw::Skin* s = rw::Skin::get(a->geometry)) {
                        if (s && s->numBones > 0) {
                            ++skinnedAtomics;
                            maxBones = std::max(maxBones, s->numBones);
                            totalSkinnedVerts += a->geometry->numVertices;
                            if (numWeights < 0) numWeights = s->numWeights;
                            if (detail && s->indices && s->weights) {
                                for (int v = 0; v < 3 && v < a->geometry->numVertices; ++v) {
                                    const uint8* idx = s->indices + (size_t)v * 4;
                                    const float* w = s->weights + (size_t)v * s->numWeights;
                                    std::cerr << "   [" << nm << "] vert" << v << " idx=["
                                              << idx[0] << "," << idx[1] << "," << idx[2] << "," << idx[3]
                                              << "] w=[" << w[0];
                                    for (int k = 1; k < s->numWeights; ++k) std::cerr << "," << w[k];
                                    std::cerr << "]\n";
                                }
                            }
                        }
                    }
                }
                c->destroy();
            }
            in.close();
            rwFree(data);
            std::cerr << "charpak " << nm << ": clumps=" << clumps
                      << " skinnedAtomics=" << skinnedAtomics
                      << " maxBones=" << maxBones
                      << " numWeights=" << numWeights
                      << " skinnedVerts=" << totalSkinnedVerts
                        << " hierNodes=" << hierNodes << "\n";
        }
    }

    // ---- CHARPAK x asylum-allanims cross-match: which character skeleton do the level's animations drive? ----
    {
        const std::string base = TestPaths::gameRoot() + "levels/asylum/";
        const std::string charpak = TestPaths::gameRoot() + "levels/GLOBAL/CHARPAK/";
        auto loadIfp = [](const std::string& p) {
            struct A { std::string name; std::unordered_set<int16> ids; };
            std::vector<A> out;
            std::FILE* f = std::fopen(p.c_str(), "rb");
            if (!f) return out;
            std::fseek(f, 0, SEEK_END); long len = std::ftell(f); std::fseek(f, 0, SEEK_SET);
            std::vector<uint8> buf(len > 0 ? (size_t)len : 0u);
            if (len > 0) buf.resize(std::fread(buf.data(), 1, buf.size(), f));
            std::fclose(f);
            auto r = Assets::parseIfp(buf.data(), (uint32)buf.size());
            if (!r) return out;
            for (const auto& bl : r->blocks)
                for (const auto& an : bl.animations) {
                    A a; a.name = an.name;
                    for (const auto& bt : an.bones) a.ids.insert(bt.boneId);
                    out.push_back(std::move(a));
                }
            return out;
        };
        auto anims = loadIfp(base + "allanims.ifp");
        std::cerr << "\n=== CHARPAK x asylum allanims cross-match (" << anims.size() << " animations) ===\n";
        const char* names[] = {"bun_pc", "cash_pc", "hel_pc", "pig_pc", "trmp_pc"};
        for (const char* nm : names) {
            auto dff = Assets::Dff::load(charpak + nm + ".dff");
            if (!dff || dff->empty()) { std::cerr << nm << ": LOAD FAIL\n"; continue; }
            const auto& c = dff->at(0);
            int N = (int)c.bones.size();
            std::unordered_set<int16> idSet;
            for (const auto& b : c.bones) idSet.insert((int16)b.id);
            struct M { const char* name; int ov; int T; };
            std::vector<M> ranked;
            for (auto& [aname, ids] : anims) {
                int ov = 0; for (int16 id : idSet) if (ids.count(id)) ++ov;
                ranked.push_back({aname.c_str(), ov, (int)ids.size()});
            }
            std::sort(ranked.begin(), ranked.end(), [](const M& a, const M& b){ return a.ov > b.ov; });
            int topN = (std::string(nm) == "bun_pc") ? 10 : 3;
            std::cerr << "\n" << nm << "  (skeleton bones=" << N << ") top matches:\n";
            for (int k = 0; k < topN && k < (int)ranked.size(); ++k) {
                const auto& m = ranked[k];
                int recall = N ? 100 * m.ov / N : 0;   // % of this model's bones the anim drives
                int prec   = m.T ? 100 * m.ov / m.T : 0; // % of the anim's tracks that belong to this model
                std::cerr << "   ov=" << m.ov << "/" << N
                          << "  recall=" << recall << "%  precision=" << prec << "%"
                          << "  anim='" << m.name << "' (tracks=" << m.T << ")\n";
            }
            if (std::string(nm) == "bun_pc") {
                for (auto& [aname, ids] : anims)
                    if (aname.find("Idle_Action_Idle_01") != std::string::npos) {
                        int ov = 0; for (int16 id : idSet) if (ids.count(id)) ++ov;
                        int recall = N ? 100 * ov / N : 0, prec = (int)ids.size() ? 100 * ov / (int)ids.size() : 0;
                        std::cerr << "   >>> CURRENTLY PLAYING '" << aname << "': ov=" << ov << "/" << N
                                  << " recall=" << recall << "% precision=" << prec << "%\n";
                    }
            }
        }
    }

    // ---- DFF loader skeleton diagnostic: verify skin+skeleton extraction via our own loader ----
    {
        const std::string bun =
            TestPaths::gameRoot() + "levels/GLOBAL/CHARPAK/bun_pc.dff";
        auto dff = Assets::Dff::load(bun);
        if (!dff) {
            std::cerr << "dffloader bun_pc: LOAD FAIL\n";
        } else {
            const auto& clumps = *dff;
            std::cerr << "dffloader bun_pc: clumps=" << clumps.size() << "\n";
            for (size_t ci = 0; ci < clumps.size(); ++ci) {
                const auto& c = clumps[ci];
                std::cerr << "  clump" << ci << ": atomics=" << c.atomics.size()
                          << " bones=" << c.bones.size()
                          << " localSpace=" << (c.localSpaceMatrices ? 1 : 0) << "\n";
                for (const auto& a : c.atomics) {
                    std::cerr << "    atomic: verts=" << a.vertices.size()
                              << " skinned=" << (a.skinned ? 1 : 0)
                              << " numBones=" << a.numBones
                              << " idxArr=" << a.boneIndices.size()
                              << " wArr=" << a.weights.size();
                    if (a.skinned && !a.boneIndices.empty()) {
                        const auto& i0 = a.boneIndices[0];
                        const auto& w0 = a.weights[0];
                        std::cerr << " v0 idx=[" << (int)i0[0] << "," << (int)i0[1] << ","
                                  << (int)i0[2] << "," << (int)i0[3]
                                  << "] w=[" << w0[0] << "," << w0[1] << "," << w0[2] << "," << w0[3] << "]";
                    }
                    std::cerr << "\n";
                }
                int show = c.bones.size() < 14 ? (int)c.bones.size() : 14;
                for (int i = 0; i < show; ++i) {
                    const auto& b = c.bones[i];
                    std::cerr << "    bone" << i << " id=" << b.id
                              << " flags=0x" << std::hex << b.nodeFlags << std::dec
                              << " localT=(" << b.localRest[12] << "," << b.localRest[13] << "," << b.localRest[14] << ")"
                              << " worldT=(" << b.restLocal[12] << "," << b.restLocal[13] << "," << b.restLocal[14] << ")"
                              << " invT=(" << b.inverseBind[12] << "," << b.inverseBind[13] << "," << b.inverseBind[14] << ")\n";
                }
            }
        }
    }

    // ---- scene1.bsp (RWWorld octree) format validation probe ----
    Assets::World::dump(base + "scene1.bsp");

    // ---- scene2.bsp: the decal layer (Jury_Turf: court markings, blood, signs, painted shadows) ----
    {
        auto decals = Assets::World::load(TestPaths::gameRoot() + "levels/jury_turf/scene2.bsp");
        if (!decals) { std::cerr << "scene2: LOAD FAIL " << decals.error().message << "\n"; return 1; }
        const auto& names = decals->materialNames;
        const bool court = std::find(names.begin(), names.end(), "Bow_BB_Court") != names.end();
        if (decals->totalTris != 326 || !court || decals->layoutInvariantOk != decals->leafCount)
        {
            std::cerr << "scene2: tris=" << decals->totalTris << " court=" << court << " (expected 326 and the court markings)\n";
            return 1;
        }
        std::cerr << "scene2: decal layer ok (" << decals->totalTris << " tris, " << names.size() << " materials)\n";
    }


    // ---- modelspc.txd (named textures) ----
    auto txd = Assets::Txd::loadAndBind(base + "pak/modelspc.txd");
    std::unordered_set<std::string> txdNames;
    if (txd.has_value()) {
        std::cerr << "txd textures=" << txd->size() << "\n";
        for (size_t i = 0; i < txd->size(); ++i) {
            const auto& t = txd->at(i);
            txdNames.insert(t.name);
            if (i < 40)
                std::cerr << "  tex[" << i << "] '" << t.name << "' " << t.width << "x"
                          << t.height << " mips=" << t.mipLevels << " fmt=0x" << std::hex
                          << t.format << std::dec << "\n";
        }
    } else {
        std::cerr << "txd failed " << txd.error().message << "\n";
    }

    // ---- modelspc.dff ----
    auto dff = Assets::Dff::load(base + "pak/modelspc.dff");
    if (!dff.has_value()) {
        std::cerr << "dff failed " << dff.error().message << "\n";
        return 1;
    }
    std::cerr << "dff clumps=" << dff->size() << "\n";

    std::set<std::string> usedNames;
    std::unordered_map<std::string, std::vector<size_t>> texToClumps;
    int bestClump = -1;
    int bestMat = -1;

    for (size_t c = 0; c < dff->size(); ++c) {
        const auto& clump = dff->at(c);
        size_t totalMats = 0;
        size_t totalVerts = 0;
        float mn[3] = {1e9f, 1e9f, 1e9f};
        float mx[3] = {-1e9f, -1e9f, -1e9f};
        bool any = false;
        for (const auto& a : clump.atomics) {
            totalMats += a.materials.size();
            totalVerts += a.vertices.size();
            for (const auto& m : a.materials) {
                usedNames.insert(m.textureName);
                if (!m.textureName.empty())
                    texToClumps[m.textureName].push_back(c);
            }
            for (const auto& v : a.vertices) {
                const float p[3] = {v.position.x, v.position.y, v.position.z};
                if (!any) {
                    for (int k = 0; k < 3; ++k) {
                        mn[k] = p[k];
                        mx[k] = p[k];
                    }
                    any = true;
                }
                for (int k = 0; k < 3; ++k) {
                    if (p[k] < mn[k]) mn[k] = p[k];
                    if (p[k] > mx[k]) mx[k] = p[k];
                }
            }
        }
        if (static_cast<int>(totalMats) > bestMat) {
            bestMat = static_cast<int>(totalMats);
            bestClump = static_cast<int>(c);
        }
        std::cerr << "clump[" << c << "] atoms=" << clump.atomics.size()
                  << " mats=" << totalMats << " verts=" << totalVerts
                  << " ext=";
        if (any)
            std::cerr << (mx[0] - mn[0]) << "x" << (mx[1] - mn[1]) << "x" << (mx[2] - mn[2]);
        else
            std::cerr << "-";
        std::cerr << "\n";
        static const uint32 kDemo[] = {6u, 7u, 12u, 26u, 30u};
        bool isDemo = false;
        for (uint32 d : kDemo)
            if (d == c) { isDemo = true; break; }
        if (isDemo && any) {
            std::cerr << "    extents x=" << (mx[0] - mn[0])
                      << " y=" << (mx[1] - mn[1])
                      << " z=" << (mx[2] - mn[2]) << "\n";
            for (const auto& a : clump.atomics) {
                for (size_t mi = 0; mi < a.materials.size(); ++mi) {
                    const auto& m = a.materials[mi];
                    bool found = txdNames.count(m.textureName) > 0;
                    std::cerr << "    mat[" << mi << "] '" << m.textureName << "' "
                              << (found ? "FOUND" : "MISSING") << "\n";
                }
            }
        }
    }

    size_t foundCount = 0;
    for (const auto& n : usedNames)
        if (txdNames.count(n) > 0)
            ++foundCount;
    std::cerr << "global: distinct material names=" << usedNames.size()
              << " found-in-txd=" << foundCount
              << " | busiest clump=" << bestClump << " (mats=" << bestMat << ")\n";

    // ---- entity.inst (placements) ----
    {
        auto inst = Assets::Inst::load(base + "entity.inst");
        if (inst.has_value()) {
            std::cerr << "inst placements=" << inst->size() << "\n";
            std::unordered_map<std::string, int> classCount;
            float mn[3] = {1e9f, 1e9f, 1e9f};
            float mx[3] = {-1e9f, -1e9f, -1e9f};
            for (const auto& p : *inst) {
                classCount[p.entityClass]++;
                const float pos[3] = {p.position.x, p.position.y, p.position.z};
                for (int k = 0; k < 3; ++k) {
                    const float v = pos[k];
                    if (v < mn[k]) mn[k] = v;
                    if (v > mx[k]) mx[k] = v;
                }
            }
            for (size_t i = 0; i < inst->size() && i < 6; ++i) {
                const auto& p = inst->at(i);
                std::cerr << "  [" << i << "] rec='" << p.record << "' int='" << p.internalName
                          << "' class='" << p.entityClass << "' pos=(" << p.position.x << ","
                          << p.position.y << "," << p.position.z << ") params=" << p.params.size();
                for (size_t j = 0; j < p.params.size() && j < 8; ++j)
                    std::cerr << " " << p.params[j];
                std::cerr << "\n";
            }
            std::cerr << "  class histogram:";
            for (const auto& [k, v] : classCount)
                std::cerr << " " << k << "=" << v;
            std::cerr << "\n  pos range x=" << mn[0] << ".." << mx[0]
                      << " y=" << mn[1] << ".." << mx[1]
                      << " z=" << mn[2] << ".." << mx[2] << "\n";
            // Diagnostic: for the sampled placements used by main.cpp, print the
            // rotation quaternion and where the level up-axis (+Z) ends up. A
            // properly-placed prop keeps +Z near (0,0,1) (upright) with only a yaw.
            for (size_t i = 0; i < inst->size(); i += 13) {
                const auto& p = inst->at(i);
                const Math::Quaternion q{p.rotation.w, p.rotation.x, p.rotation.y, p.rotation.z};
                const auto M = q.normalized().toMatrix();
                const Math::Vector3 up{M.m02, M.m12, M.m22};  // column 3 = image of +Z
                std::cerr << "  rot[" << i << "] '" << p.record << "' q=(" << p.rotation.x
                          << "," << p.rotation.y << "," << p.rotation.z << "," << p.rotation.w
                          << ") len=" << q.length() << " upZ=(" << up.x << "," << up.y << ","
                          << up.z << ")\n";
            }
        } else {
            std::cerr << "inst failed " << inst.error().message << "\n";
        }
    }

    // ---- entity->model mapping viability: .inst internalName vs DFF texture names ----
    {
        auto i1 = Assets::Inst::load(base + "entity.inst");
        auto i2 = Assets::Inst::load(base + "entity2.inst");
        std::unordered_set<std::string> intNames;
        size_t totalPlacements = 0;
        for (const auto& src : {i1, i2}) {
            if (!src.has_value()) continue;
            totalPlacements += src->size();
            for (const auto& p : *src)
                intNames.insert(p.internalName);
        }
        std::cerr << "mapping: placements=" << totalPlacements
                  << " distinct internalNames=" << intNames.size()
                  << " dff texture names=" << texToClumps.size() << "\n";
        size_t uniq = 0, none = 0, multi = 0;
        for (const auto& n : intNames) {
            auto it = texToClumps.find(n);
            if (it == texToClumps.end()) { ++none; continue; }
            if (it->second.size() == 1) ++uniq; else ++multi;
        }
        std::cerr << "mapping: internalName -> exactly-1 clump=" << uniq
                  << " no-match=" << none << " multi-clump=" << multi << "\n";
        {
            std::vector<std::string> sorted(intNames.begin(), intNames.end());
            std::sort(sorted.begin(), sorted.end());
            std::cerr << "  sample internalNames (" << sorted.size() << "):";
            for (size_t k = 0; k < sorted.size() && k < 30; ++k)
                std::cerr << "\n    '" << sorted[k] << "'";
            std::cerr << "\n";
        }
        {
            std::vector<std::string> tnames;
            for (const auto& [n, v] : texToClumps) tnames.push_back(n);
            std::sort(tnames.begin(), tnames.end());
            std::cerr << "  dff texture names (" << tnames.size() << "):";
            for (size_t k = 0; k < tnames.size(); ++k) {
                if (k % 4 == 0) std::cerr << "\n    ";
                std::cerr << "'" << tnames[k] << "'";
            }
            std::cerr << "\n";
        }
    }

    // ---- collisions.col (collision archive) format validation + placement match probe ----
    {
        auto col = Assets::Col::load(base + "collisions.col");
        if (!col.has_value()) {
            std::cerr << "col failed " << col.error().message << "\n";
        } else {
            const auto& c = *col;
            std::cerr << "col entries=" << c.entries.size()
                      << " spheres=" << c.totalSpheres
                      << " lines=" << c.totalLines
                      << " boxes=" << c.totalBoxes
                      << " verticals=" << c.totalVerticals
                      << " faces=" << c.totalFaces << "\n";
            for (size_t i = 0; i < c.entries.size() && i < 8; ++i) {
                const auto& e = c.entries.at(i);
                std::cerr << "  [" << i << "] '" << e.name << "' r=" << e.radius
                          << " sph=" << e.spheres.size() << " line=" << e.lines.size()
                          << " vert=" << e.verticals.size() << " face=" << e.faces.size() << "\n";
            }
            // Cross-reference: how many .inst placements have a matching collision entry?
            // Placements carry an instance suffix of 1-3 digits appended to the base model
            // name (no underscore): entry "CJ_AIRCON" -> placements "CJ_AIRCON01"..". Try
            // exact match first, then strip k trailing digits for k in [1..3].
            auto i1 = Assets::Inst::load(base + "entity.inst");
            auto i2 = Assets::Inst::load(base + "entity2.inst");
            size_t exactMatched = 0, suffixMatched = 0, unmatched = 0;
            std::unordered_set<std::string> matchedBases;
            for (const auto& src : {i1, i2}) {
                if (!src.has_value()) continue;
                for (const auto& p : *src) {
                    const std::string& nm = p.internalName;
                    if (c.find(nm)) { ++exactMatched; matchedBases.insert(nm); continue; }
                    bool hit = false;
                    for (int k = 1; k <= 3 && !hit; ++k) {
                        if (static_cast<int>(nm.size()) < k + 1) break;
                        bool allDigits = true;
                        for (size_t j = nm.size() - k; j < nm.size(); ++j)
                            if (nm[j] < '0' || nm[j] > '9') { allDigits = false; break; }
                        if (!allDigits) continue;
                        const std::string baseName = nm.substr(0, nm.size() - k);
                        if (c.find(baseName)) { ++suffixMatched; matchedBases.insert(baseName); hit = true; }
                    }
                    if (!hit) ++unmatched;
                }
            }
            std::cerr << "col<->inst: exact=" << exactMatched
                      << " suffix=" << suffixMatched
                      << " unmatched=" << unmatched
                      << " distinct matched bases=" << matchedBases.size() << "\n";
        }
    }

    // ---- allanims.ifp parse + structure dump (Phase 6 animation source) ----
    {
        const std::string ifpPath = base + "allanims.ifp";
        std::FILE* f = std::fopen(ifpPath.c_str(), "rb");
        if (!f) {
            std::cerr << "ifp: OPEN FAIL " << ifpPath << "\n";
        } else {
            std::fseek(f, 0, SEEK_END); long len = std::ftell(f); std::fseek(f, 0, SEEK_SET);
            std::vector<uint8> buf(len > 0 ? static_cast<size_t>(len) : 0u);
            if (len > 0) buf.resize(std::fread(buf.data(), 1, buf.size(), f));
            std::fclose(f);
            auto res = Assets::parseIfp(buf.data(), static_cast<uint32>(buf.size()));
            if (!res) {
                std::cerr << "ifp: PARSE FAIL " << res.error().message << "\n";
            } else {
                const auto& ifp = *res;
                std::cerr << "ifp OK blocks=" << ifp.numBlocks()
                          << " anims=" << ifp.totalAnimations()
                          << " bones=" << ifp.totalBones() << "\n";
                for (size_t bi = 0; bi < ifp.blocks.size(); ++bi) {
                    const auto& bl = ifp.blocks[bi];
                    std::cerr << "  block[" << bi << "] '" << bl.name << "' anims=" << bl.numAnimations() << "\n";
                    for (size_t ai = 0; ai < bl.animations.size() && ai < 12; ++ai) {
                        const auto& an = bl.animations[ai];
                        uint32 totalFrames = 0u; int withPos = 0;
                        for (const auto& b : an.bones) {
                            totalFrames += static_cast<uint32>(b.frames.size());
                            if (b.frameType > 1) ++withPos;
                        }
                        std::cerr << "    '" << an.name << "' dur=" << an.duration
                                  << "s bones=" << an.bones.size()
                                  << " frames=" << totalFrames
                                  << " posBones=" << withPos << "\n";
                    }
                }
            }
        }
    }

    // ---- bun_pc skeleton + IFP convention diagnostic (Phase 6) ----
    {
        const std::string charPath =
            TestPaths::gameRoot() + "levels/GLOBAL/CHARPAK/bun_pc.dff";
        auto clumps = Assets::Dff::load(charPath);
        if (!clumps.has_value() || clumps->empty()) {
            std::cerr << "diag: bun_pc load failed\n";
        } else {
            const auto& c = clumps->at(0);
            // --- texture name diagnostic (Phase 6 #3) ---
            std::cerr << "diag: bun_pc atomics=" << c.atomics.size() << "\n";
            for (size_t ai = 0; ai < c.atomics.size(); ++ai) {
                const auto& at = c.atomics[ai];
                std::cerr << "  atomic[" << ai << "] mats=" << at.materials.size() << " tris=" << at.triangles.size() << "\n";
                for (size_t mi = 0; mi < at.materials.size(); ++mi) {
                    const auto& mn = at.materials[mi].textureName;
                    std::cerr << "    mat[" << mi << "] name='" << (mn.empty() ? "(empty)" : mn.c_str()) << "'\n";
                }
            }
            auto ctdx = Assets::Txd::load(
                TestPaths::gameRoot() + "levels/GLOBAL/CHARPAK/bun_pc.txd");
            if (ctdx.has_value()) {
                std::cerr << "diag: bun_pc.txd entries=" << ctdx->size() << "\n";
                for (const auto& e : *ctdx) {
                    float mn = 1e9f, mx = -1e9f, sum = 0.0f; size_t cnt = 0;
                    for (std::size_t i = 0; i + 3 < e.rgba.size(); i += 4) {
                        const float v = (e.rgba[i] + e.rgba[i + 1] + e.rgba[i + 2]) / (3.0f * 255.0f);
                        mn = std::min(mn, v); mx = std::max(mx, v); sum += v; ++cnt;
                    }
                    std::cerr << "    tex '" << e.name << "' " << e.width << "x" << e.height
                              << " px=" << cnt << " lum[min=" << mn << " max=" << mx
                              << " avg=" << (cnt ? sum / static_cast<float>(cnt) : 0.0f) << "]\n";
                }
            } else {
                std::cerr << "diag: bun_pc.txd load failed\n";
            }
            // per-material geometry + UV diagnostics: determine which material is head vs body
            for (size_t ai = 0; ai < c.atomics.size(); ++ai) {
                const auto& at = c.atomics[ai];
                std::unordered_map<uint32, std::vector<size_t>> byMat;
                for (size_t ti = 0; ti < at.triangles.size(); ++ti)
                    byMat[at.triangles[ti].materialIndex].push_back(ti);
                for (auto& [mi, tris] : byMat) {
                    float mnx=1e9f,mny=1e9f,mnz=1e9f,mxx=-1e9f,mxy=-1e9f,mxz=-1e9f;
                    float uvx0=1e9f,uvy0=1e9f,uvx1=-1e9f,uvy1=-1e9f; size_t vcnt=0;
                    for (size_t ti : tris) {
                        const auto& t = at.triangles[ti];
                        for (uint32 vi : {t.a,t.b,t.c}) {
                            const auto& v = at.vertices[vi];
                            mnx=std::min(mnx,v.position.x); mny=std::min(mny,v.position.y); mnz=std::min(mnz,v.position.z);
                            mxx=std::max(mxx,v.position.x); mxy=std::max(mxy,v.position.y); mxz=std::max(mxz,v.position.z);
                            uvx0=std::min(uvx0,v.uv.x); uvy0=std::min(uvy0,v.uv.y);
                            uvx1=std::max(uvx1,v.uv.x); uvy1=std::max(uvy1,v.uv.y); ++vcnt;
                        }
                    }
                    std::cerr << "  mat["<<mi<<"] vrefs="<<vcnt
                              <<" pos[min=("<<mnx<<","<<mny<<","<<mnz<<") max=("<<mxx<<","<<mxy<<","<<mxz<<")]"
                              <<" uv[x="<<uvx0<<".."<<uvx1<<" y="<<uvy0<<".."<<uvy1<<"]\n";
                }
            }
            int32 n = static_cast<int32>(c.bones.size());
            auto fromCM = [](const float* cm) {
                Math::Matrix4x4 m;
                m.m00 = cm[0];  m.m10 = cm[1];  m.m20 = cm[2];  m.m30 = cm[3];
                m.m01 = cm[4];  m.m11 = cm[5];  m.m21 = cm[6];  m.m31 = cm[7];
                m.m02 = cm[8];  m.m12 = cm[9];  m.m22 = cm[10]; m.m32 = cm[11];
                m.m03 = cm[12]; m.m13 = cm[13]; m.m23 = cm[14]; m.m33 = cm[15];
                return m;
            };
            auto posStr = [](const Math::Matrix4x4& m) {
                char buf[64]; std::snprintf(buf, sizeof(buf), "(%.3f,%.3f,%.3f)", m.m03, m.m13, m.m23);
                return std::string(buf);
            };
            // W_rest[i] = inv(invBind[i])  (reliable bind-world from Skin data)
            std::vector<Math::Matrix4x4> wRest(n), lRest(n);
            for (int32 i = 0; i < n; ++i) wRest[i] = fromCM(c.bones[i].inverseBind).inverse();

            // UP-AXIS diagnostic: bounding box of bind-pose bone positions. The axis with the
            // largest span is the model's "up". Also print head/foot extremes by index so we can
            // pick a correct upright orientation for placement (fixes tilt/facing symptom).
            {
                float mn[3] = {1e9f, 1e9f, 1e9f}, mx[3] = {-1e9f, -1e9f, -1e9f};
                for (int32 i = 0; i < n; ++i) {
                    float px = wRest[i].m03, py = wRest[i].m13, pz = wRest[i].m23;
                    mn[0] = std::min(mn[0], px); mx[0] = std::max(mx[0], px);
                    mn[1] = std::min(mn[1], py); mx[1] = std::max(mx[1], py);
                    mn[2] = std::min(mn[2], pz); mx[2] = std::max(mx[2], pz);
                }
                printf("  UPAXIS bbox: X[%.3f..%.3f span=%.3f] Y[%.3f..%.3f span=%.3f] Z[%.3f..%.3f span=%.3f]\n",
                       mn[0], mx[0], mx[0]-mn[0], mn[1], mx[1], mx[1]-mn[1], mn[2], mx[2], mx[2]-mn[2]);
                // print a few bones' bind positions (root + spread) to eyeball head vs feet
                for (int32 i : {0, 1, 2, n/4, n/2, 3*n/4, n-1}) {
                    if (i >= 0 && i < n)
                        printf("    bone[%d] bindPos=(%.3f, %.3f, %.3f)\n", i, wRest[i].m03, wRest[i].m13, wRest[i].m23);
                }
            }

            // parent chain via PUSH/POP stack walk (mirrors HAnimHierarchy::updateMatrices)
            std::vector<int32> parent(n, -1);
            std::vector<int32> stk; stk.push_back(-1);
            int32 cur = -1;
            int totalPush = 0, totalPop = 0;
            for (int32 i = 0; i < n; ++i) {
                parent[i] = cur;
                if (c.bones[i].nodeFlags & 0x2u) { stk.push_back(cur); ++totalPush; }   // PUSH
                cur = i;
                if (c.bones[i].nodeFlags & 0x1u) {
                    ++totalPop;
                    if (stk.size() > 1) { stk.pop_back(); cur = stk.back(); }
                    else { stk.clear(); stk.push_back(-1); cur = -1; }   // guard underflow
                }
            }
            std::cerr << "diag flags: push=" << totalPush << " pop=" << totalPop
                      << " (balanced if pop<=push+1)\n";
            for (int32 i = 0; i < n; ++i) {
                int32 p = parent[i];
                lRest[i] = (p >= 0) ? wRest[p].inverse() * wRest[i] : wRest[i];
            }
            // compare derived lRest vs DFF's authoritative localRest (fi->matrix)
            std::vector<Math::Matrix4x4> lRestDff(n);
            for (int32 i = 0; i < n; ++i) lRestDff[i] = fromCM(c.bones[i].localRest);
            {
                float32 maxPosDiff = 0, maxRotDeg = 0; int worstBone = -1;
                for (int32 i = 0; i < n; ++i) {
                    Math::Vector3 dp{lRestDff[i].m03-lRest[i].m03, lRestDff[i].m13-lRest[i].m13, lRestDff[i].m23-lRest[i].m23};
                    float32 pd = std::sqrt(dp.x*dp.x+dp.y*dp.y+dp.z*dp.z);
                    Math::Matrix4x4 D = lRest[i].inverse() * lRestDff[i];   // rotation diff (both pure-ish)
                    float32 tr=(D.m00+D.m11+D.m22); float32 ca=Math::clamp((tr-1.0f)*0.5f,-1.0f,1.0f);
                    float32 deg = Math::degrees(std::acos(ca));
                    if (pd > maxPosDiff) { maxPosDiff = pd; }
                    if (deg > maxRotDeg) { maxRotDeg = deg; worstBone = i; }
                }
                std::cerr << "diag lRest derived-vs-DFF: maxPosDiff=" << maxPosDiff
                          << " maxRotDeg=" << maxRotDeg << " (worst b" << worstBone << ")\n";
            }
            // consistency: compose DFF localRest through hierarchy -> wRestDff, check wRestDff*invBind ~ identity
            {
                std::vector<Math::Matrix4x4> wD(n);
                std::vector<Math::Matrix4x4> st; Math::Matrix4x4 cp = Math::Matrix4x4::identity(); st.push_back(cp);
                for (int32 i = 0; i < n; ++i) {
                    wD[i] = cp * lRestDff[i];
                    if (c.bones[i].nodeFlags & 0x2u) st.push_back(cp);
                    cp = wD[i];
                    if ((c.bones[i].nodeFlags & 0x1u) && st.size() > 1u) { st.pop_back(); cp = st.back(); }
                }
                // DECISIVE: does MY nodeFlags walk of DFF localRest reproduce RW's authoritative restLocal?
                {
                    float32 maxPos = 0, maxRotDeg = 0; int wp = -1, wr = -1;
                    for (int32 i = 0; i < n; ++i) {
                        Math::Matrix4x4 rl = fromCM(c.bones[i].restLocal);
                        Math::Vector3 dp{wD[i].m03-rl.m03, wD[i].m13-rl.m13, wD[i].m23-rl.m23};
                        float32 pd = std::sqrt(dp.x*dp.x+dp.y*dp.y+dp.z*dp.z);
                        Math::Matrix4x4 D = rl.inverse() * wD[i];
                        float32 tr=(D.m00+D.m11+D.m22); float32 ca=Math::clamp((tr-1.0f)*0.5f,-1.0f,1.0f);
                        float32 deg = Math::degrees(std::acos(ca));
                        if (pd>maxPos){maxPos=pd;wp=i;} if(deg>maxRotDeg){maxRotDeg=deg;wr=i;}
                    }
                    std::cerr << "diag WALK-vs-restLocal: maxPosDiff=" << maxPos << " (b" << wp
                              << ") maxRotDeg=" << maxRotDeg << " (b" << wr << ") [~0 => my walk matches RW]\n";
                    for (int32 i = 0; i < n && i < 8; ++i) {
                        Math::Matrix4x4 rl = fromCM(c.bones[i].restLocal);
                        std::cerr << "    b" << i << " myWalkPos=(" << wD[i].m03<<","<<wD[i].m13<<","<<wD[i].m23
                                  << ") rwRestPos=(" << rl.m03<<","<<rl.m13<<","<<rl.m23 << ")\n";
                    }
                }
                float32 maxDev = 0; int worstB = -1;
                for (int32 i = 0; i < n; ++i) {
                    Math::Matrix4x4 chk = wD[i] * fromCM(c.bones[i].inverseBind);   // should be ~identity
                    float32 dev = std::sqrt(
                        (chk.m00-1)*(chk.m00-1)+(chk.m01*chk.m01+chk.m02*chk.m02) +
                        (chk.m10*chk.m10+(chk.m11-1)*(chk.m11-1)+chk.m12*chk.m12) +
                        (chk.m20*chk.m20+chk.m21*chk.m21+(chk.m22-1)*(chk.m22-1)) +
                        chk.m03*chk.m03+chk.m13*chk.m13+chk.m23*chk.m23);
                    if (dev > maxDev) { maxDev = dev; worstB = i; }
                    std::cerr << "    b" << i << " flags=" << c.bones[i].nodeFlags
                              << " wDffPos=(" << wD[i].m03<<","<<wD[i].m13<<","<<wD[i].m23
                              << ") dev=" << dev << "\n";
                }
                std::cerr << "diag DFF localRest consistency: wDff*invBind maxDev=" << maxDev
                          << " (worst b" << worstB << ") [~0 means consistent]\n";
            }
            std::cerr << "diag bun_pc bones=" << n << "\n";
            for (int32 i = 0; i < n && i < 6; ++i) {
                Math::Matrix4x4 chk = wRest[i] * fromCM(c.bones[i].inverseBind);   // should be ~identity
                float32 det = (wRest[i].m00*(wRest[i].m11*wRest[i].m22-wRest[i].m12*wRest[i].m21)
                             - wRest[i].m01*(wRest[i].m10*wRest[i].m22-wRest[i].m12*wRest[i].m20)
                             + wRest[i].m02*(wRest[i].m10*wRest[i].m21-wRest[i].m11*wRest[i].m20));
                std::cerr << "  b" << i << " id=" << c.bones[i].id << " par=" << parent[i]
                          << " wRest*invBind=(" << chk.m00 << "," << chk.m11 << "," << chk.m22 << ")"
                          << " detRot=" << det << "\n";
            }

            // IFP: find the animation with max boneId overlap, compare first keyframe to L_rest.
            const std::string ifpPath = base + "allanims.ifp";
            std::FILE* f = std::fopen(ifpPath.c_str(), "rb");
            if (f) {
                std::fseek(f, 0, SEEK_END); long len = std::ftell(f); std::fseek(f, 0, SEEK_SET);
                std::vector<uint8> buf(len > 0 ? static_cast<size_t>(len) : 0u);
                if (len > 0) buf.resize(std::fread(buf.data(), 1, buf.size(), f));
                std::fclose(f);
                auto res = Assets::parseIfp(buf.data(), static_cast<uint32>(buf.size()));
                if (!res) {
                    std::cerr << "diag: ifp parse failed\n";
                } else {
                    const auto& ifp = *res;
                    std::unordered_set<int16> idSet;
                    for (const auto& b : c.bones) idSet.insert(static_cast<int16>(b.id));
                    const Assets::IfpAnimation* best = nullptr; int bestOverlap = -1;
                    for (const auto& bl : ifp.blocks)
                        for (const auto& an : bl.animations) {
                            int ov = 0;
                            for (const auto& bt : an.bones) if (idSet.count(bt.boneId)) ++ov;
                            if (ov > bestOverlap) { bestOverlap = ov; best = &an; }
                        }
                    std::cerr << "diag best anim '" << best->name << "' dur=" << best->duration
                              << "s tracks=" << best->bones.size() << " overlapWithBun=" << bestOverlap << "\n";
                    {
                        std::unordered_set<int16> trackIds;
                        for (const auto& bt : best->bones) trackIds.insert(bt.boneId);
                        std::string missing;
                        for (int32 i = 0; i < n; ++i)
                            if (!trackIds.count(static_cast<int16>(c.bones[i].id)))
                                missing += " b" + std::to_string(i) + "(id=" + std::to_string(c.bones[i].id) + ",par=" + std::to_string(parent[i]) + ")";
                        std::cerr << "diag bones WITHOUT track:" << (missing.empty() ? " none" : missing) << "\n";
                    }
                    int shown = 0;
                    for (const auto& bt : best->bones) {
                        if (!idSet.count(bt.boneId)) continue;
                        // find skeleton index for this boneId
                        int32 idx = -1;
                        for (int32 i = 0; i < n; ++i) if (c.bones[i].id == bt.boneId) { idx = i; break; }
                        const auto& fr0 = bt.frames.front();
                        std::cerr << "  id=" << bt.boneId << " b" << idx << " type=" << (int)bt.frameType
                                  << " nfr=" << bt.frames.size()
                                  << " ifpPos=(" << fr0.pos[0] << "," << fr0.pos[1] << "," << fr0.pos[2] << ")"
                                  << " lRestT=" << posStr(lRest[idx]) << "\n";
                        if (++shown >= 8) break;
                    }
                }
            }

            // ---- composition sanity test: replicate main.cpp walk, compare W_anim vs W_rest per joint ----
            {
                std::FILE* f2 = std::fopen(ifpPath.c_str(), "rb");
                if (f2) {
                    std::fseek(f2, 0, SEEK_END); long l2 = std::ftell(f2); std::fseek(f2, 0, SEEK_SET);
                    std::vector<uint8> b2(l2 > 0 ? static_cast<size_t>(l2) : 0u);
                    if (l2 > 0) b2.resize(std::fread(b2.data(), 1, b2.size(), f2));
                    std::fclose(f2);
                    auto r2 = Assets::parseIfp(b2.data(), static_cast<uint32>(b2.size()));
                    if (r2 && !r2->blocks.empty()) {
                        const Assets::IfpAnimation* anim = r2->findAnimation("Idle_Action_Idle_01");
                        if (!anim) anim = &r2->blocks[0].animations.front();
                        std::vector<int32> tIdx(n, -1); std::vector<uint8> tType(n, 0u);
                        for (int32 i = 0; i < n; ++i)
                            for (size_t k = 0; k < anim->bones.size(); ++k)
                                if (anim->bones[k].boneId == c.bones[i].id) { tIdx[i] = static_cast<int32>(k); tType[i] = anim->bones[k].frameType; break; }
                        float32 dur = (anim->duration > 1e-3f) ? anim->duration : 1.0f;
                        std::cerr << "diag comp test anim='" << anim->name << "' dur=" << dur << "\n";
                        for (float32 tt : {0.0f, dur * 0.5f}) {
                            std::vector<Math::Matrix4x4> wAnim(n);
                            std::vector<Math::Matrix4x4> stk; Math::Matrix4x4 cp = Math::Matrix4x4::identity(); stk.push_back(cp);
                            for (int32 i = 0; i < n; ++i) {
                                if (tIdx[i] >= 0) {
                                    float32 q[4] = {0,0,0,1}, p[3] = {0,0,0};
                                    Assets::sampleBone(*anim, c.bones[i].id, tt, q, p);
                                    Math::Matrix4x4 L = lRest[i];
                                    if (tType[i] < 3u) { auto Rq = Math::Quaternion{q[3],q[0],q[1],q[2]}.toMatrix().make4x4(); L.m00=Rq.m00;L.m01=Rq.m01;L.m02=Rq.m02;L.m10=Rq.m10;L.m11=Rq.m11;L.m12=Rq.m12;L.m20=Rq.m20;L.m21=Rq.m21;L.m22=Rq.m22; }
                                    if (tType[i] > 1u) { L.m03=p[0];L.m13=p[1];L.m23=p[2]; }
                                    wAnim[i] = cp * L;
                                } else {
                                    wAnim[i] = cp * lRest[i];
                                }
                                if (c.bones[i].nodeFlags & 0x2u) stk.push_back(cp);
                                cp = wAnim[i];
                                if ((c.bones[i].nodeFlags & 0x1u) && stk.size() > 1u) { stk.pop_back(); cp = stk.back(); }
                            }
                            std::cerr << "  t=" << tt << "\n";
                            for (int32 i = 0; i < n; ++i) {
                                Math::Vector3 rp{wRest[i].m03,wRest[i].m13,wRest[i].m23};
                                Math::Vector3 ap{wAnim[i].m03,wAnim[i].m13,wAnim[i].m23};
                                float32 dx=ap.x-rp.x, dy=ap.y-rp.y, dz=ap.z-rp.z;
                                std::cerr << "    b" << i << (tIdx[i]>=0?"*":" ") << " rest=(" << rp.x<<","<<rp.y<<","<<rp.z
                                          << ") anim=(" << ap.x<<","<<ap.y<<","<<ap.z
                                          << ") d=" << std::sqrt(dx*dx+dy*dy+dz*dz) << "\n";
                            }
                        }

                        // ---- decisive: raw quats + angles at t=0, and current-vs-transposed fold magnitude ----
                        {
                            // mode: 0=abs xyzw(current) 1=abs wxyz 2=delta Rrest*Rifp xyzw 3=transposed abs xyzw
                            auto walk = [&](int mode) {
                                std::vector<Math::Matrix4x4> wA(n);
                                std::vector<Math::Matrix4x4> st; Math::Matrix4x4 c2 = Math::Matrix4x4::identity(); st.push_back(c2);
                                for (int32 i = 0; i < n; ++i) {
                                    if (tIdx[i] >= 0) {
                                        float32 q[4]={0,0,0,1}, p[3]={0,0,0};
                                        Assets::sampleBone(*anim, c.bones[i].id, 0.0f, q, p);
                                        Math::Matrix4x4 L = lRest[i];
                                        if (tType[i] < 3u) {
                                            Math::Quaternion qq;
                                            if (mode == 1) qq = Math::Quaternion{q[0],q[1],q[2],q[3]};   // wxyz
                                            else          qq = Math::Quaternion{q[3],q[0],q[1],q[2]};   // xyzw
                                            auto Rq = qq.toMatrix().make4x4();
                                            if (mode == 3) Rq = Rq.transpose();
                                            if (mode == 2) { Math::Matrix4x4 Rr; {auto Ll=lRest[i];Rr.m00=Ll.m00;Rr.m01=Ll.m01;Rr.m02=Ll.m02;Rr.m10=Ll.m10;Rr.m11=Ll.m11;Rr.m12=Ll.m12;Rr.m20=Ll.m20;Rr.m21=Ll.m21;Rr.m22=Ll.m22;} Rq = Rr * Rq; }
                                            L.m00=Rq.m00;L.m01=Rq.m01;L.m02=Rq.m02;L.m10=Rq.m10;L.m11=Rq.m11;L.m12=Rq.m12;L.m20=Rq.m20;L.m21=Rq.m21;L.m22=Rq.m22;
                                        }
                                        if (tType[i] > 1u) { L.m03=p[0];L.m13=p[1];L.m23=p[2]; }
                                        wA[i] = c2 * L;
                                    } else { wA[i] = c2 * lRest[i]; }
                                    if (c.bones[i].nodeFlags & 0x2u) st.push_back(c2);
                                    c2 = wA[i];
                                    if ((c.bones[i].nodeFlags & 0x1u) && st.size() > 1u) { st.pop_back(); c2 = st.back(); }
                                }
                                float32 mx=0; for (int32 i=0;i<n;++i){ Math::Vector3 rp{wRest[i].m03,wRest[i].m13,wRest[i].m23}; Math::Vector3 ap{wA[i].m03,wA[i].m13,wA[i].m23}; float32 dx=ap.x-rp.x,dy=ap.y-rp.y,dz=ap.z-rp.z; mx=std::max(mx,std::sqrt(dx*dx+dy*dy+dz*dz)); }
                                return mx;
                            };
                            std::cerr << "  FOLD t=0: absXYZW=" << walk(0)
                                      << " absWXYZ=" << walk(1)
                                      << " deltaRrestRifp=" << walk(2)
                                      << " transposedAbs=" << walk(3) << "\n";

                            // scan ALL animations for min max-fold at t=0 under WXYZ (mode 1): a static pose should be ~0
                            auto foldOf = [&](const Assets::IfpAnimation* an, int mode) {
                                std::vector<int32> ti(n, -1); std::vector<uint8> tt(n, 0u);
                                for (int32 i = 0; i < n; ++i)
                                    for (size_t k = 0; k < an->bones.size(); ++k)
                                        if (an->bones[k].boneId == c.bones[i].id) { ti[i] = static_cast<int32>(k); tt[i] = an->bones[k].frameType; break; }
                                std::vector<Math::Matrix4x4> wA(n);
                                std::vector<Math::Matrix4x4> st; Math::Matrix4x4 c2 = Math::Matrix4x4::identity(); st.push_back(c2);
                                for (int32 i = 0; i < n; ++i) {
                                    if (ti[i] >= 0) {
                                        float32 q[4]={0,0,0,1}, p[3]={0,0,0};
                                        Assets::sampleBone(*an, c.bones[i].id, 0.0f, q, p);
                                        Math::Matrix4x4 L = lRest[i];
                                        if (tt[i] < 3u) {
                                            Math::Quaternion qq;
                                            if (mode == 1) qq = Math::Quaternion{q[0],q[1],q[2],q[3]}; else qq = Math::Quaternion{q[3],q[0],q[1],q[2]};
                                            auto Rq = qq.toMatrix().make4x4(); L.m00=Rq.m00;L.m01=Rq.m01;L.m02=Rq.m02;L.m10=Rq.m10;L.m11=Rq.m11;L.m12=Rq.m12;L.m20=Rq.m20;L.m21=Rq.m21;L.m22=Rq.m22;
                                        }
                                        if (tt[i] > 1u) { L.m03=p[0];L.m13=p[1];L.m23=p[2]; }
                                        wA[i] = c2 * L;
                                    } else { wA[i] = c2 * lRest[i]; }
                                    if (c.bones[i].nodeFlags & 0x2u) st.push_back(c2);
                                    c2 = wA[i];
                                    if ((c.bones[i].nodeFlags & 0x1u) && st.size() > 1u) { st.pop_back(); c2 = st.back(); }
                                }
                                float32 mx=0; for (int32 i=0;i<n;++i){ Math::Vector3 rp{wRest[i].m03,wRest[i].m13,wRest[i].m23}; Math::Vector3 ap{wA[i].m03,wA[i].m13,wA[i].m23}; float32 dx=ap.x-rp.x,dy=ap.y-rp.y,dz=ap.z-rp.z; mx=std::max(mx,std::sqrt(dx*dx+dy*dy+dz*dz)); }
                                return mx;
                            };
                            struct Res { std::string name; int32 overlap; float32 fX, fW; };
                            std::vector<Res> res;
                            for (auto& blk : r2->blocks) for (auto& an : blk.animations) {
                                int32 ov = 0;
                                for (size_t k = 0; k < an.bones.size(); ++k)
                                    for (int32 i = 0; i < n; ++i) if (c.bones[i].id == an.bones[k].boneId) { ov++; break; }
                                res.push_back({an.name, ov, foldOf(&an, 0), foldOf(&an, 1)});
                            }
                            // only anims that actually move the body (overlap >= 15 bones)
                            auto near = [&](float32 f){ return f < 0.05f; };
                            int cntX=0, cntW=0; float32 minX=9e9f, minW=9e9f; std::string nX,nW;
                            for (auto& r : res) if (r.overlap >= 15) {
                                if (near(r.fX)) ++cntX;
                                if (near(r.fW)) ++cntW;
                                if (r.fX < minX){minX=r.fX;nX=r.name;}
                                if (r.fW < minW){minW=r.fW;nW=r.name;}
                            }
                            std::cerr << "  SCAN body-anims(overlap>=15): nearRest(<0.05) XYZW=" << cntX << " WXYZ=" << cntW
                                      << "\n    minXyzw='" << nX << "' f=" << minX
                                      << "   minWxyz='" << nW << "' f=" << minW << "\n";

                            // raw keyframe sanity: unit-length + smooth consecutive motion (WXYZ order)
                            {
                                auto angBetween = [](const float32 a[4], const float32 b[4]) {
                                    float32 d = a[0]*b[0]+a[1]*b[1]+a[2]*b[2]+a[3]*b[3]; if (d<0) d=-d;
                                    return Math::degrees(std::acos(Math::clamp(d,-1.0f,1.0f)));   // angle between quats (sign-agnostic)
                                };
                                int shown = 0;
                                for (auto& bt : anim->bones) {
                                    if (bt.frameType >= 3u || bt.frames.size() < 3 || !bt.frames[0].hasQuat) continue;
                                    const auto& F = bt.frames;
                                    float32 mag0 = std::sqrt(F[0].quat[0]*F[0].quat[0]+F[0].quat[1]*F[0].quat[1]+F[0].quat[2]*F[0].quat[2]+F[0].quat[3]*F[0].quat[3]);
                                    float32 maxStep = 0; for (size_t k=1;k<F.size();++k) maxStep = std::max(maxStep, angBetween(F[k-1].quat, F[k].quat));
                                    std::cerr << "    bone id=" << bt.boneId << " type=" << (int)bt.frameType << " nfr=" << F.size()
                                              << " t0=" << F[0].time << " tN=" << F.back().time
                                              << " mag0=" << mag0 << " maxStepDeg=" << maxStep
                                              << " q0=(" << F[0].quat[0]<<","<<F[0].quat[1]<<","<<F[0].quat[2]<<","<<F[0].quat[3] << ")\n";
                                    if (++shown >= 8) break;
                                }
                            }
                            int shown2 = 0;
                            for (int32 i = 0; i < n && shown2 < 14; ++i) {
                                if (tIdx[i] < 0) continue;
                                float32 q[4]={0,0,0,1}, p[3]={0,0,0};
                                Assets::sampleBone(*anim, c.bones[i].id, 0.0f, q, p);
                                Math::Quaternion qq{q[3],q[0],q[1],q[2]};
                                // delta rotation from rest: D = R_rest^-1 * R_ifp, angle from trace
                                Math::Matrix4x4 Rr; { auto L=lRest[i]; Rr.m00=L.m00;Rr.m01=L.m01;Rr.m02=L.m02;Rr.m10=L.m10;Rr.m11=L.m11;Rr.m12=L.m12;Rr.m20=L.m20;Rr.m21=L.m21;Rr.m22=L.m22; }
                                Math::Matrix4x4 Ri = qq.toMatrix().make4x4();
                                Math::Matrix4x4 D = Rr.inverse() * Ri;
                                float32 tr=(D.m00+D.m11+D.m22); float32 ca=Math::clamp((tr-1.0f)*0.5f,-1.0f,1.0f);
                                std::cerr << "    b" << i << " q=(" << q[0]<<","<<q[1]<<","<<q[2]<<","<<q[3]
                                          << ") deltaFromRest=" << (Math::degrees(std::acos(ca)))
                                          << " p=(" << p[0]<<","<<p[1]<<","<<p[2] << ")\n";
                                ++shown2;
                            }
                        }
                    }
                }
            }
        }
    }

    // ---- MLS parser golden-file test ----
    {
        Assets::MlsFile mls;
        bool ok = mls.load(base + "scripts/asylum.mls");
        if (!ok) { std::cerr << "mls: LOAD FAIL\n"; return 1; }
        size_t n = mls.scriptCount();
        if (n != 56) { std::cerr << "mls: expected 56 scripts, got " << n << "\n"; return 1; }

        int idx = mls.findScriptByEntity("gen_save_point01");
        if (idx < 0) { std::cerr << "mls: gen_save_point01 not found\n"; return 1; }
        const auto& sc = mls.script(idx);
        if (sc.code.size() != 46) { std::cerr << "mls: savepoint codeWords=" << sc.code.size() << " expected 46\n"; return 1; }
        if (sc.events.empty()) { std::cerr << "mls: savepoint has no events\n"; return 1; }
        if (std::string(sc.events[0].name) != "onlevelsave") { std::cerr << "mls: event name='" << sc.events[0].name << "'\n"; return 1; }

        int idx2 = mls.findScriptByEntity("asylum");
        if (idx2 < 0) { std::cerr << "mls: levelscript 'asylum' not found\n"; return 1; }
        const auto& lvl = mls.script(idx2);
        if (lvl.code.size() != 4244) { std::cerr << "mls: levelscript codeWords=" << lvl.code.size() << " expected 4244\n"; return 1; }

        std::cerr << "mls: ok scripts=56 savepoint=46w levelscript=4244w\n";
    }

    // ---- SFX bank parser golden-file test ----
    {
        Assets::SfxBank bank;
        bool ok = bank.load(TestPaths::gameRoot() + "audio/PC/SFX");
        if (!ok) { std::cerr << "sfx: LOAD FAIL\n"; return 1; }
        size_t n = bank.sampleCount();
        if (n != 14098) { std::cerr << "sfx: expected 14098 records, got " << n << "\n"; return 1; }

        Assets::SfxSample s;
        if (!bank.getSample("SFX_FOOTSTEP_CONCRETE_1", s)) { std::cerr << "sfx: FOOTSTEP_CONCRETE_1 not found\n"; return 1; }
        if (s.length() != 4096) { std::cerr << "sfx: footstep len=" << s.length() << " expected 4096\n"; return 1; }
        if (s.sampleRate != 17000) { std::cerr << "sfx: footstep rate=" << s.sampleRate << " expected 17000\n"; return 1; }

        Assets::SfxSample s2;
        if (!bank.getSample("SFX_COL_WOOD_DOOR_1", s2)) { std::cerr << "sfx: COL_WOOD_DOOR_1 not found\n"; return 1; }
        if (s2.length() == 0 || s2.sampleRate == 0) { std::cerr << "sfx: door sample empty\n"; return 1; }

        std::cerr << "sfx: ok records=14098 footstep=4096B@17kHz door=" << s2.length() << "B@" << s2.sampleRate << "Hz\n";
    }

    std::cerr << "assets_test: done\n";
    return 0;
}
