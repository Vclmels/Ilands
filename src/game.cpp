#include "game.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <cstdlib>

namespace ilands {

static void SaveFramebufferBMP(const std::string& path, int w, int h) {
    std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_BGR, GL_UNSIGNED_BYTE, pixels.data());
    uint32_t rowStride = (w * 3 + 3) & ~3u;
    uint32_t dataSize = rowStride * h;
    uint32_t fileSize = 54 + dataSize;
    uint8_t hdr[54] = {};
    hdr[0] = 'B'; hdr[1] = 'M';
    std::memcpy(hdr + 2, &fileSize, 4);
    uint32_t off = 54; std::memcpy(hdr + 10, &off, 4);
    uint32_t biSize = 40; std::memcpy(hdr + 14, &biSize, 4);
    std::memcpy(hdr + 18, &w, 4);
    std::memcpy(hdr + 22, &h, 4);
    uint16_t planes = 1; std::memcpy(hdr + 26, &planes, 2);
    uint16_t bpp = 24; std::memcpy(hdr + 28, &bpp, 2);
    std::memcpy(hdr + 34, &dataSize, 4);
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fwrite(hdr, 1, 54, f);
    std::vector<uint8_t> pad(rowStride - w * 3, 0);
    for (int y = 0; y < h; ++y) {
        std::fwrite(pixels.data() + static_cast<size_t>(y) * w * 3, 1, static_cast<size_t>(w) * 3, f);
        if (!pad.empty()) std::fwrite(pad.data(), 1, pad.size(), f);
    }
    std::fclose(f);
}

Game::~Game() {
    UnloadWorldSession();
    network_.Shutdown();
    renderer_.Shutdown();
    assets_.Unload();
    if (glCtx_) {
        SDL_GL_DeleteContext(glCtx_);
        glCtx_ = nullptr;
    }
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    SDL_Quit();
}

void Game::InitInventoryList() {
    inventory_ = {
        BlockType::Sand, BlockType::Grass, BlockType::Dirt, BlockType::Leaves, BlockType::Log,
        BlockType::Stone, BlockType::Bedrock, BlockType::Light, BlockType::Wood, BlockType::HalfWood,
        BlockType::StoneBrick, BlockType::HalfStoneBrick, BlockType::Brick, BlockType::HalfBrick,
        BlockType::Slab, BlockType::HalfSlab, BlockType::Glass, BlockType::Window,
        BlockType::Cobblestone, BlockType::Gravel, BlockType::Marble, BlockType::Gold,
        BlockType::Metal, BlockType::Crate, BlockType::Table, BlockType::Cushion,
        BlockType::Bookshelf, BlockType::Gift, BlockType::Berries, BlockType::Sakura,
        BlockType::MossStone, BlockType::Hay,
        BlockType::Red, BlockType::HalfRed, BlockType::Orange, BlockType::HalfOrange,
        BlockType::Yellow, BlockType::HalfYellow, BlockType::Green, BlockType::HalfGreen,
        BlockType::Lime, BlockType::HalfLime, BlockType::Cyan, BlockType::HalfCyan,
        BlockType::Blue, BlockType::HalfBlue, BlockType::LightBlue, BlockType::HalfLightBlue,
        BlockType::Purple, BlockType::HalfPurple, BlockType::Magenta, BlockType::HalfMagenta,
        BlockType::Pink, BlockType::HalfPink, BlockType::Brown, BlockType::HalfBrown,
        BlockType::Black, BlockType::HalfBlack, BlockType::Gray, BlockType::HalfGray,
        BlockType::Silver, BlockType::HalfSilver, BlockType::White, BlockType::HalfWhite
    };
    for (int i = 0; i < 5; ++i) {
        buildItems_[i] = inventory_[i];
    }
    activeSlot_ = 0;
}

void Game::LoadConfig() {
    playerName_ = LanNetwork::GetDefaultPlayerName();
    std::ifstream in(exeDir_ + "/saves/config.dat");
    if (!in.is_open()) return;
    std::string line;
    if (std::getline(in, line) && !line.empty()) {
        playerName_ = line.substr(0, 18);
    }
    if (std::getline(in, line) && !line.empty()) {
        directIpInput_ = line.substr(0, 32);
    }
}

void Game::SaveConfig() const {
    std::ofstream out(exeDir_ + "/saves/config.dat", std::ios::trunc);
    if (!out.is_open()) return;
    out << playerName_ << '\n' << directIpInput_ << '\n';
}

bool Game::Init(const std::string& exeDir) {
    exeDir_ = exeDir;
    InitInventoryList();

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    window_ = SDL_CreateWindow(
        "iLands",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        winW_, winH_,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN
    );
    if (!window_) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }

    glCtx_ = SDL_GL_CreateContext(window_);
    if (!glCtx_) {
        std::fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_GL_SetSwapInterval(1);

    if (!gl::LoadFunctions()) {
        std::fprintf(stderr, "Failed to load OpenGL 3.3 functions\n");
        return false;
    }

    if (!assets_.Load(exeDir_)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "iLands - Assets Missing",
                                 "Could not find folder src/assets.", window_);
        return false;
    }

    if (!renderer_.Init(assets_)) {
        std::fprintf(stderr, "Renderer initialization failed\n");
        return false;
    }

    network_.InitWinsock();
    storage_ = std::make_unique<WorldStorage>(exeDir_ + "/saves");
    LoadConfig();
    selectedWorldIdx_ = 0;
    state_ = AppState::Splash;
    splashTimer_ = 0.0f;
    running_ = true;
    return true;
}

bool Game::RunSmokeTest(const std::string& exeDir, const std::string& screenshotDir) {
    if (!Init(exeDir)) return false;

    state_ = AppState::MainMenu;
    Render();
    SDL_GL_SwapWindow(window_);
    if (!screenshotDir.empty()) SaveFramebufferBMP(screenshotDir + "/v2_mainmenu.bmp", winW_, winH_);

    state_ = AppState::SoloSelect;
    selectedWorldIdx_ = 0;
    Render();
    SDL_GL_SwapWindow(window_);
    if (!screenshotDir.empty()) SaveFramebufferBMP(screenshotDir + "/v2_solo.bmp", winW_, winH_);

    const auto& worlds = storage_->GetWorlds();
    if (worlds.empty()) return false;
    LoadWorldSession(worlds[0]);
    world_->BuildDirtyChunksAround(player_.pos.x, player_.pos.z, 1024);
    renderer_.SyncDirtyChunks(*world_);

    auto savedUpdates = storage_->LoadWorldUpdates(activeWorldMeta_.id);
    if (!network_.StartHosting(activeWorldMeta_.title, "Axel", activeWorldMeta_.seed,
                               activeWorldMeta_.type, hourOfDay_, savedUpdates, player_.pos)) {
        std::fprintf(stderr, "LAN host failed to start!\n");
        return false;
    }

    LanNetwork testClient;
    testClient.InitWinsock();
    std::string err;
    if (!testClient.ConnectToHost("127.0.0.1", network_.hostPort(), "Bauty", err)) {
        std::fprintf(stderr, "LAN client connect failed: %s\n", err.c_str());
        return false;
    }

    Vec3 clientPos = player_.pos + Vec3(0.0f, 0.0f, -6.0f);
    int cx = static_cast<int>(std::floor(clientPos.x));
    int cy = static_cast<int>(std::floor(clientPos.z));
    for (int z = World::BlocksHigh - 2; z >= 1; --z) {
        BlockType bt = world_->GetBlockType(cx, cy, z);
        if (bt != BlockType::Air && bt != BlockType::Water && bt != BlockType::Null) {
            clientPos.y = static_cast<float>(z + 1);
            break;
        }
    }

    NetWelcomeData clientWelcome;
    bool welcomeOk = false;
    for (int step = 0; step < 30; ++step) {
        totalTime_ += 0.05f;
        Update(0.05f);
        testClient.Update(0.05f, totalTime_, clientPos, 0.0f, 0.0f, hourOfDay_);
        if (!welcomeOk && testClient.PopWelcome(clientWelcome)) {
            welcomeOk = true;
            testClient.SendChatMessage("siiiiiiiiii");
            testClient.SendChatMessage("de mi mundo o ke costruimos algo");
        }
        Sleep(10);
    }

    AddChatMessage("Axel", "esta su estadio");
    network_.SendChatMessage("esta su estadio");
    for (int step = 0; step < 10; ++step) {
        totalTime_ += 0.05f;
        Update(0.05f);
        testClient.Update(0.05f, totalTime_, clientPos, 0.0f, 0.0f, hourOfDay_);
        Sleep(5);
    }

    Vec3 eye = player_.pos + Vec3(0.0f, player_.eyeHeight, 0.0f);
    Vec3 toClient = (clientPos + Vec3(0.0f, 1.0f, 0.0f)) - eye;
    player_.yaw = std::atan2(toClient.x, -toClient.z);
    player_.pitch = std::atan2(toClient.y, std::sqrt(toClient.x * toClient.x + toClient.z * toClient.z));
    hourOfDay_ = 11.0f;

    chatOpen_ = false;
    pauseOpen_ = false;
    Render();
    SDL_GL_SwapWindow(window_);
    if (!screenshotDir.empty()) SaveFramebufferBMP(screenshotDir + "/v2_multiplayer_ingame.bmp", winW_, winH_);

    chatOpen_ = true;
    chatInput_ = "holaaaaaaaaaaaa";
    Render();
    SDL_GL_SwapWindow(window_);
    if (!screenshotDir.empty()) SaveFramebufferBMP(screenshotDir + "/v2_chat.bmp", winW_, winH_);

    chatOpen_ = false;
    pauseOpen_ = true;
    Render();
    SDL_GL_SwapWindow(window_);
    if (!screenshotDir.empty()) SaveFramebufferBMP(screenshotDir + "/v2_pause_lan.bmp", winW_, winH_);

    pauseOpen_ = false;
    LanNetwork discoverer;
    discoverer.StartDiscovery();
    for (int i = 0; i < 15; ++i) {
        totalTime_ += 0.1f;
        network_.Update(0.1f, totalTime_, player_.pos, player_.yaw, player_.pitch, hourOfDay_);
        discoverer.PollDiscovery(totalTime_);
        Sleep(15);
    }
    int foundLan = static_cast<int>(discoverer.GetDiscoveredGames().size());

    testClient.Disconnect();
    discoverer.StopDiscovery();
    UnloadWorldSession();

    state_ = AppState::MultiplayerMenu;
    network_.StartDiscovery();
    Render();
    SDL_GL_SwapWindow(window_);
    if (!screenshotDir.empty()) SaveFramebufferBMP(screenshotDir + "/v2_mp_menu.bmp", winW_, winH_);
    network_.StopDiscovery();

    std::printf("SMOKE_LAN_OK: welcome=%d remotePlayers=%zu discoveredLan=%d seed=%d\n",
                welcomeOk ? 1 : 0,
                clientWelcome.received ? size_t(1) : size_t(0),
                foundLan,
                clientWelcome.seed);
    std::fflush(stdout);
    return welcomeOk;
}

void Game::LoadWorldSession(const SavedWorldMeta& meta, bool isRemoteClient,
                            const std::vector<WorldUpdateEntry>* remoteUpdates) {
    if (!isRemoteClient) {
        UnloadWorldSession();
    } else {
        SetMouseCaptured(false);
        renderer_.ResetWorldBuffers();
        world_.reset();
        particles_.clear();
    }

    isRemoteClientSession_ = isRemoteClient;
    activeWorldMeta_ = meta;
    hourOfDay_ = meta.hour;
    saveHourAccum_ = 0.0f;
    timePaused_ = false;
    inventoryOpen_ = false;
    pauseOpen_ = false;
    chatOpen_ = false;
    chatInput_.clear();
    chatHistory_.clear();
    chatScrollOffset_ = 0;
    particles_.clear();

    world_ = std::make_unique<World>(meta.seed, meta.type, assets_.blockAtlasUvs());
    std::vector<WorldUpdateEntry> updates = remoteUpdates ? *remoteUpdates : storage_->LoadWorldUpdates(meta.id);
    world_->GenerateAll(updates);

    int sx = World::BlocksWide / 2;
    int sy = World::BlocksDeep / 2;
    int spawnZ = World::SeaLevel + 2;
    for (int z = World::BlocksHigh - 2; z >= 1; --z) {
        BlockType bt = world_->GetBlockType(sx, sy, z);
        if (bt != BlockType::Air && bt != BlockType::Water && bt != BlockType::Null) {
            spawnZ = z + 1;
            break;
        }
    }
    player_ = PlayerState{};
    player_.pos = {sx + 0.5f, static_cast<float>(spawnZ) + 0.1f, sy + 0.5f};
    player_.yaw = 0.0f;
    player_.pitch = -0.15f;

    renderer_.UploadWorldStaticMeshes(*world_);
    world_->BuildDirtyChunksAround(player_.pos.x, player_.pos.z, 64);
    renderer_.SyncDirtyChunks(*world_);

    state_ = AppState::Playing;
    SetMouseCaptured(true);
}

