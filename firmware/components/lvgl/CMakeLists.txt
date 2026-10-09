file(GLOB_RECURSE SRCS "src/*.c")
idf_component_register(SRCS ${SRCS} INCLUDE_DIRS "." "src" REQUIRES esp_timer)
target_compile_definitions(${COMPONENT_LIB} PUBLIC LV_CONF_INCLUDE_SIMPLE)
