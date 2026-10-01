# Tests : cibles CMake + CTest. Chaque test a son propre main() et se lie a
# browsereco_core (plus de linkage manuel des .o par run_tests.sh).
#
# Chaque test tourne dans SON PROPRE dossier vide (WORKING_DIRECTORY +
# BROWSERECO_DATA_DIR), recree avant chaque execution de la suite : un test ne
# doit jamais voir un settings.txt / cache.db / history.db reel, ni les reglages
# laisses par un autre test ou par une execution precedente.

set(_unit_tests
    test_adblock
    test_images
    test_searchengine
    test_settings
    test_history
    test_homepage
    test_icons
    test_eco_stats
)
set(_gui_tests
    serp_test
    freeze_test
    ui_test
)
set(_all_tests ${_unit_tests} ${_gui_tests})

set(_dirs)
foreach(t ${_all_tests})
    list(APPEND _dirs "${CMAKE_CURRENT_BINARY_DIR}/run/${t}")
endforeach()
list(JOIN _dirs ";" _dirs_joined)

# Fixture CTest : les dossiers sont vides et frais pour tous les tests.
add_test(NAME clean_test_dirs
         COMMAND ${CMAKE_COMMAND} "-DTESTDIRS=${_dirs_joined}"
                 -P "${CMAKE_CURRENT_SOURCE_DIR}/prepare_dirs.cmake")
set_tests_properties(clean_test_dirs PROPERTIES FIXTURES_SETUP clean)

function(browsereco_add_test name)
    cmake_parse_arguments(ARG "" "TIMEOUT" "" ${ARGN})
    qt_add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE browsereco_core Qt6::Test)
    target_compile_definitions(${name} PRIVATE
        BROWSERECO_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    if(MSVC)
        target_compile_options(${name} PRIVATE /W4)
    else()
        target_compile_options(${name} PRIVATE -Wall -Wextra)
    endif()
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        TIMEOUT "${ARG_TIMEOUT}"
        FIXTURES_REQUIRED clean
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/run/${name}"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_CURRENT_BINARY_DIR}/run/xdg;BROWSERECO_SHOTS=${CMAKE_CURRENT_BINARY_DIR}/run/${name}/shots;BROWSERECO_DATA_DIR=${CMAKE_CURRENT_BINARY_DIR}/run/${name}"
    )
endfunction()

# Tests purs (pas de WebEngine) : timeout court
foreach(t ${_unit_tests})
    browsereco_add_test(${t} TIMEOUT 120)
endforeach()

# Tests qui instancient QWebEngineView : demarrage de Chromium, marge plus large
foreach(t ${_gui_tests})
    browsereco_add_test(${t} TIMEOUT 300)
endforeach()