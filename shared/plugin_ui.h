#pragma once

#include "plugin_ui_api.h"

#include <cstdarg>
#include <cstdio>

#ifndef IMGUI_VERSION

struct ImVec2 {
	float x;
	float y;
	ImVec2() : x(0), y(0) {}
	ImVec2(float x_, float y_) : x(x_), y(y_) {}
	ImVec2 operator+(const ImVec2 &rhs) const { return ImVec2(x + rhs.x, y + rhs.y); }
	ImVec2 operator-(const ImVec2 &rhs) const { return ImVec2(x - rhs.x, y - rhs.y); }
};

struct ImVec4 {
	float x;
	float y;
	float z;
	float w;
	ImVec4() : x(0), y(0), z(0), w(0) {}
	ImVec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
};

struct ImGuiIO {
	ImVec2 DisplaySize;
};

struct ImDrawList {
	void *host;

	void AddText(const ImVec2 &pos, ImU32 col, const char *text,
	             const char *text_end = nullptr) const;
	void AddRectFilled(const ImVec2 &p_min, const ImVec2 &p_max, ImU32 col,
	                   float rounding = 0.0f, int flags = 0) const;
	void AddCircleFilled(const ImVec2 &center, float radius, ImU32 col,
	                     int num_segments = 12) const;
};

struct ImGuiWindow {
	ImVec2 Size;
	ImDrawList *DrawList;
};

struct ImColor {
	ImU32 Value;
	ImColor() : Value(0) {}
	ImColor(int r, int g, int b, int a = 255) { Value = IM_COL32(r, g, b, a); }
	ImColor(const ImVec4 &col) { Value = ImGuiColorConvertFloat4ToU32(col); }
	operator ImU32() const { return Value; }

private:
	static ImU32 IM_COL32(int r, int g, int b, int a) {
		return static_cast<ImU32>((a << 24) | (b << 16) | (g << 8) | r);
	}
	static ImU32 ImGuiColorConvertFloat4ToU32(const ImVec4 &in) {
		return IM_COL32(static_cast<int>(in.x * 255.0f + 0.5f),
		                static_cast<int>(in.y * 255.0f + 0.5f),
		                static_cast<int>(in.z * 255.0f + 0.5f),
		                static_cast<int>(in.w * 255.0f + 0.5f));
	}
};

enum ImGuiCond_ { ImGuiCond_Always = 1 << 0 };
enum ImGuiWindowFlags_ {
	ImGuiWindowFlags_NoDecoration = 1 << 0,
	ImGuiWindowFlags_AlwaysAutoResize = 1 << 1,
	ImGuiWindowFlags_NoInputs = 1 << 2,
	ImGuiWindowFlags_NoNav = 1 << 3,
	ImGuiWindowFlags_NoFocusOnAppearing = 1 << 4,
};
enum ImGuiInputTextFlags_ {
	ImGuiInputTextFlags_EnterReturnsTrue = 1 << 0,
	ImGuiInputTextFlags_CallbackCompletion = 1 << 6,
	ImGuiInputTextFlags_CallbackHistory = 1 << 7,
	ImGuiInputTextFlags_ReadOnly = 1 << 14,
};
enum ImGuiTreeNodeFlags_ {
	ImGuiTreeNodeFlags_DefaultOpen = 1 << 5,
};

#ifndef IM_ARRAYSIZE
#define IM_ARRAYSIZE(_ARR) (sizeof(_ARR) / sizeof((_ARR)[0]))
#endif
enum ImGuiCol_ {
	ImGuiCol_Text = 0,
	ImGuiCol_WindowBg = 2,
};
enum ImGuiKey_ {
	ImGuiKey_UpArrow = 3,
	ImGuiKey_DownArrow = 4,
};
enum ImGuiStyleVar_ {
	ImGuiStyleVar_Alpha = 0,
	ImGuiStyleVar_WindowPadding = 1,
	ImGuiStyleVar_WindowRounding = 2,
	ImGuiStyleVar_WindowBorderSize = 3,
	ImGuiStyleVar_WindowMinSize = 4,
	ImGuiStyleVar_WindowTitleAlign = 5,
	ImGuiStyleVar_ChildRounding = 6,
	ImGuiStyleVar_ChildBorderSize = 7,
	ImGuiStyleVar_PopupRounding = 8,
	ImGuiStyleVar_PopupBorderSize = 9,
	ImGuiStyleVar_FramePadding = 10,
	ImGuiStyleVar_FrameRounding = 11,
	ImGuiStyleVar_FrameBorderSize = 12,
	ImGuiStyleVar_ItemSpacing = 13,
	ImGuiStyleVar_ItemInnerSpacing = 14,
	ImGuiStyleVar_IndentSpacing = 15,
	ImGuiStyleVar_ScrollbarSize = 16,
	ImGuiStyleVar_ScrollbarRounding = 17,
	ImGuiStyleVar_GrabMinSize = 18,
	ImGuiStyleVar_GrabRounding = 19,
	ImGuiStyleVar_TabRounding = 20,
	ImGuiStyleVar_ButtonTextAlign = 21,
	ImGuiStyleVar_SelectableTextAlign = 22,
};
enum ImGuiMouseButton_ { ImGuiMouseButton_Left = 0 };

