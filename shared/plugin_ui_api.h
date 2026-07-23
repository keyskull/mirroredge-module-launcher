#pragma once

#include <cstddef>

struct ImDrawList;
struct ImGuiIO;
struct ImGuiWindow;
struct ImVec2;
struct ImVec4;
using ImU32 = unsigned int;

struct PluginUiInputTextCallbackData {
	unsigned eventFlag;
	int eventKey;
	char *buf;
	int bufTextLen;
	int bufSize;
	void (*deleteChars)(PluginUiInputTextCallbackData *data, int pos, int bytes_count);
	void (*insertChars)(PluginUiInputTextCallbackData *data, int pos, const char *text);
	void *userData;
};

typedef int (*PluginUiInputTextCallbackFn)(PluginUiInputTextCallbackData *data);

struct PluginUiApi {
	unsigned version;

	ImGuiIO (*GetIO)();
	float (*GetStyleAlpha)();
	float (*GetTime)();
	float (*GetTextLineHeight)();
	float (*GetFrameHeightWithSpacing)();
	float (*GetScrollY)();
	float (*GetScrollMaxY)();
	ImVec2 (*CalcTextSize)(const char *text, const char *text_end, bool hide_text_after_double_hash,
	                       float wrap_width);

	void (*SetNextWindowPos)(const ImVec2 &pos, int cond);
	void (*SetNextWindowSize)(const ImVec2 &size, int cond);
	void (*SetNextWindowBgAlpha)(float alpha);
	void (*SetWindowPos)(const ImVec2 &pos, int cond);
	void (*SetWindowSize)(const ImVec2 &size);
	void (*SetKeyboardFocusHere)(int offset);

	bool (*Begin)(const char *name, bool *open, int flags);
	void (*End)();
	bool (*BeginTabBar)(const char *str_id, int flags);
	void (*EndTabBar)();
	bool (*BeginTabItem)(const char *label, bool *open, int flags);
	void (*EndTabItem)();
	bool (*BeginChild)(const char *str_id, const ImVec2 &size, bool border, int flags);
	void (*EndChild)();
	void (*BeginTooltip)();
	void (*EndTooltip)();

	void (*Text)(const char *fmt, ...);
	void (*TextWrapped)(const char *fmt, ...);
	void (*TextDisabled)(const char *fmt, ...);
	void (*TextColored)(const ImVec4 &col, const char *fmt, ...);
	void (*TextUnformatted)(const char *text, const char *text_end);
	void (*Separator)();
	void (*Spacing)();
	void (*SameLine)(float offset_from_start_x, float spacing);
	void (*Indent)(float indent_w);
	void (*Unindent)(float indent_w);
	void (*SetTooltip)(const char *fmt, ...);
	bool (*InputFloat)(const char *label, float *v, float step, float step_fast, const char *format,
	                   int flags);
	bool (*TreeNode)(const char *id, const char *fmt, ...);
	void (*TreePop)();

	void (*PushID_Str)(const char *str_id);
	void (*PushID_Int)(int int_id);
	void (*PopID)();

	bool (*Button)(const char *label, const ImVec2 &size);
	bool (*Checkbox)(const char *label, bool *v);
	bool (*InputText)(const char *label, char *buf, size_t buf_size, int flags);
	bool (*InputTextCallback)(const char *label, char *buf, size_t buf_size, int flags,
	                          PluginUiInputTextCallbackFn callback, void *user_data);
	bool (*InputTextMultiline)(const char *label, char *buf, size_t buf_size, const ImVec2 &size,
	                           int flags);
	bool (*InputInt)(const char *label, int *v, int step, int step_fast, int flags);
	bool (*SliderInt)(const char *label, int *v, int v_min, int v_max, int *highlights,
	                  int num_highlights, const char *format);
	bool (*SliderFloat)(const char *label, float *v, float v_min, float v_max, const char *format,
	                    float power);
	bool (*BeginCombo)(const char *label, const char *preview_value, int flags);
	void (*EndCombo)();
	bool (*Selectable)(const char *label, bool selected, int flags, const ImVec2 &size);
	void (*SetItemDefaultFocus)();
	bool (*CollapsingHeader)(const char *label, int flags);
	bool (*Hotkey)(const char *label, int *k, const ImVec2 &size);

	void (*PushStyleColor)(int idx, const ImVec4 &col);
	void (*PopStyleColor)(int count);
	void (*PushStyleVar)(int idx, float val);
	void (*PushStyleVarVec2)(int idx, const ImVec2 &val);
	void (*PopStyleVar)(int count);
	void (*PushTextWrapPos)(float wrap_local_pos_x);
	void (*PopTextWrapPos)();
	void (*PushItemWidth)(float item_width);
	void (*PopItemWidth)();

	bool (*IsItemHovered)(int flags);
	bool (*IsItemVisible)();
	bool (*GetItemRectMin)(ImVec2 *out);
	bool (*GetItemRectMax)(ImVec2 *out);
	bool (*GetMousePos)(ImVec2 *out);
	bool (*IsMouseReleased)(int button);
	void (*SetScrollHereY)(float center_y_ratio);

	ImGuiWindow *(*BeginRawScene)(const char *name);
	void (*EndRawScene)();

	void (*DrawListAddText)(ImDrawList *draw_list, const ImVec2 &pos, ImU32 col, const char *text,
	                        const char *text_end);
	void (*DrawListAddRectFilled)(ImDrawList *draw_list, const ImVec2 &p_min, const ImVec2 &p_max,
	                              ImU32 col, float rounding, int flags);
	void (*DrawListAddCircleFilled)(ImDrawList *draw_list, const ImVec2 &center, float radius,
	                                ImU32 col, int num_segments);
};

#define MMOD_PLUGIN_UI_API_VERSION 1U
