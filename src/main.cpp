#include "game.hpp"
#include <filesystem>
#include <cstring>
#include <cstdio>

namespace fs = std::filesystem;

static std::string GetExecutableDir() {
    wchar_t buf[MAX_PATH] = {};
    DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len == 0) return ".";
    fs::path p(buf);
    return p.parent_path().u8string();
}

int main(int argc, char* argv[]) {
    bool smokeTest = false;
    std::string screenshotDir;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--smoke-test") == 0) {
            smokeTest = true;
        } else if (std::strcmp(argv[i], "--screenshots") == 0 && i + 1 < argc) {
            screenshotDir = argv[++i];
        }
    }

    std::string exeDir = GetExecutableDir();
    ilands::Game game;
    if (smokeTest) {
        return game.RunSmokeTest(exeDir, screenshotDir) ? 0 : 1;
    }
    if (!game.Init(exeDir)) {
        return 1;
    }
    return game.Run();
}
