# Module System — isolated per-module build discovery.
# See docs/module-system/05-phase3-build-discovery-design.md for the full design.
#
# Invoked from the top-level CMakeLists.txt as a custom target added to ALL. Every failure path
# below uses message(WARNING ...), never message(FATAL_ERROR) -- a broken module must never fail
# the overall core build (docs/module-system/05 §2-3). Failed/skipped modules simply never appear
# in MODULES_RUNTIME_DIR, so ModuleMgr::LoadModules() (docs/module-system/04) never sees them.
#
# Expected variables (passed via -D on the invoking cmake -P call):
#   MODULES_SOURCE_DIR  - absolute path to this repo's modules/ directory
#   MODULES_BUILD_DIR   - absolute path to a build-tree location for each module's own build dir
#   MODULES_RUNTIME_DIR - absolute path matching the core's Modules.Dir config key

if(NOT DEFINED MODULES_SOURCE_DIR)
    message(WARNING "[Modules] MODULES_SOURCE_DIR not set, skipping module build discovery")
    return()
endif()

if(NOT EXISTS "${MODULES_SOURCE_DIR}")
    message(STATUS "[Modules] ${MODULES_SOURCE_DIR} does not exist, nothing to build")
    return()
endif()

file(GLOB module_candidates LIST_DIRECTORIES true "${MODULES_SOURCE_DIR}/*")

foreach(candidate ${module_candidates})
    if(NOT IS_DIRECTORY "${candidate}")
        continue()
    endif()

    get_filename_component(module_dir_name "${candidate}" NAME)
    set(manifest_path "${candidate}/module.json")
    set(cmakelists_path "${candidate}/CMakeLists.txt")

    if(NOT EXISTS "${manifest_path}")
        message(WARNING "[Modules] ${module_dir_name}: missing module.json, skipping")
        continue()
    endif()

    if(NOT EXISTS "${cmakelists_path}")
        message(WARNING "[Modules] ${module_dir_name}: missing CMakeLists.txt, skipping")
        continue()
    endif()

    # TODO: parse module.json's "name" field (CMake has no native JSON parser pre-3.19's
    # string(JSON ...) -- use that once a minimum CMake version is pinned) and verify it exactly
    # matches module_dir_name per docs/module-system/05 §1. Skipping this check in the skeleton.

    set(module_build_dir "${MODULES_BUILD_DIR}/${module_dir_name}")

    message(STATUS "[Modules] Configuring ${module_dir_name}...")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -S "${candidate}" -B "${module_build_dir}"
        RESULT_VARIABLE configure_result
        OUTPUT_QUIET
        ERROR_VARIABLE configure_error
    )

    if(NOT configure_result EQUAL 0)
        message(WARNING "[Modules] ${module_dir_name}: configure failed, skipping\n${configure_error}")
        continue()
    endif()

    message(STATUS "[Modules] Building ${module_dir_name}...")
    execute_process(
        COMMAND ${CMAKE_COMMAND} --build "${module_build_dir}"
        RESULT_VARIABLE build_result
        OUTPUT_QUIET
        ERROR_VARIABLE build_error
    )

    if(NOT build_result EQUAL 0)
        message(WARNING "[Modules] ${module_dir_name}: build failed, skipping\n${build_error}")
        continue()
    endif()

    # TODO: locate the module's built executable (name/location depend on the module's own
    # CMakeLists.txt) and copy it + module.json into ${MODULES_RUNTIME_DIR}/${module_dir_name}/.
    # Skeleton stops here -- no module exists yet to validate this step against.
    message(STATUS "[Modules] ${module_dir_name}: built OK (runtime copy step not yet implemented)")
endforeach()
