find_package(GTest REQUIRED)
find_package(PostgreSQL REQUIRED)

# test_db_types: pure C++ — no libpq dependency.
add_executable(test_db_types test_db_types.cpp)
target_include_directories(test_db_types PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(test_db_types PRIVATE GTest::gtest_main)

# test_conn_pool: exercises ConnPool/ConnGuard against a bad connection string.
add_executable(test_conn_pool test_conn_pool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../conn_pool.cpp)
target_include_directories(test_conn_pool PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(test_conn_pool PRIVATE GTest::gtest_main PostgreSQL::PostgreSQL)

# test_db_manager: exercises DBManager construction and exception propagation.
add_executable(test_db_manager test_db_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../db_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../conn_pool.cpp)
target_include_directories(test_db_manager PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(test_db_manager PRIVATE GTest::gtest_main PostgreSQL::PostgreSQL)

# test_db_manager_integration: live PostgreSQL integration tests.
# Requires spark_test DB — see server/tests/test_db_manager_integration.cpp header.
add_executable(test_db_manager_integration test_db_manager_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../db_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../conn_pool.cpp)
target_include_directories(test_db_manager_integration PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(test_db_manager_integration PRIVATE GTest::gtest_main PostgreSQL::PostgreSQL)
