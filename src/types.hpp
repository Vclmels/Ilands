#pragma once
#include <cstdint>
#include <cmath>
#include <string>
#include <array>
#include <algorithm>

namespace ilands {

constexpr float PI_F = 3.14159265358979323846f;
constexpr double PI_D = 3.14159265358979323846;

struct Vec2 {
    float x = 0.0f, y = 0.0f;
    Vec2() = default;
    Vec2(float x_, float y_) : x(x_), y(y_) {}
};

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    Vec3() = default;
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    float lengthSq() const { return x * x + y * y + z * z; }
    float length() const { return std::sqrt(lengthSq()); }
    Vec3 normalized() const {
        float l = length();
        return (l > 1e-6f) ? (*this / l) : Vec3(0, 0, 0);
    }
    static float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    static Vec3 cross(const Vec3& a, const Vec3& b) {
        return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    }
};

struct Color4 {
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
    Color4() = default;
    Color4(float r_, float g_, float b_, float a_ = 1.0f) : r(r_), g(g_), b(b_), a(a_) {}
};

struct Rect {
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
};

struct IntVector3 {
    int x = 0, y = 0, z = 0;
    IntVector3() = default;
    IntVector3(int x_, int y_, int z_) : x(x_), y(y_), z(z_) {}
    bool operator==(const IntVector3& o) const { return x == o.x && y == o.y && z == o.z; }
    bool operator!=(const IntVector3& o) const { return !(*this == o); }
};

enum class WorldType : uint8_t {
    Normal = 0,
    Flat = 1
};

enum class BlockFace : uint8_t {
    Top = 0,
    Side = 1,
    Bottom = 2
};

enum class BlockFlag : uint8_t {
    IsWaterSurface = 1,
    IsWaterFilled = 2
};

enum class BlockLightType : uint8_t {
    Sky = 0,
    Emission = 1
};

enum class GUIMode : uint8_t {
    Build = 0,
    Destroy = 1
};

enum class BlockType : uint8_t {
    Null = 0,
    Air = 1,
    Water = 2,
    Grass = 3,
    Dirt = 4,
    Sand = 5,
    Wood = 6,
    Log = 7,
    Leaves = 8,
    Brick = 9,
    Cobblestone = 10,
    Glass = 11,
    Stone = 12,
    Gravel = 13,
    Light = 14,
    Lava = 15,
    HalfWood = 16,
    Black = 17,
    Blue = 18,
    Brown = 19,
    Cyan = 20,
    Gray = 21,
    Green = 22,
    LightBlue = 23,
    Lime = 24,
    Magenta = 25,
    Orange = 26,
    Pink = 27,
    Purple = 28,
    Red = 29,
    Silver = 30,
    White = 31,
    Yellow = 32,
    Slab = 33,
    HalfSlab = 34,
    Gift = 35,
    Marble = 36,
    Gold = 37,
    StoneBrick = 38,
    HalfBrick = 39,
    Window = 40,
    Metal = 41,
    Table = 42,
    HalfStoneBrick = 43,
    Bedrock = 44,
    Bookshelf = 45,
    Crate = 46,
    Berries = 47,
    Hay = 48,
    MossStone = 49,
    Sakura = 50,
    Cushion = 51,
    HalfBlack = 52,
    HalfBlue = 53,
    HalfBrown = 54,
    HalfCyan = 55,
    HalfGray = 56,
    HalfGreen = 57,
    HalfLightBlue = 58,
    HalfLime = 59,
    HalfMagenta = 60,
    HalfOrange = 61,
    HalfPink = 62,
    HalfPurple = 63,
    HalfRed = 64,
    HalfSilver = 65,
    HalfWhite = 66,
    HalfYellow = 67
};

constexpr int NUM_BLOCK_TEXTURES = 53;

