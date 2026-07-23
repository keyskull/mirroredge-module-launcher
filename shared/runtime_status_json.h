#pragma once

#include "engine_module_client.h"

#include <string>

inline void AppendJsonEngineStatus(std::string &out) {
	std::string engineJson;
	if (EngineModuleClient::TryFormatStatusJson(engineJson)) {
		out += ",\"engine\":";
		out += engineJson;
	} else {
		out += ",\"engine\":null";
	}
}
