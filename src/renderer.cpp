#include "renderer.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace ilands {

Mat4 Mat4::Identity() {
    Mat4 r;
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}

Mat4 Mat4::Perspective(float fovYRad, float aspect, float zNear, float zFar) {
    Mat4 r;
    float f = 1.0f / std::tan(fovYRad * 0.5f);
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = (zFar + zNear) / (zNear - zFar);
    r.m[11] = -1.0f;
    r.m[14] = (2.0f * zFar * zNear) / (zNear - zFar);
    return r;
}

Mat4 Mat4::Ortho(float left, float right, float bottom, float top, float zNear, float zFar) {
    Mat4 r = Identity();
    r.m[0] = 2.0f / (right - left);
    r.m[5] = 2.0f / (top - bottom);
    r.m[10] = -2.0f / (zFar - zNear);
    r.m[12] = -(right + left) / (right - left);
    r.m[13] = -(top + bottom) / (top - bottom);
    r.m[14] = -(zFar + zNear) / (zFar - zNear);
    return r;
}

Mat4 Mat4::LookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
    Vec3 f = (center - eye).normalized();
    Vec3 s = Vec3::cross(f, up).normalized();
    Vec3 u = Vec3::cross(s, f);

    Mat4 r = Identity();
    r.m[0] = s.x;  r.m[4] = s.y;  r.m[8]  = s.z;
    r.m[1] = u.x;  r.m[5] = u.y;  r.m[9]  = u.z;
    r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
    r.m[12] = -Vec3::dot(s, eye);
    r.m[13] = -Vec3::dot(u, eye);
    r.m[14] = Vec3::dot(f, eye);
    return r;
}

Mat4 Mat4::Translate(const Vec3& t) {
    Mat4 r = Identity();
    r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
    return r;
}

Mat4 Mat4::Scale(const Vec3& s) {
    Mat4 r;
    r.m[0] = s.x; r.m[5] = s.y; r.m[10] = s.z; r.m[15] = 1.0f;
    return r;
}

Mat4 Mat4::RotateX(float rad) {
    Mat4 r = Identity();
    float c = std::cos(rad), s = std::sin(rad);
    r.m[5] = c; r.m[6] = s;
    r.m[9] = -s; r.m[10] = c;
    return r;
}

Mat4 Mat4::RotateY(float rad) {
    Mat4 r = Identity();
    float c = std::cos(rad), s = std::sin(rad);
    r.m[0] = c; r.m[2] = -s;
    r.m[8] = s; r.m[10] = c;
    return r;
}

Mat4 Mat4::operator*(const Mat4& o) const {
    Mat4 r;
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += m[k * 4 + row] * o.m[col * 4 + k];
            }
            r.m[col * 4 + row] = sum;
        }
    }
    return r;
}

static const char* kTerrainVS = R"GLSL(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec4 aCol;

uniform mat4 uMVP;
uniform vec3 uCamPos;

out vec2 vUv;
out vec4 vCol;
out float vDist;

void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
    vUv = aUv;
    vCol = aCol;
    vDist = length(aPos - uCamPos);
}
)GLSL";

static const char* kTerrainOpaqueFS = R"GLSL(
#version 330 core
in vec2 vUv;
in vec4 vCol;
in float vDist;

uniform sampler2D uMainTex;
uniform vec4 uColor;
uniform vec3 uFogColor;
uniform float uFogDensity;

out vec4 FragColor;

void main() {
    vec3 skyCol = vCol.rgb * uColor.rgb;
    vec3 emCol = vec3(vCol.a);
    vec3 lit = (vCol.a > skyCol.r) ? emCol : skyCol;
    lit = max(lit, vec3(0.25));

    vec4 tex = texture(uMainTex, vUv);
    vec3 col = tex.rgb * lit;

    float fogFactor = clamp(exp(-pow(vDist * uFogDensity, 2.0)), 0.0, 1.0);
    col = mix(uFogColor, col, fogFactor);
    FragColor = vec4(col, 1.0);
}
)GLSL";

static const char* kTerrainTransFS = R"GLSL(
#version 330 core
in vec2 vUv;
in vec4 vCol;
in float vDist;

uniform sampler2D uMainTex;
uniform vec4 uColor;
uniform vec3 uFogColor;
uniform float uFogDensity;

out vec4 FragColor;

void main() {
    vec4 tex = texture(uMainTex, vUv);
    if (tex.a < 0.5) discard;

    vec3 skyCol = vCol.rgb * uColor.rgb;
    vec3 emCol = vec3(vCol.a);
    vec3 lit = (vCol.a > skyCol.r) ? emCol : skyCol;
    lit = max(lit, vec3(0.25));

    vec3 col = tex.rgb * lit;
    float fogFactor = clamp(exp(-pow(vDist * uFogDensity, 2.0)), 0.0, 1.0);
    col = mix(uFogColor, col, fogFactor);
    FragColor = vec4(col, tex.a);
}
)GLSL";

static const char* kWaterVS = R"GLSL(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aCol;

uniform mat4 uMVP;
uniform vec3 uCamPos;

out vec3 vWorldPos;
out vec4 vCol;
out float vDist;

void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
    vWorldPos = aPos;
    vCol = aCol;
    vDist = length(aPos - uCamPos);
}
)GLSL";

static const char* kWaterFS = R"GLSL(
#version 330 core
in vec3 vWorldPos;
in vec4 vCol;
in float vDist;

uniform sampler2D uBumpMap;
uniform sampler2D uColorControl;
uniform vec4 uHorizonColor;
uniform vec4 uWaveOffset;
uniform float uWaveScale;
uniform vec3 uCamPos;
uniform vec3 uFogColor;
uniform float uFogDensity;
uniform float uWaterAlpha;

out vec4 FragColor;

void main() {
    vec2 uv1 = vWorldPos.xz * uWaveScale + uWaveOffset.xy;
    vec2 uv2 = vWorldPos.xz * (uWaveScale * 0.45) + uWaveOffset.zw;

    vec3 bump1 = texture(uBumpMap, uv1).rgb * 2.0 - 1.0;
    vec3 bump2 = texture(uBumpMap, uv2).rgb * 2.0 - 1.0;
    vec3 bump = normalize(vec3((bump1.xy + bump2.xy) * 0.35, 1.0));
    vec3 normal = normalize(vec3(bump.x, bump.z, bump.y));

    vec3 viewDir = normalize(uCamPos - vWorldPos);
    float fresnel = clamp(abs(dot(viewDir, normal)), 0.02, 0.98);
    vec4 waterGrad = texture(uColorControl, vec2(fresnel, 0.5));

    vec3 lightCol = max(vCol.rgb * uHorizonColor.rgb, vec3(0.25));
    vec3 col = mix(waterGrad.rgb * lightCol, lightCol * 0.45, waterGrad.a);

    float fogFactor = clamp(exp(-pow(vDist * uFogDensity, 2.0)), 0.0, 1.0);
    col = mix(uFogColor, col, fogFactor);
    FragColor = vec4(col, uWaterAlpha);
}
)GLSL";

static const char* kSkyVS = R"GLSL(
#version 330 core
layout(location = 0) in vec3 aPos;

uniform mat4 uVP;

out vec3 vDir;

void main() {
    vDir = aPos;
    vec4 pos = uVP * vec4(aPos, 1.0);
    gl_Position = pos.xyww;
}
)GLSL";

static const char* kSkyFS = R"GLSL(
#version 330 core
in vec3 vDir;

uniform vec3 uZenithColor;
uniform vec3 uHorizonColor;
uniform vec3 uSunDir;
uniform vec3 uSunColor;
uniform sampler2D uStarsTex;
uniform sampler2D uCloudsTex;
uniform float uStarAlpha;
uniform float uCloudBright;
uniform float uTime;

out vec4 FragColor;

