cmake_minimum_required(VERSION 3.16)
project(BrowserECO VERSION 1.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Un build sans -DCMAKE_BUILD_TYPE ne serait ni optimise ni sans assertions Qt :
# on force Release pour les generateurs a configuration unique (Makefile, Ninja).
get_property(_multiConfig GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT _multiConfig AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Type de build" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

option(BROWSERECO_BUILD_TESTS "Compiler les tests" ON)
option(BROWSERECO_WERROR "Traiter les avertissements du compilateur comme des erreurs" OFF)

# Installation portable : tout dans le prefix (dossier ext4), jamais dans /usr
# ni sur une partition NTFS. CMAKE_INSTALL_PREFIX reste libre.

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets WebEngineWidgets Sql Network Gui)
if(BROWSERECO_BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
endif()

# Version unique : generee ici, consommee par main.cpp (applicationVersion,
# fenetre "A propos") et par le User-Agent. Plus de "1.0.0" vs "1.1" vs "1.1.0".
set(BROWSERECO_VERSION "${PROJECT_VERSION}")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/browsereco_version.h"
    @ONLY
)

# Une seule bibliotheque pour l'application et les tests (chaque test a son
# propre main) : pas de linkage manuel des .o comme avant.
set(CORE_SOURCES
    src/mainwindow.cpp
    src/mainwindow.h
    src/mainwindow.ui
    src/browserpage.cpp
    src/browserpage.h
    src/ecointerceptor.cpp
    src/ecointerceptor.h
    src/services/adblocker.cpp
    src/services/adblocker.h
    src/services/cachemanager.cpp
    src/services/cachemanager.h
    src/services/historymanager.cpp
    src/services/historymanager.h
    src/services/imageoptimizer.cpp
    src/services/imageoptimizer.h
    src/services/searchengine.cpp
    src/services/searchengine.h
    src/services/serpguard.cpp
    src/services/serpguard.h
    src/ui/omnibox.cpp
    src/ui/omnibox.h
    src/ui/ecopanel.cpp
    src/ui/ecopanel.h
    src/ui/icons.cpp
    src/ui/icons.h
)

qt_add_library(browsereco_core STATIC ${CORE_SOURCES})

target_include_directories(browsereco_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

target_link_libraries(browsereco_core PUBLIC
    Qt6::Widgets
    Qt6::WebEngineWidgets
    Qt6::Sql
    Qt6::Network
    Qt6::Gui
)

# Pour que Qt Creator trouve l'UI
set_target_properties(browsereco_core PROPERTIES
    AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/src"
    POSITION_INDEPENDENT_CODE ON
)

if(MSVC)
    target_compile_options(browsereco_core PRIVATE /W4 $<$<BOOL:${BROWSERECO_WERROR}>:/WX>)
else()
    target_compile_options(browsereco_core PRIVATE
        -Wall -Wextra -Wpedantic
        $<$<BOOL:${BROWSERECO_WERROR}>:-Werror>
    )
endif()

if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    set_property(TARGET browsereco_core PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()

qt_add_executable(BrowserECO src/main.cpp)
target_link_libraries(BrowserECO PRIVATE browsereco_core)
set_property(TARGET BrowserECO PROPERTY
    AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/src")

if(BROWSERECO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ------------------------------------------------------------------ packaging
# Paquet .deb : cpack --config build/CPackConfig.cmake -G DEB
set(CPACK_PACKAGE_NAME "browser-eco")
set(CPACK_PACKAGE_VENDOR "BrowserECO")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Navigateur Qt6 econome en donnees : bloque pubs et traqueurs")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/browsereco/browsereco-linux")
set(CPACK_PACKAGE_CONTACT "BrowserECO <maintainer@example.invalid>")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "BrowserECO <maintainer@example.invalid>")
set(CPACK_DEBIAN_PACKAGE_SECTION "web")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENCE")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "BrowserECO")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "libqt6core6t64 | libqt6core6, libqt6gui6, libqt6widgets6, libqt6webenginecore6, libqt6webenginewidgets6, libqt6network6, libqt6sql6, libqt6test6")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_GENERATOR "DEB;TGZ")
include(CPack)

# --------------------------------------------------------------- installation
# Portable : le binaire vit a cote de Filters/ et settings.txt (dossier du
# prefixe). Dans un paquet .deb la convention impose bin/ : c'est
# BROWSERECO_DEB_LAYOUT (active par la CI) qui decide, pas la presence de CPack
# -- sinon une simple installation portable se retrouverait avec un binaire
# dans bin/ et ne trouverait plus ses filtres.
option(BROWSERECO_DEB_LAYOUT "Binaire dans bin/ et non a la racine (paquet .deb)" OFF)
if(BROWSERECO_DEB_LAYOUT)
    set(BROWSERECO_BIN_DESTINATION "bin")
else()
    set(BROWSERECO_BIN_DESTINATION ".")
endif()
install(TARGETS BrowserECO DESTINATION "${BROWSERECO_BIN_DESTINATION}")

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Filters")
    install(DIRECTORY Filters DESTINATION .)
endif()

# Nom de l'icone resolu a l'installation (le fichier est a la racine du depot,
# pas dans resources/ comme avant -> l'installShipping silencieusement sans icone).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/app_icon_512.png" AND NOT BROWSERECO_DEB_LAYOUT)
    install(FILES app_icon_512.png DESTINATION .)
endif()

# .desktop : Exec= et Icon= prennent le prefix d'installation au lieu de chemins
# absolus d'une machine donnee, + MimeType pour pouvoir devenir navigateur
# par defaut.
# Chemins RELATIFS au prefixe choisi a l'installation (et non CMAKE_INSTALL_FULL_*,
# fige a la configuration : « cmake --install --prefix X » aurait produit un
# .desktop pointant ailleurs que le binaire reellement installe).
set(BROWSERECO_DESKTOP_EXEC "${CMAKE_INSTALL_PREFIX}/${BROWSERECO_BIN_DESTINATION}/BrowserECO")
# Valeurs figees a la configuration : install(CODE) ne voit pas
# CMAKE_INSTALL_DATADIR. On impose un chemin RELATIF au prefixe : certaines
# distributions le definissent en absolu (/usr/share), ce qui ferait ecrire
# l'entree de menu hors du dossier d'installation (et interdire le paquetage).
if(IS_ABSOLUTE "${CMAKE_INSTALL_DATADIR}")
    set(BROWSERECO_DATADIR "share")
else()
    set(BROWSERECO_DATADIR "${CMAKE_INSTALL_DATADIR}")
endif()
set(BROWSERECO_DESKTOP_ICON "${CMAKE_INSTALL_PREFIX}/${BROWSERECO_DATADIR}/icons/hicolor/512x512/apps/browser-eco.png")
# Copie a l.configuration (prefixe de la configuration) : sert a la CI, qui la
# valide avec desktop-file-validate sans installer.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/BrowserECO.desktop.in" _desktop_txt)
string(REPLACE "%%EXEC%%" "${BROWSERECO_DESKTOP_EXEC}" _desktop_txt "${_desktop_txt}")
string(REPLACE "%%ICON%%" "${BROWSERECO_DESKTOP_ICON}" _desktop_txt "${_desktop_txt}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/BrowserECO.desktop" "${_desktop_txt}")

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/app_icon_512.png")
    install(FILES app_icon_512.png
            DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/512x512/apps"
            RENAME browser-eco.png)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/packaging/browser-eco.appdata.xml")
    install(FILES packaging/browser-eco.appdata.xml
            DESTINATION "${CMAKE_INSTALL_DATADIR}/metainfo")
endif()

# Le .desktop definitif est ecrit a l'installation, avec le prefixe REELLEMENT
# utilise (« --prefix X » est pris en compte).
set(BROWSERECO_DESKTOP_TEMPLATE "${CMAKE_CURRENT_SOURCE_DIR}/BrowserECO.desktop.in")
set(BROWSERECO_DESKTOP_BINDIR "${BROWSERECO_BIN_DESTINATION}")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/install_desktop.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/install_desktop.cmake"
    @ONLY
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/BrowserECO.desktop"
        DESTINATION "${BROWSERECO_DATADIR}/applications")
# Sous CPack, le prefixe est « / » et non inscriptible : la copie ci-dessus
# (generee a la configuration avec le prefixe du paquet) fait foi. Sinon
# (« cmake --install --prefix X ») on reecrit l'entree avec le prefixe REEL.
install(CODE "execute_process(COMMAND \"\${CMAKE_COMMAND}\" \"-DPREFIX=\${CMAKE_INSTALL_PREFIX}\" \"-DDATADIR=${BROWSERECO_DATADIR}\" \"-DBINDIR=${BROWSERECO_BIN_DESTINATION}\" -P \"${CMAKE_CURRENT_BINARY_DIR}/install_desktop.cmake\")")

message(STATUS "BrowserECO ${PROJECT_VERSION} - Qt ${Qt6_VERSION} - build ${CMAKE_BUILD_TYPE}")
message(STATUS "Build: cmake -B build && cmake --build build -j$(nproc)")
message(STATUS "Tests: ctest --test-dir build --output-on-failure")
