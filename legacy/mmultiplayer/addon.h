#pragma once

#include <string>

class Addon {
  public:
    virtual ~Addon() = default;

    virtual std::string GetId() const = 0;
    virtual std::string GetName() const = 0;
    virtual std::string GetDescription() const = 0;
    virtual bool IsEnabledByDefault() { return false; }

    virtual bool Enable() = 0;
    virtual void Disable() = 0;

    bool IsEnabled() const { return enabled_; }

  protected:
    bool enabled_ = false;
};
