#include "client_platform.h"
#include "client_internal.h"

#include "menu_shim.h"
#include "ui_harness_plugin.h"

#include <cstdio>
namespace ClientInternal {
void OnRender(IDirect3DDevice9 *device) {
    if (disabled.load()) {
        return;
    }

    static const auto inputHeightOffset = 50.0f;
    static const auto inputWidthOffset = 50.0f;
    static const auto unfocusedChatMessages = 5;

    if (players.ShowNameTags) {
        auto window = ImGui::BeginRawScene("##client-backbuffer-nametags");
        if (window && window->DrawList) {
        players.Mutex.lock_shared();

        for (auto p : players.List) {
            auto *remote = PlayerRemoteActor(p);
            Classes::USkeletalMeshComponent *skel = nullptr;
            if (LevelsCompatible(p->Level, client.Level) && remote &&
                MeSdk::Safe::IsPlausibleUObject(remote) &&
                TryReadPlayerRemoteSkel(p, skel) && skel) {
                Classes::FVector pos = {};
                if (!MeSdk::Safe::Gameplay::TryReadActorLocation(remote, pos)) {
                    continue;
                }
                pos.Z = p->MaxZ + 27.5f;

                if (Engine::WorldToScreen(device, pos)) {
                    char label[160] = {};
                    if (showRemoteStanceOnNametag && p->HasRemoteMoveState) {
                        const char *stance = "Idle";
                        if (p->RemoteMovementState >= 2) {
                            stance = "Falling";
                        } else if (p->RemoteMovementState == 1) {
                            stance = "Walk";
                        } else if (p->RemoteMovementState == 0 &&
                                   p->RemotePhysics == 1) {
                            stance = "Stand";
                        }
                        _snprintf_s(label, sizeof(label), _TRUNCATE, "%s · %s",
                                    p->Name.c_str(), stance);
                    } else {
                        _snprintf_s(label, sizeof(label), _TRUNCATE, "%s",
                                    p->Name.c_str());
                    }
                    auto size = ImGui::CalcTextSize(label);
                    auto topLeft =
                        ImVec2(pos.X - size.x / 2.0f, pos.Y - size.y);

                    window->DrawList->AddRectFilled(
                        topLeft - ImVec2(3.0f, 1.0f),
                        ImVec2(pos.X + size.x / 2.0f, pos.Y) +
                            ImVec2(3.0f, 1.0f),
                        ImColor(ImVec4(0, 0, 0, 0.4f)));

                    const auto tagged =
                        client.GameMode == GameMode_Tag &&
                        p->Id == client.TaggedPlayerId;
                    window->DrawList->AddText(
                        topLeft,
                        ImColor(tagged ? ImVec4(1, 0, 0, 1) : ImVec4(1, 1, 1, 1)),
                        label);
                }
            }
        }

        players.Mutex.unlock_shared();
        ImGui::EndRawScene();
        }
    }

    const auto window = ImGui::BeginRawScene("##client-backbuffer-chat");
    if (window && window->DrawList) {
    const auto io = ImGui::GetIO();

    const auto width = io.DisplaySize.x / 3.0f;

    auto opacity = 1.0f;
    if (!chat.Focused) {
        if (chat.ShowOverlay) {
            auto diff =
                static_cast<float>(GetTickCount64() - chat.LastTime) / 1000.0f;
            if (diff > 5.0f) {
                opacity = max(0, 1.0f - (diff - 5.0f));
            }
        } else {
            opacity = 0.0f;
        }
    }

    if (opacity > 0.0f) {
        chat.Mutex.lock();

        auto body = chat.Raw;
        if (!chat.Focused) {
            auto messages = 0;
            for (auto i = static_cast<int>(body.size()) - 1; i >= 0; --i) {
                if (body[i] == '\n') {
                    ++messages;
                }

                if (messages > unfocusedChatMessages) {
                    body = body.substr(i + 1);
                    break;
                }
            }
        }

        const auto height =
            ImGui::CalcTextSize(body.c_str(), nullptr, false, width).y +
            (ImGui::GetTextLineHeight() / 6.0f);

        const auto pos = ImVec2(inputWidthOffset,
                                io.DisplaySize.y - inputHeightOffset - height);

        ImGui::SetWindowPos(pos, ImGuiCond_Always);
        ImGui::SetWindowSize(
            ImVec2(window->Size.x, max(window->Size.y, height)));

        window->DrawList->AddRectFilled(
            pos, ImVec2(pos.x + width, pos.y + height),
            ImColor(ImVec4(0, 0, 0, 0.4f * opacity)));

        ImGui::PushTextWrapPos(width);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, opacity));
        ImGui::TextWrapped("%s", body.c_str());
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();

        chat.Mutex.unlock();
    }

