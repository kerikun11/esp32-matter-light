include_guard(GLOBAL)
function(device_common_web_assets target source)
  set(common_dir "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/..")
  set(header "${CMAKE_CURRENT_BINARY_DIR}/generated/web_assets.h")
  get_filename_component(web_dir "${source}" DIRECTORY)
  file(GLOB_RECURSE web_inputs CONFIGURE_DEPENDS "${web_dir}/*.css" "${web_dir}/*.js")
  file(GLOB_RECURSE shared_inputs CONFIGURE_DEPENDS "${common_dir}/web/*.css" "${common_dir}/web/*.js")
  add_custom_command(OUTPUT "${header}"
    COMMAND ${PYTHON} "${common_dir}/tools/web/build_web.py" "${source}" "${header}"
    DEPENDS "${source}" ${web_inputs} ${shared_inputs}
            "${common_dir}/tools/web/build_web.py" "${common_dir}/tools/web/requirements.txt"
    VERBATIM)
  add_custom_target(${target}_web_assets DEPENDS "${header}")
  add_dependencies(${target} ${target}_web_assets)
  target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
endfunction()
