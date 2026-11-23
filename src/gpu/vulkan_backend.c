/*
 * Vulkan 1.3 bring-up backend (RECOMP_GPU=vulkan).
 *
 * This implements the official Nv2aBackend contract, CPU-transformed
 * Nv2aBackendExt batches, gathered-input GPU vertex-program batches, and
 * mandatory readback. The default route publishes guest memory through
 * xboxrecomp; an optional host provider also consumes completed Vulkan images.
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define VK_USE_PLATFORM_WIN32_KHR
#define VK_NO_PROTOTYPES
#include <windows.h>
#include <vulkan/vulkan.h>

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../perf_counters.h"
#include "nv2a_backend.h"
#include "nv2a_backend_ext.h"
#include "xbox_memory_layout.h"
#include "combiner_state.h"
#include "guest_snapshot.h"
#include "render_scale.h"
#include "renderer_startup.h"
#include "surface_layout.h"
#include "texture_filter.h"
#include "texture_layout.h"
#include "vulkan_backend.h"
#include "vulkan_present.h"
#include "vulkan_shaders.h"
#include "video/gpu_present.h"

#define VULKAN_COMMAND_SLOTS 8
#define VULKAN_MAX_SURFACES 64
#define VULKAN_MAX_DEPTHS 64
#define VULKAN_MAX_PIPELINES 256
#define VULKAN_MAX_TEXTURES 2048
#define VULKAN_TEXTURE_BUCKETS 4096
#define VULKAN_MAX_TEXTURE_SNAPSHOT_BYTES (128u << 20)
#define VULKAN_BATCH_OPERATIONS 128
#define VULKAN_UPLOAD_CHUNK_MIN (1u << 20)
#define VULKAN_MAX_SAMPLERS 512
#define VULKAN_MAX_VISIBILITY_REPORTS 1024
#define VULKAN_ZPASS_FLUSH_DELAY_MS 0.25

#define GUEST(va) ((uint8_t *)((uintptr_t)(va) + xbox_GetMemoryOffset()))

typedef struct VulkanFunctions {
    PFN_vkGetInstanceProcAddr GetInstanceProcAddr;
    PFN_vkEnumerateInstanceVersion EnumerateInstanceVersion;
    PFN_vkEnumerateInstanceLayerProperties EnumerateInstanceLayerProperties;
    PFN_vkEnumerateInstanceExtensionProperties EnumerateInstanceExtensionProperties;
    PFN_vkCreateInstance CreateInstance;
    PFN_vkDestroyInstance DestroyInstance;
    PFN_vkCreateDebugUtilsMessengerEXT CreateDebugUtilsMessengerEXT;
    PFN_vkDestroyDebugUtilsMessengerEXT DestroyDebugUtilsMessengerEXT;
    PFN_vkEnumeratePhysicalDevices EnumeratePhysicalDevices;
    PFN_vkGetPhysicalDeviceProperties GetPhysicalDeviceProperties;
    PFN_vkGetPhysicalDeviceFeatures2 GetPhysicalDeviceFeatures2;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties GetPhysicalDeviceQueueFamilyProperties;
    PFN_vkGetPhysicalDeviceMemoryProperties GetPhysicalDeviceMemoryProperties;
    PFN_vkGetPhysicalDeviceFormatProperties GetPhysicalDeviceFormatProperties;
    PFN_vkEnumerateDeviceExtensionProperties EnumerateDeviceExtensionProperties;
    PFN_vkCreateDevice CreateDevice;
    PFN_vkGetDeviceProcAddr GetDeviceProcAddr;
    PFN_vkDestroyDevice DestroyDevice;
    PFN_vkGetDeviceQueue GetDeviceQueue;
    PFN_vkDeviceWaitIdle DeviceWaitIdle;
    PFN_vkQueueSubmit QueueSubmit;
    PFN_vkCreateCommandPool CreateCommandPool;
    PFN_vkDestroyCommandPool DestroyCommandPool;
    PFN_vkAllocateCommandBuffers AllocateCommandBuffers;
    PFN_vkResetCommandBuffer ResetCommandBuffer;
    PFN_vkBeginCommandBuffer BeginCommandBuffer;
    PFN_vkEndCommandBuffer EndCommandBuffer;
    PFN_vkCreateFence CreateFence;
    PFN_vkDestroyFence DestroyFence;
    PFN_vkGetFenceStatus GetFenceStatus;
    PFN_vkWaitForFences WaitForFences;
    PFN_vkResetFences ResetFences;
    PFN_vkCreateQueryPool CreateQueryPool;
    PFN_vkDestroyQueryPool DestroyQueryPool;
    PFN_vkGetQueryPoolResults GetQueryPoolResults;
    PFN_vkCreateBuffer CreateBuffer;
    PFN_vkDestroyBuffer DestroyBuffer;
    PFN_vkGetBufferMemoryRequirements GetBufferMemoryRequirements;
    PFN_vkAllocateMemory AllocateMemory;
    PFN_vkFreeMemory FreeMemory;
    PFN_vkBindBufferMemory BindBufferMemory;
    PFN_vkMapMemory MapMemory;
    PFN_vkUnmapMemory UnmapMemory;
    PFN_vkCreateImage CreateImage;
    PFN_vkDestroyImage DestroyImage;
    PFN_vkGetImageMemoryRequirements GetImageMemoryRequirements;
    PFN_vkBindImageMemory BindImageMemory;
    PFN_vkCreateImageView CreateImageView;
    PFN_vkDestroyImageView DestroyImageView;
    PFN_vkCreateSampler CreateSampler;
    PFN_vkDestroySampler DestroySampler;
    PFN_vkCreateShaderModule CreateShaderModule;
    PFN_vkDestroyShaderModule DestroyShaderModule;
    PFN_vkCreateDescriptorSetLayout CreateDescriptorSetLayout;
    PFN_vkDestroyDescriptorSetLayout DestroyDescriptorSetLayout;
    PFN_vkCreatePipelineLayout CreatePipelineLayout;
    PFN_vkDestroyPipelineLayout DestroyPipelineLayout;
    PFN_vkCreateGraphicsPipelines CreateGraphicsPipelines;
    PFN_vkCreateComputePipelines CreateComputePipelines;
    PFN_vkDestroyPipeline DestroyPipeline;
    PFN_vkCreateDescriptorPool CreateDescriptorPool;
    PFN_vkDestroyDescriptorPool DestroyDescriptorPool;
    PFN_vkResetDescriptorPool ResetDescriptorPool;
    PFN_vkAllocateDescriptorSets AllocateDescriptorSets;
    PFN_vkUpdateDescriptorSets UpdateDescriptorSets;
    PFN_vkCmdPipelineBarrier CmdPipelineBarrier;
    PFN_vkCmdCopyBufferToImage CmdCopyBufferToImage;
    PFN_vkCmdCopyImage CmdCopyImage;
    PFN_vkCmdCopyImageToBuffer CmdCopyImageToBuffer;
    PFN_vkCmdClearDepthStencilImage CmdClearDepthStencilImage;
    PFN_vkCmdBeginRendering CmdBeginRendering;
    PFN_vkCmdEndRendering CmdEndRendering;
    PFN_vkCmdClearAttachments CmdClearAttachments;
    PFN_vkCmdBindPipeline CmdBindPipeline;
    PFN_vkCmdBindIndexBuffer CmdBindIndexBuffer;
    PFN_vkCmdBindVertexBuffers CmdBindVertexBuffers;
    PFN_vkCmdBindDescriptorSets CmdBindDescriptorSets;
    PFN_vkCmdSetViewport CmdSetViewport;
    PFN_vkCmdSetScissor CmdSetScissor;
    PFN_vkCmdSetBlendConstants CmdSetBlendConstants;
    PFN_vkCmdPushConstants CmdPushConstants;
    PFN_vkCmdResetQueryPool CmdResetQueryPool;
    PFN_vkCmdBeginQuery CmdBeginQuery;
    PFN_vkCmdEndQuery CmdEndQuery;
    PFN_vkCmdDraw CmdDraw;
    PFN_vkCmdDrawIndexed CmdDrawIndexed;
    PFN_vkCmdDispatch CmdDispatch;
} VulkanFunctions;

typedef struct VulkanBuffer {
    VkBuffer buffer;
    VkDeviceMemory memory;
    VkDeviceSize size;
    void *mapped;
} VulkanBuffer;

typedef struct VulkanImage {
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    VkFormat format;
    VkImageLayout layout;
    uint32_t width;
    uint32_t height;
    uint32_t levels;
    uint32_t layers;
    VkImageAspectFlags aspect;
} VulkanImage;

typedef struct VulkanUploadChunk {
    VulkanBuffer buffer;
    VkDeviceSize used;
} VulkanUploadChunk;

typedef struct VulkanUpload {
    VkBuffer buffer;
    VkDeviceSize offset;
    void *mapped;
} VulkanUpload;

typedef struct VulkanCommandSlot {
    VkCommandBuffer command;
    VkFence fence;
    uint64_t submission_serial;
    int submitted;
    int recording;
    unsigned operation_count;
    VulkanUploadChunk *uploads;
    unsigned upload_count;
    unsigned upload_capacity;
    VulkanBuffer readback;
    VulkanImage *textures;
    unsigned texture_count;
    unsigned texture_capacity;
    VkDescriptorPool descriptor_pool;
    unsigned descriptor_count;
    double zpass_divisors[VULKAN_BATCH_OPERATIONS];
    uint64_t zpass_sequences[VULKAN_BATCH_OPERATIONS];
    unsigned zpass_count;
} VulkanCommandSlot;

typedef struct VulkanVisibilityReport {
    uint32_t va;
    uint64_t stamp;
    uint64_t first_query;
    uint64_t query_target;
    uint64_t submission_target;
    double queued_ms;
    int reset;
    int submission_assigned;
} VulkanVisibilityReport;

typedef struct VulkanPerfStats {
    uint64_t draw_calls;
    uint64_t draw_ticks;
    uint64_t setup_ticks;
    uint64_t vertex_bytes;
    uint64_t vertex_ticks;
    uint64_t texture_calls;
    uint64_t texture_ticks;
    uint64_t snapshot_calls;
    uint64_t snapshot_bytes;
    uint64_t snapshot_ticks;
    uint64_t lookup_calls;
    uint64_t lookup_ticks;
    uint64_t exact_checks;
    uint64_t exact_bytes;
    uint64_t exact_ticks;
    uint64_t upload_calls;
    uint64_t upload_ticks;
    uint64_t descriptor_calls;
    uint64_t descriptor_ticks;
    uint64_t command_ticks;
    uint64_t readback_calls;
    uint64_t readback_bytes;
    uint64_t readback_ticks;
    uint64_t wait_calls;
    uint64_t wait_ticks;
    uint64_t report_calls;
    uint64_t report_ticks;
    uint64_t report_poll_calls;
    uint64_t report_poll_ticks;
    uint64_t report_deferred_calls;
    uint64_t report_complete_calls;
    uint64_t report_complete_ticks;
    uint64_t fence_poll_calls;
    uint64_t fence_poll_ticks;
} VulkanPerfStats;

typedef struct VulkanSurface {
    uint32_t va;
    GpuSurfaceLayout layout;
    uint32_t pw;
    uint32_t ph;
    VulkanImage image;
    VulkanImage logical;
    VulkanImage sample_copy;
    uint8_t *guest_shadow;
    size_t guest_shadow_bytes;
    GuestSpanIdentity guest_identity;
    uint64_t guest_check_flip;
    int gpu_dirty;
    int guest_pending_upload;
} VulkanSurface;

typedef struct VulkanDepth {
    uint32_t va;
    uint32_t width;
    uint32_t height;
    VulkanImage image;
} VulkanDepth;

typedef struct VulkanTexture {
    Nv2aTexture key;
    uint32_t faces;
    GuestSpanIdentity source_identity;
    GuestSpanIdentity palette_identity;
    uint8_t *snapshot;
    size_t source_bytes;
    size_t palette_bytes;
    size_t snapshot_bytes;
    uint64_t last_use;
    int index_next;
    VulkanImage image;
} VulkanTexture;

typedef struct VulkanSamplerKey {
    uint32_t filter;
    uint32_t control0;
    uint32_t levels;
    uint32_t addr_u;
    uint32_t addr_v;
} VulkanSamplerKey;

typedef struct VulkanSampler {
    VulkanSamplerKey key;
    VkSampler sampler;
} VulkanSampler;

typedef struct VulkanPipelineKey {
    VkFormat color_format;
    VkFormat depth_format;
    VkFormat stencil_format;
    VkBlendFactor src_color;
    VkBlendFactor dst_color;
    VkBlendOp color_op;
    VkBlendFactor src_alpha;
    VkBlendFactor dst_alpha;
    VkBlendOp alpha_op;
    VkCompareOp depth_compare;
    VkCompareOp stencil_compare;
    VkStencilOp stencil_fail;
    VkStencilOp stencil_depth_fail;
    VkStencilOp stencil_pass;
    uint32_t stencil_compare_mask;
    uint32_t stencil_write_mask;
    uint32_t stencil_reference;
    VkColorComponentFlags color_mask;
    VkCullModeFlags cull_mode;
    VkFrontFace front_face;
    uint8_t blend_enable;
    uint8_t depth_test;
    uint8_t depth_write;
    uint8_t stencil_enable;
    uint8_t clear_shader;
    uint8_t extended_shader;
    uint8_t vsh_shader;
    uint8_t resolve_shader;
} VulkanPipelineKey;

typedef struct VulkanPipeline {
    VulkanPipelineKey key;
    VkPipeline pipeline;
} VulkanPipeline;

typedef struct VulkanPushConstants {
    float geometry[4];
    float texture_info[4];
    uint32_t alpha[4];
    float clear_color[4];
} VulkanPushConstants;

typedef struct VulkanVertex {
    float position[4];
    float color[4];
    float uv[2];
} VulkanVertex;

typedef struct VulkanExtendedConstants {
    float surface[4];
    float depth[4];
    float alpha[4];
    float fog_color[4];
    float c0[8][4];
    float c1[8][4];
    float final_c0[4];
    float final_c1[4];
    float texture_info[4][4];
    uint32_t color_icw[8];
    uint32_t alpha_icw[8];
    uint32_t color_ocw[8];
    uint32_t alpha_ocw[8];
    uint32_t final_control[4];
    uint32_t texture_state[4];
    uint32_t clip_state[4];
    uint32_t vsh_data[4];
    uint32_t vsh_state[4];
    float vsh_info[4];
    float vsh_fog[4];
    uint32_t vsh_verify[4];
} VulkanExtendedConstants;

typedef struct VulkanWindowClipConstants {
    uint32_t rect[NV2A_WINDOW_CLIP_COUNT][4];
    uint32_t info[4];
} VulkanWindowClipConstants;

typedef struct VulkanVshVerification {
    uint8_t *data;
    size_t data_size;
    size_t data_capacity;
    VulkanExtendedConstants constants;
    uint32_t nverts;
    int enabled;
    int valid;
} VulkanVshVerification;

typedef struct VulkanState {
    HMODULE loader;
    VulkanFunctions fn;
    VkInstance instance;
    VkDebugUtilsMessengerEXT debug_messenger;
    VkPhysicalDevice physical_device;
    VkPhysicalDeviceProperties properties;
    VkPhysicalDeviceMemoryProperties memory;
    VkFormat depth16_format;
    VkFormat depth_stencil_format;
    VkDevice device;
    VkQueue queue;
    uint32_t queue_family;
    VkCommandPool command_pool;
    VkQueryPool zpass_query_pool;
    VulkanCommandSlot slots[VULKAN_COMMAND_SLOTS];
    VulkanCommandSlot *recording_slot;
    unsigned next_slot;
    VkDescriptorSetLayout descriptor_layout;
    VkPipelineLayout pipeline_layout;
    VkShaderModule base_vs;
    VkShaderModule base_ps;
    VkShaderModule clear_vs;
    VkShaderModule clear_ps;
    VkShaderModule extended_vs;
    VkShaderModule extended_ps;
    VkShaderModule vsh_vs;
    VkShaderModule vsh_gs;
    VkShaderModule vsh_verify_cs;
    VkShaderModule resolve_vs;
    VkShaderModule resolve_bilinear_ps;
    VkShaderModule resolve_box_ps;
    VkPipeline vsh_verify_pipeline;
    VulkanVshVerification vsh_verify;
    VulkanImage white;
    VulkanImage white_cube;
    VkSampler white_sampler;
    VkSampler resolve_sampler;
    uint64_t scale_resolves;
    VulkanSurface surfaces[VULKAN_MAX_SURFACES];
    unsigned surface_count;
    VulkanDepth depths[VULKAN_MAX_DEPTHS];
    unsigned depth_count;
    VulkanTexture textures[VULKAN_MAX_TEXTURES];
    unsigned texture_count;
    int texture_index[VULKAN_TEXTURE_BUCKETS];
    VulkanSampler samplers[VULKAN_MAX_SAMPLERS];
    unsigned sampler_count;
    uint8_t *surface_probe;
    size_t surface_probe_capacity;
    uint32_t *surface_decode;
    size_t surface_decode_capacity;
    uint8_t *texture_probe;
    size_t texture_probe_capacity;
    size_t texture_snapshot_bytes;
    uint32_t *texture_decode;
    size_t texture_decode_capacity;
    uint64_t texture_serial;
    uint64_t texture_hits;
    uint64_t texture_uploads;
    uint64_t texture_mip_uploads;
    uint64_t texture_rewrites;
    uint64_t texture_evictions;
    uint64_t texture_lookup_probes;
    uint64_t guest_surface_uploads;
    uint64_t guest_surface_preserves;
    uint64_t guest_surface_conflicts;
    uint64_t present_sources_suppressed;
    VulkanPipeline pipelines[VULKAN_MAX_PIPELINES];
    unsigned pipeline_count;
    uint64_t clears;
    uint64_t draws;
    uint64_t readbacks;
    uint64_t flips;
    uint64_t zpass_total;
    uint64_t zpass_queries;
    uint64_t zpass_reads;
    uint64_t *zpass_results;
    size_t zpass_result_count;
    size_t zpass_result_capacity;
    uint64_t zpass_result_base;
    VulkanVisibilityReport zpass_reports[VULKAN_MAX_VISIBILITY_REPORTS];
    uint64_t zpass_report_head;
    uint64_t zpass_report_tail;
    uint64_t zpass_report_query_start;
    uint64_t zpass_report_running;
    uint64_t zpass_reports_completed;
    uint64_t zpass_report_polls_deferred;
    int zpass_report_reset_next;
    uint64_t last_submission_serial;
    uint64_t completed_submission_serial;
    uint64_t submissions;
    uint64_t submission_waits;
    uint64_t upload_buffer_creates;
    uint64_t descriptor_pool_creates;
    uint64_t max_batch_operations;
    VulkanPerfStats perf;
    double diagnostic_last_ms;
    uint64_t diagnostic_last_flip;
    int zpass_enabled;
    int zpass_async;
    int texture_mips;
    int native_present;
    int present_registered;
    int installed;
    int validation;
    volatile LONG device_thread;
    volatile LONG validation_errors;
} VulkanState;

static VulkanState s_vk;

static VkSampler create_sampler(const Nv2aTexture *texture,
                                uint32_t control0, uint32_t levels);

static VKAPI_ATTR VkBool32 VKAPI_CALL validation_message(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT *data,
    void *context)
{
    const char *level = severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT ? "error"
                      : severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT ? "warning"
                      : "info";
    (void)type;
    (void)context;
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        InterlockedIncrement(&s_vk.validation_errors);
    fprintf(stderr, "[GPU-Vulkan validation:%s] %s\n", level,
            data && data->pMessage ? data->pMessage : "<no message>");
    return VK_FALSE;
}

static int test_failure(const char *stage)
{
#ifdef BRUTEFORCE_VULKAN_TESTING
    const char *requested = getenv("BRUTEFORCE_VULKAN_TEST_FAIL");
    return requested && strcmp(requested, stage) == 0;
#else
    (void)stage;
    return 0;
#endif
}

static BruteforceRendererStartupResult startup_result(
    BruteforceRendererStartupStatus status, const char *stage,
    VkResult result, const char *reason)
{
    if (result == VK_SUCCESS)
        return bruteforce_renderer_startup_result(status, stage, "%s", reason);
    return bruteforce_renderer_startup_result(
        status, stage, "%s (VkResult %d)", reason, (int)result);
}

static void runtime_failure(const char *operation, VkResult result)
{
    fprintf(stderr, "[GPU-Vulkan] %s failed (VkResult %d); recovery is not implemented,"
                    " stopping without completing outstanding GPU work\n",
            operation, (int)result);
    fflush(stderr);
    abort();
}

static void vk_require(VkResult result, const char *operation)
{
    if (result != VK_SUCCESS)
        runtime_failure(operation, result);
}

static double monotonic_ms(void)
{
    static LARGE_INTEGER frequency;
    LARGE_INTEGER counter;
    if (!frequency.QuadPart)
        QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    return counter.QuadPart * 1000.0 / frequency.QuadPart;
}

static uint64_t vulkan_perf_begin(void)
{
    return g_bruteforce_perf_counters_enabled
         ? bruteforce_perf_clock() : 0;
}

static void vulkan_perf_finish(uint64_t start, uint64_t *ticks)
{
    if (start)
        *ticks += bruteforce_perf_clock() - start;
}

static void vulkan_perf_finish_call(uint64_t start, uint64_t *ticks,
                                    uint64_t *calls)
{
    if (start) {
        *ticks += bruteforce_perf_clock() - start;
        (*calls)++;
    }
}

static int parse_bool_option(const char *name, int default_value, int *value,
                             BruteforceRendererStartupResult *error)
{
    const char *text = getenv(name);
    if (!text || !*text) {
        *value = default_value;
        return 1;
    }
    if (strcmp(text, "0") == 0 || _stricmp(text, "off") == 0 ||
        _stricmp(text, "false") == 0) {
        *value = 0;
        return 1;
    }
    if (strcmp(text, "1") == 0 || _stricmp(text, "on") == 0 ||
        _stricmp(text, "true") == 0) {
        *value = 1;
        return 1;
    }
    *error = bruteforce_renderer_startup_result(
        BRUTEFORCE_RENDERER_STARTUP_INVALID_CONFIGURATION, "options",
        "%s must be 0/off/false or 1/on/true, not \"%s\"", name, text);
    return 0;
}

static int parse_device_option(uint32_t *index, int *specified,
                               BruteforceRendererStartupResult *error)
{
    const char *text = getenv("RECOMP_VULKAN_DEVICE");
    char *end;
    unsigned long value;

    *specified = 0;
    *index = 0;
    if (!text || !*text)
        return 1;
    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno || *end || value > UINT32_MAX) {
        *error = bruteforce_renderer_startup_result(
            BRUTEFORCE_RENDERER_STARTUP_INVALID_CONFIGURATION, "options",
            "RECOMP_VULKAN_DEVICE must be a decimal physical-device index, not \"%s\"",
            text);
        return 0;
    }
    *specified = 1;
    *index = (uint32_t)value;
    return 1;
}

#define LOAD_GLOBAL(member, symbol) do { \
    s_vk.fn.member = (PFN_vk##symbol)s_vk.fn.GetInstanceProcAddr(NULL, "vk" #symbol); \
    if (!s_vk.fn.member) \
        return startup_result(BRUTEFORCE_RENDERER_STARTUP_UNAVAILABLE, "loader", \
                              VK_SUCCESS, "Vulkan loader is missing vk" #symbol); \
} while (0)

#define LOAD_INSTANCE(member, symbol) do { \
    s_vk.fn.member = (PFN_vk##symbol)s_vk.fn.GetInstanceProcAddr(s_vk.instance, "vk" #symbol); \
    if (!s_vk.fn.member) \
        return startup_result(BRUTEFORCE_RENDERER_STARTUP_INITIALIZATION_FAILED, \
                              "instance-functions", VK_SUCCESS, \
                              "Vulkan instance is missing vk" #symbol); \
} while (0)

#define LOAD_DEVICE(member, symbol) do { \
    s_vk.fn.member = (PFN_vk##symbol)s_vk.fn.GetDeviceProcAddr(s_vk.device, "vk" #symbol); \
    if (!s_vk.fn.member) \
        return startup_result(BRUTEFORCE_RENDERER_STARTUP_INITIALIZATION_FAILED, \
                              "device-functions", VK_SUCCESS, \
                              "Vulkan device is missing vk" #symbol); \
} while (0)

static BruteforceRendererStartupResult load_global_functions(void)
{
    s_vk.fn.GetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)
        GetProcAddress(s_vk.loader, "vkGetInstanceProcAddr");
    if (!s_vk.fn.GetInstanceProcAddr)
        return startup_result(BRUTEFORCE_RENDERER_STARTUP_UNAVAILABLE, "loader",
                              VK_SUCCESS, "vulkan-1.dll has no vkGetInstanceProcAddr");
    s_vk.fn.EnumerateInstanceVersion = (PFN_vkEnumerateInstanceVersion)
        s_vk.fn.GetInstanceProcAddr(NULL, "vkEnumerateInstanceVersion");
    LOAD_GLOBAL(EnumerateInstanceLayerProperties, EnumerateInstanceLayerProperties);
    LOAD_GLOBAL(EnumerateInstanceExtensionProperties, EnumerateInstanceExtensionProperties);
    LOAD_GLOBAL(CreateInstance, CreateInstance);
    return startup_result(BRUTEFORCE_RENDERER_STARTUP_SUCCESS, "loader",
                          VK_SUCCESS, "Vulkan loader functions are available");
}

static BruteforceRendererStartupResult load_instance_functions(void)
{
    LOAD_INSTANCE(DestroyInstance, DestroyInstance);
    LOAD_INSTANCE(EnumeratePhysicalDevices, EnumeratePhysicalDevices);
    LOAD_INSTANCE(GetPhysicalDeviceProperties, GetPhysicalDeviceProperties);
    LOAD_INSTANCE(GetPhysicalDeviceFeatures2, GetPhysicalDeviceFeatures2);
    LOAD_INSTANCE(GetPhysicalDeviceQueueFamilyProperties, GetPhysicalDeviceQueueFamilyProperties);
    LOAD_INSTANCE(GetPhysicalDeviceMemoryProperties, GetPhysicalDeviceMemoryProperties);
    LOAD_INSTANCE(GetPhysicalDeviceFormatProperties, GetPhysicalDeviceFormatProperties);
    LOAD_INSTANCE(EnumerateDeviceExtensionProperties,
                  EnumerateDeviceExtensionProperties);
    LOAD_INSTANCE(CreateDevice, CreateDevice);
    LOAD_INSTANCE(GetDeviceProcAddr, GetDeviceProcAddr);
    if (s_vk.validation) {
        LOAD_INSTANCE(CreateDebugUtilsMessengerEXT, CreateDebugUtilsMessengerEXT);
        LOAD_INSTANCE(DestroyDebugUtilsMessengerEXT, DestroyDebugUtilsMessengerEXT);
    }
    return startup_result(BRUTEFORCE_RENDERER_STARTUP_SUCCESS, "instance-functions",
                          VK_SUCCESS, "Vulkan instance functions are available");
}

static BruteforceRendererStartupResult load_device_functions(void)
{
    LOAD_DEVICE(DestroyDevice, DestroyDevice);
    LOAD_DEVICE(GetDeviceQueue, GetDeviceQueue);
    LOAD_DEVICE(DeviceWaitIdle, DeviceWaitIdle);
    LOAD_DEVICE(QueueSubmit, QueueSubmit);
    LOAD_DEVICE(CreateCommandPool, CreateCommandPool);
    LOAD_DEVICE(DestroyCommandPool, DestroyCommandPool);
    LOAD_DEVICE(AllocateCommandBuffers, AllocateCommandBuffers);
    LOAD_DEVICE(ResetCommandBuffer, ResetCommandBuffer);
    LOAD_DEVICE(BeginCommandBuffer, BeginCommandBuffer);
    LOAD_DEVICE(EndCommandBuffer, EndCommandBuffer);
    LOAD_DEVICE(CreateFence, CreateFence);
    LOAD_DEVICE(DestroyFence, DestroyFence);
    LOAD_DEVICE(GetFenceStatus, GetFenceStatus);
    LOAD_DEVICE(WaitForFences, WaitForFences);
    LOAD_DEVICE(ResetFences, ResetFences);
    LOAD_DEVICE(CreateQueryPool, CreateQueryPool);
    LOAD_DEVICE(DestroyQueryPool, DestroyQueryPool);
    LOAD_DEVICE(GetQueryPoolResults, GetQueryPoolResults);
    LOAD_DEVICE(CreateBuffer, CreateBuffer);
    LOAD_DEVICE(DestroyBuffer, DestroyBuffer);
    LOAD_DEVICE(GetBufferMemoryRequirements, GetBufferMemoryRequirements);
    LOAD_DEVICE(AllocateMemory, AllocateMemory);
    LOAD_DEVICE(FreeMemory, FreeMemory);
    LOAD_DEVICE(BindBufferMemory, BindBufferMemory);
    LOAD_DEVICE(MapMemory, MapMemory);
    LOAD_DEVICE(UnmapMemory, UnmapMemory);
    LOAD_DEVICE(CreateImage, CreateImage);
    LOAD_DEVICE(DestroyImage, DestroyImage);
    LOAD_DEVICE(GetImageMemoryRequirements, GetImageMemoryRequirements);
    LOAD_DEVICE(BindImageMemory, BindImageMemory);
    LOAD_DEVICE(CreateImageView, CreateImageView);
    LOAD_DEVICE(DestroyImageView, DestroyImageView);
    LOAD_DEVICE(CreateSampler, CreateSampler);
    LOAD_DEVICE(DestroySampler, DestroySampler);
    LOAD_DEVICE(CreateShaderModule, CreateShaderModule);
    LOAD_DEVICE(DestroyShaderModule, DestroyShaderModule);
    LOAD_DEVICE(CreateDescriptorSetLayout, CreateDescriptorSetLayout);
    LOAD_DEVICE(DestroyDescriptorSetLayout, DestroyDescriptorSetLayout);
    LOAD_DEVICE(CreatePipelineLayout, CreatePipelineLayout);
    LOAD_DEVICE(DestroyPipelineLayout, DestroyPipelineLayout);
    LOAD_DEVICE(CreateGraphicsPipelines, CreateGraphicsPipelines);
    LOAD_DEVICE(CreateComputePipelines, CreateComputePipelines);
    LOAD_DEVICE(DestroyPipeline, DestroyPipeline);
    LOAD_DEVICE(CreateDescriptorPool, CreateDescriptorPool);
    LOAD_DEVICE(DestroyDescriptorPool, DestroyDescriptorPool);
    LOAD_DEVICE(ResetDescriptorPool, ResetDescriptorPool);
    LOAD_DEVICE(AllocateDescriptorSets, AllocateDescriptorSets);
    LOAD_DEVICE(UpdateDescriptorSets, UpdateDescriptorSets);
    LOAD_DEVICE(CmdPipelineBarrier, CmdPipelineBarrier);
    LOAD_DEVICE(CmdCopyBufferToImage, CmdCopyBufferToImage);
    LOAD_DEVICE(CmdCopyImage, CmdCopyImage);
    LOAD_DEVICE(CmdCopyImageToBuffer, CmdCopyImageToBuffer);
    LOAD_DEVICE(CmdClearDepthStencilImage, CmdClearDepthStencilImage);
    LOAD_DEVICE(CmdBeginRendering, CmdBeginRendering);
    LOAD_DEVICE(CmdEndRendering, CmdEndRendering);
    LOAD_DEVICE(CmdClearAttachments, CmdClearAttachments);
    LOAD_DEVICE(CmdBindPipeline, CmdBindPipeline);
    LOAD_DEVICE(CmdBindIndexBuffer, CmdBindIndexBuffer);
    LOAD_DEVICE(CmdBindVertexBuffers, CmdBindVertexBuffers);
    LOAD_DEVICE(CmdBindDescriptorSets, CmdBindDescriptorSets);
    LOAD_DEVICE(CmdSetViewport, CmdSetViewport);
    LOAD_DEVICE(CmdSetScissor, CmdSetScissor);
    LOAD_DEVICE(CmdSetBlendConstants, CmdSetBlendConstants);
    LOAD_DEVICE(CmdPushConstants, CmdPushConstants);
    LOAD_DEVICE(CmdResetQueryPool, CmdResetQueryPool);
    LOAD_DEVICE(CmdBeginQuery, CmdBeginQuery);
    LOAD_DEVICE(CmdEndQuery, CmdEndQuery);
    LOAD_DEVICE(CmdDraw, CmdDraw);
    LOAD_DEVICE(CmdDrawIndexed, CmdDrawIndexed);
    LOAD_DEVICE(CmdDispatch, CmdDispatch);
    return startup_result(BRUTEFORCE_RENDERER_STARTUP_SUCCESS, "device-functions",
                          VK_SUCCESS, "Vulkan device functions are available");
}

static uint32_t find_memory_type(uint32_t bits, VkMemoryPropertyFlags required,
                                 VkMemoryPropertyFlags preferred)
{
    uint32_t fallback = UINT32_MAX;
    uint32_t i;

    for (i = 0; i < s_vk.memory.memoryTypeCount; ++i) {
        VkMemoryPropertyFlags flags = s_vk.memory.memoryTypes[i].propertyFlags;
        if (!(bits & (1u << i)) || (flags & required) != required)
            continue;
        if ((flags & preferred) == preferred)
            return i;
        if (fallback == UINT32_MAX)
            fallback = i;
    }
    return fallback;
}

static int create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                         VkMemoryPropertyFlags properties, VulkanBuffer *buffer)
{
    VkBufferCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    VkMemoryRequirements requirements;
    VkMemoryAllocateInfo allocation = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    uint32_t type;
    VkResult result;

    memset(buffer, 0, sizeof *buffer);
    info.size = size;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    result = s_vk.fn.CreateBuffer(s_vk.device, &info, NULL, &buffer->buffer);
    if (result != VK_SUCCESS)
        return 0;
    s_vk.fn.GetBufferMemoryRequirements(s_vk.device, buffer->buffer, &requirements);
    type = find_memory_type(requirements.memoryTypeBits, properties, properties);
    if (type == UINT32_MAX)
        return 0;
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = type;
    result = s_vk.fn.AllocateMemory(s_vk.device, &allocation, NULL, &buffer->memory);
    if (result != VK_SUCCESS ||
        s_vk.fn.BindBufferMemory(s_vk.device, buffer->buffer, buffer->memory, 0) != VK_SUCCESS)
        return 0;
    buffer->size = size;
    if (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        result = s_vk.fn.MapMemory(s_vk.device, buffer->memory, 0, size, 0, &buffer->mapped);
        if (result != VK_SUCCESS)
            return 0;
    }
    return 1;
}

static void destroy_buffer(VulkanBuffer *buffer)
{
    if (buffer->mapped)
        s_vk.fn.UnmapMemory(s_vk.device, buffer->memory);
    if (buffer->buffer)
        s_vk.fn.DestroyBuffer(s_vk.device, buffer->buffer, NULL);
    if (buffer->memory)
        s_vk.fn.FreeMemory(s_vk.device, buffer->memory, NULL);
    memset(buffer, 0, sizeof *buffer);
}

static int create_mip_image(uint32_t width, uint32_t height, uint32_t levels,
                            uint32_t layers, int cube, VkFormat format,
                            VkImageUsageFlags usage,
                            VkImageAspectFlags aspect, VulkanImage *image)
{
    VkImageCreateInfo info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    VkMemoryRequirements requirements;
    VkMemoryAllocateInfo allocation = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    VkImageViewCreateInfo view = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    uint32_t type;
    VkResult result;

    memset(image, 0, sizeof *image);
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent.width = width;
    info.extent.height = height;
    info.extent.depth = 1;
    info.mipLevels = levels;
    info.arrayLayers = layers;
    info.flags = cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    result = s_vk.fn.CreateImage(s_vk.device, &info, NULL, &image->image);
    if (result != VK_SUCCESS)
        return 0;
    s_vk.fn.GetImageMemoryRequirements(s_vk.device, image->image, &requirements);
    type = find_memory_type(requirements.memoryTypeBits, 0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (type == UINT32_MAX)
        return 0;
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = type;
    result = s_vk.fn.AllocateMemory(s_vk.device, &allocation, NULL, &image->memory);
    if (result != VK_SUCCESS ||
        s_vk.fn.BindImageMemory(s_vk.device, image->image, image->memory, 0) != VK_SUCCESS)
        return 0;
    view.image = image->image;
    view.viewType = cube ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
    view.format = format;
    view.subresourceRange.aspectMask = aspect;
    view.subresourceRange.levelCount = levels;
    view.subresourceRange.layerCount = layers;
    result = s_vk.fn.CreateImageView(s_vk.device, &view, NULL, &image->view);
    if (result != VK_SUCCESS)
        return 0;
    image->format = format;
    image->layout = VK_IMAGE_LAYOUT_UNDEFINED;
    image->width = width;
    image->height = height;
    image->levels = levels;
    image->layers = layers;
    image->aspect = aspect;
    return 1;
}

static int create_layered_image(uint32_t width, uint32_t height,
                                uint32_t layers, int cube, VkFormat format,
                                VkImageUsageFlags usage,
                                VkImageAspectFlags aspect, VulkanImage *image)
{
    return create_mip_image(width, height, 1, layers, cube, format, usage,
                            aspect, image);
}

static int create_image(uint32_t width, uint32_t height, VkFormat format,
                        VkImageUsageFlags usage, VkImageAspectFlags aspect,
                        VulkanImage *image)
{
    return create_layered_image(width, height, 1, 0, format, usage,
                                aspect, image);
}

static void destroy_image(VulkanImage *image)
{
    if (image->view)
        s_vk.fn.DestroyImageView(s_vk.device, image->view, NULL);
    if (image->image)
        s_vk.fn.DestroyImage(s_vk.device, image->image, NULL);
    if (image->memory)
        s_vk.fn.FreeMemory(s_vk.device, image->memory, NULL);
    memset(image, 0, sizeof *image);
}

static void image_barrier(VkCommandBuffer command, VulkanImage *image,
                          VkImageLayout new_layout,
                          VkPipelineStageFlags source_stage,
                          VkPipelineStageFlags destination_stage,
                          VkAccessFlags source_access,
                          VkAccessFlags destination_access)
{
    VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.srcAccessMask = source_access;
    barrier.dstAccessMask = destination_access;
    barrier.oldLayout = image->layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image->image;
    barrier.subresourceRange.aspectMask = image->aspect;
    barrier.subresourceRange.levelCount = image->levels;
    barrier.subresourceRange.layerCount = image->layers;
    s_vk.fn.CmdPipelineBarrier(command, source_stage, destination_stage, 0,
                               0, NULL, 0, NULL, 1, &barrier);
    image->layout = new_layout;
}

/* Round a logical edge to physical pixels exactly as the D3D11 backend does. */
static uint32_t scale_edge(uint32_t value, uint32_t logical, uint32_t physical)
{
    return logical
        ? (uint32_t)(((uint64_t)value * physical + logical / 2) / logical)
        : 0;
}

