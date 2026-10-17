cmake_minimum_required(VERSION 3.20)
project(mom_c29_probe LANGUAGES C CXX)

# Share portable job management and MOM's integer Blake2b between host and PS5.
set(MOM_MANAGEMENT_SOURCES ../manager.c ../manager-test.c
  ../../xmrig/crypto/randomx/blake2/blake2b.c)
add_library(mom-ps5-management STATIC ${MOM_MANAGEMENT_SOURCES})
add_library(mom-c29-solver STATIC solver.cpp)
find_package(Threads REQUIRED)
target_compile_features(mom-c29-solver PRIVATE cxx_std_17)
target_compile_options(mom-c29-solver PRIVATE -Wall -Wextra -Werror -fno-exceptions -fno-rtti)
target_link_libraries(mom-c29-solver PUBLIC mom-ps5-management Threads::Threads)
find_package(OpenSSL REQUIRED)
add_library(mom-ps5-pool STATIC ../pool.cpp)
target_compile_features(mom-ps5-pool PRIVATE cxx_std_17)
target_compile_options(mom-ps5-pool PRIVATE -Wall -Wextra -Werror -fno-exceptions -fno-rtti)
target_link_libraries(mom-ps5-pool PUBLIC mom-ps5-management OpenSSL::SSL OpenSSL::Crypto)
add_executable(ps5-pool-check ../pool-check.cpp)
target_compile_features(ps5-pool-check PRIVATE cxx_std_17)
target_link_libraries(ps5-pool-check PRIVATE mom-ps5-pool)
target_include_directories(mom-ps5-management PRIVATE ../../xmrig)
target_compile_features(mom-ps5-management PRIVATE c_std_11)
target_compile_options(mom-ps5-management PRIVATE -Wall -Wextra -Werror)

# Compile on the host and embed SPIR-V so a console probe needs no external shader file.
find_program(GLSLC glslc REQUIRED)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/seed.spv.inc
  COMMAND ${GLSLC} --target-env=vulkan1.0 -O -mfmt=c
    ${CMAKE_CURRENT_SOURCE_DIR}/seed.comp -o ${CMAKE_CURRENT_BINARY_DIR}/seed.spv.inc
  DEPENDS seed.comp siphash.glsl VERBATIM)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/trim.spv.inc
  COMMAND ${GLSLC} --target-env=vulkan1.0 -O -mfmt=c
    ${CMAKE_CURRENT_SOURCE_DIR}/trim.comp -o ${CMAKE_CURRENT_BINARY_DIR}/trim.spv.inc
  DEPENDS trim.comp siphash.glsl VERBATIM)
# Bucket kernels keep endpoint storage persistent and use local degree planes.
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/bucket.spv.inc
  COMMAND ${GLSLC} --target-env=vulkan1.0 -O -mfmt=c
    ${CMAKE_CURRENT_SOURCE_DIR}/bucket.comp -o ${CMAKE_CURRENT_BINARY_DIR}/bucket.spv.inc
  DEPENDS bucket.comp siphash.glsl VERBATIM)
add_executable(c29-seed-probe probe.c ${CMAKE_CURRENT_BINARY_DIR}/seed.spv.inc
  ${CMAKE_CURRENT_BINARY_DIR}/trim.spv.inc ${CMAKE_CURRENT_BINARY_DIR}/bucket.spv.inc)
