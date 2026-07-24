# Framework-free contracts shared by the engine and every input backend
# (input/) plus the framework-free logic ported from schnelle-umlaute (core/:
# mapping format, cycling order, profile merge, usage counters, config-file
# loading). input/ is header-only (an INTERFACE target publishing the include
# root); core/ carries translation units. Both share the src/ root so headers
# include as "input/..." / "core/...".
add_library(schnelle_zeichen_input INTERFACE)
target_include_directories(schnelle_zeichen_input INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

add_library(schnelle_zeichen_core STATIC core/mappings_loader.cpp)
target_include_directories(schnelle_zeichen_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
