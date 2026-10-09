file(GLOB APP_CPP "../src/*.cpp")
file(GLOB APP_C "../src/*.c")
idf_component_register(SRCS ${APP_CPP} ${APP_C} INCLUDE_DIRS "../include")
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
