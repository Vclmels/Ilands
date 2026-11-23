#pragma once
#include "types.hpp"
#include "gl_loader.hpp"
#include <string>
#include <vector>
#include <array>
#include <unordered_map>

namespace ilands {

struct ImageData {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;
};

struct GlyphInfo {
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
    float w = 0, h = 0;
    float advance = 0;
};

struct FontAtlas {
    GLuint texture = 0;
    float lineHeight = 24.0f;
    std::unordered_map<uint32_t, GlyphInfo> glyphs;
};

class Assets {
public:
    Assets() = default;
    ~Assets();

    bool Load(const std::string& exeDir);
    void Unload();

    const std::string& assetsRoot() const { return assetsRoot_; }
    GLuint blockAtlasTex() const { return blockAtlasTex_; }
    const std::array<Rect, NUM_BLOCK_TEXTURES>& blockAtlasUvs() const { return blockAtlasUvs_; }
    Color4 blockAverageColor(int texIndex) const {
        if (texIndex < 0 || texIndex >= NUM_BLOCK_TEXTURES) return {0.7f, 0.7f, 0.7f, 1.0f};
        return blockAvgColors_[texIndex];
    }

    GLuint texBackground = 0;
    GLuint texLogo = 0;
    GLuint texSplash = 0;
    GLuint texInventory = 0;
    GLuint texDestroy = 0;
    GLuint texItemSelector = 0;
    GLuint texCloseIcon = 0;
    GLuint texAddIcon = 0;
    GLuint texWorldSquare = 0;
    GLuint texUser = 0;
    GLuint texGear = 0;
    GLuint texSun = 0;
    GLuint texMoon = 0;
    GLuint texStars = 0;
    GLuint texClouds = 0;
    GLuint texWaterBump = 0;
    GLuint texOceanGradient = 0;
    GLuint texWhite = 0;

    FontAtlas fontRegular;
    FontAtlas fontBold;

private:
    bool LoadPngRGBA(const std::string& path, ImageData& out, bool flipY) const;
    GLuint CreateGLTexture(const ImageData& img, bool nearest, bool clamp) const;
    GLuint LoadTextureFile(const std::string& relPath, bool flipY, bool nearest, bool clamp) const;
    bool BuildBlockAtlas();
    bool BuildFontAtlas(const std::string& ttfRelPath, const std::wstring& faceName, int pixelSize, bool bold, FontAtlas& out);

    std::string assetsRoot_;
    ULONG_PTR gdiplusToken_ = 0;
    std::vector<std::wstring> loadedFontFiles_;

    GLuint blockAtlasTex_ = 0;
    std::array<Rect, NUM_BLOCK_TEXTURES> blockAtlasUvs_{};
    std::array<Color4, NUM_BLOCK_TEXTURES> blockAvgColors_{};
};

}
