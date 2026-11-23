#pragma once
#include "types.hpp"
#include "world.hpp"
#include "assets.hpp"
#include "renderer.hpp"
#include "network.hpp"
#include <SDL2/SDL.h>
#include <memory>
#include <vector>
#include <array>
#include <string>

namespace ilands {

enum class AppState {
    Splash,
    MainMenu,
    SoloSelect,
    MultiplayerMenu,
    Playing
};

enum class MenuModal {
    None,
    CreateWorld,
    WorldSettings,
    DeleteConfirm
};

struct ChatMessage {
    std::string sender;
    std::string text;
    float timeReceived = 0.0f;
};

struct PlayerState {
    Vec3 pos{128.0f, 50.0f, 128.0f};
    Vec3 vel{0.0f, 0.0f, 0.0f};
    float yaw = 0.0f;
    float pitch = -0.15f;
    bool grounded = false;
    bool inWater = false;
    bool submerged = false;
    float radius = 0.32f;
    float height = 1.75f;
    float eyeHeight = 1.52f;
};

class Game {
public:
    Game() = default;
    ~Game();

    bool Init(const std::string& exeDir);
    int Run();

    bool RunSmokeTest(const std::string& exeDir, const std::string& screenshotDir);

private:
    void InitInventoryList();
    void LoadConfig();
    void SaveConfig() const;

    void LoadWorldSession(const SavedWorldMeta& meta, bool isRemoteClient = false,
                          const std::vector<WorldUpdateEntry>* remoteUpdates = nullptr);
    void UnloadWorldSession();

    void ProcessEvent(const SDL_Event& ev);
    void ProcessMainMenuEvent(const SDL_Event& ev);
    void ProcessSoloSelectEvent(const SDL_Event& ev);
    void ProcessMultiplayerEvent(const SDL_Event& ev);
    void ProcessPlayingEvent(const SDL_Event& ev);

    void Update(float dt);
    void UpdatePlaying(float dt);
    void UpdateNetworking(float dt);
    void UpdatePlayerPhysics(float dt);
    void MoveAxis(Vec3& pos, float dx, float dy, float dz, bool& hitAxis);
    bool CollidesAt(const Vec3& pos) const;
    void CheckBoundsRescue();
    void SpawnBreakParticles(BlockType type, const IntVector3& pos, const SkyState& sky);
    void UpdateParticles(float dt);

    void AddChatMessage(const std::string& sender, const std::string& text);
    void SubmitChatInput();
    void OpenChat();
    void CloseChat();

    void Render();
    void RenderBackground();
    void RenderSplash();
    void RenderMainMenu();
    void RenderSoloSelect();
    void RenderMultiplayerMenu();
    void RenderPlaying();
    void RenderRemoteNametags(const Mat4& viewProj);
    void RenderHUD();
    void RenderClosedChatHUD();
    void RenderOpenChatWindow();
    void RenderInventoryOverlay();
    void RenderPauseOverlay();

    void SetMouseCaptured(bool captured);
    void SelectInventoryBlock(BlockType type);
    void PerformBreak();
    void PerformPlace();
    void PerformPickBlock();
    void AttemptJoinLan(const std::string& ipText, uint16_t defaultPort);

    bool HitRect(float mx, float my, float x, float y, float w, float h) const {
        return mx >= x && mx <= x + w && my >= y && my <= y + h;
    }

    SDL_Window* window_ = nullptr;
    SDL_GLContext glCtx_ = nullptr;
    int winW_ = 1280;
    int winH_ = 720;
    bool running_ = false;
    bool mouseCaptured_ = false;

    std::string exeDir_;
    Assets assets_;
    Renderer renderer_;
    LanNetwork network_;
    std::unique_ptr<WorldStorage> storage_;
    std::unique_ptr<World> world_;
    SavedWorldMeta activeWorldMeta_;
    bool isRemoteClientSession_ = false;

    AppState state_ = AppState::Splash;
    float splashTimer_ = 0.0f;
    float totalTime_ = 0.0f;

    std::string playerName_ = "Player";
    int selectedWorldIdx_ = 0;
    MenuModal menuModal_ = MenuModal::None;
    std::string inputName_;
    std::string inputSeed_;
    int activeInputField_ = 0;
    WorldType createWorldType_ = WorldType::Normal;

    int selectedLanIdx_ = 0;
    std::string directIpInput_ = "127.0.0.1";
    int mpActiveField_ = -1;
    std::string mpStatusMsg_;
    bool mpWaitingWelcome_ = false;
    float mpConnectTimer_ = 0.0f;

    int mouseX_ = 0;
    int mouseY_ = 0;

    PlayerState player_;
    float hourOfDay_ = 12.0f;
    bool timePaused_ = false;
    float saveHourAccum_ = 0.0f;
    bool inventoryOpen_ = false;
    bool pauseOpen_ = false;
    bool chatOpen_ = false;
    bool showDebug_ = false;

    std::vector<ChatMessage> chatHistory_;
    std::string chatInput_;
    int chatScrollOffset_ = 0;
    float chatCursorTimer_ = 0.0f;

    std::vector<BlockType> inventory_;
    std::array<BlockType, 5> buildItems_{};
    int activeSlot_ = 0;

    RaycastHit aimedHit_;
    float actionCooldown_ = 0.0f;
    bool lmbHeld_ = false;
    bool rmbHeld_ = false;
    float fpsSmooth_ = 60.0f;

    std::vector<BlockParticle> particles_;
};

}
