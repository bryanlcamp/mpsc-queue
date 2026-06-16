add_executable(mpsc_benchmark main.cpp)
target_link_libraries(mpsc_benchmark PRIVATE mpsc_buffer)
