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
