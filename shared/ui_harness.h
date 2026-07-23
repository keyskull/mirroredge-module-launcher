#pragma once

#include "imgui/imgui.h"

#include <Windows.h>

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace HarnessUi {

struct Target {
    float minX = 0.0f;
    float minY = 0.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;
};

inline auto &State() {
    struct FrameState {
        std::mutex mutex;
        std::unordered_map<std::string, Target> targets;
        std::unordered_map<std::string, Target> previous;
    };
    static FrameState state;
    return state;
}

inline void BeginFrame() {
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.previous = state.targets;
    state.targets.clear();
}

inline void RecordClient(const char *id, float clientX, float clientY) {
    if (!id || !id[0]) {
        return;
    }

    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.targets[id] = {clientX, clientY, clientX, clientY};
}

inline void RecordRect(const char *id, float minX, float minY, float maxX,
                       float maxY) {
    if (!id || !id[0] || maxX <= minX || maxY <= minY) {
        return;
    }

    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.targets[id] = {minX, minY, maxX, maxY};
}

inline void Record(const char *id, HWND /*hwnd*/) {
    if (!id || !id[0] || !ImGui::IsItemVisible()) {
        return;
    }

    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    if (max.x <= min.x || max.y <= min.y) {
        return;
    }

    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.targets[id] = {min.x, min.y, max.x, max.y};
}

inline void FormatJson(std::string &out) {
    auto &state = State();
    std::vector<std::pair<std::string, Target>> snapshot;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        const auto &source =
            state.targets.empty() ? state.previous : state.targets;
        snapshot.reserve(source.size());
        for (const auto &entry : source) {
            snapshot.emplace_back(entry.first, entry.second);
        }
    }

    out = "{\"targets\":[";
    for (size_t i = 0; i < snapshot.size(); ++i) {
        if (i > 0) {
            out.push_back(',');
        }
        const auto &key = snapshot[i].first;
        const auto &target = snapshot[i].second;
        out += "{\"id\":\"";
        for (const char ch : key) {
            if (ch == '\\' || ch == '"') {
                out.push_back('\\');
            }
            out.push_back(ch);
        }
        out += "\",\"x\":";
        out += std::to_string(static_cast<int>((target.minX + target.maxX) * 0.5f));
        out += ",\"y\":";
        out += std::to_string(static_cast<int>((target.minY + target.maxY) * 0.5f));
        out += ",\"minX\":";
        out += std::to_string(static_cast<int>(target.minX));
        out += ",\"minY\":";
        out += std::to_string(static_cast<int>(target.minY));
        out += ",\"maxX\":";
        out += std::to_string(static_cast<int>(target.maxX));
        out += ",\"maxY\":";
        out += std::to_string(static_cast<int>(target.maxY));
        out += ",\"space\":\"client\"}";
    }
    out += "]}";
}

} // namespace HarnessUi
