idf_component_register(SRC_DIRS "." INCLUDE_DIRS ".")

# set_property(TARGET ${COMPONENT_LIB} PROPERTY CXX_STANDARD 17)
# target_compile_options(${COMPONENT_LIB} PUBLIC "-DCHIP_HAVE_CONFIG_H")
# target_compile_definitions(${COMPONENT_LIB} PUBLIC CHIP_HAVE_CONFIG_H=1)

target_compile_options(${COMPONENT_LIB} PRIVATE
  -fmacro-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}/=
  -ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}/=
)