void Game::UnloadWorldSession() {
    if (world_ && storage_ && !isRemoteClientSession_) {
        storage_->SaveWorldHour(activeWorldMeta_.id, hourOfDay_);
    }
    network_.StopHosting();
    network_.Disconnect();
    isRemoteClientSession_ = false;
    mpWaitingWelcome_ = false;
    SetMouseCaptured(false);
    SDL_StopTextInput();
    renderer_.ResetWorldBuffers();
    world_.reset();
    particles_.clear();
    chatHistory_.clear();
}

void Game::SetMouseCaptured(bool captured) {
    mouseCaptured_ = captured;
    SDL_SetRelativeMouseMode(captured ? SDL_TRUE : SDL_FALSE);
}

void Game::AddChatMessage(const std::string& sender, const std::string& text) {
    if (text.empty()) return;
    chatHistory_.push_back({sender, text, totalTime_});
    if (chatHistory_.size() > 100) {
        chatHistory_.erase(chatHistory_.begin());
    }
    chatScrollOffset_ = 0;
}

void Game::OpenChat() {
    chatOpen_ = true;
    chatInput_.clear();
    chatScrollOffset_ = 0;
    chatCursorTimer_ = 0.0f;
    lmbHeld_ = rmbHeld_ = false;
    SetMouseCaptured(false);
    SDL_StartTextInput();
}

void Game::CloseChat() {
    chatOpen_ = false;
    chatInput_.clear();
    SDL_StopTextInput();
    if (!pauseOpen_ && !inventoryOpen_) {
        SetMouseCaptured(true);
    }
}

void Game::SubmitChatInput() {
    size_t a = chatInput_.find_first_not_of(" \t\r\n");
    if (a != std::string::npos) {
        size_t b = chatInput_.find_last_not_of(" \t\r\n");
        std::string msg = chatInput_.substr(a, b - a + 1);
        std::string sender = network_.isMultiplayer() ? playerName_ : (playerName_.empty() ? "You" : playerName_);
        AddChatMessage(sender, msg);
        if (network_.isMultiplayer()) {
            network_.SendChatMessage(msg);
        }
    }
    CloseChat();
}

void Game::AttemptJoinLan(const std::string& ipText, uint16_t defaultPort) {
    if (ipText.empty()) {
        mpStatusMsg_ = "Enter an IP address or select a LAN game.";
        return;
    }
    std::string host = ipText;
    uint16_t port = defaultPort;
    size_t colon = ipText.find(':');
    if (colon != std::string::npos) {
        host = ipText.substr(0, colon);
        int parsed = std::atoi(ipText.substr(colon + 1).c_str());
        if (parsed > 0 && parsed < 65536) port = static_cast<uint16_t>(parsed);
    }
    SaveConfig();
    std::string err;
    if (network_.ConnectToHost(host, port, playerName_, err)) {
        mpWaitingWelcome_ = true;
        mpConnectTimer_ = 0.0f;
        mpStatusMsg_ = "Connecting to " + host + ":" + std::to_string(port) + "...";
    } else {
        mpWaitingWelcome_ = false;
        mpStatusMsg_ = "Error: " + err;
    }
}

int Game::Run() {
    uint64_t prevTicks = SDL_GetPerformanceCounter();
    const double invFreq = 1.0 / static_cast<double>(SDL_GetPerformanceFrequency());

    while (running_) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            ProcessEvent(ev);
        }

        uint64_t now = SDL_GetPerformanceCounter();
        float dt = static_cast<float>((now - prevTicks) * invFreq);
        prevTicks = now;
        if (dt > 0.05f) dt = 0.05f;
        if (dt < 0.0001f) dt = 0.0001f;

        fpsSmooth_ = fpsSmooth_ * 0.92f + (1.0f / dt) * 0.08f;
        totalTime_ += dt;

        Update(dt);
        Render();
        SDL_GL_SwapWindow(window_);
    }
    return 0;
}

void Game::SelectInventoryBlock(BlockType type) {
    int existing = -1;
    for (int i = 0; i < 5; ++i) {
        if (buildItems_[i] == type) { existing = i; break; }
    }
    if (existing >= 0) {
        for (int i = existing; i > 0; --i) buildItems_[i] = buildItems_[i - 1];
        buildItems_[0] = type;
    } else {
        for (int i = 4; i > 0; --i) buildItems_[i] = buildItems_[i - 1];
        buildItems_[0] = type;
    }
    activeSlot_ = 0;
    inventoryOpen_ = false;
    SetMouseCaptured(true);
}

void Game::ProcessEvent(const SDL_Event& ev) {
    if (ev.type == SDL_QUIT) {
        running_ = false;
        return;
    }
    if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
        winW_ = std::max(320, ev.window.data1);
        winH_ = std::max(240, ev.window.data2);
        return;
    }
    if (ev.type == SDL_MOUSEMOTION) {
        mouseX_ = ev.motion.x;
        mouseY_ = ev.motion.y;
        if (state_ == AppState::Playing && mouseCaptured_) {
            const float sens = 0.0024f;
            player_.yaw += static_cast<float>(ev.motion.xrel) * sens;
            player_.pitch -= static_cast<float>(ev.motion.yrel) * sens;
            player_.pitch = std::clamp(player_.pitch, -1.52f, 1.52f);
        }
        return;
    }

    if (state_ == AppState::Splash) {
        if (ev.type == SDL_KEYDOWN || ev.type == SDL_MOUSEBUTTONDOWN) {
            state_ = AppState::MainMenu;
        }
        return;
    }
    if (state_ == AppState::MainMenu) { ProcessMainMenuEvent(ev); return; }
    if (state_ == AppState::SoloSelect) { ProcessSoloSelectEvent(ev); return; }
    if (state_ == AppState::MultiplayerMenu) { ProcessMultiplayerEvent(ev); return; }
    if (state_ == AppState::Playing) { ProcessPlayingEvent(ev); return; }
}

void Game::ProcessMainMenuEvent(const SDL_Event& ev) {
    if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) {
        running_ = false;
        return;
    }
    if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
        float mx = static_cast<float>(ev.button.x);
        float my = static_cast<float>(ev.button.y);
        float btnW = std::min(400.0f, winW_ * 0.36f);
        float btnH = 60.0f;
        float gap = 16.0f;
        float bx = (winW_ - btnW) * 0.5f;
        float by0 = winH_ * 0.50f;

        if (HitRect(mx, my, bx, by0, btnW, btnH)) {
            storage_->ReloadWorlds();
            selectedWorldIdx_ = 0;
            menuModal_ = MenuModal::None;
            state_ = AppState::SoloSelect;
        } else if (HitRect(mx, my, bx, by0 + (btnH + gap), btnW, btnH)) {
            mpStatusMsg_.clear();
            mpWaitingWelcome_ = false;
            mpActiveField_ = -1;
            selectedLanIdx_ = 0;
            network_.StartDiscovery();
            state_ = AppState::MultiplayerMenu;
        } else if (HitRect(mx, my, bx, by0 + (btnH + gap) * 2.0f, btnW, btnH)) {
            running_ = false;
        }
    }
}

void Game::ProcessSoloSelectEvent(const SDL_Event& ev) {
    const auto& worlds = storage_->GetWorlds();

    if (ev.type == SDL_TEXTINPUT && (menuModal_ == MenuModal::CreateWorld || menuModal_ == MenuModal::WorldSettings)) {
        std::string& target = (menuModal_ == MenuModal::CreateWorld && activeInputField_ == 1) ? inputSeed_ : inputName_;
        for (const char* p = ev.text.text; *p; ++p) {
            char c = *p;
            if (activeInputField_ == 1 && menuModal_ == MenuModal::CreateWorld) {
                if (c >= '0' && c <= '9' && target.size() < 7) target.push_back(c);
            } else {
                if (static_cast<unsigned char>(c) >= 32 && target.size() < 24) target.push_back(c);
            }
        }
        return;
    }

    if (ev.type == SDL_KEYDOWN) {
        SDL_Keycode k = ev.key.keysym.sym;
        if (menuModal_ != MenuModal::None) {
            if (k == SDLK_ESCAPE) {
                menuModal_ = MenuModal::None;
                SDL_StopTextInput();
            } else if (k == SDLK_BACKSPACE) {
                std::string& target = (menuModal_ == MenuModal::CreateWorld && activeInputField_ == 1) ? inputSeed_ : inputName_;
                if (!target.empty()) target.pop_back();
            } else if (k == SDLK_TAB && menuModal_ == MenuModal::CreateWorld) {
                activeInputField_ = 1 - activeInputField_;
            } else if (k == SDLK_RETURN) {
                if (menuModal_ == MenuModal::CreateWorld && !inputName_.empty()) {
                    int seed = inputSeed_.empty() ? 0 : std::atoi(inputSeed_.c_str());
                    storage_->CreateWorld(inputName_, createWorldType_, seed);
                    selectedWorldIdx_ = static_cast<int>(storage_->GetWorlds().size()) - 1;
                    menuModal_ = MenuModal::None;
                    SDL_StopTextInput();
                } else if (menuModal_ == MenuModal::WorldSettings && !inputName_.empty()) {
                    if (selectedWorldIdx_ >= 0 && selectedWorldIdx_ < static_cast<int>(worlds.size())) {
                        storage_->RenameWorld(worlds[selectedWorldIdx_].id, inputName_);
                    }
                    menuModal_ = MenuModal::None;
                    SDL_StopTextInput();
                }
            }
            return;
        }
        if (k == SDLK_ESCAPE) {
            state_ = AppState::MainMenu;
        } else if (k == SDLK_RETURN && !worlds.empty()) {
            LoadWorldSession(worlds[std::clamp(selectedWorldIdx_, 0, static_cast<int>(worlds.size()) - 1)]);
        }
        return;
    }

    if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
        float mx = static_cast<float>(ev.button.x);
        float my = static_cast<float>(ev.button.y);

        if (menuModal_ == MenuModal::CreateWorld) {
            float mw = 460.0f, mh = 340.0f;
            float mx0 = (winW_ - mw) * 0.5f, my0 = (winH_ - mh) * 0.5f;
            if (HitRect(mx, my, mx0 + 30, my0 + 86, mw - 60, 40)) activeInputField_ = 0;
            if (HitRect(mx, my, mx0 + 30, my0 + 158, mw - 60, 40)) activeInputField_ = 1;
            if (HitRect(mx, my, mx0 + 30, my0 + 216, 190, 38)) createWorldType_ = WorldType::Normal;
            if (HitRect(mx, my, mx0 + mw - 220, my0 + 216, 190, 38)) createWorldType_ = WorldType::Flat;
            if (HitRect(mx, my, mx0 + 30, my0 + mh - 62, 180, 42)) {
                menuModal_ = MenuModal::None;
                SDL_StopTextInput();
            } else if (HitRect(mx, my, mx0 + mw - 210, my0 + mh - 62, 180, 42) && !inputName_.empty()) {
                int seed = inputSeed_.empty() ? 0 : std::atoi(inputSeed_.c_str());
                storage_->CreateWorld(inputName_, createWorldType_, seed);
                selectedWorldIdx_ = static_cast<int>(storage_->GetWorlds().size()) - 1;
                menuModal_ = MenuModal::None;
                SDL_StopTextInput();
            }
            return;
        }
        if (menuModal_ == MenuModal::WorldSettings) {
            float mw = 440.0f, mh = 250.0f;
            float mx0 = (winW_ - mw) * 0.5f, my0 = (winH_ - mh) * 0.5f;
            if (HitRect(mx, my, mx0 + 30, my0 + mh - 62, 118, 42)) {
                menuModal_ = MenuModal::None;
                SDL_StopTextInput();
            } else if (HitRect(mx, my, mx0 + 161, my0 + mh - 62, 118, 42) && worlds.size() > 1) {
                menuModal_ = MenuModal::DeleteConfirm;
                SDL_StopTextInput();
            } else if (HitRect(mx, my, mx0 + mw - 148, my0 + mh - 62, 118, 42) && !inputName_.empty()) {
                if (selectedWorldIdx_ >= 0 && selectedWorldIdx_ < static_cast<int>(worlds.size())) {
                    storage_->RenameWorld(worlds[selectedWorldIdx_].id, inputName_);
                }
                menuModal_ = MenuModal::None;
                SDL_StopTextInput();
            }
            return;
        }
        if (menuModal_ == MenuModal::DeleteConfirm) {
            float mw = 440.0f, mh = 200.0f;
            float mx0 = (winW_ - mw) * 0.5f, my0 = (winH_ - mh) * 0.5f;
            if (HitRect(mx, my, mx0 + 30, my0 + mh - 62, 170, 42)) {
                menuModal_ = MenuModal::None;
            } else if (HitRect(mx, my, mx0 + mw - 200, my0 + mh - 62, 170, 42)) {
                if (selectedWorldIdx_ >= 0 && selectedWorldIdx_ < static_cast<int>(worlds.size())) {
                    storage_->DeleteWorld(worlds[selectedWorldIdx_].id);
                    selectedWorldIdx_ = std::max(0, std::min(selectedWorldIdx_, static_cast<int>(storage_->GetWorlds().size()) - 1));
                }
                menuModal_ = MenuModal::None;
            }
            return;
        }

        float marginX = 24.0f, topY = 20.0f, gap = 14.0f, cardH = 112.0f;
        float cardW = (winW_ - marginX * 2.0f - gap) * 0.5f;
        int totalCards = static_cast<int>(worlds.size()) + 1;

        for (int i = 0; i < totalCards; ++i) {
            int col = i % 2;
            int row = i / 2;
            float cx = marginX + col * (cardW + gap);
            float cy = topY + row * (cardH + gap);
            if (cy + cardH > winH_ - 110.0f) break;

            if (HitRect(mx, my, cx, cy, cardW, cardH)) {
                if (i == static_cast<int>(worlds.size())) {
                    inputName_ = "iLand " + std::to_string(worlds.size() + 1);
                    inputSeed_.clear();
                    createWorldType_ = WorldType::Normal;
                    activeInputField_ = 0;
                    menuModal_ = MenuModal::CreateWorld;
                    SDL_StartTextInput();
                    return;
                }
                if (i == selectedWorldIdx_ && HitRect(mx, my, cx + cardW - 52, cy + 8, 42, 42)) {
                    inputName_ = worlds[i].title;
                    activeInputField_ = 0;
                    menuModal_ = MenuModal::WorldSettings;
                    SDL_StartTextInput();
                    return;
                }
                if (selectedWorldIdx_ == i && ev.button.clicks >= 2) {
                    LoadWorldSession(worlds[i]);
                    return;
                }
                selectedWorldIdx_ = i;
                return;
            }
        }

        if (HitRect(mx, my, 24.0f, winH_ - 96.0f, 175.0f, 72.0f)) {
            state_ = AppState::MainMenu;
            return;
        }
        if (HitRect(mx, my, winW_ - 210.0f, winH_ - 96.0f, 186.0f, 72.0f) && !worlds.empty()) {
            LoadWorldSession(worlds[std::clamp(selectedWorldIdx_, 0, static_cast<int>(worlds.size()) - 1)]);
            return;
        }
    }
}