    ImGui::EndRawScene();
    }

    if (showLatency && connected.load() && latencyMs >= 0) {
        const auto latencyWindow = ImGui::BeginRawScene("##client-backbuffer-latency");
        if (latencyWindow && latencyWindow->DrawList) {
        const auto io = ImGui::GetIO();
        char latencyText[0x40];
        snprintf(latencyText, sizeof(latencyText), "Ping: %d ms", latencyMs);

        const auto size = ImGui::CalcTextSize(latencyText);
        const auto pos =
            ImVec2(io.DisplaySize.x - size.x - 12.0f, 12.0f);

        latencyWindow->DrawList->AddRectFilled(
            pos - ImVec2(4.0f, 2.0f),
            ImVec2(pos.x + size.x, pos.y + size.y) + ImVec2(4.0f, 2.0f),
            ImColor(ImVec4(0, 0, 0, 0.45f)));
        latencyWindow->DrawList->AddText(
            pos, ImColor(ImVec4(0.85f, 1.0f, 0.85f, 1.0f)), latencyText);
        ImGui::EndRawScene();
        }
    }

    if (chat.Focused) {
        if (ImGui::BeginRawScene("##client-backbuffer-chatinput")) {
        const auto io = ImGui::GetIO();

        ImGui::SetWindowPos(
            ImVec2(inputWidthOffset, io.DisplaySize.y - inputHeightOffset),
            ImGuiCond_Always);
        ImGui::SetKeyboardFocusHere(0);

        ImGui::PushItemWidth(io.DisplaySize.x - inputWidthOffset * 2);
        ImGui::InputText("##client-chat-overlay-input", chatInput,
                         sizeof(chatInput));
        ImGui::PopItemWidth();

        ImGui::EndRawScene();
        }
    }

    OnRenderGames(device);
}

