find_package(PostgreSQL REQUIRED)

add_executable(server server.cpp db_manager.cpp conn_pool.cpp)
target_link_libraries(server PRIVATE algorithms Crow::Crow PostgreSQL::PostgreSQL)