void main() {
    vec3 d = normalize(vDir);
    float h = clamp(d.y, 0.0, 1.0);
    vec3 sky = mix(uHorizonColor, uZenithColor, pow(h, 0.55));

    float sunDot = max(dot(d, normalize(uSunDir)), 0.0);
    sky += uSunColor * pow(sunDot, 24.0) * 0.45;
    sky += uSunColor * pow(sunDot, 256.0) * 0.9;

    if (uStarAlpha > 0.01 && d.y > 0.0) {
        vec2 starUv = vec2(atan(d.z, d.x) / 6.283185 + 0.5, asin(clamp(d.y, -1.0, 1.0)) / 1.570796) * 3.0;
        vec3 stars = texture(uStarsTex, starUv).rgb;
        sky += stars * uStarAlpha * smoothstep(0.05, 0.35, d.y);
    }

    if (d.y > 0.04) {
        vec2 cloudUv = d.xz / (d.y + 0.25) * 0.35 + vec2(uTime * 0.006, uTime * 0.003);
        vec4 c = texture(uCloudsTex, cloudUv);
        float cloudFade = smoothstep(0.04, 0.28, d.y);
        float cloudAmt = c.a * cloudFade * 0.65;
        vec3 cloudCol = mix(uHorizonColor, vec3(1.0), 0.7) * uCloudBright;
        sky = mix(sky, cloudCol, cloudAmt);
    }

    FragColor = vec4(sky, 1.0);
}
)GLSL";

static const char* kSimple3DVS = R"GLSL(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUv;

uniform mat4 uMVP;
out vec2 vUv;

void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
    vUv = aUv;
}
)GLSL";

static const char* kSimple3DFS = R"GLSL(
#version 330 core
in vec2 vUv;
uniform sampler2D uTex;
uniform vec4 uColor;
uniform int uUseTex;

out vec4 FragColor;

void main() {
    vec4 c = uColor;
    if (uUseTex != 0) {
        c *= texture(uTex, vUv);
        if (c.a < 0.02) discard;
    }
    FragColor = c;
}
)GLSL";

static const char* kIcon3DVS = R"GLSL(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec4 aCol;

uniform mat4 uMVP;
out vec2 vUv;
out vec4 vCol;

void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
    vUv = aUv;
    vCol = aCol;
}
)GLSL";

static const char* kIcon3DFS = R"GLSL(
#version 330 core
in vec2 vUv;
in vec4 vCol;
uniform sampler2D uMainTex;

out vec4 FragColor;

void main() {
    vec4 tex = texture(uMainTex, vUv);
    if (tex.a < 0.2) discard;
    FragColor = vec4(tex.rgb * vCol.rgb, tex.a);
}
)GLSL";

static const char* kCapsuleVS = R"GLSL(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;

uniform mat4 uMVP;
uniform mat4 uModel;
uniform vec3 uCamPos;

out vec3 vNormal;
out float vDist;

void main() {
    vec4 wp = uModel * vec4(aPos, 1.0);
    gl_Position = uMVP * vec4(aPos, 1.0);
    vNormal = normalize(mat3(uModel) * aNormal);
    vDist = length(wp.xyz - uCamPos);
}
)GLSL";

static const char* kCapsuleFS = R"GLSL(
#version 330 core
in vec3 vNormal;
in float vDist;

uniform vec3 uLightDir;
uniform vec3 uTint;
uniform vec3 uFogColor;
uniform float uFogDensity;

out vec4 FragColor;

void main() {
    float ndl = max(dot(normalize(vNormal), normalize(uLightDir)), 0.0);
    vec3 col = uTint * (0.76 + 0.24 * ndl);
    float fogFactor = clamp(exp(-pow(vDist * uFogDensity, 2.0)), 0.0, 1.0);
    col = mix(uFogColor, col, fogFactor);
    FragColor = vec4(col, 1.0);
}
)GLSL";

static const char* kUIVS = R"GLSL(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec4 aCol;

uniform mat4 uOrtho;
out vec2 vUv;
out vec4 vCol;

void main() {
    gl_Position = uOrtho * vec4(aPos, 0.0, 1.0);
    vUv = aUv;
    vCol = aCol;
}
)GLSL";

static const char* kUIFS = R"GLSL(
#version 330 core
in vec2 vUv;
in vec4 vCol;
uniform sampler2D uTex;

out vec4 FragColor;

void main() {
    FragColor = texture(uTex, vUv) * vCol;
}
)GLSL";

Renderer::~Renderer() {
    Shutdown();
}

GLuint Renderer::CompileShader(GLenum type, const char* src) const {
    GLuint sh = gl::glCreateShader(type);
    gl::glShaderSource(sh, 1, &src, nullptr);
    gl::glCompileShader(sh);
    GLint ok = 0;
    gl::glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        gl::glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
        std::fprintf(stderr, "Shader compile error: %s\n", log);
        gl::glDeleteShader(sh);
        return 0;
    }
    return sh;
}

GLuint Renderer::LinkProgram(const char* vsSrc, const char* fsSrc) const {
    GLuint vs = CompileShader(GL_VERTEX_SHADER, vsSrc);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fsSrc);
    if (!vs || !fs) {
        if (vs) gl::glDeleteShader(vs);
        if (fs) gl::glDeleteShader(fs);
        return 0;
    }
    GLuint prog = gl::glCreateProgram();
    gl::glAttachShader(prog, vs);
    gl::glAttachShader(prog, fs);
    gl::glLinkProgram(prog);
    gl::glDeleteShader(vs);
    gl::glDeleteShader(fs);

    GLint ok = 0;
    gl::glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        gl::glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        std::fprintf(stderr, "Program link error: %s\n", log);
        gl::glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

bool Renderer::Init(const Assets& assets) {
    assets_ = &assets;
    progTerrainOpaque_ = LinkProgram(kTerrainVS, kTerrainOpaqueFS);
    progTerrainTrans_  = LinkProgram(kTerrainVS, kTerrainTransFS);
    progWater_         = LinkProgram(kWaterVS, kWaterFS);
    progSky_           = LinkProgram(kSkyVS, kSkyFS);
    progSimple3D_      = LinkProgram(kSimple3DVS, kSimple3DFS);
    progCapsule_       = LinkProgram(kCapsuleVS, kCapsuleFS);
    progIcon3D_        = LinkProgram(kIcon3DVS, kIcon3DFS);
    progUI_            = LinkProgram(kUIVS, kUIFS);

    if (!progTerrainOpaque_ || !progTerrainTrans_ || !progWater_ || !progSky_ ||
        !progSimple3D_ || !progCapsule_ || !progIcon3D_ || !progUI_) {
        return false;
    }

    InitSkyAndQuadMeshes();
    chunkGpus_.resize(World::ChunksWide * World::ChunksDeep);
    return true;
}

