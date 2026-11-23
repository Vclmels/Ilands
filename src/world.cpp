#include "world.hpp"
#include <queue>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <random>
#include <cmath>

namespace fs = std::filesystem;

namespace ilands {

World::World(int seed, WorldType type, const std::array<Rect, NUM_BLOCK_TEXTURES>& atlasUvs)
    : seed_(seed), type_(type), perlin_(seed), random_(seed), atlasUvs_(atlasUvs) {
    heightMap_.assign(BlocksWide * BlocksDeep, 0);

    float light = 1.0f;
    for (int i = 15; i >= 0; --i) {
        lightAmounts_[i] = light;
        light *= 0.9f;
    }

    chunks_.reserve(ChunksWide * ChunksDeep);
    for (int cx = 0; cx < ChunksWide; ++cx) {
        for (int cy = 0; cy < ChunksDeep; ++cy) {
            chunks_.push_back(std::make_unique<Chunk>(cx, cy));
        }
    }
}

void World::GenerateAll(const std::vector<WorldUpdateEntry>& savedUpdates) {
    GenerateTerrain();
    DecorateTerrain();

    for (const auto& u : savedUpdates) {
        if (u.x < 0 || u.y < 0 || u.z < 0 || u.x >= BlocksWide || u.y >= BlocksDeep || u.z >= BlocksHigh) {
            continue;
        }
        if (Block::GetEmissionLightSourceLevel(u.type) > 0) {
            int cx = u.x / ChunkBlockWidth;
            int cy = u.y / ChunkBlockDepth;
            GetChunkRef(cx, cy).emissionLights.emplace_back(u.x, u.y, u.z);
        }
        SetBlockType(u.x, u.y, u.z, u.type, false);
    }

    FillInitialWater();
    GenerateInitialLight();

    for (int cx = 0; cx < ChunksWide; ++cx) {
        for (int cy = 0; cy < ChunksDeep; ++cy) {
            Chunk& c = GetChunkRef(cx, cy);
            PropagateChunkSkyLightSources(c, false);
            c.lightInitialized = true;
        }
    }
}

BlockType World::GetBlockType(int x, int y, int z) const {
    if (x < 0 || y < 0 || z < 0 || x >= BlocksWide || y >= BlocksDeep || z >= BlocksHigh) {
        return BlockType::Null;
    }
    int cx = x / ChunkBlockWidth;
    int cy = y / ChunkBlockDepth;
    int bx = x % ChunkBlockWidth;
    int by = y % ChunkBlockDepth;
    return GetChunkRef(cx, cy).blocks[bx][by][z].type;
}

void World::SetBlockType(int x, int y, int z, BlockType blockType, bool doRegeneration) {
    if (x < 0 || y < 0 || z < 0 || x >= BlocksWide || y >= BlocksDeep || z >= BlocksHigh) {
        return;
    }
    int cx = x / ChunkBlockWidth;
    int cy = y / ChunkBlockDepth;
    int bx = x % ChunkBlockWidth;
    int by = y % ChunkBlockDepth;
    Block& b = GetChunkRef(cx, cy).blocks[bx][by][z];
    BlockType prevType = b.type;
    b.type = blockType;

    if (doRegeneration) {
        RegenerateChunks(cx, cy, bx, by);
        RefillWaterFlags(x, y, z, prevType, blockType);
        RegenerateBlockLight(x, y, z, prevType, blockType);
    }
}

bool World::HasBlockFlag(int x, int y, int z, BlockFlag flag) const {
    if (x < 0 || y < 0 || z < 0 || x >= BlocksWide || y >= BlocksDeep || z >= BlocksHigh) {
        return false;
    }
    int cx = x / ChunkBlockWidth;
    int cy = y / ChunkBlockDepth;
    int bx = x % ChunkBlockWidth;
    int by = y % ChunkBlockDepth;
    return (GetChunkRef(cx, cy).blocks[bx][by][z].flags & static_cast<uint8_t>(flag)) != 0;
}

void World::SetBlockFlag(int x, int y, int z, BlockFlag flag) {
    if (x < 0 || y < 0 || z < 0 || x >= BlocksWide || y >= BlocksDeep || z >= BlocksHigh) {
        return;
    }
    int cx = x / ChunkBlockWidth;
    int cy = y / ChunkBlockDepth;
    int bx = x % ChunkBlockWidth;
    int by = y % ChunkBlockDepth;
    GetChunkRef(cx, cy).blocks[bx][by][z].flags |= static_cast<uint8_t>(flag);
}

void World::ClearBlockFlag(int x, int y, int z, BlockFlag flag) {
    if (x < 0 || y < 0 || z < 0 || x >= BlocksWide || y >= BlocksDeep || z >= BlocksHigh) {
        return;
    }
    int cx = x / ChunkBlockWidth;
    int cy = y / ChunkBlockDepth;
    int bx = x % ChunkBlockWidth;
    int by = y % ChunkBlockDepth;
    GetChunkRef(cx, cy).blocks[bx][by][z].flags &= static_cast<uint8_t>(~static_cast<uint8_t>(flag));
}

bool World::NextToBlockType(int x, int y, int z, BlockType type) const {
    return GetBlockType(x + 1, y, z) == type ||
           GetBlockType(x - 1, y, z) == type ||
           GetBlockType(x, y + 1, z) == type ||
           GetBlockType(x, y - 1, z) == type;
}

bool World::AdjacentToBlockType(int x, int y, int z, BlockType type) const {
    return GetBlockType(x + 1, y, z) == type ||
           GetBlockType(x - 1, y, z) == type ||
           GetBlockType(x, y + 1, z) == type ||
           GetBlockType(x, y - 1, z) == type ||
           GetBlockType(x, y, z + 1) == type ||
           GetBlockType(x, y, z - 1) == type;
}

bool World::AdjacentToWater(int x, int y, int z) const {
    if (AdjacentToBlockType(x, y, z, BlockType::Water) ||
        (NextToBlockType(x, y, z, BlockType::Null) && z <= SeaLevel)) {
        return true;
    }
    if (HasBlockFlag(x + 1, y, z, BlockFlag::IsWaterFilled) ||
        HasBlockFlag(x - 1, y, z, BlockFlag::IsWaterFilled) ||
        HasBlockFlag(x, y + 1, z, BlockFlag::IsWaterFilled) ||
        HasBlockFlag(x, y - 1, z, BlockFlag::IsWaterFilled) ||
        HasBlockFlag(x, y, z + 1, BlockFlag::IsWaterFilled) ||
        HasBlockFlag(x, y, z - 1, BlockFlag::IsWaterFilled)) {
        return true;
    }
    return false;
}

bool World::SpaceAroundIsEmpty(int x, int y, int z, int widthAround, int depthAround, int heightAbove) const {
    for (int i = x - widthAround; i <= x + widthAround; ++i) {
        for (int j = y - depthAround; j <= y + depthAround; ++j) {
            for (int k = z; k < z + heightAbove; ++k) {
                if (GetBlockType(i, j, k) != BlockType::Air) {
                    return false;
                }
            }
        }
    }
    return true;
}

std::vector<IntVector3> World::FillWithWater(int sx, int sy, int sz, bool doRegeneration) {
    std::vector<IntVector3> result;
    std::queue<IntVector3> q;
    q.emplace(sx, sy, sz);

    while (!q.empty()) {
        IntVector3 p = q.front();
        q.pop();

        if (p.x < 0 || p.y < 0 || p.z < 0 || p.x >= BlocksWide || p.y >= BlocksDeep || p.z > SeaLevel) {
            continue;
        }
        if (HasBlockFlag(p.x, p.y, p.z, BlockFlag::IsWaterFilled)) {
            continue;
        }
        BlockType bt = GetBlockType(p.x, p.y, p.z);
        if (!Block::IsFillable(bt)) {
            continue;
        }

        if (bt == BlockType::Air) {
            SetBlockType(p.x, p.y, p.z, BlockType::Water, doRegeneration);
            if (p.z == SeaLevel) {
                result.push_back(p);
            }
        } else {
            SetBlockFlag(p.x, p.y, p.z, BlockFlag::IsWaterFilled);
            if (doRegeneration) {
                RegenerateChunks(p.x / ChunkBlockWidth, p.y / ChunkBlockDepth, p.x % ChunkBlockWidth, p.y % ChunkBlockDepth);
            }
        }
        SetWaterSurface(p.x, p.y, p.z);

        q.emplace(p.x + 1, p.y, p.z);
        q.emplace(p.x - 1, p.y, p.z);
        q.emplace(p.x, p.y + 1, p.z);
        q.emplace(p.x, p.y - 1, p.z);
        q.emplace(p.x, p.y, p.z + 1);
        if (!Block::IsPartialBlock(bt)) {
            q.emplace(p.x, p.y, p.z - 1);
        }
    }
    return result;
}

void World::SetWaterSurface(int x, int y, int z) {
    if (z == SeaLevel) {
        BlockType bt = GetBlockType(x, y, z);
        if (bt == BlockType::Water || HasBlockFlag(x, y, z, BlockFlag::IsWaterFilled)) {
            SetBlockFlag(x, y, z, BlockFlag::IsWaterSurface);
        }
    }
}

void World::RefillWaterFlags(int x, int y, int z, BlockType prevType, BlockType blockType) {
    if (blockType != BlockType::Water && !Block::IsFillable(blockType)) {
        ClearBlockFlag(x, y, z, BlockFlag::IsWaterFilled);
        ClearBlockFlag(x, y, z, BlockFlag::IsWaterSurface);
    }
    if (prevType == BlockType::Water && Block::IsFillable(blockType)) {
        SetBlockFlag(x, y, z, BlockFlag::IsWaterFilled);
    }
}

int World::GetGroundHeight(int blockX, int blockY) const {
    float cx = static_cast<float>(BlocksWide / 2);
    float cy = static_cast<float>(BlocksDeep / 2);
    float dx = static_cast<float>(blockX) - cx;
    float dy = static_cast<float>(blockY) - cy;
    float dist = std::sqrt(dx * dx + dy * dy);
    float maxDist = std::sqrt(cx * cx + cy * cy);
    double falloff = 0.5 - static_cast<double>(dist / maxDist);

    double nx = static_cast<double>(blockX) / 64.0;
    double ny = static_cast<double>(blockY) / 64.0;
    double n1 = perlin_.Noise(nx, ny, 0.0);
    double n2 = perlin_.Noise(nx * 2.0, ny * 2.0, 0.0);
    double n3 = perlin_.Noise(nx * 4.0, ny * 4.0, 0.0);
    double n4 = perlin_.Noise(nx * 8.0, ny * 8.0, 0.0);
    double n5 = perlin_.Noise(nx * 16.0, ny * 16.0, 0.0);
    double h = falloff * 16.0 + n1 * 16.0 + n2 * 8.0 + n3 * 4.0 + n4 * 2.0 + n5 * 1.0;
    return static_cast<int>(h) + SeaLevel;
}

void World::GenerateTerrain() {
    for (int cx = 0; cx < ChunksWide; ++cx) {
        for (int cy = 0; cy < ChunksDeep; ++cy) {
            Chunk& chunk = GetChunkRef(cx, cy);
            for (int bx = 0; bx < ChunkBlockWidth; ++bx) {
                int blockX = cx * ChunkBlockWidth + bx;
                for (int by = 0; by < ChunkBlockDepth; ++by) {
                    int blockY = cy * ChunkBlockDepth + by;
                    int groundH = GetGroundHeight(blockX, blockY);
                    if (groundH < 1) groundH = 1;
                    else if (groundH > BlocksHigh - 1) groundH = BlocksHigh - 1;
                    if (type_ == WorldType::Flat && groundH > SeaLevel) {
                        groundH = SeaLevel;
                    }
                    heightMap_[blockX * BlocksDeep + blockY] = groundH;

                    for (int z = BlocksHigh - 1; z >= 0; --z) {
                        BlockType bt = BlockType::Air;
                        if (z > groundH) {
                            bt = (z > SeaLevel) ? BlockType::Air : BlockType::Water;
                        } else if (z == groundH) {
                            if (z <= SeaLevel) {
                                bt = (z != SeaLevel) ? BlockType::Dirt : BlockType::Sand;
                            } else {
                                bt = BlockType::Grass;
                            }
                        } else if (static_cast<double>(z) > static_cast<double>(groundH) * 0.8) {
                            bt = BlockType::Dirt;
                        } else if (static_cast<double>(z) > static_cast<double>(groundH) * 0.4) {
                            if (static_cast<double>(blockX) > static_cast<double>(BlocksWide) * 0.25 &&
                                static_cast<double>(blockX) < static_cast<double>(BlocksWide) * 0.75 &&
                                static_cast<double>(blockY) > static_cast<double>(BlocksDeep) * 0.25 &&
                                static_cast<double>(blockY) < static_cast<double>(BlocksDeep) * 0.75) {
                                double nx = static_cast<double>(blockX) / 64.0;
                                double ny = static_cast<double>(blockY) / 64.0;
                                double nz = static_cast<double>(z) / 64.0;
                                double c1 = perlin_.Noise(nx, ny, nz * 5.0);
                                double c2 = perlin_.Noise(nx * 2.0, ny * 2.0, nz * 10.0);
                                double cave = c1 * 1.0 + c2 * 2.0;
                                bt = (cave > 0.5) ? BlockType::Air : BlockType::Stone;
                            } else {
                                bt = BlockType::Bedrock;
                            }
                        } else if (z > 0) {
                            bt = BlockType::Stone;
                        } else {
                            bt = BlockType::Lava;
                            chunk.emissionLights.emplace_back(blockX, blockY, z);
                        }
                        chunk.blocks[bx][by][z].type = bt;
                    }
                }
            }
        }
    }
}

void World::DecorateTerrain() {
    float cx = static_cast<float>(BlocksWide / 2);
    float cy = static_cast<float>(BlocksDeep / 2);
    float maxDist = std::sqrt(cx * cx + cy * cy);

    for (int x = 0; x < BlocksWide; ++x) {
        for (int y = 0; y < BlocksDeep; ++y) {
            int z = heightMap_[x * BlocksDeep + y];
            if (z > SeaLevel) {
                float dx = static_cast<float>(x) - cx;
                float dy = static_cast<float>(y) - cy;
                float dist = std::sqrt(dx * dx + dy * dy);
                double radial = static_cast<double>(dist / maxDist) - 0.5;
                double nx = static_cast<double>(x) / 64.0;
                double ny = static_cast<double>(y) / 64.0;
                double n1 = perlin_.Noise(nx, ny, 0.0);
                double n2 = perlin_.Noise(nx * 2.0, ny * 2.0, 0.0);
                double val = n1 * 4.0 + n2 * 2.0 + radial * 1.0;
                if (val > 0.3) {
                    CreateStandardTree(x, y, z);
                }
            }
        }
    }
}

bool World::CreateStandardTree(int x, int y, int z) {
    if (NextToBlockType(x, y, z, BlockType::Air)) {
        return false;
    }
    if (random_.Next(100) > 1) {
        return false;
    }
    int height = random_.Next(5, 8);
    if (GetBlockType(x, y, z + 1) != BlockType::Air ||
        GetBlockType(x, y, z + 2) != BlockType::Air ||
        GetBlockType(x, y, z + 3) != BlockType::Air ||
        !SpaceAroundIsEmpty(x, y, z + 4, 2, 2, height - 3)) {
        return false;
    }
    for (int i = z; i <= z + height; ++i) {
        SetBlockType(x, y, i, BlockType::Log, false);
        int rel = i - z;
        int canopyH = 1;
        if ((height < 7 && rel > 2) || (height >= 7 && rel > 3)) {
            CreateTreeBranch(x, y + 1, x, y + 2, x + 1, y + 1, x + 1, y + 2, x + 2, y + 1, x + 2, y + 2, i, canopyH);
            CreateTreeBranch(x + 1, y, x + 2, y, x + 1, y - 1, x + 2, y - 1, x + 1, y - 2, x + 2, y - 2, i, canopyH);
            CreateTreeBranch(x, y - 1, x, y - 2, x - 1, y - 1, x - 1, y - 2, x - 2, y - 1, x - 2, y - 2, i, canopyH);
            CreateTreeBranch(x - 1, y, x - 2, y, x - 1, y + 1, x - 2, y + 1, x - 1, y + 2, x - 2, y + 2, i, canopyH);
            canopyH++;
        }
    }
    CreateTreeTop(x, y, z + height + 1);
    return true;
}

void World::CreateTreeBranch(int x0, int y0, int x1, int y1, int x2, int y2, int x3, int y3, int x4, int y4, int x5, int y5, int z, int canopyHeight) {
    SetBlockType(x0, y0, z, BlockType::Leaves, false);
    if (canopyHeight != 1 && random_.Next(10) <= canopyHeight) {
        return;
    }
    SetBlockType(x1, y1, z, BlockType::Leaves, false);
    SetBlockType(x2, y2, z, BlockType::Leaves, false);
    if (random_.Next(8) > canopyHeight) {
        SetBlockType(x3, y3, z, BlockType::Leaves, false);
        SetBlockType(x4, y4, z, BlockType::Leaves, false);
        if (random_.Next(8) > canopyHeight) {
            SetBlockType(x5, y5, z, BlockType::Leaves, false);
        }
    }
}

void World::CreateTreeTop(int x, int y, int z) {
    SetBlockType(x, y, z, BlockType::Leaves, false);
    SetBlockType(x + 1, y, z, BlockType::Leaves, false);
    SetBlockType(x - 1, y, z, BlockType::Leaves, false);
    SetBlockType(x, y + 1, z, BlockType::Leaves, false);
    SetBlockType(x, y - 1, z, BlockType::Leaves, false);
}

void World::FillInitialWater() {
    for (int x = 0; x < BlocksWide; ++x) {
        for (int y = 0; y < BlocksDeep; ++y) {
            for (int z = 0; z <= SeaLevel; ++z) {
                if (!HasBlockFlag(x, y, z, BlockFlag::IsWaterFilled)) {
                    BlockType bt = GetBlockType(x, y, z);
                    if (Block::IsFillable(bt) && AdjacentToWater(x, y, z)) {
                        FillWithWater(x, y, z, false);
                    }
                }
            }
        }
    }
    for (int x = 0; x < BlocksWide; ++x) {
        for (int y = 0; y < BlocksDeep; ++y) {
            SetWaterSurface(x, y, SeaLevel);
        }
    }
}

int World::GetBlockLightLevel(int x, int y, int z, BlockLightType lightType) const {
    if (x < 0 || y < 0 || z < 0 || x >= BlocksWide || y >= BlocksDeep) {
        return 0;
    }
    if (z >= BlocksHigh) {
        return (lightType == BlockLightType::Sky) ? 15 : 0;
    }
    int cx = x / ChunkBlockWidth;
    int cy = y / ChunkBlockDepth;
    int bx = x % ChunkBlockWidth;
    int by = y % ChunkBlockDepth;
    const Block& b = GetChunkRef(cx, cy).blocks[bx][by][z];
    return (lightType == BlockLightType::Sky) ? b.skyLightLevel() : b.emissionLightLevel();
}

void World::SetBlockLightLevel(int x, int y, int z, BlockLightType lightType, int lightLevel, bool doRegeneration, bool doPropagation) {
    if (x < 0 || y < 0 || z < 0 || x >= BlocksWide || y >= BlocksDeep || z >= BlocksHigh) {
        return;
    }
    struct Node { int x, y, z, lvl; };
    std::queue<Node> q;
    q.push({x, y, z, lightLevel});

    while (!q.empty()) {
        Node n = q.front();
        q.pop();
        if (n.lvl <= 0 || n.x < 0 || n.y < 0 || n.z < 0 || n.x >= BlocksWide || n.y >= BlocksDeep || n.z >= BlocksHigh) {
            continue;
        }
        int cx = n.x / ChunkBlockWidth;
        int cy = n.y / ChunkBlockDepth;
        int bx = n.x % ChunkBlockWidth;
        int by = n.y % ChunkBlockDepth;
        Block& b = GetChunkRef(cx, cy).blocks[bx][by][n.z];
        int cur = (lightType == BlockLightType::Sky) ? b.skyLightLevel() : b.emissionLightLevel();
        if (cur < n.lvl && (Block::IsTransparent(b.type) || Block::IsPartialBlock(b.type))) {
            if (lightType == BlockLightType::Sky) b.setSkyLightLevel(n.lvl);
            else b.setEmissionLightLevel(n.lvl);

            if (doRegeneration) {
                RegenerateChunks(cx, cy, bx, by);
            }
            if (doPropagation && n.lvl > 1) {
                int nl = n.lvl - 1;
                if (n.x < BlocksWide - 1) q.push({n.x + 1, n.y, n.z, nl});
                if (n.x > 0)              q.push({n.x - 1, n.y, n.z, nl});
                if (n.y < BlocksDeep - 1) q.push({n.x, n.y + 1, n.z, nl});
                if (n.y > 0)              q.push({n.x, n.y - 1, n.z, nl});
                if (n.z < BlocksHigh - 1 && !Block::IsPartialBlock(GetBlockType(n.x, n.y, n.z + 1))) {
                    q.push({n.x, n.y, n.z + 1, nl});
                }
                if (n.z > 0 && !Block::IsPartialBlock(b.type)) {
                    q.push({n.x, n.y, n.z - 1, nl});
                }
            }
        }
    }
}

void World::SetLightAroundBlock(int x, int y, int z, BlockLightType lightType, bool doRegeneration) {
    int lvl = GetBlockLightLevel(x, y, z, lightType) - 1;
    if (lightType == BlockLightType::Emission) {
        int em = Block::GetEmissionLightSourceLevel(GetBlockType(x, y, z));
        if (em > lvl) lvl = em;
    }
    if (lvl > 0) {
        SetLightAroundBlock(x, y, z, lightType, lvl, doRegeneration);
    }
}

void World::SetLightAroundBlock(int x, int y, int z, BlockLightType lightType, int lightLevel, bool doRegeneration) {
    if (x < BlocksWide - 1) SetBlockLightLevel(x + 1, y, z, lightType, lightLevel, doRegeneration, true);
    if (x > 0)              SetBlockLightLevel(x - 1, y, z, lightType, lightLevel, doRegeneration, true);
    if (y < BlocksDeep - 1) SetBlockLightLevel(x, y + 1, z, lightType, lightLevel, doRegeneration, true);
    if (y > 0)              SetBlockLightLevel(x, y - 1, z, lightType, lightLevel, doRegeneration, true);
    if (z < BlocksHigh - 1 && !Block::IsPartialBlock(GetBlockType(x, y, z + 1))) {
        SetBlockLightLevel(x, y, z + 1, lightType, lightLevel, doRegeneration, true);
    }
    if (z > 0 && !Block::IsPartialBlock(GetBlockType(x, y, z))) {
        SetBlockLightLevel(x, y, z - 1, lightType, lightLevel, doRegeneration, true);
    }
}

void World::ResetBlockLightLevel(int x, int y, int z, BlockLightType lightType) {
    int lvl = GetBlockLightLevel(x, y, z, lightType);
    ResetBlockLightLevel(x, y, z, lightType, lvl, true);
}

void World::ResetBlockLightLevel(int x, int y, int z, BlockLightType lightType, int lightLevel, bool doRegeneration) {
    struct Node { int x, y, z, lvl; };
    std::queue<Node> q;
    q.push({x, y, z, lightLevel});

    while (!q.empty()) {
        Node n = q.front();
        q.pop();
        if (n.x < 0 || n.y < 0 || n.z < 0 || n.x >= BlocksWide || n.y >= BlocksDeep || n.z >= BlocksHigh) {
            continue;
        }
        int cx = n.x / ChunkBlockWidth;
        int cy = n.y / ChunkBlockDepth;
        int bx = n.x % ChunkBlockWidth;
        int by = n.y % ChunkBlockDepth;
        Block& b = GetChunkRef(cx, cy).blocks[bx][by][n.z];
        int cur = (lightType == BlockLightType::Sky) ? b.skyLightLevel() : b.emissionLightLevel();
        if (cur != n.lvl) {
            if (cur > 0) {
                if (lightType == BlockLightType::Sky) pendingSkyLights_.emplace_back(n.x, n.y, n.z);
                else pendingEmissionLights_.emplace_back(n.x, n.y, n.z);
            }
            continue;
        }
        if (cur == 0) continue;

        if (lightType == BlockLightType::Sky) b.setSkyLightLevel(0);
        else b.setEmissionLightLevel(0);

        if (doRegeneration) {
            RegenerateChunks(cx, cy, bx, by);
        }
        if (n.lvl > 1) {
            int nl = n.lvl - 1;
            if (n.x < BlocksWide - 1) q.push({n.x + 1, n.y, n.z, nl});
            if (n.x > 0)              q.push({n.x - 1, n.y, n.z, nl});
            if (n.y < BlocksDeep - 1) q.push({n.x, n.y + 1, n.z, nl});
            if (n.y > 0)              q.push({n.x, n.y - 1, n.z, nl});
            if (n.z < BlocksHigh - 1) q.push({n.x, n.y, n.z + 1, nl});
            if (n.z > 0)              q.push({n.x, n.y, n.z - 1, nl});
        }
    }
}

void World::ResetLightAroundBlock(int x, int y, int z, BlockLightType lightType, int lightLevel, bool doRegeneration) {
    if (x < BlocksWide - 1) ResetBlockLightLevel(x + 1, y, z, lightType, lightLevel, doRegeneration);
    if (x > 0)              ResetBlockLightLevel(x - 1, y, z, lightType, lightLevel, doRegeneration);
    if (y < BlocksDeep - 1) ResetBlockLightLevel(x, y + 1, z, lightType, lightLevel, doRegeneration);
    if (y > 0)              ResetBlockLightLevel(x, y - 1, z, lightType, lightLevel, doRegeneration);
    if (z < BlocksHigh - 1) ResetBlockLightLevel(x, y, z + 1, lightType, lightLevel, doRegeneration);
    if (z > 0)              ResetBlockLightLevel(x, y, z - 1, lightType, lightLevel, doRegeneration);
}

void World::GenerateInitialLight() {
    for (int cx = 0; cx < ChunksWide; ++cx) {
        for (int cy = 0; cy < ChunksDeep; ++cy) {
            for (int bx = 0; bx < ChunkBlockWidth; ++bx) {
                int x = cx * ChunkBlockWidth + bx;
                for (int by = 0; by < ChunkBlockDepth; ++by) {
                    int y = cy * ChunkBlockDepth + by;
                    AddSkyLightSource(x, y, ChunkBlockHeight - 1, false);
                }
            }
        }
    }
    PropagateEmissionLightSources();
}

void World::AddSkyLightSource(int x, int y, int z, bool doRegeneration) {
    int lvl = 15;
    while (z >= 0 && Block::IsSkyLightSourceable(GetBlockType(x, y, z))) {
        if (GetBlockType(x, y, z) == BlockType::Water && lvl > 0) {
            lvl--;
        }
        SetBlockLightLevel(x, y, z, BlockLightType::Sky, lvl, doRegeneration, false);
        z--;
    }
}

void World::RemoveSkyLightSource(int x, int y, int z) {
    do {
        ResetBlockLightLevel(x, y, z, BlockLightType::Sky);
        z--;
    } while (z >= 0 && Block::IsSkyLightSourceable(GetBlockType(x, y, z)));
}

void World::PropagateSkyLightSource(int x, int y, int z, bool doRegeneration) {
    while (z >= 0 && Block::IsSkyLightSourceable(GetBlockType(x, y, z))) {
        SetLightAroundBlock(x, y, z, BlockLightType::Sky, doRegeneration);
        z--;
    }
}

void World::PropagateChunkSkyLightSources(Chunk& chunk, bool doRegeneration) {
    for (int bx = 0; bx < ChunkBlockWidth; ++bx) {
        int x = chunk.cx * ChunkBlockWidth + bx;
        for (int by = 0; by < ChunkBlockDepth; ++by) {
            int y = chunk.cy * ChunkBlockDepth + by;
            PropagateSkyLightSource(x, y, ChunkBlockHeight - 1, doRegeneration);
        }
    }
}

void World::PropagateEmissionLightSources() {
    for (int cx = 0; cx < ChunksWide; ++cx) {
        for (int cy = 0; cy < ChunksDeep; ++cy) {
            Chunk& chunk = GetChunkRef(cx, cy);
            for (const auto& p : chunk.emissionLights) {
                SetLightAroundBlock(p.x, p.y, p.z, BlockLightType::Emission, false);
            }
            chunk.emissionLights.clear();
        }
    }
}

void World::RegenerateBlockLight(int x, int y, int z, BlockType prevType, BlockType blockType) {
    if (GetBlockLightLevel(x, y, z + 1, BlockLightType::Sky) == 15) {
        if (Block::IsSkyLightSourceable(blockType)) {
            AddSkyLightSource(x, y, z, true);
            PropagateSkyLightSource(x, y, z, true);
        } else {
            RemoveSkyLightSource(x, y, z);
        }
    } else if (Block::IsTransparent(blockType) || Block::IsPartialBlock(blockType)) {
        SetLightAroundBlock(x + 1, y, z, BlockLightType::Sky, true);
        SetLightAroundBlock(x - 1, y, z, BlockLightType::Sky, true);
        SetLightAroundBlock(x, y + 1, z, BlockLightType::Sky, true);
        SetLightAroundBlock(x, y - 1, z, BlockLightType::Sky, true);
        SetLightAroundBlock(x, y, z + 1, BlockLightType::Sky, true);
        SetLightAroundBlock(x, y, z - 1, BlockLightType::Sky, true);
    } else {
        ResetBlockLightLevel(x, y, z, BlockLightType::Sky);
    }

    if (Block::IsTransparent(blockType) || Block::IsPartialBlock(blockType)) {
        SetLightAroundBlock(x + 1, y, z, BlockLightType::Emission, true);
        SetLightAroundBlock(x - 1, y, z, BlockLightType::Emission, true);
        SetLightAroundBlock(x, y + 1, z, BlockLightType::Emission, true);
        SetLightAroundBlock(x, y - 1, z, BlockLightType::Emission, true);
        SetLightAroundBlock(x, y, z + 1, BlockLightType::Emission, true);
        SetLightAroundBlock(x, y, z - 1, BlockLightType::Emission, true);
    } else {
        ResetBlockLightLevel(x, y, z, BlockLightType::Emission);
    }

    int emNew = Block::GetEmissionLightSourceLevel(blockType);
    int emOld = Block::GetEmissionLightSourceLevel(prevType);
    if (emNew > 0) {
        SetLightAroundBlock(x, y, z, BlockLightType::Emission, emNew, true);
    }
    if (emOld > 0) {
        ResetLightAroundBlock(x, y, z, BlockLightType::Emission, emOld, true);
    }
    PropagatePendingLight(true);
}

void World::PropagatePendingLight(bool doRegeneration) {
    auto sky = std::move(pendingSkyLights_);
    auto em = std::move(pendingEmissionLights_);
    pendingSkyLights_.clear();
    pendingEmissionLights_.clear();
    for (const auto& p : sky) {
        SetLightAroundBlock(p.x, p.y, p.z, BlockLightType::Sky, doRegeneration);
    }
    for (const auto& p : em) {
        SetLightAroundBlock(p.x, p.y, p.z, BlockLightType::Emission, doRegeneration);
    }
}

void World::MarkChunkDirty(int cx, int cy) {
    if (cx >= 0 && cx < ChunksWide && cy >= 0 && cy < ChunksDeep) {
        GetChunkRef(cx, cy).meshDirty = true;
    }
}

void World::RegenerateChunks(int cx, int cy, int bx, int by) {
    MarkChunkDirty(cx, cy);
    if (bx == 0 && cx > 0) MarkChunkDirty(cx - 1, cy);
    else if (bx == ChunkBlockWidth - 1 && cx < ChunksWide - 1) MarkChunkDirty(cx + 1, cy);
    if (by == 0 && cy > 0) MarkChunkDirty(cx, cy - 1);
    else if (by == ChunkBlockDepth - 1 && cy < ChunksDeep - 1) MarkChunkDirty(cx, cy + 1);
}

float World::GetBlockRenderHeight(int x, int y, int z, BlockType blockType) const {
    if (blockType == BlockType::Water && !HasBlockFlag(x, y, z, BlockFlag::IsWaterSurface)) {
        return 1.0f;
    }
    return Block::GetBlockHeight(blockType);
}

bool World::IsBlockSideValid(BlockType blockType, BlockType neighborBlockType) {
    if (blockType == BlockType::Null || blockType == BlockType::Air) return false;
    if (blockType == BlockType::Water) {
        if (Block::IsFillable(neighborBlockType)) return false;
    }
    if (blockType == neighborBlockType && !Block::IsPartialBlock(blockType) && blockType != BlockType::Table) {
        return false;
    }
    return true;
}

void World::AddBlockSide(float x1, float y1, float z1, float x2, float y2, float z2,
                         float x3, float y3, float z3, float x4, float y4, float z4,
                         Chunk& chunk, BlockType blockType, BlockType neighborBlockType,
                         BlockFace blockFace, float blockFaceColor, int skyLight, int emissionLight) {
    if (!IsBlockSideValid(blockType, neighborBlockType)) return;
    if (blockType == BlockType::Water) {
        AddWaterSide(x1, y1, z1, x2, y2, z2, x3, y3, z3, x4, y4, z4, chunk, 1.0f, skyLight, emissionLight, false);
        return;
    }

    auto& verts = Block::IsTransparent(blockType) ? chunk.transVerts : chunk.landVerts;
    auto& indices = Block::IsTransparent(blockType) ? chunk.transIndices : chunk.landIndices;

    float skyC = blockFaceColor * lightAmounts_[std::clamp(skyLight, 0, 15)];
    float emC = blockFaceColor * lightAmounts_[std::clamp(emissionLight, 0, 15)];

    int texIdx = GetBlockTextureIndex(blockType, blockFace);
    const Rect& rect = atlasUvs_[texIdx];
    float epsU = rect.w * 0.002f;
    float epsV = rect.h * 0.002f;

    float u0 = rect.x + epsU;
    float v0 = rect.y + epsV;
    float u1 = rect.x + rect.w - epsU;
    float v1 = rect.y + rect.h - epsV;

    if (blockFace == BlockFace::Side) {
        float bh = Block::GetBlockHeight(blockType);
        v1 = rect.y + (rect.h - 2.0f * epsV) * bh + epsV;
    }

    float ox = static_cast<float>(chunk.cx * ChunkBlockWidth);
    float oy = static_cast<float>(chunk.cy * ChunkBlockDepth);

    uint32_t base = static_cast<uint32_t>(verts.size());
    verts.push_back({ox + x1, z1, oy + y1, u0, v0, skyC, skyC, skyC, emC});
    verts.push_back({ox + x2, z2, oy + y2, u0, v1, skyC, skyC, skyC, emC});
    verts.push_back({ox + x3, z3, oy + y3, u1, v1, skyC, skyC, skyC, emC});
    verts.push_back({ox + x4, z4, oy + y4, u1, v0, skyC, skyC, skyC, emC});

    indices.push_back(base);
    indices.push_back(base + 1);
    indices.push_back(base + 2);
    indices.push_back(base + 2);
    indices.push_back(base + 3);
    indices.push_back(base);
}

void World::AddWaterSide(float x1, float y1, float z1, float x2, float y2, float z2,
                         float x3, float y3, float z3, float x4, float y4, float z4,
                         Chunk& chunk, float blockFaceColor, int skyLight, int emissionLight, bool addBackSide) {
    float ox = static_cast<float>(chunk.cx * ChunkBlockWidth);
    float oy = static_cast<float>(chunk.cy * ChunkBlockDepth);

    float skyC = blockFaceColor * lightAmounts_[std::clamp(skyLight, 0, 15)];
    float emC = blockFaceColor * lightAmounts_[std::clamp(emissionLight, 0, 15)];

    uint32_t base = static_cast<uint32_t>(chunk.waterVerts.size());
    chunk.waterVerts.push_back({ox + x1, z1, oy + y1, skyC, skyC, skyC, emC});
    chunk.waterVerts.push_back({ox + x2, z2, oy + y2, skyC, skyC, skyC, emC});
    chunk.waterVerts.push_back({ox + x3, z3, oy + y3, skyC, skyC, skyC, emC});
    chunk.waterVerts.push_back({ox + x4, z4, oy + y4, skyC, skyC, skyC, emC});

    chunk.waterIndices.push_back(base);
    chunk.waterIndices.push_back(base + 1);
    chunk.waterIndices.push_back(base + 2);
    chunk.waterIndices.push_back(base + 2);
    chunk.waterIndices.push_back(base + 3);
    chunk.waterIndices.push_back(base);

    if (addBackSide) {
        AddWaterSide(x3, y3, z3, x2, y2, z2, x1, y1, z1, x4, y4, z4, chunk, blockFaceColor * 0.5f, skyLight, emissionLight, false);
    }
}

void World::BuildChunkMesh(Chunk& chunk) {
    chunk.landVerts.clear();
    chunk.landIndices.clear();
    chunk.transVerts.clear();
    chunk.transIndices.clear();
    chunk.waterVerts.clear();
    chunk.waterIndices.clear();

    for (int x = 0; x < ChunkBlockWidth; ++x) {
        int blockX = chunk.cx * ChunkBlockWidth + x;
        for (int y = 0; y < ChunkBlockDepth; ++y) {
            int blockY = chunk.cy * ChunkBlockDepth + y;
            for (int z = 0; z < ChunkBlockHeight; ++z) {
                int blockZ = z;
                const Block& blk = chunk.blocks[x][y][z];
                BlockType type = blk.type;
                if (!Block::IsTransparent(type) && !Block::IsPartialBlock(type)) {
                    continue;
                }
                int skyL = blk.skyLightLevel();
                int emL = blk.emissionLightLevel();
                if (Block::GetBlockHeight(type) < 1.0f) {
                    int aboveSky = GetBlockLightLevel(blockX, blockY, blockZ + 1, BlockLightType::Sky);
                    if (aboveSky > skyL) skyL = aboveSky;
                }

                float fx = static_cast<float>(x);
                float fy = static_cast<float>(y);
                float fz = static_cast<float>(z);

                if (blockY > 0) {
                    BlockType nb = GetBlockType(blockX, blockY - 1, blockZ);
                    float bh = GetBlockRenderHeight(blockX, blockY, blockZ, nb);
                    AddBlockSide(fx + 1, fy, fz, fx + 1, fy, fz + bh, fx, fy, fz + bh, fx, fy, fz,
                                 chunk, nb, type, BlockFace::Side, 0.65f, skyL, emL);
                }
                if (blockX > 0) {
                    BlockType nb = GetBlockType(blockX - 1, blockY, blockZ);
                    float bh = GetBlockRenderHeight(blockX, blockY, blockZ, nb);
                    AddBlockSide(fx, fy, fz, fx, fy, fz + bh, fx, fy + 1, fz + bh, fx, fy + 1, fz,
                                 chunk, nb, type, BlockFace::Side, 0.75f, skyL, emL);
                }
                if (blockY < BlocksDeep - 1) {
                    BlockType nb = GetBlockType(blockX, blockY + 1, blockZ);
                    float bh = GetBlockRenderHeight(blockX, blockY, blockZ, nb);
                    AddBlockSide(fx, fy + 1, fz, fx, fy + 1, fz + bh, fx + 1, fy + 1, fz + bh, fx + 1, fy + 1, fz,
                                 chunk, nb, type, BlockFace::Side, 0.65f, skyL, emL);
                }
                if (blockX < BlocksWide - 1) {
                    BlockType nb = GetBlockType(blockX + 1, blockY, blockZ);
                    float bh = GetBlockRenderHeight(blockX, blockY, blockZ, nb);
                    AddBlockSide(fx + 1, fy + 1, fz, fx + 1, fy + 1, fz + bh, fx + 1, fy, fz + bh, fx + 1, fy, fz,
                                 chunk, nb, type, BlockFace::Side, 0.75f, skyL, emL);
                }
                if (blockZ < BlocksHigh - 1) {
                    BlockType nb = GetBlockType(blockX, blockY, blockZ + 1);
                    AddBlockSide(fx + 1, fy, fz + 1, fx + 1, fy + 1, fz + 1, fx, fy + 1, fz + 1, fx, fy, fz + 1,
                                 chunk, nb, type, BlockFace::Bottom, 0.55f, skyL, emL);
                }
                if (blockZ > 0) {
                    BlockType nb = GetBlockType(blockX, blockY, blockZ - 1);
                    float bh = GetBlockRenderHeight(blockX, blockY, blockZ, nb);
                    if (bh >= 1.0f) {
                        AddBlockSide(fx, fy, fz - 1 + bh, fx, fy + 1, fz - 1 + bh, fx + 1, fy + 1, fz - 1 + bh, fx + 1, fy, fz - 1 + bh,
                                     chunk, nb, type, BlockFace::Top, 0.85f, skyL, emL);
                    }
                }

                float ownH = Block::GetBlockHeight(type);
                if (Block::IsPartialBlock(type)) {
                    AddBlockSide(fx, fy, fz + ownH, fx, fy + 1, fz + ownH, fx + 1, fy + 1, fz + ownH, fx + 1, fy, fz + ownH,
                                 chunk, type, BlockType::Air, BlockFace::Top, 0.85f, skyL, emL);
                }
                if (HasBlockFlag(blockX, blockY, blockZ, BlockFlag::IsWaterSurface)) {
                    float wh = Block::GetBlockHeight(BlockType::Water);
                    AddWaterSide(fx, fy, fz + wh, fx, fy + 1, fz + wh, fx + 1, fy + 1, fz + wh, fx + 1, fy, fz + wh,
                                 chunk, 1.0f, skyL, emL, true);
                }
                if (Block::IsDoubleSided(type)) {
                    float eps = 0.01f;
                    AddBlockSide(fx + 1, fy + eps, fz, fx + 1, fy + eps, fz + ownH, fx, fy + eps, fz + ownH, fx, fy + eps, fz,
                                 chunk, type, BlockType::Air, BlockFace::Side, 0.6f, skyL, emL);
                    AddBlockSide(fx + eps, fy, fz, fx + eps, fy, fz + ownH, fx + eps, fy + 1, fz + ownH, fx + eps, fy + 1, fz,
                                 chunk, type, BlockType::Air, BlockFace::Side, 0.7f, skyL, emL);
                    AddBlockSide(fx, fy + 1 - eps, fz, fx, fy + 1 - eps, fz + ownH, fx + 1, fy + 1 - eps, fz + ownH, fx + 1, fy + 1 - eps, fz,
                                 chunk, type, BlockType::Air, BlockFace::Side, 0.6f, skyL, emL);
                    AddBlockSide(fx + 1 - eps, fy + 1, fz, fx + 1 - eps, fy + 1, fz + ownH, fx + 1 - eps, fy, fz + ownH, fx + 1 - eps, fy, fz,
                                 chunk, type, BlockType::Air, BlockFace::Side, 0.7f, skyL, emL);
                    AddBlockSide(fx, fy, fz + eps, fx, fy + 1, fz + eps, fx + 1, fy + 1, fz + eps, fx + 1, fy, fz + eps,
                                 chunk, type, BlockType::Air, BlockFace::Bottom, 0.8f, skyL, emL);
                    AddBlockSide(fx + 1, fy, fz + ownH - eps, fx + 1, fy + 1, fz + ownH - eps, fx, fy + 1, fz + ownH - eps, fx, fy, fz + ownH - eps,
                                 chunk, type, BlockType::Air, BlockFace::Top, 0.5f, skyL, emL);
                }
            }
        }
    }

    chunk.meshDirty = false;
    chunk.gpuDirty = true;
}

int World::BuildDirtyChunksAround(float playerX, float playerZ, int maxPerFrame) {
    struct DirtyRef {
        int idx;
        float distSq;
    };
    std::vector<DirtyRef> dirty;
    dirty.reserve(64);
    for (int cx = 0; cx < ChunksWide; ++cx) {
        for (int cy = 0; cy < ChunksDeep; ++cy) {
            int idx = cx * ChunksDeep + cy;
            if (chunks_[idx]->meshDirty) {
                float dx = (cx * ChunkBlockWidth + 4.0f) - playerX;
                float dz = (cy * ChunkBlockDepth + 4.0f) - playerZ;
                dirty.push_back({idx, dx * dx + dz * dz});
            }
        }
    }
    if (dirty.empty()) return 0;

    int count = std::min(static_cast<int>(dirty.size()), maxPerFrame);
    std::partial_sort(dirty.begin(), dirty.begin() + count, dirty.end(),
                      [](const DirtyRef& a, const DirtyRef& b) { return a.distSq < b.distSq; });

    for (int i = 0; i < count; ++i) {
        BuildChunkMesh(*chunks_[dirty[i].idx]);
    }
    return count;
}

void World::BuildOceanMesh(std::vector<WaterVertex>& outVerts, std::vector<uint32_t>& outIndices) const {
    outVerts.clear();
    outIndices.clear();

    auto addPlane = [&](int x1, int y1, int x2, int y2, int x3, int y3, int x4, int y4, int height, float planeColor, bool otherSide, auto& self) -> void {
        uint32_t base = static_cast<uint32_t>(outVerts.size());
        float wy = static_cast<float>(height) + Block::GetBlockHeight(BlockType::Water) - 1.0f;
        float c = lightAmounts_[15] * planeColor;
        outVerts.push_back({static_cast<float>(x1), wy, static_cast<float>(y1), c, c, c, 1.0f});
        outVerts.push_back({static_cast<float>(x2), wy, static_cast<float>(y2), c, c, c, 1.0f});
        outVerts.push_back({static_cast<float>(x3), wy, static_cast<float>(y3), c, c, c, 1.0f});
        outVerts.push_back({static_cast<float>(x4), wy, static_cast<float>(y4), c, c, c, 1.0f});
        outIndices.push_back(base);
        outIndices.push_back(base + 1);
        outIndices.push_back(base + 2);
        outIndices.push_back(base + 2);
        outIndices.push_back(base + 3);
        outIndices.push_back(base);
        if (!otherSide) {
            self(x3, y3, x2, y2, x1, y1, x4, y4, height, 0.5f, true, self);
        }
    };

    int h = SeaLevel + 1;
    int bw = BlocksWide, bd = BlocksDeep, od = OceanDistance;
    addPlane(0, -od, 0, 0, bw, 0, bw, -od, h, 1.0f, false, addPlane);
    addPlane(-od, bd, 0, bd, 0, 0, -od, 0, h, 1.0f, false, addPlane);
    addPlane(bw, bd + od, bw, bd, 0, bd, 0, bd + od, h, 1.0f, false, addPlane);
    addPlane(bw + od, 0, bw, 0, bw, bd, bw + od, bd, h, 1.0f, false, addPlane);
    addPlane(-od, 0, 0, 0, 0, -od, -od, -od, h, 1.0f, false, addPlane);
    addPlane(0, bd + od, 0, bd, -od, bd, -od, bd + od, h, 1.0f, false, addPlane);
    addPlane(bw + od, bd, bw, bd, bw, bd + od, bw + od, bd + od, h, 1.0f, false, addPlane);
    addPlane(bw, -od, bw, 0, bw + od, 0, bw + od, -od, h, 1.0f, false, addPlane);
}

void World::BuildOceanFloorMesh(std::vector<TerrainVertex>& outVerts, std::vector<uint32_t>& outIndices) const {
    outVerts.clear();
    outIndices.clear();

    const Rect& rect = atlasUvs_[GetBlockTextureIndex(BlockType::Stone, BlockFace::Side)];
    float c = lightAmounts_[0];

    auto addPlane = [&](int x1, int y1, int z1, int x2, int y2, int z2, int x3, int y3, int z3, int x4, int y4, int z4) {
        uint32_t base = static_cast<uint32_t>(outVerts.size());
        outVerts.push_back({static_cast<float>(x1), static_cast<float>(z1), static_cast<float>(y1), rect.x, rect.y, c, c, c, c});
        outVerts.push_back({static_cast<float>(x2), static_cast<float>(z2), static_cast<float>(y2), rect.x, rect.y + rect.h, c, c, c, c});
        outVerts.push_back({static_cast<float>(x3), static_cast<float>(z3), static_cast<float>(y3), rect.x + rect.w, rect.y + rect.h, c, c, c, c});
        outVerts.push_back({static_cast<float>(x4), static_cast<float>(z4), static_cast<float>(y4), rect.x + rect.w, rect.y, c, c, c, c});
        outIndices.push_back(base);
        outIndices.push_back(base + 1);
        outIndices.push_back(base + 2);
        outIndices.push_back(base + 2);
        outIndices.push_back(base + 3);
        outIndices.push_back(base);
    };

    int h = SeaLevel + 1;
    int bw = BlocksWide, bd = BlocksDeep, od = OceanDistance;
    addPlane(-od, -od, 0, -od, bd + od, 0, bw + od, bd + od, 0, bw + od, -od, 0);
    addPlane(bw + od, -od, h, -od, -od, h, -od, -od, 0, bw + od, -od, 0);
    addPlane(-od, -od, h, -od, bd + od, h, -od, bd + od, 0, -od, -od, 0);
    addPlane(-od, bd + od, 0, -od, bd + od, h, bw + od, bd + od, h, bw + od, bd + od, 0);
    addPlane(bw + od, bd + od, 0, bw + od, bd + od, h, bw + od, -od, h, bw + od, -od, 0);
}

RaycastHit World::Raycast(const Vec3& origin, const Vec3& dir, float maxDist) const {
    RaycastHit result;
    Vec3 d = dir.normalized();
    if (d.lengthSq() < 1e-6f) return result;

    int x = static_cast<int>(std::floor(origin.x));
    int y = static_cast<int>(std::floor(origin.z));
    int z = static_cast<int>(std::floor(origin.y));

    int stepX = (d.x > 0) ? 1 : ((d.x < 0) ? -1 : 0);
    int stepY = (d.z > 0) ? 1 : ((d.z < 0) ? -1 : 0);
    int stepZ = (d.y > 0) ? 1 : ((d.y < 0) ? -1 : 0);

    auto intBound = [](float s, float ds) {
        if (ds > 0.0f) return (std::floor(s + 1.0f) - s) / ds;
        if (ds < 0.0f) return (s - std::floor(s)) / (-ds);
        return 1e30f;
    };

    float tMaxX = intBound(origin.x, d.x);
    float tMaxY = intBound(origin.z, d.z);
    float tMaxZ = intBound(origin.y, d.y);

    float tDeltaX = (stepX != 0) ? std::abs(1.0f / d.x) : 1e30f;
    float tDeltaY = (stepY != 0) ? std::abs(1.0f / d.z) : 1e30f;
    float tDeltaZ = (stepZ != 0) ? std::abs(1.0f / d.y) : 1e30f;

    float t = 0.0f;
    IntVector3 prev(x, y, z);
    Vec3 lastNormal(0, 0, 0);

    for (int iter = 0; iter < 128 && t <= maxDist; ++iter) {
        BlockType bt = GetBlockType(x, y, z);
        if (bt != BlockType::Null && bt != BlockType::Air && bt != BlockType::Water) {
            float bh = Block::GetBlockHeight(bt);
            float minX = static_cast<float>(x), maxX = minX + 1.0f;
            float minY = static_cast<float>(z), maxY = minY + bh;
            float minZ = static_cast<float>(y), maxZ = minZ + 1.0f;

            float tNear = -1e30f, tFar = 1e30f;
            Vec3 hitN(0, 0, 0);
            auto testSlab = [&](float o, float v, float bmin, float bmax, Vec3 nMin, Vec3 nMax) -> bool {
                if (std::abs(v) < 1e-7f) {
                    return (o >= bmin && o <= bmax);
                }
                float t1 = (bmin - o) / v;
                float t2 = (bmax - o) / v;
                Vec3 n1 = nMin;
                if (t1 > t2) { std::swap(t1, t2); n1 = nMax; }
                if (t1 > tNear) { tNear = t1; hitN = n1; }
                if (t2 < tFar) tFar = t2;
                return tNear <= tFar;
            };

            if (testSlab(origin.x, d.x, minX, maxX, {-1, 0, 0}, {1, 0, 0}) &&
                testSlab(origin.y, d.y, minY, maxY, {0, -1, 0}, {0, 1, 0}) &&
                testSlab(origin.z, d.z, minZ, maxZ, {0, 0, -1}, {0, 0, 1}) &&
                tFar >= 0.0f && tNear <= maxDist) {
                float th = (tNear >= 0.0f) ? tNear : 0.0f;
                result.hit = true;
                result.blockPos = {x, y, z};
                result.hitPoint = origin + d * th;
                result.normal = (hitN.lengthSq() > 0.5f) ? hitN : lastNormal;
                result.placePos = {
                    x + static_cast<int>(std::round(result.normal.x)),
                    y + static_cast<int>(std::round(result.normal.z)),
                    z + static_cast<int>(std::round(result.normal.y))
                };
                return result;
            }
        }

        prev = {x, y, z};
        if (tMaxX < tMaxY && tMaxX < tMaxZ) {
            t = tMaxX;
            x += stepX;
            tMaxX += tDeltaX;
            lastNormal = {static_cast<float>(-stepX), 0, 0};
        } else if (tMaxY < tMaxZ) {
            t = tMaxY;
            y += stepY;
            tMaxY += tDeltaY;
            lastNormal = {0, 0, static_cast<float>(-stepY)};
        } else {
            t = tMaxZ;
            z += stepZ;
            tMaxZ += tDeltaZ;
            lastNormal = {0, static_cast<float>(-stepZ), 0};
        }
    }
    return result;
}

bool World::TryBuildBlock(const IntVector3& pos, BlockType type, const Vec3& playerPos, float playerRadius, float playerHeight, std::vector<WorldUpdateEntry>& outUpdates) {
    if (pos.x < 0 || pos.y < 0 || pos.z <= 0 || pos.x >= BlocksWide || pos.y >= BlocksDeep || pos.z >= BlocksHigh - 1) {
        return false;
    }
    BlockType existing = GetBlockType(pos.x, pos.y, pos.z);
    if (existing != BlockType::Air && existing != BlockType::Water) {
        return false;
    }

    float pMinX = playerPos.x - playerRadius, pMaxX = playerPos.x + playerRadius;
    float pMinY = playerPos.y,                pMaxY = playerPos.y + playerHeight;
    float pMinZ = playerPos.z - playerRadius, pMaxZ = playerPos.z + playerRadius;

    float bMinX = static_cast<float>(pos.x), bMaxX = bMinX + 1.0f;
    float bMinY = static_cast<float>(pos.z), bMaxY = bMinY + Block::GetBlockHeight(type);
    float bMinZ = static_cast<float>(pos.y), bMaxZ = bMinZ + 1.0f;

    if (pMinX < bMaxX && pMaxX > bMinX && pMinY < bMaxY && pMaxY > bMinY && pMinZ < bMaxZ && pMaxZ > bMinZ) {
        return false;
    }

    SetBlockType(pos.x, pos.y, pos.z, type, true);
    outUpdates.push_back({pos.x, pos.y, pos.z, type});
    return true;
}

bool World::TryDestroyBlock(const IntVector3& pos, BlockType& outDestroyedType, std::vector<WorldUpdateEntry>& outUpdates) {
    if (pos.x < 0 || pos.y < 0 || pos.z <= 0 || pos.x >= BlocksWide || pos.y >= BlocksDeep || pos.z >= BlocksHigh - 1) {
        return false;
    }
    outDestroyedType = GetBlockType(pos.x, pos.y, pos.z);
    if (outDestroyedType == BlockType::Air || outDestroyedType == BlockType::Water || outDestroyedType == BlockType::Null) {
        return false;
    }

    SetBlockType(pos.x, pos.y, pos.z, BlockType::Air, true);
    ClearBlockFlag(pos.x, pos.y, pos.z, BlockFlag::IsWaterFilled);
    ClearBlockFlag(pos.x, pos.y, pos.z, BlockFlag::IsWaterSurface);

    if (AdjacentToWater(pos.x, pos.y, pos.z) && pos.z <= SeaLevel) {
        if (pos.z < SeaLevel) {
            outUpdates.push_back({pos.x, pos.y, pos.z, BlockType::Water});
        }
        auto filled = FillWithWater(pos.x, pos.y, pos.z, true);
        for (const auto& f : filled) {
            outUpdates.push_back({f.x, f.y, f.z, BlockType::Water});
        }
    } else {
        outUpdates.push_back({pos.x, pos.y, pos.z, BlockType::Air});
    }
    return true;
}

WorldStorage::WorldStorage(const std::string& saveDir) : saveDir_(saveDir) {
    std::error_code ec;
    fs::create_directories(saveDir_, ec);
    ReloadWorlds();
    if (worlds_.empty()) {
        CreateWorld("My iLand", WorldType::Normal, 0);
    }
}

std::string WorldStorage::IndexPath() const {
    return saveDir_ + "/worlds.dat";
}

std::string WorldStorage::UpdatesPath(int worldId) const {
    return saveDir_ + "/world_" + std::to_string(worldId) + "_updates.bin";
}

void WorldStorage::ReloadWorlds() {
    worlds_.clear();
    nextId_ = 1;
    std::ifstream in(IndexPath());
    if (!in.is_open()) return;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::istringstream iss(line);
        SavedWorldMeta m;
        int typeInt = 0;
        if (iss >> m.id >> m.seed >> typeInt >> m.hour) {
            m.type = (typeInt == 1) ? WorldType::Flat : WorldType::Normal;
            std::string rest;
            std::getline(iss, rest);
            if (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
            m.title = rest.empty() ? "My iLand" : rest;
            worlds_.push_back(m);
            if (m.id >= nextId_) nextId_ = m.id + 1;
        }
    }
}

void WorldStorage::SaveIndex() const {
    std::ofstream out(IndexPath(), std::ios::trunc);
    for (const auto& m : worlds_) {
        out << m.id << ' ' << m.seed << ' ' << static_cast<int>(m.type) << ' ' << m.hour << ' ' << m.title << '\n';
    }
}

SavedWorldMeta WorldStorage::CreateWorld(const std::string& title, WorldType type, int customSeed) {
    SavedWorldMeta m;
    m.id = nextId_++;
    if (customSeed > 0) {
        m.seed = customSeed;
    } else {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<int> dist(1, 9999);
        m.seed = dist(gen);
    }
    m.type = type;
    m.title = title.empty() ? "My iLand" : title;
    m.hour = 12.0f;
    worlds_.push_back(m);
    SaveIndex();
    return m;
}

void WorldStorage::RenameWorld(int worldId, const std::string& newTitle) {
    for (auto& w : worlds_) {
        if (w.id == worldId && !newTitle.empty()) {
            w.title = newTitle;
            break;
        }
    }
    SaveIndex();
}

void WorldStorage::DeleteWorld(int worldId) {
    worlds_.erase(std::remove_if(worlds_.begin(), worlds_.end(),
                                 [worldId](const SavedWorldMeta& w) { return w.id == worldId; }),
                  worlds_.end());
    std::error_code ec;
    fs::remove(UpdatesPath(worldId), ec);
    SaveIndex();
}

void WorldStorage::SaveWorldHour(int worldId, float hour) {
    for (auto& w : worlds_) {
        if (w.id == worldId) {
            w.hour = hour;
            break;
        }
    }
    SaveIndex();
}

std::vector<WorldUpdateEntry> WorldStorage::LoadWorldUpdates(int worldId) const {
    std::vector<WorldUpdateEntry> out;
    std::ifstream in(UpdatesPath(worldId), std::ios::binary);
    if (!in.is_open()) return out;

    struct DiskRec { int16_t x, y, z; uint8_t t; uint8_t pad; };
    DiskRec rec;
    while (in.read(reinterpret_cast<char*>(&rec), sizeof(DiskRec))) {
        out.push_back({rec.x, rec.y, rec.z, static_cast<BlockType>(rec.t)});
    }
    return out;
}

void WorldStorage::AppendWorldUpdates(int worldId, const std::vector<WorldUpdateEntry>& updates) {
    if (updates.empty()) return;
    std::ofstream out(UpdatesPath(worldId), std::ios::binary | std::ios::app);
    struct DiskRec { int16_t x, y, z; uint8_t t; uint8_t pad; };
    for (const auto& u : updates) {
        DiskRec rec{static_cast<int16_t>(u.x), static_cast<int16_t>(u.y), static_cast<int16_t>(u.z), static_cast<uint8_t>(u.type), 0};
        out.write(reinterpret_cast<const char*>(&rec), sizeof(DiskRec));
    }
}

}
