#include "beatsaber-hook/shared/utils/hooking.hpp"
#include "beatsaber-hook/shared/utils/il2cpp-utils.hpp"

#include "custom-types/shared/register.hpp"

#include "TwitchIRC/TwitchIRCClient.hpp"

#include "bsml/shared/BSML.hpp"
#include "bsml/shared/BSML/MainThreadScheduler.hpp"

#include "UnityEngine/SceneManagement/Scene.hpp"
#include "UnityEngine/SceneManagement/SceneManager.hpp"

#include "CustomTypes/ChatHandler.hpp"
#include "ChatBuilder.hpp"
#include "logger.hpp"
#include "ModConfig.hpp"
#include "ModSettingsViewController.hpp"

#include <map>
#include <thread>
#include <iomanip>
#include <sstream>
#include <chrono>

static modloader::ModInfo modInfo{MOD_ID, VERSION, 0};

//TODO: Add to ModConfig
std::unordered_set<std::string> Blacklist;

std::map<std::string, std::string> usersColorCache;

bool threadRunning = false;

template <typename T>
inline std::string int_to_hex(T val, size_t width=sizeof(T)*2) {
    std::stringstream ss;
    ss << "#" << std::setfill('0') << std::setw(width) << std::hex << (val|0) << "ff";
    return ss.str();
}

void AddChatObject(std::string text) {
    ChatObject chatObject = {};
    chatObject.Text = text;
    chatObject.GameObject = nullptr;
    if(chatHandler)
        chatHandler->AddChatObject(chatObject);
}

void OnChatMessage(IRCMessage ircMessage, TwitchIRCClient* client) {
    std::string username = ircMessage.prefix.nick;
    std::string message = ircMessage.parameters.at(ircMessage.parameters.size() - 1);
    if (Blacklist.count(username)) {
        getLogger().info("Twitch Chat: Blacklisted user %s sent the message: %s", username.c_str(), message.c_str());
        return;
    } else {
        getLogger().info("Twitch Chat: User %s sent the message: %s", username.c_str(), message.c_str());
    }
    if (usersColorCache.find(username) == usersColorCache.end())
        usersColorCache.emplace(username, int_to_hex(rand() % 0x1000000, 6));
    std::string text = "<color=" + usersColorCache[username] + ">" + username +
                       "</color>: " + message;
    AddChatObject(text);
}

#define JOIN_RETRY_DELAY 3000
#define CONNECT_RETRY_DELAY 15000

void TwitchIRCThread() {
    if(threadRunning) 
        return;
    threadRunning = true;
    getLogger().info("Thread Started!");
    TwitchIRCClient client = TwitchIRCClient();
    std::string currentChannel = "";
    using namespace std::chrono;
    milliseconds lastJoinTry = 0ms;
    milliseconds lastConnectTry = 0ms;
    bool wasConnected = false;
    while(threadRunning) {
        auto currentTime = duration_cast<milliseconds>(high_resolution_clock::now().time_since_epoch());
        if(client.Connected()) {
            std::string targetChannel = getModConfig().Channel.GetValue();
            if(currentChannel != targetChannel) {
                if ((currentTime - lastJoinTry).count() > JOIN_RETRY_DELAY) {
                    lastJoinTry = currentTime; 
                    if(client.JoinChannel(targetChannel)) {
                        currentChannel = targetChannel;
                        getLogger().info("Twitch Chat: Joined Channel %s!", currentChannel.c_str());
                        AddChatObject("<color=#FFFFFFFF>Joined Channel:</color> <color=#FFB300FF>" + currentChannel + "</color>");
                    }
                }
            }
            client.ReceiveData();
        } else {
            if(wasConnected) {
                wasConnected = false;
                getLogger().info("Twitch Chat: Disconnected!");
                AddChatObject("<color=#FF0000FF>Disconnected!</color>");
            }
            if ((currentTime - lastConnectTry).count() > CONNECT_RETRY_DELAY) {
                getLogger().info("Twitch Chat: Connecting...");
                lastConnectTry = currentTime;
                if (client.InitSocket()) {
                    if (client.Connect()) {
                        if (client.Login("justinfan" + std::to_string(1030307 + rand() % 1030307), "xxx")) {
                            wasConnected = true;
                            AddChatObject("<color=#FFFFFFFF>Logged In!</color>");
                            getLogger().info("Twitch Chat: Logged In!");
                            client.HookIRCCommand("PRIVMSG", OnChatMessage);
                            currentChannel = "";
                        }
                    }
                }
            }
        }
        std::this_thread::yield();
    }
    if(wasConnected) {
        wasConnected = false;
        getLogger().info("Twitch Chat: Disconnected!");
        AddChatObject("<color=#FF0000FF>Disconnected!</color>");
    }
    threadRunning = false;
    client.Disconnect();
    getLogger().info("Thread Stopped!");
}

MAKE_HOOK_MATCH(SceneManager_Internal_ActiveSceneChanged,
                &UnityEngine::SceneManagement::SceneManager::Internal_ActiveSceneChanged,
                void, UnityEngine::SceneManagement::Scene prevScene, UnityEngine::SceneManagement::Scene nextScene) {
    SceneManager_Internal_ActiveSceneChanged(prevScene, nextScene);
    if(nextScene.IsValid()) {
        std::string sceneName = to_utf8(csstrtostr(nextScene.get_name()));
        if(sceneName.find("Menu") != std::string::npos) {
            BSML::MainThreadScheduler::Schedule(
                [] {
                    if(!chatHandler)
                        CreateChatGameObject();
                    if (!threadRunning)
                        std::thread (TwitchIRCThread).detach();
                }
            );
        }
    }
}

MOD_EXTERN_FUNC void setup(CModInfo *info) noexcept {
    ModInfo.id = "ChatUI";
    ModInfo.version = VERSION;
    *info = modInfo.to_c();

    Blacklist.insert("dootybot");
    Blacklist.insert("nightbot");

    getConfig().Load();
}

extern "C" void late_load() {

    il2cpp_functions::Init();

    BSML::Init();

    custom_types::Register::AutoRegister();

    BSML::Register::RegisterMainMenuViewController(ChatUI, DidActivate);

    PaperLogger.info("Starting ChatUI installation...");

    INSTALL_HOOK(getLogger(), SceneManager_Internal_ActiveSceneChanged);
    
    PaperLogger.info("Successfully installed ChatUI!");
}