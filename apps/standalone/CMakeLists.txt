include_directories(
    ${CMAKE_BINARY_DIR}/toolboxes/core
)


add_subdirectory(cpu)
option(DISABLE_STANDALONE_GPU_APPS "Skip standalone GPU apps" Off)
if (CUDA_FOUND AND NOT DISABLE_STANDALONE_GPU_APPS)
  add_subdirectory(gpu)
endif()
