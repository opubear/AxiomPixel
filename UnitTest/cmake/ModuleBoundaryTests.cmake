if(NOT DEFINED FIXTURE_ROOT)
    message(FATAL_ERROR "FIXTURE_ROOT is required")
endif()

function(check_boundary name owner dependency include_text diagnostic)
    set(root "${FIXTURE_ROOT}/${name}")
    file(MAKE_DIRECTORY "${root}/Dev/Debug" "${root}/Runtime/Gameplay"
        "${root}/Runtime/Simulation" "${root}/Runtime/Render" "${root}/Runtime/System")
    get_filename_component(owner_directory "${root}/${owner}" DIRECTORY)
    get_filename_component(dependency_directory "${root}/${dependency}" DIRECTORY)
    file(MAKE_DIRECTORY "${owner_directory}" "${dependency_directory}")
    file(WRITE "${root}/${dependency}" "#pragma once\n")
    file(WRITE "${root}/${owner}" "${include_text}\n")
    execute_process(COMMAND "${CMAKE_COMMAND}" "-DPIXEL_SOURCE_DIR=${root}"
        -P "${CMAKE_CURRENT_LIST_DIR}/VerifyModuleBoundaries.cmake"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(diagnostic STREQUAL "")
        if(NOT result STREQUAL "0")
            message(FATAL_ERROR "${name}: permitted dependency rejected: ${output}${error}")
        endif()
    elseif(result STREQUAL "0" OR NOT error MATCHES "${diagnostic}")
        message(FATAL_ERROR "${name}: expected ${diagnostic}, got ${result}: ${output}${error}")
    endif()
endfunction()

foreach(style IN ITEMS quoted angle)
    if(style STREQUAL "quoted")
        set(open "\"")
        set(close "\"")
    else()
        set(open "<")
        set(close ">")
    endif()
    foreach(extension IN ITEMS h hpp hxx inl c cc cpp cxx)
        check_boundary("${style}_${extension}" "Runtime/Gameplay/Canary.${extension}" "Runtime/Render/Canary.h"
            "# include${open}Runtime/Render/Canary.h${close}" "Gameplay depends on a runtime consumer")
    endforeach()
    check_boundary("${style}_gameplay_simulation" "Runtime/Gameplay/Canary.cpp" "Runtime/Simulation/Canary.h"
        "#include ${open}Runtime/Simulation/Canary.h${close}" "")
    check_boundary("${style}_application" "Runtime/Gameplay/Application/Canary.cpp" "Runtime/Render/Canary.h"
        "#include ${open}Runtime/Render/Canary.h${close}" "")
    check_boundary("${style}_simulation_gameplay" "Runtime/Simulation/Canary.cpp" "Runtime/Gameplay/Canary.h"
        "#include ${open}Runtime/Gameplay/Canary.h${close}" "Simulation depends on an active consumer")
    check_boundary("${style}_input_gameplay" "Runtime/System/IO/Input/Canary.cpp" "Runtime/Gameplay/Canary.h"
        "#include ${open}Runtime/Gameplay/Canary.h${close}" "")
    check_boundary("${style}_input_render" "Runtime/System/IO/Input/Canary.cpp" "Runtime/Render/Canary.h"
        "#include ${open}Runtime/Render/Canary.h${close}" "Input bypasses gameplay or presentation ports")
    check_boundary("${style}_system_input" "Runtime/System/Service/Canary.cpp" "Runtime/System/IO/Input/Canary.h"
        "#include ${open}Runtime/System/IO/Input/Canary.h${close}" "System depends on its client")
    check_boundary("${style}_normalized" "Runtime/Gameplay/Canary.cpp" "Runtime/Render/Canary.h"
        "#include ${open}Runtime/Gameplay/../Render/Canary.h${close}" "Gameplay depends on a runtime consumer")
endforeach()
check_boundary(relative "Runtime/Gameplay/Canary.cpp" "Runtime/Render/Canary.h"
    "#include \"../Render/Canary.h\"" "Gameplay depends on a runtime consumer")
message(STATUS "Module boundary positive/negative fixtures passed")
