// librw_test.cpp - parse-only integration test against librw (NULL platform).
// Validates that librw can read the game's real .txd (RenderWare Texture Dictionary)
// and .dff (RenderWare Model Dictionary / Clump) files and expose the geometry/texture
// data. Used to confirm the integration path before wiring it into the Vulkan engine.
#include <iostream>
#include <string>

#include "rw.h"
#include "test_paths.h"

namespace {

const std::string kDefaultTxd = TestPaths::gameRoot() + "levels/asylum/picload_pc.txd";
const std::string kDefaultDff = TestPaths::gameRoot() + "levels/asylum/pak/modelspc.dff";

}  // namespace

int main(int argc, char** argv) {
    std::string txdPath = kDefaultTxd;
    std::string dffPath = kDefaultDff;
    if (argc > 1) txdPath = argv[1];
    if (argc > 2) dffPath = argv[2];

    std::cout << "librw_test: init engine (NULL platform)\n";
    if (!rw::Engine::init()) {
        std::cerr << "  Engine::init failed\n";
        return 1;
    }
    if (!rw::Engine::open(nullptr)) {
        std::cerr << "  Engine::open failed\n";
        return 1;
    }
    if (!rw::Engine::start()) {
        std::cerr << "  Engine::start failed\n";
        return 1;
    }
    std::cout << "librw_test: engine started\n";

    rw::TexDictionary* currentTxd = nullptr;

    // ---------------- TXD (RenderWare Texture Dictionary) ----------------
    if (!txdPath.empty()) {
        std::cout << "librw_test: loading TXD " << txdPath << "\n";
        rw::uint32 len = 0;
        rw::uint8* data = rw::getFileContents(txdPath.c_str(), &len);
        if (!data) {
            std::cerr << "  couldn't open TXD\n";
        } else {
            rw::StreamMemory in;
            in.open(data, len);
            if (rw::findChunk(&in, rw::ID_TEXDICTIONARY, nullptr, nullptr)) {
                currentTxd = rw::TexDictionary::streamRead(&in);
                if (currentTxd) {
                    std::cout << "  TXD: " << currentTxd->count() << " textures\n";
                    int shown = 0;
                    FORLIST(lnk, currentTxd->textures) {
                        rw::Texture* tex = LLLinkGetData(lnk, rw::Texture, inDict);
                        if (tex && tex->raster) {
                            std::cout << "  [" << shown << "] " << tex->name
                                      << "  raster=" << tex->raster->width << "x"
                                      << tex->raster->height << "  stride="
                                      << tex->raster->stride
                                      << "  format=0x" << std::hex
                                      << tex->raster->format << std::dec << "\n";
                            {
                                std::cout << "    pixels_before_lock="
                                          << (tex->raster->pixels ? "set" : "nil") << "\n";
                                rw::uint8* px = tex->raster->lock(0, rw::Raster::LOCKREAD);
                                if (px) {
                                    std::cout << "    first pixel bytes: ";
                                    for (int k = 0; k < 8; ++k)
                                        std::cout << int(px[k]) << ' ';
                                    std::cout << "\n";
                                    tex->raster->unlock(0);
                                } else {
                                    std::cout << "    lock failed\n";
                                }
                            }
                            ++shown;
                            if (shown >= 12) {
                                std::cout << "  ... (truncated at " << shown << ")\n";
                                break;
                            }
                        }
                    }
                    rw::TexDictionary::setCurrent(currentTxd);
                }
            } else {
                std::cerr << "  no TEXDICTIONARY chunk found\n";
            }
            in.close();
            rwFree(data);
        }
    }

    // ---------------- DFF (RenderWare Model Dictionary / Clump) ----------------
    if (!dffPath.empty()) {
        std::cout << "librw_test: loading DFF " << dffPath << "\n";
        rw::uint32 len = 0;
        rw::uint8* data = rw::getFileContents(dffPath.c_str(), &len);
        if (!data) {
            std::cerr << "  couldn't open DFF\n";
        } else {
            rw::StreamMemory in;
            in.open(data, len);
            int clumpShown = 0;
            bool foundAny = false;
            while (rw::findChunk(&in, rw::ID_CLUMP, nullptr, nullptr)) {
                rw::Clump* clump = rw::Clump::streamRead(&in);
                if (!clump)
                    break;
                foundAny = true;
                std::cout << "  clump[" << clumpShown << "]: atomics="
                          << clump->countAtomics() << "\n";

                if (clumpShown < 3) {
                    int atomicShown = 0;
                    FORLIST(lnk, clump->atomics) {
                        rw::Atomic* atomic = rw::Atomic::fromClump(lnk);
                        if (!atomic || !atomic->geometry)
                            continue;
                        rw::Geometry* geo = atomic->geometry;
                        std::cout << "    atomic[" << atomicShown << "] verts="
                                  << geo->numVertices
                                  << " tris=" << geo->numTriangles
                                  << " texcoordsets=" << geo->numTexCoordSets
                                  << " flags=0x" << std::hex << geo->flags << std::dec
                                  << "\n";
                        {
                            std::cout << "      matList.numMaterials="
                                      << geo->matList.numMaterials << "\n";
                            if (geo->matList.materials) {
                                for (int i = 0; i < geo->matList.numMaterials && i < 8; ++i) {
                                    const rw::Material* mat = geo->matList.materials[i];
                                    const char* texName = "nil";
                                    if (mat && mat->texture)
                                        texName = mat->texture->name;
                                    std::cout << "      material[" << i << "]=mat:"
                                              << (mat ? "set" : "nil")
                                              << " tex:" << texName << "\n";
                                }
                            }

                            if (geo->morphTargets && geo->numMorphTargets > 0) {
                                const rw::V3d* verts = geo->morphTargets[0].vertices;
                                const rw::V3d* norms = geo->morphTargets[0].normals;
                                std::cout << "      morph0 vertices="
                                          << (verts ? "set" : "nil")
                                          << " normals="
                                          << (norms ? "set" : "nil") << "\n";
                                if (verts) {
                                    std::cout << "      first 4 vertices: ";
                                    for (int i = 0; i < 4 && i < geo->numVertices; ++i)
                                        std::cout << verts[i].x << ','
                                                  << verts[i].y << ','
                                                  << verts[i].z << ' ';
                                    std::cout << "\n";
                                }
                                if (norms) {
                                    std::cout << "      first 4 normals: ";
                                    for (int i = 0; i < 4 && i < geo->numVertices; ++i)
                                        std::cout << norms[i].x << ','
                                                  << norms[i].y << ','
                                                  << norms[i].z << ' ';
                                    std::cout << "\n";
                                }
                            }

                            std::cout << "      first 8 triangles: ";
                            for (int i = 0; i < 8 && i < geo->numTriangles; ++i) {
                                const rw::Triangle& t = geo->triangles[i];
                                std::cout << t.v[0] << ',' << t.v[1] << ',' << t.v[2]
                                          << "/mat" << t.matId << ' ';
                            }
                            std::cout << "\n";

                            if (geo->numTexCoordSets > 0 && geo->texCoords[0]) {
                                std::cout << "      first 4 texcoords: ";
                                for (int i = 0; i < 4 && i < geo->numVertices; ++i)
                                    std::cout << geo->texCoords[0][i].u << ','
                                              << geo->texCoords[0][i].v << ' ';
                                std::cout << "\n";
                            }
                        }
                        ++atomicShown;
                        if (atomicShown >= 1)
                            break;
                    }
                }

                clump->destroy();
                ++clumpShown;
                if (clumpShown >= 5) {
                    std::cout << "  ... (truncated at " << clumpShown << " clumps)\n";
                    break;
                }
            }
            if (!foundAny) {
                std::cerr << "  no CLUMP chunk found\n";
            }
            in.close();
            rwFree(data);
        }
    }

    if (currentTxd) {
        rw::TexDictionary::setCurrent(nullptr);
        currentTxd->destroy();
    }

    std::cout << "librw_test: shutting down\n";
    rw::Engine::stop();
    rw::Engine::close();
    rw::Engine::term();
    std::cout << "librw_test: done\n";
    return 0;
}
