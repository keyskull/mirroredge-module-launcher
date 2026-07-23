#pragma once

#include "feature_plugin_host.h"

namespace Menu {
inline void AddTab(const char *name, FeatureMenuTabCallback callback) {
	FeaturePluginHost::AddTab(name, callback);
}
inline void RemoveTab(const char *name) { FeaturePluginHost::RemoveTab(name); }
inline void Hide() { FeaturePluginHost::HideMenu(); }
} // namespace Menu

namespace ModHost {
inline bool IsAttached() { return FeaturePluginHost::IsAttached(); }
} // namespace ModHost