namespace PluginUi {

inline const PluginUiApi *&GApi() {
	static const PluginUiApi *storage = nullptr;
	return storage;
}

inline void Bind(const PluginUiApi *api) { GApi() = api; }
inline bool IsBound() { return GApi() != nullptr; }

inline ImGuiIO GetIO() { return GApi() ? GApi()->GetIO() : ImGuiIO{}; }
inline float GetStyleAlpha() { return GApi() ? GApi()->GetStyleAlpha() : 1.0f; }
inline float GetTime() { return GApi() ? GApi()->GetTime() : 0.0f; }
inline float GetTextLineHeight() { return GApi() ? GApi()->GetTextLineHeight() : 0.0f; }
inline float GetFrameHeightWithSpacing() {
	return GApi() ? GApi()->GetFrameHeightWithSpacing() : 0.0f;
}
inline float GetScrollY() { return GApi() ? GApi()->GetScrollY() : 0.0f; }
inline float GetScrollMaxY() { return GApi() ? GApi()->GetScrollMaxY() : 0.0f; }
inline ImVec2 CalcTextSize(const char *text, const char *text_end = nullptr,
                           bool hide_text_after_double_hash = false,
                           float wrap_width = -1.0f) {
	return GApi() ? GApi()->CalcTextSize(text, text_end, hide_text_after_double_hash, wrap_width)
	             : ImVec2{};
}

inline void SetNextWindowPos(const ImVec2 &pos, int cond = ImGuiCond_Always) {
	if (GApi()) {
		GApi()->SetNextWindowPos(pos, cond);
	}
}
inline void SetNextWindowSize(const ImVec2 &size, int cond = ImGuiCond_Always) {
	if (GApi()) {
		GApi()->SetNextWindowSize(size, cond);
	}
}
inline void SetNextWindowBgAlpha(float alpha) {
	if (GApi()) {
		GApi()->SetNextWindowBgAlpha(alpha);
	}
}
inline void SetWindowPos(const ImVec2 &pos, int cond = ImGuiCond_Always) {
	if (GApi()) {
		GApi()->SetWindowPos(pos, cond);
	}
}
inline void SetWindowSize(const ImVec2 &size) {
	if (GApi()) {
		GApi()->SetWindowSize(size);
	}
}
inline void SetKeyboardFocusHere(int offset = 0) {
	if (GApi()) {
		GApi()->SetKeyboardFocusHere(offset);
	}
}

inline bool Begin(const char *name, bool *open = nullptr, int flags = 0) {
	return GApi() && GApi()->Begin(name, open, flags);
}
inline void End() {
	if (GApi()) {
		GApi()->End();
	}
}
inline bool BeginTabBar(const char *str_id, int flags = 0) {
	return GApi() && GApi()->BeginTabBar(str_id, flags);
}
inline void EndTabBar() {
	if (GApi()) {
		GApi()->EndTabBar();
	}
}
inline bool BeginTabItem(const char *label, bool *open = nullptr, int flags = 0) {
	return GApi() && GApi()->BeginTabItem(label, open, flags);
}
inline void EndTabItem() {
	if (GApi()) {
		GApi()->EndTabItem();
	}
}
inline bool BeginChild(const char *str_id, const ImVec2 &size = ImVec2(0, 0), bool border = false,
                       int flags = 0) {
	return GApi() && GApi()->BeginChild(str_id, size, border, flags);
}
inline void EndChild() {
	if (GApi()) {
		GApi()->EndChild();
	}
}
inline void BeginTooltip() {
	if (GApi()) {
		GApi()->BeginTooltip();
	}
}
inline void EndTooltip() {
	if (GApi()) {
		GApi()->EndTooltip();
	}
}

inline void Text(const char *fmt, ...) {
	if (!GApi()) {
		return;
	}
	char buffer[2048];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);
	GApi()->Text("%s", buffer);
}
inline void TextWrapped(const char *fmt, ...) {
	if (!GApi()) {
		return;
	}
	char buffer[2048];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);
	GApi()->TextWrapped("%s", buffer);
}
inline void TextDisabled(const char *fmt, ...) {
	if (!GApi()) {
		return;
	}
	char buffer[2048];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);
	GApi()->TextDisabled("%s", buffer);
}
inline void TextColored(const ImVec4 &col, const char *fmt, ...) {
	if (!GApi()) {
		return;
	}
	char buffer[2048];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);
	GApi()->TextColored(col, "%s", buffer);
}
inline void TextUnformatted(const char *text, const char *text_end = nullptr) {
	if (GApi()) {
		GApi()->TextUnformatted(text, text_end);
	}
}
inline void Separator() {
	if (GApi()) {
		GApi()->Separator();
	}
}
inline void Spacing() {
	if (GApi()) {
		GApi()->Spacing();
	}
}
inline void SameLine(float offset_from_start_x = 0.0f, float spacing = -1.0f) {
	if (GApi()) {
		GApi()->SameLine(offset_from_start_x, spacing);
	}
}
inline void Indent(float indent_w = 0.0f) {
	if (GApi()) {
		GApi()->Indent(indent_w);
	}
}
inline void Unindent(float indent_w = 0.0f) {
	if (GApi()) {
		GApi()->Unindent(indent_w);
	}
}
inline void SetTooltip(const char *fmt, ...) {
	if (!GApi()) {
		return;
	}
	char buffer[2048];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);
	GApi()->SetTooltip("%s", buffer);
}
inline bool InputFloat(const char *label, float *v, float step = 0.0f, float step_fast = 0.0f,
                       const char *format = "%.3f", int flags = 0) {
	return GApi() && GApi()->InputFloat(label, v, step, step_fast, format, flags);
}
inline bool TreeNode(const char *id, const char *fmt, ...) {
	if (!GApi()) {
		return false;
	}
	char buffer[2048];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);
	return GApi()->TreeNode(id, "%s", buffer);
}
inline void TreePop() {
	if (GApi()) {
		GApi()->TreePop();
	}
}