inline int GetBlockTextureIndex(BlockType type, BlockFace face) {
    switch (type) {
        case BlockType::Grass:
            return (face == BlockFace::Top) ? 0 : (face == BlockFace::Side ? 1 : 2);
        case BlockType::Dirt:        return 2;
        case BlockType::Sand:        return 3;
        case BlockType::Wood:
        case BlockType::HalfWood:    return 4;
        case BlockType::Log:
            return (face == BlockFace::Side) ? 5 : 6;
        case BlockType::Leaves:      return 7;
        case BlockType::Brick:
        case BlockType::HalfBrick:   return 8;
        case BlockType::Cobblestone: return 9;
        case BlockType::Glass:       return 10;
        case BlockType::Stone:       return 11;
        case BlockType::Gravel:      return 12;
        case BlockType::Light:       return 13;
        case BlockType::Lava:        return 14;
        case BlockType::Black:
        case BlockType::HalfBlack:   return 15;
        case BlockType::Blue:
        case BlockType::HalfBlue:    return 16;
        case BlockType::Brown:
        case BlockType::HalfBrown:   return 17;
        case BlockType::Cyan:
        case BlockType::HalfCyan:    return 18;
        case BlockType::Gray:
        case BlockType::HalfGray:    return 19;
        case BlockType::Green:
        case BlockType::HalfGreen:   return 20;
        case BlockType::LightBlue:
        case BlockType::HalfLightBlue: return 21;
        case BlockType::Lime:
        case BlockType::HalfLime:    return 22;
        case BlockType::Magenta:
        case BlockType::HalfMagenta: return 23;
        case BlockType::Orange:
        case BlockType::HalfOrange:  return 24;
        case BlockType::Pink:
        case BlockType::HalfPink:    return 25;
        case BlockType::Purple:
        case BlockType::HalfPurple:  return 26;
        case BlockType::Red:
        case BlockType::HalfRed:     return 27;
        case BlockType::Silver:
        case BlockType::HalfSilver:  return 28;
        case BlockType::White:
        case BlockType::HalfWhite:   return 29;
        case BlockType::Yellow:
        case BlockType::HalfYellow:  return 30;
        case BlockType::Slab:
        case BlockType::HalfSlab:
            return (face == BlockFace::Side) ? 31 : 32;
        case BlockType::Gift:
            return (face == BlockFace::Top) ? 34 : (face == BlockFace::Side ? 33 : 35);
        case BlockType::Marble:      return 36;
        case BlockType::Gold:        return 37;
        case BlockType::StoneBrick:
        case BlockType::HalfStoneBrick: return 38;
        case BlockType::Window:      return 39;
        case BlockType::Metal:       return 40;
        case BlockType::Table:
            return (face == BlockFace::Top) ? 4 : (face == BlockFace::Side ? 41 : 42);
        case BlockType::Bedrock:     return 43;
        case BlockType::Bookshelf:
            return (face == BlockFace::Side) ? 44 : 4;
        case BlockType::Crate:       return 45;
        case BlockType::Berries:
            return (face == BlockFace::Bottom) ? 7 : 46;
        case BlockType::Hay:
            return (face == BlockFace::Side) ? 47 : 48;
        case BlockType::MossStone:   return 49;
        case BlockType::Sakura:      return 50;
        case BlockType::Cushion:
            return (face == BlockFace::Top) ? 52 : (face == BlockFace::Side ? 51 : 4);
        default:
            return 11;
    }
}

inline const char* GetBlockName(BlockType type) {
    switch (type) {
        case BlockType::Air: return "Air";
        case BlockType::Water: return "Water";
        case BlockType::Grass: return "Grass";
        case BlockType::Dirt: return "Dirt";
        case BlockType::Sand: return "Sand";
        case BlockType::Wood: return "Wood";
        case BlockType::Log: return "Log";
        case BlockType::Leaves: return "Leaves";
        case BlockType::Brick: return "Brick";
        case BlockType::Cobblestone: return "Cobblestone";
        case BlockType::Glass: return "Glass";
        case BlockType::Stone: return "Stone";
        case BlockType::Gravel: return "Gravel";
        case BlockType::Light: return "Light";
        case BlockType::Lava: return "Lava";
        case BlockType::HalfWood: return "Half Wood";
        case BlockType::Black: return "Black";
        case BlockType::Blue: return "Blue";
        case BlockType::Brown: return "Brown";
        case BlockType::Cyan: return "Cyan";
        case BlockType::Gray: return "Gray";
        case BlockType::Green: return "Green";
        case BlockType::LightBlue: return "Light Blue";
        case BlockType::Lime: return "Lime";
        case BlockType::Magenta: return "Magenta";
        case BlockType::Orange: return "Orange";
        case BlockType::Pink: return "Pink";
        case BlockType::Purple: return "Purple";
        case BlockType::Red: return "Red";
        case BlockType::Silver: return "Silver";
        case BlockType::White: return "White";
        case BlockType::Yellow: return "Yellow";
        case BlockType::Slab: return "Slab";
        case BlockType::HalfSlab: return "Half Slab";
        case BlockType::Gift: return "Gift";
        case BlockType::Marble: return "Marble";
        case BlockType::Gold: return "Gold";
        case BlockType::StoneBrick: return "Stone Brick";
        case BlockType::HalfBrick: return "Half Brick";
        case BlockType::Window: return "Window";
        case BlockType::Metal: return "Metal";
        case BlockType::Table: return "Table";
        case BlockType::HalfStoneBrick: return "Half Stone Brick";
        case BlockType::Bedrock: return "Bedrock";
        case BlockType::Bookshelf: return "Bookshelf";
        case BlockType::Crate: return "Crate";
        case BlockType::Berries: return "Berries";
        case BlockType::Hay: return "Hay";
        case BlockType::MossStone: return "Moss Stone";
        case BlockType::Sakura: return "Sakura";
        case BlockType::Cushion: return "Cushion";
        case BlockType::HalfBlack: return "Half Black";
        case BlockType::HalfBlue: return "Half Blue";
        case BlockType::HalfBrown: return "Half Brown";
        case BlockType::HalfCyan: return "Half Cyan";
        case BlockType::HalfGray: return "Half Gray";
        case BlockType::HalfGreen: return "Half Green";
        case BlockType::HalfLightBlue: return "Half Light Blue";
        case BlockType::HalfLime: return "Half Lime";
        case BlockType::HalfMagenta: return "Half Magenta";
        case BlockType::HalfOrange: return "Half Orange";
        case BlockType::HalfPink: return "Half Pink";
        case BlockType::HalfPurple: return "Half Purple";
        case BlockType::HalfRed: return "Half Red";
        case BlockType::HalfSilver: return "Half Silver";
        case BlockType::HalfWhite: return "Half White";
        case BlockType::HalfYellow: return "Half Yellow";
        default: return "Unknown";
    }
}