void Game::ProcessMultiplayerEvent(const SDL_Event& ev) {
    if (ev.type == SDL_TEXTINPUT && mpActiveField_ >= 0) {
        std::string& target = (mpActiveField_ == 0) ? playerName_ : directIpInput_;
        size_t maxLen = (mpActiveField_ == 0) ? 18 : 28;
        for (const char* p = ev.text.text; *p; ++p) {
            char c = *p;
            if (static_cast<unsigned char>(c) >= 32 && target.size() < maxLen) {
                target.push_back(c);
            }
        }
        return;
    }

    if (ev.type == SDL_KEYDOWN) {
        SDL_Keycode k = ev.key.keysym.sym;
        if (k == SDLK_ESCAPE) {
            if (mpActiveField_ >= 0) {
                mpActiveField_ = -1;
                SDL_StopTextInput();
                SaveConfig();
            } else {
                network_.StopDiscovery();
                network_.Disconnect();
                mpWaitingWelcome_ = false;
                SaveConfig();
                state_ = AppState::MainMenu;
            }
            return;
        }
        if (mpActiveField_ >= 0) {
            std::string& target = (mpActiveField_ == 0) ? playerName_ : directIpInput_;
            if (k == SDLK_BACKSPACE && !target.empty()) {
                target.pop_back();
            } else if (k == SDLK_RETURN) {
                SDL_StopTextInput();
                if (mpActiveField_ == 1) {
                    mpActiveField_ = -1;
                    AttemptJoinLan(directIpInput_, LAN_DEFAULT_TCP_PORT);
                } else {
                    mpActiveField_ = -1;
                    SaveConfig();
                }
            } else if (k == SDLK_TAB) {
                mpActiveField_ = 1 - mpActiveField_;
            }
            return;
        }
        if (k == SDLK_RETURN) {
            const auto& games = network_.GetDiscoveredGames();
            if (!games.empty()) {
                const auto& g = games[std::clamp(selectedLanIdx_, 0, static_cast<int>(games.size()) - 1)];
                AttemptJoinLan(g.ip, g.port);
            } else {
                AttemptJoinLan(directIpInput_, LAN_DEFAULT_TCP_PORT);
            }
        }
        return;
    }

    if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
        float mx = static_cast<float>(ev.button.x);
        float my = static_cast<float>(ev.button.y);

        float barX = 24.0f, barY = 18.0f, barW = winW_ - 48.0f;
        float nameBoxX = barX + 140.0f, nameBoxW = 210.0f;
        float ipBoxX = barX + barW - 360.0f, ipBoxW = 220.0f;
        float connBtnX = barX + barW - 130.0f, connBtnW = 116.0f;

        if (HitRect(mx, my, nameBoxX, barY + 12, nameBoxW, 38)) {
            mpActiveField_ = 0;
            SDL_StartTextInput();
            return;
        }
        if (HitRect(mx, my, ipBoxX, barY + 12, ipBoxW, 38)) {
            mpActiveField_ = 1;
            SDL_StartTextInput();
            return;
        }
        mpActiveField_ = -1;
        SDL_StopTextInput();
        SaveConfig();

        if (HitRect(mx, my, connBtnX, barY + 12, connBtnW, 38)) {
            AttemptJoinLan(directIpInput_, LAN_DEFAULT_TCP_PORT);
            return;
        }

        const auto& games = network_.GetDiscoveredGames();
        float marginX = 24.0f, topY = 98.0f, gap = 14.0f, cardH = 112.0f;
        float cardW = (winW_ - marginX * 2.0f - gap) * 0.5f;
        for (int i = 0; i < static_cast<int>(games.size()); ++i) {
            int col = i % 2;
            int row = i / 2;
            float cx = marginX + col * (cardW + gap);
            float cy = topY + row * (cardH + gap);
            if (HitRect(mx, my, cx, cy, cardW, cardH)) {
                if (selectedLanIdx_ == i && ev.button.clicks >= 2) {
                    AttemptJoinLan(games[i].ip, games[i].port);
                    return;
                }
                selectedLanIdx_ = i;
                return;
            }
        }

        if (HitRect(mx, my, 24.0f, winH_ - 96.0f, 175.0f, 72.0f)) {
            network_.StopDiscovery();
            network_.Disconnect();
            mpWaitingWelcome_ = false;
            state_ = AppState::MainMenu;
            return;
        }
        if (HitRect(mx, my, winW_ - 210.0f, winH_ - 96.0f, 186.0f, 72.0f)) {
            if (!games.empty()) {
                const auto& g = games[std::clamp(selectedLanIdx_, 0, static_cast<int>(games.size()) - 1)];
                AttemptJoinLan(g.ip, g.port);
            } else {
                AttemptJoinLan(directIpInput_, LAN_DEFAULT_TCP_PORT);
            }
            return;
        }
    }
}

void Game::ProcessPlayingEvent(const SDL_Event& ev) {
    if (chatOpen_) {
        if (ev.type == SDL_TEXTINPUT) {
            for (const char* p = ev.text.text; *p; ++p) {
                char c = *p;
                if (static_cast<unsigned char>(c) >= 32 && chatInput_.size() < 140) {
                    chatInput_.push_back(c);
                }
            }
            return;
        }
        if (ev.type == SDL_KEYDOWN) {
            SDL_Keycode k = ev.key.keysym.sym;
            if (k == SDLK_ESCAPE) {
                CloseChat();
            } else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
                SubmitChatInput();
            } else if (k == SDLK_BACKSPACE && !chatInput_.empty()) {
                chatInput_.pop_back();
            }
            return;
        }
        if (ev.type == SDL_MOUSEWHEEL) {
            int maxScroll = std::max(0, static_cast<int>(chatHistory_.size()) - 12);
            chatScrollOffset_ = std::clamp(chatScrollOffset_ + ev.wheel.y, 0, maxScroll);
            return;
        }
        if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
            float mx = static_cast<float>(ev.button.x);
            float my = static_cast<float>(ev.button.y);
            float boxY = winH_ * 0.25f;
            float boxH = winH_ * 0.62f;
            float inY = boxY + boxH + 10.0f;
            float sendX = winW_ - 170.0f;
            if (HitRect(mx, my, sendX, inY, 156.0f, 44.0f)) {
                SubmitChatInput();
                return;
            }
            if (my < boxY - 8.0f) {
                CloseChat();
            }
        }
        return;
    }

    if (ev.type == SDL_KEYDOWN && !ev.key.repeat) {
        SDL_Keycode k = ev.key.keysym.sym;
        if (k == SDLK_ESCAPE) {
            if (inventoryOpen_) {
                inventoryOpen_ = false;
                SetMouseCaptured(true);
            } else {
                pauseOpen_ = !pauseOpen_;
                SetMouseCaptured(!pauseOpen_);
                lmbHeld_ = rmbHeld_ = false;
            }
            return;
        }
        if (k == SDLK_t && !pauseOpen_ && !inventoryOpen_) {
            OpenChat();
            return;
        }
        if (k == SDLK_e || k == SDLK_i || k == SDLK_TAB) {
            if (!pauseOpen_) {
                inventoryOpen_ = !inventoryOpen_;
                SetMouseCaptured(!inventoryOpen_);
                lmbHeld_ = rmbHeld_ = false;
            }
            return;
        }
        if (pauseOpen_ || inventoryOpen_) return;

        if (k >= SDLK_1 && k <= SDLK_5) activeSlot_ = static_cast<int>(k - SDLK_1);
        else if (k == SDLK_g) {
            hourOfDay_ = std::fmod(hourOfDay_ + 2.0f, 24.0f);
        } else if (k == SDLK_p) {
            timePaused_ = !timePaused_;
        } else if (k == SDLK_F3) {
            showDebug_ = !showDebug_;
        }
        return;
    }

    if (ev.type == SDL_MOUSEWHEEL && !pauseOpen_ && !inventoryOpen_) {
        if (ev.wheel.y > 0) activeSlot_ = (activeSlot_ + 4) % 5;
        else if (ev.wheel.y < 0) activeSlot_ = (activeSlot_ + 1) % 5;
        return;
    }

    if (ev.type == SDL_MOUSEBUTTONDOWN) {
        float mx = static_cast<float>(ev.button.x);
        float my = static_cast<float>(ev.button.y);

        if (inventoryOpen_ && ev.button.button == SDL_BUTTON_LEFT) {
            constexpr int cols = 10;
            float cell = 64.0f;
            float gap = 8.0f;
            int rows = (static_cast<int>(inventory_.size()) + cols - 1) / cols;
            float gridW = cols * cell + (cols - 1) * gap;
            float gridH = rows * cell + (rows - 1) * gap;
            float gx = (winW_ - gridW) * 0.5f;
            float gy = (winH_ - gridH) * 0.5f + 18.0f;

            if (HitRect(mx, my, gx + gridW - 22, gy - 54, 34, 34)) {
                inventoryOpen_ = false;
                SetMouseCaptured(true);
                return;
            }
            for (int i = 0; i < static_cast<int>(inventory_.size()); ++i) {
                int c = i % cols;
                int r = i / cols;
                float cx = gx + c * (cell + gap);
                float cy = gy + r * (cell + gap);
                if (HitRect(mx, my, cx, cy, cell, cell)) {
                    SelectInventoryBlock(inventory_[i]);
                    return;
                }
            }
            return;
        }

        if (pauseOpen_ && ev.button.button == SDL_BUTTON_LEFT) {
            float mw = 390.0f, mh = 290.0f;
            float mx0 = (winW_ - mw) * 0.5f, my0 = (winH_ - mh) * 0.5f;
            float bx = mx0 + 30.0f, bw = mw - 60.0f;

            if (!isRemoteClientSession_ && HitRect(mx, my, bx, my0 + 68, bw, 44)) {
                if (network_.isHosting()) {
                    network_.StopHosting();
                    AddChatMessage("", "LAN mode disabled.");
                } else {
                    auto updates = storage_->LoadWorldUpdates(activeWorldMeta_.id);
                    if (network_.StartHosting(activeWorldMeta_.title, playerName_, activeWorldMeta_.seed,
                                              activeWorldMeta_.type, hourOfDay_, updates, player_.pos)) {
                        AddChatMessage("", "LAN mode enabled (" + network_.localIp() + ":" + std::to_string(network_.hostPort()) + ")");
                    } else {
                        AddChatMessage("", "Failed to open LAN port.");
                    }
                }
            } else if (HitRect(mx, my, bx, my0 + 148, bw, 44)) {
                UnloadWorldSession();
                storage_->ReloadWorlds();
                state_ = AppState::MainMenu;
            } else if (HitRect(mx, my, bx, my0 + 206, bw, 44)) {
                UnloadWorldSession();
                running_ = false;
            }
            return;
        }

        if (!mouseCaptured_) {
            SetMouseCaptured(true);
            return;
        }

        if (ev.button.button == SDL_BUTTON_LEFT) {
            lmbHeld_ = true;
            PerformBreak();
            actionCooldown_ = 0.24f;
        } else if (ev.button.button == SDL_BUTTON_RIGHT) {
            rmbHeld_ = true;
            PerformPlace();
            actionCooldown_ = 0.24f;
        } else if (ev.button.button == SDL_BUTTON_MIDDLE) {
            PerformPickBlock();
        }
    }

    if (ev.type == SDL_MOUSEBUTTONUP) {
        if (ev.button.button == SDL_BUTTON_LEFT) lmbHeld_ = false;
        if (ev.button.button == SDL_BUTTON_RIGHT) rmbHeld_ = false;
    }
}