void Renderer::InitSkyAndQuadMeshes() {
    static const float skyCube[] = {
        -1, 1,-1, -1,-1,-1,  1,-1,-1,  1,-1,-1,  1, 1,-1, -1, 1,-1,
        -1,-1, 1, -1,-1,-1, -1, 1,-1, -1, 1,-1, -1, 1, 1, -1,-1, 1,
         1,-1,-1,  1,-1, 1,  1, 1, 1,  1, 1, 1,  1, 1,-1,  1,-1,-1,
        -1,-1, 1, -1, 1, 1,  1, 1, 1,  1, 1, 1,  1,-1, 1, -1,-1, 1,
        -1, 1,-1,  1, 1,-1,  1, 1, 1,  1, 1, 1, -1, 1, 1, -1, 1,-1,
        -1,-1,-1, -1,-1, 1,  1,-1,-1,  1,-1,-1, -1,-1, 1,  1,-1, 1
    };
    skyVertCount_ = 36;
    gl::glGenVertexArrays(1, &skyVao_);
    gl::glGenBuffers(1, &skyVbo_);
    gl::glBindVertexArray(skyVao_);
    gl::glBindBuffer(GL_ARRAY_BUFFER, skyVbo_);
    gl::glBufferData(GL_ARRAY_BUFFER, sizeof(skyCube), skyCube, GL_STATIC_DRAW);
    gl::glEnableVertexAttribArray(0);
    gl::glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);

    gl::glGenVertexArrays(1, &quad3DVao_);
    gl::glGenBuffers(1, &quad3DVbo_);
    gl::glBindVertexArray(quad3DVao_);
    gl::glBindBuffer(GL_ARRAY_BUFFER, quad3DVbo_);
    gl::glBufferData(GL_ARRAY_BUFFER, 6 * 5 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    gl::glEnableVertexAttribArray(0);
    gl::glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
    gl::glEnableVertexAttribArray(1);
    gl::glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));

    gl::glGenVertexArrays(1, &outlineVao_);
    gl::glGenBuffers(1, &outlineVbo_);
    gl::glBindVertexArray(outlineVao_);
    gl::glBindBuffer(GL_ARRAY_BUFFER, outlineVbo_);
    gl::glBufferData(GL_ARRAY_BUFFER, 24 * 5 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    gl::glEnableVertexAttribArray(0);
    gl::glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
    gl::glEnableVertexAttribArray(1);
    gl::glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));

    gl::glGenVertexArrays(1, &uiVao_);
    gl::glGenBuffers(1, &uiVbo_);
    gl::glBindVertexArray(uiVao_);
    gl::glBindBuffer(GL_ARRAY_BUFFER, uiVbo_);
    gl::glBufferData(GL_ARRAY_BUFFER, 6 * 8 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    gl::glEnableVertexAttribArray(0);
    gl::glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), reinterpret_cast<void*>(0));
    gl::glEnableVertexAttribArray(1);
    gl::glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));
    gl::glEnableVertexAttribArray(2);
    gl::glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), reinterpret_cast<void*>(4 * sizeof(float)));

    {
        struct CapVert { float x, y, z, nx, ny, nz; };
        std::vector<CapVert> cv;
        std::vector<uint32_t> ci;
        constexpr int slices = 24;
        constexpr int hemiRings = 6;
        const float R = 0.36f;
        const float cylBottom = R;
        const float cylTop = 1.78f - R;

        std::vector<std::pair<float, float>> rings;
        struct RingSpec { float y, r, ny, nr; };
        std::vector<RingSpec> specs;
        for (int i = 0; i <= hemiRings; ++i) {
            float a = -PI_F * 0.5f + (PI_F * 0.5f) * (static_cast<float>(i) / hemiRings);
            specs.push_back({cylBottom + R * std::sin(a), R * std::cos(a), std::sin(a), std::cos(a)});
        }
        for (int i = 0; i <= hemiRings; ++i) {
            float a = (PI_F * 0.5f) * (static_cast<float>(i) / hemiRings);
            specs.push_back({cylTop + R * std::sin(a), R * std::cos(a), std::sin(a), std::cos(a)});
        }

        for (const auto& sp : specs) {
            for (int j = 0; j <= slices; ++j) {
                float theta = (2.0f * PI_F * j) / slices;
                float ct = std::cos(theta), st = std::sin(theta);
                cv.push_back({sp.r * ct, sp.y, sp.r * st, sp.nr * ct, sp.ny, sp.nr * st});
            }
        }
        int stride = slices + 1;
        for (size_t r = 0; r + 1 < specs.size(); ++r) {
            for (int j = 0; j < slices; ++j) {
                uint32_t a = static_cast<uint32_t>(r * stride + j);
                uint32_t b = a + 1;
                uint32_t c = static_cast<uint32_t>((r + 1) * stride + j);
                uint32_t d = c + 1;
                ci.push_back(a); ci.push_back(c); ci.push_back(b);
                ci.push_back(b); ci.push_back(c); ci.push_back(d);
            }
        }
        capsuleIndexCount_ = static_cast<uint32_t>(ci.size());
        gl::glGenVertexArrays(1, &capsuleVao_);
        gl::glGenBuffers(1, &capsuleVbo_);
        gl::glGenBuffers(1, &capsuleEbo_);
        gl::glBindVertexArray(capsuleVao_);
        gl::glBindBuffer(GL_ARRAY_BUFFER, capsuleVbo_);
        gl::glBufferData(GL_ARRAY_BUFFER, cv.size() * sizeof(CapVert), cv.data(), GL_STATIC_DRAW);
        gl::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, capsuleEbo_);
        gl::glBufferData(GL_ELEMENT_ARRAY_BUFFER, ci.size() * sizeof(uint32_t), ci.data(), GL_STATIC_DRAW);
        gl::glEnableVertexAttribArray(0);
        gl::glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(CapVert), reinterpret_cast<void*>(0));
        gl::glEnableVertexAttribArray(1);
        gl::glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(CapVert), reinterpret_cast<void*>(3 * sizeof(float)));
    }

    gl::glGenVertexArrays(1, &iconVao_);
    gl::glGenBuffers(1, &iconVbo_);
    gl::glBindVertexArray(iconVao_);
    gl::glBindBuffer(GL_ARRAY_BUFFER, iconVbo_);
    gl::glBufferData(GL_ARRAY_BUFFER, 72 * sizeof(TerrainVertex), nullptr, GL_DYNAMIC_DRAW);
    gl::glEnableVertexAttribArray(0);
    gl::glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(TerrainVertex), reinterpret_cast<void*>(0));
    gl::glEnableVertexAttribArray(1);
    gl::glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(TerrainVertex), reinterpret_cast<void*>(3 * sizeof(float)));
    gl::glEnableVertexAttribArray(2);
    gl::glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(TerrainVertex), reinterpret_cast<void*>(5 * sizeof(float)));

    gl::glBindVertexArray(0);
}

void Renderer::FreeChunkGpu(ChunkGpu& g) {
    if (g.landVao)  { gl::glDeleteVertexArrays(1, &g.landVao);  g.landVao = 0; }
    if (g.landVbo)  { gl::glDeleteBuffers(1, &g.landVbo);       g.landVbo = 0; }
    if (g.landEbo)  { gl::glDeleteBuffers(1, &g.landEbo);       g.landEbo = 0; }
    g.landCount = 0;

    if (g.transVao) { gl::glDeleteVertexArrays(1, &g.transVao); g.transVao = 0; }
    if (g.transVbo) { gl::glDeleteBuffers(1, &g.transVbo);      g.transVbo = 0; }
    if (g.transEbo) { gl::glDeleteBuffers(1, &g.transEbo);      g.transEbo = 0; }
    g.transCount = 0;

    if (g.waterVao) { gl::glDeleteVertexArrays(1, &g.waterVao); g.waterVao = 0; }
    if (g.waterVbo) { gl::glDeleteBuffers(1, &g.waterVbo);      g.waterVbo = 0; }
    if (g.waterEbo) { gl::glDeleteBuffers(1, &g.waterEbo);      g.waterEbo = 0; }
    g.waterCount = 0;
}

void Renderer::ResetWorldBuffers() {
    for (auto& g : chunkGpus_) FreeChunkGpu(g);
    if (oceanVao_) { gl::glDeleteVertexArrays(1, &oceanVao_); oceanVao_ = 0; }
    if (oceanVbo_) { gl::glDeleteBuffers(1, &oceanVbo_);      oceanVbo_ = 0; }
    if (oceanEbo_) { gl::glDeleteBuffers(1, &oceanEbo_);      oceanEbo_ = 0; }
    oceanIndexCount_ = 0;

    if (floorVao_) { gl::glDeleteVertexArrays(1, &floorVao_); floorVao_ = 0; }
    if (floorVbo_) { gl::glDeleteBuffers(1, &floorVbo_);      floorVbo_ = 0; }
    if (floorEbo_) { gl::glDeleteBuffers(1, &floorEbo_);      floorEbo_ = 0; }
    floorIndexCount_ = 0;
}