struct Block {
    static constexpr int LightLevels = 16;

    BlockType type = BlockType::Null;
    uint8_t blockLight = 0;
    uint8_t flags = 0;

    int skyLightLevel() const { return blockLight & 0x0F; }
    void setSkyLightLevel(int v) {
        blockLight = static_cast<uint8_t>((blockLight & 0xF0) | (v & 0x0F));
    }

    int emissionLightLevel() const { return (blockLight >> 4) & 0x0F; }
    void setEmissionLightLevel(int v) {
        blockLight = static_cast<uint8_t>((blockLight & 0x0F) | ((v & 0x0F) << 4));
    }

    static float GetBlockHeight(BlockType blockType) {
        switch (blockType) {
            case BlockType::Water:
                return 0.9f;
            case BlockType::HalfWood:
            case BlockType::HalfSlab:
            case BlockType::HalfBrick:
            case BlockType::HalfStoneBrick:
            case BlockType::Cushion:
            case BlockType::HalfBlack:
            case BlockType::HalfBlue:
            case BlockType::HalfBrown:
            case BlockType::HalfCyan:
            case BlockType::HalfGray:
            case BlockType::HalfGreen:
            case BlockType::HalfLightBlue:
            case BlockType::HalfLime:
            case BlockType::HalfMagenta:
            case BlockType::HalfOrange:
            case BlockType::HalfPink:
            case BlockType::HalfPurple:
            case BlockType::HalfRed:
            case BlockType::HalfSilver:
            case BlockType::HalfWhite:
            case BlockType::HalfYellow:
                return 0.5f;
            default:
                return 1.0f;
        }
    }

    static int GetEmissionLightSourceLevel(BlockType blockType) {
        if (blockType == BlockType::Light) return 15;
        if (blockType == BlockType::Lava) return 12;
        return 0;
    }

    static bool IsSkyLightSourceable(BlockType blockType) {
        return blockType == BlockType::Air || blockType == BlockType::Water ||
               blockType == BlockType::Glass || blockType == BlockType::Window;
    }

    static bool IsFluid(BlockType blockType) {
        return blockType == BlockType::Air || blockType == BlockType::Water;
    }

    static bool IsTransparent(BlockType blockType) {
        return blockType == BlockType::Air || blockType == BlockType::Water ||
               blockType == BlockType::Leaves || blockType == BlockType::Sakura ||
               blockType == BlockType::Berries || blockType == BlockType::Glass ||
               blockType == BlockType::Window || blockType == BlockType::Table;
    }

    static bool IsPartialBlock(BlockType blockType) {
        return GetBlockHeight(blockType) == 0.5f;
    }

    static bool IsDoubleSided(BlockType blockType) {
        return blockType == BlockType::Glass || blockType == BlockType::Window ||
               blockType == BlockType::Table;
    }

    static bool IsFillable(BlockType blockType) {
        return blockType == BlockType::Air || blockType == BlockType::Leaves ||
               blockType == BlockType::Sakura || blockType == BlockType::Berries ||
               blockType == BlockType::Table || IsPartialBlock(blockType);
    }
};

