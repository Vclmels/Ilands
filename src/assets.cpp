#include "assets.hpp"
#include <objidl.h>
#include <propidl.h>
#include <gdiplus.h>
#include <filesystem>
#include <cstring>
#include <cstdio>

namespace fs = std::filesystem;

namespace ilands {

static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

static const char* kBlockTextureFiles[NUM_BLOCK_TEXTURES] = {
    "grass_top.png",
    "grass.png",
    "dirt.png",
    "sand.png",
    "wood.png",
    "log.png",
    "log_top.png",
    "leaves.png",
    "brick.png",
    "cobblestone.png",
    "glass.png",
    "stone.png",
    "gravel.png",
    "light.png",
    "lava.png",
    "black.png",
    "blue.png",
    "brown.png",
    "cyan.png",
    "gray.png",
    "green.png",
    "lightblue.png",
    "lime.png",
    "magenta.png",
    "orange.png",
    "pink.png",
    "purple.png",
    "red.png",
    "silver.png",
    "white.png",
    "yellow.png",
    "slab.png",
    "slab_top.png",
    "gift.png",
    "gift_top.png",
    "gift_bottom.png",
    "marble.png",
    "gold.png",
    "stonebrick.png",
    "window_0.png",
    "metal.png",
    "table.png",
    "table_bottom.png",
    "bedrock.png",
    "bookshelf.png",
    "crate.png",
    "berries.png",
    "hay.png",
    "hay_top.png",
    "mossstone.png",
    "sakura.png",
    "cushion.png",
    "cushion_top.png"
};

Assets::~Assets() {
    Unload();
}

void Assets::Unload() {
    auto del = [](GLuint& t) {
        if (t) { glDeleteTextures(1, &t); t = 0; }
    };
    del(blockAtlasTex_);
    del(texBackground);
    del(texLogo);
    del(texSplash);
    del(texInventory);
    del(texDestroy);
    del(texItemSelector);
    del(texCloseIcon);
    del(texAddIcon);
    del(texWorldSquare);
    del(texUser);
    del(texGear);
    del(texSun);
    del(texMoon);
    del(texStars);
    del(texClouds);
    del(texWaterBump);
    del(texOceanGradient);
    del(texWhite);
    del(fontRegular.texture);
    del(fontBold.texture);

    for (const auto& f : loadedFontFiles_) {
        RemoveFontResourceExW(f.c_str(), FR_PRIVATE, nullptr);
    }
    loadedFontFiles_.clear();

    if (gdiplusToken_) {
        Gdiplus::GdiplusShutdown(gdiplusToken_);
        gdiplusToken_ = 0;
    }
}

bool Assets::Load(const std::string& exeDir) {
    Gdiplus::GdiplusStartupInput input;
    if (Gdiplus::GdiplusStartup(&gdiplusToken_, &input, nullptr) != Gdiplus::Ok) {
        std::fprintf(stderr, "Failed to initialize GDI+\n");
        return false;
    }

    std::vector<std::string> candidates = {
        exeDir + "/src/assets",
        "./src/assets",
        "../src/assets",
        "C:/Users/valen/Desktop/ilands/src/assets"
    };
    for (const auto& c : candidates) {
        std::error_code ec;
        if (fs::exists(c + "/Texture2D/grass_top.png", ec)) {
            assetsRoot_ = c;
            break;
        }
    }
    if (assetsRoot_.empty()) {
        std::fprintf(stderr, "Could not locate AssetRipper Assets folder!\n");
        return false;
    }

    ImageData whiteImg;
    whiteImg.width = 1;
    whiteImg.height = 1;
    whiteImg.rgba = {255, 255, 255, 255};
    texWhite = CreateGLTexture(whiteImg, true, true);

    if (!BuildBlockAtlas()) {
        return false;
    }

    texBackground   = LoadTextureFile("Texture2D/ilands_background.png", false, false, true);
    texLogo         = LoadTextureFile("Texture2D/logo.png", false, false, true);
    texSplash       = LoadTextureFile("Texture2D/splash.png", false, false, true);
    texInventory    = LoadTextureFile("Texture2D/inventory.png", false, false, true);
    texDestroy      = LoadTextureFile("Texture2D/destroy.png", false, false, true);
    texItemSelector = LoadTextureFile("Texture2D/item_selector.png", false, true, true);
    texCloseIcon    = LoadTextureFile("Texture2D/close_icon.png", false, false, true);
    texAddIcon      = LoadTextureFile("Texture2D/ilands_add.png", false, false, true);
    texWorldSquare  = LoadTextureFile("Texture2D/ilands_square.png", false, false, true);
    texUser         = LoadTextureFile("Texture2D/user.png", false, false, true);

    {
        ImageData atlasImg;
        if (LoadPngRGBA(assetsRoot_ + "/Texture2D/NGUI Atlas.png", atlasImg, false) &&
            atlasImg.width >= 580 && atlasImg.height >= 1560) {
            int rx0 = 298, ry0 = 1265, rx1 = 570, ry1 = 1545;
            int minX = rx1, minY = ry1, maxX = rx0, maxY = ry0;
            for (int y = ry0; y < ry1; ++y) {
                for (int x = rx0; x < rx1; ++x) {
                    size_t idx = (static_cast<size_t>(y) * atlasImg.width + x) * 4;
                    uint8_t r = atlasImg.rgba[idx], g = atlasImg.rgba[idx + 1], b = atlasImg.rgba[idx + 2], a = atlasImg.rgba[idx + 3];
                    if (a > 20 && r > 180 && g > 180 && b > 180) {
                        if (x < minX) minX = x;
                        if (x > maxX) maxX = x;
                        if (y < minY) minY = y;
                        if (y > maxY) maxY = y;
                    }
                }
            }
            if (maxX > minX + 16 && maxY > minY + 16) {
                ImageData gear;
                gear.width = maxX - minX + 1;
                gear.height = maxY - minY + 1;
                gear.rgba.assign(static_cast<size_t>(gear.width) * gear.height * 4, 0);
                for (int y = 0; y < gear.height; ++y) {
                    for (int x = 0; x < gear.width; ++x) {
                        size_t sIdx = (static_cast<size_t>(minY + y) * atlasImg.width + (minX + x)) * 4;
                        size_t dIdx = (static_cast<size_t>(y) * gear.width + x) * 4;
                        uint8_t r = atlasImg.rgba[sIdx], g = atlasImg.rgba[sIdx + 1], b = atlasImg.rgba[sIdx + 2], a = atlasImg.rgba[sIdx + 3];
                        if (a > 10 && r > 150 && g > 150 && b > 150) {
                            gear.rgba[dIdx + 0] = 255;
                            gear.rgba[dIdx + 1] = 255;
                            gear.rgba[dIdx + 2] = 255;
                            gear.rgba[dIdx + 3] = a;
                        }
                    }
                }
                texGear = CreateGLTexture(gear, false, true);
            }
        }
        if (!texGear) texGear = texWhite;
    }

    texSun           = LoadTextureFile("Texture2D/sun_texture.png", true, false, true);
    texMoon          = LoadTextureFile("Texture2D/moon_texture.png", true, false, true);
    texStars         = LoadTextureFile("Texture2D/stars_texture.png", true, false, false);
    texClouds        = LoadTextureFile("Texture2D/clouds_texture.png", true, false, false);
    texWaterBump     = LoadTextureFile("Texture2D/Waterbump.png", true, false, false);
    texOceanGradient = LoadTextureFile("Texture2D/oceangradient.png", true, false, true);

    BuildFontAtlas("Font/MontserratAlternates-Regular.ttf", L"Montserrat Alternates", 22, false, fontRegular);
    BuildFontAtlas("Font/MontserratAlternates-Bold.ttf", L"Montserrat Alternates", 28, true, fontBold);

    return true;
}

bool Assets::LoadPngRGBA(const std::string& path, ImageData& out, bool flipY) const {
    std::wstring wpath = Utf8ToWide(path);
    Gdiplus::Bitmap bmp(wpath.c_str());
    if (bmp.GetLastStatus() != Gdiplus::Ok) {
        std::fprintf(stderr, "Failed to load PNG: %s\n", path.c_str());
        return false;
    }

    int w = static_cast<int>(bmp.GetWidth());
    int h = static_cast<int>(bmp.GetHeight());
    Gdiplus::Rect rc(0, 0, w, h);
    Gdiplus::BitmapData data;
    if (bmp.LockBits(&rc, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data) != Gdiplus::Ok) {
        return false;
    }

    out.width = w;
    out.height = h;
    out.rgba.resize(static_cast<size_t>(w) * h * 4);

    for (int y = 0; y < h; ++y) {
        int dstY = flipY ? (h - 1 - y) : y;
        const uint8_t* srcRow = static_cast<const uint8_t*>(data.Scan0) + y * data.Stride;
        uint8_t* dstRow = out.rgba.data() + static_cast<size_t>(dstY) * w * 4;
        for (int x = 0; x < w; ++x) {
            dstRow[x * 4 + 0] = srcRow[x * 4 + 2];
            dstRow[x * 4 + 1] = srcRow[x * 4 + 1];
            dstRow[x * 4 + 2] = srcRow[x * 4 + 0];
            dstRow[x * 4 + 3] = srcRow[x * 4 + 3];
        }
    }

    bmp.UnlockBits(&data);
    return true;
}

GLuint Assets::CreateGLTexture(const ImageData& img, bool nearest, bool clamp) const {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, clamp ? GL_CLAMP_TO_EDGE : GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, clamp ? GL_CLAMP_TO_EDGE : GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img.width, img.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.rgba.data());
    return tex;
}

