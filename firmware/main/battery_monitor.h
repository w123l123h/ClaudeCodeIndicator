#pragma once

#include <functional>

using BatteryCallback = std::function<void(bool low)>;

class BatteryMonitor {
public:
    void set_callback(BatteryCallback cb);
    void start();
    float read_voltage();
};
