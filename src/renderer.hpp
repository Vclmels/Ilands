#pragma once
#include "types.hpp"
#include "world.hpp"
#include "assets.hpp"
#include "network.hpp"
#include "gl_loader.hpp"
#include <array>
#include <vector>
#include <string>

namespace ilands {

struct Mat4 {
    float m[16] = {};

    static Mat4 Identity();
    static Mat4 Perspective(float fovYRad, float aspect, float zNear, float zFar);
    static Mat4 Ortho(float left, float right, float bottom, float top, float zNear, float zFar);
    static Mat4 LookAt(const Vec3& eye, const Vec3& center, const Vec3& up);
    static Mat4 Translate(const Vec3& t);
    static Mat4 Scale(const Vec3& s);
    static Mat4 RotateX(float rad);
    static Mat4 RotateY(float rad);
    Mat4 operator*(const Mat4& o) const;
};

struct SkyState {
    Color4 sunColor;
    Color4 terrainColor;
    Color4 horizonColor;
    Color4 fogColor;
    float fogDensity = 0.008f;
    Color4 skyZenith;
    Color4 skyHorizon;
    Vec3 sunDir;
    float starAlpha = 0.0f;
    float cloudBrightness = 1.0f;
};

struct BlockParticle {
    Vec3 pos;
    Vec3 vel;
    float life = 0.0f;
    float maxLife = 0.6f;
    float size = 0.14f;
    Color4 color;
    Rect uv;
};

struct ChunkGpu {
    GLuint landVao = 0, landVbo = 0, landEbo = 0;
    uint32_t landCount = 0;

    GLuint transVao = 0, transVbo = 0, transEbo = 0;
    uint32_t transCount = 0;

    GLuint waterVao = 0, waterVbo = 0, waterEbo = 0;
    uint32_t waterCount = 0;
};

class Renderer {
public:
    Renderer() = default;
    ~Renderer();

    bool Init(const Assets& assets);
    void Shutdown();

    void ResetWorldBuffers();
    void UploadWorldStaticMeshes(const World& world);
    void SyncDirtyChunks(World& world);

    static SkyState ComputeSkyState(float hourOfDay, bool submerged, float submergedSkyLight);

    void BeginFrame(int width, int height, const Color4& clearColor);

    void RenderSky(const Mat4& view, const Mat4& proj, const Vec3& camPos, const SkyState& sky, float timeSec);
    void RenderWorld(const World& world, const Mat4& view, const Mat4& proj, const Vec3& camPos,
                     const SkyState& sky, bool submerged, float timeSec);
    void RenderBlockOutline(const Mat4& viewProj, const IntVector3& blockPos, float blockHeight);
    void RenderParticles(const Mat4& view, const Mat4& proj, const std::vector<BlockParticle>& particles);
    void RenderRemotePlayers(const Mat4& view, const Mat4& proj, const Vec3& camPos,
                             const SkyState& sky, const std::unordered_map<uint32_t, RemotePlayer>& players);
    static bool ProjectToScreen(const Vec3& worldPos, const Mat4& viewProj, int screenW, int screenH,
                                float& outX, float& outY, float& outDist);

    void BeginUI(int width, int height);
    void DrawRect(float x, float y, float w, float h, const Color4& color);
    void DrawGradientRect(float x, float y, float w, float h, const Color4& topColor, const Color4& bottomColor);
    void DrawRectOutline(float x, float y, float w, float h, float thickness, const Color4& color);
    void DrawTexture(GLuint tex, float x, float y, float w, float h, const Color4& tint = {1, 1, 1, 1},
                     float u0 = 0.0f, float v0 = 0.0f, float u1 = 1.0f, float v1 = 1.0f);
    void DrawText(const FontAtlas& font, const std::string& utf8, float x, float y, float scale,
                  const Color4& color, bool shadow = true);
    float MeasureText(const FontAtlas& font, const std::string& utf8, float scale) const;
    void DrawTextCentered(const FontAtlas& font, const std::string& utf8, float cx, float y, float scale,
                          const Color4& color, bool shadow = true);
    void DrawBlockIcon3D(BlockType blockType, float cx, float cy, float sizePx, int screenW, int screenH);
    void EndUI();

private:
    GLuint CompileShader(GLenum type, const char* src) const;
    GLuint LinkProgram(const char* vsSrc, const char* fsSrc) const;
    void UploadChunk(const Chunk& chunk, ChunkGpu& gpu);
    void FreeChunkGpu(ChunkGpu& gpu);
    void InitSkyAndQuadMeshes();

    const Assets* assets_ = nullptr;

    GLuint progTerrainOpaque_ = 0;
    GLuint progTerrainTrans_ = 0;
    GLuint progWater_ = 0;
    GLuint progSky_ = 0;
    GLuint progSimple3D_ = 0;
    GLuint progCapsule_ = 0;
    GLuint progUI_ = 0;
    GLuint progIcon3D_ = 0;

    std::vector<ChunkGpu> chunkGpus_;

    GLuint oceanVao_ = 0, oceanVbo_ = 0, oceanEbo_ = 0;
    uint32_t oceanIndexCount_ = 0;

    GLuint floorVao_ = 0, floorVbo_ = 0, floorEbo_ = 0;
    uint32_t floorIndexCount_ = 0;

    GLuint skyVao_ = 0, skyVbo_ = 0;
    uint32_t skyVertCount_ = 0;

    GLuint capsuleVao_ = 0, capsuleVbo_ = 0, capsuleEbo_ = 0;
    uint32_t capsuleIndexCount_ = 0;

    GLuint quad3DVao_ = 0, quad3DVbo_ = 0;
    GLuint outlineVao_ = 0, outlineVbo_ = 0;
    GLuint uiVao_ = 0, uiVbo_ = 0;
    GLuint iconVao_ = 0, iconVbo_ = 0;

    Mat4 uiOrtho_;
    int screenW_ = 1280;
    int screenH_ = 720;
};

}