static int surface_scaled(const VulkanSurface *surface)
{
    return surface->pw != surface->layout.width ||
           surface->ph != surface->layout.height;
}

static void record_scale_blit(VulkanCommandSlot *slot, VulkanImage *source,
                              uint32_t source_width, uint32_t source_height,
                              VulkanImage *target,
                              uint32_t target_width, uint32_t target_height);

static void resolve_zpass_query(VulkanCommandSlot *slot)
{
    uint64_t counts[VULKAN_BATCH_OPERATIONS];
    uint32_t first_query;
    unsigned i;
    VkResult result;

    if (!slot->zpass_count)
        return;
    first_query = (uint32_t)(slot - s_vk.slots) * VULKAN_BATCH_OPERATIONS;
    result = s_vk.fn.GetQueryPoolResults(
        s_vk.device, s_vk.zpass_query_pool, first_query, slot->zpass_count,
        sizeof counts, counts, sizeof counts[0], VK_QUERY_RESULT_64_BIT);
    if (result != VK_SUCCESS)
        runtime_failure("reading a visibility query", result);
    for (i = 0; i < slot->zpass_count; ++i) {
        double divisor = slot->zpass_divisors[i] > 0.0
                       ? slot->zpass_divisors[i] : 1.0;
        uint64_t count = divisor == 1.0 ? counts[i]
                       : (uint64_t)((double)counts[i] / divisor + 0.5);
        if (s_vk.zpass_async) {
            uint64_t expected =
                s_vk.zpass_result_base + s_vk.zpass_result_count;
            if (slot->zpass_sequences[i] != expected) {
                fprintf(stderr, "[GPU-Vulkan] visibility query result order"
                                " mismatch: expected %llu, received %llu\n",
                        (unsigned long long)expected,
                        (unsigned long long)slot->zpass_sequences[i]);
                abort();
            }
            if (s_vk.zpass_result_count == s_vk.zpass_result_capacity) {
                size_t capacity = s_vk.zpass_result_capacity
                                ? s_vk.zpass_result_capacity * 2 : 256;
                uint64_t *results;
                if (capacity < s_vk.zpass_result_capacity ||
                    capacity > SIZE_MAX / sizeof *results)
                    runtime_failure("growing visibility query results",
                                    VK_ERROR_OUT_OF_HOST_MEMORY);
                results = realloc(s_vk.zpass_results,
                                  capacity * sizeof *results);
                if (!results)
                    runtime_failure("growing visibility query results",
                                    VK_ERROR_OUT_OF_HOST_MEMORY);
                s_vk.zpass_results = results;
                s_vk.zpass_result_capacity = capacity;
            }
            s_vk.zpass_results[s_vk.zpass_result_count++] = count;
        } else if (count > UINT64_MAX - s_vk.zpass_total) {
            s_vk.zpass_total = UINT64_MAX;
        } else {
            s_vk.zpass_total += count;
        }
    }
    slot->zpass_count = 0;
}

static VulkanCommandSlot *find_submission(uint64_t serial)
{
    unsigned i;
    for (i = 0; i < VULKAN_COMMAND_SLOTS; ++i)
        if (s_vk.slots[i].submitted &&
            s_vk.slots[i].submission_serial == serial)
            return &s_vk.slots[i];
    return NULL;
}

static void retire_submission(VulkanCommandSlot *slot)
{
    uint64_t expected = s_vk.completed_submission_serial + 1;
    if (!slot || slot->submission_serial != expected) {
        fprintf(stderr, "[GPU-Vulkan] submission retirement order mismatch:"
                        " expected %llu, received %llu\n",
                (unsigned long long)expected,
                (unsigned long long)(slot ? slot->submission_serial : 0));
        abort();
    }
    slot->submitted = 0;
    resolve_zpass_query(slot);
    s_vk.completed_submission_serial = slot->submission_serial;
    slot->submission_serial = 0;
}

static void retire_through(uint64_t serial)
{
    while (s_vk.completed_submission_serial < serial) {
        VulkanCommandSlot *slot =
            find_submission(s_vk.completed_submission_serial + 1);
        if (!slot) {
            fprintf(stderr, "[GPU-Vulkan] cannot find submission %llu"
                            " while retiring through %llu\n",
                    (unsigned long long)(s_vk.completed_submission_serial + 1),
                    (unsigned long long)serial);
            abort();
        }
        retire_submission(slot);
    }
}

static void wait_for_submission(uint64_t serial)
{
    VulkanCommandSlot *slot;
    uint64_t perf_start;
    if (serial <= s_vk.completed_submission_serial)
        return;
    slot = find_submission(serial);
    if (!slot) {
        fprintf(stderr, "[GPU-Vulkan] cannot find submission %llu to wait for\n",
                (unsigned long long)serial);
        abort();
    }
    s_vk.submission_waits++;
    perf_start = vulkan_perf_begin();
    vk_require(s_vk.fn.WaitForFences(
                   s_vk.device, 1, &slot->fence, VK_TRUE, UINT64_MAX),
               "waiting for submitted work");
    retire_through(serial);
    vulkan_perf_finish_call(
        perf_start, &s_vk.perf.wait_ticks, &s_vk.perf.wait_calls);
}

static void wait_slot(VulkanCommandSlot *slot)
{
    if (slot->submitted)
        wait_for_submission(slot->submission_serial);
}

static void poll_submissions(void)
{
    for (;;) {
        VulkanCommandSlot *slot =
            find_submission(s_vk.completed_submission_serial + 1);
        VkResult result;
        uint64_t perf_start;
        if (!slot)
            return;
        perf_start = vulkan_perf_begin();
        result = s_vk.fn.GetFenceStatus(s_vk.device, slot->fence);
        vulkan_perf_finish_call(
            perf_start, &s_vk.perf.fence_poll_ticks,
            &s_vk.perf.fence_poll_calls);
        if (result == VK_NOT_READY)
            return;
        vk_require(result, "polling submitted work");
        retire_submission(slot);
    }
}

static void reset_slot(VulkanCommandSlot *slot)
{
    unsigned i;
    if (slot->descriptor_pool)
        vk_require(s_vk.fn.ResetDescriptorPool(
                       s_vk.device, slot->descriptor_pool, 0),
                   "resetting a descriptor pool");
    for (i = 0; i < slot->texture_count; ++i)
        destroy_image(&slot->textures[i]);
    for (i = 0; i < slot->upload_count; ++i)
        slot->uploads[i].used = 0;
    slot->texture_count = 0;
    slot->descriptor_count = 0;
    slot->operation_count = 0;
    slot->zpass_count = 0;
}

static void destroy_slot(VulkanCommandSlot *slot)
{
    unsigned i;
    if (slot->descriptor_pool)
        s_vk.fn.DestroyDescriptorPool(s_vk.device, slot->descriptor_pool, NULL);
    for (i = 0; i < slot->texture_count; ++i)
        destroy_image(&slot->textures[i]);
    for (i = 0; i < slot->upload_count; ++i)
        destroy_buffer(&slot->uploads[i].buffer);
    destroy_buffer(&slot->readback);
    free(slot->textures);
    free(slot->uploads);
    slot->textures = NULL;
    slot->uploads = NULL;
    slot->descriptor_pool = VK_NULL_HANDLE;
    slot->texture_count = slot->texture_capacity = 0;
    slot->upload_count = slot->upload_capacity = 0;
}

static void claim_device_thread(void)
{
    LONG current = (LONG)GetCurrentThreadId();
    LONG owner = InterlockedCompareExchange(&s_vk.device_thread, current, 0);
    if (owner && owner != current) {
        fprintf(stderr, "[GPU-Vulkan] device work moved from thread %ld"
                        " to thread %ld; Vulkan object access is not"
                        " externally synchronized\n",
                (long)owner, (long)current);
        abort();
    }
}

static VulkanCommandSlot *begin_commands(void)
{
    VulkanCommandSlot *slot;
    VkCommandBufferBeginInfo begin = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };

    claim_device_thread();
    if (s_vk.recording_slot)
        return s_vk.recording_slot;
    slot = &s_vk.slots[s_vk.next_slot++ % VULKAN_COMMAND_SLOTS];
    wait_slot(slot);
    reset_slot(slot);
    vk_require(s_vk.fn.ResetCommandBuffer(slot->command, 0), "resetting a command buffer");
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vk_require(s_vk.fn.BeginCommandBuffer(slot->command, &begin),
               "beginning a command buffer");
    slot->recording = 1;
    s_vk.recording_slot = slot;
    return slot;
}

static void assign_recorded_reports(uint64_t submission_serial)
{
    uint64_t index;
    if (!s_vk.zpass_async)
        return;
    for (index = s_vk.zpass_report_head;
         index != s_vk.zpass_report_tail; ++index) {
        VulkanVisibilityReport *report =
            &s_vk.zpass_reports[index % VULKAN_MAX_VISIBILITY_REPORTS];
        if (!report->submission_assigned) {
            report->submission_target = submission_serial;
            report->submission_assigned = 1;
        }
    }
}

static void submit_commands(VulkanCommandSlot *slot, int wait)
{
    VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    VkResult result;
    if (!slot || !slot->recording)
        return;
    vk_require(s_vk.fn.EndCommandBuffer(slot->command), "ending a command buffer");
    vk_require(s_vk.fn.ResetFences(s_vk.device, 1, &slot->fence),
               "resetting a submission fence");
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &slot->command;
    bruteforce_vulkan_present_queue_lock();
    result = s_vk.fn.QueueSubmit(s_vk.queue, 1, &submit, slot->fence);
    bruteforce_vulkan_present_queue_unlock();
    vk_require(result, "submitting GPU work");
    s_vk.submissions++;
    if (slot->operation_count > s_vk.max_batch_operations)
        s_vk.max_batch_operations = slot->operation_count;
    slot->recording = 0;
    if (s_vk.recording_slot == slot)
        s_vk.recording_slot = NULL;
    slot->submission_serial = ++s_vk.last_submission_serial;
    assign_recorded_reports(slot->submission_serial);
    slot->submitted = 1;
    if (wait)
        wait_slot(slot);
}

static void finish_commands(VulkanCommandSlot *slot)
{
    slot->operation_count++;
    if (slot->operation_count >= VULKAN_BATCH_OPERATIONS)
        submit_commands(slot, 0);
}

static void flush_recording(void)
{
    if (s_vk.recording_slot)
        submit_commands(s_vk.recording_slot, 0);
}

static VkDeviceSize aligned_offset(VkDeviceSize value, VkDeviceSize alignment)
{
    VkDeviceSize mask;
    if (!alignment)
        alignment = 1;
    mask = alignment - 1;
    if (value > UINT64_MAX - mask)
        runtime_failure("aligning an upload allocation", VK_ERROR_OUT_OF_HOST_MEMORY);
    return (value + mask) & ~mask;
}

static VkDeviceSize upload_chunk_size(VkDeviceSize required)
{
    VkDeviceSize size = VULKAN_UPLOAD_CHUNK_MIN;
    while (size < required) {
        if (size > UINT64_MAX / 2)
            return required;
        size *= 2;
    }
    return size;
}

static VulkanUpload slot_upload(VulkanCommandSlot *slot, VkDeviceSize size,
                                VkDeviceSize alignment)
{
    static const VkBufferUsageFlags usage =
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VulkanUpload result;
    VulkanUploadChunk *chunk = NULL;
    VkDeviceSize offset;
    unsigned i;

    memset(&result, 0, sizeof result);
    for (i = 0; i < slot->upload_count; ++i) {
        offset = aligned_offset(slot->uploads[i].used, alignment);
        if (offset <= slot->uploads[i].buffer.size &&
            size <= slot->uploads[i].buffer.size - offset) {
            chunk = &slot->uploads[i];
            break;
        }
    }
    if (!chunk) {
        if (slot->upload_count == slot->upload_capacity) {
            unsigned capacity = slot->upload_capacity ? slot->upload_capacity * 2 : 2;
            VulkanUploadChunk *uploads;
            if (capacity < slot->upload_capacity)
                runtime_failure("growing upload chunks", VK_ERROR_OUT_OF_HOST_MEMORY);
            uploads = realloc(slot->uploads, capacity * sizeof *uploads);
            if (!uploads)
                runtime_failure("growing upload chunks", VK_ERROR_OUT_OF_HOST_MEMORY);
            memset(uploads + slot->upload_capacity, 0,
                   (capacity - slot->upload_capacity) * sizeof *uploads);
            slot->uploads = uploads;
            slot->upload_capacity = capacity;
        }
        chunk = &slot->uploads[slot->upload_count++];
        if (!create_buffer(upload_chunk_size(size), usage,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                           &chunk->buffer))
            runtime_failure("creating an upload buffer", VK_ERROR_OUT_OF_DEVICE_MEMORY);
        s_vk.upload_buffer_creates++;
        offset = 0;
    } else {
        offset = aligned_offset(chunk->used, alignment);
    }
    chunk->used = offset + size;
    result.buffer = chunk->buffer.buffer;
    result.offset = offset;
    result.mapped = (uint8_t *)chunk->buffer.mapped + offset;
    return result;
}

static VulkanBuffer *slot_readback(VulkanCommandSlot *slot, VkDeviceSize size)
{
    if (slot->readback.buffer && slot->readback.size < size)
        destroy_buffer(&slot->readback);
    if (!slot->readback.buffer &&
        !create_buffer(upload_chunk_size(size), VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                       &slot->readback))
        runtime_failure("creating a readback buffer", VK_ERROR_OUT_OF_DEVICE_MEMORY);
    return &slot->readback;
}

static VulkanImage *slot_texture(VulkanCommandSlot *slot)
{
    if (slot->texture_count == slot->texture_capacity) {
        unsigned capacity = slot->texture_capacity ? slot->texture_capacity * 2 : 4;
        VulkanImage *textures;
        if (capacity < slot->texture_capacity)
            runtime_failure("growing transient texture images",
                            VK_ERROR_OUT_OF_HOST_MEMORY);
        textures = realloc(slot->textures, capacity * sizeof *textures);
        if (!textures)
            runtime_failure("growing transient texture images",
                            VK_ERROR_OUT_OF_HOST_MEMORY);
        memset(textures + slot->texture_capacity, 0,
               (capacity - slot->texture_capacity) * sizeof *textures);
        slot->textures = textures;
        slot->texture_capacity = capacity;
    }
    return &slot->textures[slot->texture_count++];
}

static int readable_guest_range(uint32_t va, size_t bytes, int write)
{
    const DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                           PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                           PAGE_EXECUTE_WRITECOPY;
    const DWORD writable = PAGE_READWRITE | PAGE_WRITECOPY |
                           PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    uintptr_t offset = (uintptr_t)xbox_GetMemoryOffset();
    uintptr_t current, end;
    uint64_t guest_end = (uint64_t)va + bytes;

    if (!va || !bytes || guest_end > UINT32_MAX || offset > UINTPTR_MAX - va)
        return 0;
    current = offset + va;
    if (bytes > UINTPTR_MAX - current)
        return 0;
    end = current + bytes;
    while (current < end) {
        MEMORY_BASIC_INFORMATION info;
        uintptr_t base;
        uintptr_t available;
        DWORD accepted = write ? writable : readable;
        if (VirtualQuery((const void *)current, &info, sizeof info) != sizeof info ||
            info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
            !(info.Protect & accepted))
            return 0;
        base = (uintptr_t)info.BaseAddress;
        if (current < base || current - base >= info.RegionSize)
            return 0;
        available = info.RegionSize - (current - base);
        current += available < end - current ? available : end - current;
    }
    return 1;
}

static int guarded_guest_write(void *destination, const void *source, size_t bytes)
{
    __try {
        memcpy(destination, source, bytes);
        return 1;
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ||
                GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR ||
                GetExceptionCode() == EXCEPTION_GUARD_PAGE
                    ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return 0;
    }
}

static void upload_regions_to_image(VulkanCommandSlot *slot,
                                    VulkanImage *image,
                                    const void *pixels, size_t bytes,
                                    VkBufferImageCopy *regions,
                                    uint32_t region_count)
{
    VulkanUpload staging = slot_upload(slot, bytes, 4);
    uint32_t i;
    memcpy(staging.mapped, pixels, bytes);
    for (i = 0; i < region_count; ++i)
        regions[i].bufferOffset += staging.offset;
    image_barrier(slot->command, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                  0, VK_ACCESS_TRANSFER_WRITE_BIT);
    s_vk.fn.CmdCopyBufferToImage(slot->command, staging.buffer, image->image,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                 region_count, regions);
}

static void upload_to_image(VulkanCommandSlot *slot, VulkanImage *image,
                            const void *pixels, size_t bytes)
{
    VkBufferImageCopy copy;
    memset(&copy, 0, sizeof copy);
    copy.imageSubresource.aspectMask = image->aspect;
    copy.imageSubresource.layerCount = image->layers;
    copy.imageExtent.width = image->width;
    copy.imageExtent.height = image->height;
    copy.imageExtent.depth = 1;
    upload_regions_to_image(slot, image, pixels, bytes, &copy, 1);
}

static VulkanSurface *find_surface(uint32_t va)
{
    unsigned i;
    for (i = 0; i < s_vk.surface_count; ++i)
        if (s_vk.surfaces[i].va == va)
            return &s_vk.surfaces[i];
    return NULL;
}

static VulkanSurface *find_texture_surface_alias(const Nv2aTexture *texture,
                                                 const GpuTextureLayout *layout)
{
    uint64_t texture_span;
    uint64_t texture_start;
    uint64_t texture_end;
    unsigned i;

    if (!texture || !layout)
        return NULL;
    texture_span = layout->span;
    texture_start = texture->offset;
    texture_end = texture_span > UINT64_MAX - texture_start
                ? UINT64_MAX : texture_start + texture_span;
    for (i = 0; i < s_vk.surface_count; ++i) {
        VulkanSurface *surface = &s_vk.surfaces[i];
        uint64_t surface_start = surface->va;
        uint64_t surface_end = surface_start + surface->layout.span_bytes;
        if (texture_start < surface_end && surface_start < texture_end)
            return surface;
    }
    return NULL;
}

static VkPipelineStageFlags image_layout_stage(VkImageLayout layout);
static VkAccessFlags image_layout_access(VkImageLayout layout);

static int ensure_surface_probe(size_t bytes)
{
    uint8_t *probe;

    if (bytes <= s_vk.surface_probe_capacity)
        return 1;
    probe = realloc(s_vk.surface_probe, bytes);
    if (!probe)
        return 0;
    s_vk.surface_probe = probe;
    s_vk.surface_probe_capacity = bytes;
    return 1;
}

static int ensure_surface_decode(size_t texels)
{
    uint32_t *decoded;

    if (texels <= s_vk.surface_decode_capacity)
        return 1;
    if (texels > SIZE_MAX / sizeof *decoded)
        return 0;
    decoded = realloc(s_vk.surface_decode, texels * sizeof *decoded);
    if (!decoded)
        return 0;
    s_vk.surface_decode = decoded;
    s_vk.surface_decode_capacity = texels;
    return 1;
}

static int same_surface_guest_identity(const GuestSpanIdentity *a,
                                       const GuestSpanIdentity *b)
{
    return a->layout == b->layout &&
           a->allocation == b->allocation &&
           a->bounds == b->bounds;
}

static int snapshot_surface_guest(VulkanSurface *surface,
                                  GuestSpanIdentity *identity)
{
    GuestCopyResult result;

    if (!ensure_surface_probe(surface->layout.span_bytes))
        return 0;
    result = gpu_guest_copy(surface->va, surface->layout.span_bytes,
                            s_vk.surface_probe,
                            GUEST_BOUNDS_TRACK_IF_REGISTERED, identity);
    if (result != GUEST_COPY_OK) {
        fprintf(stderr, "[GPU-Vulkan] cannot snapshot guest color surface"
                        " %08X (result %d)\n", surface->va, (int)result);
        return 0;
    }
    return 1;
}

static void remember_surface_guest(VulkanSurface *surface,
                                   const GuestSpanIdentity *identity)
{
    memcpy(surface->guest_shadow, s_vk.surface_probe,
           surface->guest_shadow_bytes);
    surface->guest_identity = *identity;
    surface->guest_check_flip = s_vk.flips;
}

static int decode_surface_guest(VulkanSurface *surface)
{
    size_t texels = (size_t)surface->layout.width * surface->layout.height;
    uint32_t y;

    if (!ensure_surface_decode(texels))
        return 0;
    for (y = 0; y < surface->layout.height; ++y) {
        const uint8_t *source =
            surface->guest_shadow + (size_t)y * surface->layout.pitch;
        uint32_t *destination =
            s_vk.surface_decode + (size_t)y * surface->layout.width;
        uint32_t x;
        if (surface->layout.bytes_per_pixel == 4) {
            memcpy(destination, source,
                   (size_t)surface->layout.width * sizeof *destination);
            continue;
        }
        for (x = 0; x < surface->layout.width; ++x) {
            uint16_t pixel;
            uint32_t r, g, b;
            memcpy(&pixel, source + (size_t)x * 2, sizeof pixel);
            r = (pixel >> 11) & 0x1Fu;
            g = (pixel >> 5) & 0x3Fu;
            b = pixel & 0x1Fu;
            r = (r << 3) | (r >> 2);
            g = (g << 2) | (g >> 4);
            b = (b << 3) | (b >> 2);
            destination[x] = 0xFF000000u | (r << 16) | (g << 8) | b;
        }
    }
    return 1;
}