class DotNetRandom {
public:
    explicit DotNetRandom(int32_t seed) {
        constexpr int32_t MSEED = 161803398;
        constexpr int32_t MBIG = 2147483647;
        int32_t subtraction = (seed == INT32_MIN) ? INT32_MAX : std::abs(seed);
        int32_t mj = MSEED - subtraction;
        seedArray_[55] = mj;
        int32_t mk = 1;
        for (int i = 1; i < 55; ++i) {
            int ii = (21 * i) % 55;
            seedArray_[ii] = mk;
            mk = mj - mk;
            if (mk < 0) mk += MBIG;
            mj = seedArray_[ii];
        }
        for (int k = 1; k < 5; ++k) {
            for (int i = 1; i < 56; ++i) {
                seedArray_[i] -= seedArray_[1 + (i + 30) % 55];
                if (seedArray_[i] < 0) seedArray_[i] += MBIG;
            }
        }
        inext_ = 0;
        inextp_ = 21;
    }

    double NextDouble() {
        return InternalSample() * (1.0 / 2147483647.0);
    }

private:
    int32_t InternalSample() {
        constexpr int32_t MBIG = 2147483647;
        int locINext = inext_ + 1;
        int locINextp = inextp_ + 1;
        if (locINext >= 56) locINext = 1;
        if (locINextp >= 56) locINextp = 1;
        int32_t retVal = seedArray_[locINext] - seedArray_[locINextp];
        if (retVal == MBIG) retVal--;
        if (retVal < 0) retVal += MBIG;
        seedArray_[locINext] = retVal;
        inext_ = locINext;
        inextp_ = locINextp;
        return retVal;
    }

    int inext_ = 0;
    int inextp_ = 0;
    int32_t seedArray_[56] = {};
};

class MersenneTwister {
public:
    explicit MersenneTwister(int32_t seed) {
        init(static_cast<uint32_t>(seed));
    }

    int Next(int maxValue) {
        if (maxValue <= 1) return 0;
        return static_cast<int>(NextDouble() * static_cast<double>(maxValue));
    }

    int Next(int minValue, int maxValue) {
        if (maxValue <= minValue) return minValue;
        return Next(maxValue - minValue) + minValue;
    }

    double NextDouble() {
        return compute53BitRandom(0.0, 1.1102230246251565e-16);
    }

private:
    uint32_t GenerateUInt32() {
        static constexpr uint32_t mag01[2] = {0u, 2567483615u};
        uint32_t y;
        if (mti_ >= 624) {
            int kk;
            for (kk = 0; kk < 227; ++kk) {
                y = (mt_[kk] & 0x80000000u) | (mt_[kk + 1] & 0x7FFFFFFFu);
                mt_[kk] = mt_[kk + 397] ^ (y >> 1) ^ mag01[y & 1u];
            }
            for (; kk < 623; ++kk) {
                y = (mt_[kk] & 0x80000000u) | (mt_[kk + 1] & 0x7FFFFFFFu);
                mt_[kk] = mt_[kk - 227] ^ (y >> 1) ^ mag01[y & 1u];
            }
            y = (mt_[623] & 0x80000000u) | (mt_[0] & 0x7FFFFFFFu);
            mt_[623] = mt_[396] ^ (y >> 1) ^ mag01[y & 1u];
            mti_ = 0;
        }
        y = mt_[mti_++];
        y ^= (y >> 11);
        y ^= (y << 7) & 0x9D2C5680u;
        y ^= (y << 15) & 0xEFC60000u;
        y ^= (y >> 18);
        return y;
    }

    void init(uint32_t seed) {
        mt_[0] = seed;
        for (mti_ = 1; mti_ < 624; ++mti_) {
            mt_[mti_] = 1812433253u * (mt_[mti_ - 1] ^ (mt_[mti_ - 1] >> 30)) + static_cast<uint32_t>(mti_);
        }
    }

    double compute53BitRandom(double translate, double scale) {
        uint64_t a = static_cast<uint64_t>(GenerateUInt32()) >> 5;
        uint64_t b = static_cast<uint64_t>(GenerateUInt32()) >> 6;
        return (static_cast<double>(a) * 67108864.0 + static_cast<double>(b) + translate) * scale;
    }

    uint32_t mt_[624] = {};
    int mti_ = 625;
};

class PerlinNoise {
public:
    explicit PerlinNoise(int32_t seed) {
        DotNetRandom rng(seed);
        for (int i = 0; i < 256; ++i) {
            double z = 1.0 - 2.0 * rng.NextDouble();
            double r = std::sqrt(std::max(0.0, 1.0 - z * z));
            double theta = PI_D * 2.0 * rng.NextDouble();
            gradients_[i * 3]     = r * std::cos(theta);
            gradients_[i * 3 + 1] = r * std::sin(theta);
            gradients_[i * 3 + 2] = z;
        }
    }

