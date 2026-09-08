# ps4_vulkan: real Vulkan 1.0 ICD over the PS4's native GNM/VideoOut system
# libraries, replacing vkgl/piglet as WoWee's PS4 rendering backend. See
# ps4/third_party/ps4_vulkan/NOTICE.md for what is vendored here and why, and
# README.md for why piglet was replaced (its eglGetDisplay is unavailable to
# installed homebrew titles on this port's test hardware, confirmed
# independently of WoWee's own code).
#
# Provides the Vulkan::Vulkan target the renderer already compiles against
# (vk_context.cpp, vk_shader.cpp, vk_utils.cpp, imgui_impl_vulkan, VMA,
# vk-bootstrap) - real vulkan.h, not vkgl's subset header - plus the prebuilt
# libopengnm.a (GNM/VideoOut) and libpsbc.orbis.a (SPIR-V -> GCN shader
# compiler, a patched standalone Mesa/RADV/ACO build) as transitive link
# dependencies.

set(WOWEE_PS4_VULKAN_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/ps4/third_party/ps4_vulkan")

set(WOWEE_PS4_VULKAN_ICD_SOURCES
    ${WOWEE_PS4_VULKAN_ROOT}/source/vulkan-ps4/src/vk_ps4_entry.c
    ${WOWEE_PS4_VULKAN_ROOT}/source/vulkan-ps4/src/vk_ps4_dispatch.c
    ${WOWEE_PS4_VULKAN_ROOT}/source/vulkan-ps4/src/vk_ps4_entrypoints.c
    ${WOWEE_PS4_VULKAN_ROOT}/source/vulkan-ps4/src/vk_ps4_instance.c
    ${WOWEE_PS4_VULKAN_ROOT}/source/vulkan-ps4/src/vk_ps4_device.c
    ${WOWEE_PS4_VULKAN_ROOT}/source/vulkan-ps4/src/vk_ps4_format.c
    ${WOWEE_PS4_VULKAN_ROOT}/source/vulkan-ps4/src/vk_ps4_memory.c
    ${WOWEE_PS4_VULKAN_ROOT}/source/vulkan-ps4/src/vk_ps4_buffer.c
    ${WOWEE_PS4_VULKAN_ROOT}/source/vulkan-ps4/src/vk_ps4_image.c
    ${WOWEE_PS4_VULKAN_ROOT}/source/vulkan-ps4/src/vk_ps4_render_pass.c
    ${WOWEE_PS4_VULKAN_ROOT}/source/vulkan-ps4/src/vk_ps4_shader.c
    ${WOWEE_PS4_VULKAN_ROOT}/source/vulkan-ps4/src/vk_ps4_pipeline.c
    ${WOWEE_PS4_VULKAN_ROOT}/source/vulkan-ps4/src/vk_ps4_descriptor.c
    ${WOWEE_PS4_VULKAN_ROOT}/source/vulkan-ps4/src/vk_ps4_command.c
    ${WOWEE_PS4_VULKAN_ROOT}/source/vulkan-ps4/src/vk_ps4_swapchain.c
    ${WOWEE_PS4_VULKAN_ROOT}/source/vulkan-ps4/src/vk_ps4_queue.c
    ${WOWEE_PS4_VULKAN_ROOT}/source/vulkan-ps4/src/vk_ps4_sync.c
    ${WOWEE_PS4_VULKAN_ROOT}/source/vulkan-ps4/src/vk_ps4_query.c
    ${WOWEE_PS4_VULKAN_ROOT}/source/vulkan-ps4/src/vk_ps4_stubs.c
    ${WOWEE_PS4_VULKAN_ROOT}/source/vulkan-ps4/src/vk_ps4_vulkan11.c
    ${WOWEE_PS4_VULKAN_ROOT}/source/vulkan-ps4/src/vk_ps4_log.c
    # Stub implementations for a handful of Mesa utility functions
    # libpsbc.orbis.a references but doesn't archive an object for: BLAKE3's
    # x86 SIMD dispatch targets (unreachable - PS4 is GCN, not x86, so the
    # portable BLAKE3 path is the one actually taken) and _mesa_log_multiline
    # (a logging sink). Without this, --gc-sections still finds these
    # symbols reachable from the (used) BLAKE3 dispatch and NIR logging code
    # and the link fails on them even though none of these stub bodies ever
    # runs. See its header comment for the DXT/S3TC note.
    ${WOWEE_PS4_VULKAN_ROOT}/source/vulkan-ps4/src/mesa_stubs.c
)

add_library(ps4_vulkan_icd STATIC ${WOWEE_PS4_VULKAN_ICD_SOURCES})
target_include_directories(ps4_vulkan_icd SYSTEM PUBLIC
    ${WOWEE_PS4_VULKAN_ROOT}/include
)
target_include_directories(ps4_vulkan_icd PRIVATE
    ${OO_PS4_TOOLCHAIN}/include/orbis
)
target_compile_definitions(ps4_vulkan_icd PRIVATE VK_PS4_HAVE_PSBC=1)
set_target_properties(ps4_vulkan_icd PROPERTIES C_STANDARD 11 LINKER_LANGUAGE C)
target_compile_options(ps4_vulkan_icd PRIVATE
    -Wno-unused-function -Wno-unused-parameter -Wno-unused-variable
    -Wno-macro-redefined -Wno-typedef-redefinition -Wno-visibility
)

add_library(Vulkan::Vulkan ALIAS ps4_vulkan_icd)

add_library(ps4_opengnm STATIC IMPORTED GLOBAL)
set_target_properties(ps4_opengnm PROPERTIES
    IMPORTED_LOCATION "${WOWEE_PS4_VULKAN_ROOT}/lib/libopengnm.a"
)
add_library(ps4_psbc STATIC IMPORTED GLOBAL)
set_target_properties(ps4_psbc PROPERTIES
    IMPORTED_LOCATION "${WOWEE_PS4_VULKAN_ROOT}/lib/libpsbc.orbis.a"
)

# ps4_vulkan_icd, ps4_opengnm and ps4_psbc cross-reference each other's
# symbols; cmake/ps4.toolchain.cmake wraps the whole final <LINK_LIBRARIES>
# list in --start-group/--end-group so ld.lld resolves them regardless of
# link order, matching the upstream Makefile.orbis's --whole-archive handling.
target_link_libraries(ps4_vulkan_icd PUBLIC ps4_opengnm ps4_psbc)
