idf_component_register(
    SRCS
        lwmalloc.c
        main.cpp
    INCLUDE_DIRS "."
    REQUIRES
        gui
        sensors
        settings
        bsp_extra
        esp_event
        audio_alert
        ble_mouse_hid
)