    double Noise(double x, double y, double z) const {
        int ix = static_cast<int>(std::floor(x));
        double fx0 = x - ix;
        double fx1 = fx0 - 1.0;
        double wx = Smooth(fx0);

        int iy = static_cast<int>(std::floor(y));
        double fy0 = y - iy;
        double fy1 = fy0 - 1.0;
        double wy = Smooth(fy0);

        int iz = static_cast<int>(std::floor(z));
        double fz0 = z - iz;
        double fz1 = fz0 - 1.0;
        double wz = Smooth(fz0);

        double v0 = Lattice(ix, iy, iz, fx0, fy0, fz0);
        double v1 = Lattice(ix + 1, iy, iz, fx1, fy0, fz0);
        double vx0 = Lerp(wx, v0, v1);

        v0 = Lattice(ix, iy + 1, iz, fx0, fy1, fz0);
        v1 = Lattice(ix + 1, iy + 1, iz, fx1, fy1, fz0);
        double vx1 = Lerp(wx, v0, v1);
        double vy0 = Lerp(wy, vx0, vx1);

        v0 = Lattice(ix, iy, iz + 1, fx0, fy0, fz1);
        v1 = Lattice(ix + 1, iy, iz + 1, fx1, fy0, fz1);
        vx0 = Lerp(wx, v0, v1);

        v0 = Lattice(ix, iy + 1, iz + 1, fx0, fy1, fz1);
        v1 = Lattice(ix + 1, iy + 1, iz + 1, fx1, fy1, fz1);
        vx1 = Lerp(wx, v0, v1);
        double vy1 = Lerp(wy, vx0, vx1);

        return Lerp(wz, vy0, vy1);
    }

private:
    static inline constexpr uint8_t kPerm[256] = {
        225, 155, 210, 108, 175, 199, 221, 144, 203, 116,
         70, 213,  69, 158,  33, 252,   5,  82, 173, 133,
        222, 139, 174,  27,   9,  71,  90, 246,  75, 130,
         91, 191, 169, 138,   2, 151, 194, 235,  81,   7,
         25, 113, 228, 159, 205, 253, 134, 142, 248,  65,
        224, 217,  22, 121, 229,  63,  89, 103,  96, 104,
        156,  17, 201, 129,  36,   8, 165, 110, 237, 117,
        231,  56, 132, 211, 152,  20, 181, 111, 239, 218,
        170, 163,  51, 172, 157,  47,  80, 212, 176, 250,
         87,  49,  99, 242, 136, 189, 162, 115,  44,  43,
        124,  94, 150,  16, 141, 247,  32,  10, 198, 223,
        255,  72,  53, 131,  84,  57, 220, 197,  58,  50,
        208,  11, 241,  28,   3, 192,  62, 202,  18, 215,
        153,  24,  76,  41,  15, 179,  39,  46,  55,   6,
        128, 167,  23, 188, 106,  34, 187, 140, 164,  73,
        112, 182, 244, 195, 227,  13,  35,  77, 196, 185,
         26, 200, 226, 119,  31, 123, 168, 125, 249,  68,
        183, 230, 177, 135, 160, 180,  12,   1, 243, 148,
        102, 166,  38, 238, 251,  37, 240, 126,  64,  74,
        161,  40, 184, 149, 171, 178, 101,  66,  29,  59,
        146,  61, 254, 107,  42,  86, 154,   4, 236, 232,
        120,  21, 233, 209,  45,  98, 193, 114,  78,  19,
        206,  14, 118, 127,  48,  79, 147,  85,  30, 207,
        219,  54,  88, 234, 190, 122,  95,  67, 143, 109,
        137, 214, 145,  93,  92, 100, 245,   0, 216, 186,
         60,  83, 105,  97, 204,  52
    };

    int Permutate(int x) const { return kPerm[x & 0xFF]; }
    int Index(int ix, int iy, int iz) const { return Permutate(ix + Permutate(iy + Permutate(iz))); }
    double Lattice(int ix, int iy, int iz, double fx, double fy, double fz) const {
        int idx = Index(ix, iy, iz) * 3;
        return gradients_[idx] * fx + gradients_[idx + 1] * fy + gradients_[idx + 2] * fz;
    }
    static double Lerp(double t, double a, double b) { return a + t * (b - a); }
    static double Smooth(double x) { return x * x * (3.0 - 2.0 * x); }

    double gradients_[768] = {};
};

}