void Game::Update(float dt) {
    if (state_ == AppState::Splash) {
        splashTimer_ += dt;
        if (splashTimer_ >= 1.4f) state_ = AppState::MainMenu;
        return;
    }

    if (state_ == AppState::MultiplayerMenu) {
        network_.PollDiscovery(totalTime_);
        if (mpWaitingWelcome_) {
            mpConnectTimer_ += dt;
            network_.Update(dt, totalTime_, player_.pos, player_.yaw, player_.pitch, hourOfDay_);
            NetWelcomeData w;
            if (network_.PopWelcome(w)) {
                mpWaitingWelcome_ = false;
                network_.StopDiscovery();
                SavedWorldMeta remoteMeta;
                remoteMeta.id = -1;
                remoteMeta.seed = w.seed;
                remoteMeta.type = w.worldType;
                remoteMeta.title = w.worldTitle.empty() ? "LAN iLand" : w.worldTitle;
                remoteMeta.hour = w.hourOfDay;
                LoadWorldSession(remoteMeta, true, &w.updates);
                AddChatMessage("", "Connected to " + remoteMeta.title + ".");
                return;
            }
            std::string reason;
            if (network_.ConsumeDisconnected(reason) || mpConnectTimer_ > 5.0f) {
                mpWaitingWelcome_ = false;
                network_.Disconnect();
                mpStatusMsg_ = reason.empty() ? "Connection timed out." : reason;
            }
        }
        return;
    }

    if (state_ == AppState::Playing) {
        UpdatePlaying(dt);
    }
}

void Game::UpdateNetworking(float dt) {
    if (!network_.isMultiplayer() || !world_) return;

    network_.Update(dt, totalTime_, player_.pos, player_.yaw, player_.pitch, hourOfDay_);

    for (const auto& ce : network_.TakeChatEvents()) {
        AddChatMessage(ce.sender, ce.message);
    }

    auto blockEvents = network_.TakeBlockEvents();
    if (!blockEvents.empty()) {
        SkyState sky = Renderer::ComputeSkyState(hourOfDay_, player_.submerged, 15.0f);
        for (const auto& be : blockEvents) {
            if (be.isDestroy) {
                BlockType destroyed = BlockType::Air;
                std::vector<WorldUpdateEntry> updates;
                if (world_->TryDestroyBlock({be.x, be.y, be.z}, destroyed, updates)) {
                    SpawnBreakParticles(destroyed, {be.x, be.y, be.z}, sky);
                    if (network_.isHosting()) {
                        storage_->AppendWorldUpdates(activeWorldMeta_.id, updates);
                        for (const auto& u : updates) network_.HostRecordWorldUpdate(u);
                    }
                }
            } else {
                world_->SetBlockType(be.x, be.y, be.z, be.type, true);
                if (network_.isHosting()) {
                    WorldUpdateEntry u{be.x, be.y, be.z, be.type};
                    storage_->AppendWorldUpdates(activeWorldMeta_.id, {u});
                    network_.HostRecordWorldUpdate(u);
                }
            }
        }
        world_->BuildDirtyChunksAround(player_.pos.x, player_.pos.z, 16);
        renderer_.SyncDirtyChunks(*world_);
    }

    float syncedHour = 0.0f;
    if (network_.isClient() && network_.ConsumeTimeSync(syncedHour)) {
        hourOfDay_ = syncedHour;
    }

    std::string reason;
    if (network_.isClient() && network_.ConsumeDisconnected(reason)) {
        UnloadWorldSession();
        mpStatusMsg_ = reason;
        network_.StartDiscovery();
        state_ = AppState::MultiplayerMenu;
    }
}

void Game::UpdatePlaying(float dt) {
    if (!world_) return;

    world_->BuildDirtyChunksAround(player_.pos.x, player_.pos.z, 16);
    renderer_.SyncDirtyChunks(*world_);

    UpdateNetworking(dt);
    if (!world_ || state_ != AppState::Playing) return;

    if (chatOpen_) chatCursorTimer_ += dt;

    if (!pauseOpen_ || network_.isMultiplayer()) {
        if (!timePaused_ && !network_.isClient()) {
            constexpr float dayMin = 3.0f;
            constexpr float nightMin = 2.0f;
            if (hourOfDay_ >= 6.0f && hourOfDay_ < 18.0f) {
                hourOfDay_ += dt * 12.0f / (dayMin * 60.0f);
            } else {
                hourOfDay_ += dt * 12.0f / (nightMin * 60.0f);
            }
            if (hourOfDay_ >= 24.0f) hourOfDay_ -= 24.0f;
        }
    }

    if (!isRemoteClientSession_) {
        saveHourAccum_ += dt;
        if (saveHourAccum_ >= 10.0f) {
            storage_->SaveWorldHour(activeWorldMeta_.id, hourOfDay_);
            saveHourAccum_ = 0.0f;
        }
    }

    if (!pauseOpen_ && !inventoryOpen_ && !chatOpen_) {
        UpdatePlayerPhysics(dt);
    }

    Vec3 eye = player_.pos + Vec3(0.0f, player_.eyeHeight, 0.0f);
    Vec3 look(
        std::sin(player_.yaw) * std::cos(player_.pitch),
        std::sin(player_.pitch),
        -std::cos(player_.yaw) * std::cos(player_.pitch)
    );
    aimedHit_ = world_->Raycast(eye, look, 5.5f);

    if (actionCooldown_ > 0.0f) actionCooldown_ -= dt;
    if (mouseCaptured_ && !chatOpen_ && !inventoryOpen_ && !pauseOpen_ && actionCooldown_ <= 0.0f) {
        if (lmbHeld_) {
            PerformBreak();
            actionCooldown_ = 0.22f;
        } else if (rmbHeld_) {
            PerformPlace();
            actionCooldown_ = 0.22f;
        }
    }

    UpdateParticles(dt);
}

bool Game::CollidesAt(const Vec3& pos) const {
    float r = player_.radius;
    int minX = static_cast<int>(std::floor(pos.x - r));
    int maxX = static_cast<int>(std::floor(pos.x + r));
    int minZ = static_cast<int>(std::floor(pos.z - r));
    int maxZ = static_cast<int>(std::floor(pos.z + r));
    int minY = static_cast<int>(std::floor(pos.y));
    int maxY = static_cast<int>(std::floor(pos.y + player_.height - 0.01f));

    for (int x = minX; x <= maxX; ++x) {
        for (int z = minZ; z <= maxZ; ++z) {
            for (int y = minY; y <= maxY; ++y) {
                BlockType bt = world_->GetBlockType(x, z, y);
                if (bt == BlockType::Null || bt == BlockType::Air || bt == BlockType::Water) {
                    continue;
                }
                float bh = Block::GetBlockHeight(bt);
                float bTop = static_cast<float>(y) + bh;
                if (pos.y < bTop - 0.001f && pos.y + player_.height > static_cast<float>(y) + 0.001f) {
                    return true;
                }
            }
        }
    }
    return false;
}

void Game::MoveAxis(Vec3& pos, float dx, float dy, float dz, bool& hitAxis) {
    hitAxis = false;
    Vec3 next = pos + Vec3(dx, dy, dz);
    if (!CollidesAt(next)) {
        pos = next;
        return;
    }
    if (dy == 0.0f && player_.grounded) {
        Vec3 stepUp = next + Vec3(0.0f, 0.55f, 0.0f);
        if (!CollidesAt(stepUp)) {
            pos = stepUp;
            return;
        }
    }
    float lo = 0.0f, hi = 1.0f;
    for (int i = 0; i < 6; ++i) {
        float mid = (lo + hi) * 0.5f;
        if (CollidesAt(pos + Vec3(dx * mid, dy * mid, dz * mid))) hi = mid;
        else lo = mid;
    }
    pos += Vec3(dx * lo, dy * lo, dz * lo);
    hitAxis = true;
}

void Game::CheckBoundsRescue() {
    player_.pos.x = std::clamp(player_.pos.x, 0.4f, static_cast<float>(World::BlocksWide) - 0.4f);
    player_.pos.z = std::clamp(player_.pos.z, 0.4f, static_cast<float>(World::BlocksDeep) - 0.4f);
    player_.pos.y = std::clamp(player_.pos.y, 1.0f, static_cast<float>(World::BlocksHigh) + 2.5f);

    if (CollidesAt(player_.pos)) {
        for (int tries = 0; tries < 10; ++tries) {
            player_.pos.y += 0.5f;
            if (!CollidesAt(player_.pos)) break;
        }
    }
}

void Game::UpdatePlayerPhysics(float dt) {
    const uint8_t* keys = SDL_GetKeyboardState(nullptr);
    float moveF = 0.0f, moveR = 0.0f;
    if (keys[SDL_SCANCODE_W]) moveF += 1.0f;
    if (keys[SDL_SCANCODE_S]) moveF -= 1.0f;
    if (keys[SDL_SCANCODE_D]) moveR += 1.0f;
    if (keys[SDL_SCANCODE_A]) moveR -= 1.0f;
    bool jumpKey = keys[SDL_SCANCODE_SPACE] != 0;
    bool downKey = (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_LCTRL]) != 0;

    int bx = static_cast<int>(std::floor(player_.pos.x));
    int by = static_cast<int>(std::floor(player_.pos.z));
    int bz = static_cast<int>(std::floor(player_.pos.y + 0.5f));
    player_.inWater = (world_->GetBlockType(bx, by, bz) == BlockType::Water);

    Vec3 eye = player_.pos + Vec3(0.0f, player_.eyeHeight, 0.0f);
    int ex = static_cast<int>(std::floor(eye.x));
    int ey = static_cast<int>(std::floor(eye.z));
    int ez = static_cast<int>(std::floor(eye.y));
    player_.submerged = false;
    if (world_->GetBlockType(ex, ey, ez) == BlockType::Water) {
        if (world_->GetBlockType(ex, ey, ez + 1) != BlockType::Air) {
            player_.submerged = true;
        } else {
            float surf = static_cast<float>(ez) + Block::GetBlockHeight(BlockType::Water) + 0.01f;
            if (eye.y < surf) player_.submerged = true;
        }
    }

    Vec3 forward(std::sin(player_.yaw), 0.0f, -std::cos(player_.yaw));
    Vec3 right(std::cos(player_.yaw), 0.0f, std::sin(player_.yaw));
    Vec3 wishDir = forward * moveF + right * moveR;
    if (wishDir.lengthSq() > 1e-4f) wishDir = wishDir.normalized();

    if (player_.inWater) {
        float swimSpeed = 4.2f;
        player_.vel.x = wishDir.x * swimSpeed;
        player_.vel.z = wishDir.z * swimSpeed;
        if (jumpKey) {
            player_.vel.y = 4.4f;
            if (world_->GetBlockType(bx, by, bz + 1) == BlockType::Air) player_.vel.y = 5.4f;
        } else if (downKey) {
            player_.vel.y = -4.0f;
        } else {
            player_.vel.y = player_.vel.y * 0.85f - 1.5f * dt;
            if (player_.vel.y < -2.2f) player_.vel.y = -2.2f;
        }
        player_.grounded = false;
    } else {
        float walkSpeed = 5.8f;
        float accel = player_.grounded ? 28.0f : 12.0f;
        float targetX = wishDir.x * walkSpeed;
        float targetZ = wishDir.z * walkSpeed;
        player_.vel.x += std::clamp(targetX - player_.vel.x, -accel * dt, accel * dt);
        player_.vel.z += std::clamp(targetZ - player_.vel.z, -accel * dt, accel * dt);

        player_.vel.y -= 20.0f * dt;
        if (player_.vel.y < -22.0f) player_.vel.y = -22.0f;

        if (jumpKey && player_.grounded) {
            player_.vel.y = 7.4f;
            player_.grounded = false;
        }
    }

    bool hitX = false, hitY = false, hitZ = false;
    MoveAxis(player_.pos, player_.vel.x * dt, 0.0f, 0.0f, hitX);
    if (hitX) player_.vel.x = 0.0f;
    MoveAxis(player_.pos, 0.0f, 0.0f, player_.vel.z * dt, hitZ);
    if (hitZ) player_.vel.z = 0.0f;
    MoveAxis(player_.pos, 0.0f, player_.vel.y * dt, 0.0f, hitY);
    if (hitY) {
        if (player_.vel.y < 0.0f) player_.grounded = true;
        player_.vel.y = 0.0f;
    } else {
        player_.grounded = false;
    }

    CheckBoundsRescue();
}

