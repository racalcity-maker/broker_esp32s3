set(HELIX_DIR ${CMAKE_CURRENT_LIST_DIR}/third_party/helix)
file(GLOB HELIX_SRCS "${HELIX_DIR}/*.c")

idf_component_register(
    SRCS "audio_player.c" "audio_player_decode.c" "audio_player_output.c" "audio_player_status.c" "audio_player_volume.c" "helix_mp3_wrapper.c" "helix_shim.c" ${HELIX_SRCS}
    INCLUDE_DIRS "include" ${HELIX_DIR}/utils
    PRIV_INCLUDE_DIRS ${HELIX_DIR}
    REQUIRES event_bus esp_timer driver fatfs sdmmc nvs_flash error_monitor sd_storage
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE AUDIO_HAVE_HELIX=1)
