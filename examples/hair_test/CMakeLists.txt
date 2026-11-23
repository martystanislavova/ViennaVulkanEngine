add_executable(hair_test
    hair_test.cpp
)

target_link_libraries(hair_test PRIVATE
    ViennaVulkanEngine::ViennaVulkanEngine
    vve_example_support
)

add_dependencies(hair_test
    ViennaVulkanEngine
    vve_simple_shaders
)

vve_set_output_dirs(hair_test)