GLuint Assets::LoadTextureFile(const std::string& relPath, bool flipY, bool nearest, bool clamp) const {
    ImageData img;
    if (!LoadPngRGBA(assetsRoot_ + "/" + relPath, img, flipY)) {
        return texWhite;
    }
    return CreateGLTexture(img, nearest, clamp);
}

bool Assets::BuildBlockAtlas() {
    constexpr int Grid = 8;
    constexpr int Tile = 32;
    constexpr int AtlasSize = Grid * Tile;

    ImageData atlas;
    atlas.width = AtlasSize;
    atlas.height = AtlasSize;
    atlas.rgba.assign(static_cast<size_t>(AtlasSize) * AtlasSize * 4, 255);

    for (int i = 0; i < NUM_BLOCK_TEXTURES; ++i) {
        int col = i % Grid;
        int row = i / Grid;
        std::string fullPath = assetsRoot_ + "/Texture2D/" + kBlockTextureFiles[i];
        ImageData tile;
        bool ok = LoadPngRGBA(fullPath, tile, true);
        if (!ok || tile.width != Tile || tile.height != Tile) {
            std::fprintf(stderr, "Warning: block texture %s missing or unexpected size\n", kBlockTextureFiles[i]);
            tile.width = Tile;
            tile.height = Tile;
            tile.rgba.assign(Tile * Tile * 4, 200);
        }

        double sumR = 0, sumG = 0, sumB = 0;
        int countVis = 0;
        for (int y = 0; y < Tile; ++y) {
            int ay = row * Tile + y;
            int ax = col * Tile;
            std::memcpy(&atlas.rgba[(static_cast<size_t>(ay) * AtlasSize + ax) * 4],
                        &tile.rgba[static_cast<size_t>(y) * Tile * 4],
                        Tile * 4);
            for (int x = 0; x < Tile; ++x) {
                uint8_t a = tile.rgba[(y * Tile + x) * 4 + 3];
                if (a > 128) {
                    sumR += tile.rgba[(y * Tile + x) * 4 + 0] / 255.0;
                    sumG += tile.rgba[(y * Tile + x) * 4 + 1] / 255.0;
                    sumB += tile.rgba[(y * Tile + x) * 4 + 2] / 255.0;
                    countVis++;
                }
            }
        }

        if (countVis > 0) {
            blockAvgColors_[i] = {static_cast<float>(sumR / countVis),
                                  static_cast<float>(sumG / countVis),
                                  static_cast<float>(sumB / countVis), 1.0f};
        } else {
            blockAvgColors_[i] = {0.7f, 0.7f, 0.7f, 1.0f};
        }

        blockAtlasUvs_[i] = {
            static_cast<float>(col) / static_cast<float>(Grid),
            static_cast<float>(row) / static_cast<float>(Grid),
            1.0f / static_cast<float>(Grid),
            1.0f / static_cast<float>(Grid)
        };
    }

    blockAtlasTex_ = CreateGLTexture(atlas, true, true);
    return blockAtlasTex_ != 0;
}

