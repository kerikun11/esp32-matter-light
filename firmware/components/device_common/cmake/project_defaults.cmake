# The bundled Matter SDK uses C++17. Avoid C++20 rewritten comparison
# candidates in ClosureControl with GCC 14 without patching SDK sources.
idf_component_get_property(matter_lib espressif__esp_matter COMPONENT_LIB)
target_compile_options(${matter_lib} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>")
