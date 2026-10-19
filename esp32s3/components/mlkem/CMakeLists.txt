# ESP-IDF component that compiles mlkem-native as ONE C file.
# The library itself lives in ../../../mlkem-native (a git clone).

# ---- Experiment knob: 512, 768 or 1024 ----
set(MLKEM_LEVEL 768)

set(MLKEM_DIR "${COMPONENT_DIR}/../../../mlkem-native/mlkem")

idf_component_register(
    SRCS         "${MLKEM_DIR}/mlkem_native.c"
    INCLUDE_DIRS "${MLKEM_DIR}")

# PUBLIC: every component that uses this one (our main.cpp) sees the same
# settings, so the header and the library always agree.
target_compile_definitions(${COMPONENT_LIB} PUBLIC
    MLK_CONFIG_PARAMETER_SET=${MLKEM_LEVEL}
    MLK_CONFIG_NAMESPACE_PREFIX=mlkem)