void Renderer::Shutdown() {
    ResetWorldBuffers();
    auto delVao = [](GLuint& v) { if (v) { gl::glDeleteVertexArrays(1, &v); v = 0; } };
    auto delBuf = [](GLuint& b) { if (b) { gl::glDeleteBuffers(1, &b); b = 0; } };
    auto delPrg = [](GLuint& p) { if (p) { gl::glDeleteProgram(p); p = 0; } };

    delVao(skyVao_);     delBuf(skyVbo_);
    delVao(capsuleVao_); delBuf(capsuleVbo_); delBuf(capsuleEbo_);
    delVao(quad3DVao_);  delBuf(quad3DVbo_);
    delVao(outlineVao_); delBuf(outlineVbo_);
    delVao(uiVao_);      delBuf(uiVbo_);
    delVao(iconVao_);    delBuf(iconVbo_);

    delPrg(progTerrainOpaque_);
    delPrg(progTerrainTrans_);
    delPrg(progWater_);
    delPrg(progSky_);
    delPrg(progSimple3D_);
    delPrg(progCapsule_);
    delPrg(progIcon3D_);
    delPrg(progUI_);
}

void Renderer::UploadWorldStaticMeshes(const World& world) {
    ResetWorldBuffers();

    std::vector<WaterVertex> oceanV;
    std::vector<uint32_t> oceanI;
    world.BuildOceanMesh(oceanV, oceanI);
    oceanIndexCount_ = static_cast<uint32_t>(oceanI.size());
    if (oceanIndexCount_ > 0) {
        gl::glGenVertexArrays(1, &oceanVao_);
        gl::glGenBuffers(1, &oceanVbo_);
        gl::glGenBuffers(1, &oceanEbo_);
        gl::glBindVertexArray(oceanVao_);
        gl::glBindBuffer(GL_ARRAY_BUFFER, oceanVbo_);
        gl::glBufferData(GL_ARRAY_BUFFER, oceanV.size() * sizeof(WaterVertex), oceanV.data(), GL_STATIC_DRAW);
        gl::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, oceanEbo_);
        gl::glBufferData(GL_ELEMENT_ARRAY_BUFFER, oceanI.size() * sizeof(uint32_t), oceanI.data(), GL_STATIC_DRAW);
        gl::glEnableVertexAttribArray(0);
        gl::glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(WaterVertex), reinterpret_cast<void*>(0));
        gl::glEnableVertexAttribArray(1);
        gl::glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(WaterVertex), reinterpret_cast<void*>(3 * sizeof(float)));
    }

    std::vector<TerrainVertex> floorV;
    std::vector<uint32_t> floorI;
    world.BuildOceanFloorMesh(floorV, floorI);
    floorIndexCount_ = static_cast<uint32_t>(floorI.size());
    if (floorIndexCount_ > 0) {
        gl::glGenVertexArrays(1, &floorVao_);
        gl::glGenBuffers(1, &floorVbo_);
        gl::glGenBuffers(1, &floorEbo_);
        gl::glBindVertexArray(floorVao_);
        gl::glBindBuffer(GL_ARRAY_BUFFER, floorVbo_);
        gl::glBufferData(GL_ARRAY_BUFFER, floorV.size() * sizeof(TerrainVertex), floorV.data(), GL_STATIC_DRAW);
        gl::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, floorEbo_);
        gl::glBufferData(GL_ELEMENT_ARRAY_BUFFER, floorI.size() * sizeof(uint32_t), floorI.data(), GL_STATIC_DRAW);
        gl::glEnableVertexAttribArray(0);
        gl::glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(TerrainVertex), reinterpret_cast<void*>(0));
        gl::glEnableVertexAttribArray(1);
        gl::glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(TerrainVertex), reinterpret_cast<void*>(3 * sizeof(float)));
        gl::glEnableVertexAttribArray(2);
        gl::glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(TerrainVertex), reinterpret_cast<void*>(5 * sizeof(float)));
    }
    gl::glBindVertexArray(0);
}

void Renderer::UploadChunk(const Chunk& chunk, ChunkGpu& g) {
    auto uploadTerrain = [](const std::vector<TerrainVertex>& verts, const std::vector<uint32_t>& idx,
                            GLuint& vao, GLuint& vbo, GLuint& ebo, uint32_t& count) {
        count = static_cast<uint32_t>(idx.size());
        if (count == 0) return;
        if (!vao) {
            gl::glGenVertexArrays(1, &vao);
            gl::glGenBuffers(1, &vbo);
            gl::glGenBuffers(1, &ebo);
            gl::glBindVertexArray(vao);
            gl::glBindBuffer(GL_ARRAY_BUFFER, vbo);
            gl::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
            gl::glEnableVertexAttribArray(0);
            gl::glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(TerrainVertex), reinterpret_cast<void*>(0));
            gl::glEnableVertexAttribArray(1);
            gl::glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(TerrainVertex), reinterpret_cast<void*>(3 * sizeof(float)));
            gl::glEnableVertexAttribArray(2);
            gl::glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(TerrainVertex), reinterpret_cast<void*>(5 * sizeof(float)));
        } else {
            gl::glBindVertexArray(vao);
            gl::glBindBuffer(GL_ARRAY_BUFFER, vbo);
            gl::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        }
        gl::glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(TerrainVertex), verts.data(), GL_STATIC_DRAW);
        gl::glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(uint32_t), idx.data(), GL_STATIC_DRAW);
    };

    uploadTerrain(chunk.landVerts, chunk.landIndices, g.landVao, g.landVbo, g.landEbo, g.landCount);
    uploadTerrain(chunk.transVerts, chunk.transIndices, g.transVao, g.transVbo, g.transEbo, g.transCount);

    g.waterCount = static_cast<uint32_t>(chunk.waterIndices.size());
    if (g.waterCount > 0) {
        if (!g.waterVao) {
            gl::glGenVertexArrays(1, &g.waterVao);
            gl::glGenBuffers(1, &g.waterVbo);
            gl::glGenBuffers(1, &g.waterEbo);
            gl::glBindVertexArray(g.waterVao);
            gl::glBindBuffer(GL_ARRAY_BUFFER, g.waterVbo);
            gl::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g.waterEbo);
            gl::glEnableVertexAttribArray(0);
            gl::glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(WaterVertex), reinterpret_cast<void*>(0));
            gl::glEnableVertexAttribArray(1);
            gl::glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(WaterVertex), reinterpret_cast<void*>(3 * sizeof(float)));
        } else {
            gl::glBindVertexArray(g.waterVao);
            gl::glBindBuffer(GL_ARRAY_BUFFER, g.waterVbo);
            gl::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g.waterEbo);
        }
        gl::glBufferData(GL_ARRAY_BUFFER, chunk.waterVerts.size() * sizeof(WaterVertex), chunk.waterVerts.data(), GL_STATIC_DRAW);
        gl::glBufferData(GL_ELEMENT_ARRAY_BUFFER, chunk.waterIndices.size() * sizeof(uint32_t), chunk.waterIndices.data(), GL_STATIC_DRAW);
    }
    gl::glBindVertexArray(0);
}

void Renderer::SyncDirtyChunks(World& world) {
    for (int cx = 0; cx < World::ChunksWide; ++cx) {
        for (int cy = 0; cy < World::ChunksDeep; ++cy) {
            Chunk& c = world.GetChunkRef(cx, cy);
            if (c.gpuDirty && !c.meshDirty) {
                UploadChunk(c, chunkGpus_[cx * World::ChunksDeep + cy]);
                c.gpuDirty = false;
            }
        }
    }
}