bool Assets::BuildFontAtlas(const std::string& ttfRelPath, const std::wstring& faceName, int pixelSize, bool bold, FontAtlas& out) {
    std::wstring wfontPath = Utf8ToWide(assetsRoot_ + "/" + ttfRelPath);
    if (AddFontResourceExW(wfontPath.c_str(), FR_PRIVATE, nullptr) > 0) {
        loadedFontFiles_.push_back(wfontPath);
    }

    std::vector<uint32_t> codepoints;
    for (uint32_t c = 32; c <= 126; ++c) codepoints.push_back(c);
    const uint32_t extras[] = {0x00A1, 0x00BF, 0x00C1, 0x00C9, 0x00CD, 0x00D1, 0x00D3, 0x00DA, 0x00DC,
                               0x00E1, 0x00E9, 0x00ED, 0x00F1, 0x00F3, 0x00FA, 0x00FC};
    for (uint32_t c : extras) codepoints.push_back(c);

    constexpr int AtlasW = 512;
    constexpr int AtlasH = 512;

    HDC hdc = CreateCompatibleDC(nullptr);
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = AtlasW;
    bmi.bmiHeader.biHeight = -AtlasH;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP hbmp = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ oldBmp = SelectObject(hdc, hbmp);

    HFONT hfont = CreateFontW(
        -pixelSize, 0, 0, 0,
        bold ? FW_BOLD : FW_NORMAL,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        faceName.c_str()
    );
    HGDIOBJ oldFont = SelectObject(hdc, hfont);

    std::memset(bits, 0, static_cast<size_t>(AtlasW) * AtlasH * 4);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));

    TEXTMETRICW tm{};
    GetTextMetricsW(hdc, &tm);
    out.lineHeight = static_cast<float>(tm.tmHeight);

    int penX = 2;
    int penY = 2;
    int rowH = tm.tmHeight + 4;

    for (uint32_t cp : codepoints) {
        wchar_t wc = static_cast<wchar_t>(cp);
        SIZE sz{};
        GetTextExtentPoint32W(hdc, &wc, 1, &sz);
        int gw = sz.cx + 2;
        int gh = tm.tmHeight;
        if (penX + gw + 2 >= AtlasW) {
            penX = 2;
            penY += rowH;
        }
        if (penY + gh + 2 >= AtlasH) break;

        TextOutW(hdc, penX, penY, &wc, 1);

        GlyphInfo gi;
        gi.u0 = static_cast<float>(penX) / AtlasW;
        gi.v0 = static_cast<float>(penY) / AtlasH;
        gi.u1 = static_cast<float>(penX + gw) / AtlasW;
        gi.v1 = static_cast<float>(penY + gh) / AtlasH;
        gi.w = static_cast<float>(gw);
        gi.h = static_cast<float>(gh);
        gi.advance = static_cast<float>(sz.cx);
        out.glyphs[cp] = gi;

        penX += gw + 3;
    }

    GdiFlush();

    ImageData img;
    img.width = AtlasW;
    img.height = AtlasH;
    img.rgba.resize(static_cast<size_t>(AtlasW) * AtlasH * 4);
    const uint8_t* src = static_cast<const uint8_t*>(bits);
    for (int i = 0; i < AtlasW * AtlasH; ++i) {
        uint8_t v = src[i * 4 + 0];
        img.rgba[i * 4 + 0] = 255;
        img.rgba[i * 4 + 1] = 255;
        img.rgba[i * 4 + 2] = 255;
        img.rgba[i * 4 + 3] = v;
    }

    out.texture = CreateGLTexture(img, false, true);

    SelectObject(hdc, oldFont);
    SelectObject(hdc, oldBmp);
    DeleteObject(hfont);
    DeleteObject(hbmp);
    DeleteDC(hdc);

    return out.texture != 0;
}

}
