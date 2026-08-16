FILE(GLOB_RECURSE main ${CMAKE_SOURCE_DIR}/main/*.c)

idf_component_register(SRCS ${main}
                        INCLUDE_DIRS "."
                        REQUIRES bsp_display lvgl esp_lvgl_port bsp_i2c bsp_stc8h1kxx bsp_uart bsp_audio bsp_mic bsp_sd bsp_rtc elecrow_ui nvs_flash spiffs esp_wifi bt esp_netif esp_http_server app_update esp_audio_codec
                        EMBED_FILES "sounds/new_tag.wav" "sounds/duplicate.wav")
                                 