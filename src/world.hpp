#pragma once
#include "types.hpp"
#include <vector>
#include <memory>
#include <unordered_map>

namespace ilands {

struct TerrainVertex {
    float x, y, z;
    float u, v;
    float r, g, b, a;
};

struct WaterVertex {
    float x, y, z;
    float r, g, b, a;
};

struct Chunk {
    static constexpr int W = 8;
    static constexpr int D = 8;
    static constexpr int H = 65;

    int cx = 0;
    int cy = 0;
    Block blocks[W][D][H];

    std::vector<TerrainVertex> landVerts;
    std::vector<uint32_t> landIndices;

    std::vector<TerrainVertex> transVerts;
    std::vector<uint32_t> transIndices;

    std::vector<WaterVertex> waterVerts;
    std::vector<uint32_t> waterIndices;

    std::vector<IntVector3> emissionLights;
    bool lightInitialized = false;
    bool meshDirty = true;
    bool gpuDirty = true;

    Chunk(int x_, int y_) : cx(x_), cy(y_) {}
};

struct WorldUpdateEntry {
    int x, y, z;
    BlockType type;
};

struct SavedWorldMeta {
    int id = 1;
    int seed = 1;
    WorldType type = WorldType::Normal;
    std::string title = "My iLand";
    float hour = 12.0f;
};

struct RaycastHit {
    bool hit = false;
    IntVector3 blockPos;
    IntVector3 placePos;
    Vec3 hitPoint;
    Vec3 normal;
};

class World {
public:
    static constexpr int ChunksWide = 32;
    static constexpr int ChunksDeep = 32;
    static constexpr int ChunkBlockWidth = 8;
    static constexpr int ChunkBlockDepth = 8;
    static constexpr int ChunkBlockHeight = 65;
    static constexpr int BlocksWide = ChunksWide * ChunkBlockWidth;
    static constexpr int BlocksDeep = ChunksDeep * ChunkBlockDepth;
    static constexpr int BlocksHigh = ChunkBlockHeight;
    static constexpr int SeaLevel = 30;
    static constexpr int OceanDistance = 500;

    World(int seed, WorldType type, const std::array<Rect, NUM_BLOCK_TEXTURES>& atlasUvs);

    void GenerateAll(const std::vector<WorldUpdateEntry>& savedUpdates);

    BlockType GetBlockType(int x, int y, int z) const;
    void SetBlockType(int x, int y, int z, BlockType blockType, bool doRegeneration = true);

    bool HasBlockFlag(int x, int y, int z, BlockFlag flag) const;
    void SetBlockFlag(int x, int y, int z, BlockFlag flag);
    void ClearBlockFlag(int x, int y, int z, BlockFlag flag);

    bool NextToBlockType(int x, int y, int z, BlockType type) const;
    bool AdjacentToBlockType(int x, int y, int z, BlockType type) const;
    bool AdjacentToWater(int x, int y, int z) const;
    bool SpaceAroundIsEmpty(int x, int y, int z, int widthAround, int depthAround, int heightAbove) const;

    std::vector<IntVector3> FillWithWater(int x, int y, int z, bool doRegeneration = true);
    void SetWaterSurface(int x, int y, int z);

    int GetBlockLightLevel(int x, int y, int z, BlockLightType lightType) const;
    void SetBlockLightLevel(int x, int y, int z, BlockLightType lightType, int lightLevel, bool doRegeneration = true, bool doPropagation = true);
    void SetLightAroundBlock(int x, int y, int z, BlockLightType lightType, bool doRegeneration);
    void SetLightAroundBlock(int x, int y, int z, BlockLightType lightType, int lightLevel, bool doRegeneration);
    void ResetBlockLightLevel(int x, int y, int z, BlockLightType lightType);
    void ResetBlockLightLevel(int x, int y, int z, BlockLightType lightType, int lightLevel, bool doRegeneration);
    void ResetLightAroundBlock(int x, int y, int z, BlockLightType lightType, int lightLevel, bool doRegeneration);

    void MarkChunkDirty(int cx, int cy);
    void RegenerateChunks(int cx, int cy, int bx, int by);
    void BuildChunkMesh(Chunk& chunk);
    int BuildDirtyChunksAround(float playerX, float playerZ, int maxPerFrame);

    void BuildOceanMesh(std::vector<WaterVertex>& outVerts, std::vector<uint32_t>& outIndices) const;
    void BuildOceanFloorMesh(std::vector<TerrainVertex>& outVerts, std::vector<uint32_t>& outIndices) const;

