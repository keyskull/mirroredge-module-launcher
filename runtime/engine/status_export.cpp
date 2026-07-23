#include "engine.h"
#include "engine_api.h"

#include "me_sdk/runtime/init.h"
#include "me_sdk/runtime/sdk_errors.h"
#include "runtime_version.h"
#include "version.h"

#include <string>

namespace {

std::string JsonEscape(const char *text) {
	if (!text) {
		return {};
	}

	std::string out;
	out.reserve(strlen(text) + 8);
	for (const unsigned char *p = reinterpret_cast<const unsigned char *>(text); *p;
	     ++p) {
		const unsigned char c = *p;
		if (c == '"' || c == '\\') {
			out.push_back('\\');
		}
		if (c == '"' || c == '\\' || c >= 0x20) {
			out.push_back(static_cast<char>(c));
		}
	}
	return out;
}

void AppendSdkStatusJson(std::string &out) {
	const auto err = MeSdk::GetLastSdkError();
	const auto &st = MeSdk::GetLastRuntimeStatus();
	out += ",\"sdkError\":";
	out += std::to_string(static_cast<uint32_t>(err));
	out += ",\"sdkErrorName\":\"";
	out += JsonEscape(MeSdk::SdkErrorName(err));
	out += "\"";
	out += ",\"sdkImageSize\":";
	out += std::to_string(st.imageSize);
	out += ",\"sdkGNamesCount\":";
	out += std::to_string(st.gnamesCount);
	out += ",\"sdkGObjectsCount\":";
	out += std::to_string(st.gobjectsCount);
}

} // namespace

extern "C" __declspec(dllexport) int __cdecl MMOD_EngineFormatStatusJson(char *out,
                                                                         int outChars) {
	std::string json = "{\"component\":\"engine\"";
	json += ",\"version\":\"";
	json += MMOD_ENGINE_VERSION_STRING;
	json += "\"";
	json += ",\"modReady\":";
	json += Engine::IsModReady() ? "true" : "false";
	json += ",\"initializing\":";
	json += Engine::IsInitializing() ? "true" : "false";
	json += ",\"gameReady\":";
	json += Engine::IsGameReadyForModInit() ? "true" : "false";
	AppendSdkStatusJson(json);
	json += ",\"presentationHooks\":";
	json += Engine::ArePresentationHooksInstalled() ? "true" : "false";
	json += ",\"gameplayHooks\":";
	json += Engine::AreGameplayHooksInstalled() ? "true" : "false";
	json += ",\"proxyActive\":";
	json += Engine::IsModD3D9ProxyActive() ? "true" : "false";
	json += ",\"hostedMode\":";
	json += Engine::IsHostedMode() ? "true" : "false";
	json += ",\"hostedGameplayLive\":";
	json += Engine::IsHostedGameplayLive() ? "true" : "false";
	json += "}";

	if (!out || outChars <= 0) {
		return static_cast<int>(json.size()) + 1;
	}

	strncpy(out, json.c_str(), outChars - 1);
	out[outChars - 1] = '\0';
	return static_cast<int>(json.size());
}