void Game::SpawnBreakParticles(BlockType type, const IntVector3& pos, const SkyState& sky) {
    int texIdx = GetBlockTextureIndex(type, BlockFace::Side);
    const Rect& tile = assets_.blockAtlasUvs()[texIdx];
    int skyL = world_->GetBlockLightLevel(pos.x, pos.y, pos.z, BlockLightType::Sky);
    int emL = world_->GetBlockLightLevel(pos.x, pos.y, pos.z, BlockLightType::Emission);
    float s = std::max(0.25f, std::max(world_->lightAmount(skyL) * sky.terrainColor.r, world_->lightAmount(emL)));

    for (int i = 0; i < 12; ++i) {
        BlockParticle p;
        float rx = (std::rand() / static_cast<float>(RAND_MAX) - 0.5f);
        float ry = (std::rand() / static_cast<float>(RAND_MAX) - 0.5f);
        float rz = (std::rand() / static_cast<float>(RAND_MAX) - 0.5f);
        p.pos = {pos.x + 0.5f + rx * 0.5f, pos.z + 0.5f + ry * 0.5f, pos.y + 0.5f + rz * 0.5f};
        p.vel = {rx * 3.2f, 2.0f + std::abs(ry) * 3.0f, rz * 3.2f};
        p.maxLife = 0.45f + (std::rand() / static_cast<float>(RAND_MAX)) * 0.35f;
        p.life = p.maxLife;
        p.size = 0.11f + (std::rand() / static_cast<float>(RAND_MAX)) * 0.08f;
        p.color = {s, s, s, 1.0f};
        float su = (std::rand() / static_cast<float>(RAND_MAX)) * 0.7f;
        float sv = (std::rand() / static_cast<float>(RAND_MAX)) * 0.7f;
        p.uv = {tile.x + tile.w * su, tile.y + tile.h * sv, tile.w * 0.28f, tile.h * 0.28f};
        particles_.push_back(p);
    }
}

void Game::UpdateParticles(float dt) {
    for (auto& p : particles_) {
        p.life -= dt;
        p.vel.y -= 12.0f * dt;
        p.pos += p.vel * dt;
    }
    particles_.erase(std::remove_if(particles_.begin(), particles_.end(),
                                    [](const BlockParticle& p) { return p.life <= 0.0f; }),
                     particles_.end());
}

void Game::PerformBreak() {
    if (!world_ || !aimedHit_.hit) return;
    BlockType destroyed = BlockType::Air;
    std::vector<WorldUpdateEntry> updates;
    if (world_->TryDestroyBlock(aimedHit_.blockPos, destroyed, updates)) {
        if (!isRemoteClientSession_) {
            storage_->AppendWorldUpdates(activeWorldMeta_.id, updates);
            for (const auto& u : updates) network_.HostRecordWorldUpdate(u);
        }
        network_.SendBlockChange(aimedHit_.blockPos.x, aimedHit_.blockPos.y, aimedHit_.blockPos.z, BlockType::Air, true);
        SkyState sky = Renderer::ComputeSkyState(hourOfDay_, player_.submerged, 15.0f);
        SpawnBreakParticles(destroyed, aimedHit_.blockPos, sky);
        world_->BuildDirtyChunksAround(player_.pos.x, player_.pos.z, 16);
        renderer_.SyncDirtyChunks(*world_);
    }
}

void Game::PerformPlace() {
    if (!world_ || !aimedHit_.hit) return;
    BlockType placeType = buildItems_[activeSlot_];
    std::vector<WorldUpdateEntry> updates;
    if (world_->TryBuildBlock(aimedHit_.placePos, placeType, player_.pos, player_.radius, player_.height, updates)) {
        if (!isRemoteClientSession_) {
            storage_->AppendWorldUpdates(activeWorldMeta_.id, updates);
            for (const auto& u : updates) network_.HostRecordWorldUpdate(u);
        }
        network_.SendBlockChange(aimedHit_.placePos.x, aimedHit_.placePos.y, aimedHit_.placePos.z, placeType, false);
        world_->BuildDirtyChunksAround(player_.pos.x, player_.pos.z, 16);
        renderer_.SyncDirtyChunks(*world_);
    }
}

void Game::PerformPickBlock() {
    if (!world_ || !aimedHit_.hit) return;
    BlockType bt = world_->GetBlockType(aimedHit_.blockPos.x, aimedHit_.blockPos.y, aimedHit_.blockPos.z);
    for (auto it : inventory_) {
        if (it == bt) {
            SelectInventoryBlock(bt);
            break;
        }
    }
}

void Game::Render() {
    if (state_ == AppState::Splash) RenderSplash();
    else if (state_ == AppState::MainMenu) RenderMainMenu();
    else if (state_ == AppState::SoloSelect) RenderSoloSelect();
    else if (state_ == AppState::MultiplayerMenu) RenderMultiplayerMenu();
    else RenderPlaying();
}

void Game::RenderBackground() {
    float bgScale = std::max(winW_ / 2048.0f, winH_ / 1024.0f);
    float bgW = 2048.0f * bgScale;
    float bgH = 1024.0f * bgScale;
    renderer_.DrawTexture(assets_.texBackground, (winW_ - bgW) * 0.5f, (winH_ - bgH) * 0.5f, bgW, bgH);
}

void Game::RenderSplash() {
    renderer_.BeginFrame(winW_, winH_, {0.06f, 0.14f, 0.24f, 1.0f});
    renderer_.BeginUI(winW_, winH_);
    float scale = std::max(winW_ / 2048.0f, winH_ / 1024.0f);
    float w = 2048.0f * scale;
    float h = 1024.0f * scale;
    float alpha = std::min(1.0f, splashTimer_ * 2.5f);
    renderer_.DrawTexture(assets_.texSplash, (winW_ - w) * 0.5f, (winH_ - h) * 0.5f, w, h, {1, 1, 1, alpha});
    renderer_.EndUI();
}

void Game::RenderMainMenu() {
    renderer_.BeginFrame(winW_, winH_, {0.10f, 0.28f, 0.42f, 1.0f});
    renderer_.BeginUI(winW_, winH_);
    RenderBackground();

    float logoW = std::min(560.0f, winW_ * 0.46f);
    float logoH = logoW * 0.5f;
    renderer_.DrawTexture(assets_.texLogo, (winW_ - logoW) * 0.5f, winH_ * 0.06f, logoW, logoH);

    float btnW = std::min(400.0f, winW_ * 0.36f);
    float btnH = 60.0f;
    float gap = 16.0f;
    float bx = (winW_ - btnW) * 0.5f;
    float by0 = winH_ * 0.50f;

    auto drawGlossyBtn = [&](float y, const char* label, const Color4& topC, const Color4& botC, GLuint leftIcon) {
        bool hov = HitRect(static_cast<float>(mouseX_), static_cast<float>(mouseY_), bx, y, btnW, btnH);
        Color4 t = hov ? Color4{std::min(1.0f, topC.r + 0.08f), std::min(1.0f, topC.g + 0.08f), std::min(1.0f, topC.b + 0.08f), topC.a} : topC;
        Color4 b = hov ? Color4{std::min(1.0f, botC.r + 0.08f), std::min(1.0f, botC.g + 0.08f), std::min(1.0f, botC.b + 0.08f), botC.a} : botC;
        renderer_.DrawRect(bx + 3, y + 4, btnW, btnH, {0.0f, 0.0f, 0.0f, 0.28f});
        renderer_.DrawGradientRect(bx, y, btnW, btnH, t, b);
        renderer_.DrawRectOutline(bx, y, btnW, btnH, 1.5f, {1.0f, 1.0f, 1.0f, 0.28f});

        float textScale = 0.96f;
        float tw = renderer_.MeasureText(assets_.fontBold, label, textScale);
        if (leftIcon) {
            float iconSz = 32.0f;
            float totalW = iconSz + 12.0f + tw;
            float sx = bx + (btnW - totalW) * 0.5f;
            renderer_.DrawTexture(leftIcon, sx, y + (btnH - iconSz) * 0.5f, iconSz, iconSz);
            renderer_.DrawText(assets_.fontBold, label, sx + iconSz + 12.0f, y + (btnH - assets_.fontBold.lineHeight * textScale) * 0.5f, textScale, {1, 1, 1, 0.98f}, true);
        } else {
            renderer_.DrawTextCentered(assets_.fontBold, label, bx + btnW * 0.5f,
                                       y + (btnH - assets_.fontBold.lineHeight * textScale) * 0.5f,
                                       textScale, {1, 1, 1, 0.98f}, true);
        }
    };

    drawGlossyBtn(by0, "Play Solo",
                  {0.28f, 0.62f, 0.88f, 0.90f}, {0.16f, 0.48f, 0.76f, 0.94f}, 0);
    drawGlossyBtn(by0 + (btnH + gap), "Multiplayer",
                  {0.28f, 0.62f, 0.88f, 0.90f}, {0.16f, 0.48f, 0.76f, 0.94f}, assets_.texUser);
    drawGlossyBtn(by0 + (btnH + gap) * 2.0f, "Exit",
                  {0.05f, 0.44f, 0.08f, 0.92f}, {0.02f, 0.30f, 0.04f, 0.96f}, 0);

    renderer_.EndUI();
}

