add_executable(simple_webgpu
    simple_webgpu.cpp
)

target_link_libraries(simple_webgpu PRIVATE webgpu)