SkyState Renderer::ComputeSkyState(float hourOfDay, bool submerged, float submergedSkyLight) {
    SkyState s;
    float h = std::fmod(hourOfDay, 24.0f);
    if (h < 0.0f) h += 24.0f;

    float sunAngle = (h - 6.0f) / 24.0f * 2.0f * PI_F;
    s.sunDir = Vec3(std::cos(sunAngle), std::sin(sunAngle), -0.25f).normalized();
    float elev = std::sin(sunAngle);

    auto lerpF = [](float a, float b, float t) { return a + (b - a) * std::clamp(t, 0.0f, 1.0f); };
    auto lerpC = [&](const Color4& a, const Color4& b, float t) {
        return Color4(lerpF(a.r, b.r, t), lerpF(a.g, b.g, t), lerpF(a.b, b.b, t), 1.0f);
    };

    Color4 nightSun(0.18f, 0.22f, 0.34f, 1.0f);
    Color4 dawnSun(0.95f, 0.58f, 0.38f, 1.0f);
    Color4 daySun(1.0f, 0.98f, 0.94f, 1.0f);

    Color4 nightZenith(0.03f, 0.05f, 0.14f, 1.0f);
    Color4 dawnZenith(0.24f, 0.34f, 0.62f, 1.0f);
    Color4 dayZenith(0.18f, 0.52f, 0.92f, 1.0f);

    Color4 nightHoriz(0.06f, 0.09f, 0.20f, 1.0f);
    Color4 dawnHoriz(0.95f, 0.58f, 0.40f, 1.0f);
    Color4 dayHoriz(0.62f, 0.84f, 0.98f, 1.0f);

    if (elev <= -0.15f) {
        s.sunColor = nightSun;
        s.skyZenith = nightZenith;
        s.skyHorizon = nightHoriz;
        s.starAlpha = 1.0f;
        s.cloudBrightness = 0.22f;
    } else if (elev <= 0.15f) {
        float t = (elev + 0.15f) / 0.30f;
        s.sunColor = lerpC(nightSun, dawnSun, t);
        s.skyZenith = lerpC(nightZenith, dawnZenith, t);
        s.skyHorizon = lerpC(nightHoriz, dawnHoriz, t);
        s.starAlpha = 1.0f - t;
        s.cloudBrightness = lerpF(0.22f, 0.80f, t);
    } else if (elev <= 0.45f) {
        float t = (elev - 0.15f) / 0.30f;
        s.sunColor = lerpC(dawnSun, daySun, t);
        s.skyZenith = lerpC(dawnZenith, dayZenith, t);
        s.skyHorizon = lerpC(dawnHoriz, dayHoriz, t);
        s.starAlpha = 0.0f;
        s.cloudBrightness = lerpF(0.80f, 1.0f, t);
    } else {
        s.sunColor = daySun;
        s.skyZenith = dayZenith;
        s.skyHorizon = dayHoriz;
        s.starAlpha = 0.0f;
        s.cloudBrightness = 1.0f;
    }

    s.terrainColor = Color4(
        std::min(1.25f, s.sunColor.r + 0.4f),
        std::min(1.25f, s.sunColor.g + 0.4f),
        std::min(1.25f, s.sunColor.b + 0.4f), 1.0f);

    Color4 waterColor(0.5f, 0.8f, 1.0f, 1.0f);
    s.horizonColor = Color4(
        waterColor.r * s.sunColor.r * 1.8f,
        waterColor.g * s.sunColor.g * 1.8f,
        waterColor.b * s.sunColor.b * 1.8f, 1.0f);

    if (submerged) {
        float factor = std::clamp(submergedSkyLight / 15.0f, 0.25f, 1.0f);
        s.fogColor = Color4(
            std::max(0.05f, (s.sunColor.r - 0.85f) * factor + 0.12f),
            std::max(0.10f, (s.sunColor.g - 0.70f) * factor + 0.28f),
            std::max(0.18f, (s.sunColor.b - 0.60f) * factor + 0.42f), 1.0f);
        s.fogDensity = 0.04f;
    } else {
        s.fogColor = Color4(s.horizonColor.r * 0.6f, s.horizonColor.g * 0.6f, s.horizonColor.b * 0.6f, 1.0f);
        s.fogDensity = 0.0045f;
    }
    return s;
}

void Renderer::BeginFrame(int width, int height, const Color4& clearColor) {
    screenW_ = width;
    screenH_ = height;
    glViewport(0, 0, width, height);
    glClearColor(clearColor.r, clearColor.g, clearColor.b, clearColor.a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Renderer::RenderSky(const Mat4& view, const Mat4& proj, const Vec3& camPos, const SkyState& sky, float timeSec) {
    Mat4 rotView = view;
    rotView.m[12] = rotView.m[13] = rotView.m[14] = 0.0f;
    Mat4 vp = proj * rotView;

    glDisable(GL_CULL_FACE);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);

    gl::glUseProgram(progSky_);
    gl::glUniformMatrix4fv(gl::glGetUniformLocation(progSky_, "uVP"), 1, GL_FALSE, vp.m);
    gl::glUniform3f(gl::glGetUniformLocation(progSky_, "uZenithColor"), sky.skyZenith.r, sky.skyZenith.g, sky.skyZenith.b);
    gl::glUniform3f(gl::glGetUniformLocation(progSky_, "uHorizonColor"), sky.skyHorizon.r, sky.skyHorizon.g, sky.skyHorizon.b);
    gl::glUniform3f(gl::glGetUniformLocation(progSky_, "uSunDir"), sky.sunDir.x, sky.sunDir.y, sky.sunDir.z);
    gl::glUniform3f(gl::glGetUniformLocation(progSky_, "uSunColor"), sky.sunColor.r, sky.sunColor.g, sky.sunColor.b);
    gl::glUniform1f(gl::glGetUniformLocation(progSky_, "uStarAlpha"), sky.starAlpha);
    gl::glUniform1f(gl::glGetUniformLocation(progSky_, "uCloudBright"), sky.cloudBrightness);
    gl::glUniform1f(gl::glGetUniformLocation(progSky_, "uTime"), timeSec);

    gl::glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, assets_->texStars);
    gl::glUniform1i(gl::glGetUniformLocation(progSky_, "uStarsTex"), 0);

    gl::glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, assets_->texClouds);
    gl::glUniform1i(gl::glGetUniformLocation(progSky_, "uCloudsTex"), 1);

    gl::glBindVertexArray(skyVao_);
    glDrawArrays(GL_TRIANGLES, 0, skyVertCount_);

    auto drawCelestial = [&](const Vec3& dir, float size, GLuint tex, const Color4& tint, bool additive) {
        if (dir.y < -0.15f) return;
        Vec3 center = camPos + dir * 220.0f;
        Vec3 right(view.m[0], view.m[4], view.m[8]);
        Vec3 up(view.m[1], view.m[5], view.m[9]);

        Vec3 p0 = center - right * size - up * size;
        Vec3 p1 = center + right * size - up * size;
        Vec3 p2 = center + right * size + up * size;
        Vec3 p3 = center - right * size + up * size;

        float verts[] = {
            p0.x, p0.y, p0.z, 0.0f, 0.0f,
            p1.x, p1.y, p1.z, 1.0f, 0.0f,
            p2.x, p2.y, p2.z, 1.0f, 1.0f,
            p2.x, p2.y, p2.z, 1.0f, 1.0f,
            p3.x, p3.y, p3.z, 0.0f, 1.0f,
            p0.x, p0.y, p0.z, 0.0f, 0.0f
        };

        Mat4 mvp = proj * view;
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, additive ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);

        gl::glUseProgram(progSimple3D_);
        gl::glUniformMatrix4fv(gl::glGetUniformLocation(progSimple3D_, "uMVP"), 1, GL_FALSE, mvp.m);
        gl::glUniform4f(gl::glGetUniformLocation(progSimple3D_, "uColor"), tint.r, tint.g, tint.b, tint.a);
        gl::glUniform1i(gl::glGetUniformLocation(progSimple3D_, "uUseTex"), 1);

        gl::glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex);
        gl::glUniform1i(gl::glGetUniformLocation(progSimple3D_, "uTex"), 0);

        gl::glBindVertexArray(quad3DVao_);
        gl::glBindBuffer(GL_ARRAY_BUFFER, quad3DVbo_);
        gl::glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glDisable(GL_BLEND);
    };

    drawCelestial(sky.sunDir, 26.0f, assets_->texSun, {1.0f, 0.96f, 0.85f, 0.95f}, true);
    drawCelestial(sky.sunDir * -1.0f, 16.0f, assets_->texMoon, {0.9f, 0.95f, 1.0f, 0.9f}, true);

    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    gl::glBindVertexArray(0);
}