void Game::RenderSoloSelect() {
    renderer_.BeginFrame(winW_, winH_, {0.10f, 0.28f, 0.42f, 1.0f});
    renderer_.BeginUI(winW_, winH_);
    RenderBackground();

    const auto& worlds = storage_->GetWorlds();
    float marginX = 24.0f, topY = 20.0f, gap = 14.0f, cardH = 112.0f;
    float cardW = (winW_ - marginX * 2.0f - gap) * 0.5f;
    int totalCards = static_cast<int>(worlds.size()) + 1;

    for (int i = 0; i < totalCards; ++i) {
        int col = i % 2;
        int row = i / 2;
        float cx = marginX + col * (cardW + gap);
        float cy = topY + row * (cardH + gap);
        if (cy + cardH > winH_ - 110.0f) break;

        bool isCreateCard = (i == static_cast<int>(worlds.size()));
        bool isSel = (!isCreateCard && i == selectedWorldIdx_);
        bool hov = (menuModal_ == MenuModal::None) && HitRect(static_cast<float>(mouseX_), static_cast<float>(mouseY_), cx, cy, cardW, cardH);

        renderer_.DrawRect(cx + 2, cy + 3, cardW, cardH, {0, 0, 0, 0.24f});

        if (isSel) {
            renderer_.DrawGradientRect(cx, cy, cardW, cardH,
                                       {0.78f, 0.62f, 0.46f, 0.88f},
                                       {0.44f, 0.30f, 0.18f, 0.90f});
            renderer_.DrawRectOutline(cx, cy, cardW, cardH, 2.0f, {0.22f, 0.13f, 0.06f, 0.78f});
        } else {
            float lift = hov ? 0.05f : 0.0f;
            renderer_.DrawGradientRect(cx, cy, cardW, cardH,
                                       {0.88f + lift, 0.90f + lift, 0.92f + lift, 0.80f},
                                       {0.62f + lift, 0.65f + lift, 0.68f + lift, 0.82f});
            renderer_.DrawRectOutline(cx, cy, cardW, cardH, 1.5f, {0.26f, 0.30f, 0.34f, 0.52f});
        }

        float iconSz = 84.0f;
        renderer_.DrawTexture(isCreateCard ? assets_.texAddIcon : assets_.texWorldSquare,
                              cx + 14.0f, cy + (cardH - iconSz) * 0.5f, iconSz, iconSz);

        std::string title = isCreateCard ? "Create New iLand" : worlds[i].title;
        renderer_.DrawText(assets_.fontBold, title, cx + 116.0f, cy + (cardH - assets_.fontBold.lineHeight * 0.86f) * 0.5f,
                           0.86f, {0.10f, 0.10f, 0.10f, 0.95f}, false);

        if (isSel) {
            bool gearHov = (menuModal_ == MenuModal::None) && HitRect(static_cast<float>(mouseX_), static_cast<float>(mouseY_), cx + cardW - 52, cy + 8, 42, 42);
            renderer_.DrawTexture(assets_.texGear, cx + cardW - 48.0f, cy + 12.0f, 34.0f, 34.0f,
                                  gearHov ? Color4{0.02f, 0.02f, 0.02f, 1.0f} : Color4{0.12f, 0.12f, 0.12f, 0.92f});
        }
    }

    {
        float bx = 24.0f, by = winH_ - 96.0f, bw = 175.0f, bh = 72.0f;
        bool hov = (menuModal_ == MenuModal::None) && HitRect(static_cast<float>(mouseX_), static_cast<float>(mouseY_), bx, by, bw, bh);
        renderer_.DrawRect(bx + 2, by + 3, bw, bh, {0, 0, 0, 0.28f});
        renderer_.DrawGradientRect(bx, by, bw, bh,
                                   hov ? Color4{0.96f, 0.97f, 0.98f, 0.96f} : Color4{0.90f, 0.92f, 0.94f, 0.92f},
                                   hov ? Color4{0.78f, 0.80f, 0.82f, 0.96f} : Color4{0.70f, 0.72f, 0.75f, 0.92f});
        renderer_.DrawRectOutline(bx, by, bw, bh, 1.5f, {0.45f, 0.48f, 0.52f, 0.65f});
        renderer_.DrawTextCentered(assets_.fontBold, "Back", bx + bw * 0.5f,
                                   by + (bh - assets_.fontBold.lineHeight * 0.92f) * 0.5f,
                                   0.92f, {0.10f, 0.10f, 0.10f, 0.95f}, false);
    }

    {
        float bx = winW_ - 210.0f, by = winH_ - 96.0f, bw = 186.0f, bh = 72.0f;
        bool hov = (menuModal_ == MenuModal::None) && HitRect(static_cast<float>(mouseX_), static_cast<float>(mouseY_), bx, by, bw, bh);
        renderer_.DrawRect(bx + 2, by + 3, bw, bh, {0, 0, 0, 0.30f});
        renderer_.DrawGradientRect(bx, by, bw, bh,
                                   hov ? Color4{0.08f, 0.52f, 0.12f, 0.96f} : Color4{0.04f, 0.42f, 0.08f, 0.94f},
                                   hov ? Color4{0.04f, 0.36f, 0.07f, 0.96f} : Color4{0.02f, 0.28f, 0.04f, 0.96f});
        renderer_.DrawRectOutline(bx, by, bw, bh, 1.5f, {1.0f, 1.0f, 1.0f, 0.22f});
        renderer_.DrawTextCentered(assets_.fontBold, "Play", bx + bw * 0.5f,
                                   by + (bh - assets_.fontBold.lineHeight * 0.96f) * 0.5f,
                                   0.96f, {0.96f, 0.98f, 0.96f, 1.0f}, true);
    }

    if (menuModal_ != MenuModal::None) {
        renderer_.DrawRect(0, 0, static_cast<float>(winW_), static_cast<float>(winH_), {0, 0, 0, 0.58f});
        if (menuModal_ == MenuModal::CreateWorld) {
            float mw = 460.0f, mh = 340.0f;
            float mx0 = (winW_ - mw) * 0.5f, my0 = (winH_ - mh) * 0.5f;
            renderer_.DrawRect(mx0, my0, mw, mh, {0.09f, 0.13f, 0.20f, 0.96f});
            renderer_.DrawRectOutline(mx0, my0, mw, mh, 2.0f, {0.5f, 0.82f, 1.0f, 0.7f});
            renderer_.DrawTextCentered(assets_.fontBold, "CREATE NEW iLAND", mx0 + mw * 0.5f, my0 + 18, 0.82f, {1, 1, 1, 1});

            renderer_.DrawText(assets_.fontRegular, "World Name:", mx0 + 30, my0 + 62, 0.75f, {0.8f, 0.9f, 1.0f, 1});
            renderer_.DrawRect(mx0 + 30, my0 + 86, mw - 60, 40, {0.04f, 0.07f, 0.12f, 0.95f});
            renderer_.DrawRectOutline(mx0 + 30, my0 + 86, mw - 60, 40, activeInputField_ == 0 ? 2.0f : 1.0f,
                                      activeInputField_ == 0 ? Color4{0.4f, 0.82f, 1.0f, 1} : Color4{1, 1, 1, 0.25f});
            renderer_.DrawText(assets_.fontRegular, inputName_ + (activeInputField_ == 0 ? "_" : ""), mx0 + 40, my0 + 95, 0.85f, {1, 1, 1, 1});

            renderer_.DrawText(assets_.fontRegular, "Seed (optional, blank = random):", mx0 + 30, my0 + 134, 0.75f, {0.8f, 0.9f, 1.0f, 1});
            renderer_.DrawRect(mx0 + 30, my0 + 158, mw - 60, 40, {0.04f, 0.07f, 0.12f, 0.95f});
            renderer_.DrawRectOutline(mx0 + 30, my0 + 158, mw - 60, 40, activeInputField_ == 1 ? 2.0f : 1.0f,
                                      activeInputField_ == 1 ? Color4{0.4f, 0.82f, 1.0f, 1} : Color4{1, 1, 1, 0.25f});
            renderer_.DrawText(assets_.fontRegular, inputSeed_ + (activeInputField_ == 1 ? "_" : ""), mx0 + 40, my0 + 167, 0.85f, {1, 1, 1, 1});

            bool isNorm = (createWorldType_ == WorldType::Normal);
            renderer_.DrawRect(mx0 + 30, my0 + 216, 190, 38, isNorm ? Color4{0.18f, 0.56f, 0.82f, 0.95f} : Color4{0.16f, 0.20f, 0.28f, 0.85f});
            renderer_.DrawTextCentered(assets_.fontBold, "Normal", mx0 + 125, my0 + 225, 0.72f, {1, 1, 1, 1});
            renderer_.DrawRect(mx0 + mw - 220, my0 + 216, 190, 38, !isNorm ? Color4{0.18f, 0.56f, 0.82f, 0.95f} : Color4{0.16f, 0.20f, 0.28f, 0.85f});
            renderer_.DrawTextCentered(assets_.fontBold, "Flat", mx0 + mw - 125, my0 + 225, 0.72f, {1, 1, 1, 1});

            renderer_.DrawRect(mx0 + 30, my0 + mh - 62, 180, 42, {0.35f, 0.38f, 0.44f, 0.9f});
            renderer_.DrawTextCentered(assets_.fontBold, "CANCEL", mx0 + 120, my0 + mh - 52, 0.72f, {1, 1, 1, 1});
            renderer_.DrawRect(mx0 + mw - 210, my0 + mh - 62, 180, 42, {0.10f, 0.56f, 0.18f, 0.95f});
            renderer_.DrawTextCentered(assets_.fontBold, "CREATE", mx0 + mw - 120, my0 + mh - 52, 0.72f, {1, 1, 1, 1});
        } else if (menuModal_ == MenuModal::WorldSettings) {
            float mw = 440.0f, mh = 250.0f;
            float mx0 = (winW_ - mw) * 0.5f, my0 = (winH_ - mh) * 0.5f;
            renderer_.DrawRect(mx0, my0, mw, mh, {0.09f, 0.13f, 0.20f, 0.96f});
            renderer_.DrawRectOutline(mx0, my0, mw, mh, 2.0f, {0.5f, 0.82f, 1.0f, 0.7f});
            renderer_.DrawTextCentered(assets_.fontBold, "iLAND SETTINGS", mx0 + mw * 0.5f, my0 + 20, 0.82f, {1, 1, 1, 1});
            renderer_.DrawText(assets_.fontRegular, "Name:", mx0 + 30, my0 + 68, 0.76f, {0.8f, 0.9f, 1.0f, 1});
            renderer_.DrawRect(mx0 + 30, my0 + 94, mw - 60, 42, {0.04f, 0.07f, 0.12f, 0.95f});
            renderer_.DrawRectOutline(mx0 + 30, my0 + 94, mw - 60, 42, 2.0f, {0.4f, 0.82f, 1.0f, 1});
            renderer_.DrawText(assets_.fontRegular, inputName_ + "_", mx0 + 40, my0 + 104, 0.85f, {1, 1, 1, 1});

            renderer_.DrawRect(mx0 + 30, my0 + mh - 62, 118, 42, {0.35f, 0.38f, 0.44f, 0.9f});
            renderer_.DrawTextCentered(assets_.fontBold, "CANCEL", mx0 + 89, my0 + mh - 52, 0.68f, {1, 1, 1, 1});

            bool canDel = worlds.size() > 1;
            renderer_.DrawRect(mx0 + 161, my0 + mh - 62, 118, 42, canDel ? Color4{0.74f, 0.20f, 0.20f, 0.94f} : Color4{0.25f, 0.26f, 0.28f, 0.6f});
            renderer_.DrawTextCentered(assets_.fontBold, "DELETE", mx0 + 220, my0 + mh - 52, 0.68f, {1, 1, 1, canDel ? 1.0f : 0.45f});

            renderer_.DrawRect(mx0 + mw - 148, my0 + mh - 62, 118, 42, {0.10f, 0.56f, 0.18f, 0.95f});
            renderer_.DrawTextCentered(assets_.fontBold, "SAVE", mx0 + mw - 89, my0 + mh - 52, 0.68f, {1, 1, 1, 1});
        } else if (menuModal_ == MenuModal::DeleteConfirm) {
            float mw = 440.0f, mh = 200.0f;
            float mx0 = (winW_ - mw) * 0.5f, my0 = (winH_ - mh) * 0.5f;
            renderer_.DrawRect(mx0, my0, mw, mh, {0.12f, 0.09f, 0.12f, 0.96f});
            renderer_.DrawRectOutline(mx0, my0, mw, mh, 2.0f, {0.9f, 0.35f, 0.35f, 0.8f});
            renderer_.DrawTextCentered(assets_.fontBold, "DELETE iLAND?", mx0 + mw * 0.5f, my0 + 26, 0.82f, {1, 0.85f, 0.85f, 1});
            renderer_.DrawTextCentered(assets_.fontRegular, "This cannot be undone.", mx0 + mw * 0.5f, my0 + 78, 0.82f, {0.9f, 0.9f, 0.9f, 0.9f});
            renderer_.DrawRect(mx0 + 30, my0 + mh - 62, 170, 42, {0.35f, 0.38f, 0.44f, 0.9f});
            renderer_.DrawTextCentered(assets_.fontBold, "CANCEL", mx0 + 115, my0 + mh - 52, 0.72f, {1, 1, 1, 1});
            renderer_.DrawRect(mx0 + mw - 200, my0 + mh - 62, 170, 42, {0.76f, 0.20f, 0.20f, 0.95f});
            renderer_.DrawTextCentered(assets_.fontBold, "DELETE", mx0 + mw - 115, my0 + mh - 52, 0.72f, {1, 1, 1, 1});
        }
    }

    renderer_.EndUI();
}

