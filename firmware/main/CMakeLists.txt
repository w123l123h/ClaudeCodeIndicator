idf_component_register(
    SRCS "Application.cpp" "application.cpp" "main.cpp" "led_controller.cpp" "led_state_manager.cpp" "ble_server.cpp" "battery_monitor.cpp" "power_manager.cpp"
    INCLUDE_DIRS "."
    REQUIRES led_strip nvs_flash bt esp_adc json driver
)