inline void PushID(const char *str_id) {
	if (GApi()) {
		GApi()->PushID_Str(str_id);
	}
}
inline void PushID(int int_id) {
	if (GApi()) {
		GApi()->PushID_Int(int_id);
	}
}
inline void PopID() {
	if (GApi()) {
		GApi()->PopID();
	}
}

inline bool Button(const char *label, const ImVec2 &size = ImVec2(0, 0)) {
	return GApi() && GApi()->Button(label, size);
}
inline bool Checkbox(const char *label, bool *v) {
	return GApi() && GApi()->Checkbox(label, v);
}
inline bool InputText(const char *label, char *buf, size_t buf_size, int flags = 0) {
	return GApi() && GApi()->InputText(label, buf, buf_size, flags);
}
inline bool InputTextCallback(const char *label, char *buf, size_t buf_size, int flags,
                              PluginUiInputTextCallbackFn callback,
                              void *user_data = nullptr) {
	return GApi() &&
	       GApi()->InputTextCallback(label, buf, buf_size, flags, callback, user_data);
}
inline bool InputTextMultiline(const char *label, char *buf, size_t buf_size,
                               const ImVec2 &size = ImVec2(0, 0), int flags = 0) {
	return GApi() && GApi()->InputTextMultiline(label, buf, buf_size, size, flags);
}
inline bool InputInt(const char *label, int *v, int step = 1, int step_fast = 100,
                     int flags = 0) {
	return GApi() && GApi()->InputInt(label, v, step, step_fast, flags);
}
inline bool SliderInt(const char *label, int *v, int v_min, int v_max, int *highlights = nullptr,
                      int num_highlights = 0, const char *format = "%d") {
	return GApi() && GApi()->SliderInt(label, v, v_min, v_max, highlights, num_highlights, format);
}
inline bool SliderFloat(const char *label, float *v, float v_min, float v_max,
                        const char *format = "%.3f", float power = 1.0f) {
	return GApi() && GApi()->SliderFloat(label, v, v_min, v_max, format, power);
}
inline bool BeginCombo(const char *label, const char *preview_value, int flags = 0) {
	return GApi() && GApi()->BeginCombo(label, preview_value, flags);
}
inline void EndCombo() {
	if (GApi()) {
		GApi()->EndCombo();
	}
}
inline bool Selectable(const char *label, bool selected = false, int flags = 0,
                       const ImVec2 &size = ImVec2(0, 0)) {
	return GApi() && GApi()->Selectable(label, selected, flags, size);
}
inline void SetItemDefaultFocus() {
	if (GApi()) {
		GApi()->SetItemDefaultFocus();
	}
}
inline bool CollapsingHeader(const char *label, int flags = 0) {
	return GApi() && GApi()->CollapsingHeader(label, flags);
}
inline bool Hotkey(const char *label, int *k, const ImVec2 &size = ImVec2(0, 0)) {
	return GApi() && GApi()->Hotkey(label, k, size);
}

