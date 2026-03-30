find_package(PostgreSQL REQUIRED)

add_executable(server server.cpp)
target_link_libraries(server PRIVATE algorithms Crow::Crow PostgreSQL::PostgreSQL)
