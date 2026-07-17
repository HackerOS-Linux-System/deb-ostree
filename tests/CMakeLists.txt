add_executable(test_config_parser test_config_parser.cpp)
target_link_libraries(test_config_parser PRIVATE deb_ostree_core)
target_include_directories(test_config_parser PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME config_parser COMMAND test_config_parser)

# NOWE v0.1.0
add_executable(test_solv_pool test_solv_pool.cpp)
target_link_libraries(test_solv_pool PRIVATE deb_ostree_core)
target_include_directories(test_solv_pool PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME solv_pool COMMAND test_solv_pool)

add_executable(test_status_db test_status_db.cpp)
target_link_libraries(test_status_db PRIVATE deb_ostree_core)
target_include_directories(test_status_db PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME status_db COMMAND test_status_db)

add_executable(test_apt_repo_index test_apt_repo_index.cpp)
target_link_libraries(test_apt_repo_index PRIVATE deb_ostree_core)
target_include_directories(test_apt_repo_index PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME apt_repo_index COMMAND test_apt_repo_index)

add_executable(test_index_cache test_index_cache.cpp)
target_link_libraries(test_index_cache PRIVATE deb_ostree_core)
target_include_directories(test_index_cache PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME index_cache COMMAND test_index_cache)

add_executable(test_gpg_verifier test_gpg_verifier.cpp)
target_link_libraries(test_gpg_verifier PRIVATE deb_ostree_core)
target_include_directories(test_gpg_verifier PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME gpg_verifier COMMAND test_gpg_verifier)

# Test integracyjny v0.2.0 (#1)
add_executable(test_integration test_integration.cpp)
target_link_libraries(test_integration PRIVATE deb_ostree_core)
target_include_directories(test_integration PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME integration COMMAND test_integration)

# Test OCI ref v0.2.0
add_executable(test_oci_ref test_oci_ref.cpp)
target_link_libraries(test_oci_ref PRIVATE deb_ostree_core)
target_include_directories(test_oci_ref PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME oci_ref COMMAND test_oci_ref)
