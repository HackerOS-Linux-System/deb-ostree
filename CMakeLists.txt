cmake_minimum_required(VERSION 3.16)

project(deb-ostree
    VERSION 0.1.0
    DESCRIPTION "rpm-ostree dla systemow Debian/apt + OCI/bootc (immutable image-based)"
    HOMEPAGE_URL "https://github.com/TwojaOrg/deb-ostree"
    LANGUAGES CXX)

# ── Standard i flagi ────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD          17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# UWAGA: C++17, nie C++20 -- libsolv uzywa pola "requires" w Solvable
# co koliduje ze slowem kluczowym C++20 (concepts).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

add_compile_options(
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter   # GLib callbacks czesto maja nieuzywane parametry
)

# ── Zależności systemowe (pkg-config) ───────────────────────────────────────
find_package(PkgConfig REQUIRED)
find_package(Threads   REQUIRED)

pkg_check_modules(OSTREE  REQUIRED ostree-1)
pkg_check_modules(GLIB    REQUIRED glib-2.0)
pkg_check_modules(GOBJECT REQUIRED gobject-2.0)
pkg_check_modules(GIO     REQUIRED gio-2.0)

# libsolv, libcurl, openssl, liblzma, zlib, libzstd
pkg_check_modules(LIBSOLV REQUIRED libsolv)
pkg_check_modules(CURL    REQUIRED libcurl)
pkg_check_modules(OPENSSL REQUIRED openssl)
pkg_check_modules(LZMA    REQUIRED liblzma)
pkg_check_modules(ZLIB    REQUIRED zlib)
pkg_check_modules(ZSTD    REQUIRED libzstd)

# ── Biblioteka core (statyczna) ──────────────────────────────────────────────
add_library(deb_ostree_core STATIC
    # Infrastruktura
    src/logging.cpp
    src/process.cpp
    src/state_store.cpp
    src/hk_parser.cpp
    src/progress.cpp
    # OSTree / sysroot
    src/ostree_repo.cpp
    src/sysroot.cpp
    src/overlay_manager.cpp
    # Apt / pakiety
    src/apt_repo_index.cpp
    src/compress_util.cpp
    src/tar_extractor.cpp
    src/deb_archive.cpp
    src/deb_fetcher.cpp
    src/solv_pool.cpp
    src/status_db.cpp
    # NOWE v0.1.0
    src/gpg_verifier.cpp
    src/index_cache.cpp
    src/transaction_lock.cpp
    src/tree_export.cpp
    src/dpkg_status.cpp
    src/maintainer_scripts.cpp
    # Warstwowanie
    src/deb_layer.cpp
    src/oci_puller.cpp
)

target_include_directories(deb_ostree_core
    PUBLIC
        ${CMAKE_SOURCE_DIR}
        ${OSTREE_INCLUDE_DIRS}
        ${GLIB_INCLUDE_DIRS}
        ${GOBJECT_INCLUDE_DIRS}
        ${GIO_INCLUDE_DIRS}
        ${LIBSOLV_INCLUDE_DIRS}
        ${CURL_INCLUDE_DIRS}
        ${OPENSSL_INCLUDE_DIRS}
        ${LZMA_INCLUDE_DIRS}
        ${ZLIB_INCLUDE_DIRS}
        ${ZSTD_INCLUDE_DIRS}
)

target_link_directories(deb_ostree_core PUBLIC
    ${OSTREE_LIBRARY_DIRS}
    ${GLIB_LIBRARY_DIRS}
    ${GOBJECT_LIBRARY_DIRS}
    ${GIO_LIBRARY_DIRS}
    ${LIBSOLV_LIBRARY_DIRS}
    ${CURL_LIBRARY_DIRS}
    ${OPENSSL_LIBRARY_DIRS}
    ${LZMA_LIBRARY_DIRS}
    ${ZLIB_LIBRARY_DIRS}
    ${ZSTD_LIBRARY_DIRS}
)

target_link_libraries(deb_ostree_core
    PUBLIC
        ${OSTREE_LIBRARIES}
        ${GLIB_LIBRARIES}
        ${GOBJECT_LIBRARIES}
        ${GIO_LIBRARIES}
        ${LIBSOLV_LIBRARIES}
        ${CURL_LIBRARIES}
        ${OPENSSL_LIBRARIES}
        ${LZMA_LIBRARIES}
        ${ZLIB_LIBRARIES}
        ${ZSTD_LIBRARIES}
        Threads::Threads
)

target_compile_options(deb_ostree_core PUBLIC
    ${OSTREE_CFLAGS_OTHER}
    ${GLIB_CFLAGS_OTHER}
    ${LIBSOLV_CFLAGS_OTHER}
    ${CURL_CFLAGS_OTHER}
)

# ── Binarka CLI ──────────────────────────────────────────────────────────────
add_executable(deb-ostree
    src/main.cpp
    # Istniejące komendy
    src/cmd_status.cpp
    src/cmd_install.cpp
    src/cmd_uninstall.cpp
    src/cmd_upgrade.cpp
    src/cmd_rollback.cpp
    src/cmd_rebase.cpp
    src/cmd_deploy.cpp
    src/cmd_initramfs.cpp
    # NOWE v0.1.0
    src/cmd_search.cpp
    src/cmd_list.cpp
    src/cmd_pin.cpp
    src/cmd_cleanup.cpp
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