void Renderer::RenderWorld(const World& world, const Mat4& view, const Mat4& proj, const Vec3& camPos,
                           const SkyState& sky, bool submerged, float timeSec) {
    Mat4 mvp = proj * view;
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    gl::glUseProgram(progTerrainOpaque_);
    gl::glUniformMatrix4fv(gl::glGetUniformLocation(progTerrainOpaque_, "uMVP"), 1, GL_FALSE, mvp.m);
    gl::glUniform3f(gl::glGetUniformLocation(progTerrainOpaque_, "uCamPos"), camPos.x, camPos.y, camPos.z);
    gl::glUniform4f(gl::glGetUniformLocation(progTerrainOpaque_, "uColor"),
                    sky.terrainColor.r, sky.terrainColor.g, sky.terrainColor.b, 1.0f);
    gl::glUniform3f(gl::glGetUniformLocation(progTerrainOpaque_, "uFogColor"),
                    sky.fogColor.r, sky.fogColor.g, sky.fogColor.b);
    gl::glUniform1f(gl::glGetUniformLocation(progTerrainOpaque_, "uFogDensity"), sky.fogDensity);

    gl::glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, assets_->blockAtlasTex());
    gl::glUniform1i(gl::glGetUniformLocation(progTerrainOpaque_, "uMainTex"), 0);

    if (floorIndexCount_ > 0) {
        gl::glBindVertexArray(floorVao_);
        glDrawElements(GL_TRIANGLES, floorIndexCount_, GL_UNSIGNED_INT, nullptr);
    }

    for (const auto& g : chunkGpus_) {
        if (g.landCount > 0) {
            gl::glBindVertexArray(g.landVao);
            glDrawElements(GL_TRIANGLES, g.landCount, GL_UNSIGNED_INT, nullptr);
        }
    }

    gl::glUseProgram(progTerrainTrans_);
    gl::glUniformMatrix4fv(gl::glGetUniformLocation(progTerrainTrans_, "uMVP"), 1, GL_FALSE, mvp.m);
    gl::glUniform3f(gl::glGetUniformLocation(progTerrainTrans_, "uCamPos"), camPos.x, camPos.y, camPos.z);
    gl::glUniform4f(gl::glGetUniformLocation(progTerrainTrans_, "uColor"),
                    sky.terrainColor.r, sky.terrainColor.g, sky.terrainColor.b, 1.0f);
    gl::glUniform3f(gl::glGetUniformLocation(progTerrainTrans_, "uFogColor"),
                    sky.fogColor.r, sky.fogColor.g, sky.fogColor.b);
    gl::glUniform1f(gl::glGetUniformLocation(progTerrainTrans_, "uFogDensity"), sky.fogDensity);
    gl::glUniform1i(gl::glGetUniformLocation(progTerrainTrans_, "uMainTex"), 0);

    for (const auto& g : chunkGpus_) {
        if (g.transCount > 0) {
            gl::glBindVertexArray(g.transVao);
            glDrawElements(GL_TRIANGLES, g.transCount, GL_UNSIGNED_INT, nullptr);
        }
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    float waveScale = 0.07f;
    float t = timeSec / 20.0f;
    float wx1 = std::fmod(19.0f * t * waveScale, 1.0f);
    float wy1 = std::fmod(9.0f * t * waveScale, 1.0f);
    float wx2 = std::fmod(-16.0f * t * waveScale, 1.0f);
    float wy2 = std::fmod(-7.0f * t * waveScale, 1.0f);

    gl::glUseProgram(progWater_);
    gl::glUniformMatrix4fv(gl::glGetUniformLocation(progWater_, "uMVP"), 1, GL_FALSE, mvp.m);
    gl::glUniform3f(gl::glGetUniformLocation(progWater_, "uCamPos"), camPos.x, camPos.y, camPos.z);
    gl::glUniform4f(gl::glGetUniformLocation(progWater_, "uHorizonColor"),
                    sky.horizonColor.r, sky.horizonColor.g, sky.horizonColor.b, 1.0f);
    gl::glUniform4f(gl::glGetUniformLocation(progWater_, "uWaveOffset"), wx1, wy1, wx2, wy2);
    gl::glUniform1f(gl::glGetUniformLocation(progWater_, "uWaveScale"), waveScale);
    gl::glUniform3f(gl::glGetUniformLocation(progWater_, "uFogColor"),
                    sky.fogColor.r, sky.fogColor.g, sky.fogColor.b);
    gl::glUniform1f(gl::glGetUniformLocation(progWater_, "uFogDensity"), sky.fogDensity);
    gl::glUniform1f(gl::glGetUniformLocation(progWater_, "uWaterAlpha"), submerged ? 0.9f : 0.8f);

    gl::glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, assets_->texWaterBump);
    gl::glUniform1i(gl::glGetUniformLocation(progWater_, "uBumpMap"), 0);

    gl::glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, assets_->texOceanGradient);
    gl::glUniform1i(gl::glGetUniformLocation(progWater_, "uColorControl"), 1);

    if (oceanIndexCount_ > 0) {
        gl::glBindVertexArray(oceanVao_);
        glDrawElements(GL_TRIANGLES, oceanIndexCount_, GL_UNSIGNED_INT, nullptr);
    }

    for (const auto& g : chunkGpus_) {
        if (g.waterCount > 0) {
            gl::glBindVertexArray(g.waterVao);
            glDrawElements(GL_TRIANGLES, g.waterCount, GL_UNSIGNED_INT, nullptr);
        }
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    gl::glBindVertexArray(0);
    (void)world;
}

void Renderer::RenderBlockOutline(const Mat4& viewProj, const IntVector3& b, float blockHeight) {
    float e = 0.004f;
    float x0 = static_cast<float>(b.x) - e, x1 = static_cast<float>(b.x) + 1.0f + e;
    float y0 = static_cast<float>(b.z) - e, y1 = static_cast<float>(b.z) + blockHeight + e;
    float z0 = static_cast<float>(b.y) - e, z1 = static_cast<float>(b.y) + 1.0f + e;

    float lines[] = {
        x0,y0,z0,0,0, x1,y0,z0,0,0,
        x1,y0,z0,0,0, x1,y0,z1,0,0,
        x1,y0,z1,0,0, x0,y0,z1,0,0,
        x0,y0,z1,0,0, x0,y0,z0,0,0,
        x0,y1,z0,0,0, x1,y1,z0,0,0,
        x1,y1,z0,0,0, x1,y1,z1,0,0,
        x1,y1,z1,0,0, x0,y1,z1,0,0,
        x0,y1,z1,0,0, x0,y1,z0,0,0,
        x0,y0,z0,0,0, x0,y1,z0,0,0,
        x1,y0,z0,0,0, x1,y1,z0,0,0,
        x1,y0,z1,0,0, x1,y1,z1,0,0,
        x0,y0,z1,0,0, x0,y1,z1,0,0
    };

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glLineWidth(2.0f);

    gl::glUseProgram(progSimple3D_);
    gl::glUniformMatrix4fv(gl::glGetUniformLocation(progSimple3D_, "uMVP"), 1, GL_FALSE, viewProj.m);
    gl::glUniform4f(gl::glGetUniformLocation(progSimple3D_, "uColor"), 0.08f, 0.08f, 0.08f, 0.85f);
    gl::glUniform1i(gl::glGetUniformLocation(progSimple3D_, "uUseTex"), 0);

    gl::glBindVertexArray(outlineVao_);
    gl::glBindBuffer(GL_ARRAY_BUFFER, outlineVbo_);
    gl::glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(lines), lines);
    glDrawArrays(GL_LINES, 0, 24);

    glDisable(GL_BLEND);
    gl::glBindVertexArray(0);
}

