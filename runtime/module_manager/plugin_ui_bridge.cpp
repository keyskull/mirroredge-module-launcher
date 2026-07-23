#include "plugin_ui_bridge.h"

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "plugin_ui_api.h"
#include "plugin_ui.h"

#include <cstdarg>

namespace {

// Plugin-side layout (matches shared/plugin_ui.h; used when real imgui owns type names).
struct PluginImGuiIO {
	::ImVec2 DisplaySize;
};

struct PluginImDrawList {
	void *host;
};

struct PluginImGuiWindow {
	::ImVec2 Size;
	PluginImDrawList *DrawList;
};

thread_local PluginImDrawList tlDrawList = {};
thread_local PluginImGuiWindow tlWindow = {};

static ::ImDrawList *RealDrawList(ImDrawList *draw_list) {
	if (!draw_list) {
		return nullptr;
	}
	return static_cast<::ImDrawList *>(reinterpret_cast<PluginImDrawList *>(draw_list)->host);
}

static PluginImGuiIO BridgeGetIO() {
	const ::ImGuiIO &real = ImGui::GetIO();
	return PluginImGuiIO{real.DisplaySize};
}

static float BridgeGetStyleAlpha() { return ImGui::GetStyle().Alpha; }

static float BridgeGetTime() { return static_cast<float>(ImGui::GetTime()); }

static float BridgeGetTextLineHeight() { return ImGui::GetTextLineHeight(); }

static float BridgeGetFrameHeightWithSpacing() {
	return ImGui::GetFrameHeightWithSpacing();
}

static float BridgeGetScrollY() { return ImGui::GetScrollY(); }

static float BridgeGetScrollMaxY() { return ImGui::GetScrollMaxY(); }

static ::ImVec2 BridgeCalcTextSize(const char *text, const char *text_end,
                                   bool hide_text_after_double_hash, float wrap_width) {
	return ImGui::CalcTextSize(text, text_end, hide_text_after_double_hash, wrap_width);
}

static void BridgeSetNextWindowPos(const ::ImVec2 &pos, int cond) {
	ImGui::SetNextWindowPos(pos, static_cast<ImGuiCond>(cond));
}

static void BridgeSetNextWindowSize(const ::ImVec2 &size, int cond) {
	ImGui::SetNextWindowSize(size, static_cast<ImGuiCond>(cond));
}

static void BridgeSetNextWindowBgAlpha(float alpha) { ImGui::SetNextWindowBgAlpha(alpha); }

static void BridgeSetWindowPos(const ::ImVec2 &pos, int cond) {
	ImGui::SetWindowPos(pos, static_cast<ImGuiCond>(cond));
}

static void BridgeSetWindowSize(const ::ImVec2 &size) { ImGui::SetWindowSize(size); }

static void BridgeSetKeyboardFocusHere(int offset) { ImGui::SetKeyboardFocusHere(offset); }

static bool BridgeBegin(const char *name, bool *open, int flags) {
	return ImGui::Begin(name, open, static_cast<ImGuiWindowFlags>(flags));
}

static void BridgeEnd() { ImGui::End(); }

static bool BridgeBeginTabBar(const char *str_id, int flags) {
	return ImGui::BeginTabBar(str_id, static_cast<ImGuiTabBarFlags>(flags));
}

static void BridgeEndTabBar() { ImGui::EndTabBar(); }

static bool BridgeBeginTabItem(const char *label, bool *open, int flags) {
	return ImGui::BeginTabItem(label, open, static_cast<ImGuiTabItemFlags>(flags));
}

static void BridgeEndTabItem() { ImGui::EndTabItem(); }

static bool BridgeBeginChild(const char *str_id, const ::ImVec2 &size, bool border,
                             int flags) {
	return ImGui::BeginChild(str_id, size, border, static_cast<ImGuiWindowFlags>(flags));
}

static void BridgeEndChild() { ImGui::EndChild(); }

static void BridgeBeginTooltip() { ImGui::BeginTooltip(); }

static void BridgeEndTooltip() { ImGui::EndTooltip(); }

static void BridgeText(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	ImGui::TextV(fmt, args);
	va_end(args);
}

static void BridgeTextWrapped(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	ImGui::TextWrappedV(fmt, args);
	va_end(args);
}

static void BridgeTextDisabled(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	ImGui::TextDisabledV(fmt, args);
	va_end(args);
}

static void BridgeTextColored(const ::ImVec4 &col, const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	ImGui::TextColoredV(col, fmt, args);
	va_end(args);
}

static void BridgeTextUnformatted(const char *text, const char *text_end) {
	ImGui::TextUnformatted(text, text_end);
}

static void BridgeSeparator() { ImGui::Separator(); }

static void BridgeSpacing() { ImGui::Spacing(); }

static void BridgeSameLine(float offset_from_start_x, float spacing) {
	ImGui::SameLine(offset_from_start_x, spacing);
}

static void BridgeIndent(float indent_w) { ImGui::Indent(indent_w); }

static void BridgeUnindent(float indent_w) { ImGui::Unindent(indent_w); }

static void BridgeSetTooltip(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	ImGui::SetTooltipV(fmt, args);
	va_end(args);
}

static bool BridgeInputFloat(const char *label, float *v, float step, float step_fast,
                             const char *format, int flags) {
	return ImGui::InputFloat(label, v, step, step_fast, format,
	                         static_cast<ImGuiInputTextFlags>(flags));
}

static bool BridgeTreeNode(const char *id, const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	const bool result = ImGui::TreeNodeV(id, fmt, args);
	va_end(args);
	return result;
}

static void BridgeTreePop() { ImGui::TreePop(); }

static void BridgePushID_Str(const char *str_id) { ImGui::PushID(str_id); }

static void BridgePushID_Int(int int_id) { ImGui::PushID(int_id); }

static void BridgePopID() { ImGui::PopID(); }

static bool BridgeButton(const char *label, const ::ImVec2 &size) {
	return ImGui::Button(label, size);
}

static bool BridgeCheckbox(const char *label, bool *v) { return ImGui::Checkbox(label, v); }

static bool BridgeInputText(const char *label, char *buf, size_t buf_size, int flags) {
	return ImGui::InputText(label, buf, buf_size, static_cast<ImGuiInputTextFlags>(flags));
}

static void BridgeDeleteChars(PluginUiInputTextCallbackData *data, int pos, int bytes_count) {
	if (!data || !data->userData) {
		return;
	}
	auto *imguiData = static_cast<ImGuiInputTextCallbackData *>(data->userData);
	imguiData->DeleteChars(pos, bytes_count);
}

static void BridgeInsertChars(PluginUiInputTextCallbackData *data, int pos, const char *text) {
	if (!data || !data->userData || !text) {
		return;
	}
	auto *imguiData = static_cast<ImGuiInputTextCallbackData *>(data->userData);
	imguiData->InsertChars(pos, text);
}

struct InputTextCallbackPayload {
	PluginUiInputTextCallbackFn cb = nullptr;
	void *user = nullptr;
};

static thread_local InputTextCallbackPayload g_inputTextCallbackPayload;

static int InputTextCallbackTrampoline(ImGuiInputTextCallbackData *data) {
	if (!g_inputTextCallbackPayload.cb) {
		return 0;
	}

	PluginUiInputTextCallbackData bridged = {};
	bridged.eventFlag = data->EventFlag;
	bridged.eventKey = data->EventKey;
	bridged.buf = data->Buf;
	bridged.bufTextLen = data->BufTextLen;
	bridged.bufSize = data->BufSize;
	bridged.deleteChars = BridgeDeleteChars;
	bridged.insertChars = BridgeInsertChars;
	bridged.userData = data;
	return g_inputTextCallbackPayload.cb(&bridged);
}

static bool BridgeInputTextCallback(const char *label, char *buf, size_t buf_size, int flags,
                                    PluginUiInputTextCallbackFn callback, void *user_data) {
	g_inputTextCallbackPayload.cb = callback;
	g_inputTextCallbackPayload.user = user_data;
	return ImGui::InputText(label, buf, buf_size, static_cast<ImGuiInputTextFlags>(flags),
	                        InputTextCallbackTrampoline, nullptr);
}

static bool BridgeInputTextMultiline(const char *label, char *buf, size_t buf_size,
                                     const ::ImVec2 &size, int flags) {
	return ImGui::InputTextMultiline(label, buf, buf_size, size,
	                                 static_cast<ImGuiInputTextFlags>(flags));
}

static bool BridgeInputInt(const char *label, int *v, int step, int step_fast, int flags) {
	return ImGui::InputInt(label, v, step, step_fast, static_cast<ImGuiInputTextFlags>(flags));
}

static bool BridgeSliderInt(const char *label, int *v, int v_min, int v_max, int *highlights,
                            int num_highlights, const char *format) {
	return ImGui::SliderInt(label, v, v_min, v_max, highlights, num_highlights, format);
}

static bool BridgeSliderFloat(const char *label, float *v, float v_min, float v_max,
                              const char *format, float power) {
	return ImGui::SliderFloat(label, v, v_min, v_max, format, power);
}

static bool BridgeBeginCombo(const char *label, const char *preview_value, int flags) {
	return ImGui::BeginCombo(label, preview_value, static_cast<ImGuiComboFlags>(flags));
}

static void BridgeEndCombo() { ImGui::EndCombo(); }

static bool BridgeSelectable(const char *label, bool selected, int flags,
                             const ::ImVec2 &size) {
	return ImGui::Selectable(label, selected, static_cast<ImGuiSelectableFlags>(flags), size);
}

static void BridgeSetItemDefaultFocus() { ImGui::SetItemDefaultFocus(); }

static bool BridgeCollapsingHeader(const char *label, int flags) {
	return ImGui::CollapsingHeader(label, static_cast<ImGuiTreeNodeFlags>(flags));
}

static bool BridgeHotkey(const char *label, int *k, const ::ImVec2 &size) {
	return ImGui::Hotkey(label, k, size);
}

static void BridgePushStyleColor(int idx, const ::ImVec4 &col) {
	ImGui::PushStyleColor(static_cast<ImGuiCol>(idx), col);
}

static void BridgePopStyleColor(int count) { ImGui::PopStyleColor(count); }

static void BridgePushStyleVar(int idx, float val) {
	ImGui::PushStyleVar(static_cast<ImGuiStyleVar>(idx), val);
}

static void BridgePushStyleVarVec2(int idx, const ::ImVec2 &val) {
	ImGui::PushStyleVar(static_cast<ImGuiStyleVar>(idx), val);
}

static void BridgePopStyleVar(int count) { ImGui::PopStyleVar(count); }

static void BridgePushTextWrapPos(float wrap_local_pos_x) {
	ImGui::PushTextWrapPos(wrap_local_pos_x);
}

static void BridgePopTextWrapPos() { ImGui::PopTextWrapPos(); }

static void BridgePushItemWidth(float item_width) { ImGui::PushItemWidth(item_width); }

static void BridgePopItemWidth() { ImGui::PopItemWidth(); }

static bool BridgeIsItemHovered(int flags) {
	return ImGui::IsItemHovered(static_cast<ImGuiHoveredFlags>(flags));
}

static bool BridgeIsItemVisible() { return ImGui::IsItemVisible(); }

static bool BridgeGetItemRectMin(ImVec2 *out) {
	if (!out) {
		return false;
	}
	const ::ImVec2 rect = ImGui::GetItemRectMin();
	out->x = rect.x;
	out->y = rect.y;
	return true;
}

static bool BridgeGetItemRectMax(ImVec2 *out) {
	if (!out) {
		return false;
	}
	const ::ImVec2 rect = ImGui::GetItemRectMax();
	out->x = rect.x;
	out->y = rect.y;
	return true;
}

static bool BridgeGetMousePos(ImVec2 *out) {
	if (!out) {
		return false;
	}
	const ::ImVec2 pos = ImGui::GetIO().MousePos;
	out->x = pos.x;
	out->y = pos.y;
	return true;
}

static bool BridgeIsMouseReleased(int button) { return ImGui::IsMouseReleased(button); }

static void BridgeSetScrollHereY(float center_y_ratio) {
	ImGui::SetScrollHereY(center_y_ratio);
}

static ImGuiWindow *BridgeBeginRawScene(const char *name) {
	::ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
	::ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	::ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    if (!::ImGui::Begin(name, nullptr,
                        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                            ImGuiWindowFlags_NoMove |
                            ImGuiWindowFlags_NoBringToFrontOnFocus)) {
        ::ImGui::PopStyleColor();
        ::ImGui::PopStyleVar(2);
        return nullptr;
    }

    auto &io = ::ImGui::GetIO();
    ::ImGui::SetWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ::ImGui::SetWindowSize(ImVec2(io.DisplaySize.x, io.DisplaySize.y),
                           ImGuiCond_Always);

	tlWindow.Size = ImGui::GetWindowSize();
	tlDrawList.host = ImGui::GetWindowDrawList();
	tlWindow.DrawList = &tlDrawList;
	::ImGui::GetCurrentWindow()->DrawList->PushClipRectFullScreen();
	return reinterpret_cast<ImGuiWindow *>(&tlWindow);
}

static void BridgeEndRawScene() { ImGui::EndRawScene(); }

static void BridgeDrawListAddText(ImDrawList *draw_list, const ::ImVec2 &pos, ImU32 col,
                                  const char *text, const char *text_end) {
	if (::ImDrawList *real = RealDrawList(draw_list)) {
		real->AddText(pos, col, text, text_end);
	}
}

static void BridgeDrawListAddRectFilled(ImDrawList *draw_list, const ::ImVec2 &p_min,
                                        const ::ImVec2 &p_max, ImU32 col, float rounding,
                                        int flags) {
	if (::ImDrawList *real = RealDrawList(draw_list)) {
		real->AddRectFilled(p_min, p_max, col, rounding, static_cast<ImDrawCornerFlags>(flags));
	}
}

static void BridgeDrawListAddCircleFilled(ImDrawList *draw_list, const ::ImVec2 &center,
                                          float radius, ImU32 col, int num_segments) {
	if (::ImDrawList *real = RealDrawList(draw_list)) {
		real->AddCircleFilled(center, radius, col, num_segments);
	}
}

static PluginUiApi g_api = {};

static void InitApi() {
	g_api.version = MMOD_PLUGIN_UI_API_VERSION;
	g_api.GetIO = reinterpret_cast<decltype(g_api.GetIO)>(BridgeGetIO);
	g_api.GetStyleAlpha = BridgeGetStyleAlpha;
	g_api.GetTime = BridgeGetTime;
	g_api.GetTextLineHeight = BridgeGetTextLineHeight;
	g_api.GetFrameHeightWithSpacing = BridgeGetFrameHeightWithSpacing;
	g_api.GetScrollY = BridgeGetScrollY;
	g_api.GetScrollMaxY = BridgeGetScrollMaxY;
	g_api.CalcTextSize = BridgeCalcTextSize;
	g_api.SetNextWindowPos = BridgeSetNextWindowPos;
	g_api.SetNextWindowSize = BridgeSetNextWindowSize;
	g_api.SetNextWindowBgAlpha = BridgeSetNextWindowBgAlpha;
	g_api.SetWindowPos = BridgeSetWindowPos;
	g_api.SetWindowSize = BridgeSetWindowSize;
	g_api.SetKeyboardFocusHere = BridgeSetKeyboardFocusHere;
	g_api.Begin = BridgeBegin;
	g_api.End = BridgeEnd;
	g_api.BeginTabBar = BridgeBeginTabBar;
	g_api.EndTabBar = BridgeEndTabBar;
	g_api.BeginTabItem = BridgeBeginTabItem;
	g_api.EndTabItem = BridgeEndTabItem;
	g_api.BeginChild = BridgeBeginChild;
	g_api.EndChild = BridgeEndChild;
	g_api.BeginTooltip = BridgeBeginTooltip;
	g_api.EndTooltip = BridgeEndTooltip;
	g_api.Text = BridgeText;
	g_api.TextWrapped = BridgeTextWrapped;
	g_api.TextDisabled = BridgeTextDisabled;
	g_api.TextColored = BridgeTextColored;
	g_api.TextUnformatted = BridgeTextUnformatted;
	g_api.Separator = BridgeSeparator;
	g_api.Spacing = BridgeSpacing;
	g_api.SameLine = BridgeSameLine;
	g_api.Indent = BridgeIndent;
	g_api.Unindent = BridgeUnindent;
	g_api.SetTooltip = BridgeSetTooltip;
	g_api.InputFloat = BridgeInputFloat;
	g_api.TreeNode = BridgeTreeNode;
	g_api.TreePop = BridgeTreePop;
	g_api.PushID_Str = BridgePushID_Str;
	g_api.PushID_Int = BridgePushID_Int;
	g_api.PopID = BridgePopID;
	g_api.Button = BridgeButton;
	g_api.Checkbox = BridgeCheckbox;
	g_api.InputText = BridgeInputText;
	g_api.InputTextCallback = BridgeInputTextCallback;
	g_api.InputTextMultiline = BridgeInputTextMultiline;
	g_api.InputInt = BridgeInputInt;
	g_api.SliderInt = BridgeSliderInt;
	g_api.SliderFloat = BridgeSliderFloat;
	g_api.BeginCombo = BridgeBeginCombo;
	g_api.EndCombo = BridgeEndCombo;
	g_api.Selectable = BridgeSelectable;
	g_api.SetItemDefaultFocus = BridgeSetItemDefaultFocus;
	g_api.CollapsingHeader = BridgeCollapsingHeader;
	g_api.Hotkey = BridgeHotkey;
	g_api.PushStyleColor = BridgePushStyleColor;
	g_api.PopStyleColor = BridgePopStyleColor;
	g_api.PushStyleVar = BridgePushStyleVar;
	g_api.PushStyleVarVec2 = BridgePushStyleVarVec2;
	g_api.PopStyleVar = BridgePopStyleVar;
	g_api.PushTextWrapPos = BridgePushTextWrapPos;
	g_api.PopTextWrapPos = BridgePopTextWrapPos;
	g_api.PushItemWidth = BridgePushItemWidth;
	g_api.PopItemWidth = BridgePopItemWidth;
	g_api.IsItemHovered = BridgeIsItemHovered;
	g_api.IsItemVisible = BridgeIsItemVisible;
	g_api.GetItemRectMin = BridgeGetItemRectMin;
	g_api.GetItemRectMax = BridgeGetItemRectMax;
	g_api.GetMousePos = BridgeGetMousePos;
	g_api.IsMouseReleased = BridgeIsMouseReleased;
	g_api.SetScrollHereY = BridgeSetScrollHereY;
	g_api.BeginRawScene = BridgeBeginRawScene;
	g_api.EndRawScene = BridgeEndRawScene;
	g_api.DrawListAddText = BridgeDrawListAddText;
	g_api.DrawListAddRectFilled = BridgeDrawListAddRectFilled;
	g_api.DrawListAddCircleFilled = BridgeDrawListAddCircleFilled;
}

} // namespace

const PluginUiApi *PluginUiBridge_GetApi() {
	static bool initialized = false;
	if (!initialized) {
		InitApi();
		initialized = true;
	}
	return &g_api;
}
