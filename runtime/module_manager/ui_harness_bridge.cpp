#include <cstring>

#include "ui_harness.h"

extern "C" __declspec(dllexport) void __stdcall MmHarnessBeginFrame() {
	HarnessUi::BeginFrame();
}

extern "C" __declspec(dllexport) void __stdcall MmHarnessRecordRect(const char *id,
                                                                    float minX,
                                                                    float minY,
                                                                    float maxX,
                                                                    float maxY) {
	HarnessUi::RecordRect(id, minX, minY, maxX, maxY);
}

extern "C" __declspec(dllexport) int __stdcall MmHarnessFormatJson(char *out, int outChars) {
	std::string json;
	HarnessUi::FormatJson(json);
	if (!out || outChars <= 0) {
		return static_cast<int>(json.size()) + 1;
	}
	strncpy(out, json.c_str(), outChars - 1);
	out[outChars - 1] = '\0';
	return static_cast<int>(json.size());
}