void Game::RenderMultiplayerMenu() {
    renderer_.BeginFrame(winW_, winH_, {0.10f, 0.28f, 0.42f, 1.0f});
    renderer_.BeginUI(winW_, winH_);
    RenderBackground();

    float barX = 24.0f, barY = 18.0f, barW = winW_ - 48.0f, barH = 62.0f;
    renderer_.DrawRect(barX, barY, barW, barH, {0.06f, 0.11f, 0.18f, 0.84f});
    renderer_.DrawRectOutline(barX, barY, barW, barH, 1.5f, {0.55f, 0.82f, 1.0f, 0.55f});

    renderer_.DrawText(assets_.fontBold, "Your Name:", barX + 16.0f, barY + 20.0f, 0.70f, {0.92f, 0.96f, 1.0f, 1.0f});
    float nameBoxX = barX + 140.0f, nameBoxW = 210.0f;
    renderer_.DrawRect(nameBoxX, barY + 12.0f, nameBoxW, 38.0f, {0.04f, 0.07f, 0.12f, 0.94f});
    renderer_.DrawRectOutline(nameBoxX, barY + 12.0f, nameBoxW, 38.0f, mpActiveField_ == 0 ? 2.0f : 1.0f,
                              mpActiveField_ == 0 ? Color4{0.35f, 0.82f, 1.0f, 1} : Color4{1, 1, 1, 0.28f});
    renderer_.DrawText(assets_.fontRegular, playerName_ + (mpActiveField_ == 0 ? "_" : ""), nameBoxX + 10.0f, barY + 20.0f, 0.82f, {1, 1, 1, 1});

    float ipLabelX = barX + barW - 468.0f;
    if (ipLabelX > nameBoxX + nameBoxW + 16.0f) {
        renderer_.DrawText(assets_.fontBold, "IP LAN:", ipLabelX + 26.0f, barY + 20.0f, 0.70f, {0.92f, 0.96f, 1.0f, 1.0f});
    }
    float ipBoxX = barX + barW - 360.0f, ipBoxW = 220.0f;
    renderer_.DrawRect(ipBoxX, barY + 12.0f, ipBoxW, 38.0f, {0.04f, 0.07f, 0.12f, 0.94f});
    renderer_.DrawRectOutline(ipBoxX, barY + 12.0f, ipBoxW, 38.0f, mpActiveField_ == 1 ? 2.0f : 1.0f,
                              mpActiveField_ == 1 ? Color4{0.35f, 0.82f, 1.0f, 1} : Color4{1, 1, 1, 0.28f});
    renderer_.DrawText(assets_.fontRegular, directIpInput_ + (mpActiveField_ == 1 ? "_" : ""), ipBoxX + 10.0f, barY + 20.0f, 0.82f, {1, 1, 1, 1});

    float connBtnX = barX + barW - 130.0f, connBtnW = 116.0f;
    bool connHov = HitRect(static_cast<float>(mouseX_), static_cast<float>(mouseY_), connBtnX, barY + 12.0f, connBtnW, 38.0f);
    renderer_.DrawGradientRect(connBtnX, barY + 12.0f, connBtnW, 38.0f,
                               connHov ? Color4{0.32f, 0.68f, 0.94f, 0.96f} : Color4{0.22f, 0.56f, 0.84f, 0.94f},
                               connHov ? Color4{0.20f, 0.54f, 0.82f, 0.96f} : Color4{0.14f, 0.44f, 0.72f, 0.94f});
    renderer_.DrawTextCentered(assets_.fontBold, "Connect", connBtnX + connBtnW * 0.5f, barY + 21.0f, 0.66f, {1, 1, 1, 1});

    const auto& games = network_.GetDiscoveredGames();
    float marginX = 24.0f, topY = 98.0f, gap = 14.0f, cardH = 112.0f;
    float cardW = (winW_ - marginX * 2.0f - gap) * 0.5f;

    if (games.empty()) {
        float fullW = winW_ - marginX * 2.0f;
        renderer_.DrawRect(marginX + 2, topY + 3, fullW, cardH, {0, 0, 0, 0.24f});
        renderer_.DrawGradientRect(marginX, topY, fullW, cardH,
                                   {0.88f, 0.90f, 0.92f, 0.84f},
                                   {0.64f, 0.68f, 0.72f, 0.86f});
        renderer_.DrawRectOutline(marginX, topY, fullW, cardH, 1.5f, {0.26f, 0.30f, 0.34f, 0.55f});
        renderer_.DrawTexture(assets_.texUser, marginX + 14.0f, topY + 14.0f, 84.0f, 84.0f);
        renderer_.DrawText(assets_.fontBold, "Searching for LAN games on your Wi-Fi...", marginX + 118.0f, topY + 26.0f, 0.82f, {0.10f, 0.10f, 0.10f, 0.95f}, false);
        renderer_.DrawText(assets_.fontRegular, "On the other PC open a world, press ESC -> 'LAN' (or connect directly by IP above).",
                           marginX + 118.0f, topY + 62.0f, 0.72f, {0.18f, 0.22f, 0.26f, 0.95f}, false);
    } else {
        for (int i = 0; i < static_cast<int>(games.size()); ++i) {
            const auto& g = games[i];
            int col = i % 2;
            int row = i / 2;
            float cx = marginX + col * (cardW + gap);
            float cy = topY + row * (cardH + gap);
            bool isSel = (i == selectedLanIdx_);
            renderer_.DrawRect(cx + 2, cy + 3, cardW, cardH, {0, 0, 0, 0.24f});
            if (isSel) {
                renderer_.DrawGradientRect(cx, cy, cardW, cardH,
                                           {0.78f, 0.62f, 0.46f, 0.88f},
                                           {0.44f, 0.30f, 0.18f, 0.90f});
                renderer_.DrawRectOutline(cx, cy, cardW, cardH, 2.0f, {0.22f, 0.13f, 0.06f, 0.78f});
            } else {
                renderer_.DrawGradientRect(cx, cy, cardW, cardH,
                                           {0.88f, 0.90f, 0.92f, 0.80f},
                                           {0.62f, 0.65f, 0.68f, 0.82f});
                renderer_.DrawRectOutline(cx, cy, cardW, cardH, 1.5f, {0.26f, 0.30f, 0.34f, 0.52f});
            }
            renderer_.DrawTexture(assets_.texWorldSquare, cx + 14.0f, cy + 14.0f, 84.0f, 84.0f);
            renderer_.DrawText(assets_.fontBold, g.worldTitle, cx + 114.0f, cy + 20.0f, 0.82f, {0.10f, 0.10f, 0.10f, 0.96f}, false);
            std::string sub = "Host: " + g.hostName + "  |  " + g.ip + ":" + std::to_string(g.port) +
                              "  |  " + std::to_string(g.playerCount) + " players";
            renderer_.DrawText(assets_.fontRegular, sub, cx + 114.0f, cy + 60.0f, 0.68f,
                               isSel ? Color4{0.98f, 0.96f, 0.92f, 0.95f} : Color4{0.16f, 0.20f, 0.24f, 0.92f}, isSel);
        }
    }

    if (!mpStatusMsg_.empty()) {
        float sy = winH_ - 146.0f;
        renderer_.DrawRect(24.0f, sy, winW_ - 48.0f, 36.0f, {0.06f, 0.10f, 0.16f, 0.84f});
        renderer_.DrawTextCentered(assets_.fontRegular, mpStatusMsg_, winW_ * 0.5f, sy + 8.0f, 0.80f, {1.0f, 0.92f, 0.45f, 1.0f});
    }

    {
        float bx = 24.0f, by = winH_ - 96.0f, bw = 175.0f, bh = 72.0f;
        bool hov = HitRect(static_cast<float>(mouseX_), static_cast<float>(mouseY_), bx, by, bw, bh);
        renderer_.DrawRect(bx + 2, by + 3, bw, bh, {0, 0, 0, 0.28f});
        renderer_.DrawGradientRect(bx, by, bw, bh,
                                   hov ? Color4{0.96f, 0.97f, 0.98f, 0.96f} : Color4{0.90f, 0.92f, 0.94f, 0.92f},
                                   hov ? Color4{0.78f, 0.80f, 0.82f, 0.96f} : Color4{0.70f, 0.72f, 0.75f, 0.92f});
        renderer_.DrawRectOutline(bx, by, bw, bh, 1.5f, {0.45f, 0.48f, 0.52f, 0.65f});
        renderer_.DrawTextCentered(assets_.fontBold, "Back", bx + bw * 0.5f,
                                   by + (bh - assets_.fontBold.lineHeight * 0.92f) * 0.5f,
                                   0.92f, {0.10f, 0.10f, 0.10f, 0.95f}, false);
    }

    {
        float bx = winW_ - 210.0f, by = winH_ - 96.0f, bw = 186.0f, bh = 72.0f;
        bool hov = HitRect(static_cast<float>(mouseX_), static_cast<float>(mouseY_), bx, by, bw, bh);
        renderer_.DrawRect(bx + 2, by + 3, bw, bh, {0, 0, 0, 0.30f});
        renderer_.DrawGradientRect(bx, by, bw, bh,
                                   hov ? Color4{0.08f, 0.52f, 0.12f, 0.96f} : Color4{0.04f, 0.42f, 0.08f, 0.94f},
                                   hov ? Color4{0.04f, 0.36f, 0.07f, 0.96f} : Color4{0.02f, 0.28f, 0.04f, 0.96f});
        renderer_.DrawRectOutline(bx, by, bw, bh, 1.5f, {1.0f, 1.0f, 1.0f, 0.22f});
        renderer_.DrawTextCentered(assets_.fontBold, "Play", bx + bw * 0.5f,
                                   by + (bh - assets_.fontBold.lineHeight * 0.96f) * 0.5f,
                                   0.96f, {0.96f, 0.98f, 0.96f, 1.0f}, true);
    }

    renderer_.EndUI();
}

void Game::RenderPlaying() {
    if (!world_) return;

    Vec3 eye = player_.pos + Vec3(0.0f, player_.eyeHeight, 0.0f);
    Vec3 look(
        std::sin(player_.yaw) * std::cos(player_.pitch),
        std::sin(player_.pitch),
        -std::cos(player_.yaw) * std::cos(player_.pitch)
    );

    float subSky = static_cast<float>(world_->GetBlockLightLevel(
        static_cast<int>(std::floor(eye.x)),
        static_cast<int>(std::floor(eye.z)),
        static_cast<int>(std::floor(eye.y)),
        BlockLightType::Sky));
    SkyState sky = Renderer::ComputeSkyState(hourOfDay_, player_.submerged, subSky);

    renderer_.BeginFrame(winW_, winH_, sky.fogColor);

    float aspect = static_cast<float>(winW_) / static_cast<float>(std::max(1, winH_));
    Mat4 proj = Mat4::Perspective(65.0f * PI_F / 180.0f, aspect, 0.1f, 700.0f);
    Mat4 view = Mat4::LookAt(eye, eye + look, {0.0f, 1.0f, 0.0f});
    Mat4 vp = proj * view;

    renderer_.RenderSky(view, proj, eye, sky, totalTime_);
    renderer_.RenderWorld(*world_, view, proj, eye, sky, player_.submerged, totalTime_);
    renderer_.RenderRemotePlayers(view, proj, eye, sky, network_.remotePlayers());

    if (aimedHit_.hit && !inventoryOpen_ && !pauseOpen_ && !chatOpen_) {
        BlockType bt = world_->GetBlockType(aimedHit_.blockPos.x, aimedHit_.blockPos.y, aimedHit_.blockPos.z);
        renderer_.RenderBlockOutline(vp, aimedHit_.blockPos, Block::GetBlockHeight(bt));
    }

    renderer_.RenderParticles(view, proj, particles_);

    renderer_.BeginUI(winW_, winH_);
    if (player_.submerged) {
        renderer_.DrawRect(0, 0, static_cast<float>(winW_), static_cast<float>(winH_), {0.08f, 0.28f, 0.45f, 0.34f});
    }

    RenderRemoteNametags(vp);

    if (chatOpen_) {
        RenderOpenChatWindow();
    } else {
        RenderHUD();
        RenderClosedChatHUD();
    }

    if (inventoryOpen_) RenderInventoryOverlay();
    if (pauseOpen_) RenderPauseOverlay();
    renderer_.EndUI();
}

void Game::RenderRemoteNametags(const Mat4& vp) {
    for (const auto& kv : network_.remotePlayers()) {
        const RemotePlayer& rp = kv.second;
        float sx = 0, sy = 0, dist = 0;
        if (Renderer::ProjectToScreen(rp.renderPos + Vec3(0.0f, 2.08f, 0.0f), vp, winW_, winH_, sx, sy, dist)) {
            if (dist < 140.0f) {
                float scale = std::clamp(8.5f / (dist + 6.5f), 0.55f, 1.05f);
                renderer_.DrawTextCentered(assets_.fontBold, rp.name, sx, sy - assets_.fontBold.lineHeight * scale * 0.5f,
                                           scale, {1.0f, 1.0f, 1.0f, 0.96f}, true);
            }
        }
    }
}

void Game::RenderClosedChatHUD() {
    if (pauseOpen_ || inventoryOpen_) return;
    std::vector<const ChatMessage*> recent;
    for (const auto& m : chatHistory_) {
        if ((totalTime_ - m.timeReceived) <= 10.0f) {
            recent.push_back(&m);
        }
    }
    if (recent.empty()) return;

    float scale = 0.78f;
    float lineH = assets_.fontBold.lineHeight * scale + 3.0f;
    int maxShow = std::min(6, static_cast<int>(recent.size()));
    float baseY = winH_ - 114.0f - lineH;

    for (int i = 0; i < maxShow; ++i) {
        const ChatMessage* m = recent[recent.size() - 1 - i];
        float y = baseY - i * lineH;
        float x = 14.0f;
        if (m->sender.empty()) {
            renderer_.DrawText(assets_.fontBold, m->text, x, y, scale, {0.0f, 1.0f, 0.957f, 1.0f}, true);
        } else {
            std::string prefix = m->sender + ": ";
            renderer_.DrawText(assets_.fontBold, prefix, x, y, scale, {1.0f, 0.859f, 0.0f, 1.0f}, true);
            float pw = renderer_.MeasureText(assets_.fontBold, prefix, scale);
            renderer_.DrawText(assets_.fontBold, m->text, x + pw, y, scale, {1.0f, 1.0f, 1.0f, 1.0f}, true);
        }
    }
}