target_include_directories(c29-seed-probe PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_compile_features(c29-seed-probe PRIVATE c_std_11)
target_compile_options(c29-seed-probe PRIVATE -Wall -Wextra -Werror)
target_link_libraries(c29-seed-probe PRIVATE mom-c29-solver mom-ps5-pool)

if(PS5)
  # RADV needs its ICD dispatch and platform link recipe, beyond a generic Vulkan link.
  message(FATAL_ERROR "Build the native RADV title with scripts/build-ps5-c29.sh")
else()
  find_package(Vulkan REQUIRED)
  target_link_libraries(c29-seed-probe PRIVATE Vulkan::Vulkan)
  enable_testing()
  # Exercise MOM's unchanged forest with its existing comprehensive cycle regression tests.
  add_executable(mom-c29-cycle-test ../../tests/native/c29_cycle.cpp)
  target_compile_features(mom-c29-cycle-test PRIVATE cxx_std_17)
  target_compile_options(mom-c29-cycle-test PRIVATE -UNDEBUG)
  add_test(NAME mom_c29_cycles COMMAND mom-c29-cycle-test)
  add_executable(ps5-c29-solver-test ../../tests/native/ps5_c29_solver.cpp)
  target_compile_features(ps5-c29-solver-test PRIVATE cxx_std_17)
  target_compile_options(ps5-c29-solver-test PRIVATE -Wall -Wextra -Werror)
  target_link_libraries(ps5-c29-solver-test PRIVATE mom-c29-solver)
  add_test(NAME ps5_c29_solver COMMAND ps5-c29-solver-test)
  add_executable(mom-ps5-management-test ../manager-test.c)
  target_compile_definitions(mom-ps5-management-test PRIVATE MOM_MANAGER_TEST_MAIN=1)
  target_link_libraries(mom-ps5-management-test PRIVATE mom-ps5-management)
  add_test(NAME ps5_management COMMAND mom-ps5-management-test)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  # Drive real sockets, TLS, reconnects and proof serialization through a deterministic pool.
  add_executable(ps5-pool-test ../../tests/native/ps5_pool.cpp)
  target_compile_features(ps5-pool-test PRIVATE cxx_std_17)
  target_link_libraries(ps5-pool-test PRIVATE mom-ps5-pool)
  add_test(NAME ps5_pool COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/test_ps5_pool.py $<TARGET_FILE:ps5-pool-test>)
  set_tests_properties(ps5_pool PROPERTIES TIMEOUT 30)
  add_test(NAME c29_reference COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/test_c29_ps5_reference.py)
  add_test(NAME c29_gpu_seed COMMAND c29-seed-probe)
  set_tests_properties(c29_gpu_seed PROPERTIES TIMEOUT 60)
  # Compare padded first-trim layouts against the endpoint oracle, including batched transitions.
  foreach(variant IN ITEMS padded padded_permuted padded_batched)
    add_test(NAME c29_gpu_${variant} COMMAND c29-seed-probe)
    set_tests_properties(c29_gpu_${variant} PROPERTIES TIMEOUT 60)
  endforeach()
  set_tests_properties(c29_gpu_padded PROPERTIES ENVIRONMENT "MOM_C29_TUNE=5 256 0 2")
  set_tests_properties(c29_gpu_padded_permuted PROPERTIES ENVIRONMENT "MOM_C29_TUNE=7 256 0 0")
  set_tests_properties(c29_gpu_padded_batched PROPERTIES ENVIRONMENT "MOM_C29_TUNE=5 256 4 2")
  # Circular seed tiles exercise wraparound with both individual and batched trim commands.
  foreach(variant IN ITEMS ring ring_batched)
    add_test(NAME c29_gpu_${variant} COMMAND c29-seed-probe)
    set_tests_properties(c29_gpu_${variant} PROPERTIES TIMEOUT 60)
  endforeach()
  set_tests_properties(c29_gpu_ring PROPERTIES ENVIRONMENT "MOM_C29_TUNE=5 256 0 3")
  set_tests_properties(c29_gpu_ring_batched PROPERTIES ENVIRONMENT "MOM_C29_TUNE=5 256 4 3")
  # Select the CPU path through the same public tuning parser used by native mining.
  add_test(NAME c29_gpu_flat_cpu COMMAND c29-seed-probe)
  set_tests_properties(c29_gpu_flat_cpu PROPERTIES TIMEOUT 60 ENVIRONMENT "MOM_C29_TUNE=5 256 0 3 1")
  # Exercise all independent widths and partial generation groups against the endpoint oracle.
  foreach(seed_width IN ITEMS 128 256)
    foreach(redistribute_width IN ITEMS 128 256)
      add_test(NAME c29_gpu_wg_${seed_width}_${redistribute_width} COMMAND c29-seed-probe)
      set_tests_properties(c29_gpu_wg_${seed_width}_${redistribute_width} PROPERTIES
        TIMEOUT 60 ENVIRONMENT "MOM_C29_TUNE=5 256 0 3 1 ${seed_width} ${redistribute_width}")
    endforeach()
  endforeach()
  foreach(mode IN ITEMS 0 1 2)
    add_test(NAME c29_gpu_wg_legacy_${mode} COMMAND c29-seed-probe)
    set_tests_properties(c29_gpu_wg_legacy_${mode} PROPERTIES
      TIMEOUT 60 ENVIRONMENT "MOM_C29_TUNE=5 256 0 ${mode} 1 128 128")
  endforeach()
  add_test(NAME c29_gpu_wg_batched COMMAND c29-seed-probe)
  set_tests_properties(c29_gpu_wg_batched PROPERTIES
    TIMEOUT 60 ENVIRONMENT "MOM_C29_TUNE=5 256 4 3 1 128 128")
  # Offset changes must preserve exact endpoints, including permuted and batched source flags.
  foreach(step IN ITEMS 16 24 32 40 48 64)
    add_test(NAME c29_gpu_stride_${step} COMMAND c29-seed-probe)
    set_tests_properties(c29_gpu_stride_${step} PROPERTIES
      TIMEOUT 60 ENVIRONMENT "MOM_C29_TUNE=5 256 0 3 1 256 256 ${step}")
  endforeach()
  add_test(NAME c29_gpu_stride_permuted COMMAND c29-seed-probe)
  set_tests_properties(c29_gpu_stride_permuted PROPERTIES
    TIMEOUT 60 ENVIRONMENT "MOM_C29_TUNE=7 256 0 3 1 256 256 64")
  add_test(NAME c29_gpu_stride_batched COMMAND c29-seed-probe)
  set_tests_properties(c29_gpu_stride_batched PROPERTIES
    TIMEOUT 60 ENVIRONMENT "MOM_C29_TUNE=5 256 4 3 1 256 256 16")
  # Cache collisions and range overflow must preserve the direct-write endpoint multiset.
  foreach(cache_mode IN ITEMS 1 2)
    foreach(cache_layout IN ITEMS 5 7)
      add_test(NAME c29_gpu_cache_${cache_mode}_${cache_layout} COMMAND c29-seed-probe)
      set_tests_properties(c29_gpu_cache_${cache_mode}_${cache_layout} PROPERTIES
        TIMEOUT 60 ENVIRONMENT "MOM_C29_TUNE=${cache_layout} 256 0 3 1 256 256 32 ${cache_mode}")
    endforeach()
  endforeach()
  option(C29_TEST_ICD_DISPATCH "Test the native dispatch path against an ICD library" OFF)
  if(C29_TEST_ICD_DISPATCH)
    # Point Vulkan_LIBRARY directly at an ICD (e.g. MoltenVK), not the Vulkan loader.
    add_executable(c29-icd-seed-probe probe.c ${CMAKE_CURRENT_BINARY_DIR}/seed.spv.inc
      ${CMAKE_CURRENT_BINARY_DIR}/trim.spv.inc ${CMAKE_CURRENT_BINARY_DIR}/bucket.spv.inc)
    target_include_directories(c29-icd-seed-probe PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
    target_compile_definitions(c29-icd-seed-probe PRIVATE MOM_PS5_RADV=1)
    target_compile_features(c29-icd-seed-probe PRIVATE c_std_11)
    target_compile_options(c29-icd-seed-probe PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(c29-icd-seed-probe PRIVATE Vulkan::Vulkan)
    target_link_libraries(c29-icd-seed-probe PRIVATE mom-c29-solver mom-ps5-pool)
    add_test(NAME c29_icd_gpu_seed COMMAND c29-icd-seed-probe)
    set_tests_properties(c29_icd_gpu_seed PROPERTIES TIMEOUT 60)
  endif()
endif()
