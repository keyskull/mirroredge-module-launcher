#include "stdafx.h"

#include "config.h"

const LauncherConfig &LauncherConfig::Get() {
	static const LauncherConfig config;
	return config;
}