    RaycastHit Raycast(const Vec3& origin, const Vec3& dir, float maxDist) const;

    bool TryBuildBlock(const IntVector3& pos, BlockType type, const Vec3& playerPos, float playerRadius, float playerHeight, std::vector<WorldUpdateEntry>& outUpdates);
    bool TryDestroyBlock(const IntVector3& pos, BlockType& outDestroyedType, std::vector<WorldUpdateEntry>& outUpdates);

    Chunk& GetChunkRef(int cx, int cy) { return *chunks_[cx * ChunksDeep + cy]; }
    const Chunk& GetChunkRef(int cx, int cy) const { return *chunks_[cx * ChunksDeep + cy]; }

    int seed() const { return seed_; }
    WorldType type() const { return type_; }
    float lightAmount(int lvl) const { return lightAmounts_[std::clamp(lvl, 0, 15)]; }
    int heightAt(int x, int y) const { return heightMap_[x * BlocksDeep + y]; }
    const std::array<Rect, NUM_BLOCK_TEXTURES>& atlasUvs() const { return atlasUvs_; }

private:
    void GenerateTerrain();
    int GetGroundHeight(int blockX, int blockY) const;
    void DecorateTerrain();
    bool CreateStandardTree(int x, int y, int z);
    void CreateTreeBranch(int x0, int y0, int x1, int y1, int x2, int y2, int x3, int y3, int x4, int y4, int x5, int y5, int z, int canopyHeight);
    void CreateTreeTop(int x, int y, int z);
    void FillInitialWater();
    void GenerateInitialLight();
    void AddSkyLightSource(int x, int y, int z, bool doRegeneration);
    void RemoveSkyLightSource(int x, int y, int z);
    void PropagateSkyLightSource(int x, int y, int z, bool doRegeneration);
    void PropagateChunkSkyLightSources(Chunk& chunk, bool doRegeneration);
    void PropagateEmissionLightSources();
    void RegenerateBlockLight(int x, int y, int z, BlockType prevType, BlockType newType);
    void PropagatePendingLight(bool doRegeneration);
    void RefillWaterFlags(int x, int y, int z, BlockType prevType, BlockType newType);

    float GetBlockRenderHeight(int x, int y, int z, BlockType blockType) const;
    static bool IsBlockSideValid(BlockType blockType, BlockType neighborBlockType);
    void AddBlockSide(float x1, float y1, float z1, float x2, float y2, float z2,
                      float x3, float y3, float z3, float x4, float y4, float z4,
                      Chunk& chunk, BlockType blockType, BlockType neighborBlockType,
                      BlockFace blockFace, float blockFaceColor, int skyLight, int emissionLight);
    void AddWaterSide(float x1, float y1, float z1, float x2, float y2, float z2,
                      float x3, float y3, float z3, float x4, float y4, float z4,
                      Chunk& chunk, float blockFaceColor, int skyLight, int emissionLight, bool addBackSide);

    int seed_ = 1;
    WorldType type_ = WorldType::Normal;
    PerlinNoise perlin_;
    MersenneTwister random_;
    std::array<Rect, NUM_BLOCK_TEXTURES> atlasUvs_;
    float lightAmounts_[16] = {};
    std::vector<int> heightMap_;
    std::vector<std::unique_ptr<Chunk>> chunks_;
    std::vector<IntVector3> pendingSkyLights_;
    std::vector<IntVector3> pendingEmissionLights_;
};

class WorldStorage {
public:
    explicit WorldStorage(const std::string& saveDir);

    const std::vector<SavedWorldMeta>& GetWorlds() const { return worlds_; }
    void ReloadWorlds();
    SavedWorldMeta CreateWorld(const std::string& title, WorldType type, int customSeed = 0);
    void RenameWorld(int worldId, const std::string& newTitle);
    void DeleteWorld(int worldId);
    void SaveWorldHour(int worldId, float hour);

    std::vector<WorldUpdateEntry> LoadWorldUpdates(int worldId) const;
    void AppendWorldUpdates(int worldId, const std::vector<WorldUpdateEntry>& updates);

private:
    void SaveIndex() const;
    std::string IndexPath() const;
    std::string UpdatesPath(int worldId) const;

    std::string saveDir_;
    std::vector<SavedWorldMeta> worlds_;
    int nextId_ = 1;
};

}
