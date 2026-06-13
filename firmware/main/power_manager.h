#pragma once

#include "esp_pm.h"

class PowerManager {
public:
    void start();
    void enable();       // Release PM lock → allow light sleep (BLE connected)
    void disable();      // Acquire PM lock → prevent light sleep (no connection)
    void on_activity();  // No-op (BLE controller handles wakeup automatically)

private:
    esp_pm_lock_handle_t m_pm_lock = nullptr;
};
