cmake_minimum_required(VERSION 3.16)

project(deb-ostree
    VERSION 0.0.1
    DESCRIPTION "rpm-ostree dla systemow Debian/apt + OCI/bootc (immutable image-based)"
    HOMEPAGE_URL "https://github.com/TwojaOrg/deb-ostree"
    LANGUAGES CXX)

# ── Standard i flagi ────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

add_compile_options(
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter   # GLib callbacks czesto maja nieuzywane parametry
)

# ── Zaleznosci systemowe (pkg-config) ───────────────────────────────────────
find_package(PkgConfig REQUIRED)
find_package(Threads   REQUIRED)

# libostree udostepnia "ostree-1.pc" (pakiet: libostree-dev)
pkg_check_modules(OSTREE  REQUIRED ostree-1)
pkg_check_modules(GLIB    REQUIRED glib-2.0)
pkg_check_modules(GOBJECT REQUIRED gobject-2.0)
pkg_check_modules(GIO     REQUIRED gio-2.0)

# ── Biblioteka core (statyczna) ──────────────────────────────────────────────
#
# Struktura plikow:
#   cmd/   -- pliki .h  (headery, typy, interfejsy modułów)
#   src/   -- pliki .cpp (implementacje + pliki podkomend cmd_*.cpp)
#
add_library(deb_ostree_core STATIC
    src/logging.cpp
    src/process.cpp
    src/state_store.cpp
    src/overlay_manager.cpp
    src/deb_layer.cpp
    src/oci_puller.cpp
    src/ostree_repo.cpp
    src/sysroot.cpp
    src/hk_parser.cpp
)

# Headery z cmd/ sa widoczne jako "../cmd/<plik>.h" z poziomu src/.
# WAZNE: include dirs libostree/GLib musza byc PUBLIC, nie PRIVATE -- pliki
# cmd_*.cpp (czesc targetu "deb-ostree", nie "deb_ostree_core") wlaczaja
# cmd/sysroot.h i cmd/ostree_repo.h, ktore z kolei wlaczaja <ostree.h> i
# <glib.h>. Gdyby te include dirs byly PRIVATE, propagowalyby sie tylko
# wewnatrz samej biblioteki deb_ostree_core, a "deb-ostree" linkujace ja
# przez target_link_libraries nie dostalby ich na sciezce kompilacji --
# stąd "fatal error: ostree.h: Nie ma takiego pliku ani katalogu".
target_include_directories(deb_ostree_core
    PUBLIC
        ${CMAKE_SOURCE_DIR}
        ${OSTREE_INCLUDE_DIRS}
        ${GLIB_INCLUDE_DIRS}
        ${GOBJECT_INCLUDE_DIRS}
        ${GIO_INCLUDE_DIRS}
)

target_link_directories(deb_ostree_core PUBLIC
    ${OSTREE_LIBRARY_DIRS}
    ${GLIB_LIBRARY_DIRS}
    ${GOBJECT_LIBRARY_DIRS}
    ${GIO_LIBRARY_DIRS}
)

target_link_libraries(deb_ostree_core
    PUBLIC
        ${OSTREE_LIBRARIES}
        ${GLIB_LIBRARIES}
        ${GOBJECT_LIBRARIES}
        ${GIO_LIBRARIES}
        Threads::Threads
)

target_compile_options(deb_ostree_core PUBLIC
    ${OSTREE_CFLAGS_OTHER}
    ${GLIB_CFLAGS_OTHER}
)

# ── Binarka CLI ──────────────────────────────────────────────────────────────
add_executable(deb-ostree
    src/main.cpp
    src/cmd_status.cpp
    src/cmd_install.cpp
    src/cmd_uninstall.cpp
    src/cmd_upgrade.cpp
    src/cmd_rollback.cpp
    src/cmd_rebase.cpp
    src/cmd_deploy.cpp
    src/cmd_cleanup.cpp
    src/cmd_initramfs.cpp
)

target_link_libraries(deb-ostree PRIVATE deb_ostree_core)
target_include_directories(deb-ostree PRIVATE ${CMAKE_SOURCE_DIR})

# ── Instalacja ───────────────────────────────────────────────────────────────
include(GNUInstallDirs)

install(TARGETS deb-ostree
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(FILES config/deb-ostree.hk.example
        DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/deb-ostree
        RENAME deb-ostree.hk
        OPTIONAL)

# ── Testy ────────────────────────────────────────────────────────────────────
enable_testing()
add_subdirectory(tests)
