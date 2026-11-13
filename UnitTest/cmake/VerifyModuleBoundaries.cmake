# Check resolved local includes, including relative paths, so moving a header
# cannot silently make Simulation depend on gameplay or a service on its client.
function(expect_directories path)
    file(GLOB entries RELATIVE "${path}" "${path}/*")
    set(actual "")
    foreach(entry IN LISTS entries)
        if(IS_DIRECTORY "${path}/${entry}")
            list(APPEND actual "${entry}")
        endif()
    endforeach()
    set(expected ${ARGN})
    list(SORT actual)
    list(SORT expected)
    if(NOT "${actual}" STREQUAL "${expected}")
        message(FATAL_ERROR "Unexpected module directories in ${path}: ${actual}; expected ${expected}")
    endif()
endfunction()
expect_directories("${PIXEL_SOURCE_DIR}" Dev Runtime)
expect_directories("${PIXEL_SOURCE_DIR}/Runtime" Gameplay Simulation Render System)
expect_directories("${PIXEL_SOURCE_DIR}/Dev" Debug)

file(GLOB_RECURSE sources "${PIXEL_SOURCE_DIR}/*.h" "${PIXEL_SOURCE_DIR}/*.hpp"
    "${PIXEL_SOURCE_DIR}/*.hxx" "${PIXEL_SOURCE_DIR}/*.inl"
    "${PIXEL_SOURCE_DIR}/*.c" "${PIXEL_SOURCE_DIR}/*.cc"
    "${PIXEL_SOURCE_DIR}/*.cpp" "${PIXEL_SOURCE_DIR}/*.cxx")
foreach(source IN LISTS sources)
    file(RELATIVE_PATH owner "${PIXEL_SOURCE_DIR}" "${source}")
    get_filename_component(directory "${source}" DIRECTORY)
    file(STRINGS "${source}" includes REGEX "^[ \t]*#[ \t]*include[ \t]*[\"<]")
    foreach(include IN LISTS includes)
        string(REGEX REPLACE "^[ \t]*#[ \t]*include[ \t]*([\"<])([^\">]+)[\">].*" "\\1;\\2" parts "${include}")
        list(GET parts 0 delimiter)
        list(GET parts 1 header)
        if(delimiter STREQUAL "\"" AND EXISTS "${directory}/${header}")
            get_filename_component(resolved "${directory}/${header}" ABSOLUTE)
            file(RELATIVE_PATH dependency "${PIXEL_SOURCE_DIR}" "${resolved}")
        elseif(EXISTS "${PIXEL_SOURCE_DIR}/${header}")
            get_filename_component(resolved "${PIXEL_SOURCE_DIR}/${header}" ABSOLUTE)
            file(RELATIVE_PATH dependency "${PIXEL_SOURCE_DIR}" "${resolved}")
        else()
            continue() # Standard library, language public headers or engine API.
        endif()
        # IO/Input adapts gameplay events. Other System services remain reusable
        # and must not acquire a gameplay dependency through the input adapter.
        if(owner MATCHES "^Runtime/System/" AND NOT owner MATCHES "^Runtime/System/IO/Input/"
            AND (dependency MATCHES "^(Runtime/(Gameplay|Simulation|Render)|Dev)/"
                OR dependency MATCHES "^Runtime/System/IO/Input/"))
            message(FATAL_ERROR "System depends on its client: ${owner} -> ${dependency}")
        endif()
        if(owner MATCHES "^Runtime/Simulation/"
            AND (dependency MATCHES "^Runtime/(Gameplay|Render)/" OR dependency MATCHES "^Runtime/System/IO/Input/"))
            message(FATAL_ERROR "Simulation depends on an active consumer: ${owner} -> ${dependency}")
        endif()
        if(owner MATCHES "^Runtime/Simulation/" AND dependency MATCHES "^Dev/" AND NOT dependency MATCHES "^Dev/Debug/Performance/")
            message(FATAL_ERROR "Simulation depends on a dev scenario: ${owner} -> ${dependency}")
        endif()
        if(owner MATCHES "^Runtime/Gameplay/" AND NOT owner MATCHES "^Runtime/Gameplay/Application/"
            AND (dependency MATCHES "^Runtime/Render/" OR dependency MATCHES "^Runtime/System/IO/Input/"))
            message(FATAL_ERROR "Gameplay depends on a runtime consumer: ${owner} -> ${dependency}")
        endif()
        if(owner MATCHES "^Runtime/System/IO/Input/" AND dependency MATCHES "^(Runtime/(Simulation|Render)|Dev)/")
            message(FATAL_ERROR "Input bypasses gameplay or presentation ports: ${owner} -> ${dependency}")
        endif()
    endforeach()
endforeach()