inline void PushStyleColor(int idx, const ImVec4 &col) {
	if (GApi()) {
		GApi()->PushStyleColor(idx, col);
	}
}
inline void PopStyleColor(int count = 1) {
	if (GApi()) {
		GApi()->PopStyleColor(count);
	}
}
inline void PushStyleVar(int idx, float val) {
	if (GApi()) {
		GApi()->PushStyleVar(idx, val);
	}
}
inline void PushStyleVar(int idx, const ImVec2 &val) {
	if (GApi() && GApi()->PushStyleVarVec2) {
		GApi()->PushStyleVarVec2(idx, val);
	}
}
inline void PopStyleVar(int count = 1) {
	if (GApi()) {
		GApi()->PopStyleVar(count);
	}
}
inline void PushTextWrapPos(float wrap_local_pos_x = 0.0f) {
	if (GApi()) {
		GApi()->PushTextWrapPos(wrap_local_pos_x);
	}
}
inline void PopTextWrapPos() {
	if (GApi()) {
		GApi()->PopTextWrapPos();
	}
}
inline void PushItemWidth(float item_width) {
	if (GApi()) {
		GApi()->PushItemWidth(item_width);
	}
}
inline void PopItemWidth() {
	if (GApi()) {
		GApi()->PopItemWidth();
	}
}

inline bool IsItemHovered(int flags = 0) { return GApi() && GApi()->IsItemHovered(flags); }
inline bool IsItemVisible() { return GApi() && GApi()->IsItemVisible(); }
inline bool GetItemRectMin(ImVec2 *out) {
	return GApi() && GApi()->GetItemRectMin && GApi()->GetItemRectMin(out);
}
inline bool GetItemRectMax(ImVec2 *out) {
	return GApi() && GApi()->GetItemRectMax && GApi()->GetItemRectMax(out);
}
inline bool GetMousePos(ImVec2 *out) {
	return GApi() && GApi()->GetMousePos && GApi()->GetMousePos(out);
}
inline bool IsMouseReleased(int button) { return GApi() && GApi()->IsMouseReleased(button); }
inline void SetScrollHereY(float center_y_ratio = 0.5f) {
	if (GApi()) {
		GApi()->SetScrollHereY(center_y_ratio);
	}
}

inline ImGuiWindow *BeginRawScene(const char *name) {
	return GApi() ? GApi()->BeginRawScene(name) : nullptr;
}
inline void EndRawScene() {
	if (GApi()) {
		GApi()->EndRawScene();
	}
}

struct StyleProxy {
	float Alpha;
};
inline StyleProxy GetStyle() { return StyleProxy{GetStyleAlpha()}; }

} // namespace PluginUi

inline void ImDrawList::AddText(const ImVec2 &pos, ImU32 col, const char *text,
                                 const char *text_end) const {
	if (PluginUi::GApi()) {
		PluginUi::GApi()->DrawListAddText(const_cast<ImDrawList *>(this), pos, col, text,
		                                 text_end);
	}
}
inline void ImDrawList::AddRectFilled(const ImVec2 &p_min, const ImVec2 &p_max, ImU32 col,
                                      float rounding, int flags) const {
	if (PluginUi::GApi()) {
		PluginUi::GApi()->DrawListAddRectFilled(const_cast<ImDrawList *>(this), p_min, p_max, col,
		                                       rounding, flags);
	}
}
inline void ImDrawList::AddCircleFilled(const ImVec2 &center, float radius, ImU32 col,
                                        int num_segments) const {
	if (PluginUi::GApi()) {
		PluginUi::GApi()->DrawListAddCircleFilled(const_cast<ImDrawList *>(this), center, radius,
		                                         col, num_segments);
	}
}

#define ImGui PluginUi

#endif // IMGUI_VERSION
