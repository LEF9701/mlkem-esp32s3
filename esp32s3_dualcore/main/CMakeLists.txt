idf_component_register(
    SRCS         "main.cpp"
    INCLUDE_DIRS "." "../../common"
    REQUIRES     mlkem esp_timer bootloader_support)