void OnRenderGames(IDirect3DDevice9 *device) {
    if (!showTagDistanceOverlay && !showTagCooldownOverlay) {
        return;
    }

    if (client.Level.empty() || client.Level == Map_MainMenu) {
        return;
    }

    // B0: ResolveLocal (cache/PC) — bare GetPlayerPawn misses soft-resolve.
    Classes::FVector localLoc = {};
    if (!TryGetLocalHostLocation(localLoc)) {
        return;
    }
    MeSdk::Safe::Gameplay::PawnPoseSnapshot localPose = {};
    localPose.location = localLoc;

    int playersInTheSameLevel = 0;
    float longestNameWidth = 0.0f;

    for (const auto &p : players.List) {
        Classes::FVector remLoc = {};
        if (p && LevelsCompatible(p->Level, client.Level) &&
            TryGetRemoteLocation(p, remLoc)) {
            ++playersInTheSameLevel;
            const float nameWidth =
                ImGui::CalcTextSize(p->Name.c_str(), nullptr, false).x;
            if (nameWidth > longestNameWidth) {
                longestNameWidth = nameWidth;
            }
        }
    }

    if (playersInTheSameLevel == 0) {
        return;
    }

    char buffer[0x200] = {0};
    const auto io = ImGui::GetIO();
    const float textHeight = ImGui::GetTextLineHeight();

    static const float padding = 5.0f;
    static const float maxNameWidth = 192.0f;

    if (showTagDistanceOverlay) {
        auto window = ImGui::BeginRawScene("##tag-info");
        if (window && window->DrawList) {

        static const float rightPadding = 80.0f;
        float y = (playersInTheSameLevel * textHeight) - textHeight + (padding / 2);
        const float x = min(maxNameWidth, longestNameWidth);

        window->DrawList->AddRectFilled(
            ImVec2(),
            ImVec2(rightPadding + x + padding, y + padding + textHeight),
            ImColor(ImVec4(0, 0, 0, 0.4f)));

        for (const auto &p : players.List) {
            Classes::FVector remLoc = {};
            if (!p || !LevelsCompatible(p->Level, client.Level) ||
                !TryGetRemoteLocation(p, remLoc)) {
                continue;
            }

            const float dist = MeSdk::Distance(remLoc, localPose.location);
            if (dist >= 10.0f) {
                snprintf(buffer, sizeof(buffer), "%.0f m", dist);
            } else {
                snprintf(buffer, sizeof(buffer), "%.1f m", dist);
            }

            auto name = p->Name;
            auto color = ImColor(ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

            if (client.GameMode == GameMode_Tag &&
                p->Id == client.TaggedPlayerId) {
                color = ImColor(ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
            }

            if (ImGui::CalcTextSize(name.c_str(), nullptr, false).x >
                maxNameWidth) {
                int charactersToRemove = 3;
                do {
                    name = p->Name;
                    name = name.substr(0, name.size() - charactersToRemove++) +
                           "...";
                } while (ImGui::CalcTextSize(name.c_str(), nullptr, false).x >
                         maxNameWidth);
            }

            window->DrawList->AddText(ImVec2(padding, y), color, buffer);
            window->DrawList->AddText(ImVec2(rightPadding, y), color,
                                      name.c_str());

            y -= textHeight;
        }

        ImGui::EndRawScene();
        }
    }

    if (showTagCooldownOverlay) {
        if (taggedTimed == 0) {
            return;
        }

        const auto timeLeftTick =
            taggedTimed +
            (static_cast<unsigned long long>(client.CoolDownTag) * 1000) -
            GetTickCount64();
        const auto timeLeft = static_cast<float>(timeLeftTick) / 1000.0f;

        if (timeLeft < 0.0f || timeLeft > UINT_MAX) {
            return;
        }

        auto playerName = client.Name;
        if (client.Id != client.TaggedPlayerId) {
            const auto player = GetPlayerById(client.TaggedPlayerId);
            if (!player) {
                return;
            }
            playerName = player->Name;
        }

        auto name = playerName;
        if (ImGui::CalcTextSize(name.c_str(), nullptr, false).x > maxNameWidth) {
            int charactersToRemove = 3;
            do {
                name = playerName;
                name = name.substr(0, name.size() - charactersToRemove++) +
                       "...";
            } while (ImGui::CalcTextSize(name.c_str(), nullptr, false).x >
                     maxNameWidth);
        }

        snprintf(buffer, sizeof(buffer), "%s can move in %.1f seconds", name.c_str(), timeLeft);

        auto window = ImGui::BeginRawScene("##tag-timeleft");
        if (window && window->DrawList) {

        const float textSize = ImGui::CalcTextSize(buffer, nullptr, false).x;
        const float topMiddleX = io.DisplaySize.x / 2.0f - textSize / 2.0f;

        window->DrawList->AddRectFilled(
            ImVec2(topMiddleX - padding, 0),
            ImVec2(topMiddleX + textSize + padding, textHeight + padding),
            ImColor(ImVec4(0, 0, 0, 0.4f)));
        window->DrawList->AddText(
            ImVec2(topMiddleX, padding / 2.0f),
            ImColor(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)), buffer);

        ImGui::EndRawScene();
        }
    }
}

void RenderTagMinigamesSection() {
    if (!ImGui::CollapsingHeader("Tag / Minigames##client-tag-section",
                                 ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    ImGui::Text("Tag");

    if (ImGui::Checkbox("Distance Overlay##Tag-DistanceOverlay",
                        &showTagDistanceOverlay)) {
        Settings::SetSetting("games", "tagShowDistanceOverlay",
                             showTagDistanceOverlay);
    }
    HelpMarker(
        "Shows the distance to other players in meters. Tag doesn't need to be "
        "enabled for this");

    if (client.GameMode == GameMode_Tag) {
        ImGui::Checkbox("Cooldown Overlay##Tag-CooldownOverlay",
                        &showTagCooldownOverlay);
        HelpMarker("When someone gets tagged, shows the cooldown at the top "
                   "middle of the screen until they can move again");
    }

    if (client.Level == Map_MainMenu || players.List.empty()) {
        ImGui::Separator();
        ImGui::TextWrapped(
            "You can't start tag when you're in the main menu or when there are "
            "no other players in the room!");
    } else {
        char buffer[0xFF];

        if (client.GameMode == GameMode_None) {
            ImGui::SliderInt("Cooldown Timer##Tag-CooldownSlider", &tagCooldown,
                             1, 60);
            HelpMarker(
                "Cooldown in seconds (1â€“60). Default is 5.");

            ImGui::Separator();

            if (ImGui::Button("Update Cooldown Timer##Tag-UpdateCooldownTime")) {
                client.CoolDownTag = tagCooldown;
                SendJsonMessage({
                    {"type", "cooldown"},
                    {"cooldown", tagCooldown},
                });

                snprintf(buffer, sizeof(buffer),
                          "[Tag] %s changed the cooldown to be %d second%s",
                          client.Name.c_str(), tagCooldown,
                          tagCooldown != 1 ? "s" : "");

                SendJsonMessage({
                    {"type", "announce"},
                    {"body", buffer},
                });
            }

            ImGui::SameLine();
            if (ImGui::Button("Start Tag##client-start-tag")) {
                SendJsonMessage({
                    {"type", "startTagGameMode"},
                });

                snprintf(buffer, sizeof(buffer), "[Tag] %s started tag",
                          client.Name.c_str());

                SendJsonMessage({
                    {"type", "announce"},
                    {"body", buffer},
                });
            }
            HarnessUi::Record("mm/multiplayer/client-start-tag", Engine::GetWindow());
        }

        if (client.GameMode == GameMode_Tag) {
            if (ImGui::Button("End Tag##Tag-EndTag")) {
                SendJsonMessage({
                    {"type", "endGameMode"},
                });

                snprintf(buffer, sizeof(buffer), "[Tag] %s ended tag",
                          client.Name.c_str());

                SendJsonMessage({
                    {"type", "announce"},
                    {"body", buffer},
                });
            }
        }
    }
}

void EnsureClientRuntimeHooks() {
    if (hooksRegistered) {
        return;
    }
    hooksRegistered = true;
    Engine::QueueMainThreadTask(InstallClientRuntimeHooks);
}

void MultiplayerTab() {
    EnsureClientRuntimeHooks();
    StartClientListenerIfNeeded();

    // #region agent log
    static bool loggedTabEnter = false;
    if (!loggedTabEnter) {
        loggedTabEnter = true;
        MpDebugLog("client.cpp:MultiplayerTab", "tab_enter", "H-T");
    }
    // #endregion

    HarnessUi::BeginFrame();

    // --- Connection status ---
    if (connected.load()) {
        const int remoteCount = static_cast<int>(players.List.size());
        if (latencyMs >= 0) {
            ImGui::TextColored({0.0f, 1.0f, 0.0f, 1.0f},
                               "Status: Connected  |  Ping: %d ms  |  Players: %d",
                               latencyMs, remoteCount);
        } else {
            ImGui::TextColored({0.0f, 1.0f, 0.0f, 1.0f},
                               "Status: Connected  |  Ping: measuring...  |  Players: %d",
                               remoteCount);
        }
        ImGui::SameLine();
        if (ImGui::Button("Disconnect##client-disconnect", {120, 0})) {
            disabled.store(true);
            Disconnect();
        }
    } else {
        if (disabled.load()) {
            ImGui::TextColored({1.0f, 0.5f, 0.0f, 1.0f},
                               "Status: Disconnected");
        } else {
            const auto error = GetConnectionError();
            if (!error.empty()) {
                ImGui::TextColored({1.0f, 1.0f, 0.0f, 1.0f},
                                   "Status: Connecting...");
                ImGui::TextColored({1.0f, 0.5f, 0.3f, 1.0f},
                                   "  Error: %s", error.c_str());
            } else {
                ImGui::TextColored({1.0f, 1.0f, 0.0f, 1.0f},
                                   "Status: Connecting...");
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Connect##client-connect", {120, 0})) {
            g_connectionAttempt.store(0);
            ClearConnectionError();
            disabled.store(false);
            StartClientListenerIfNeeded();
        }
    }

    ImGui::Separator();

    ImGui::Text("Level: %s", client.Level.empty() ? "(unknown)" : client.Level.c_str());
    if (connected.load()) {
        ImGui::SameLine();
        if (ImGui::Button("Set Gameplay##client-level-gameplay")) {
            ApplyManualClientLevel("gameplay");
        }
        ImGui::SameLine();
        if (ImGui::Button("Set Menu##client-level-menu")) {
            ApplyManualClientLevel("tdmainmenu");
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Diag##client-diag")) {
        const int hosted = ModHost::IsAttached() ? 1 : 0;
        const int dis = disabled.load() ? 1 : 0;
        const int connectedVal = connected.load() ? 1 : 0;
        const int loadingVal = loading.load() ? 1 : 0;
        const int live = Engine::IsHostedGameplayLive() ? 1 : 0;
        const int hooksOk = Engine::AreGameplayHooksInstalled() ? 1 : 0;
        int spawned = 0;
        int posed = 0;
        players.Mutex.lock_shared();
        for (const auto &p : players.List) {
            auto *remote = PlayerRemoteActor(p);
            if (!p || !remote || !MeSdk::Safe::IsPlausibleUObject(remote)) {
                continue;
            }
            ++spawned;
            Classes::FVector loc = {};
            MeSdk::Safe::Gameplay::TryReadActorLocation(remote, loc);
            const int hasPose = p->ToTime ? 1 : 0;
            if (hasPose) {
                ++posed;
            }
            ClientLogf("client: diag remote id=%x actor=%p pose=%d loc=(%.0f,%.0f,%.0f)",
                       p->Id, p->Actor, hasPose, loc.X, loc.Y, loc.Z);
        }
        players.Mutex.unlock_shared();
        char diag[512] = {};
        snprintf(diag, sizeof(diag),
                 "DIAG: button — hosted=%d disabled=%d connected=%d loading=%d level=%s live=%d hooks=%d players=%zu spawned=%d posed=%d\n",
                 hosted, dis, connectedVal, loadingVal, client.Level.c_str(), live, hooksOk,
                 players.List.size(), spawned, posed);
        OutputDebugStringA(diag);
        ClientLogf("client: diag hosted=%d disabled=%d connected=%d loading=%d level=%s live=%d hooks=%d players=%zu spawned=%d posed=%d",
                   hosted, dis, connectedVal, loadingVal, client.Level.c_str(), live, hooksOk,
                   players.List.size(), spawned, posed);
    }

    ImGui::Separator();

    const auto nameInputCallback = []() {
        if (client.Name != nameInput) {
            auto empty = true;
            for (auto c : std::string(nameInput)) {
                if (!isblank(c)) {
                    empty = false;
                    break;
                }
            }

            if (!empty) {
                AddChatMessage(client.Name + " renamed to " + nameInput);
                client.Name = nameInput;
                Settings::SetSetting("client", "name", client.Name);

                if (connected.load()) {
                    SendJsonMessage({
                        {"type", "name"},
                        {"id", client.Id},
                        {"name", client.Name},
                    });
                }
            }
        }
    };

    ImGui::Text("Name");
    ImGui::SameLine();
    ImGui::InputText("##client-name-input", nameInput, sizeof(nameInput));

    ImGui::SameLine();
    if (ImGui::Button("Change##client-name-button") && ImGui::IsItemHovered()) {
        nameInputCallback();
    }

    ImGui::Text("Character");
    ImGui::SameLine();

    const auto selectedCharacter =
        Engine::Characters[static_cast<size_t>(client.Character)];

    if (ImGui::BeginCombo("##client-character", selectedCharacter)) {
        for (auto i = 0; i < static_cast<int>(Engine::Character::Max); ++i) {
            const auto c = Engine::Characters[i];
            const auto s = (c == selectedCharacter);

            if (ImGui::Selectable(c, s)) {
                client.Character = static_cast<Engine::Character>(i);
                Settings::SetSetting("client", "character", client.Character);

                if (connected.load()) {
                    SendJsonMessage({
                        {"type", "character"},
                        {"id", client.Id},
                        {"character", client.Character},
                    });
                }
            }

            if (s) {
                ImGui::SetItemDefaultFocus();
            }
        }

        ImGui::EndCombo();
    }

    const auto serverInputCallback = []() {
        const auto nextServer = NormalizeServerHost(serverInput);
        if (GetConfiguredServerHost() != nextServer) {
            auto empty = true;
            for (auto c : nextServer) {
                if (!isblank(c)) {
                    empty = false;
                    break;
                }
            }

            if (!empty) {
                SetConfiguredServerHost(nextServer);
                Settings::SetSetting("client", "server", nextServer);
                const size_t n = sizeof(serverInput) - 1;
                strncpy(serverInput, nextServer.c_str(), n);
                serverInput[n] = '\0';
                g_connectionAttempt.store(0);
                ClearConnectionError();

                if (!disabled.load()) {
                    Disconnect();
                }
            }
        }
    };

    ImGui::Text("Server");
    ImGui::SameLine();
    ImGui::InputText("##client-server-input", serverInput,
                         sizeof(serverInput));
    HarnessUi::Record("mm/multiplayer/server-input", Engine::GetWindow());

    ImGui::SameLine();
    if (ImGui::Button("Apply##client-server-button")) {
        serverInputCallback();
    }
    HarnessUi::Record("mm/multiplayer/server-apply", Engine::GetWindow());

    if (ImGui::Checkbox("Interpolation##client-interpolation",
                        &interpolationEnabled)) {
        Settings::SetSetting("client", "interpolation", interpolationEnabled);
    }

    ImGui::SameLine();
    if (ImGui::Checkbox("Show Latency##client-show-latency", &showLatency)) {
        Settings::SetSetting("client", "showLatency", showLatency);
    }

    if (interpolationEnabled) {
        if (ImGui::Checkbox("Auto Interp Delay##client-interp-auto",
                            &interpolationDelayAuto)) {
            Settings::SetSetting("client", "interpolationDelayAuto",
                                 interpolationDelayAuto);
            if (!interpolationDelayAuto) {
                interpolationDelayMs = interpolationDelayBaseMs;
            }
        }
        if (ImGui::SliderInt("Interp Delay (ms)##client-interp-delay",
                             &interpolationDelayBaseMs, 0, 250)) {
            Settings::SetSetting("client", "interpolationDelay",
                                 interpolationDelayBaseMs);
            if (!interpolationDelayAuto) {
                interpolationDelayMs = interpolationDelayBaseMs;
            }
        }
        if (interpolationDelayAuto) {
            ImGui::TextDisabled(
                "live auto delay: %d ms (base %d; per-remote)",
                interpolationDelayMs, interpolationDelayBaseMs);
        }
    }

    if (ImGui::Checkbox("Pose Smooth##client-pose-smooth", &poseSmoothEnabled)) {
        Settings::SetSetting("client", "poseSmooth", poseSmoothEnabled);
    }
    if (poseSmoothEnabled) {
        if (ImGui::SliderFloat("Smooth Alpha##client-pose-smooth-alpha",
                               &poseSmoothAlpha, 0.15f, 1.0f, "%.2f")) {
            Settings::SetSetting("client", "poseSmoothAlpha", poseSmoothAlpha);
        }
        if (ImGui::SliderFloat("Snap Dist (UU)##client-pose-snap", &poseSnapUu,
                               100.0f, 800.0f, "%.0f")) {
            Settings::SetSetting("client", "poseSnapUu", poseSnapUu);
        }
    }

    if (ImGui::Checkbox("Bone Smooth##client-bone-smooth", &boneSmoothEnabled)) {
        Settings::SetSetting("client", "boneSmooth", boneSmoothEnabled);
    }
    if (boneSmoothEnabled) {
        if (ImGui::SliderFloat("Bone Alpha##client-bone-smooth-alpha",
                               &boneSmoothAlpha, 0.15f, 1.0f, "%.2f")) {
            Settings::SetSetting("client", "boneSmoothAlpha", boneSmoothAlpha);
        }
        if (ImGui::SliderFloat("Idle Bone Alpha##client-bone-idle-alpha",
                               &boneSmoothIdleAlpha, 0.15f, 1.0f, "%.2f")) {
            Settings::SetSetting("client", "boneSmoothIdleAlpha",
                                 boneSmoothIdleAlpha);
        }
        if (ImGui::SliderFloat("Walk Bone Alpha##client-bone-walk-alpha",
                               &boneSmoothWalkAlpha, 0.15f, 1.0f, "%.2f")) {
            Settings::SetSetting("client", "boneSmoothWalkAlpha",
                                 boneSmoothWalkAlpha);
        }
    }

    if (ImGui::Checkbox("Stance on Nametag##client-stance-nametag",
                        &showRemoteStanceOnNametag)) {
        Settings::SetSetting("client", "showRemoteStanceOnNametag",
                             showRemoteStanceOnNametag);
    }

    const auto roomInputCallback = []() {
        if (room != roomInput) {
            auto empty = true;
            for (auto c : std::string(roomInput)) {
                if (!isblank(c)) {
                    empty = false;
                    break;
                }
            }

            if (!empty) {
                room = roomInput;
                Settings::SetSetting("client", "room", room);

                if (!disabled.load()) {
                    Disconnect();
                }
            }
        }
    };

    ImGui::Text("Room");
    ImGui::SameLine();
    ImGui::InputText("##client-room-input", roomInput, sizeof(roomInput));

    ImGui::SameLine();
    if (ImGui::Button("Join##client-room-button")) {
        roomInputCallback();
    }
    HarnessUi::Record("mm/multiplayer/client-join", Engine::GetWindow());

    if (ImGui::Hotkey("Chat Keybind##client-chat-keybind", &chat.Keybind)) {
        Settings::SetSetting("client", "chatKeybind", chat.Keybind);
    }

    if (ImGui::Checkbox("Show Nametags##client-show-nametags",
                        &players.ShowNameTags)) {

        Settings::SetSetting("client", "showNameTags", players.ShowNameTags);
    }

    ImGui::SameLine();

    if (ImGui::Checkbox("Show Chat Overlay##client-show-chat",
                        &chat.ShowOverlay)) {

        Settings::SetSetting("client", "showChatOverlay", chat.ShowOverlay);
    }

    if (ImGui::Checkbox("Soft Collision##client-soft-collision",
                        &softCollisionEnabled)) {
        Settings::SetSetting("client", "softCollision", softCollisionEnabled);
    }
    ImGui::SameLine();
    HelpMarker(
        "Fake XY push when you overlap remotes. Separates remotes on the live "
        "pose path and lightly nudges you; does not use UE physics or "
        "disconnect mutates (KI-2026-012).");

    if (ImGui::Checkbox("World Clamp##client-world-clamp",
                        &worldClampEnabled)) {
        Settings::SetSetting("client", "worldClamp", worldClampEnabled);
    }
    ImGui::SameLine();
    HelpMarker(
        "Snap remotes that jump too high above you (floor) or take a huge XY "
        "step (wall). Also clamps lateral offset to your look axis so Kate "
        "stays on narrow walks (body yaw corridor; solar/fence) without Trace.");
    if (worldClampEnabled &&
        ImGui::SliderFloat("Max Lateral (UU)##client-world-clamp-lat",
                           &worldClampMaxLateral, 40.0f, 250.0f, "%.0f")) {
        Settings::SetSetting("client", "worldClampMaxLateral",
                             worldClampMaxLateral);
    }

    if (ImGui::Hotkey("Interact Keybind##client-interact-keybind",
                      &interactKeybind)) {
        Settings::SetSetting("client", "interactKeybind", interactKeybind);
    }
    ImGui::SameLine();
    HelpMarker(
        "Press near another player to send a wave (TCP chat only; does not "
        "mutate remote actors).");
    if (ImGui::SliderFloat("Interact Range (m)##client-interact-range",
                           &interactMaxMeters, 0.5f, 8.0f, "%.1f")) {
        Settings::SetSetting("client", "interactMaxMeters", interactMaxMeters);
    }
    if (ImGui::Button("Wave Nearest##client-wave-nearest") &&
        connected.load()) {
        TrySendNearestInteract("wave");
    }

    ImGui::Text("Chat");

    chat.Mutex.lock();
    ImGui::InputTextMultiline(
        "##client-chat", const_cast<char *>(chat.Raw.c_str()), chat.Raw.size(),
        {0, 0}, ImGuiInputTextFlags_ReadOnly);
    chat.Mutex.unlock();

    ImGui::InputText("##client-chat-input", chatInput, sizeof(chatInput));

    ImGui::SameLine();
    if (ImGui::Button("Send##client-chat-send")) {
        SendChatInput();
    }
    HarnessUi::Record("mm/multiplayer/client-send", Engine::GetWindow());

    players.Mutex.lock_shared();
    const auto playerCount = players.List.size();
    if (ImGui::TreeNode("##client-players", "Players (%d)", playerCount)) {
        if (playerCount == 0) {
            ImGui::TextDisabled("No other players connected");
        } else {
            for (auto p : players.List) {
                ImGui::Text("%s", p->Name.c_str());
                ImGui::SameLine();
                ImGui::TextDisabled("- %s", p->Level.empty() ? "..." : p->Level.c_str());
                ImGui::SameLine();

                if (ImGui::Button(
                        ("Goto##client-goto-" + std::to_string(p->Id)).c_str()) &&
                    LevelsCompatible(p->Level, client.Level) &&
                    PlayerHasRemoteVisual(p)) {

                    auto *pawn = ResolveLocalPlayerPawn(false);
                    Classes::FVector dest = {};
                    if (pawn && TryGetRemoteLocation(p, dest)) {
                        MeSdk::Safe::Gameplay::TryWriteActorLocation(pawn, dest);
                    }
                }
            }
        }

        ImGui::TreePop();
    }
    players.Mutex.unlock_shared();

    ImGui::Separator();
    RenderTagMinigamesSection();
}
} // namespace ClientInternal
