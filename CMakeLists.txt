set(SOURCES
    "src/sqlite3.c"
    "src/esp32.c"
    "src/shox96_0_2.c"    
)

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "private_include"
)

set_source_files_properties(
    "src/sqlite3.c"
    PROPERTIES COMPILE_FLAGS "-w -Os"
)

## target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu99 -g3 -fno-stack-protector -ffunction-sections -fdata-sections -fstrict-volatile-bitfields -mlongcalls -nostdlib -Wpointer-arith -Wno-error=unused-value -Wno-error=unused-label -Wno-error=unused-function -Wno-error=unused-but-set-variable -Wno-error=unused-variable -Wno-error=deprecated-declarations -Wno-error=char-subscripts -Wno-error=maybe-uninitialized -Wno-unused-parameter -Wno-sign-compare -Wno-old-style-declaration -MMD -c -DF_CPU=240000000L -DESP32 -DCORE_DEBUG_LEVEL=0 -DNDEBUG)