void Game::RenderOpenChatWindow() {
    float boxX = 12.0f;
    float boxY = winH_ * 0.25f;
    float boxW = winW_ - 42.0f;
    float boxH = winH_ * 0.62f;

    renderer_.DrawRect(boxX, boxY, boxW, boxH, {0.16f, 0.16f, 0.16f, 0.64f});
    renderer_.DrawRectOutline(boxX, boxY, boxW, boxH, 1.5f, {0.82f, 0.82f, 0.82f, 0.42f});

    float sbX = boxX + boxW + 8.0f;
    float sbW = 14.0f;
    renderer_.DrawRect(sbX, boxY, sbW, boxH, {0.04f, 0.04f, 0.04f, 0.85f});
    renderer_.DrawRectOutline(sbX, boxY, sbW, boxH, 1.0f, {0.35f, 0.35f, 0.35f, 0.55f});

    float scale = 0.82f;
    float lineH = assets_.fontBold.lineHeight * scale + 5.0f;
    int maxLines = std::max(1, static_cast<int>((boxH - 20.0f) / lineH));
    int total = static_cast<int>(chatHistory_.size());
    int endIdx = std::max(0, total - chatScrollOffset_);
    int startIdx = std::max(0, endIdx - maxLines);

    float bottomLineY = boxY + boxH - 12.0f - lineH;
    int rowFromBottom = 0;
    for (int idx = endIdx - 1; idx >= startIdx; --idx) {
        const auto& m = chatHistory_[idx];
        float y = bottomLineY - rowFromBottom * lineH;
        float x = boxX + 14.0f;
        if (m.sender.empty()) {
            renderer_.DrawText(assets_.fontBold, m.text, x, y, scale, {0.0f, 1.0f, 0.957f, 1.0f}, true);
        } else {
            std::string prefix = m.sender + ": ";
            renderer_.DrawText(assets_.fontBold, prefix, x, y, scale, {1.0f, 0.859f, 0.0f, 1.0f}, true);
            float pw = renderer_.MeasureText(assets_.fontBold, prefix, scale);
            renderer_.DrawText(assets_.fontBold, m.text, x + pw, y, scale, {1.0f, 1.0f, 1.0f, 1.0f}, true);
        }
        rowFromBottom++;
    }

    if (total > maxLines) {
        float thumbH = std::max(28.0f, boxH * (static_cast<float>(maxLines) / total));
        int maxScroll = total - maxLines;
        float frac = 1.0f - (static_cast<float>(chatScrollOffset_) / std::max(1, maxScroll));
        float thumbY = boxY + (boxH - thumbH) * frac;
        renderer_.DrawRect(sbX + 2, thumbY, sbW - 4, thumbH, {0.55f, 0.55f, 0.55f, 0.85f});
    }

    float inY = boxY + boxH + 10.0f;
    float inH = 44.0f;
    float sendW = 156.0f;
    float inW = winW_ - 24.0f - sendW - 10.0f;

    renderer_.DrawRect(boxX, inY, inW, inH, {0.16f, 0.16f, 0.16f, 0.68f});
    renderer_.DrawRectOutline(boxX, inY, inW, inH, 1.5f, {0.82f, 0.82f, 0.82f, 0.45f});

    bool blink = (std::fmod(chatCursorTimer_, 1.0f) < 0.55f);
    std::string shown = chatInput_ + (blink ? "|" : "");
    renderer_.DrawText(assets_.fontRegular, shown, boxX + 12.0f,
                       inY + (inH - assets_.fontRegular.lineHeight * 0.92f) * 0.5f,
                       0.92f, {0.96f, 0.96f, 0.96f, 1.0f}, true);

    float sendX = boxX + inW + 10.0f;
    bool sendHov = HitRect(static_cast<float>(mouseX_), static_cast<float>(mouseY_), sendX, inY, sendW, inH);
    renderer_.DrawRect(sendX, inY, sendW, inH, sendHov ? Color4{0.26f, 0.26f, 0.26f, 0.80f} : Color4{0.18f, 0.18f, 0.18f, 0.72f});
    renderer_.DrawRectOutline(sendX, inY, sendW, inH, 1.5f, {0.82f, 0.82f, 0.82f, 0.45f});
    renderer_.DrawTextCentered(assets_.fontBold, "Send", sendX + sendW * 0.5f,
                               inY + (inH - assets_.fontBold.lineHeight * 0.78f) * 0.5f,
                               0.78f, {0.86f, 0.86f, 0.86f, 0.96f}, true);
}

void Game::RenderHUD() {
    if (!inventoryOpen_ && !pauseOpen_) {
        float cx = winW_ * 0.5f, cy = winH_ * 0.5f;
        renderer_.DrawRect(cx - 9, cy - 1.5f, 18, 3, {0, 0, 0, 0.55f});
        renderer_.DrawRect(cx - 1.5f, cy - 9, 3, 18, {0, 0, 0, 0.55f});
        renderer_.DrawRect(cx - 8, cy - 1, 16, 2, {1, 1, 1, 0.92f});
        renderer_.DrawRect(cx - 1, cy - 8, 2, 16, {1, 1, 1, 0.92f});
    }

    constexpr int totalSlots = 7;
    float slotSize = 58.0f;
    float gap = 8.0f;
    float barW = totalSlots * slotSize + (totalSlots + 1) * gap;
    float barH = slotSize + gap * 2.0f;
    float barX = (winW_ - barW) * 0.5f;
    float barY = winH_ - barH - 14.0f;

    renderer_.DrawRect(barX, barY, barW, barH, {0.05f, 0.08f, 0.13f, 0.72f});
    renderer_.DrawRectOutline(barX, barY, barW, barH, 1.5f, {1, 1, 1, 0.25f});

    for (int i = 0; i < 5; ++i) {
        float sx = barX + gap + i * (slotSize + gap);
        float sy = barY + gap;
        renderer_.DrawRect(sx, sy, slotSize, slotSize, {0.14f, 0.18f, 0.24f, 0.75f});
        renderer_.DrawBlockIcon3D(buildItems_[i], sx + slotSize * 0.5f, sy + slotSize * 0.5f, slotSize * 0.88f, winW_, winH_);
        if (i == activeSlot_) {
            renderer_.DrawTexture(assets_.texItemSelector, sx - 3, sy - 3, slotSize + 6, slotSize + 6, {1, 1, 1, 0.95f});
            renderer_.DrawRectOutline(sx - 2, sy - 2, slotSize + 4, slotSize + 4, 2.0f, {1.0f, 0.92f, 0.35f, 0.95f});
        }
        renderer_.DrawText(assets_.fontRegular, std::to_string(i + 1), sx + 5, sy + 2, 0.62f, {1, 1, 1, 0.85f});
    }

    {
        float sx = barX + gap + 5 * (slotSize + gap);
        float sy = barY + gap;
        renderer_.DrawRect(sx, sy, slotSize, slotSize, inventoryOpen_ ? Color4{0.22f, 0.54f, 0.80f, 0.88f} : Color4{0.14f, 0.18f, 0.24f, 0.75f});
        renderer_.DrawTexture(assets_.texInventory, sx + 6, sy + 6, slotSize - 12, slotSize - 12);
        renderer_.DrawText(assets_.fontRegular, "E", sx + 5, sy + 2, 0.62f, {1, 1, 1, 0.85f});
    }

    {
        float sx = barX + gap + 6 * (slotSize + gap);
        float sy = barY + gap;
        renderer_.DrawRect(sx, sy, slotSize, slotSize, {0.14f, 0.18f, 0.24f, 0.75f});
        renderer_.DrawTexture(assets_.texDestroy, sx + 6, sy + 6, slotSize - 12, slotSize - 12);
        renderer_.DrawText(assets_.fontRegular, "LMB", sx + 4, sy + slotSize - 16, 0.52f, {1, 1, 1, 0.75f});
    }

    std::string label = GetBlockName(buildItems_[activeSlot_]);
    if (network_.isHosting()) label += "  [LAN HOST]";
    else if (network_.isClient()) label += "  [LAN]";
    renderer_.DrawTextCentered(assets_.fontBold, label, winW_ * 0.5f, barY - 26.0f, 0.68f, {1, 1, 1, 0.95f});

    if (showDebug_) {
        std::ostringstream oss;
        oss << "FPS: " << static_cast<int>(fpsSmooth_ + 0.5f)
            << " | Pos: " << std::fixed << std::setprecision(1) << player_.pos.x << ", " << player_.pos.y << ", " << player_.pos.z
            << " | Hour: " << std::setprecision(1) << hourOfDay_
            << " | Players: " << network_.totalPlayerCount();
        renderer_.DrawRect(12, 72, 490, 28, {0.04f, 0.07f, 0.12f, 0.68f});
        renderer_.DrawText(assets_.fontRegular, oss.str(), 20, 77, 0.64f, {0.75f, 1.0f, 0.80f, 1.0f});
    }
}

void Game::RenderInventoryOverlay() {
    renderer_.DrawRect(0, 0, static_cast<float>(winW_), static_cast<float>(winH_), {0, 0, 0, 0.62f});

    constexpr int cols = 10;
    float cell = 64.0f;
    float gap = 8.0f;
    int rows = (static_cast<int>(inventory_.size()) + cols - 1) / cols;
    float gridW = cols * cell + (cols - 1) * gap;
    float gridH = rows * cell + (rows - 1) * gap;
    float gx = (winW_ - gridW) * 0.5f;
    float gy = (winH_ - gridH) * 0.5f + 18.0f;

    renderer_.DrawRect(gx - 22, gy - 62, gridW + 44, gridH + 102, {0.07f, 0.11f, 0.18f, 0.94f});
    renderer_.DrawRectOutline(gx - 22, gy - 62, gridW + 44, gridH + 102, 2.0f, {0.45f, 0.78f, 1.0f, 0.65f});
    renderer_.DrawText(assets_.fontBold, "BLOCK INVENTORY (64 BLOCKS)", gx, gy - 48, 0.80f, {1, 1, 1, 1});
    renderer_.DrawTexture(assets_.texCloseIcon, gx + gridW - 22, gy - 54, 34, 34);

    BlockType hoveredBlock = BlockType::Null;
    for (int i = 0; i < static_cast<int>(inventory_.size()); ++i) {
        int c = i % cols;
        int r = i / cols;
        float cx = gx + c * (cell + gap);
        float cy = gy + r * (cell + gap);
        bool hov = HitRect(static_cast<float>(mouseX_), static_cast<float>(mouseY_), cx, cy, cell, cell);
        if (hov) hoveredBlock = inventory_[i];

        renderer_.DrawRect(cx, cy, cell, cell, hov ? Color4{0.24f, 0.52f, 0.80f, 0.90f} : Color4{0.12f, 0.17f, 0.25f, 0.86f});
        renderer_.DrawRectOutline(cx, cy, cell, cell, hov ? 2.0f : 1.0f, hov ? Color4{1, 0.95f, 0.4f, 1} : Color4{1, 1, 1, 0.16f});
        renderer_.DrawBlockIcon3D(inventory_[i], cx + cell * 0.5f, cy + cell * 0.5f, cell * 0.86f, winW_, winH_);
    }

    std::string tip = (hoveredBlock != BlockType::Null)
        ? std::string("Click to equip: ") + GetBlockName(hoveredBlock)
        : "Click any block to equip to hotbar (E / ESC to close)";
    renderer_.DrawTextCentered(assets_.fontRegular, tip, winW_ * 0.5f, gy + gridH + 10.0f, 0.78f, {0.92f, 0.96f, 1.0f, 0.95f});
}

void Game::RenderPauseOverlay() {
    renderer_.DrawRect(0, 0, static_cast<float>(winW_), static_cast<float>(winH_), {0, 0, 0, 0.58f});
    float mw = 390.0f, mh = 290.0f;
    float mx0 = (winW_ - mw) * 0.5f, my0 = (winH_ - mh) * 0.5f;
    float bx = mx0 + 30.0f, bw = mw - 60.0f;

    renderer_.DrawRect(mx0, my0, mw, mh, {0.07f, 0.11f, 0.18f, 0.94f});
    renderer_.DrawRectOutline(mx0, my0, mw, mh, 2.0f, {0.45f, 0.78f, 1.0f, 0.65f});
    renderer_.DrawTextCentered(assets_.fontBold, activeWorldMeta_.title, mx0 + mw * 0.5f, my0 + 18, 0.85f, {1, 1, 1, 1});

    auto btn = [&](float x, float y, float w, float h, const std::string& label, const Color4& col) {
        bool hov = HitRect(static_cast<float>(mouseX_), static_cast<float>(mouseY_), x, y, w, h);
        Color4 c = hov ? Color4{std::min(1.0f, col.r + 0.12f), std::min(1.0f, col.g + 0.12f), std::min(1.0f, col.b + 0.12f), 0.96f} : col;
        renderer_.DrawRect(x, y, w, h, c);
        renderer_.DrawRectOutline(x, y, w, h, 1.5f, {1, 1, 1, 0.30f});
        renderer_.DrawTextCentered(assets_.fontBold, label, x + w * 0.5f, y + (h - 20) * 0.5f, 0.66f, {1, 1, 1, 1});
    };

    if (!isRemoteClientSession_) {
        if (network_.isHosting()) {
            btn(bx, my0 + 68, bw, 44, "LAN: ON (Disable)", {0.12f, 0.58f, 0.24f, 0.96f});
            std::string lanInfo = "Wi-Fi IP: " + network_.localIp() + ":" + std::to_string(network_.hostPort()) +
                                  "  |  Players: " + std::to_string(network_.totalPlayerCount());
            renderer_.DrawTextCentered(assets_.fontRegular, lanInfo, mx0 + mw * 0.5f, my0 + 118, 0.62f, {0.40f, 1.0f, 0.92f, 1.0f});
        } else {
            btn(bx, my0 + 68, bw, 44, "LAN", {0.18f, 0.48f, 0.80f, 0.94f});
        }
    } else {
        renderer_.DrawRect(bx, my0 + 68, bw, 44, {0.14f, 0.30f, 0.44f, 0.85f});
        renderer_.DrawTextCentered(assets_.fontBold, "LAN (" + std::to_string(network_.totalPlayerCount()) + " players)",
                                   mx0 + mw * 0.5f, my0 + 80, 0.66f, {0.40f, 1.0f, 0.95f, 1.0f});
    }

    btn(bx, my0 + 148, bw, 44, isRemoteClientSession_ ? "Disconnect & Main Menu" : "Save & Main Menu", {0.28f, 0.36f, 0.48f, 0.92f});
    btn(bx, my0 + 206, bw, 44, "Quit Game", {0.72f, 0.22f, 0.22f, 0.92f});
}

}