static void ensure_surface_logical(VulkanSurface *surface)
{
    if (surface->logical.image)
        return;
    if (!create_image(surface->layout.width, surface->layout.height,
                      VK_FORMAT_B8G8R8A8_UNORM,
                      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                      VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                      VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                      VK_IMAGE_USAGE_SAMPLED_BIT,
                      VK_IMAGE_ASPECT_COLOR_BIT, &surface->logical))
        runtime_failure("creating a logical color resolve surface",
                        VK_ERROR_OUT_OF_DEVICE_MEMORY);
}

static int upload_surface_guest(VulkanSurface *surface,
                                VulkanCommandSlot *slot)
{
    size_t texels = (size_t)surface->layout.width * surface->layout.height;
    VkImageLayout old_layout = surface->image.layout;

    if (!decode_surface_guest(surface))
        return 0;
    if (surface_scaled(surface)) {
        /*
         * Guest memory holds the logical picture. Upload it at guest size and
         * resample it into the physical target, as the D3D11 seed does.
         */
        ensure_surface_logical(surface);
        old_layout = surface->logical.layout;
        if (old_layout != VK_IMAGE_LAYOUT_UNDEFINED)
            image_barrier(slot->command, &surface->logical,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          image_layout_stage(old_layout),
                          VK_PIPELINE_STAGE_TRANSFER_BIT,
                          image_layout_access(old_layout),
                          VK_ACCESS_TRANSFER_WRITE_BIT);
        upload_to_image(slot, &surface->logical, s_vk.surface_decode,
                        texels * sizeof *s_vk.surface_decode);
        image_barrier(slot->command, &surface->logical,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      VK_PIPELINE_STAGE_TRANSFER_BIT,
                      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                      VK_ACCESS_TRANSFER_WRITE_BIT,
                      VK_ACCESS_SHADER_READ_BIT);
        record_scale_blit(slot, &surface->logical,
                          surface->layout.width, surface->layout.height,
                          &surface->image, surface->pw, surface->ph);
        surface->guest_pending_upload = 0;
        s_vk.guest_surface_uploads++;
        return 1;
    }
    if (old_layout != VK_IMAGE_LAYOUT_UNDEFINED)
        image_barrier(slot->command, &surface->image,
                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      image_layout_stage(old_layout),
                      VK_PIPELINE_STAGE_TRANSFER_BIT,
                      image_layout_access(old_layout),
                      VK_ACCESS_TRANSFER_WRITE_BIT);
    upload_to_image(slot, &surface->image, s_vk.surface_decode,
                    texels * sizeof *s_vk.surface_decode);
    image_barrier(slot->command, &surface->image,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_TRANSFER_WRITE_BIT,
                  VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    surface->guest_pending_upload = 0;
    s_vk.guest_surface_uploads++;
    return 1;
}

static int sync_surface_guest(VulkanSurface *surface,
                              VulkanCommandSlot *slot, int force,
                              int upload)
{
    GuestSpanIdentity identity;
    int identity_changed;
    int bytes_changed;
    int changed;

    if (!force && surface->guest_check_flip == s_vk.flips) {
        if (upload && surface->guest_pending_upload &&
            !upload_surface_guest(surface, slot))
            return 0;
        return 1;
    }
    if (!snapshot_surface_guest(surface, &identity))
        return 0;
    identity_changed =
        !same_surface_guest_identity(&surface->guest_identity, &identity);
    bytes_changed =
        memcmp(surface->guest_shadow, s_vk.surface_probe,
               surface->guest_shadow_bytes) != 0;
    changed = identity_changed || bytes_changed;
    surface->guest_check_flip = s_vk.flips;
    if (changed) {
        if (surface->gpu_dirty) {
            static unsigned reports;
            size_t first = SIZE_MAX;
            size_t last = 0;
            size_t count = 0;
            size_t i;

            if (identity_changed) {
                fprintf(stderr, "[GPU-Vulkan] guest allocation identity changed"
                                " for dirty color surface %08X\n", surface->va);
                return 0;
            }
            for (i = 0; i < surface->guest_shadow_bytes; ++i) {
                if (surface->guest_shadow[i] == s_vk.surface_probe[i])
                    continue;
                if (first == SIZE_MAX)
                    first = i;
                last = i;
                count++;
            }
            s_vk.guest_surface_conflicts++;
            if (reports++ < 16) {
                fprintf(stderr, "[GPU-Vulkan] guest writes raced GPU color work"
                                " on surface %08X at flip %llu; changed %zu bytes"
                                " in [%zu,%zu] of %zu (%ux%u pitch %u bpp %u);"
                                " preserving the GPU frame%s\n",
                        surface->va, (unsigned long long)s_vk.flips, count,
                        first == SIZE_MAX ? 0 : first,
                        first == SIZE_MAX ? 0 : last,
                        surface->guest_shadow_bytes,
                        surface->layout.width, surface->layout.height,
                        surface->layout.pitch, surface->layout.bytes_per_pixel,
                        reports == 16 ? " (further reports suppressed)" : "");
            }
            return 2;
        }
        remember_surface_guest(surface, &identity);
        surface->guest_pending_upload = 1;
        s_vk.guest_surface_preserves++;
    }
    if (upload && surface->guest_pending_upload &&
        !upload_surface_guest(surface, slot))
        return 0;
    return 1;
}

static VulkanSurface *get_surface(const Nv2aSurface *input, VulkanCommandSlot *slot)
{
    GpuSurfaceLayout layout;
    VulkanSurface *surface = find_surface(input ? input->color_va : 0);
    GuestSpanIdentity identity;
    uint32_t aa_sx;
    uint32_t aa_sy;
    uint32_t pw;
    uint32_t ph;

    if (!input || !input->color_va || !gpu_surface_layout(input, &layout) ||
        input->pitch % layout.bytes_per_pixel != 0 ||
        layout.width > 16384 || layout.height > 16384) {
        fprintf(stderr, "[GPU-Vulkan] invalid color surface layout\n");
        abort();
    }
    aa_sx = input->aa_sx > 1 ? input->aa_sx : 1;
    aa_sy = input->aa_sy > 1 ? input->aa_sy : 1;
    if (!gpu_scale_surface_size(layout.width, layout.height, aa_sx, aa_sy,
                                &pw, &ph) ||
        pw > s_vk.properties.limits.maxImageDimension2D ||
        ph > s_vk.properties.limits.maxImageDimension2D) {
        fprintf(stderr, "[GPU-Vulkan] color surface %08X (%ux%u, AA %ux%u)"
                        " has no supported physical size at render scale"
                        " %.3f\n", input->color_va, layout.width,
                layout.height, aa_sx, aa_sy, gpu_scale_value());
        abort();
    }
    if (surface) {
        if (surface->layout.width != layout.width ||
            surface->layout.height != layout.height ||
            surface->layout.pitch != layout.pitch ||
            surface->layout.bytes_per_pixel != layout.bytes_per_pixel ||
            surface->pw != pw || surface->ph != ph) {
            fprintf(stderr, "[GPU-Vulkan] color surface %08X changed layout during"
                            " bring-up; safe migration is not implemented\n",
                    input->color_va);
            abort();
        }
        if (!sync_surface_guest(surface, slot, 0, 1)) {
            fprintf(stderr, "[GPU-Vulkan] cannot synchronize guest color"
                            " surface %08X\n", input->color_va);
            abort();
        }
        return surface;
    }
    if (s_vk.surface_count == VULKAN_MAX_SURFACES) {
        fprintf(stderr, "[GPU-Vulkan] color surface limit reached\n");
        abort();
    }
    surface = &s_vk.surfaces[s_vk.surface_count++];
    memset(surface, 0, sizeof *surface);
    surface->va = input->color_va;
    surface->layout = layout;
    surface->pw = pw;
    surface->ph = ph;
    surface->guest_shadow_bytes = layout.span_bytes;
    surface->guest_shadow = malloc(surface->guest_shadow_bytes);
    if (!surface->guest_shadow ||
        !snapshot_surface_guest(surface, &identity)) {
        fprintf(stderr, "[GPU-Vulkan] cannot allocate or initialize color"
                        " surface snapshot\n");
        abort();
    }
    remember_surface_guest(surface, &identity);
    if (!create_image(pw, ph, VK_FORMAT_B8G8R8A8_UNORM,
                      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                      VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                      VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                      VK_IMAGE_USAGE_SAMPLED_BIT,
                      VK_IMAGE_ASPECT_COLOR_BIT, &surface->image)) {
        runtime_failure("creating a color surface", VK_ERROR_OUT_OF_DEVICE_MEMORY);
    }
    if (!upload_surface_guest(surface, slot))
        runtime_failure("uploading a guest color surface",
                        VK_ERROR_OUT_OF_HOST_MEMORY);
    return surface;
}

static VkFormat current_depth_format(void)
{
    uint32_t format = (nv2a_backend_reg(0x0208) >> 4) & 0xFu;
    return format == 1 ? s_vk.depth16_format : s_vk.depth_stencil_format;
}

static VulkanDepth *get_depth(const Nv2aRenderState *state,
                              const VulkanSurface *surface,
                              VulkanCommandSlot *slot)
{
    VulkanDepth *depth = NULL;
    VkClearDepthStencilValue clear = { 1.0f, 0 };
    VkImageSubresourceRange range;
    VkFormat format;
    VkImageAspectFlags aspect;
    unsigned i;

    if (!state || !state->zeta_va)
        return NULL;
    for (i = 0; i < s_vk.depth_count; ++i)
        if (s_vk.depths[i].va == state->zeta_va) {
            depth = &s_vk.depths[i];
            break;
        }
    format = current_depth_format();
    aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (format == VK_FORMAT_D24_UNORM_S8_UINT ||
        format == VK_FORMAT_D32_SFLOAT_S8_UINT)
        aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
    if (depth) {
        if (depth->width != surface->pw ||
            depth->height != surface->ph ||
            depth->image.format != format) {
            fprintf(stderr, "[GPU-Vulkan] depth surface %08X changed layout during"
                            " bring-up; safe migration is not implemented\n",
                    state->zeta_va);
            abort();
        }
        return depth;
    }
    if (s_vk.depth_count == VULKAN_MAX_DEPTHS) {
        fprintf(stderr, "[GPU-Vulkan] depth surface limit reached\n");
        abort();
    }
    depth = &s_vk.depths[s_vk.depth_count++];
    memset(depth, 0, sizeof *depth);
    depth->va = state->zeta_va;
    depth->width = surface->pw;
    depth->height = surface->ph;
    if (!create_image(depth->width, depth->height, format,
                      VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                      VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                      aspect, &depth->image))
        runtime_failure("creating a depth surface", VK_ERROR_OUT_OF_DEVICE_MEMORY);
    image_barrier(slot->command, &depth->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                  0, VK_ACCESS_TRANSFER_WRITE_BIT);
    memset(&range, 0, sizeof range);
    range.aspectMask = aspect;
    range.levelCount = 1;
    range.layerCount = 1;
    s_vk.fn.CmdClearDepthStencilImage(slot->command, depth->image.image,
                                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                      &clear, 1, &range);
    image_barrier(slot->command, &depth->image,
                  VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                  VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                  VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                  VK_ACCESS_TRANSFER_WRITE_BIT,
                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    return depth;
}

static int map_compare(uint32_t value, VkCompareOp *operation)
{
    if (value < 0x200u || value > 0x207u)
        return 0;
    *operation = (VkCompareOp)(VK_COMPARE_OP_NEVER + (value - 0x200u));
    return 1;
}

static int map_stencil(uint32_t value, VkStencilOp *operation)
{
    switch (value) {
    case 0: *operation = VK_STENCIL_OP_ZERO; return 1;
    case 0x1E00: *operation = VK_STENCIL_OP_KEEP; return 1;
    case 0x1E01: *operation = VK_STENCIL_OP_REPLACE; return 1;
    case 0x1E02: *operation = VK_STENCIL_OP_INCREMENT_AND_CLAMP; return 1;
    case 0x1E03: *operation = VK_STENCIL_OP_DECREMENT_AND_CLAMP; return 1;
    case 0x150A: *operation = VK_STENCIL_OP_INVERT; return 1;
    case 0x8507: *operation = VK_STENCIL_OP_INCREMENT_AND_WRAP; return 1;
    case 0x8508: *operation = VK_STENCIL_OP_DECREMENT_AND_WRAP; return 1;
    default: return 0;
    }
}

static int map_blend_factor(uint32_t value, VkBlendFactor *factor)
{
    switch (value) {
    case 0: *factor = VK_BLEND_FACTOR_ZERO; return 1;
    case 1: *factor = VK_BLEND_FACTOR_ONE; return 1;
    case 0x300: *factor = VK_BLEND_FACTOR_SRC_COLOR; return 1;
    case 0x301: *factor = VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR; return 1;
    case 0x302: *factor = VK_BLEND_FACTOR_SRC_ALPHA; return 1;
    case 0x303: *factor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; return 1;
    case 0x304: *factor = VK_BLEND_FACTOR_DST_ALPHA; return 1;
    case 0x305: *factor = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA; return 1;
    case 0x306: *factor = VK_BLEND_FACTOR_DST_COLOR; return 1;
    case 0x307: *factor = VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR; return 1;
    case 0x308: *factor = VK_BLEND_FACTOR_SRC_ALPHA_SATURATE; return 1;
    case 0x8001: *factor = VK_BLEND_FACTOR_CONSTANT_COLOR; return 1;
    case 0x8002: *factor = VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR; return 1;
    case 0x8003: *factor = VK_BLEND_FACTOR_CONSTANT_ALPHA; return 1;
    case 0x8004: *factor = VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA; return 1;
    default: return 0;
    }
}

static int map_blend_op(uint32_t value, VkBlendOp *operation)
{
    switch (value) {
    case 0x8006: *operation = VK_BLEND_OP_ADD; return 1;
    case 0x800A: *operation = VK_BLEND_OP_SUBTRACT; return 1;
    case 0x800B: *operation = VK_BLEND_OP_REVERSE_SUBTRACT; return 1;
    case 0x8007: *operation = VK_BLEND_OP_MIN; return 1;
    case 0x8008: *operation = VK_BLEND_OP_MAX; return 1;
    default: return 0;
    }
}

static VkColorComponentFlags color_mask(uint32_t mask)
{
    VkColorComponentFlags result = 0;
    if (mask & 0x00010000u) result |= VK_COLOR_COMPONENT_R_BIT;
    if (mask & 0x00000100u) result |= VK_COLOR_COMPONENT_G_BIT;
    if (mask & 0x00000001u) result |= VK_COLOR_COMPONENT_B_BIT;
    if (mask & 0x01000000u) result |= VK_COLOR_COMPONENT_A_BIT;
    return result;
}

static int pipeline_key(const Nv2aRenderState *state, VkFormat color,
                        VkFormat depth, int clear_shader,
                        VkColorComponentFlags clear_mask,
                        VulkanPipelineKey *key)
{
    memset(key, 0, sizeof *key);
    key->color_format = color;
    key->depth_format = depth;
    key->stencil_format =
        depth == VK_FORMAT_D24_UNORM_S8_UINT ||
        depth == VK_FORMAT_D32_SFLOAT_S8_UINT ? depth : VK_FORMAT_UNDEFINED;
    key->src_color = key->src_alpha = VK_BLEND_FACTOR_ONE;
    key->dst_color = key->dst_alpha = VK_BLEND_FACTOR_ZERO;
    key->color_op = key->alpha_op = VK_BLEND_OP_ADD;
    key->depth_compare = VK_COMPARE_OP_ALWAYS;
    key->stencil_compare = VK_COMPARE_OP_ALWAYS;
    key->stencil_fail = key->stencil_depth_fail = key->stencil_pass = VK_STENCIL_OP_KEEP;
    key->stencil_compare_mask = key->stencil_write_mask = 0xFF;
    key->color_mask = clear_shader ? clear_mask :
                      state ? color_mask(state->color_mask) :
                      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    key->front_face = VK_FRONT_FACE_CLOCKWISE;
    key->clear_shader = (uint8_t)clear_shader;
    if (clear_shader || !state)
        return 1;
    if (state->blend_enable) {
        if (!map_blend_factor(state->blend_src, &key->src_color) ||
            !map_blend_factor(state->blend_dst, &key->dst_color) ||
            !map_blend_op(state->blend_eq, &key->color_op))
            return 0;
        key->src_alpha = key->src_color;
        key->dst_alpha = key->dst_color;
        key->alpha_op = key->color_op;
        key->blend_enable = 1;
    }
    if (depth != VK_FORMAT_UNDEFINED) {
        if (!map_compare(state->depth_func, &key->depth_compare))
            return 0;
        key->depth_test = state->depth_test_enable != 0;
        key->depth_write = state->depth_write != 0;
        if (state->stencil_enable && key->stencil_format != VK_FORMAT_UNDEFINED) {
            if (!map_compare(state->stencil_func, &key->stencil_compare) ||
                !map_stencil(state->stencil_fail, &key->stencil_fail) ||
                !map_stencil(state->stencil_zfail, &key->stencil_depth_fail) ||
                !map_stencil(state->stencil_zpass, &key->stencil_pass))
                return 0;
            key->stencil_compare_mask = state->stencil_func_mask;
            key->stencil_write_mask = state->stencil_write_mask;
            key->stencil_reference = state->stencil_ref & 0xFFu;
            key->stencil_enable = 1;
        }
    }
    if (state->cull_enable) {
        if (state->cull_face == 0x404)
            key->cull_mode = VK_CULL_MODE_FRONT_BIT;
        else if (state->cull_face == 0x405)
            key->cull_mode = VK_CULL_MODE_BACK_BIT;
        else if (state->cull_face == 0x408)
            key->cull_mode = VK_CULL_MODE_FRONT_AND_BACK;
        else
            return 0;
    }
    if (state->front_face == 0x900)
        key->front_face = VK_FRONT_FACE_CLOCKWISE;
    else if (state->front_face == 0x901)
        key->front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    else
        return 0;
    return 1;
}

static VkPipeline create_pipeline(const VulkanPipelineKey *key, VkResult *creation_result)
{
    VkPipelineShaderStageCreateInfo stages[3];
    VkVertexInputBindingDescription binding;
    VkVertexInputAttributeDescription attributes[8];
    VkPipelineVertexInputStateCreateInfo vertices = {
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO
    };
    VkPipelineInputAssemblyStateCreateInfo assembly = {
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO
    };
    VkPipelineViewportStateCreateInfo viewport = {
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO
    };
    VkPipelineRasterizationStateCreateInfo raster = {
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO
    };
    VkPipelineMultisampleStateCreateInfo multisample = {
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO
    };
    VkPipelineDepthStencilStateCreateInfo depth = {
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO
    };
    VkPipelineColorBlendAttachmentState attachment;
    VkPipelineColorBlendStateCreateInfo blend = {
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO
    };
    VkDynamicState dynamic_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_BLEND_CONSTANTS,
    };
    VkPipelineDynamicStateCreateInfo dynamic = {
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO
    };
    VkPipelineRenderingCreateInfo rendering = {
        VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO
    };
    VkGraphicsPipelineCreateInfo info = {
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO
    };
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkStencilOpState stencil;
    VkResult result;
    unsigned fragment_stage = key->vsh_shader ? 2 : 1;

    memset(stages, 0, sizeof stages);
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = key->resolve_shader ? s_vk.resolve_vs :
                       key->vsh_shader ? s_vk.vsh_vs :
                       key->clear_shader ? s_vk.clear_vs :
                       key->extended_shader ? s_vk.extended_vs : s_vk.base_vs;
    stages[0].pName = key->resolve_shader ? "resolve_vs" :
                      key->vsh_shader ? "vsh_vs" :
                      key->clear_shader ? "clear_vs" :
                      key->extended_shader ? "extended_vs" : "base_vs";
    if (key->vsh_shader) {
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_GEOMETRY_BIT;
        stages[1].module = s_vk.vsh_gs;
        stages[1].pName = "vsh_gs";
    }
    stages[fragment_stage].sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[fragment_stage].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[fragment_stage].module =
        key->resolve_shader == 2 ? s_vk.resolve_box_ps :
        key->resolve_shader ? s_vk.resolve_bilinear_ps :
        key->clear_shader ? s_vk.clear_ps :
        key->extended_shader ? s_vk.extended_ps : s_vk.base_ps;
    stages[fragment_stage].pName =
        key->resolve_shader == 2 ? "resolve_box_ps" :
        key->resolve_shader ? "resolve_bilinear_ps" :
        key->clear_shader ? "clear_ps" :
        key->extended_shader ? "extended_ps" : "base_ps";

    memset(&binding, 0, sizeof binding);
    binding.binding = 0;
    binding.stride = key->extended_shader ? sizeof(Nv2aXVertex)
                                          : sizeof(VulkanVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    memset(attributes, 0, sizeof attributes);
    attributes[0].location = 0;
    attributes[0].binding = 0;
    attributes[0].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributes[0].offset = offsetof(VulkanVertex, position);
    attributes[1].location = 1;
    attributes[1].binding = 0;
    attributes[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributes[1].offset = offsetof(VulkanVertex, color);
    attributes[2].location = 2;
    attributes[2].binding = 0;
    if (key->extended_shader) {
        attributes[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributes[2].offset = offsetof(Nv2aXVertex, d1);
        attributes[3].location = 3;
        attributes[3].binding = 0;
        attributes[3].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributes[3].offset = offsetof(Nv2aXVertex, fog);
        attributes[4].location = 4;
        attributes[4].binding = 0;
        attributes[4].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributes[4].offset = offsetof(Nv2aXVertex, tc[0]);
        attributes[5].location = 5;
        attributes[5].binding = 0;
        attributes[5].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributes[5].offset = offsetof(Nv2aXVertex, tc[1]);
        attributes[6].location = 6;
        attributes[6].binding = 0;
        attributes[6].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributes[6].offset = offsetof(Nv2aXVertex, tc[2]);
        attributes[7].location = 7;
        attributes[7].binding = 0;
        attributes[7].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributes[7].offset = offsetof(Nv2aXVertex, tc[3]);
    } else {
        attributes[2].format = VK_FORMAT_R32G32_SFLOAT;
        attributes[2].offset = offsetof(VulkanVertex, uv);
    }
    if (!key->clear_shader && !key->vsh_shader && !key->resolve_shader) {
        vertices.vertexBindingDescriptionCount = 1;
        vertices.pVertexBindingDescriptions = &binding;
        vertices.vertexAttributeDescriptionCount = key->extended_shader ? 8 : 3;
        vertices.pVertexAttributeDescriptions = attributes;
    }
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = key->cull_mode;
    raster.frontFace = key->front_face;
    raster.lineWidth = 1.0f;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    depth.depthTestEnable = key->depth_test;
    depth.depthWriteEnable = key->depth_write;
    depth.depthCompareOp = key->depth_compare;
    depth.stencilTestEnable = key->stencil_enable;
    memset(&stencil, 0, sizeof stencil);
    stencil.failOp = key->stencil_fail;
    stencil.passOp = key->stencil_pass;
    stencil.depthFailOp = key->stencil_depth_fail;
    stencil.compareOp = key->stencil_compare;
    stencil.compareMask = key->stencil_compare_mask;
    stencil.writeMask = key->stencil_write_mask;
    stencil.reference = key->stencil_reference;
    depth.front = depth.back = stencil;
    memset(&attachment, 0, sizeof attachment);
    attachment.blendEnable = key->blend_enable;
    attachment.srcColorBlendFactor = key->src_color;
    attachment.dstColorBlendFactor = key->dst_color;
    attachment.colorBlendOp = key->color_op;
    attachment.srcAlphaBlendFactor = key->src_alpha;
    attachment.dstAlphaBlendFactor = key->dst_alpha;
    attachment.alphaBlendOp = key->alpha_op;
    attachment.colorWriteMask = key->color_mask;
    blend.attachmentCount = 1;
    blend.pAttachments = &attachment;
    dynamic.dynamicStateCount = sizeof dynamic_states / sizeof dynamic_states[0];
    dynamic.pDynamicStates = dynamic_states;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &key->color_format;
    rendering.depthAttachmentFormat = key->depth_format;
    rendering.stencilAttachmentFormat = key->stencil_format;
    info.pNext = &rendering;
    info.stageCount = key->vsh_shader ? 3 : 2;
    info.pStages = stages;
    info.pVertexInputState = &vertices;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = s_vk.pipeline_layout;
    result = test_failure("graphics-pipeline")
           ? VK_ERROR_INITIALIZATION_FAILED
           : s_vk.fn.CreateGraphicsPipelines(s_vk.device, VK_NULL_HANDLE, 1,
                                             &info, NULL, &pipeline);
    if (creation_result)
        *creation_result = result;
    if (result != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return pipeline;
}

static VkPipeline cache_pipeline(const VulkanPipelineKey *key, VkResult *creation_result)
{
    VkPipeline pipeline;
    unsigned i;
    if (creation_result)
        *creation_result = VK_SUCCESS;
    for (i = 0; i < s_vk.pipeline_count; ++i)
        if (memcmp(&s_vk.pipelines[i].key, key, sizeof *key) == 0)
            return s_vk.pipelines[i].pipeline;
    if (s_vk.pipeline_count == VULKAN_MAX_PIPELINES) {
        if (creation_result)
            *creation_result = VK_ERROR_TOO_MANY_OBJECTS;
        return VK_NULL_HANDLE;
    }
    pipeline = create_pipeline(key, creation_result);
    if (!pipeline)
        return VK_NULL_HANDLE;
    s_vk.pipelines[s_vk.pipeline_count].key = *key;
    s_vk.pipelines[s_vk.pipeline_count].pipeline = pipeline;
    s_vk.pipeline_count++;
    return pipeline;
}

static VkPipeline get_pipeline(const VulkanPipelineKey *key)
{
    VkResult result;
    VkPipeline pipeline = cache_pipeline(key, &result);
    if (!pipeline)
        runtime_failure("creating a graphics pipeline", result);
    return pipeline;
}

static void set_viewport_scissor(VkCommandBuffer command,
                                 const VulkanSurface *surface,
                                 const Nv2aSurface *input,
                                 const VkRect2D *override)
{
    VkViewport viewport;
    VkRect2D scissor;
    memset(&viewport, 0, sizeof viewport);
    viewport.width = (float)surface->pw;
    viewport.height = -(float)surface->ph;
    viewport.y = (float)surface->ph;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    if (override) {
        scissor = *override;
    } else {
        scissor.offset.x = 0;
        scissor.offset.y = 0;
        scissor.extent.width = scale_edge(
            input->width < surface->layout.width
                ? input->width : surface->layout.width,
            surface->layout.width, surface->pw);
        scissor.extent.height = scale_edge(
            input->height < surface->layout.height
                ? input->height : surface->layout.height,
            surface->layout.height, surface->ph);
    }
    s_vk.fn.CmdSetViewport(command, 0, 1, &viewport);
    s_vk.fn.CmdSetScissor(command, 0, 1, &scissor);
}

static void begin_rendering_image(VkCommandBuffer command,
                                  VulkanImage *image,
                                  uint32_t width, uint32_t height,
                                  const VulkanDepth *depth)
{
    VkRenderingAttachmentInfo color = {
        VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO
    };
    VkRenderingAttachmentInfo depth_attachment = {
        VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO
    };
    VkRenderingInfo info = { VK_STRUCTURE_TYPE_RENDERING_INFO };

    if (image->layout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        VkPipelineStageFlags source_stage =
            image->layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
            : VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        VkAccessFlags source_access =
            image->layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            ? VK_ACCESS_SHADER_READ_BIT
            : VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        image_barrier(command, image,
                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      source_stage,
                      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                      source_access,
                      VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    }
    color.imageView = image->view;
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    info.renderArea.extent.width = width;
    info.renderArea.extent.height = height;
    info.layerCount = 1;
    info.colorAttachmentCount = 1;
    info.pColorAttachments = &color;
    if (depth) {
        depth_attachment.imageView = depth->image.view;
        depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        info.pDepthAttachment = &depth_attachment;
        if (depth->image.aspect & VK_IMAGE_ASPECT_STENCIL_BIT)
            info.pStencilAttachment = &depth_attachment;
    }
    s_vk.fn.CmdBeginRendering(command, &info);
}

static void begin_rendering(VkCommandBuffer command,
                            VulkanSurface *surface,
                            const VulkanDepth *depth)
{
    begin_rendering_image(command, &surface->image, surface->pw, surface->ph,
                          depth);
}

static VkRect2D clear_rectangle(const Nv2aSurface *input,
                                const VulkanSurface *surface)
{
    VkRect2D result;
    uint32_t bounds[4];
    uint32_t sx = input->aa_sx > 1 ? 2 : 1;
    uint32_t sy = input->aa_sy > 1 ? 2 : 1;

    result.offset.x = result.offset.y = 0;
    result.extent.width = surface->pw;
    result.extent.height = surface->ph;
    if (nv2a_backend_clear_rect(bounds)) {
        uint32_t x0 = bounds[0] * sx;
        uint32_t y0 = bounds[1] * sy;
        uint32_t x1 = bounds[2] * sx;
        uint32_t y1 = bounds[3] * sy;
        if (x0 > surface->layout.width) x0 = surface->layout.width;
        if (y0 > surface->layout.height) y0 = surface->layout.height;
        if (x1 > surface->layout.width) x1 = surface->layout.width;
        if (y1 > surface->layout.height) y1 = surface->layout.height;
        x0 = scale_edge(x0, surface->layout.width, surface->pw);
        y0 = scale_edge(y0, surface->layout.height, surface->ph);
        x1 = scale_edge(x1, surface->layout.width, surface->pw);
        y1 = scale_edge(y1, surface->layout.height, surface->ph);
        result.offset.x = (int32_t)x0;
        result.offset.y = (int32_t)y0;
        result.extent.width = x1 > x0 ? x1 - x0 : 0;
        result.extent.height = y1 > y0 ? y1 - y0 : 0;
    }
    return result;
}

static void unpack_color(uint32_t argb, float color[4])
{
    color[0] = ((argb >> 16) & 0xFFu) / 255.0f;
    color[1] = ((argb >> 8) & 0xFFu) / 255.0f;
    color[2] = (argb & 0xFFu) / 255.0f;
    color[3] = ((argb >> 24) & 0xFFu) / 255.0f;
}

static int mirror_full_surface_clear(VulkanSurface *surface, uint32_t argb)
{
    GuestSpanIdentity identity;
    size_t row = (size_t)surface->layout.width *
                 surface->layout.bytes_per_pixel;
    uint32_t y;

    if (!readable_guest_range(surface->va, surface->layout.span_bytes, 1))
        return 0;
    for (y = 0; y < surface->layout.height; ++y) {
        uint8_t *shadow =
            surface->guest_shadow + (size_t)y * surface->layout.pitch;
        uint32_t x;
        if (surface->layout.bytes_per_pixel == 4) {
            uint32_t *pixels = (uint32_t *)shadow;
            for (x = 0; x < surface->layout.width; ++x)
                pixels[x] = argb;
        } else {
            uint16_t *pixels = (uint16_t *)shadow;
            uint16_t color = (uint16_t)(((argb >> 8) & 0xF800u) |
                                        ((argb >> 5) & 0x07E0u) |
                                        ((argb >> 3) & 0x001Fu));
            for (x = 0; x < surface->layout.width; ++x)
                pixels[x] = color;
        }
        if (!guarded_guest_write(
                GUEST(surface->va) + (size_t)y * surface->layout.pitch,
                shadow, row))
            return 0;
    }
    if (!snapshot_surface_guest(surface, &identity) ||
        memcmp(surface->guest_shadow, s_vk.surface_probe,
               surface->guest_shadow_bytes) != 0)
        return 0;
    remember_surface_guest(surface, &identity);
    surface->guest_pending_upload = 0;
    surface->gpu_dirty = 0;
    return 1;
}

static void vulkan_clear(const Nv2aSurface *input, const Nv2aRenderState *state,
                         uint32_t flags, uint32_t argb, uint32_t zstencil)
{
    VulkanCommandSlot *slot = begin_commands();
    VulkanSurface *surface = get_surface(input, slot);
    VulkanDepth *depth = get_depth(state, surface, slot);
    VkRect2D rectangle = clear_rectangle(input, surface);
    VkClearAttachment attachments[2];
    VkClearRect clear_rect;
    uint32_t attachment_count = 0;
    VkColorComponentFlags mask = 0;
    int full_color_clear;

    if (!rectangle.extent.width || !rectangle.extent.height) {
        finish_commands(slot);
        return;
    }
    full_color_clear =
        (flags & 0xF0u) == 0xF0u &&
        rectangle.offset.x == 0 && rectangle.offset.y == 0 &&
        rectangle.extent.width == surface->pw &&
        rectangle.extent.height == surface->ph;
    begin_rendering(slot->command, surface, depth);
    memset(attachments, 0, sizeof attachments);
    memset(&clear_rect, 0, sizeof clear_rect);
    clear_rect.rect = rectangle;
    clear_rect.layerCount = 1;
    if ((flags & 0xF0u) == 0xF0u) {
        attachments[attachment_count].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        unpack_color(argb, attachments[attachment_count].clearValue.color.float32);
        attachment_count++;
    } else if (flags & 0xF0u) {
        VulkanPipelineKey key;
        VulkanPushConstants constants;
        VkPipeline pipeline;
        memset(&constants, 0, sizeof constants);
        if (flags & 0x10u) mask |= VK_COLOR_COMPONENT_R_BIT;
        if (flags & 0x20u) mask |= VK_COLOR_COMPONENT_G_BIT;
        if (flags & 0x40u) mask |= VK_COLOR_COMPONENT_B_BIT;
        if (flags & 0x80u) mask |= VK_COLOR_COMPONENT_A_BIT;
        if (!pipeline_key(NULL, surface->image.format,
                          depth ? depth->image.format : VK_FORMAT_UNDEFINED,
                          1, mask, &key))
            runtime_failure("mapping clear state", VK_ERROR_FORMAT_NOT_SUPPORTED);
        pipeline = get_pipeline(&key);
        unpack_color(argb, constants.clear_color);
        set_viewport_scissor(slot->command, surface, input, &rectangle);
        s_vk.fn.CmdBindPipeline(slot->command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        s_vk.fn.CmdPushConstants(slot->command, s_vk.pipeline_layout,
                                 VK_SHADER_STAGE_VERTEX_BIT |
                                 VK_SHADER_STAGE_FRAGMENT_BIT,
                                 0, sizeof constants, &constants);
        s_vk.fn.CmdDraw(slot->command, 3, 1, 0, 0);
    }
    if ((flags & 3u) && depth) {
        attachments[attachment_count].aspectMask = 0;
        if (flags & 1u)
            attachments[attachment_count].aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
        if ((flags & 2u) && (depth->image.aspect & VK_IMAGE_ASPECT_STENCIL_BIT))
            attachments[attachment_count].aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        attachments[attachment_count].clearValue.depthStencil.depth =
            ((nv2a_backend_reg(0x0208) >> 4) & 0xFu) == 1
                ? (zstencil & 0xFFFFu) / 65535.0f
                : (zstencil >> 8) / 16777215.0f;
        attachments[attachment_count].clearValue.depthStencil.stencil =
            zstencil & 0xFFu;
        if (attachments[attachment_count].aspectMask)
            attachment_count++;
    } else if ((flags & 3u) && state && state->zeta_va) {
        fprintf(stderr, "[GPU-Vulkan] cannot clear unavailable depth surface %08X\n",
                state->zeta_va);
        abort();
    }
    if (attachment_count)
        s_vk.fn.CmdClearAttachments(slot->command, attachment_count,
                                    attachments, 1, &clear_rect);
    s_vk.fn.CmdEndRendering(slot->command);
    if (flags & 0xF0u) {
        if (full_color_clear) {
            if (!mirror_full_surface_clear(surface, argb)) {
                fprintf(stderr, "[GPU-Vulkan] cannot mirror full color clear"
                                " for surface %08X\n", surface->va);
                abort();
            }
        } else {
            surface->gpu_dirty = 1;
        }
    }
    finish_commands(slot);
    s_vk.clears++;
}

static VkSamplerAddressMode address_mode(uint32_t value)
{
    switch (value) {
    case 1: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case 2: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case 3: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case 4: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    default:
        fprintf(stderr, "[GPU-Vulkan] unsupported texture address mode 0x%X\n", value);
        abort();
    }
}

static VkFilter vulkan_filter(GpuTextureFilterMode mode)
{
    return mode == GPU_TEXTURE_FILTER_LINEAR ? VK_FILTER_LINEAR
                                              : VK_FILTER_NEAREST;
}

static VkSampler create_sampler(const Nv2aTexture *texture,
                                uint32_t control0, uint32_t levels)
{
    VkSamplerCreateInfo info = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    GpuTextureFilterState state;
    VkSampler sampler = VK_NULL_HANDLE;
    unsigned warnings = gpu_texture_filter_state(
        &state, texture->filter, control0, levels, s_vk.texture_mips, 0);
    float bias_limit = s_vk.properties.limits.maxSamplerLodBias;

    if (state.lod_bias > bias_limit) {
        state.lod_bias = bias_limit;
        warnings |= GPU_SAMPLER_BIAS_CLAMPED;
    } else if (state.lod_bias < -bias_limit) {
        state.lod_bias = -bias_limit;
        warnings |= GPU_SAMPLER_BIAS_CLAMPED;
    }
    if (warnings) {
        static unsigned reports;
        if (reports++ < 16) {
            fprintf(stderr, "[GPU-MIPS] Vulkan sampler filter 0x%08X:",
                    texture->filter);
            if (warnings & GPU_SAMPLER_UNKNOWN_MIN)
                fprintf(stderr, " unknown minification filter, using point;");
            if (warnings & GPU_SAMPLER_UNKNOWN_MAG)
                fprintf(stderr, " unknown magnification filter, using point;");
            if (warnings & GPU_SAMPLER_CONVOLUTION)
                fprintf(stderr, " convolution approximated by linear filtering;");
            if (warnings & GPU_SAMPLER_BIAS_CLAMPED)
                fprintf(stderr, " LOD bias clamped to the device limit;");
            if (warnings & GPU_SAMPLER_LOD_RANGE)
                fprintf(stderr, " inverted LOD clamps collapsed to the maximum;");
            fprintf(stderr, "%s\n",
                    reports == 16 ? " further reports suppressed" : "");
        }
    }
    info.magFilter = vulkan_filter(state.mag_filter);
    info.minFilter = vulkan_filter(state.min_filter);
    info.mipmapMode = state.mip_filter == GPU_TEXTURE_FILTER_LINEAR
                    ? VK_SAMPLER_MIPMAP_MODE_LINEAR
                    : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    info.addressModeU = address_mode(texture->addr_u);
    info.addressModeV = address_mode(texture->addr_v);
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.mipLodBias = state.lod_bias;
    info.minLod = state.min_lod;
    info.maxLod = state.max_lod;
    info.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    if (s_vk.fn.CreateSampler(s_vk.device, &info, NULL, &sampler) != VK_SUCCESS)
        runtime_failure("creating a texture sampler", VK_ERROR_OUT_OF_DEVICE_MEMORY);
    return sampler;
}

static VkSampler get_sampler(const Nv2aTexture *texture,
                             uint32_t control0, uint32_t levels)
{
    VulkanSamplerKey key;
    unsigned i;

    key.filter = texture->filter;
    key.control0 = control0;
    key.levels = levels;
    key.addr_u = texture->addr_u;
    key.addr_v = texture->addr_v;
    for (i = 0; i < s_vk.sampler_count; ++i)
        if (!memcmp(&s_vk.samplers[i].key, &key, sizeof key))
            return s_vk.samplers[i].sampler;
    if (s_vk.sampler_count == VULKAN_MAX_SAMPLERS)
        runtime_failure("caching texture samplers", VK_ERROR_OUT_OF_HOST_MEMORY);
    s_vk.samplers[s_vk.sampler_count].key = key;
    s_vk.samplers[s_vk.sampler_count].sampler =
        create_sampler(texture, control0, levels);
    return s_vk.samplers[s_vk.sampler_count++].sampler;
}

static int mip_filter_requested(uint32_t filter)
{
    uint32_t min_filter = (filter >> 16) & 0xFFu;
    return min_filter >= 3 && min_filter <= 6;
}

static void mip_fallback(const char *reason, const Nv2aTexture *texture,
                         unsigned stage)
{
    static unsigned reports;
    if (reports++ < 32)
        fprintf(stderr, "[GPU-MIPS] Vulkan stage %u texture 0x%08X"
                        " format 0x%02X: %s%s\n",
                stage, texture->offset, texture->color, reason,
                reports == 32 ? " (further mip fallback reports suppressed)" : "");
}

static int same_guest_identity(const GuestSpanIdentity *left,
                               const GuestSpanIdentity *right)
{
    return left->layout && left->layout == right->layout &&
           left->allocation == right->allocation &&
           left->bounds == right->bounds;
}

static const char *guest_copy_result_name(GuestCopyResult result)
{
    switch (result) {
    case GUEST_COPY_INVALID: return "invalid guest range";
    case GUEST_COPY_SHORT: return "tracked allocation is too short";
    case GUEST_COPY_UNREGISTERED: return "guest range lacks registered bounds";
    case GUEST_COPY_CHANGED: return "allocation or mapping changed during the read";
    case GUEST_COPY_UNREADABLE: return "guest range is not readable";
    default: return "unknown checked-copy failure";
    }
}

static int ensure_texture_probe(size_t bytes)
{
    uint8_t *probe;

    if (bytes <= s_vk.texture_probe_capacity)
        return 1;
    probe = realloc(s_vk.texture_probe, bytes);
    if (!probe)
        return 0;
    s_vk.texture_probe = probe;
    s_vk.texture_probe_capacity = bytes;
    return 1;
}

static int ensure_texture_decode(size_t texels)
{
    uint32_t *decoded;

    if (texels <= s_vk.texture_decode_capacity)
        return 1;
    if (texels > SIZE_MAX / sizeof *decoded)
        return 0;
    decoded = realloc(s_vk.texture_decode, texels * sizeof *decoded);
    if (!decoded)
        return 0;
    s_vk.texture_decode = decoded;
    s_vk.texture_decode_capacity = texels;
    return 1;
}

static int texture_key_matches(const VulkanTexture *entry,
                               const Nv2aTexture *texture,
                               uint32_t palette_entries, uint32_t faces)
{
    return entry->faces == faces &&
           entry->key.offset == texture->offset &&
           entry->key.width == texture->width &&
           entry->key.height == texture->height &&
           entry->key.pitch == texture->pitch &&
           entry->key.color == texture->color &&
           entry->key.levels == texture->levels &&
           entry->key.palette == texture->palette &&
           entry->key.palette_entries == palette_entries;
}

static unsigned texture_key_bucket_values(
    const Nv2aTexture *texture, uint32_t palette_entries, uint32_t faces)
{
    uint32_t values[] = {
        texture->offset, texture->width, texture->height, texture->pitch,
        texture->color, texture->levels, texture->palette, palette_entries,
        faces,
    };
    uint64_t hash = 1469598103934665603ull;
    size_t i;
    for (i = 0; i < sizeof values / sizeof values[0]; ++i) {
        hash = (hash ^ values[i]) * 0x100000001B3ull;
        hash ^= hash >> 29;
    }
    return (unsigned)((hash ^ (hash >> 32)) &
                      (VULKAN_TEXTURE_BUCKETS - 1));
}

static void texture_index_corrupt(const char *operation, int index)
{
    fprintf(stderr, "[GPU-Vulkan] texture descriptor index corrupt during"
                    " %s at slot %d\n", operation, index);
    abort();
}

static void texture_index_insert(VulkanTexture *entry)
{
    int index = (int)(entry - s_vk.textures);
    unsigned bucket;
    if (index < 0 || (unsigned)index >= s_vk.texture_count)
        texture_index_corrupt("insert", index);
    bucket = texture_key_bucket_values(
        &entry->key, entry->key.palette_entries, entry->faces);
    entry->index_next = s_vk.texture_index[bucket];
    s_vk.texture_index[bucket] = index + 1;
}

static void texture_index_remove(VulkanTexture *entry)
{
    int index = (int)(entry - s_vk.textures);
    int *link;
    if (index < 0 || (unsigned)index >= s_vk.texture_count)
        texture_index_corrupt("remove", index);
    link = &s_vk.texture_index[texture_key_bucket_values(
        &entry->key, entry->key.palette_entries, entry->faces)];
    while (*link) {
        VulkanTexture *candidate;
        if (*link < 1 || (unsigned)*link > s_vk.texture_count)
            texture_index_corrupt("remove traversal", *link - 1);
        if (*link == index + 1) {
            *link = entry->index_next;
            entry->index_next = 0;
            return;
        }
        candidate = &s_vk.textures[*link - 1];
        link = &candidate->index_next;
    }
    texture_index_corrupt("remove missing", index);
}

static VulkanTexture *find_cached_texture(const Nv2aTexture *texture,
                                          uint32_t palette_entries,
                                          uint32_t faces)
{
    int link = s_vk.texture_index[texture_key_bucket_values(
        texture, palette_entries, faces)];
    while (link) {
        VulkanTexture *entry;
        if (link < 1 || (unsigned)link > s_vk.texture_count)
            texture_index_corrupt("lookup", link - 1);
        entry = &s_vk.textures[link - 1];
        s_vk.texture_lookup_probes++;
        if (texture_key_matches(entry, texture, palette_entries, faces))
            return entry;
        link = entry->index_next;
    }
    return NULL;
}

static void release_cached_texture(VulkanTexture *entry)
{
    if (!entry->image.image)
        return;
    destroy_image(&entry->image);
    free(entry->snapshot);
    if (entry->snapshot_bytes <= s_vk.texture_snapshot_bytes)
        s_vk.texture_snapshot_bytes -= entry->snapshot_bytes;
    memset(entry, 0, sizeof *entry);
}

static void retire_cached_texture(VulkanCommandSlot *slot,
                                  VulkanTexture *entry)
{
    VulkanImage *retired;
    if (!entry->image.image)
        return;
    texture_index_remove(entry);
    retired = slot_texture(slot);
    *retired = entry->image;
    memset(&entry->image, 0, sizeof entry->image);
    free(entry->snapshot);
    if (entry->snapshot_bytes <= s_vk.texture_snapshot_bytes)
        s_vk.texture_snapshot_bytes -= entry->snapshot_bytes;
    memset(entry, 0, sizeof *entry);
}

static VulkanTexture *least_recent_texture(const VulkanTexture *exclude)
{
    VulkanTexture *oldest = NULL;
    unsigned i;

    for (i = 0; i < s_vk.texture_count; ++i) {
        VulkanTexture *entry = &s_vk.textures[i];
        if (!entry->image.image || entry == exclude)
            continue;
        if (!oldest || entry->last_use < oldest->last_use)
            oldest = entry;
    }
    return oldest;
}

static VulkanTexture *texture_cache_slot(VulkanCommandSlot *slot,
                                         VulkanTexture *entry,
                                         size_t snapshot_bytes)
{
    unsigned i;

    if (entry)
        retire_cached_texture(slot, entry);
    while (snapshot_bytes > VULKAN_MAX_TEXTURE_SNAPSHOT_BYTES -
                            s_vk.texture_snapshot_bytes) {
        VulkanTexture *oldest = least_recent_texture(entry);
        if (!oldest)
            return NULL;
        retire_cached_texture(slot, oldest);
        s_vk.texture_evictions++;
    }
    if (entry)
        return entry;
    for (i = 0; i < s_vk.texture_count; ++i)
        if (!s_vk.textures[i].image.image)
            return &s_vk.textures[i];
    if (s_vk.texture_count < VULKAN_MAX_TEXTURES)
        return &s_vk.textures[s_vk.texture_count++];
    entry = least_recent_texture(NULL);
    if (!entry)
        return NULL;
    retire_cached_texture(slot, entry);
    s_vk.texture_evictions++;
    return entry;
}

static int snapshot_texture(const Nv2aTexture *texture,
                            uint32_t faces,
                            unsigned stage,
                            GpuTextureLayout *layout,
                            uint32_t *palette_entries,
                            size_t *source_bytes,
                            size_t *palette_bytes,
                            GuestSpanIdentity *source_identity,
                            GuestSpanIdentity *palette_identity)
{
    GpuTextureLayout base;
    GuestCopyResult result;
    size_t total;

    if (!gpu_texture_layout(texture, (int)faces, s_vk.texture_mips, layout))
        return 0;
    *source_bytes = layout->span;
    *palette_entries = texture->color == 0x0B
                     ? nv2a_p8_palette_entries(texture->palette_entries) : 0;
    if (texture->color == 0x0B && (!*palette_entries || !texture->palette))
        return 0;
    *palette_bytes = (size_t)*palette_entries * 4;
    if (*palette_bytes > SIZE_MAX - *source_bytes)
        return 0;
    total = *source_bytes + *palette_bytes;
    if (!ensure_texture_probe(total))
        return 0;
    result = gpu_guest_copy(texture->offset, *source_bytes, s_vk.texture_probe,
                            GUEST_BOUNDS_TRACK_IF_REGISTERED, source_identity);
    if (result != GUEST_COPY_OK && s_vk.texture_mips && layout->levels > 1 &&
        gpu_texture_layout(texture, (int)faces, 0, &base) &&
        (faces != 6 || base.face_stride == layout->face_stride)) {
        *source_bytes = base.span;
        if (*palette_bytes > SIZE_MAX - *source_bytes)
            return 0;
        total = *source_bytes + *palette_bytes;
        if (!ensure_texture_probe(total))
            return 0;
        result = gpu_guest_copy(texture->offset, *source_bytes, s_vk.texture_probe,
                                GUEST_BOUNDS_TRACK_IF_REGISTERED, source_identity);
        if (result == GUEST_COPY_OK) {
            *layout = base;
            mip_fallback("mip chain is unreadable or shorter than its tracked"
                         " allocation; falling back to level 0",
                         texture, stage);
        }
    }
    if (result != GUEST_COPY_OK) {
        fprintf(stderr, "[GPU-Vulkan] texture snapshot at %08X failed: %s\n",
                texture->offset, guest_copy_result_name(result));
        return 0;
    }
    if (*palette_bytes) {
        result = gpu_guest_copy(texture->palette, *palette_bytes,
                                s_vk.texture_probe + *source_bytes,
                                GUEST_BOUNDS_TRACK_IF_REGISTERED, palette_identity);
        if (result != GUEST_COPY_OK) {
            fprintf(stderr, "[GPU-Vulkan] palette snapshot at %08X failed: %s\n",
                    texture->palette, guest_copy_result_name(result));
            return 0;
        }
    } else {
        memset(palette_identity, 0, sizeof *palette_identity);
    }
    if (!gpu_guest_identity_still_valid(
            texture->offset, *source_bytes, GUEST_BOUNDS_TRACK_IF_REGISTERED,
            source_identity) ||
        (*palette_bytes &&
         !gpu_guest_identity_still_valid(
             texture->palette, *palette_bytes, GUEST_BOUNDS_TRACK_IF_REGISTERED,
             palette_identity))) {
        fprintf(stderr, "[GPU-Vulkan] texture allocation changed after snapshot at %08X\n",
                texture->offset);
        return 0;
    }
    return 1;
}

static int surface_texture_format_compatible(const VulkanSurface *surface,
                                             const Nv2aTexture *texture)
{
    return (surface->layout.bytes_per_pixel == 4 &&
            (texture->color == 0x12 || texture->color == 0x06 ||
             texture->color == 0x2E)) ||
           (surface->layout.bytes_per_pixel == 2 && texture->color == 0x11);
}

static int surface_texture_region(const VulkanSurface *surface,
                                  const Nv2aTexture *texture,
                                  uint32_t *x, uint32_t *y)
{
    uint64_t delta;
    uint64_t x_bytes;

    if (!surface || texture->offset < surface->va ||
        !surface_texture_format_compatible(surface, texture))
        return 0;
    if (texture->pitch != surface->layout.pitch &&
        !(texture->offset == surface->va &&
          texture->color == 0x06 && !texture->pitch &&
          surface->layout.pitch == surface->layout.width * 4))
        return 0;
    delta = (uint64_t)texture->offset - surface->va;
    *y = (uint32_t)(delta / surface->layout.pitch);
    x_bytes = delta % surface->layout.pitch;
    if (x_bytes % surface->layout.bytes_per_pixel)
        return 0;
    *x = (uint32_t)(x_bytes / surface->layout.bytes_per_pixel);
    return *x <= surface->layout.width &&
           texture->width <= surface->layout.width - *x &&
           *y <= surface->layout.height &&
           texture->height <= surface->layout.height - *y;
}

static VkPipelineStageFlags image_layout_stage(VkImageLayout layout)
{
    switch (layout) {
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        return VK_PIPELINE_STAGE_TRANSFER_BIT;
    default:
        return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }
}

static VkAccessFlags image_layout_access(VkImageLayout layout)
{
    switch (layout) {
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        return VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
               VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        return VK_ACCESS_SHADER_READ_BIT;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        return VK_ACCESS_TRANSFER_READ_BIT;
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        return VK_ACCESS_TRANSFER_WRITE_BIT;
    default:
        return VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    }
}

static VkImageView copy_surface_texture_region(VulkanCommandSlot *slot,
                                               VulkanSurface *alias,
                                               const Nv2aTexture *texture,
                                               uint32_t x, uint32_t y)
{
    VkImageCopy copy;
    VkImageLayout old_layout = alias->image.layout;
    VulkanImage *copy_image = slot_texture(slot);
    uint32_t px0 = scale_edge(x, alias->layout.width, alias->pw);
    uint32_t py0 = scale_edge(y, alias->layout.height, alias->ph);
    uint32_t px1 = scale_edge(x + texture->width, alias->layout.width,
                              alias->pw);
    uint32_t py1 = scale_edge(y + texture->height, alias->layout.height,
                              alias->ph);

    /* A downscaled sliver keeps at least one physical texel. */
    if (px1 <= px0) {
        if (px0 >= alias->pw)
            px0 = alias->pw - 1;
        px1 = px0 + 1;
    }
    if (py1 <= py0) {
        if (py0 >= alias->ph)
            py0 = alias->ph - 1;
        py1 = py0 + 1;
    }
    if (!create_image(px1 - px0, py1 - py0, alias->image.format,
                      VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                      VK_IMAGE_USAGE_SAMPLED_BIT,
                      VK_IMAGE_ASPECT_COLOR_BIT, copy_image))
        runtime_failure("creating a color alias subimage",
                        VK_ERROR_OUT_OF_DEVICE_MEMORY);
    image_barrier(slot->command, &alias->image,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  image_layout_stage(old_layout),
                  VK_PIPELINE_STAGE_TRANSFER_BIT,
                  image_layout_access(old_layout),
                  VK_ACCESS_TRANSFER_READ_BIT);
    image_barrier(slot->command, copy_image,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT,
                  0, VK_ACCESS_TRANSFER_WRITE_BIT);
    memset(&copy, 0, sizeof copy);
    copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.srcSubresource.layerCount = 1;
    copy.srcOffset.x = (int32_t)px0;
    copy.srcOffset.y = (int32_t)py0;
    copy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.dstSubresource.layerCount = 1;
    copy.extent.width = px1 - px0;
    copy.extent.height = py1 - py0;
    copy.extent.depth = 1;
    s_vk.fn.CmdCopyImage(slot->command, alias->image.image,
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         copy_image->image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    image_barrier(slot->command, copy_image,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                  VK_ACCESS_TRANSFER_WRITE_BIT,
                  VK_ACCESS_SHADER_READ_BIT);
    image_barrier(slot->command, &alias->image, old_layout,
                  VK_PIPELINE_STAGE_TRANSFER_BIT,
                  image_layout_stage(old_layout),
                  VK_ACCESS_TRANSFER_READ_BIT,
                  image_layout_access(old_layout));
    return copy_image->view;
}

static VkImageView prepare_surface_texture(VulkanCommandSlot *slot,
                                           VulkanSurface *alias,
                                           VulkanSurface *target,
                                           const Nv2aTexture *texture)
{
    uint32_t x;
    uint32_t y;

    if (!surface_texture_region(alias, texture, &x, &y)) {
        fprintf(stderr, "[GPU-Vulkan] texture %08X overlaps color surface %08X"
                        " with an incompatible layout (texture %ux%u pitch %u"
                        " format 0x%02X; surface %ux%u pitch %u bpp %u)\n",
                texture->offset, alias->va, texture->width, texture->height,
                texture->pitch, texture->color, alias->layout.width,
                alias->layout.height, alias->layout.pitch,
                alias->layout.bytes_per_pixel);
        abort();
    }
    if (x || y || texture->width != alias->layout.width ||
        texture->height != alias->layout.height)
        return copy_surface_texture_region(slot, alias, texture, x, y);
    if (alias == target) {
        VkImageCopy copy;
        VkPipelineStageFlags alias_source_stage =
            alias->image.layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
            : VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkAccessFlags alias_source_access =
            alias->image.layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            ? VK_ACCESS_SHADER_READ_BIT
            : VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
              VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        VkPipelineStageFlags copy_source_stage =
            alias->sample_copy.layout == VK_IMAGE_LAYOUT_UNDEFINED
            ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
            : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        VkAccessFlags copy_source_access =
            alias->sample_copy.layout == VK_IMAGE_LAYOUT_UNDEFINED
            ? 0 : VK_ACCESS_SHADER_READ_BIT;

        if (!alias->sample_copy.image &&
            !create_image(alias->pw, alias->ph,
                          alias->image.format,
                          VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                          VK_IMAGE_USAGE_SAMPLED_BIT,
                          VK_IMAGE_ASPECT_COLOR_BIT, &alias->sample_copy))
            runtime_failure("creating a color alias snapshot",
                            VK_ERROR_OUT_OF_DEVICE_MEMORY);
        image_barrier(slot->command, &alias->image,
                      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      alias_source_stage,
                      VK_PIPELINE_STAGE_TRANSFER_BIT,
                      alias_source_access,
                      VK_ACCESS_TRANSFER_READ_BIT);
        image_barrier(slot->command, &alias->sample_copy,
                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      copy_source_stage,
                      VK_PIPELINE_STAGE_TRANSFER_BIT,
                      copy_source_access,
                      VK_ACCESS_TRANSFER_WRITE_BIT);
        memset(&copy, 0, sizeof copy);
        copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.srcSubresource.layerCount = 1;
        copy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.dstSubresource.layerCount = 1;
        copy.extent.width = alias->pw;
        copy.extent.height = alias->ph;
        copy.extent.depth = 1;
        s_vk.fn.CmdCopyImage(slot->command, alias->image.image,
                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                             alias->sample_copy.image,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        image_barrier(slot->command, &alias->sample_copy,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      VK_PIPELINE_STAGE_TRANSFER_BIT,
                      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                      VK_ACCESS_TRANSFER_WRITE_BIT,
                      VK_ACCESS_SHADER_READ_BIT);
        image_barrier(slot->command, &alias->image,
                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      VK_PIPELINE_STAGE_TRANSFER_BIT,
                      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_ACCESS_TRANSFER_READ_BIT,
                      VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        return alias->sample_copy.view;
    }
    if (alias->image.layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        image_barrier(slot->command, &alias->image,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                      VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
                      VK_ACCESS_SHADER_READ_BIT);
    return alias->image.view;
}

static int prepare_texture(VulkanCommandSlot *slot, VulkanSurface *target,
                           const Nv2aTexture *texture, int cube, unsigned stage,
                           VkImageView *view, VkSampler *sampler)
{
    VkBufferImageCopy regions[GPU_TEXTURE_MAX_FACES * GPU_TEXTURE_MAX_LEVELS];
    VulkanSurface *alias;
    VulkanTexture *entry;
    GpuTextureLayout layout;
    GuestSpanIdentity source_identity;
    GuestSpanIdentity palette_identity;
    uint32_t palette_entries;
    size_t texels;
    size_t decoded_texels = 0;
    size_t used_texels = 0;
    size_t bytes;
    size_t source_bytes;
    size_t palette_bytes;
    size_t snapshot_bytes;
    uint32_t faces = cube ? 6u : 1u;
    uint32_t face;
    uint32_t level;
    uint32_t region_count = 0;
    uint32_t control0 = nv2a_backend_reg(0x1B0C + stage * 0x40);
    int cache_hit;
    uint64_t perf_start;

    if (!texture) {
        *view = cube ? s_vk.white_cube.view : s_vk.white.view;
        *sampler = s_vk.white_sampler;
        return 0;
    }
    if (!texture->width || !texture->height ||
        texture->width > 16384 || texture->height > 16384 ||
        texture->height > SIZE_MAX / texture->width) {
        fprintf(stderr, "[GPU-Vulkan] invalid texture dimensions %ux%u\n",
                texture->width, texture->height);
        abort();
    }
    if (!gpu_texture_layout(texture, (int)faces, s_vk.texture_mips, &layout)) {
        fprintf(stderr, "[GPU-Vulkan] unsupported or invalid texture layout 0x%02X"
                        " at %08X\n", texture->color, texture->offset);
        abort();
    }
    alias = faces == 1 ? find_texture_surface_alias(texture, &layout) : NULL;
    if (alias) {
        if (s_vk.texture_mips &&
            (texture->levels > 1 || mip_filter_requested(texture->filter)))
            mip_fallback("render-target alias is level 0 only", texture, stage);
        *view = prepare_surface_texture(slot, alias, target, texture);
        *sampler = get_sampler(texture, control0, 1);
        return 1;
    }
    if (texture->color == 0x2E) {
        static unsigned reports;
        if (reports < 32) {
            ++reports;
            fprintf(stderr, "[GPU-Vulkan] non-aliased LIN_D24S8 texture at"
                            " %08X samples white, matching the CPU reference%s\n",
                    texture->offset,
                    reports == 32 ? " (further reports suppressed)" : "");
        }
        *view = cube ? s_vk.white_cube.view : s_vk.white.view;
        *sampler = s_vk.white_sampler;
        return 0;
    }
    perf_start = vulkan_perf_begin();
    if (!snapshot_texture(texture, faces, stage, &layout,
                          &palette_entries, &source_bytes,
                          &palette_bytes, &source_identity, &palette_identity)) {
        fprintf(stderr, "[GPU-Vulkan] unsupported or invalid texture layout 0x%02X"
                        " at %08X\n", texture->color, texture->offset);
        abort();
    }
    if (perf_start) {
        vulkan_perf_finish_call(
            perf_start, &s_vk.perf.snapshot_ticks,
            &s_vk.perf.snapshot_calls);
        s_vk.perf.snapshot_bytes += source_bytes + palette_bytes;
    }
    if (s_vk.texture_mips) {
        if (layout.storage != GPU_TEXTURE_LINEAR &&
            texture->levels > layout.levels)
            mip_fallback("declared levels exceed the 1x1 chain; capped",
                         texture, stage);
        if (layout.storage == GPU_TEXTURE_LINEAR &&
            mip_filter_requested(texture->filter))
            mip_fallback("linear texture has no mip chain; sampling level 0",
                         texture, stage);
        else if (layout.levels == 1 &&
                 mip_filter_requested(texture->filter))
            mip_fallback("no additional mip levels declared; sampling level 0",
                         texture, stage);
    }
    snapshot_bytes = source_bytes + palette_bytes;
    perf_start = vulkan_perf_begin();
    entry = find_cached_texture(texture, palette_entries, faces);
    vulkan_perf_finish_call(
        perf_start, &s_vk.perf.lookup_ticks, &s_vk.perf.lookup_calls);
    perf_start = entry ? vulkan_perf_begin() : 0;
    cache_hit = entry && entry->snapshot_bytes == snapshot_bytes &&
                entry->source_bytes == source_bytes &&
                entry->palette_bytes == palette_bytes &&
                same_guest_identity(&entry->source_identity, &source_identity) &&
                (!palette_bytes ||
                 same_guest_identity(&entry->palette_identity, &palette_identity)) &&
                memcmp(entry->snapshot, s_vk.texture_probe, snapshot_bytes) == 0;
    if (perf_start) {
        vulkan_perf_finish_call(
            perf_start, &s_vk.perf.exact_ticks,
            &s_vk.perf.exact_checks);
        s_vk.perf.exact_bytes += snapshot_bytes;
    }
    if (cache_hit) {
        entry->last_use = ++s_vk.texture_serial;
        s_vk.texture_hits++;
        *view = entry->image.view;
        *sampler = get_sampler(texture, control0, entry->image.levels);
        return 1;
    }
    perf_start = vulkan_perf_begin();
    for (face = 0; face < faces; ++face)
        for (level = 0; level < layout.levels; ++level) {
            const GpuTextureSubresource *subresource =
                &layout.subresource[face][level];
            texels = (size_t)subresource->width * subresource->height;
            if (texels > SIZE_MAX - decoded_texels) {
                fprintf(stderr, "[GPU-Vulkan] texture size overflow\n");
                abort();
            }
            decoded_texels += texels;
        }
    if (decoded_texels > SIZE_MAX / sizeof *s_vk.texture_decode) {
        fprintf(stderr, "[GPU-Vulkan] texture size overflow\n");
        abort();
    }
    bytes = decoded_texels * sizeof *s_vk.texture_decode;
    if (!ensure_texture_decode(decoded_texels)) {
        fprintf(stderr, "[GPU-Vulkan] cannot allocate decoded texture\n");
        abort();
    }
    memset(regions, 0, sizeof regions);
    for (face = 0; face < faces; ++face) {
        for (level = 0; level < layout.levels; ++level) {
            const GpuTextureSubresource *subresource =
                &layout.subresource[face][level];
            Nv2aTexture face_texture = *texture;
            VkBufferImageCopy *region = &regions[region_count++];
            int decoded;

            texels = (size_t)subresource->width * subresource->height;
            face_texture.width = subresource->width;
            face_texture.height = subresource->height;
            face_texture.pitch = layout.storage == GPU_TEXTURE_LINEAR
                               ? (uint32_t)subresource->row_pitch : 0;
            face_texture.levels = 1;
            if (texture->color == 0x0B)
                decoded = nv2a_backend_decode_p8_data(
                    &face_texture, s_vk.texture_probe + subresource->offset,
                    subresource->size, s_vk.texture_probe + source_bytes,
                    palette_bytes, s_vk.texture_decode + used_texels, texels);
            else
                decoded = nv2a_backend_decode_texture_data(
                    &face_texture, s_vk.texture_probe + subresource->offset,
                    subresource->size, s_vk.texture_decode + used_texels, texels);
            if (!decoded) {
                fprintf(stderr, "[GPU-Vulkan] unsupported or invalid texture"
                                " format 0x%02X at %08X face %u level %u\n",
                        texture->color, texture->offset, face, level);
                abort();
            }
            region->bufferOffset = used_texels * sizeof *s_vk.texture_decode;
            region->imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region->imageSubresource.mipLevel = level;
            region->imageSubresource.baseArrayLayer = face;
            region->imageSubresource.layerCount = 1;
            region->imageExtent.width = subresource->width;
            region->imageExtent.height = subresource->height;
            region->imageExtent.depth = 1;
            used_texels += texels;
        }
    }
    if (entry)
        s_vk.texture_rewrites++;
    entry = texture_cache_slot(slot, entry, snapshot_bytes);
    if (!entry) {
        fprintf(stderr, "[GPU-Vulkan] texture snapshot cache is exhausted\n");
        abort();
    }
    entry->snapshot = malloc(snapshot_bytes);
    if (!entry->snapshot) {
        fprintf(stderr, "[GPU-Vulkan] cannot retain texture snapshot\n");
        abort();
    }
    if (!create_mip_image(
            texture->width, texture->height, layout.levels, faces, cube,
            VK_FORMAT_B8G8R8A8_UNORM,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT, &entry->image)) {
        runtime_failure("creating a sampled texture", VK_ERROR_OUT_OF_DEVICE_MEMORY);
    }
    upload_regions_to_image(slot, &entry->image, s_vk.texture_decode, bytes,
                            regions, region_count);
    image_barrier(slot->command, &entry->image,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                  VK_ACCESS_TRANSFER_WRITE_BIT,
                  VK_ACCESS_SHADER_READ_BIT);
    memcpy(entry->snapshot, s_vk.texture_probe, snapshot_bytes);
    entry->snapshot_bytes = snapshot_bytes;
    entry->source_bytes = source_bytes;
    entry->palette_bytes = palette_bytes;
    entry->source_identity = source_identity;
    entry->palette_identity = palette_identity;
    entry->key = *texture;
    entry->key.palette_entries = palette_entries;
    entry->faces = faces;
    entry->last_use = ++s_vk.texture_serial;
    texture_index_insert(entry);
    s_vk.texture_snapshot_bytes += snapshot_bytes;
    s_vk.texture_uploads++;
    if (layout.levels > 1) {
        static unsigned reports;
        s_vk.texture_mip_uploads++;
        if (reports++ < 32)
            fprintf(stderr, "[GPU-MIPS] Vulkan stage %u uploaded %u levels"
                            " for texture 0x%08X format 0x%02X%s\n",
                    stage, layout.levels, texture->offset, texture->color,
                    reports == 32 ? " (further mip upload reports suppressed)" : "");
    }
    *view = entry->image.view;
    *sampler = get_sampler(texture, control0, layout.levels);
    vulkan_perf_finish_call(
        perf_start, &s_vk.perf.upload_ticks, &s_vk.perf.upload_calls);
    return 1;
}

static VkDescriptorSet allocate_descriptor_set(VulkanCommandSlot *slot)
{
    VkDescriptorPoolSize sizes[4];
    VkDescriptorPoolCreateInfo pool = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    VkDescriptorSetAllocateInfo allocation = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO
    };
    VkDescriptorSet set = VK_NULL_HANDLE;

    if (slot->descriptor_count == VULKAN_BATCH_OPERATIONS)
        runtime_failure("allocating batched descriptor sets",
                        VK_ERROR_OUT_OF_POOL_MEMORY);
    if (!slot->descriptor_pool) {
        memset(sizes, 0, sizeof sizes);
        sizes[0].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        sizes[0].descriptorCount = 8 * VULKAN_BATCH_OPERATIONS;
        sizes[1].type = VK_DESCRIPTOR_TYPE_SAMPLER;
        sizes[1].descriptorCount = 4 * VULKAN_BATCH_OPERATIONS;
        sizes[2].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        sizes[2].descriptorCount = 3 * VULKAN_BATCH_OPERATIONS;
        sizes[3].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        sizes[3].descriptorCount = 3 * VULKAN_BATCH_OPERATIONS;
        pool.maxSets = VULKAN_BATCH_OPERATIONS;
        pool.poolSizeCount = 4;
        pool.pPoolSizes = sizes;
        vk_require(s_vk.fn.CreateDescriptorPool(s_vk.device, &pool, NULL,
                                                &slot->descriptor_pool),
                   "creating a descriptor pool");
        s_vk.descriptor_pool_creates++;
    }
    allocation.descriptorPool = slot->descriptor_pool;
    allocation.descriptorSetCount = 1;
    allocation.pSetLayouts = &s_vk.descriptor_layout;
    vk_require(s_vk.fn.AllocateDescriptorSets(s_vk.device, &allocation, &set),
               "allocating a descriptor set");
    slot->descriptor_count++;
    return set;
}

static VkDescriptorSet create_descriptor_set(VulkanCommandSlot *slot,
                                             VkImageView view,
                                             VkSampler sampler,
                                             VkBuffer window,
                                             VkDeviceSize window_offset)
{
    VkDescriptorImageInfo image;
    VkDescriptorImageInfo sampler_info;
    VkDescriptorBufferInfo window_buffer;
    VkWriteDescriptorSet writes[3];
    VkDescriptorSet set = allocate_descriptor_set(slot);

    memset(&image, 0, sizeof image);
    image.imageView = view;
    image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    memset(&sampler_info, 0, sizeof sampler_info);
    sampler_info.sampler = sampler;
    memset(&window_buffer, 0, sizeof window_buffer);
    window_buffer.buffer = window;
    window_buffer.offset = window_offset;
    window_buffer.range = sizeof(VulkanWindowClipConstants);
    memset(writes, 0, sizeof writes);
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = set;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    writes[0].pImageInfo = &image;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = set;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    writes[1].pImageInfo = &sampler_info;
    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = set;
    writes[2].dstBinding = 15;
    writes[2].descriptorCount = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[2].pBufferInfo = &window_buffer;
    s_vk.fn.UpdateDescriptorSets(s_vk.device, 3, writes, 0, NULL);
    return set;
}

/*
 * Resample one complete color image into another with the Blood Wake-derived
 * bilinear seed (upscale) or box downsample (downscale). The source must be
 * in SHADER_READ_ONLY layout; the target is left as a color attachment with
 * its writes visible to later attachment and transfer work.
 */
static void record_scale_blit(VulkanCommandSlot *slot, VulkanImage *source,
                              uint32_t source_width, uint32_t source_height,
                              VulkanImage *target,
                              uint32_t target_width, uint32_t target_height)
{
    VulkanPipelineKey key;
    VulkanPushConstants constants;
    VulkanUpload window;
    VkDescriptorSet descriptors;
    VkPipeline pipeline;
    VkViewport viewport;
    VkRect2D scissor;

    if (!pipeline_key(NULL, target->format, VK_FORMAT_UNDEFINED, 0, 0, &key))
        runtime_failure("mapping render-scale resolve state",
                        VK_ERROR_FORMAT_NOT_SUPPORTED);
    key.resolve_shader = source_width > target_width ||
                         source_height > target_height ? 2 : 1;
    pipeline = get_pipeline(&key);
    window = slot_upload(
        slot, sizeof(VulkanWindowClipConstants),
        s_vk.properties.limits.minUniformBufferOffsetAlignment);
    memset(window.mapped, 0, sizeof(VulkanWindowClipConstants));
    descriptors = create_descriptor_set(
        slot, source->view, s_vk.resolve_sampler, window.buffer, window.offset);
    memset(&constants, 0, sizeof constants);
    constants.geometry[0] = (float)source_width;
    constants.geometry[1] = (float)source_height;
    constants.geometry[2] = 1.0f / source_width;
    constants.geometry[3] = 1.0f / source_height;
    constants.texture_info[0] = (float)target_width;
    constants.texture_info[1] = (float)target_height;
    constants.texture_info[2] = 1.0f / target_width;
    constants.texture_info[3] = 1.0f / target_height;
    memset(&viewport, 0, sizeof viewport);
    viewport.width = (float)target_width;
    viewport.height = (float)target_height;
    viewport.maxDepth = 1.0f;
    scissor.offset.x = scissor.offset.y = 0;
    scissor.extent.width = target_width;
    scissor.extent.height = target_height;
    begin_rendering_image(slot->command, target, target_width, target_height,
                          NULL);
    s_vk.fn.CmdSetViewport(slot->command, 0, 1, &viewport);
    s_vk.fn.CmdSetScissor(slot->command, 0, 1, &scissor);
    s_vk.fn.CmdBindPipeline(slot->command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline);
    s_vk.fn.CmdBindDescriptorSets(slot->command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  s_vk.pipeline_layout, 0, 1, &descriptors,
                                  0, NULL);
    s_vk.fn.CmdPushConstants(slot->command, s_vk.pipeline_layout,
                             VK_SHADER_STAGE_VERTEX_BIT |
                             VK_SHADER_STAGE_FRAGMENT_BIT,
                             0, sizeof constants, &constants);
    s_vk.fn.CmdDraw(slot->command, 3, 1, 0, 0);
    s_vk.fn.CmdEndRendering(slot->command);
    image_barrier(slot->command, target,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                  VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                  VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                  VK_ACCESS_TRANSFER_READ_BIT);
    s_vk.scale_resolves++;
}

static VkDescriptorSet create_extended_descriptor_set(
    VulkanCommandSlot *slot, const VkImageView views[4],
    const VkImageView cube_views[4], const VkSampler samplers[4],
    VkBuffer constants, VkDeviceSize constants_offset,
    VkBuffer window, VkDeviceSize window_offset,
    VkBuffer storage, VkDeviceSize storage_offset, VkDeviceSize storage_size)
{
    VkDescriptorImageInfo images[4];
    VkDescriptorImageInfo cube_images[4];
    VkDescriptorImageInfo sampler_info[4];
    VkDescriptorBufferInfo buffer;
    VkDescriptorBufferInfo window_buffer;
    VkDescriptorBufferInfo storage_buffer;
    VkWriteDescriptorSet writes[15];
    VkDescriptorSet set = allocate_descriptor_set(slot);
    unsigned write_count = 14;
    unsigned i;

    memset(images, 0, sizeof images);
    memset(cube_images, 0, sizeof cube_images);
    memset(sampler_info, 0, sizeof sampler_info);
    memset(&buffer, 0, sizeof buffer);
    memset(&window_buffer, 0, sizeof window_buffer);
    memset(&storage_buffer, 0, sizeof storage_buffer);
    memset(writes, 0, sizeof writes);
    for (i = 0; i < 4; ++i) {
        images[i].imageView = views[i];
        images[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        sampler_info[i].sampler = samplers[i];
        cube_images[i].imageView = cube_views[i];
        cube_images[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        writes[i * 2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i * 2].dstSet = set;
        writes[i * 2].dstBinding = i * 2;
        writes[i * 2].descriptorCount = 1;
        writes[i * 2].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        writes[i * 2].pImageInfo = &images[i];
        writes[i * 2 + 1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i * 2 + 1].dstSet = set;
        writes[i * 2 + 1].dstBinding = i * 2 + 1;
        writes[i * 2 + 1].descriptorCount = 1;
        writes[i * 2 + 1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        writes[i * 2 + 1].pImageInfo = &sampler_info[i];
        writes[9 + i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[9 + i].dstSet = set;
        writes[9 + i].dstBinding = 9 + i;
        writes[9 + i].descriptorCount = 1;
        writes[9 + i].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        writes[9 + i].pImageInfo = &cube_images[i];
    }
    buffer.buffer = constants;
    buffer.offset = constants_offset;
    buffer.range = sizeof(VulkanExtendedConstants);
    writes[8].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[8].dstSet = set;
    writes[8].dstBinding = 8;
    writes[8].descriptorCount = 1;
    writes[8].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[8].pBufferInfo = &buffer;
    window_buffer.buffer = window;
    window_buffer.offset = window_offset;
    window_buffer.range = sizeof(VulkanWindowClipConstants);
    writes[13].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[13].dstSet = set;
    writes[13].dstBinding = 15;
    writes[13].descriptorCount = 1;
    writes[13].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[13].pBufferInfo = &window_buffer;
    if (storage) {
        storage_buffer.buffer = storage;
        storage_buffer.offset = storage_offset;
        storage_buffer.range = storage_size;
        writes[14].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[14].dstSet = set;
        writes[14].dstBinding = 13;
        writes[14].descriptorCount = 1;
        writes[14].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[14].pBufferInfo = &storage_buffer;
        write_count++;
    }
    s_vk.fn.UpdateDescriptorSets(
        s_vk.device, write_count, writes, 0, NULL);
    return set;
}

static VkDescriptorSet create_vsh_verify_descriptor_set(
    VulkanCommandSlot *slot, const VulkanUpload *constants,
    const VulkanUpload *data, VkDeviceSize data_size,
    const VulkanUpload *output, VkDeviceSize output_size)
{
    VkDescriptorBufferInfo buffers[3];
    VkWriteDescriptorSet writes[3];
    static const uint32_t bindings[3] = { 8, 13, 14 };
    static const VkDescriptorType types[3] = {
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    };
    const VulkanUpload *uploads[3] = { constants, data, output };
    const VkDeviceSize sizes[3] = {
        sizeof(VulkanExtendedConstants), data_size, output_size,
    };
    VkDescriptorSet set = allocate_descriptor_set(slot);
    unsigned i;

    memset(buffers, 0, sizeof buffers);
    memset(writes, 0, sizeof writes);
    for (i = 0; i < 3; ++i) {
        buffers[i].buffer = uploads[i]->buffer;
        buffers[i].offset = uploads[i]->offset;
        buffers[i].range = sizes[i];
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = set;
        writes[i].dstBinding = bindings[i];
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = types[i];
        writes[i].pBufferInfo = &buffers[i];
    }
    s_vk.fn.UpdateDescriptorSets(s_vk.device, 3, writes, 0, NULL);
    return set;
}

static void fill_vertices(const Nv2aBatch *batch, VulkanVertex *vertices)
{
    uint32_t i;
    for (i = 0; i < batch->count; ++i) {
        const Nv2aVertex *source = &batch->vertices[i];
        VulkanVertex *destination = &vertices[i];
        destination->position[0] = source->x;
        destination->position[1] = source->y;
        destination->position[2] = source->z;
        destination->position[3] = source->rhw;
        destination->color[0] = ((source->diffuse >> 16) & 0xFFu) / 255.0f;
        destination->color[1] = ((source->diffuse >> 8) & 0xFFu) / 255.0f;
        destination->color[2] = (source->diffuse & 0xFFu) / 255.0f;
        destination->color[3] = ((source->diffuse >> 24) & 0xFFu) / 255.0f;
        destination->uv[0] = source->u;
        destination->uv[1] = source->v;
    }
}

static int prepare_zpass_query(VulkanCommandSlot *slot)
{
    uint32_t query;
    if (!s_vk.zpass_enabled)
        return -1;
    if (slot->zpass_count == VULKAN_BATCH_OPERATIONS)
        runtime_failure("allocating a visibility query",
                        VK_ERROR_OUT_OF_HOST_MEMORY);
    query = (uint32_t)(slot - s_vk.slots) * VULKAN_BATCH_OPERATIONS +
            slot->zpass_count;
    s_vk.fn.CmdResetQueryPool(slot->command, s_vk.zpass_query_pool, query, 1);
    return (int)slot->zpass_count;
}

static void start_zpass_query(VulkanCommandSlot *slot, int query_index)
{
    uint32_t query;
    if (query_index < 0)
        return;
    query = (uint32_t)(slot - s_vk.slots) * VULKAN_BATCH_OPERATIONS +
            (uint32_t)query_index;
    s_vk.fn.CmdBeginQuery(slot->command, s_vk.zpass_query_pool, query,
                          VK_QUERY_CONTROL_PRECISE_BIT);
}

static void end_zpass_query(VulkanCommandSlot *slot,
                            const Nv2aSurface *surface,
                            const VulkanSurface *target, int query_index)
{
    double divisor;
    uint32_t query;
    if (query_index < 0)
        return;
    query = (uint32_t)(slot - s_vk.slots) * VULKAN_BATCH_OPERATIONS +
            (uint32_t)query_index;
    s_vk.fn.CmdEndQuery(slot->command, s_vk.zpass_query_pool, query);
    /*
     * Count guest-screen pixels: undo the guest AA sample factor and the
     * physical-to-logical render-scale area ratio of this target.
     */
    divisor = (double)(surface->aa_sx ? surface->aa_sx : 1) *
              (surface->aa_sy ? surface->aa_sy : 1);
    if (target->pw != target->layout.width ||
        target->ph != target->layout.height)
        divisor *= ((double)target->pw * target->ph) /
                   ((double)target->layout.width * target->layout.height);
    slot->zpass_sequences[slot->zpass_count] = s_vk.zpass_queries;
    slot->zpass_divisors[slot->zpass_count++] = divisor;
    s_vk.zpass_queries++;
}

static void fill_window_clip_constants(
    VulkanWindowClipConstants *constants, const Nv2aSurface *surface,
    const VulkanSurface *target);

static void vulkan_draw(const Nv2aSurface *input, const Nv2aBatch *batch)
{
    VulkanCommandSlot *slot;
    VulkanSurface *surface;
    VulkanDepth *depth;
    VulkanUpload vertices;
    VulkanUpload window;
    VulkanPipelineKey key;
    VulkanPushConstants constants;
    VkPipeline pipeline;
    VkImageView texture_view;
    VkSampler sampler;
    VkDescriptorSet descriptors;
    VkDeviceSize offset = 0;
    float blend[4];
    size_t vertex_bytes;
    int count_zpass;
    uint64_t draw_start;
    uint64_t perf_start;

    if (!batch || !batch->vertices || !batch->state || !batch->count)
        return;
    if (batch->count % 3 || batch->count > SIZE_MAX / sizeof(VulkanVertex)) {
        fprintf(stderr, "[GPU-Vulkan] invalid triangle-list vertex count %u\n", batch->count);
        abort();
    }
    draw_start = vulkan_perf_begin();
    perf_start = draw_start;
    slot = begin_commands();
    surface = get_surface(input, slot);
    depth = get_depth(batch->state, surface, slot);
    if (!pipeline_key(batch->state, surface->image.format,
                      depth ? depth->image.format : VK_FORMAT_UNDEFINED,
                      0, 0, &key)) {
        fprintf(stderr, "[GPU-Vulkan] unsupported draw state\n");
        abort();
    }
    if (key.cull_mode == VK_CULL_MODE_FRONT_AND_BACK) {
        finish_commands(slot);
        return;
    }
    pipeline = get_pipeline(&key);
    vulkan_perf_finish(perf_start, &s_vk.perf.setup_ticks);
    perf_start = vulkan_perf_begin();
    vertex_bytes = (size_t)batch->count * sizeof(VulkanVertex);
    vertices = slot_upload(slot, vertex_bytes, 4);
    fill_vertices(batch, vertices.mapped);
    window = slot_upload(
        slot, sizeof(VulkanWindowClipConstants),
        s_vk.properties.limits.minUniformBufferOffsetAlignment);
    fill_window_clip_constants(window.mapped, input, surface);
    if (perf_start)
        s_vk.perf.vertex_bytes += vertex_bytes;
    vulkan_perf_finish(perf_start, &s_vk.perf.vertex_ticks);
    perf_start = vulkan_perf_begin();
    prepare_texture(slot, surface, batch->texture, 0, 0,
                    &texture_view, &sampler);
    vulkan_perf_finish_call(
        perf_start, &s_vk.perf.texture_ticks, &s_vk.perf.texture_calls);
    perf_start = vulkan_perf_begin();
    descriptors = create_descriptor_set(
        slot, texture_view, sampler, window.buffer, window.offset);
    vulkan_perf_finish_call(
        perf_start, &s_vk.perf.descriptor_ticks,
        &s_vk.perf.descriptor_calls);
    memset(&constants, 0, sizeof constants);
    constants.geometry[0] = 1.0f / surface->layout.width;
    constants.geometry[1] = 1.0f / surface->layout.height;
    constants.geometry[2] = 0.0f;
    constants.geometry[3] =
        1.0f / ((((nv2a_backend_reg(0x0208) >> 4) & 0xFu) == 1)
                ? 65535.0f : 16777215.0f);
    if (batch->texture) {
        constants.texture_info[0] = 1.0f / batch->texture->width;
        constants.texture_info[1] = 1.0f / batch->texture->height;
        constants.texture_info[2] = 1.0f;
    }
    constants.texture_info[3] = batch->state->alpha_test_enable ? 1.0f : 0.0f;
    constants.alpha[0] = batch->state->alpha_func >= 0x200u
                       ? batch->state->alpha_func - 0x200u : UINT32_MAX;
    constants.alpha[1] = batch->state->alpha_ref;
    unpack_color(batch->state->blend_color, blend);
    perf_start = vulkan_perf_begin();
    count_zpass = prepare_zpass_query(slot);
    begin_rendering(slot->command, surface, depth);
    start_zpass_query(slot, count_zpass);
    set_viewport_scissor(slot->command, surface, input, NULL);
    s_vk.fn.CmdBindPipeline(slot->command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    offset = vertices.offset;
    s_vk.fn.CmdBindVertexBuffers(slot->command, 0, 1, &vertices.buffer, &offset);
    s_vk.fn.CmdBindDescriptorSets(slot->command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  s_vk.pipeline_layout, 0, 1, &descriptors, 0, NULL);
    s_vk.fn.CmdSetBlendConstants(slot->command, blend);
    s_vk.fn.CmdPushConstants(slot->command, s_vk.pipeline_layout,
                             VK_SHADER_STAGE_VERTEX_BIT |
                             VK_SHADER_STAGE_FRAGMENT_BIT,
                             0, sizeof constants, &constants);
    s_vk.fn.CmdDraw(slot->command, batch->count, 1, 0, 0);
    end_zpass_query(slot, input, surface, count_zpass);
    s_vk.fn.CmdEndRendering(slot->command);
    finish_commands(slot);
    vulkan_perf_finish(perf_start, &s_vk.perf.command_ticks);
    surface->gpu_dirty = 1;
    s_vk.draws++;
    vulkan_perf_finish_call(
        draw_start, &s_vk.perf.draw_ticks, &s_vk.perf.draw_calls);
}

static void fill_window_clip_constants(
    VulkanWindowClipConstants *constants, const Nv2aSurface *surface,
    const VulkanSurface *target)
{
    unsigned count;
    unsigned i;
    uint32_t sx;
    uint32_t sy;

    memset(constants, 0, sizeof *constants);
    if (!surface->window_clip_enabled || surface->window_clip.axes != 3)
        return;
    sx = surface->aa_sx > 1 ? 2 : 1;
    sy = surface->aa_sy > 1 ? 2 : 1;
    count = surface->window_clip.uniform ? 1 : NV2A_WINDOW_CLIP_COUNT;
    constants->info[0] = count;
    constants->info[1] = surface->window_clip.exclusive;
    for (i = 0; i < count; ++i) {
        uint32_t horizontal = surface->window_clip.horizontal[i];
        uint32_t vertical = surface->window_clip.vertical[i];
        uint32_t x0 = (horizontal & 0xFFFu) * sx;
        uint32_t x1 = (((horizontal >> 16) & 0xFFFu) + 1) * sx;
        uint32_t y0 = (vertical & 0xFFFu) * sy;
        uint32_t y1 = (((vertical >> 16) & 0xFFFu) + 1) * sy;
        /* Inclusive logical bounds become inclusive physical bounds. */
        constants->rect[i][0] =
            scale_edge(x0, target->layout.width, target->pw);
        constants->rect[i][1] =
            scale_edge(x1, target->layout.width, target->pw) - 1;
        constants->rect[i][2] =
            scale_edge(y0, target->layout.height, target->ph);
        constants->rect[i][3] =
            scale_edge(y1, target->layout.height, target->ph) - 1;
    }
}

static void fill_extended_constants(VulkanExtendedConstants *constants,
                                    const VulkanSurface *surface,
                                    const Nv2aXBatch *batch,
                                    const Nv2aCombiner *combiner)
{
    unsigned i;
    memset(constants, 0, sizeof *constants);
    constants->surface[0] = (float)surface->layout.width;
    constants->surface[1] = (float)surface->layout.height;
    constants->surface[2] = 1.0f / surface->layout.width;
    constants->surface[3] = 1.0f / surface->layout.height;
    constants->depth[1] =
        1.0f / ((((nv2a_backend_reg(0x0208) >> 4) & 0xFu) == 1)
                ? 65535.0f : 16777215.0f);
    if (batch->state->alpha_test_enable) {
        constants->alpha[0] =
            (float)(batch->state->alpha_func >= 0x200u &&
                    batch->state->alpha_func <= 0x207u
                    ? batch->state->alpha_func - 0x200u : 7u);
        constants->alpha[1] = (float)(batch->state->alpha_ref & 0xFFu);
        constants->alpha[2] = 1.0f;
    }
    memcpy(constants->fog_color, batch->fogc, sizeof constants->fog_color);
    for (i = 0; i < 8; ++i) {
        unpack_color(combiner->factor0[i], constants->c0[i]);
        unpack_color(combiner->factor1[i], constants->c1[i]);
    }
    unpack_color(combiner->final_c0, constants->final_c0);
    unpack_color(combiner->final_c1, constants->final_c1);
    memcpy(constants->color_icw, combiner->color_icw,
           sizeof constants->color_icw);
    memcpy(constants->alpha_icw, combiner->alpha_icw,
           sizeof constants->alpha_icw);
    memcpy(constants->color_ocw, combiner->color_ocw,
           sizeof constants->color_ocw);
    memcpy(constants->alpha_ocw, combiner->alpha_ocw,
           sizeof constants->alpha_ocw);
    constants->final_control[0] = combiner->final0;
    constants->final_control[1] = combiner->final1;
    constants->final_control[2] = combiner->control;
    constants->final_control[3] = combiner->stage_program;
    constants->clip_state[0] = batch->clip_plane_mode;
}

static void vulkan_xdraw(const Nv2aSurface *input, const Nv2aXBatch *batch)
{
    Nv2aCombiner default_combiner;
    const Nv2aCombiner *combiner;
    VulkanCommandSlot *slot;
    VulkanSurface *surface;
    VulkanDepth *depth;
    VulkanUpload vertices;
    VulkanUpload constant_buffer;
    VulkanUpload window;
    VulkanExtendedConstants *constants;
    VulkanPipelineKey key;
    VkPipeline pipeline;
    VkImageView texture_views[4];
    VkImageView cube_views[4];
    VkSampler samplers[4];
    VkDescriptorSet descriptors;
    VkDeviceSize offset = 0;
    float blend[4];
    size_t vertex_bytes;
    unsigned stage;
    int count_zpass;
    uint64_t draw_start;
    uint64_t perf_start;
    uint64_t texture_calls = 0;

    if (!batch || !batch->vertices || !batch->state || !batch->count)
        return;
    if (batch->count % 3 || batch->count > SIZE_MAX / sizeof(Nv2aXVertex)) {
        fprintf(stderr, "[GPU-Vulkan] invalid extended triangle-list vertex"
                        " count %u\n", batch->count);
        abort();
    }
    combiner = batch->rc;
    if (!combiner) {
        gpu_combiner_default(batch->tex_valid[0], &default_combiner);
        combiner = &default_combiner;
    }
    draw_start = vulkan_perf_begin();
    perf_start = draw_start;
    slot = begin_commands();
    surface = get_surface(input, slot);
    depth = get_depth(batch->state, surface, slot);
    if (!pipeline_key(batch->state, surface->image.format,
                      depth ? depth->image.format : VK_FORMAT_UNDEFINED,
                      0, 0, &key)) {
        fprintf(stderr, "[GPU-Vulkan] unsupported extended draw state\n");
        abort();
    }
    key.extended_shader = 1;
    if (!batch->cull)
        key.cull_mode = VK_CULL_MODE_NONE;
    if (key.cull_mode == VK_CULL_MODE_FRONT_AND_BACK) {
        finish_commands(slot);
        return;
    }
    pipeline = get_pipeline(&key);
    vulkan_perf_finish(perf_start, &s_vk.perf.setup_ticks);
    perf_start = vulkan_perf_begin();
    vertex_bytes = (size_t)batch->count * sizeof(Nv2aXVertex);
    vertices = slot_upload(slot, vertex_bytes, 4);
    memcpy(vertices.mapped, batch->vertices, vertex_bytes);
    constant_buffer = slot_upload(
        slot, sizeof(VulkanExtendedConstants),
        s_vk.properties.limits.minUniformBufferOffsetAlignment);
    constants = constant_buffer.mapped;
    fill_extended_constants(constants, surface, batch, combiner);
    window = slot_upload(
        slot, sizeof(VulkanWindowClipConstants),
        s_vk.properties.limits.minUniformBufferOffsetAlignment);
    fill_window_clip_constants(window.mapped, input, surface);
    if (perf_start)
        s_vk.perf.vertex_bytes += vertex_bytes;
    vulkan_perf_finish(perf_start, &s_vk.perf.vertex_ticks);
    perf_start = vulkan_perf_begin();
    for (stage = 0; stage < 4; ++stage) {
        uint32_t mode = (combiner->stage_program >> (stage * 5)) & 0x1Fu;
        int cube = mode == 3 && batch->tex_cube[stage];
        texture_views[stage] = s_vk.white.view;
        cube_views[stage] = s_vk.white_cube.view;
        samplers[stage] = s_vk.white_sampler;
        if (!batch->tex_valid[stage] || mode < 1 || mode > 3)
            continue;
        texture_calls++;
        if (!prepare_texture(
                slot, surface, &batch->tex[stage], cube, stage,
                cube ? &cube_views[stage] : &texture_views[stage],
                &samplers[stage]))
            continue;
        constants->texture_info[stage][0] =
            1.0f / batch->tex[stage].width;
        constants->texture_info[stage][1] =
            1.0f / batch->tex[stage].height;
        constants->texture_state[stage] =
            1u | (batch->tex_normalized[stage] ? 2u : 0u) |
            (cube ? 4u : 0u);
    }
    if (perf_start)
        s_vk.perf.texture_calls += texture_calls;
    vulkan_perf_finish(perf_start, &s_vk.perf.texture_ticks);
    perf_start = vulkan_perf_begin();
    descriptors = create_extended_descriptor_set(
        slot, texture_views, cube_views, samplers,
        constant_buffer.buffer, constant_buffer.offset,
        window.buffer, window.offset,
        VK_NULL_HANDLE, 0, 0);
    vulkan_perf_finish_call(
        perf_start, &s_vk.perf.descriptor_ticks,
        &s_vk.perf.descriptor_calls);
    unpack_color(batch->state->blend_color, blend);
    perf_start = vulkan_perf_begin();
    count_zpass = prepare_zpass_query(slot);
    begin_rendering(slot->command, surface, depth);
    start_zpass_query(slot, count_zpass);
    set_viewport_scissor(slot->command, surface, input, NULL);
    s_vk.fn.CmdBindPipeline(slot->command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline);
    offset = vertices.offset;
    s_vk.fn.CmdBindVertexBuffers(slot->command, 0, 1, &vertices.buffer,
                                 &offset);
    s_vk.fn.CmdBindDescriptorSets(slot->command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  s_vk.pipeline_layout, 0, 1, &descriptors,
                                  0, NULL);
    s_vk.fn.CmdSetBlendConstants(slot->command, blend);
    s_vk.fn.CmdDraw(slot->command, batch->count, 1, 0, 0);
    end_zpass_query(slot, input, surface, count_zpass);
    s_vk.fn.CmdEndRendering(slot->command);
    finish_commands(slot);
    vulkan_perf_finish(perf_start, &s_vk.perf.command_ticks);
    surface->gpu_dirty = 1;
    s_vk.draws++;
    vulkan_perf_finish_call(
        draw_start, &s_vk.perf.draw_ticks, &s_vk.perf.draw_calls);
}

static int vulkan_xdraw_vsh(const Nv2aSurface *input,
                            const Nv2aXVsBatch *batch)
{
    const Nv2aXBatch *xb;
    Nv2aCombiner default_combiner;
    const Nv2aCombiner *combiner;
    VulkanCommandSlot *slot;
    VulkanSurface *surface;
    VulkanDepth *depth;
    VulkanUpload data;
    VulkanUpload indices;
    VulkanUpload constant_buffer;
    VulkanUpload window;
    VulkanExtendedConstants *constants;
    VulkanPipelineKey key;
    VkPipeline pipeline;
    VkImageView texture_views[4];
    VkImageView cube_views[4];
    VkSampler samplers[4];
    VkDescriptorSet descriptors;
    float blend[4];
    size_t program_bytes;
    size_t constant_bytes;
    size_t input_bytes;
    size_t data_bytes;
    size_t index_bytes;
    unsigned expected_inputs = 0;
    unsigned stage;
    unsigned i;
    int count_zpass;
    uint64_t draw_start;
    uint64_t perf_start;
    uint64_t texture_calls = 0;

    if (!batch || !batch->program || !batch->consts || !batch->indices ||
        !batch->xb.state || !batch->ninstr || batch->ninstr > 136 ||
        !batch->nverts || !batch->nindices || batch->nindices % 3)
        return 0;
    for (i = 0; i < 16; ++i)
        expected_inputs += (batch->input_mask >> i) & 1u;
    if (batch->ninputs != expected_inputs ||
        (batch->ninputs && !batch->inputs))
        return 0;
    if (batch->nverts > SIZE_MAX /
            (batch->ninputs ? batch->ninputs : 1) /
            (4 * sizeof(*batch->inputs)) ||
        batch->nindices > SIZE_MAX / sizeof(*batch->indices))
        return 0;
    for (i = 0; i < batch->nindices; ++i)
        if (batch->indices[i] >= batch->nverts)
            return 0;

    program_bytes = (size_t)batch->ninstr * sizeof(*batch->program);
    constant_bytes = 192 * sizeof(*batch->consts);
    input_bytes = (size_t)batch->nverts * batch->ninputs *
                  4 * sizeof(*batch->inputs);
    if (program_bytes > SIZE_MAX - constant_bytes ||
        program_bytes + constant_bytes > SIZE_MAX - input_bytes ||
        program_bytes + constant_bytes + input_bytes > UINT32_MAX)
        return 0;
    data_bytes = program_bytes + constant_bytes + input_bytes;
    index_bytes = (size_t)batch->nindices * sizeof(*batch->indices);
    xb = &batch->xb;
    if (s_vk.vsh_verify.enabled)
        s_vk.vsh_verify.valid = 0;
    combiner = xb->rc;
    if (!combiner) {
        gpu_combiner_default(xb->tex_valid[0], &default_combiner);
        combiner = &default_combiner;
    }

    draw_start = vulkan_perf_begin();
    perf_start = draw_start;
    slot = begin_commands();
    surface = get_surface(input, slot);
    depth = get_depth(xb->state, surface, slot);
    if (!pipeline_key(xb->state, surface->image.format,
                      depth ? depth->image.format : VK_FORMAT_UNDEFINED,
                      0, 0, &key)) {
        finish_commands(slot);
        return 0;
    }
    key.extended_shader = 1;
    key.vsh_shader = 1;
    if (!xb->cull)
        key.cull_mode = VK_CULL_MODE_NONE;
    if (key.cull_mode == VK_CULL_MODE_FRONT_AND_BACK) {
        finish_commands(slot);
        return 1;
    }
    pipeline = get_pipeline(&key);
    vulkan_perf_finish(perf_start, &s_vk.perf.setup_ticks);

    perf_start = vulkan_perf_begin();
    data = slot_upload(
        slot, data_bytes,
        s_vk.properties.limits.minStorageBufferOffsetAlignment);
    memcpy(data.mapped, batch->program, program_bytes);
    memcpy((uint8_t *)data.mapped + program_bytes,
           batch->consts, constant_bytes);
    if (input_bytes)
        memcpy((uint8_t *)data.mapped + program_bytes + constant_bytes,
               batch->inputs, input_bytes);
    indices = slot_upload(slot, index_bytes, sizeof(*batch->indices));
    memcpy(indices.mapped, batch->indices, index_bytes);
    constant_buffer = slot_upload(
        slot, sizeof(VulkanExtendedConstants),
        s_vk.properties.limits.minUniformBufferOffsetAlignment);
    constants = constant_buffer.mapped;
    fill_extended_constants(constants, surface, xb, combiner);
    window = slot_upload(
        slot, sizeof(VulkanWindowClipConstants),
        s_vk.properties.limits.minUniformBufferOffsetAlignment);
    fill_window_clip_constants(window.mapped, input, surface);
    constants->vsh_data[0] = 0;
    constants->vsh_data[1] = (uint32_t)program_bytes;
    constants->vsh_data[2] =
        (uint32_t)(program_bytes + constant_bytes);
    constants->vsh_data[3] = batch->ninputs;
    constants->vsh_state[0] = batch->ninstr;
    constants->vsh_state[1] = batch->input_mask;
    constants->vsh_state[2] = batch->fog_enable;
    constants->vsh_state[3] = batch->fog_mode;
    constants->vsh_info[0] = batch->aa[0];
    constants->vsh_info[1] = batch->aa[1];
    constants->vsh_info[2] = xb->near_clip ? 1.0f : 0.0f;
    constants->vsh_info[3] = xb->near_z;
    constants->vsh_fog[0] = batch->fog_param[0];
    constants->vsh_fog[1] = batch->fog_param[1];
    constants->vsh_verify[0] = batch->nverts;
    if (s_vk.vsh_verify.enabled) {
        VulkanVshVerification *verification = &s_vk.vsh_verify;
        if (data_bytes > verification->data_capacity) {
            uint8_t *replacement = realloc(verification->data, data_bytes);
            if (!replacement) {
                verification->valid = 0;
                fprintf(stderr, "[VSH-VERIFY] cannot retain %zu bytes for"
                                " Vulkan verification\n", data_bytes);
            } else {
                verification->data = replacement;
                verification->data_capacity = data_bytes;
            }
        }
        if (data_bytes <= verification->data_capacity) {
            memcpy(verification->data, data.mapped, data_bytes);
            verification->data_size = data_bytes;
            verification->constants = *constants;
            verification->nverts = batch->nverts;
            verification->valid = 1;
        }
    }
    if (perf_start)
        s_vk.perf.vertex_bytes += data_bytes + index_bytes;
    vulkan_perf_finish(perf_start, &s_vk.perf.vertex_ticks);

    perf_start = vulkan_perf_begin();
    for (stage = 0; stage < 4; ++stage) {
        uint32_t mode = (combiner->stage_program >> (stage * 5)) & 0x1Fu;
        int cube = mode == 3 && xb->tex_cube[stage];
        texture_views[stage] = s_vk.white.view;
        cube_views[stage] = s_vk.white_cube.view;
        samplers[stage] = s_vk.white_sampler;
        if (!xb->tex_valid[stage] || mode < 1 || mode > 3)
            continue;
        texture_calls++;
        if (!prepare_texture(
                slot, surface, &xb->tex[stage], cube, stage,
                cube ? &cube_views[stage] : &texture_views[stage],
                &samplers[stage]))
            continue;
        constants->texture_info[stage][0] =
            1.0f / xb->tex[stage].width;
        constants->texture_info[stage][1] =
            1.0f / xb->tex[stage].height;
        constants->texture_state[stage] =
            1u | (xb->tex_normalized[stage] ? 2u : 0u) |
            (cube ? 4u : 0u);
    }
    if (perf_start)
        s_vk.perf.texture_calls += texture_calls;
    vulkan_perf_finish(perf_start, &s_vk.perf.texture_ticks);

    perf_start = vulkan_perf_begin();
    descriptors = create_extended_descriptor_set(
        slot, texture_views, cube_views, samplers,
        constant_buffer.buffer, constant_buffer.offset,
        window.buffer, window.offset,
        data.buffer, data.offset, data_bytes);
    vulkan_perf_finish_call(
        perf_start, &s_vk.perf.descriptor_ticks,
        &s_vk.perf.descriptor_calls);
    unpack_color(xb->state->blend_color, blend);

    perf_start = vulkan_perf_begin();
    count_zpass = prepare_zpass_query(slot);
    begin_rendering(slot->command, surface, depth);
    start_zpass_query(slot, count_zpass);
    set_viewport_scissor(slot->command, surface, input, NULL);
    s_vk.fn.CmdBindPipeline(slot->command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline);
    s_vk.fn.CmdBindIndexBuffer(slot->command, indices.buffer,
                               indices.offset, VK_INDEX_TYPE_UINT32);
    s_vk.fn.CmdBindDescriptorSets(
        slot->command, VK_PIPELINE_BIND_POINT_GRAPHICS,
        s_vk.pipeline_layout, 0, 1, &descriptors, 0, NULL);
    s_vk.fn.CmdSetBlendConstants(slot->command, blend);
    s_vk.fn.CmdDrawIndexed(
        slot->command, batch->nindices, 1, 0, 0, 0);
    end_zpass_query(slot, input, surface, count_zpass);
    s_vk.fn.CmdEndRendering(slot->command);
    finish_commands(slot);
    vulkan_perf_finish(perf_start, &s_vk.perf.command_ticks);
    surface->gpu_dirty = 1;
    s_vk.draws++;
    vulkan_perf_finish_call(
        draw_start, &s_vk.perf.draw_ticks, &s_vk.perf.draw_calls);
    return 1;
}

static int vulkan_vsh_readback(Nv2aXVsOut *out, uint32_t nverts)
{
    VulkanVshVerification *verification = &s_vk.vsh_verify;
    VulkanCommandSlot *slot;
    VulkanUpload data;
    VulkanUpload constants;
    VulkanUpload output;
    VkDescriptorSet descriptors;
    VkBufferMemoryBarrier barrier = {
        VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER
    };
    size_t output_bytes;
    uint32_t i;

    if (!out || !verification->valid ||
        nverts != verification->nverts || !nverts ||
        nverts > SIZE_MAX / sizeof(*out))
        return 0;
    output_bytes = (size_t)nverts * sizeof(*out);
    slot = begin_commands();
    data = slot_upload(
        slot, verification->data_size,
        s_vk.properties.limits.minStorageBufferOffsetAlignment);
    memcpy(data.mapped, verification->data, verification->data_size);
    constants = slot_upload(
        slot, sizeof(verification->constants),
        s_vk.properties.limits.minUniformBufferOffsetAlignment);
    memcpy(constants.mapped, &verification->constants,
           sizeof(verification->constants));
    output = slot_upload(
        slot, output_bytes,
        s_vk.properties.limits.minStorageBufferOffsetAlignment);
    descriptors = create_vsh_verify_descriptor_set(
        slot, &constants, &data, verification->data_size,
        &output, output_bytes);
    s_vk.fn.CmdBindPipeline(
        slot->command, VK_PIPELINE_BIND_POINT_COMPUTE,
        s_vk.vsh_verify_pipeline);
    s_vk.fn.CmdBindDescriptorSets(
        slot->command, VK_PIPELINE_BIND_POINT_COMPUTE,
        s_vk.pipeline_layout, 0, 1, &descriptors, 0, NULL);
    s_vk.fn.CmdDispatch(slot->command, (nverts + 63u) / 64u, 1, 1);
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = output.buffer;
    barrier.offset = output.offset;
    barrier.size = output_bytes;
    s_vk.fn.CmdPipelineBarrier(
        slot->command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL, 1, &barrier, 0, NULL);
    finish_commands(slot);
    if (slot->recording)
        submit_commands(slot, 1);
    else if (slot->submitted)
        wait_slot(slot);
    memcpy(out, output.mapped, output_bytes);
    for (i = 0; i < nverts; ++i) {
        float *position = out[i].pos;
        float w = position[3];
        if (w != 0.0f) {
            float depth = position[2] / w;
            position[0] = (position[0] / w + 1.0f) * 0.5f *
                          verification->constants.surface[0];
            position[1] = (1.0f - position[1] / w) * 0.5f *
                          verification->constants.surface[1];
            position[2] = verification->constants.depth[1] != 0.0f
                        ? depth / verification->constants.depth[1] +
                          verification->constants.depth[0]
                        : depth;
        }
    }
    return 1;
}

static void drain_zpass_queries(void)
{
    flush_recording();
    wait_for_submission(s_vk.last_submission_serial);
}

static void prune_zpass_results(void)
{
    uint64_t keep = s_vk.zpass_report_head != s_vk.zpass_report_tail
                  ? s_vk.zpass_reports[
                        s_vk.zpass_report_head %
                        VULKAN_MAX_VISIBILITY_REPORTS].first_query
                  : s_vk.zpass_report_query_start;
    uint64_t available = s_vk.zpass_result_base +
                         s_vk.zpass_result_count;
    size_t remove;
    if (keep <= s_vk.zpass_result_base)
        return;
    if (keep > available)
        keep = available;
    remove = (size_t)(keep - s_vk.zpass_result_base);
    if (remove < s_vk.zpass_result_count)
        memmove(s_vk.zpass_results, s_vk.zpass_results + remove,
                (s_vk.zpass_result_count - remove) *
                sizeof *s_vk.zpass_results);
    s_vk.zpass_result_count -= remove;
    s_vk.zpass_result_base = keep;
}

static uint64_t zpass_result_sum(uint64_t first, uint64_t target)
{
    uint64_t total = 0;
    size_t begin;
    size_t end;
    size_t i;
    if (first > target || first < s_vk.zpass_result_base ||
        target - s_vk.zpass_result_base > s_vk.zpass_result_count) {
        fprintf(stderr, "[GPU-Vulkan] invalid visibility result range"
                        " [%llu, %llu) at base %llu with %zu results\n",
                (unsigned long long)first,
                (unsigned long long)target,
                (unsigned long long)s_vk.zpass_result_base,
                s_vk.zpass_result_count);
        abort();
    }
    begin = (size_t)(first - s_vk.zpass_result_base);
    end = (size_t)(target - s_vk.zpass_result_base);
    for (i = begin; i < end; ++i) {
        if (s_vk.zpass_results[i] > UINT64_MAX - total)
            return UINT64_MAX;
        total += s_vk.zpass_results[i];
    }
    return total;
}

static void complete_zpass_reports(void)
{
    uint64_t perf_start = vulkan_perf_begin();
    poll_submissions();
    while (s_vk.zpass_report_head != s_vk.zpass_report_tail) {
        VulkanVisibilityReport *report =
            &s_vk.zpass_reports[
                s_vk.zpass_report_head % VULKAN_MAX_VISIBILITY_REPORTS];
        uint64_t available = s_vk.zpass_result_base +
                             s_vk.zpass_result_count;
        uint64_t count;
        uint32_t guest_count;
        uint8_t *destination;
        if (!report->submission_assigned ||
            report->submission_target > s_vk.completed_submission_serial ||
            report->query_target > available)
            break;
        count = zpass_result_sum(report->first_query,
                                 report->query_target);
        if (report->reset)
            s_vk.zpass_report_running = 0;
        if (count > UINT64_MAX - s_vk.zpass_report_running)
            s_vk.zpass_report_running = UINT64_MAX;
        else
            s_vk.zpass_report_running += count;
        guest_count = s_vk.zpass_report_running > UINT32_MAX
                    ? UINT32_MAX : (uint32_t)s_vk.zpass_report_running;
        destination = GUEST(report->va);
        memcpy(destination, &report->stamp, sizeof report->stamp);
        memcpy(destination + 8, &guest_count, sizeof guest_count);
        memset(destination + 12, 0, 4);
        s_vk.zpass_report_head++;
        s_vk.zpass_reports_completed++;
        prune_zpass_results();
    }
    vulkan_perf_finish_call(
        perf_start, &s_vk.perf.report_complete_ticks,
        &s_vk.perf.report_complete_calls);
}

static int vulkan_zpass_report(uint32_t report_va, uint64_t stamp)
{
    VulkanVisibilityReport *report;
    uint64_t perf_start;
    if (!s_vk.zpass_async)
        return 0;
    perf_start = vulkan_perf_begin();
    claim_device_thread();
    s_vk.zpass_reads++;
    complete_zpass_reports();
    while (s_vk.zpass_report_tail - s_vk.zpass_report_head ==
           VULKAN_MAX_VISIBILITY_REPORTS) {
        VulkanVisibilityReport *oldest =
            &s_vk.zpass_reports[
                s_vk.zpass_report_head % VULKAN_MAX_VISIBILITY_REPORTS];
        if (!oldest->submission_assigned)
            flush_recording();
        if (oldest->submission_assigned)
            wait_for_submission(oldest->submission_target);
        complete_zpass_reports();
    }
    report = &s_vk.zpass_reports[
        s_vk.zpass_report_tail % VULKAN_MAX_VISIBILITY_REPORTS];
    report->va = report_va;
    report->stamp = stamp;
    report->first_query = s_vk.zpass_report_query_start;
    report->query_target = s_vk.zpass_queries;
    report->queued_ms = monotonic_ms();
    if (s_vk.recording_slot) {
        report->submission_target = 0;
        report->submission_assigned = 0;
    } else {
        report->submission_target = s_vk.last_submission_serial;
        report->submission_assigned = 1;
    }
    report->reset = s_vk.zpass_report_reset_next;
    s_vk.zpass_report_query_start = report->query_target;
    s_vk.zpass_report_reset_next = 0;
    s_vk.zpass_report_tail++;
    complete_zpass_reports();
    vulkan_perf_finish_call(
        perf_start, &s_vk.perf.report_ticks,
        &s_vk.perf.report_calls);
    return 1;
}

static void vulkan_poll(void)
{
    LONG owner;
    VulkanVisibilityReport *report;
    uint64_t perf_start;
    double age_ms;
    if (!s_vk.zpass_async)
        return;
    owner = InterlockedCompareExchange(&s_vk.device_thread, 0, 0);
    if (!owner || (DWORD)owner != GetCurrentThreadId())
        return;
    if (s_vk.zpass_report_head != s_vk.zpass_report_tail) {
        report = &s_vk.zpass_reports[
            s_vk.zpass_report_head % VULKAN_MAX_VISIBILITY_REPORTS];
        if (!report->submission_assigned) {
            age_ms = monotonic_ms() - report->queued_ms;
            if (age_ms < VULKAN_ZPASS_FLUSH_DELAY_MS) {
                s_vk.zpass_report_polls_deferred++;
                if (g_bruteforce_perf_counters_enabled)
                    s_vk.perf.report_deferred_calls++;
                return;
            }
        }
    }
    perf_start = vulkan_perf_begin();
    if (s_vk.zpass_report_head != s_vk.zpass_report_tail) {
        report = &s_vk.zpass_reports[
            s_vk.zpass_report_head % VULKAN_MAX_VISIBILITY_REPORTS];
        if (!report->submission_assigned)
            flush_recording();
    }
    complete_zpass_reports();
    vulkan_perf_finish_call(
        perf_start, &s_vk.perf.report_poll_ticks,
        &s_vk.perf.report_poll_calls);
}

static void vulkan_zpass(int op)
{
    claim_device_thread();
    if (op == NV2A_ZPASS_BEGIN) {
        s_vk.zpass_enabled = 1;
    } else if (op == NV2A_ZPASS_END) {
        s_vk.zpass_enabled = 0;
    } else if (op == NV2A_ZPASS_CLEAR) {
        if (s_vk.zpass_async) {
            s_vk.zpass_report_query_start = s_vk.zpass_queries;
            s_vk.zpass_report_reset_next = 1;
            prune_zpass_results();
        } else {
            drain_zpass_queries();
            s_vk.zpass_total = 0;
        }
    } else {
        fprintf(stderr, "[GPU-Vulkan] invalid visibility operation %d\n", op);
        abort();
    }
}

static uint32_t vulkan_zpass_count(void)
{
    claim_device_thread();
    drain_zpass_queries();
    s_vk.zpass_reads++;
    return s_vk.zpass_total > UINT32_MAX ? UINT32_MAX
                                         : (uint32_t)s_vk.zpass_total;
}

static int vulkan_readback_internal(uint32_t color_va, int publish_source)
{
    VulkanSurface *surface = find_surface(color_va);
    VulkanCommandSlot *slot;
    VulkanBuffer *readback;
    BruteforceVulkanPresentSource *present_source = NULL;
    VkBufferImageCopy copy;
    size_t row;
    size_t guest_row;
    size_t bytes;
    uint16_t *converted = NULL;
    uint32_t y;
    uint64_t perf_start;
    GuestSpanIdentity identity;
    int sync_result;

    if (!surface)
        return 0;
    perf_start = vulkan_perf_begin();
    sync_result = sync_surface_guest(surface, NULL, 1, 0);
    if (!sync_result)
        return 0;
    if (publish_source && s_vk.present_registered) {
        if (surface->guest_pending_upload)
            s_vk.present_sources_suppressed++;
        else
            present_source = bruteforce_vulkan_present_begin_source(
                surface->pw, surface->ph,
                xbox_FramebufferWindowNextFrameId());
    }
    if (!surface->gpu_dirty) {
        if (present_source) {
            slot = begin_commands();
            image_barrier(slot->command, &surface->image,
                          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                          VK_PIPELINE_STAGE_TRANSFER_BIT,
                          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                          VK_ACCESS_TRANSFER_READ_BIT);
            bruteforce_vulkan_present_record_source(
                present_source, slot->command, surface->image.image);
            image_barrier(slot->command, &surface->image,
                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                          VK_PIPELINE_STAGE_TRANSFER_BIT,
                          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                          VK_ACCESS_TRANSFER_READ_BIT,
                          VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
            slot->operation_count++;
            submit_commands(slot, 1);
            bruteforce_vulkan_present_finish_source(present_source);
        } else {
            flush_recording();
            wait_for_submission(s_vk.last_submission_serial);
        }
        vulkan_perf_finish_call(
            perf_start, &s_vk.perf.readback_ticks,
            &s_vk.perf.readback_calls);
        return 1;
    }
    row = (size_t)surface->layout.width * sizeof(uint32_t);
    guest_row = (size_t)surface->layout.width * surface->layout.bytes_per_pixel;
    if (surface->layout.height > SIZE_MAX / row)
        runtime_failure("sizing surface readback", VK_ERROR_OUT_OF_HOST_MEMORY);
    bytes = row * surface->layout.height;
    slot = begin_commands();
    readback = slot_readback(slot, bytes);
    memset(&copy, 0, sizeof copy);
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent.width = surface->layout.width;
    copy.imageExtent.height = surface->layout.height;
    copy.imageExtent.depth = 1;
    if (surface_scaled(surface)) {
        VkImageLayout old_layout = surface->image.layout;
        /*
         * Guest memory receives the logical picture: box-downsample (or
         * bilinear-upsample) the physical target into the logical image.
         * The physical target remains authoritative for later GPU work.
         */
        ensure_surface_logical(surface);
        image_barrier(slot->command, &surface->image,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      image_layout_stage(old_layout),
                      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                      image_layout_access(old_layout),
                      VK_ACCESS_SHADER_READ_BIT);
        record_scale_blit(slot, &surface->image, surface->pw, surface->ph,
                          &surface->logical,
                          surface->layout.width, surface->layout.height);
        image_barrier(slot->command, &surface->logical,
                      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                      VK_PIPELINE_STAGE_TRANSFER_BIT,
                      VK_PIPELINE_STAGE_TRANSFER_BIT,
                      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                      VK_ACCESS_TRANSFER_READ_BIT);
        s_vk.fn.CmdCopyImageToBuffer(slot->command, surface->logical.image,
                                     VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                     readback->buffer, 1, &copy);
        image_barrier(slot->command, &surface->image,
                      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                      VK_PIPELINE_STAGE_TRANSFER_BIT,
                      VK_ACCESS_SHADER_READ_BIT,
                      VK_ACCESS_TRANSFER_READ_BIT);
    } else {
        image_barrier(slot->command, &surface->image,
                      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_PIPELINE_STAGE_TRANSFER_BIT,
                      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                      VK_ACCESS_TRANSFER_READ_BIT);
        s_vk.fn.CmdCopyImageToBuffer(slot->command, surface->image.image,
                                     VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                     readback->buffer, 1, &copy);
    }
    if (present_source)
        bruteforce_vulkan_present_record_source(
            present_source, slot->command, surface->image.image);
    image_barrier(slot->command, &surface->image,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_TRANSFER_READ_BIT,
                  VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    slot->operation_count++;
    submit_commands(slot, 1);
    if (present_source)
        bruteforce_vulkan_present_finish_source(present_source);
    if (!readable_guest_range(color_va, surface->layout.span_bytes, 1)) {
        fprintf(stderr, "[GPU-Vulkan] guest color surface %08X is not writable\n", color_va);
        abort();
    }
    if (surface->layout.bytes_per_pixel == 2) {
        converted = malloc(guest_row);
        if (!converted)
            runtime_failure("allocating RGB565 readback conversion",
                            VK_ERROR_OUT_OF_HOST_MEMORY);
    }
    for (y = 0; y < surface->layout.height; ++y) {
        const uint8_t *source = (const uint8_t *)readback->mapped + (size_t)y * row;
        const void *write_source = source;
        uint32_t x;
        if (converted) {
            const uint32_t *pixels = (const uint32_t *)source;
            for (x = 0; x < surface->layout.width; ++x) {
                uint32_t pixel = pixels[x];
                converted[x] = (uint16_t)(((pixel >> 8) & 0xF800u) |
                                          ((pixel >> 5) & 0x07E0u) |
                                          ((pixel >> 3) & 0x001Fu));
            }
            write_source = converted;
        }
        if (!guarded_guest_write(
                GUEST(color_va) + (size_t)y * surface->layout.pitch,
                write_source, guest_row)) {
            free(converted);
            fprintf(stderr, "[GPU-Vulkan] guest color write for %08X failed\n", color_va);
            abort();
        }
    }
    free(converted);
    if (!snapshot_surface_guest(surface, &identity))
        return 0;
    remember_surface_guest(surface, &identity);
    surface->guest_pending_upload = 0;
    surface->gpu_dirty = 0;
    s_vk.readbacks++;
    if (perf_start)
        s_vk.perf.readback_bytes += surface->layout.span_bytes;
    vulkan_perf_finish_call(
        perf_start, &s_vk.perf.readback_ticks,
        &s_vk.perf.readback_calls);
    return 1;
}

static int vulkan_readback(uint32_t color_va)
{
    return vulkan_readback_internal(color_va, 0);
}

static int vulkan_flip_readback(uint32_t color_va)
{
    int tracked = find_surface(color_va) != NULL;
    int result = vulkan_readback_internal(color_va, 1);
    if (tracked && !result) {
        fprintf(stderr, "[GPU-Vulkan] cannot make tracked color surface %08X guest-visible;"
                        " stopping before presentation\n", color_va);
        abort();
    }
    return result;
}

static void report_vulkan_perf(double interval_ms)
{
    VulkanPerfStats *perf = &s_vk.perf;
    fprintf(stderr,
            "[PERF-VULKAN] interval_ms=%.3f draws=%llu draw_ms=%.3f"
            " setup_ms=%.3f vertex_bytes=%llu vertex_ms=%.3f"
            " texture_calls=%llu texture_ms=%.3f"
            " snapshots=%llu snapshot_bytes=%llu snapshot_ms=%.3f"
            " lookups=%llu lookup_ms=%.3f"
            " exact_checks=%llu exact_bytes=%llu exact_ms=%.3f"
            " uploads=%llu upload_ms=%.3f"
            " descriptors=%llu descriptor_ms=%.3f command_ms=%.3f"
            " readbacks=%llu readback_bytes=%llu readback_ms=%.3f"
            " waits=%llu wait_ms=%.3f"
            " reports=%llu report_ms=%.3f"
            " report_polls=%llu report_poll_ms=%.3f"
            " report_deferred=%llu"
            " report_completes=%llu report_complete_ms=%.3f"
            " fence_polls=%llu fence_poll_ms=%.3f\n",
            interval_ms,
            (unsigned long long)perf->draw_calls,
            bruteforce_perf_ticks_ms(perf->draw_ticks),
            bruteforce_perf_ticks_ms(perf->setup_ticks),
            (unsigned long long)perf->vertex_bytes,
            bruteforce_perf_ticks_ms(perf->vertex_ticks),
            (unsigned long long)perf->texture_calls,
            bruteforce_perf_ticks_ms(perf->texture_ticks),
            (unsigned long long)perf->snapshot_calls,
            (unsigned long long)perf->snapshot_bytes,
            bruteforce_perf_ticks_ms(perf->snapshot_ticks),
            (unsigned long long)perf->lookup_calls,
            bruteforce_perf_ticks_ms(perf->lookup_ticks),
            (unsigned long long)perf->exact_checks,
            (unsigned long long)perf->exact_bytes,
            bruteforce_perf_ticks_ms(perf->exact_ticks),
            (unsigned long long)perf->upload_calls,
            bruteforce_perf_ticks_ms(perf->upload_ticks),
            (unsigned long long)perf->descriptor_calls,
            bruteforce_perf_ticks_ms(perf->descriptor_ticks),
            bruteforce_perf_ticks_ms(perf->command_ticks),
            (unsigned long long)perf->readback_calls,
            (unsigned long long)perf->readback_bytes,
            bruteforce_perf_ticks_ms(perf->readback_ticks),
            (unsigned long long)perf->wait_calls,
            bruteforce_perf_ticks_ms(perf->wait_ticks),
            (unsigned long long)perf->report_calls,
            bruteforce_perf_ticks_ms(perf->report_ticks),
            (unsigned long long)perf->report_poll_calls,
            bruteforce_perf_ticks_ms(perf->report_poll_ticks),
            (unsigned long long)perf->report_deferred_calls,
            (unsigned long long)perf->report_complete_calls,
            bruteforce_perf_ticks_ms(perf->report_complete_ticks),
            (unsigned long long)perf->fence_poll_calls,
            bruteforce_perf_ticks_ms(perf->fence_poll_ticks));
    memset(perf, 0, sizeof *perf);
}

static void vulkan_flip(void)
{
    double now;
    double interval_ms;
    uint64_t interval_flips;

    s_vk.flips++;
    if (!s_vk.diagnostic_last_ms) {
        s_vk.diagnostic_last_ms = monotonic_ms();
        s_vk.diagnostic_last_flip = 0;
    }
    if ((s_vk.flips % 300) == 0) {
        now = monotonic_ms();
        interval_ms = now - s_vk.diagnostic_last_ms;
        interval_flips = s_vk.flips - s_vk.diagnostic_last_flip;
        fprintf(stderr, "[GPU-Vulkan] %llu flips, %llu draws, %llu clears,"
                        " %llu readbacks, %llu guest uploads,"
                        " %llu guest preserves, %llu guest conflicts,"
                        " %llu native sources suppressed,"
                        " %u surfaces, %u pipelines, %u textures,"
                        " %llu cache hits, %llu lookup probes, %llu uploads,"
                        " %llu mip uploads,"
                        " %llu visibility queries, %llu visibility reads,"
                        " %llu async reports, %llu pending,"
                        " %llu deferred report polls,"
                        " %llu submissions, %llu fence waits, max batch %llu,"
                        " %llu upload buffers, %llu descriptor pools,"
                        " %u samplers, %llu scale resolves,"
                        " interval %llu flips in %.3f ms"
                        " (%.3f flips/s)\n",
                (unsigned long long)s_vk.flips,
                (unsigned long long)s_vk.draws,
                (unsigned long long)s_vk.clears,
                (unsigned long long)s_vk.readbacks,
                (unsigned long long)s_vk.guest_surface_uploads,
                (unsigned long long)s_vk.guest_surface_preserves,
                (unsigned long long)s_vk.guest_surface_conflicts,
                (unsigned long long)s_vk.present_sources_suppressed,
                s_vk.surface_count, s_vk.pipeline_count, s_vk.texture_count,
                (unsigned long long)s_vk.texture_hits,
                (unsigned long long)s_vk.texture_lookup_probes,
                (unsigned long long)s_vk.texture_uploads,
                (unsigned long long)s_vk.texture_mip_uploads,
                (unsigned long long)s_vk.zpass_queries,
                (unsigned long long)s_vk.zpass_reads,
                (unsigned long long)s_vk.zpass_reports_completed,
                (unsigned long long)(s_vk.zpass_report_tail -
                                     s_vk.zpass_report_head),
                (unsigned long long)s_vk.zpass_report_polls_deferred,
                (unsigned long long)s_vk.submissions,
                (unsigned long long)s_vk.submission_waits,
                (unsigned long long)s_vk.max_batch_operations,
                (unsigned long long)s_vk.upload_buffer_creates,
                (unsigned long long)s_vk.descriptor_pool_creates,
                s_vk.sampler_count,
                (unsigned long long)s_vk.scale_resolves,
                (unsigned long long)interval_flips,
                interval_ms,
                interval_ms > 0.0
                    ? (double)interval_flips * 1000.0 / interval_ms : 0.0);
        if (g_bruteforce_perf_counters_enabled)
            report_vulkan_perf(interval_ms);
        s_vk.diagnostic_last_ms = now;
        s_vk.diagnostic_last_flip = s_vk.flips;
    }
}

static const Nv2aBackend s_backend = {
    vulkan_clear,
    vulkan_draw,
    vulkan_flip,
};

static const Nv2aBackendExt s_backend_ext = {
    vulkan_xdraw,
    vulkan_xdraw_vsh,
    vulkan_vsh_readback,
    NULL,
    vulkan_flip_readback,
    vulkan_zpass,
    vulkan_zpass_count,
    vulkan_zpass_report,
    vulkan_poll,
    NV2A_BACKEND_CAP_WINDOW_CLIP,
};

static void destroy_state(void)
{
    unsigned i;
    int idle = 0;
    if (s_vk.present_registered) {
        if (!bruteforce_vulkan_present_unregister()) {
            fprintf(stderr, "[GPU-Vulkan] presentation provider is still active;"
                            " refusing to destroy its Vulkan device\n");
            abort();
        }
        s_vk.present_registered = 0;
    }
    if (s_vk.device && s_vk.fn.DeviceWaitIdle) {
        flush_recording();
        bruteforce_vulkan_present_queue_lock();
        idle = s_vk.fn.DeviceWaitIdle(s_vk.device) == VK_SUCCESS;
        bruteforce_vulkan_present_queue_unlock();
        if (idle && s_vk.last_submission_serial >
                    s_vk.completed_submission_serial)
            retire_through(s_vk.last_submission_serial);
        if (idle && s_vk.zpass_async)
            complete_zpass_reports();
    }
    if (s_vk.device) {
        for (i = 0; i < VULKAN_COMMAND_SLOTS; ++i) {
            s_vk.slots[i].submitted = 0;
            destroy_slot(&s_vk.slots[i]);
        }
        for (i = 0; i < s_vk.texture_count; ++i)
            release_cached_texture(&s_vk.textures[i]);
        for (i = 0; i < s_vk.sampler_count; ++i)
            if (s_vk.samplers[i].sampler)
                s_vk.fn.DestroySampler(
                    s_vk.device, s_vk.samplers[i].sampler, NULL);
        for (i = 0; i < s_vk.pipeline_count; ++i)
            if (s_vk.pipelines[i].pipeline)
                s_vk.fn.DestroyPipeline(s_vk.device, s_vk.pipelines[i].pipeline, NULL);
        if (s_vk.vsh_verify_pipeline)
            s_vk.fn.DestroyPipeline(
                s_vk.device, s_vk.vsh_verify_pipeline, NULL);
        for (i = 0; i < s_vk.surface_count; ++i) {
            destroy_image(&s_vk.surfaces[i].sample_copy);
            destroy_image(&s_vk.surfaces[i].logical);
        }
        for (i = 0; i < s_vk.surface_count; ++i) {
            destroy_image(&s_vk.surfaces[i].image);
            free(s_vk.surfaces[i].guest_shadow);
        }
        for (i = 0; i < s_vk.depth_count; ++i)
            destroy_image(&s_vk.depths[i].image);
        if (s_vk.white_sampler)
            s_vk.fn.DestroySampler(s_vk.device, s_vk.white_sampler, NULL);
        if (s_vk.resolve_sampler)
            s_vk.fn.DestroySampler(s_vk.device, s_vk.resolve_sampler, NULL);
        destroy_image(&s_vk.white_cube);
        destroy_image(&s_vk.white);
        if (s_vk.base_vs) s_vk.fn.DestroyShaderModule(s_vk.device, s_vk.base_vs, NULL);
        if (s_vk.base_ps) s_vk.fn.DestroyShaderModule(s_vk.device, s_vk.base_ps, NULL);
        if (s_vk.clear_vs) s_vk.fn.DestroyShaderModule(s_vk.device, s_vk.clear_vs, NULL);
        if (s_vk.clear_ps) s_vk.fn.DestroyShaderModule(s_vk.device, s_vk.clear_ps, NULL);
        if (s_vk.extended_vs) s_vk.fn.DestroyShaderModule(s_vk.device, s_vk.extended_vs, NULL);
        if (s_vk.extended_ps) s_vk.fn.DestroyShaderModule(s_vk.device, s_vk.extended_ps, NULL);
        if (s_vk.vsh_vs) s_vk.fn.DestroyShaderModule(s_vk.device, s_vk.vsh_vs, NULL);
        if (s_vk.vsh_gs) s_vk.fn.DestroyShaderModule(s_vk.device, s_vk.vsh_gs, NULL);
        if (s_vk.vsh_verify_cs)
            s_vk.fn.DestroyShaderModule(
                s_vk.device, s_vk.vsh_verify_cs, NULL);
        if (s_vk.resolve_vs)
            s_vk.fn.DestroyShaderModule(s_vk.device, s_vk.resolve_vs, NULL);
        if (s_vk.resolve_bilinear_ps)
            s_vk.fn.DestroyShaderModule(
                s_vk.device, s_vk.resolve_bilinear_ps, NULL);
        if (s_vk.resolve_box_ps)
            s_vk.fn.DestroyShaderModule(
                s_vk.device, s_vk.resolve_box_ps, NULL);
        if (s_vk.pipeline_layout)
            s_vk.fn.DestroyPipelineLayout(s_vk.device, s_vk.pipeline_layout, NULL);
        if (s_vk.descriptor_layout)
            s_vk.fn.DestroyDescriptorSetLayout(s_vk.device, s_vk.descriptor_layout, NULL);
        if (s_vk.zpass_query_pool)
            s_vk.fn.DestroyQueryPool(s_vk.device, s_vk.zpass_query_pool, NULL);
        for (i = 0; i < VULKAN_COMMAND_SLOTS; ++i)
            if (s_vk.slots[i].fence)
                s_vk.fn.DestroyFence(s_vk.device, s_vk.slots[i].fence, NULL);
        if (s_vk.command_pool)
            s_vk.fn.DestroyCommandPool(s_vk.device, s_vk.command_pool, NULL);
        if (s_vk.fn.DestroyDevice)
            s_vk.fn.DestroyDevice(s_vk.device, NULL);
    }
    free(s_vk.surface_probe);
    free(s_vk.surface_decode);
    free(s_vk.texture_probe);
    free(s_vk.vsh_verify.data);
    free(s_vk.texture_decode);
    free(s_vk.zpass_results);
    if (s_vk.debug_messenger && s_vk.fn.DestroyDebugUtilsMessengerEXT)
        s_vk.fn.DestroyDebugUtilsMessengerEXT(
            s_vk.instance, s_vk.debug_messenger, NULL);
    if (s_vk.instance && s_vk.fn.DestroyInstance)
        s_vk.fn.DestroyInstance(s_vk.instance, NULL);
    if (s_vk.loader)
        FreeLibrary(s_vk.loader);
    memset(&s_vk, 0, sizeof s_vk);
}

static int has_layer(const char *name)
{
    VkLayerProperties *layers;
    uint32_t count = 0;
    uint32_t i;
    int found = 0;
    if (s_vk.fn.EnumerateInstanceLayerProperties(&count, NULL) != VK_SUCCESS || !count)
        return 0;
    layers = calloc(count, sizeof *layers);
    if (!layers)
        return 0;
    if (s_vk.fn.EnumerateInstanceLayerProperties(&count, layers) == VK_SUCCESS)
        for (i = 0; i < count; ++i)
            if (strcmp(layers[i].layerName, name) == 0) {
                found = 1;
                break;
            }
    free(layers);
    return found;
}

static int has_instance_extension(const char *name)
{
    VkExtensionProperties *extensions;
    uint32_t count = 0;
    uint32_t i;
    int found = 0;
    if (s_vk.fn.EnumerateInstanceExtensionProperties(NULL, &count, NULL) != VK_SUCCESS ||
        !count)
        return 0;
    extensions = calloc(count, sizeof *extensions);
    if (!extensions)
        return 0;
    if (s_vk.fn.EnumerateInstanceExtensionProperties(NULL, &count, extensions) == VK_SUCCESS)
        for (i = 0; i < count; ++i)
            if (strcmp(extensions[i].extensionName, name) == 0) {
                found = 1;
                break;
            }
    free(extensions);
    return found;
}

static int has_device_extension(VkPhysicalDevice device, const char *name)
{
    VkExtensionProperties *extensions;
    uint32_t count = 0;
    uint32_t i;
    int found = 0;
    if (s_vk.fn.EnumerateDeviceExtensionProperties(
            device, NULL, &count, NULL) != VK_SUCCESS || !count)
        return 0;
    extensions = calloc(count, sizeof *extensions);
    if (!extensions)
        return 0;
    if (s_vk.fn.EnumerateDeviceExtensionProperties(
            device, NULL, &count, extensions) == VK_SUCCESS)
        for (i = 0; i < count; ++i)
            if (strcmp(extensions[i].extensionName, name) == 0) {
                found = 1;
                break;
            }
    free(extensions);
    return found;
}

static int device_queue_family(VkPhysicalDevice device, uint32_t *family)
{
    VkQueueFamilyProperties *properties;
    uint32_t count = 0;
    uint32_t i;
    s_vk.fn.GetPhysicalDeviceQueueFamilyProperties(device, &count, NULL);
    if (!count)
        return 0;
    properties = calloc(count, sizeof *properties);
    if (!properties)
        return 0;
    s_vk.fn.GetPhysicalDeviceQueueFamilyProperties(device, &count, properties);
    for (i = 0; i < count; ++i)
        if (properties[i].queueCount &&
            (properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            *family = i;
            free(properties);
            return 1;
        }
    free(properties);
    return 0;
}

static int format_supported(VkPhysicalDevice device, VkFormat format,
                            VkFormatFeatureFlags required)
{
    VkFormatProperties properties;
    s_vk.fn.GetPhysicalDeviceFormatProperties(device, format, &properties);
    return (properties.optimalTilingFeatures & required) == required;
}

static int choose_depth_formats(VkPhysicalDevice device,
                                VkFormat *depth16, VkFormat *depth_stencil)
{
    if (format_supported(device, VK_FORMAT_D16_UNORM,
                         VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
        *depth16 = VK_FORMAT_D16_UNORM;
    else if (format_supported(device, VK_FORMAT_D32_SFLOAT,
                              VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
        *depth16 = VK_FORMAT_D32_SFLOAT;
    else
        return 0;
    if (format_supported(device, VK_FORMAT_D24_UNORM_S8_UINT,
                         VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
        *depth_stencil = VK_FORMAT_D24_UNORM_S8_UINT;
    else if (format_supported(device, VK_FORMAT_D32_SFLOAT_S8_UINT,
                              VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
        *depth_stencil = VK_FORMAT_D32_SFLOAT_S8_UINT;
    else
        return 0;
    return 1;
}

static int device_suitable(VkPhysicalDevice device, uint32_t *queue_family,
                           VkPhysicalDeviceProperties *properties,
                           VkFormat *depth16, VkFormat *depth_stencil,
                           char *reason, size_t reason_size)
{
    VkPhysicalDeviceVulkan13Features features13 = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES
    };
    VkPhysicalDeviceFeatures2 features = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2
    };
    VkFormatProperties color;
    VkFormatFeatureFlags color_features =
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
        VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
        VK_FORMAT_FEATURE_TRANSFER_DST_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    int has_queue;
    if (s_vk.native_present)
        color_features |= VK_FORMAT_FEATURE_BLIT_SRC_BIT;
    features.pNext = &features13;
    s_vk.fn.GetPhysicalDeviceProperties(device, properties);
    s_vk.fn.GetPhysicalDeviceFeatures2(device, &features);
    has_queue = device_queue_family(device, queue_family);
    s_vk.fn.GetPhysicalDeviceFormatProperties(
        device, VK_FORMAT_B8G8R8A8_UNORM, &color);
    if (properties->apiVersion < VK_API_VERSION_1_3)
        snprintf(reason, reason_size, "API %u.%u is older than 1.3",
                 VK_API_VERSION_MAJOR(properties->apiVersion),
                 VK_API_VERSION_MINOR(properties->apiVersion));
    else if (!features13.dynamicRendering)
        snprintf(reason, reason_size, "dynamicRendering is unavailable");
    else if (!features13.shaderDemoteToHelperInvocation)
        snprintf(reason, reason_size,
                 "shaderDemoteToHelperInvocation is unavailable");
    else if (!features.features.geometryShader)
        snprintf(reason, reason_size, "geometry shaders are unavailable");
    else if (!features.features.shaderClipDistance)
        snprintf(reason, reason_size, "shader clip distances are unavailable");
    else if (!features.features.occlusionQueryPrecise)
        snprintf(reason, reason_size, "precise occlusion queries are unavailable");
    else if (!has_queue)
        snprintf(reason, reason_size, "no graphics queue is available");
    else if (s_vk.native_present &&
             !has_device_extension(device, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
        snprintf(reason, reason_size, "VK_KHR_swapchain is unavailable");
    else if (!format_supported(device, VK_FORMAT_B8G8R8A8_UNORM,
                               color_features))
        snprintf(reason, reason_size, "B8G8R8A8 features are 0x%08X",
                 color.optimalTilingFeatures);
    else if (!choose_depth_formats(device, depth16, depth_stencil))
        snprintf(reason, reason_size,
                 "no supported 16-bit depth and depth/stencil format pair");
    else
        return 1;
    return 0;
}

static int device_score(const VkPhysicalDeviceProperties *properties)
{
    switch (properties->deviceType) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return 500;
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return 400;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return 300;
    case VK_PHYSICAL_DEVICE_TYPE_CPU: return 200;
    default: return 100;
    }
}

static BruteforceRendererStartupResult create_shader(
    const uint32_t *code, size_t size, VkShaderModule *module)
{
    VkShaderModuleCreateInfo info = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    VkResult result;
    info.codeSize = size;
    info.pCode = code;
    result = s_vk.fn.CreateShaderModule(s_vk.device, &info, NULL, module);
    if (result != VK_SUCCESS)
        return startup_result(BRUTEFORCE_RENDERER_STARTUP_INITIALIZATION_FAILED,
                              "probe-pipeline", result,
                              "cannot create a pinned SPIR-V shader module");
    return startup_result(BRUTEFORCE_RENDERER_STARTUP_SUCCESS, "probe-pipeline",
                          VK_SUCCESS, "shader module created");
}

static BruteforceRendererStartupResult create_device_objects(void)
{
    VkCommandPoolCreateInfo pool = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    VkCommandBufferAllocateInfo commands = {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO
    };
    VkFenceCreateInfo fence = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkQueryPoolCreateInfo query_pool = {
        VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO
    };
    VkDescriptorSetLayoutBinding bindings[16];
    VkDescriptorSetLayoutCreateInfo descriptors = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO
    };
    VkPushConstantRange push;
    VkPipelineLayoutCreateInfo pipeline_layout = {
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO
    };
    VkComputePipelineCreateInfo compute = {
        VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO
    };
    BruteforceRendererStartupResult result;
    VulkanPipelineKey key;
    VkResult vk_result;
    VkCommandBuffer command_buffers[VULKAN_COMMAND_SLOTS];
    unsigned i;

    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex = s_vk.queue_family;
    vk_result = s_vk.fn.CreateCommandPool(s_vk.device, &pool, NULL, &s_vk.command_pool);
    if (vk_result != VK_SUCCESS)
        return startup_result(BRUTEFORCE_RENDERER_STARTUP_INITIALIZATION_FAILED,
                              "command-queue", vk_result,
                              "cannot create the Vulkan command pool");
    commands.commandPool = s_vk.command_pool;
    commands.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commands.commandBufferCount = VULKAN_COMMAND_SLOTS;
    vk_result = s_vk.fn.AllocateCommandBuffers(
        s_vk.device, &commands, command_buffers);
    if (vk_result != VK_SUCCESS)
        return startup_result(BRUTEFORCE_RENDERER_STARTUP_INITIALIZATION_FAILED,
                              "command-queue", vk_result,
                              "cannot allocate bounded command buffers");
    for (i = 0; i < VULKAN_COMMAND_SLOTS; ++i)
        s_vk.slots[i].command = command_buffers[i];
    fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (i = 0; i < VULKAN_COMMAND_SLOTS; ++i) {
        vk_result = s_vk.fn.CreateFence(s_vk.device, &fence, NULL, &s_vk.slots[i].fence);
        if (vk_result != VK_SUCCESS)
            return startup_result(BRUTEFORCE_RENDERER_STARTUP_INITIALIZATION_FAILED,
                                  "command-queue", vk_result,
                                  "cannot create a bounded submission fence");
    }
    query_pool.queryType = VK_QUERY_TYPE_OCCLUSION;
    query_pool.queryCount =
        VULKAN_COMMAND_SLOTS * VULKAN_BATCH_OPERATIONS;
    vk_result = s_vk.fn.CreateQueryPool(
        s_vk.device, &query_pool, NULL, &s_vk.zpass_query_pool);
    if (vk_result != VK_SUCCESS)
        return startup_result(BRUTEFORCE_RENDERER_STARTUP_INITIALIZATION_FAILED,
                              "command-queue", vk_result,
                              "cannot create the visibility query pool");
    memset(bindings, 0, sizeof bindings);
    for (i = 0; i < 4; ++i) {
        bindings[i * 2].binding = i * 2;
        bindings[i * 2].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        bindings[i * 2].descriptorCount = 1;
        bindings[i * 2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[i * 2 + 1].binding = i * 2 + 1;
        bindings[i * 2 + 1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        bindings[i * 2 + 1].descriptorCount = 1;
        bindings[i * 2 + 1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    bindings[8].binding = 8;
    bindings[8].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[8].descriptorCount = 1;
    bindings[8].stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
                             VK_SHADER_STAGE_FRAGMENT_BIT |
                             VK_SHADER_STAGE_COMPUTE_BIT;
    for (i = 9; i < 13; ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    bindings[13].binding = 13;
    bindings[13].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[13].descriptorCount = 1;
    bindings[13].stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
                              VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[14].binding = 14;
    bindings[14].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[14].descriptorCount = 1;
    bindings[14].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[15].binding = 15;
    bindings[15].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[15].descriptorCount = 1;
    bindings[15].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    descriptors.bindingCount = 16;
    descriptors.pBindings = bindings;
    vk_result = s_vk.fn.CreateDescriptorSetLayout(
        s_vk.device, &descriptors, NULL, &s_vk.descriptor_layout);
    if (vk_result != VK_SUCCESS)
        return startup_result(BRUTEFORCE_RENDERER_STARTUP_INITIALIZATION_FAILED,
                              "probe-pipeline", vk_result,
                              "cannot create the base descriptor layout");
    memset(&push, 0, sizeof push);
    push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    push.size = sizeof(VulkanPushConstants);
    pipeline_layout.setLayoutCount = 1;
    pipeline_layout.pSetLayouts = &s_vk.descriptor_layout;
    pipeline_layout.pushConstantRangeCount = 1;
    pipeline_layout.pPushConstantRanges = &push;
    vk_result = s_vk.fn.CreatePipelineLayout(
        s_vk.device, &pipeline_layout, NULL, &s_vk.pipeline_layout);
    if (vk_result != VK_SUCCESS)
        return startup_result(BRUTEFORCE_RENDERER_STARTUP_INITIALIZATION_FAILED,
                              "probe-pipeline", vk_result,
                              "cannot create the base pipeline layout");
    result = create_shader(bruteforce_vk_base_vs,
                           BRUTEFORCE_VK_BASE_VS_SIZE, &s_vk.base_vs);
    if (result.status != BRUTEFORCE_RENDERER_STARTUP_SUCCESS) return result;
    result = create_shader(bruteforce_vk_base_ps,
                           BRUTEFORCE_VK_BASE_PS_SIZE, &s_vk.base_ps);
    if (result.status != BRUTEFORCE_RENDERER_STARTUP_SUCCESS) return result;
    result = create_shader(bruteforce_vk_clear_vs,
                           BRUTEFORCE_VK_CLEAR_VS_SIZE, &s_vk.clear_vs);
    if (result.status != BRUTEFORCE_RENDERER_STARTUP_SUCCESS) return result;
    result = create_shader(bruteforce_vk_clear_ps,
                           BRUTEFORCE_VK_CLEAR_PS_SIZE, &s_vk.clear_ps);
    if (result.status != BRUTEFORCE_RENDERER_STARTUP_SUCCESS) return result;
    result = create_shader(bruteforce_vk_extended_vs,
                           BRUTEFORCE_VK_EXTENDED_VS_SIZE, &s_vk.extended_vs);
    if (result.status != BRUTEFORCE_RENDERER_STARTUP_SUCCESS) return result;
    result = create_shader(bruteforce_vk_extended_ps,
                           BRUTEFORCE_VK_EXTENDED_PS_SIZE, &s_vk.extended_ps);
    if (result.status != BRUTEFORCE_RENDERER_STARTUP_SUCCESS) return result;
    result = create_shader(bruteforce_vk_vsh_vs,
                           BRUTEFORCE_VK_VSH_VS_SIZE, &s_vk.vsh_vs);
    if (result.status != BRUTEFORCE_RENDERER_STARTUP_SUCCESS) return result;
    result = create_shader(bruteforce_vk_vsh_gs,
                           BRUTEFORCE_VK_VSH_GS_SIZE, &s_vk.vsh_gs);
    if (result.status != BRUTEFORCE_RENDERER_STARTUP_SUCCESS) return result;
    result = create_shader(bruteforce_vk_vsh_verify_cs,
                           BRUTEFORCE_VK_VSH_VERIFY_CS_SIZE,
                           &s_vk.vsh_verify_cs);
    if (result.status != BRUTEFORCE_RENDERER_STARTUP_SUCCESS) return result;
    result = create_shader(bruteforce_vk_resolve_vs,
                           BRUTEFORCE_VK_RESOLVE_VS_SIZE, &s_vk.resolve_vs);
    if (result.status != BRUTEFORCE_RENDERER_STARTUP_SUCCESS) return result;
    result = create_shader(bruteforce_vk_resolve_bilinear_ps,
                           BRUTEFORCE_VK_RESOLVE_BILINEAR_PS_SIZE,
                           &s_vk.resolve_bilinear_ps);
    if (result.status != BRUTEFORCE_RENDERER_STARTUP_SUCCESS) return result;
    result = create_shader(bruteforce_vk_resolve_box_ps,
                           BRUTEFORCE_VK_RESOLVE_BOX_PS_SIZE,
                           &s_vk.resolve_box_ps);
    if (result.status != BRUTEFORCE_RENDERER_STARTUP_SUCCESS) return result;
    compute.stage.sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    compute.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    compute.stage.module = s_vk.vsh_verify_cs;
    compute.stage.pName = "vsh_verify_cs";
    compute.layout = s_vk.pipeline_layout;
    vk_result = s_vk.fn.CreateComputePipelines(
        s_vk.device, VK_NULL_HANDLE, 1, &compute, NULL,
        &s_vk.vsh_verify_pipeline);
    if (vk_result != VK_SUCCESS)
        return startup_result(
            BRUTEFORCE_RENDERER_STARTUP_INITIALIZATION_FAILED,
            "probe-pipeline", vk_result,
            "cannot create the Vulkan VSH verification pipeline");
    if (test_failure("probe-pipeline"))
        return startup_result(BRUTEFORCE_RENDERER_STARTUP_INITIALIZATION_FAILED,
                              "probe-pipeline", VK_ERROR_INITIALIZATION_FAILED,
                              "injected probe-pipeline failure");
    if (!pipeline_key(NULL, VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_UNDEFINED,
                      0, 0, &key))
        return startup_result(BRUTEFORCE_RENDERER_STARTUP_INITIALIZATION_FAILED,
                              "probe-pipeline", VK_ERROR_INITIALIZATION_FAILED,
                              "cannot map the base probe pipeline");
    if (!cache_pipeline(&key, &vk_result))
        return startup_result(BRUTEFORCE_RENDERER_STARTUP_INITIALIZATION_FAILED,
                              "probe-pipeline", vk_result,
                              "cannot create the base probe pipeline");
    return startup_result(BRUTEFORCE_RENDERER_STARTUP_SUCCESS, "probe-pipeline",
                          VK_SUCCESS, "Vulkan base pipeline is ready");
}

static VkResult upload_startup_image(VulkanImage *image,
                                     const void *pixels, size_t bytes)
{
    VulkanCommandSlot *slot = &s_vk.slots[0];
    VulkanUpload staging;
    VkCommandBufferBeginInfo begin = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    VkBufferImageCopy copy;
    VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    VkResult result;

    staging = slot_upload(slot, bytes, 4);
    memcpy(staging.mapped, pixels, bytes);
    result = s_vk.fn.WaitForFences(s_vk.device, 1, &slot->fence, VK_TRUE, UINT64_MAX);
    if (result != VK_SUCCESS)
        return result;
    result = s_vk.fn.ResetCommandBuffer(slot->command, 0);
    if (result != VK_SUCCESS)
        return result;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    result = s_vk.fn.BeginCommandBuffer(slot->command, &begin);
    if (result != VK_SUCCESS)
        return result;
    image_barrier(slot->command, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                  0, VK_ACCESS_TRANSFER_WRITE_BIT);
    memset(&copy, 0, sizeof copy);
    copy.imageSubresource.aspectMask = image->aspect;
    copy.imageSubresource.layerCount = image->layers;
    copy.imageExtent.width = image->width;
    copy.imageExtent.height = image->height;
    copy.imageExtent.depth = 1;
    copy.bufferOffset = staging.offset;
    s_vk.fn.CmdCopyBufferToImage(slot->command, staging.buffer, image->image,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    image_barrier(slot->command, image,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                  VK_ACCESS_TRANSFER_WRITE_BIT,
                  VK_ACCESS_SHADER_READ_BIT);
    result = s_vk.fn.EndCommandBuffer(slot->command);
    if (result != VK_SUCCESS)
        return result;
    result = s_vk.fn.ResetFences(s_vk.device, 1, &slot->fence);
    if (result != VK_SUCCESS)
        return result;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &slot->command;
    bruteforce_vulkan_present_queue_lock();
    result = s_vk.fn.QueueSubmit(s_vk.queue, 1, &submit, slot->fence);
    bruteforce_vulkan_present_queue_unlock();
    if (result != VK_SUCCESS)
        return result;
    s_vk.submissions++;
    slot->submitted = 1;
    result = s_vk.fn.WaitForFences(s_vk.device, 1, &slot->fence, VK_TRUE, UINT64_MAX);
    if (result == VK_SUCCESS) {
        s_vk.submission_waits++;
        slot->submitted = 0;
    }
    return result;
}

static BruteforceRendererStartupResult create_white_texture(void)
{
    VkSamplerCreateInfo sampler = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    uint32_t white[6] = {
        0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu,
        0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu,
    };
    VkResult result;

    if (!create_image(1, 1, VK_FORMAT_B8G8R8A8_UNORM,
                      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                      VK_IMAGE_ASPECT_COLOR_BIT, &s_vk.white))
        return startup_result(BRUTEFORCE_RENDERER_STARTUP_INITIALIZATION_FAILED,
                              "probe-pipeline", VK_ERROR_OUT_OF_DEVICE_MEMORY,
                              "cannot create the default white texture");
    result = upload_startup_image(&s_vk.white, white, sizeof white[0]);
    if (result != VK_SUCCESS)
        return startup_result(BRUTEFORCE_RENDERER_STARTUP_INITIALIZATION_FAILED,
                              "probe-pipeline", result,
                              "cannot upload the default white texture");
    if (!create_layered_image(
            1, 1, 6, 1, VK_FORMAT_B8G8R8A8_UNORM,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT, &s_vk.white_cube))
        return startup_result(BRUTEFORCE_RENDERER_STARTUP_INITIALIZATION_FAILED,
                              "probe-pipeline", VK_ERROR_OUT_OF_DEVICE_MEMORY,
                              "cannot create the default white cube texture");
    result = upload_startup_image(&s_vk.white_cube, white, sizeof white);
    if (result != VK_SUCCESS)
        return startup_result(BRUTEFORCE_RENDERER_STARTUP_INITIALIZATION_FAILED,
                              "probe-pipeline", result,
                              "cannot upload the default white cube texture");
    sampler.magFilter = VK_FILTER_NEAREST;
    sampler.minFilter = VK_FILTER_NEAREST;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    result = s_vk.fn.CreateSampler(s_vk.device, &sampler, NULL, &s_vk.white_sampler);
    if (result != VK_SUCCESS)
        return startup_result(BRUTEFORCE_RENDERER_STARTUP_INITIALIZATION_FAILED,
                              "probe-pipeline", result,
                              "cannot create the default texture sampler");
    sampler.magFilter = VK_FILTER_LINEAR;
    sampler.minFilter = VK_FILTER_LINEAR;
    result = s_vk.fn.CreateSampler(s_vk.device, &sampler, NULL,
                                   &s_vk.resolve_sampler);
    if (result != VK_SUCCESS)
        return startup_result(BRUTEFORCE_RENDERER_STARTUP_INITIALIZATION_FAILED,
                              "probe-pipeline", result,
                              "cannot create the render-scale resolve sampler");
    return startup_result(BRUTEFORCE_RENDERER_STARTUP_SUCCESS, "probe-pipeline",
                          VK_SUCCESS, "default texture resources are ready");
}

BruteforceRendererStartupResult bruteforce_vulkan_install(void *context)
{
    const char *validation_layer = "VK_LAYER_KHRONOS_validation";
    const char *debug_extension = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    const char *zpass = getenv("RECOMP_GPU_ZPASS");
    const char *vsh_verify = getenv("RECOMP_GPU_VSH_VERIFY");
    const char *layers[1];
    const char *extensions[3];
    const char *device_extensions[1];
    VkApplicationInfo application = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    VkDebugUtilsMessengerCreateInfoEXT debug = {
        VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT
    };
    VkInstanceCreateInfo instance = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    VkPhysicalDevice *devices = NULL;
    VkPhysicalDeviceVulkan13Features features13 = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES
    };
    VkDeviceQueueCreateInfo queue = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    VkDeviceCreateInfo device = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    VkPhysicalDeviceFeatures enabled_features = {0};
    BruteforceRendererStartupResult result;
    uint32_t loader_version = VK_API_VERSION_1_0;
    uint32_t requested_device;
    uint32_t device_count = 0;
    uint32_t chosen = UINT32_MAX;
    uint32_t chosen_family = UINT32_MAX;
    uint32_t extension_count = 0;
    int requested;
    int best_score = -1;
    float priority = 1.0f;
    VkResult vk_result;
    GpuScaleConfig scale;
    uint32_t i;
    (void)context;

    if (s_vk.installed)
        return startup_result(BRUTEFORCE_RENDERER_STARTUP_SUCCESS, "complete",
                              VK_SUCCESS, "Vulkan backend is already initialized");
    memset(&s_vk, 0, sizeof s_vk);
    if (!parse_bool_option("RECOMP_VULKAN_VALIDATION", 0, &s_vk.validation, &result) ||
        !parse_bool_option("RECOMP_GPU_MIPS", 0, &s_vk.texture_mips, &result) ||
        !parse_bool_option("RECOMP_VULKAN_PRESENT", 0, &s_vk.native_present, &result) ||
        !parse_device_option(&requested_device, &requested, &result))
        return result;
    if (!gpu_scale_prepare_config(&scale))
        return startup_result(BRUTEFORCE_RENDERER_STARTUP_INVALID_CONFIGURATION,
                              "configuration", VK_SUCCESS,
                              "RECOMP_GPU_SCALE or RECOMP_GPU_OUTPUT is invalid");
    if (scale.native)
        return startup_result(BRUTEFORCE_RENDERER_STARTUP_INVALID_CONFIGURATION,
                              "configuration", VK_SUCCESS,
                              "RECOMP_GPU_SCALE=native is not implemented for"
                              " Vulkan; use a fixed numeric scale");
    s_vk.zpass_async = zpass && strcmp(zpass, "async") == 0;
    s_vk.vsh_verify.enabled = vsh_verify && atoi(vsh_verify) > 0;
    if (test_failure("loader"))
        return startup_result(BRUTEFORCE_RENDERER_STARTUP_UNAVAILABLE, "loader",
                              VK_SUCCESS, "injected loader failure");
    s_vk.loader = LoadLibraryExW(L"vulkan-1.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!s_vk.loader)
        return bruteforce_renderer_startup_result(
            BRUTEFORCE_RENDERER_STARTUP_UNAVAILABLE, "loader",
            "cannot load vulkan-1.dll (Windows error %lu)",
            (unsigned long)GetLastError());
    result = load_global_functions();
    if (result.status != BRUTEFORCE_RENDERER_STARTUP_SUCCESS)
        goto failed;
    if (!s_vk.fn.EnumerateInstanceVersion ||
        s_vk.fn.EnumerateInstanceVersion(&loader_version) != VK_SUCCESS ||
        loader_version < VK_API_VERSION_1_3) {
        result = bruteforce_renderer_startup_result(
            BRUTEFORCE_RENDERER_STARTUP_UNAVAILABLE, "loader",
            "Vulkan 1.3 is required; loader reports %u.%u.%u",
            VK_API_VERSION_MAJOR(loader_version),
            VK_API_VERSION_MINOR(loader_version),
            VK_API_VERSION_PATCH(loader_version));
        goto failed;
    }
    if (s_vk.validation &&
        (!has_layer(validation_layer) || !has_instance_extension(debug_extension))) {
        result = startup_result(BRUTEFORCE_RENDERER_STARTUP_INITIALIZATION_FAILED,
                                "validation", VK_SUCCESS,
                                "RECOMP_VULKAN_VALIDATION=1 requires"
                                " VK_LAYER_KHRONOS_validation and VK_EXT_debug_utils");
        goto failed;
    }
    if (s_vk.native_present &&
        (!has_instance_extension(VK_KHR_SURFACE_EXTENSION_NAME) ||
         !has_instance_extension(VK_KHR_WIN32_SURFACE_EXTENSION_NAME))) {
        result = startup_result(
            BRUTEFORCE_RENDERER_STARTUP_INITIALIZATION_FAILED,
            "presentation-provider", VK_SUCCESS,
            "RECOMP_VULKAN_PRESENT=1 requires VK_KHR_surface"
            " and VK_KHR_win32_surface");
        goto failed;
    }
    application.pApplicationName = "Brute Force";
    application.applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
    application.pEngineName = "xboxrecomp";
    application.engineVersion = VK_MAKE_API_VERSION(0, 0, 18, 0);
    application.apiVersion = VK_API_VERSION_1_3;
    instance.pApplicationInfo = &application;
    if (s_vk.validation) {
        layers[0] = validation_layer;
        instance.enabledLayerCount = 1;
        instance.ppEnabledLayerNames = layers;
        extensions[extension_count++] = debug_extension;
        debug.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debug.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debug.pfnUserCallback = validation_message;
        instance.pNext = &debug;
    }
    if (s_vk.native_present) {
        extensions[extension_count++] = VK_KHR_SURFACE_EXTENSION_NAME;
        extensions[extension_count++] = VK_KHR_WIN32_SURFACE_EXTENSION_NAME;
    }
    instance.enabledExtensionCount = extension_count;
    instance.ppEnabledExtensionNames = extensions;
    vk_result = test_failure("instance") ? VK_ERROR_INITIALIZATION_FAILED :
                s_vk.fn.CreateInstance(&instance, NULL, &s_vk.instance);
    if (vk_result != VK_SUCCESS) {
        result = startup_result(BRUTEFORCE_RENDERER_STARTUP_INITIALIZATION_FAILED,
                                "instance", vk_result,
                                "cannot create a Vulkan 1.3 instance");
        goto failed;
    }
    result = load_instance_functions();
    if (result.status != BRUTEFORCE_RENDERER_STARTUP_SUCCESS)
        goto failed;
    if (s_vk.validation) {
        vk_result = s_vk.fn.CreateDebugUtilsMessengerEXT(
            s_vk.instance, &debug, NULL, &s_vk.debug_messenger);
        if (vk_result != VK_SUCCESS) {
            result = startup_result(BRUTEFORCE_RENDERER_STARTUP_INITIALIZATION_FAILED,
                                    "validation", vk_result,
                                    "cannot create the Vulkan validation messenger");
            goto failed;
        }
    }
    vk_result = s_vk.fn.EnumeratePhysicalDevices(s_vk.instance, &device_count, NULL);
    if (vk_result != VK_SUCCESS || !device_count) {
        result = startup_result(BRUTEFORCE_RENDERER_STARTUP_UNAVAILABLE,
                                "physical-device", vk_result,
                                "no Vulkan physical device is available");
        goto failed;
    }
    devices = calloc(device_count, sizeof *devices);
    if (!devices) {
        result = startup_result(BRUTEFORCE_RENDERER_STARTUP_INITIALIZATION_FAILED,
                                "physical-device", VK_ERROR_OUT_OF_HOST_MEMORY,
                                "cannot allocate the Vulkan device list");
        goto failed;
    }
    vk_result = s_vk.fn.EnumeratePhysicalDevices(s_vk.instance, &device_count, devices);
    if (vk_result != VK_SUCCESS) {
        result = startup_result(BRUTEFORCE_RENDERER_STARTUP_INITIALIZATION_FAILED,
                                "physical-device", vk_result,
                                "cannot enumerate Vulkan physical devices");
        goto failed;
    }
    if (requested && requested_device >= device_count) {
        result = bruteforce_renderer_startup_result(
            BRUTEFORCE_RENDERER_STARTUP_INVALID_CONFIGURATION, "physical-device",
            "RECOMP_VULKAN_DEVICE=%u is out of range for %u physical devices",
            requested_device, device_count);
        goto failed;
    }
    for (i = 0; i < device_count; ++i) {
        VkPhysicalDeviceProperties properties;
        VkFormat depth16;
        VkFormat depth_stencil;
        uint32_t family;
        char rejection[160];
        int score;
        if (requested && i != requested_device)
            continue;
        if (!device_suitable(devices[i], &family, &properties,
                             &depth16, &depth_stencil,
                             rejection, sizeof rejection)) {
            fprintf(stderr, "[GPU-Vulkan] physical device %u \"%s\" rejected: %s\n",
                    i, properties.deviceName, rejection);
            continue;
        }
        score = device_score(&properties);
        if (score > best_score) {
            chosen = i;
            chosen_family = family;
            s_vk.properties = properties;
            s_vk.depth16_format = depth16;
            s_vk.depth_stencil_format = depth_stencil;
            best_score = score;
        }
    }
    if (chosen == UINT32_MAX) {
        result = startup_result(
            requested ? BRUTEFORCE_RENDERER_STARTUP_INITIALIZATION_FAILED
                      : BRUTEFORCE_RENDERER_STARTUP_UNAVAILABLE,
            "physical-device", VK_SUCCESS,
            "no device supports Vulkan 1.3 dynamic rendering and the required"
            " geometry/color/depth features");
        goto failed;
    }
    s_vk.physical_device = devices[chosen];
    s_vk.queue_family = chosen_family;
    free(devices);
    devices = NULL;
    features13.dynamicRendering = VK_TRUE;
    features13.shaderDemoteToHelperInvocation = VK_TRUE;
    enabled_features.geometryShader = VK_TRUE;
    enabled_features.shaderClipDistance = VK_TRUE;
    enabled_features.occlusionQueryPrecise = VK_TRUE;
    queue.queueFamilyIndex = s_vk.queue_family;
    queue.queueCount = 1;
    queue.pQueuePriorities = &priority;
    device.pNext = &features13;
    device.pEnabledFeatures = &enabled_features;
    device.queueCreateInfoCount = 1;
    device.pQueueCreateInfos = &queue;
    if (s_vk.native_present) {
        device_extensions[0] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        device.enabledExtensionCount = 1;
        device.ppEnabledExtensionNames = device_extensions;
    }
    vk_result = test_failure("device") ? VK_ERROR_INITIALIZATION_FAILED :
                s_vk.fn.CreateDevice(s_vk.physical_device, &device, NULL, &s_vk.device);
    if (vk_result != VK_SUCCESS) {
        result = startup_result(BRUTEFORCE_RENDERER_STARTUP_INITIALIZATION_FAILED,
                                "device", vk_result,
                                "cannot create the Vulkan logical device");
        goto failed;
    }
    result = load_device_functions();
    if (result.status != BRUTEFORCE_RENDERER_STARTUP_SUCCESS)
        goto failed;
    s_vk.fn.GetDeviceQueue(s_vk.device, s_vk.queue_family, 0, &s_vk.queue);
    s_vk.fn.GetPhysicalDeviceMemoryProperties(s_vk.physical_device, &s_vk.memory);
    result = create_device_objects();
    if (result.status != BRUTEFORCE_RENDERER_STARTUP_SUCCESS)
        goto failed;
    result = create_white_texture();
    if (result.status != BRUTEFORCE_RENDERER_STARTUP_SUCCESS)
        goto failed;
    if (s_vk.native_present) {
        BruteforceVulkanPresentDevice present_device;
        memset(&present_device, 0, sizeof present_device);
        present_device.instance = s_vk.instance;
        present_device.physical_device = s_vk.physical_device;
        present_device.device = s_vk.device;
        present_device.queue = s_vk.queue;
        present_device.queue_family = s_vk.queue_family;
        present_device.memory = s_vk.memory;
        present_device.get_instance_proc_addr = s_vk.fn.GetInstanceProcAddr;
        present_device.get_device_proc_addr = s_vk.fn.GetDeviceProcAddr;
        result = bruteforce_vulkan_present_register(&present_device);
        if (result.status != BRUTEFORCE_RENDERER_STARTUP_SUCCESS)
            goto failed;
        s_vk.present_registered = 1;
    }
    if (!gpu_scale_configured() && !gpu_scale_commit_config(&scale)) {
        result = startup_result(BRUTEFORCE_RENDERER_STARTUP_INVALID_CONFIGURATION,
                                "configuration", VK_SUCCESS,
                                "cannot commit the render-scale configuration");
        goto failed;
    }
    nv2a_backend_ext_register(&s_backend_ext);
    nv2a_backend_register(&s_backend);
    s_vk.installed = 1;
    fprintf(stderr, "[GPU] Vulkan backend registered (RECOMP_GPU=vulkan)\n");
    fprintf(stderr, "[GPU-Vulkan] device=\"%s\" vendor=0x%04X device=0x%04X"
                    " api=%u.%u.%u driver=0x%08X queue=%u DXC=%s"
                    " validation=%s mips=%s presentation=%s scale=%.3f\n",
            s_vk.properties.deviceName, s_vk.properties.vendorID,
            s_vk.properties.deviceID,
            VK_API_VERSION_MAJOR(s_vk.properties.apiVersion),
            VK_API_VERSION_MINOR(s_vk.properties.apiVersion),
            VK_API_VERSION_PATCH(s_vk.properties.apiVersion),
            s_vk.properties.driverVersion, s_vk.queue_family,
            BRUTEFORCE_VULKAN_DXC_ID, s_vk.validation ? "on" : "off",
            s_vk.texture_mips ? "on" : "off",
            s_vk.native_present ? "native" : "cpu", gpu_scale_value());
    if (s_vk.texture_mips)
        fprintf(stderr, "[GPU-MIPS] Vulkan guest mip chains enabled;"
                        " linear textures and render-target aliases remain"
                        " level 0\n");
    if (s_vk.zpass_async)
        fprintf(stderr, "[GPU-Vulkan] ordered asynchronous visibility reports"
                        " enabled (RECOMP_GPU_ZPASS=async)\n");
    return startup_result(BRUTEFORCE_RENDERER_STARTUP_SUCCESS, "complete",
                          VK_SUCCESS, "Vulkan backend initialized and registered");

failed:
    free(devices);
    destroy_state();
    return result;
}

void bruteforce_vulkan_shutdown(void)
{
    if (!s_vk.loader)
        return;
    if (s_vk.installed) {
        nv2a_backend_register(NULL);
        nv2a_backend_ext_register(NULL);
    }
    destroy_state();
}

#ifdef BRUTEFORCE_VULKAN_TESTING
int bruteforce_vulkan_test_installed(void)
{
    return s_vk.installed;
}

long bruteforce_vulkan_test_validation_errors(void)
{
    return InterlockedCompareExchange(&s_vk.validation_errors, 0, 0);
}

void bruteforce_vulkan_test_clear(const Nv2aSurface *surface,
                                  const Nv2aRenderState *state, uint32_t flags,
                                  uint32_t color, uint32_t zstencil)
{
    vulkan_clear(surface, state, flags, color, zstencil);
}

void bruteforce_vulkan_test_draw(const Nv2aSurface *surface, const Nv2aBatch *batch)
{
    vulkan_draw(surface, batch);
}

void bruteforce_vulkan_test_xdraw(const Nv2aSurface *surface,
                                  const Nv2aXBatch *batch)
{
    vulkan_xdraw(surface, batch);
}

int bruteforce_vulkan_test_draw_vsh(const Nv2aSurface *surface,
                                    const Nv2aXVsBatch *batch)
{
    return vulkan_xdraw_vsh(surface, batch);
}

int bruteforce_vulkan_test_vsh_readback(Nv2aXVsOut *output,
                                        uint32_t vertex_count)
{
    return vulkan_vsh_readback(output, vertex_count);
}

int bruteforce_vulkan_test_readback(uint32_t color_va)
{
    return vulkan_readback(color_va);
}

int bruteforce_vulkan_test_flip_readback(uint32_t color_va)
{
    return vulkan_flip_readback(color_va);
}

int bruteforce_vulkan_test_texture_aliases_surface(const Nv2aTexture *texture)
{
    GpuTextureLayout layout;
    return gpu_texture_layout(texture, 1, 0, &layout) &&
           find_texture_surface_alias(texture, &layout) != NULL;
}

void bruteforce_vulkan_test_texture_cache_stats(uint64_t *hits, uint64_t *uploads,
                                                uint64_t *rewrites)
{
    if (hits)
        *hits = s_vk.texture_hits;
    if (uploads)
        *uploads = s_vk.texture_uploads;
    if (rewrites)
        *rewrites = s_vk.texture_rewrites;
}

void bruteforce_vulkan_test_submission_stats(
    uint64_t *draws, uint64_t *submissions, uint64_t *max_batch_operations)
{
    if (draws)
        *draws = s_vk.draws;
    if (submissions)
        *submissions = s_vk.submissions;
    if (max_batch_operations)
        *max_batch_operations = s_vk.max_batch_operations;
}

void bruteforce_vulkan_test_zpass_poll_stats(uint64_t *deferred)
{
    if (deferred)
        *deferred = s_vk.zpass_report_polls_deferred;
}

void bruteforce_vulkan_test_set_oldest_zpass_age(double age_ms)
{
    if (s_vk.zpass_report_head != s_vk.zpass_report_tail)
        s_vk.zpass_reports[
            s_vk.zpass_report_head %
            VULKAN_MAX_VISIBILITY_REPORTS].queued_ms = monotonic_ms() - age_ms;
}

void bruteforce_vulkan_test_zpass(int op)
{
    vulkan_zpass(op);
}

uint32_t bruteforce_vulkan_test_zpass_count(void)
{
    return vulkan_zpass_count();
}

int bruteforce_vulkan_test_zpass_report(uint32_t report_va, uint64_t stamp)
{
    return vulkan_zpass_report(report_va, stamp);
}
#endif