void Renderer::RenderParticles(const Mat4& view, const Mat4& proj, const std::vector<BlockParticle>& particles) {
    if (particles.empty()) return;
    Mat4 mvp = proj * view;
    Vec3 right(view.m[0], view.m[4], view.m[8]);
    Vec3 up(view.m[1], view.m[5], view.m[9]);

    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    gl::glUseProgram(progSimple3D_);
    gl::glUniformMatrix4fv(gl::glGetUniformLocation(progSimple3D_, "uMVP"), 1, GL_FALSE, mvp.m);
    gl::glUniform1i(gl::glGetUniformLocation(progSimple3D_, "uUseTex"), 1);
    gl::glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, assets_->blockAtlasTex());
    gl::glUniform1i(gl::glGetUniformLocation(progSimple3D_, "uTex"), 0);

    gl::glBindVertexArray(quad3DVao_);
    gl::glBindBuffer(GL_ARRAY_BUFFER, quad3DVbo_);

    for (const auto& p : particles) {
        float hs = p.size * 0.5f;
        Vec3 p0 = p.pos - right * hs - up * hs;
        Vec3 p1 = p.pos + right * hs - up * hs;
        Vec3 p2 = p.pos + right * hs + up * hs;
        Vec3 p3 = p.pos - right * hs + up * hs;

        float u0 = p.uv.x, v0 = p.uv.y, u1 = p.uv.x + p.uv.w, v1 = p.uv.y + p.uv.h;
        float verts[] = {
            p0.x, p0.y, p0.z, u0, v0,
            p1.x, p1.y, p1.z, u1, v0,
            p2.x, p2.y, p2.z, u1, v1,
            p2.x, p2.y, p2.z, u1, v1,
            p3.x, p3.y, p3.z, u0, v1,
            p0.x, p0.y, p0.z, u0, v0
        };
        float alpha = std::clamp(p.life / p.maxLife * 1.5f, 0.0f, 1.0f);
        gl::glUniform4f(gl::glGetUniformLocation(progSimple3D_, "uColor"), p.color.r, p.color.g, p.color.b, alpha);
        gl::glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    gl::glBindVertexArray(0);
}

void Renderer::RenderRemotePlayers(const Mat4& view, const Mat4& proj, const Vec3& camPos,
                                   const SkyState& sky, const std::unordered_map<uint32_t, RemotePlayer>& players) {
    if (players.empty() || capsuleIndexCount_ == 0) return;

    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    gl::glUseProgram(progCapsule_);
    gl::glUniform3f(gl::glGetUniformLocation(progCapsule_, "uCamPos"), camPos.x, camPos.y, camPos.z);
    gl::glUniform3f(gl::glGetUniformLocation(progCapsule_, "uLightDir"), 0.45f, 0.85f, 0.35f);
    float bright = std::clamp(sky.terrainColor.r, 0.45f, 1.0f);
    gl::glUniform3f(gl::glGetUniformLocation(progCapsule_, "uTint"), bright, bright, bright);
    gl::glUniform3f(gl::glGetUniformLocation(progCapsule_, "uFogColor"), sky.fogColor.r, sky.fogColor.g, sky.fogColor.b);
    gl::glUniform1f(gl::glGetUniformLocation(progCapsule_, "uFogDensity"), sky.fogDensity);

    Mat4 vp = proj * view;
    gl::glBindVertexArray(capsuleVao_);
    for (const auto& kv : players) {
        const RemotePlayer& rp = kv.second;
        Mat4 model = Mat4::Translate(rp.renderPos);
        Mat4 mvp = vp * model;
        gl::glUniformMatrix4fv(gl::glGetUniformLocation(progCapsule_, "uModel"), 1, GL_FALSE, model.m);
        gl::glUniformMatrix4fv(gl::glGetUniformLocation(progCapsule_, "uMVP"), 1, GL_FALSE, mvp.m);
        glDrawElements(GL_TRIANGLES, capsuleIndexCount_, GL_UNSIGNED_INT, nullptr);
    }
    glEnable(GL_CULL_FACE);
    gl::glBindVertexArray(0);
}

bool Renderer::ProjectToScreen(const Vec3& p, const Mat4& vp, int screenW, int screenH,
                               float& outX, float& outY, float& outDist) {
    float cx = vp.m[0] * p.x + vp.m[4] * p.y + vp.m[8] * p.z + vp.m[12];
    float cy = vp.m[1] * p.x + vp.m[5] * p.y + vp.m[9] * p.z + vp.m[13];
    float cw = vp.m[3] * p.x + vp.m[7] * p.y + vp.m[11] * p.z + vp.m[15];
    if (cw <= 0.1f) return false;
    float ndcX = cx / cw;
    float ndcY = cy / cw;
    if (ndcX < -1.2f || ndcX > 1.2f || ndcY < -1.2f || ndcY > 1.2f) return false;
    outX = (ndcX * 0.5f + 0.5f) * static_cast<float>(screenW);
    outY = (0.5f - ndcY * 0.5f) * static_cast<float>(screenH);
    outDist = cw;
    return true;
}

