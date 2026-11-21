#ifndef BRUTEFORCE_VULKAN_PRESENT_H
#define BRUTEFORCE_VULKAN_PRESENT_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <windows.h>
#include <vulkan/vulkan.h>

#include "renderer_startup.h"

typedef struct BruteforceVulkanPresentDevice {
    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkDevice device;
    VkQueue queue;
    uint32_t queue_family;
    VkPhysicalDeviceMemoryProperties memory;
    PFN_vkGetInstanceProcAddr get_instance_proc_addr;
    PFN_vkGetDeviceProcAddr get_device_proc_addr;
} BruteforceVulkanPresentDevice;

typedef struct BruteforceVulkanPresentSource BruteforceVulkanPresentSource;

BruteforceRendererStartupResult bruteforce_vulkan_present_register(
    const BruteforceVulkanPresentDevice *device);
int bruteforce_vulkan_present_unregister(void);

BruteforceVulkanPresentSource *bruteforce_vulkan_present_begin_source(
    uint32_t width, uint32_t height, uint64_t frame_id);
void bruteforce_vulkan_present_record_source(
    BruteforceVulkanPresentSource *source, VkCommandBuffer command,
    VkImage image);
void bruteforce_vulkan_present_finish_source(
    BruteforceVulkanPresentSource *source);
void bruteforce_vulkan_present_cancel_source(
    BruteforceVulkanPresentSource *source);

void bruteforce_vulkan_present_queue_lock(void);
void bruteforce_vulkan_present_queue_unlock(void);

#ifdef BRUTEFORCE_VULKAN_TESTING
int bruteforce_vulkan_present_test_parse_options(void);
void bruteforce_vulkan_present_test_fail(const char *stage);
void bruteforce_vulkan_present_test_stats(
    uint64_t *native_frames, uint64_t *cpu_frames,
    uint64_t *cpu_forced_frames, uint64_t *cpu_missing_frames,
    uint64_t *dropped_sources, uint64_t *coalesced_sources,
    uint64_t *forced_sources, uint64_t *stale_sources,
    uint64_t *last_frame_id,
    uint64_t *swapchain_generations);
#endif

#endif
