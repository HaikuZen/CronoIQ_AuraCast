set(srcs
    "voice.c"
    "voice_codec.c"
    "voice_enroll.c"
    "voice_wakeword.c"
)

if(CONFIG_VOICE_WAKENET_ENABLE)
    list(APPEND srcs "voice_wakenet.c")
endif()

set(priv_reqs codec_board espressif__esp_codec_dev esp_timer)
if(CONFIG_VOICE_WAKENET_ENABLE)
    list(APPEND priv_reqs espressif__esp-sr)
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES ${priv_reqs}
)