void Renderer::BeginUI(int width, int height) {
    screenW_ = width;
    screenH_ = height;
    uiOrtho_ = Mat4::Ortho(0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, -1.0f, 1.0f);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void Renderer::EndUI() {
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    gl::glBindVertexArray(0);
}

void Renderer::DrawTexture(GLuint tex, float x, float y, float w, float h, const Color4& tint,
                           float u0, float v0, float u1, float v1) {
    float verts[] = {
        x,     y,     u0, v0, tint.r, tint.g, tint.b, tint.a,
        x + w, y,     u1, v0, tint.r, tint.g, tint.b, tint.a,
        x + w, y + h, u1, v1, tint.r, tint.g, tint.b, tint.a,
        x + w, y + h, u1, v1, tint.r, tint.g, tint.b, tint.a,
        x,     y + h, u0, v1, tint.r, tint.g, tint.b, tint.a,
        x,     y,     u0, v0, tint.r, tint.g, tint.b, tint.a
    };

    gl::glUseProgram(progUI_);
    gl::glUniformMatrix4fv(gl::glGetUniformLocation(progUI_, "uOrtho"), 1, GL_FALSE, uiOrtho_.m);
    gl::glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex ? tex : assets_->texWhite);
    gl::glUniform1i(gl::glGetUniformLocation(progUI_, "uTex"), 0);

    gl::glBindVertexArray(uiVao_);
    gl::glBindBuffer(GL_ARRAY_BUFFER, uiVbo_);
    gl::glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

void Renderer::DrawGradientRect(float x, float y, float w, float h, const Color4& topC, const Color4& botC) {
    float verts[] = {
        x,     y,     0, 0, topC.r, topC.g, topC.b, topC.a,
        x + w, y,     1, 0, topC.r, topC.g, topC.b, topC.a,
        x + w, y + h, 1, 1, botC.r, botC.g, botC.b, botC.a,
        x + w, y + h, 1, 1, botC.r, botC.g, botC.b, botC.a,
        x,     y + h, 0, 1, botC.r, botC.g, botC.b, botC.a,
        x,     y,     0, 0, topC.r, topC.g, topC.b, topC.a
    };

    gl::glUseProgram(progUI_);
    gl::glUniformMatrix4fv(gl::glGetUniformLocation(progUI_, "uOrtho"), 1, GL_FALSE, uiOrtho_.m);
    gl::glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, assets_->texWhite);
    gl::glUniform1i(gl::glGetUniformLocation(progUI_, "uTex"), 0);

    gl::glBindVertexArray(uiVao_);
    gl::glBindBuffer(GL_ARRAY_BUFFER, uiVbo_);
    gl::glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

void Renderer::DrawRect(float x, float y, float w, float h, const Color4& color) {
    DrawTexture(assets_->texWhite, x, y, w, h, color);
}

void Renderer::DrawRectOutline(float x, float y, float w, float h, float t, const Color4& color) {
    DrawRect(x, y, w, t, color);
    DrawRect(x, y + h - t, w, t, color);
    DrawRect(x, y, t, h, color);
    DrawRect(x + w - t, y, t, h, color);
}

static std::vector<uint32_t> DecodeUtf8(const std::string& s) {
    std::vector<uint32_t> out;
    size_t i = 0;
    while (i < s.size()) {
        uint8_t c = static_cast<uint8_t>(s[i]);
        if (c < 0x80) {
            out.push_back(c);
            i += 1;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) {
            uint32_t cp = ((c & 0x1F) << 6) | (static_cast<uint8_t>(s[i + 1]) & 0x3F);
            out.push_back(cp);
            i += 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < s.size()) {
            uint32_t cp = ((c & 0x0F) << 12) | ((static_cast<uint8_t>(s[i + 1]) & 0x3F) << 6) | (static_cast<uint8_t>(s[i + 2]) & 0x3F);
            out.push_back(cp);
            i += 3;
        } else {
            i += 1;
        }
    }
    return out;
}

float Renderer::MeasureText(const FontAtlas& font, const std::string& utf8, float scale) const {
    auto cps = DecodeUtf8(utf8);
    float w = 0.0f;
    for (uint32_t cp : cps) {
        auto it = font.glyphs.find(cp);
        if (it == font.glyphs.end()) it = font.glyphs.find('?');
        if (it != font.glyphs.end()) w += it->second.advance * scale;
    }
    return w;
}

void Renderer::DrawText(const FontAtlas& font, const std::string& utf8, float x, float y, float scale,
                        const Color4& color, bool shadow) {
    if (!font.texture || utf8.empty()) return;
    auto cps = DecodeUtf8(utf8);

    auto drawPass = [&](float ox, float oy, const Color4& col) {
        float penX = x + ox;
        for (uint32_t cp : cps) {
            auto it = font.glyphs.find(cp);
            if (it == font.glyphs.end()) it = font.glyphs.find('?');
            if (it == font.glyphs.end()) continue;
            const GlyphInfo& g = it->second;
            if (cp != ' ') {
                DrawTexture(font.texture, penX, y + oy, g.w * scale, g.h * scale, col, g.u0, g.v0, g.u1, g.v1);
            }
            penX += g.advance * scale;
        }
    };

    if (shadow) {
        drawPass(1.5f, 1.5f, {0.0f, 0.0f, 0.0f, color.a * 0.65f});
    }
    drawPass(0.0f, 0.0f, color);
}

void Renderer::DrawTextCentered(const FontAtlas& font, const std::string& utf8, float cx, float y, float scale,
                                const Color4& color, bool shadow) {
    float w = MeasureText(font, utf8, scale);
    DrawText(font, utf8, cx - w * 0.5f, y, scale, color, shadow);
}

void Renderer::DrawBlockIcon3D(BlockType blockType, float cx, float cy, float sizePx, int screenW, int screenH) {
    if (blockType == BlockType::Null || blockType == BlockType::Air) return;

    std::vector<TerrainVertex> verts;
    verts.reserve(36);
    float bh = Block::GetBlockHeight(blockType);

    auto addFace = [&](float x1, float y1, float z1, float x2, float y2, float z2,
                       float x3, float y3, float z3, float x4, float y4, float z4,
                       BlockFace face, float shade) {
        int texIdx = GetBlockTextureIndex(blockType, face);
        const Rect& r = assets_->blockAtlasUvs()[texIdx];
        float eu = r.w * 0.002f, ev = r.h * 0.002f;
        float u0 = r.x + eu, v0 = r.y + ev;
        float u1 = r.x + r.w - eu, v1 = r.y + r.h - ev;
        if (face == BlockFace::Side) {
            v1 = r.y + (r.h - 2.0f * ev) * bh + ev;
        }
        auto push = [&](float px, float py, float pz, float u, float v) {
            verts.push_back({px - 0.5f, pz - 0.5f, py - 0.5f, u, v, shade, shade, shade, 1.0f});
        };
        push(x1, y1, z1, u0, v0);
        push(x2, y2, z2, u0, v1);
        push(x3, y3, z3, u1, v1);
        push(x3, y3, z3, u1, v1);
        push(x4, y4, z4, u1, v0);
        push(x1, y1, z1, u0, v0);
    };

    addFace(0, 0, 0,  0, 0, bh,  1, 0, bh,  1, 0, 0,  BlockFace::Side, 0.72f);
    addFace(0, 1, 0,  0, 1, bh,  0, 0, bh,  0, 0, 0,  BlockFace::Side, 0.85f);
    addFace(1, 1, 0,  1, 1, bh,  0, 1, bh,  0, 1, 0,  BlockFace::Side, 0.72f);
    addFace(1, 0, 0,  1, 0, bh,  1, 1, bh,  1, 1, 0,  BlockFace::Side, 0.85f);
    addFace(0, 0, bh, 0, 1, bh,  1, 1, bh,  1, 0, bh, BlockFace::Top,  1.00f);
    addFace(1, 0, 0,  1, 1, 0,   0, 1, 0,   0, 0, 0,  BlockFace::Bottom, 0.55f);

    int vpSize = static_cast<int>(sizePx);
    int vpX = static_cast<int>(cx - sizePx * 0.5f);
    int vpY = static_cast<int>(screenH - (cy + sizePx * 0.5f));
    glViewport(vpX, vpY, vpSize, vpSize);

    glEnable(GL_DEPTH_TEST);
    glClear(GL_DEPTH_BUFFER_BIT);
    if (Block::IsDoubleSided(blockType)) glDisable(GL_CULL_FACE);
    else { glEnable(GL_CULL_FACE); glCullFace(GL_BACK); glFrontFace(GL_CCW); }

    Mat4 ortho = Mat4::Ortho(-0.98f, 0.98f, -0.98f, 0.98f, -5.0f, 5.0f);
    Mat4 rot = Mat4::RotateX(0.56f) * Mat4::RotateY(-0.785398f);
    Mat4 mvp = ortho * rot;

    gl::glUseProgram(progIcon3D_);
    gl::glUniformMatrix4fv(gl::glGetUniformLocation(progIcon3D_, "uMVP"), 1, GL_FALSE, mvp.m);
    gl::glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, assets_->blockAtlasTex());
    gl::glUniform1i(gl::glGetUniformLocation(progIcon3D_, "uMainTex"), 0);

    gl::glBindVertexArray(iconVao_);
    gl::glBindBuffer(GL_ARRAY_BUFFER, iconVbo_);
    gl::glBufferSubData(GL_ARRAY_BUFFER, 0, verts.size() * sizeof(TerrainVertex), verts.data());
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size()));

    glViewport(0, 0, screenW, screenH);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
}

}
