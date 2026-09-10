# Vendored from espressif/codec_board 0.5.5 (see CHANGELOG.md), trimmed for this
# project: lcd_init.c dropped (uses APIs removed in IDF 6; the board LCD is
# handled by components/board/display_st7305.c instead).
set(reqs driver esp_codec_dev fatfs)

if("${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}" VERSION_GREATER_EQUAL "5.3")
    list(APPEND reqs esp_driver_i2c esp_driver_i2s esp_driver_gpio)
endif()

idf_component_register(
    SRCS
        "cfg_parse.c"
        "codec_board.c"
        "codec_init.c"
        "dummy_codec.c"
        "drv/tca9554.c"
    INCLUDE_DIRS "./include"
    PRIV_INCLUDE_DIRS "drv"
    REQUIRES ${reqs}
    EMBED_TXTFILES "board_cfg.txt")
