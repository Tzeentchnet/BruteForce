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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "video/present_provider.h"
#include "vulkan_present.h"

#define VULKAN_PRESENT_SOURCE_COUNT 3

enum VulkanPresentSourceState {
    VULKAN_PRESENT_SOURCE_FREE,
    VULKAN_PRESENT_SOURCE_WRITING,
    VULKAN_PRESENT_SOURCE_READY,
    VULKAN_PRESENT_SOURCE_PRESENTING
};

typedef struct VulkanPresentFunctions {
    PFN_vkCreateWin32SurfaceKHR CreateWin32SurfaceKHR;
    PFN_vkDestroySurfaceKHR DestroySurfaceKHR;
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR GetPhysicalDeviceSurfaceSupportKHR;
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR GetPhysicalDeviceSurfaceCapabilitiesKHR;
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR GetPhysicalDeviceSurfaceFormatsKHR;
    PFN_vkGetPhysicalDeviceSurfacePresentModesKHR GetPhysicalDeviceSurfacePresentModesKHR;
    PFN_vkGetPhysicalDeviceFormatProperties GetPhysicalDeviceFormatProperties;
    PFN_vkCreateSwapchainKHR CreateSwapchainKHR;
    PFN_vkDestroySwapchainKHR DestroySwapchainKHR;
    PFN_vkGetSwapchainImagesKHR GetSwapchainImagesKHR;
    PFN_vkAcquireNextImageKHR AcquireNextImageKHR;
    PFN_vkQueuePresentKHR QueuePresentKHR;
    PFN_vkDeviceWaitIdle DeviceWaitIdle;
    PFN_vkQueueSubmit QueueSubmit;
    PFN_vkCreateCommandPool CreateCommandPool;
    PFN_vkDestroyCommandPool DestroyCommandPool;
    PFN_vkAllocateCommandBuffers AllocateCommandBuffers;
    PFN_vkResetCommandBuffer ResetCommandBuffer;
    PFN_vkBeginCommandBuffer BeginCommandBuffer;
    PFN_vkEndCommandBuffer EndCommandBuffer;
    PFN_vkCreateSemaphore CreateSemaphore;
    PFN_vkDestroySemaphore DestroySemaphore;
    PFN_vkCreateFence CreateFence;
    PFN_vkDestroyFence DestroyFence;
    PFN_vkWaitForFences WaitForFences;
    PFN_vkResetFences ResetFences;
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
    PFN_vkCmdPipelineBarrier CmdPipelineBarrier;
    PFN_vkCmdClearColorImage CmdClearColorImage;
    PFN_vkCmdBlitImage CmdBlitImage;
    PFN_vkCmdCopyImage CmdCopyImage;
    PFN_vkCmdCopyBufferToImage CmdCopyBufferToImage;
} VulkanPresentFunctions;

typedef struct VulkanPresentImage {
    VkImage image;
    VkDeviceMemory memory;
    VkImageLayout layout;
    uint32_t width;
    uint32_t height;
} VulkanPresentImage;

typedef struct VulkanPresentBuffer {
    VkBuffer buffer;
    VkDeviceMemory memory;
    void *mapped;
    VkDeviceSize size;
} VulkanPresentBuffer;

struct BruteforceVulkanPresentSource {
    VulkanPresentImage image;
    uint64_t frame_id;
    uint64_t serial;
    enum VulkanPresentSourceState state;
};

typedef struct VulkanPresentState {
    BruteforceVulkanPresentDevice device;
    VulkanPresentFunctions fn;
    HWND window;
    DWORD window_thread;
    VkSurfaceKHR surface;
    VkSwapchainKHR swapchain;
    VkFormat swap_format;
    VkColorSpaceKHR color_space;
    VkPresentModeKHR present_mode;
    VkExtent2D extent;
    VkImage *swap_images;
    VkImageLayout *swap_layouts;
    VkSemaphore *render_complete;
    uint32_t swap_count;
    VkCommandPool command_pool;
    VkCommandBuffer command;
    VkSemaphore acquired;
    VkFence fence;
    VulkanPresentBuffer cpu_upload;
    VulkanPresentImage cpu_image;
    BruteforceVulkanPresentSource sources[VULKAN_PRESENT_SOURCE_COUNT];
    uint32_t logical_width;
    uint32_t logical_height;
    uint64_t source_serial;
    uint64_t native_frames;
    uint64_t cpu_frames;
    uint64_t cpu_forced_frames;
    uint64_t cpu_missing_frames;
    uint64_t dropped_sources;
    uint64_t coalesced_sources;
    uint64_t forced_sources;
    uint64_t stale_sources;
    uint64_t last_frame_id;
    uint64_t swapchain_generations;
    int registered;
    int initialized;
    int stopping;
    int vsync;
    int point_filter;
} VulkanPresentState;

static VulkanPresentState s_present;
static SRWLOCK s_source_lock = SRWLOCK_INIT;
static CONDITION_VARIABLE s_source_available = CONDITION_VARIABLE_INIT;
static SRWLOCK s_queue_lock = SRWLOCK_INIT;

#ifdef BRUTEFORCE_VULKAN_TESTING
static const char *s_test_fail_stage;

static int present_test_failure(const char *stage)
{
    if (!s_test_fail_stage || strcmp(s_test_fail_stage, stage) != 0)
        return 0;
    s_test_fail_stage = NULL;
    return 1;
}
#else
static int present_test_failure(const char *stage)
{
    (void)stage;
    return 0;
}
#endif

static BruteforceRendererStartupResult present_result(
    BruteforceRendererStartupStatus status, const char *stage,
    const char *reason)
{
    return bruteforce_renderer_startup_result(status, stage, "%s", reason);
}

static void present_error(const char *operation, VkResult result)
{
    fprintf(stderr, "[GPU-Vulkan-WSI] %s failed (VkResult %d)\n",
            operation, (int)result);
}

static void present_abort(const char *operation, VkResult result)
{
    present_error(operation, result);
    fprintf(stderr, "[GPU-Vulkan-WSI] recovery is not implemented;"
                    " stopping without reusing presentation images\n");
    fflush(stderr);
    abort();
}

void bruteforce_vulkan_present_queue_lock(void)
{
    AcquireSRWLockExclusive(&s_queue_lock);
}

void bruteforce_vulkan_present_queue_unlock(void)
{
    ReleaseSRWLockExclusive(&s_queue_lock);
}

static int memory_type(uint32_t bits, VkMemoryPropertyFlags required,
                       uint32_t *index)
{
    uint32_t i;
    for (i = 0; i < s_present.device.memory.memoryTypeCount; ++i) {
        if ((bits & (1u << i)) &&
            (s_present.device.memory.memoryTypes[i].propertyFlags & required) ==
            required) {
            *index = i;
            return 1;
        }
    }
    return 0;
}

static void destroy_image(VulkanPresentImage *image)
{
    if (image->image)
        s_present.fn.DestroyImage(
            s_present.device.device, image->image, NULL);
    if (image->memory)
        s_present.fn.FreeMemory(
            s_present.device.device, image->memory, NULL);
    memset(image, 0, sizeof *image);
}

static int create_image(uint32_t width, uint32_t height,
                        VulkanPresentImage *image)
{
    VkImageCreateInfo info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    VkMemoryRequirements requirements;
    VkMemoryAllocateInfo allocation = {
        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO
    };
    uint32_t type;
    VkResult result;

    memset(image, 0, sizeof *image);
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = VK_FORMAT_B8G8R8A8_UNORM;
    info.extent.width = width;
    info.extent.height = height;
    info.extent.depth = 1;
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                 VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    result = s_present.fn.CreateImage(
        s_present.device.device, &info, NULL, &image->image);
    if (result != VK_SUCCESS) {
        present_error("creating a presentation source image", result);
        return 0;
    }
    s_present.fn.GetImageMemoryRequirements(
        s_present.device.device, image->image, &requirements);
    if (!memory_type(requirements.memoryTypeBits,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &type)) {
        fprintf(stderr, "[GPU-Vulkan-WSI] no device-local memory type"
                        " for a presentation source image\n");
        destroy_image(image);
        return 0;
    }
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = type;
    result = s_present.fn.AllocateMemory(
        s_present.device.device, &allocation, NULL, &image->memory);
    if (result != VK_SUCCESS) {
        present_error("allocating presentation source memory", result);
        destroy_image(image);
        return 0;
    }
    result = s_present.fn.BindImageMemory(
        s_present.device.device, image->image, image->memory, 0);
    if (result != VK_SUCCESS) {
        present_error("binding presentation source memory", result);
        destroy_image(image);
        return 0;
    }
    image->width = width;
    image->height = height;
    image->layout = VK_IMAGE_LAYOUT_UNDEFINED;
    return 1;
}

static void destroy_buffer(VulkanPresentBuffer *buffer)
{
    if (buffer->mapped)
        s_present.fn.UnmapMemory(
            s_present.device.device, buffer->memory);
    if (buffer->buffer)
        s_present.fn.DestroyBuffer(
            s_present.device.device, buffer->buffer, NULL);
    if (buffer->memory)
        s_present.fn.FreeMemory(
            s_present.device.device, buffer->memory, NULL);
    memset(buffer, 0, sizeof *buffer);
}

static int create_upload_buffer(VkDeviceSize size,
                                VulkanPresentBuffer *buffer)
{
    VkBufferCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    VkMemoryRequirements requirements;
    VkMemoryAllocateInfo allocation = {
        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO
    };
    uint32_t type;
    VkResult result;

    memset(buffer, 0, sizeof *buffer);
    info.size = size;
    info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    result = s_present.fn.CreateBuffer(
        s_present.device.device, &info, NULL, &buffer->buffer);
    if (result != VK_SUCCESS) {
        present_error("creating the CPU presentation upload buffer", result);
        return 0;
    }
    s_present.fn.GetBufferMemoryRequirements(
        s_present.device.device, buffer->buffer, &requirements);
    if (!memory_type(requirements.memoryTypeBits,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &type)) {
        fprintf(stderr, "[GPU-Vulkan-WSI] no coherent host-visible memory"
                        " for CPU presentation fallback\n");
        destroy_buffer(buffer);
        return 0;
    }
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = type;
    result = s_present.fn.AllocateMemory(
        s_present.device.device, &allocation, NULL, &buffer->memory);
    if (result != VK_SUCCESS) {
        present_error("allocating the CPU presentation upload buffer", result);
        destroy_buffer(buffer);
        return 0;
    }
    result = s_present.fn.BindBufferMemory(
        s_present.device.device, buffer->buffer, buffer->memory, 0);
    if (result != VK_SUCCESS) {
        present_error("binding the CPU presentation upload buffer", result);
        destroy_buffer(buffer);
        return 0;
    }
    result = s_present.fn.MapMemory(
        s_present.device.device, buffer->memory, 0, size, 0, &buffer->mapped);
    if (result != VK_SUCCESS) {
        present_error("mapping the CPU presentation upload buffer", result);
        destroy_buffer(buffer);
        return 0;
    }
    buffer->size = size;
    return 1;
}

static void image_barrier(VkCommandBuffer command, VkImage image,
                          VkImageLayout old_layout,
                          VkImageLayout new_layout,
                          VkPipelineStageFlags source_stage,
                          VkPipelineStageFlags destination_stage,
                          VkAccessFlags source_access,
                          VkAccessFlags destination_access)
{
    VkImageMemoryBarrier barrier = {
        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER
    };
    barrier.srcAccessMask = source_access;
    barrier.dstAccessMask = destination_access;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    s_present.fn.CmdPipelineBarrier(
        command, source_stage, destination_stage, 0,
        0, NULL, 0, NULL, 1, &barrier);
}

static int parse_options(void)
{
    const char *vsync = getenv("RECOMP_PRESENT_VSYNC");
    const char *filter = getenv("RECOMP_PRESENT_FILTER");
    if (vsync && strcmp(vsync, "0") != 0 && strcmp(vsync, "1") != 0) {
        fprintf(stderr, "[GPU-Vulkan-WSI] RECOMP_PRESENT_VSYNC must be 0 or 1\n");
        return 0;
    }
    if (filter && *filter && strcmp(filter, "linear") != 0 &&
        strcmp(filter, "point") != 0) {
        fprintf(stderr, "[GPU-Vulkan-WSI] RECOMP_PRESENT_FILTER must be"
                        " linear or point\n");
        return 0;
    }
    s_present.vsync = !vsync || strcmp(vsync, "0") != 0;
    s_present.point_filter = filter && strcmp(filter, "point") == 0;
    return 1;
}

static int load_functions(void)
{
#define LOAD_INSTANCE(member, name) do { \
    s_present.fn.member = (PFN_vk##name) \
        s_present.device.get_instance_proc_addr( \
            s_present.device.instance, "vk" #name); \
    if (!s_present.fn.member) { \
        fprintf(stderr, "[GPU-Vulkan-WSI] missing vk%s\n", #name); \
        return 0; \
    } \
} while (0)
#define LOAD_DEVICE(member, name) do { \
    s_present.fn.member = (PFN_vk##name) \
        s_present.device.get_device_proc_addr( \
            s_present.device.device, "vk" #name); \
    if (!s_present.fn.member) { \
        fprintf(stderr, "[GPU-Vulkan-WSI] missing vk%s\n", #name); \
        return 0; \
    } \
} while (0)
    LOAD_INSTANCE(CreateWin32SurfaceKHR, CreateWin32SurfaceKHR);
    LOAD_INSTANCE(DestroySurfaceKHR, DestroySurfaceKHR);
    LOAD_INSTANCE(GetPhysicalDeviceSurfaceSupportKHR,
                  GetPhysicalDeviceSurfaceSupportKHR);
    LOAD_INSTANCE(GetPhysicalDeviceSurfaceCapabilitiesKHR,
                  GetPhysicalDeviceSurfaceCapabilitiesKHR);
    LOAD_INSTANCE(GetPhysicalDeviceSurfaceFormatsKHR,
                  GetPhysicalDeviceSurfaceFormatsKHR);
    LOAD_INSTANCE(GetPhysicalDeviceSurfacePresentModesKHR,
                  GetPhysicalDeviceSurfacePresentModesKHR);
    LOAD_INSTANCE(GetPhysicalDeviceFormatProperties,
                  GetPhysicalDeviceFormatProperties);
    LOAD_DEVICE(CreateSwapchainKHR, CreateSwapchainKHR);
    LOAD_DEVICE(DestroySwapchainKHR, DestroySwapchainKHR);
    LOAD_DEVICE(GetSwapchainImagesKHR, GetSwapchainImagesKHR);
    LOAD_DEVICE(AcquireNextImageKHR, AcquireNextImageKHR);
    LOAD_DEVICE(QueuePresentKHR, QueuePresentKHR);
    LOAD_DEVICE(DeviceWaitIdle, DeviceWaitIdle);
    LOAD_DEVICE(QueueSubmit, QueueSubmit);
    LOAD_DEVICE(CreateCommandPool, CreateCommandPool);
    LOAD_DEVICE(DestroyCommandPool, DestroyCommandPool);
    LOAD_DEVICE(AllocateCommandBuffers, AllocateCommandBuffers);
    LOAD_DEVICE(ResetCommandBuffer, ResetCommandBuffer);
    LOAD_DEVICE(BeginCommandBuffer, BeginCommandBuffer);
    LOAD_DEVICE(EndCommandBuffer, EndCommandBuffer);
    LOAD_DEVICE(CreateSemaphore, CreateSemaphore);
    LOAD_DEVICE(DestroySemaphore, DestroySemaphore);
    LOAD_DEVICE(CreateFence, CreateFence);
    LOAD_DEVICE(DestroyFence, DestroyFence);
    LOAD_DEVICE(WaitForFences, WaitForFences);
    LOAD_DEVICE(ResetFences, ResetFences);
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
    LOAD_DEVICE(CmdPipelineBarrier, CmdPipelineBarrier);
    LOAD_DEVICE(CmdClearColorImage, CmdClearColorImage);
    LOAD_DEVICE(CmdBlitImage, CmdBlitImage);
    LOAD_DEVICE(CmdCopyImage, CmdCopyImage);
    LOAD_DEVICE(CmdCopyBufferToImage, CmdCopyBufferToImage);
#undef LOAD_DEVICE
#undef LOAD_INSTANCE
    return 1;
}

static void destroy_swapchain(void)
{
    uint32_t i;
    if (s_present.render_complete)
        for (i = 0; i < s_present.swap_count; ++i)
            if (s_present.render_complete[i])
                s_present.fn.DestroySemaphore(
                    s_present.device.device,
                    s_present.render_complete[i], NULL);
    free(s_present.render_complete);
    free(s_present.swap_layouts);
    free(s_present.swap_images);
    s_present.render_complete = NULL;
    s_present.swap_layouts = NULL;
    s_present.swap_images = NULL;
    s_present.swap_count = 0;
    if (s_present.swapchain)
        s_present.fn.DestroySwapchainKHR(
            s_present.device.device, s_present.swapchain, NULL);
    s_present.swapchain = VK_NULL_HANDLE;
    s_present.extent.width = 0;
    s_present.extent.height = 0;
}

static int client_extent(VkExtent2D *extent)
{
    RECT client;
    if (!s_present.window || !GetClientRect(s_present.window, &client))
        return 0;
    if (client.right <= client.left || client.bottom <= client.top)
        return 0;
    extent->width = (uint32_t)(client.right - client.left);
    extent->height = (uint32_t)(client.bottom - client.top);
    return 1;
}

static uint32_t clamp_u32(uint32_t value, uint32_t low, uint32_t high)
{
    if (value < low)
        return low;
    if (value > high)
        return high;
    return value;
}

static int choose_surface_format(VkSurfaceFormatKHR *selected)
{
    VkSurfaceFormatKHR *formats = NULL;
    uint32_t count = 0;
    uint32_t i;
    VkResult result = s_present.fn.GetPhysicalDeviceSurfaceFormatsKHR(
        s_present.device.physical_device, s_present.surface, &count, NULL);
    if (result != VK_SUCCESS || !count) {
        present_error("querying surface formats", result);
        return 0;
    }
    formats = calloc(count, sizeof *formats);
    if (!formats)
        return 0;
    result = s_present.fn.GetPhysicalDeviceSurfaceFormatsKHR(
        s_present.device.physical_device, s_present.surface, &count, formats);
    if (result != VK_SUCCESS) {
        present_error("reading surface formats", result);
        free(formats);
        return 0;
    }
    *selected = formats[0];
    if (count == 1 && selected->format == VK_FORMAT_UNDEFINED)
        selected->format = VK_FORMAT_B8G8R8A8_UNORM;
    for (i = 0; i < count; ++i)
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM &&
            formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            *selected = formats[i];
            break;
        }
    free(formats);
    return 1;
}

static int choose_present_mode(VkPresentModeKHR *selected)
{
    VkPresentModeKHR *modes = NULL;
    uint32_t count = 0;
    uint32_t i;
    VkResult result = s_present.fn.GetPhysicalDeviceSurfacePresentModesKHR(
        s_present.device.physical_device, s_present.surface, &count, NULL);
    if (result != VK_SUCCESS || !count) {
        present_error("querying surface present modes", result);
        return 0;
    }
    modes = calloc(count, sizeof *modes);
    if (!modes)
        return 0;
    result = s_present.fn.GetPhysicalDeviceSurfacePresentModesKHR(
        s_present.device.physical_device, s_present.surface, &count, modes);
    if (result != VK_SUCCESS) {
        present_error("reading surface present modes", result);
        free(modes);
        return 0;
    }
    *selected = VK_PRESENT_MODE_FIFO_KHR;
    if (!s_present.vsync) {
        for (i = 0; i < count; ++i)
            if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
                *selected = modes[i];
                break;
            }
        if (*selected == VK_PRESENT_MODE_FIFO_KHR)
            for (i = 0; i < count; ++i)
                if (modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR) {
                    *selected = modes[i];
                    break;
                }
    }
    free(modes);
    return 1;
}

static int recreate_swapchain(void)
{
    VkSurfaceCapabilitiesKHR capabilities;
    VkSurfaceFormatKHR format;
    VkPresentModeKHR present_mode;
    VkSwapchainCreateInfoKHR info = {
        VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR
    };
    VkSwapchainKHR old_swapchain = s_present.swapchain;
    VkImage *images = NULL;
    VkImageLayout *layouts = NULL;
    VkSemaphore *semaphores = NULL;
    VkSemaphoreCreateInfo semaphore_info = {
        VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
    };
    VkExtent2D requested;
    VkFormatProperties format_properties;
    VkCompositeAlphaFlagBitsKHR composite = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    uint32_t image_count;
    uint32_t i;
    VkResult result;

    if (!client_extent(&requested))
        return 0;
    result = s_present.fn.GetPhysicalDeviceSurfaceCapabilitiesKHR(
        s_present.device.physical_device, s_present.surface, &capabilities);
    if (result != VK_SUCCESS) {
        present_error("querying surface capabilities", result);
        return -1;
    }
    if (!(capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
        fprintf(stderr, "[GPU-Vulkan-WSI] swap-chain images do not support"
                        " transfer destinations\n");
        return -1;
    }
    if (!choose_surface_format(&format) ||
        !choose_present_mode(&present_mode))
        return -1;
    s_present.fn.GetPhysicalDeviceFormatProperties(
        s_present.device.physical_device, format.format, &format_properties);
    if (!(format_properties.optimalTilingFeatures &
          VK_FORMAT_FEATURE_BLIT_DST_BIT)) {
        fprintf(stderr, "[GPU-Vulkan-WSI] swap-chain format %d cannot be"
                        " a blit destination\n", (int)format.format);
        return -1;
    }
    if (capabilities.currentExtent.width != UINT32_MAX) {
        requested = capabilities.currentExtent;
    } else {
        requested.width = clamp_u32(
            requested.width, capabilities.minImageExtent.width,
            capabilities.maxImageExtent.width);
        requested.height = clamp_u32(
            requested.height, capabilities.minImageExtent.height,
            capabilities.maxImageExtent.height);
    }
    image_count = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount &&
        image_count > capabilities.maxImageCount)
        image_count = capabilities.maxImageCount;
    if (!(capabilities.supportedCompositeAlpha & composite)) {
        static const VkCompositeAlphaFlagBitsKHR choices[] = {
            VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR
        };
        for (i = 0; i < sizeof choices / sizeof choices[0]; ++i)
            if (capabilities.supportedCompositeAlpha & choices[i]) {
                composite = choices[i];
                break;
            }
    }
    info.surface = s_present.surface;
    info.minImageCount = image_count;
    info.imageFormat = format.format;
    info.imageColorSpace = format.colorSpace;
    info.imageExtent = requested;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = capabilities.currentTransform;
    info.compositeAlpha = composite;
    info.presentMode = present_mode;
    info.clipped = VK_TRUE;
    info.oldSwapchain = old_swapchain;

    bruteforce_vulkan_present_queue_lock();
    result = s_present.fn.DeviceWaitIdle(s_present.device.device);
    bruteforce_vulkan_present_queue_unlock();
    if (result != VK_SUCCESS) {
        present_error("waiting before swap-chain recreation", result);
        return -1;
    }
    result = s_present.fn.CreateSwapchainKHR(
        s_present.device.device, &info, NULL, &s_present.swapchain);
    if (result != VK_SUCCESS) {
        s_present.swapchain = old_swapchain;
        present_error("creating the Vulkan swap chain", result);
        return -1;
    }
    result = s_present.fn.GetSwapchainImagesKHR(
        s_present.device.device, s_present.swapchain, &image_count, NULL);
    if (result != VK_SUCCESS || !image_count) {
        present_error("querying swap-chain images", result);
        s_present.fn.DestroySwapchainKHR(
            s_present.device.device, s_present.swapchain, NULL);
        s_present.swapchain = old_swapchain;
        return -1;
    }
    images = calloc(image_count, sizeof *images);
    layouts = calloc(image_count, sizeof *layouts);
    semaphores = calloc(image_count, sizeof *semaphores);
    if (!images || !layouts || !semaphores)
        goto allocation_failed;
    result = s_present.fn.GetSwapchainImagesKHR(
        s_present.device.device, s_present.swapchain, &image_count, images);
    if (result != VK_SUCCESS) {
        present_error("reading swap-chain images", result);
        goto failed;
    }
    for (i = 0; i < image_count; ++i) {
        result = s_present.fn.CreateSemaphore(
            s_present.device.device, &semaphore_info, NULL, &semaphores[i]);
        if (result != VK_SUCCESS) {
            present_error("creating a presentation semaphore", result);
            goto failed;
        }
    }
    if (old_swapchain) {
        uint32_t old_count = s_present.swap_count;
        VkSemaphore *old_semaphores = s_present.render_complete;
        for (i = 0; i < old_count; ++i)
            if (old_semaphores[i])
                s_present.fn.DestroySemaphore(
                    s_present.device.device, old_semaphores[i], NULL);
        free(old_semaphores);
        free(s_present.swap_layouts);
        free(s_present.swap_images);
        s_present.fn.DestroySwapchainKHR(
            s_present.device.device, old_swapchain, NULL);
    }
    s_present.swap_images = images;
    s_present.swap_layouts = layouts;
    s_present.render_complete = semaphores;
    s_present.swap_count = image_count;
    s_present.swap_format = format.format;
    s_present.color_space = format.colorSpace;
    s_present.present_mode = present_mode;
    s_present.extent = requested;
    s_present.swapchain_generations++;
    fprintf(stderr, "[GPU-Vulkan-WSI] swap chain %ux%u images=%u mode=%d"
                    " filter=%s\n",
            requested.width, requested.height, image_count, (int)present_mode,
            s_present.point_filter ? "point" : "linear");
    return 1;

allocation_failed:
    fprintf(stderr, "[GPU-Vulkan-WSI] cannot allocate swap-chain metadata\n");
failed:
    if (semaphores)
        for (i = 0; i < image_count; ++i)
            if (semaphores[i])
                s_present.fn.DestroySemaphore(
                    s_present.device.device, semaphores[i], NULL);
    free(semaphores);
    free(layouts);
    free(images);
    s_present.fn.DestroySwapchainKHR(
        s_present.device.device, s_present.swapchain, NULL);
    s_present.swapchain = old_swapchain;
    return -1;
}

static int ensure_cpu_source(uint32_t width, uint32_t height)
{
    uint64_t bytes64 = (uint64_t)width * height * sizeof(uint32_t);
    VkDeviceSize bytes;
    if (!width || !height || bytes64 > SIZE_MAX)
        return 0;
    if (s_present.cpu_image.image &&
        s_present.cpu_image.width == width &&
        s_present.cpu_image.height == height &&
        s_present.cpu_upload.size >= bytes64)
        return 1;
    destroy_image(&s_present.cpu_image);
    destroy_buffer(&s_present.cpu_upload);
    bytes = (VkDeviceSize)bytes64;
    return create_upload_buffer(bytes, &s_present.cpu_upload) &&
           create_image(width, height, &s_present.cpu_image);
}

static void destroy_sources(void)
{
    unsigned i;
    for (i = 0; i < VULKAN_PRESENT_SOURCE_COUNT; ++i) {
        destroy_image(&s_present.sources[i].image);
        s_present.sources[i].state = VULKAN_PRESENT_SOURCE_FREE;
        s_present.sources[i].frame_id = 0;
    }
}

static int source_busy(void)
{
    unsigned i;
    for (i = 0; i < VULKAN_PRESENT_SOURCE_COUNT; ++i)
        if (s_present.sources[i].state == VULKAN_PRESENT_SOURCE_WRITING ||
            s_present.sources[i].state == VULKAN_PRESENT_SOURCE_PRESENTING)
            return 1;
    return 0;
}

BruteforceVulkanPresentSource *bruteforce_vulkan_present_begin_source(
    uint32_t width, uint32_t height, uint64_t frame_id)
{
    BruteforceVulkanPresentSource *source = NULL;
    unsigned i;
    for (;;) {
        AcquireSRWLockExclusive(&s_source_lock);
        if (!s_present.initialized || s_present.stopping) {
            ReleaseSRWLockExclusive(&s_source_lock);
            return NULL;
        }
        for (i = 0; i < VULKAN_PRESENT_SOURCE_COUNT; ++i)
            if (s_present.sources[i].state == VULKAN_PRESENT_SOURCE_FREE) {
                source = &s_present.sources[i];
                break;
            }
        if (!source) {
            for (i = 0; i < VULKAN_PRESENT_SOURCE_COUNT; ++i)
                if (s_present.sources[i].state == VULKAN_PRESENT_SOURCE_READY &&
                    (!source ||
                     s_present.sources[i].serial < source->serial))
                    source = &s_present.sources[i];
            if (source) {
                s_present.dropped_sources++;
                s_present.coalesced_sources++;
            }
        }
        if (source) {
            source->state = VULKAN_PRESENT_SOURCE_WRITING;
            source->frame_id = frame_id;
            source->serial = ++s_present.source_serial;
            ReleaseSRWLockExclusive(&s_source_lock);
            break;
        }
        SleepConditionVariableSRW(
            &s_source_available, &s_source_lock, INFINITE, 0);
        ReleaseSRWLockExclusive(&s_source_lock);
    }
    if (!source->image.image || source->image.width != width ||
        source->image.height != height) {
        destroy_image(&source->image);
        if (!create_image(width, height, &source->image))
            present_abort("allocating a bounded presentation source",
                          VK_ERROR_OUT_OF_DEVICE_MEMORY);
    }
    return source;
}

void bruteforce_vulkan_present_record_source(
    BruteforceVulkanPresentSource *source, VkCommandBuffer command,
    VkImage image)
{
    VkImageCopy copy;
    VkPipelineStageFlags source_stage;
    VkAccessFlags source_access;
    if (!source)
        return;
    source_stage = source->image.layout == VK_IMAGE_LAYOUT_UNDEFINED
                 ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
                 : VK_PIPELINE_STAGE_TRANSFER_BIT;
    source_access = source->image.layout == VK_IMAGE_LAYOUT_UNDEFINED
                  ? 0 : VK_ACCESS_TRANSFER_READ_BIT;
    image_barrier(command, source->image.image, source->image.layout,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  source_stage, VK_PIPELINE_STAGE_TRANSFER_BIT,
                  source_access, VK_ACCESS_TRANSFER_WRITE_BIT);
    memset(&copy, 0, sizeof copy);
    copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.srcSubresource.layerCount = 1;
    copy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.dstSubresource.layerCount = 1;
    copy.extent.width = source->image.width;
    copy.extent.height = source->image.height;
    copy.extent.depth = 1;
    s_present.fn.CmdCopyImage(
        command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        source->image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    image_barrier(command, source->image.image,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_ACCESS_TRANSFER_WRITE_BIT,
                  VK_ACCESS_TRANSFER_READ_BIT);
    source->image.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
}

void bruteforce_vulkan_present_finish_source(
    BruteforceVulkanPresentSource *source)
{
    if (!source)
        return;
    AcquireSRWLockExclusive(&s_source_lock);
    if (source->state != VULKAN_PRESENT_SOURCE_WRITING) {
        ReleaseSRWLockExclusive(&s_source_lock);
        present_abort("publishing an invalid presentation source",
                      VK_ERROR_UNKNOWN);
    }
    source->state = VULKAN_PRESENT_SOURCE_READY;
    WakeAllConditionVariable(&s_source_available);
    ReleaseSRWLockExclusive(&s_source_lock);
}

void bruteforce_vulkan_present_cancel_source(
    BruteforceVulkanPresentSource *source)
{
    if (!source)
        return;
    AcquireSRWLockExclusive(&s_source_lock);
    source->state = VULKAN_PRESENT_SOURCE_FREE;
    source->frame_id = 0;
    WakeAllConditionVariable(&s_source_available);
    ReleaseSRWLockExclusive(&s_source_lock);
}

static BruteforceVulkanPresentSource *take_source(
    uint64_t frame_id, int allow_source)
{
    BruteforceVulkanPresentSource *result = NULL;
    unsigned i;
    AcquireSRWLockExclusive(&s_source_lock);
    for (i = 0; i < VULKAN_PRESENT_SOURCE_COUNT; ++i) {
        BruteforceVulkanPresentSource *source = &s_present.sources[i];
        if (source->state != VULKAN_PRESENT_SOURCE_READY)
            continue;
        if (!allow_source || source->frame_id < frame_id) {
            source->state = VULKAN_PRESENT_SOURCE_FREE;
            source->frame_id = 0;
            s_present.dropped_sources++;
            if (!allow_source)
                s_present.forced_sources++;
            else
                s_present.stale_sources++;
        } else if (source->frame_id == frame_id && !result) {
            source->state = VULKAN_PRESENT_SOURCE_PRESENTING;
            result = source;
        }
    }
    WakeAllConditionVariable(&s_source_available);
    ReleaseSRWLockExclusive(&s_source_lock);
    return result;
}

static void release_source(BruteforceVulkanPresentSource *source)
{
    if (!source)
        return;
    AcquireSRWLockExclusive(&s_source_lock);
    source->state = VULKAN_PRESENT_SOURCE_FREE;
    source->frame_id = 0;
    WakeAllConditionVariable(&s_source_available);
    ReleaseSRWLockExclusive(&s_source_lock);
}

static int provider_output_size(void *context, uint32_t *width,
                                uint32_t *height)
{
    VkExtent2D client;
    uint64_t fitted_width;
    uint64_t fitted_height;
    (void)context;
    if (!width || !height || !client_extent(&client) ||
        !s_present.logical_width || !s_present.logical_height)
        return 0;
    if ((uint64_t)client.width * s_present.logical_height <=
        (uint64_t)client.height * s_present.logical_width) {
        fitted_width = client.width;
        fitted_height = (uint64_t)client.width *
                        s_present.logical_height /
                        s_present.logical_width;
    } else {
        fitted_height = client.height;
        fitted_width = (uint64_t)client.height *
                       s_present.logical_width /
                       s_present.logical_height;
    }
    if (!fitted_width || !fitted_height ||
        fitted_width > UINT32_MAX || fitted_height > UINT32_MAX)
        return 0;
    *width = (uint32_t)fitted_width;
    *height = (uint32_t)fitted_height;
    return 1;
}

static void destroy_provider_resources(void)
{
    if (s_present.device.device && s_present.fn.DeviceWaitIdle) {
        bruteforce_vulkan_present_queue_lock();
        s_present.fn.DeviceWaitIdle(s_present.device.device);
        bruteforce_vulkan_present_queue_unlock();
    }
    destroy_sources();
    destroy_image(&s_present.cpu_image);
    destroy_buffer(&s_present.cpu_upload);
    destroy_swapchain();
    if (s_present.fence)
        s_present.fn.DestroyFence(
            s_present.device.device, s_present.fence, NULL);
    if (s_present.acquired)
        s_present.fn.DestroySemaphore(
            s_present.device.device, s_present.acquired, NULL);
    if (s_present.command_pool)
        s_present.fn.DestroyCommandPool(
            s_present.device.device, s_present.command_pool, NULL);
    if (s_present.surface)
        s_present.fn.DestroySurfaceKHR(
            s_present.device.instance, s_present.surface, NULL);
    s_present.fence = VK_NULL_HANDLE;
    s_present.acquired = VK_NULL_HANDLE;
    s_present.command_pool = VK_NULL_HANDLE;
    s_present.command = VK_NULL_HANDLE;
    s_present.surface = VK_NULL_HANDLE;
    s_present.window = NULL;
    s_present.window_thread = 0;
}

static int provider_initialize(void *context, void *window,
                               uint32_t width, uint32_t height)
{
    VkWin32SurfaceCreateInfoKHR surface_info = {
        VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR
    };
    VkCommandPoolCreateInfo pool_info = {
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO
    };
    VkCommandBufferAllocateInfo command_info = {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO
    };
    VkSemaphoreCreateInfo semaphore_info = {
        VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
    };
    VkFenceCreateInfo fence_info = {
        VK_STRUCTURE_TYPE_FENCE_CREATE_INFO
    };
    VkBool32 supported = VK_FALSE;
    VkResult result;
    int swapchain_result;
    (void)context;

    if (!window || !width || !height || s_present.initialized)
        return -1;
    s_present.window = (HWND)window;
    s_present.window_thread = GetCurrentThreadId();
    s_present.logical_width = width;
    s_present.logical_height = height;
    s_present.stopping = 0;
    surface_info.hinstance = GetModuleHandleA(NULL);
    surface_info.hwnd = s_present.window;
    result = s_present.fn.CreateWin32SurfaceKHR(
        s_present.device.instance, &surface_info, NULL, &s_present.surface);
    if (result != VK_SUCCESS) {
        present_error("creating the Win32 Vulkan surface", result);
        return -1;
    }
    result = s_present.fn.GetPhysicalDeviceSurfaceSupportKHR(
        s_present.device.physical_device, s_present.device.queue_family,
        s_present.surface, &supported);
    if (result != VK_SUCCESS || !supported) {
        present_error("checking queue presentation support", result);
        return -1;
    }
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = s_present.device.queue_family;
    result = s_present.fn.CreateCommandPool(
        s_present.device.device, &pool_info, NULL, &s_present.command_pool);
    if (result != VK_SUCCESS) {
        present_error("creating the presentation command pool", result);
        return -1;
    }
    command_info.commandPool = s_present.command_pool;
    command_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_info.commandBufferCount = 1;
    result = s_present.fn.AllocateCommandBuffers(
        s_present.device.device, &command_info, &s_present.command);
    if (result != VK_SUCCESS) {
        present_error("allocating the presentation command buffer", result);
        return -1;
    }
    result = s_present.fn.CreateSemaphore(
        s_present.device.device, &semaphore_info, NULL, &s_present.acquired);
    if (result != VK_SUCCESS) {
        present_error("creating the image-acquired semaphore", result);
        return -1;
    }
    result = s_present.fn.CreateFence(
        s_present.device.device, &fence_info, NULL, &s_present.fence);
    if (result != VK_SUCCESS) {
        present_error("creating the presentation fence", result);
        return -1;
    }
    if (!ensure_cpu_source(width, height))
        return -1;
    swapchain_result = recreate_swapchain();
    if (swapchain_result < 0)
        return -1;
    AcquireSRWLockExclusive(&s_source_lock);
    s_present.initialized = 1;
    WakeAllConditionVariable(&s_source_available);
    ReleaseSRWLockExclusive(&s_source_lock);
    fprintf(stderr, "[GPU-Vulkan-WSI] host-owned Vulkan presentation active\n");
    return 1;
}

static int prepare_cpu_source(VkCommandBuffer command, const uint32_t *pixels,
                              uint32_t width, uint32_t height)
{
    VkBufferImageCopy copy;
    VkPipelineStageFlags source_stage;
    VkAccessFlags source_access;
    size_t bytes;
    if (!pixels || !ensure_cpu_source(width, height))
        return 0;
    bytes = (size_t)width * height * sizeof(uint32_t);
    memcpy(s_present.cpu_upload.mapped, pixels, bytes);
    source_stage = s_present.cpu_image.layout == VK_IMAGE_LAYOUT_UNDEFINED
                 ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
                 : VK_PIPELINE_STAGE_TRANSFER_BIT;
    source_access = s_present.cpu_image.layout == VK_IMAGE_LAYOUT_UNDEFINED
                  ? 0 : VK_ACCESS_TRANSFER_READ_BIT;
    image_barrier(command, s_present.cpu_image.image,
                  s_present.cpu_image.layout,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  source_stage, VK_PIPELINE_STAGE_TRANSFER_BIT,
                  source_access, VK_ACCESS_TRANSFER_WRITE_BIT);
    memset(&copy, 0, sizeof copy);
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent.width = width;
    copy.imageExtent.height = height;
    copy.imageExtent.depth = 1;
    s_present.fn.CmdCopyBufferToImage(
        command, s_present.cpu_upload.buffer,
        s_present.cpu_image.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    image_barrier(command, s_present.cpu_image.image,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_ACCESS_TRANSFER_WRITE_BIT,
                  VK_ACCESS_TRANSFER_READ_BIT);
    s_present.cpu_image.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    return 1;
}

static void fitted_blit(VkCommandBuffer command, VkImage source,
                        uint32_t source_width, uint32_t source_height,
                        VkImage destination)
{
    VkImageBlit blit;
    uint32_t width;
    uint32_t height;
    int32_t x;
    int32_t y;
    memset(&blit, 0, sizeof blit);
    if ((uint64_t)s_present.extent.width * source_height <=
        (uint64_t)s_present.extent.height * source_width) {
        width = s_present.extent.width;
        height = (uint32_t)((uint64_t)width * source_height / source_width);
    } else {
        height = s_present.extent.height;
        width = (uint32_t)((uint64_t)height * source_width / source_height);
    }
    x = (int32_t)(s_present.extent.width - width) / 2;
    y = (int32_t)(s_present.extent.height - height) / 2;
    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.layerCount = 1;
    blit.srcOffsets[1].x = (int32_t)source_width;
    blit.srcOffsets[1].y = (int32_t)source_height;
    blit.srcOffsets[1].z = 1;
    blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.dstSubresource.layerCount = 1;
    blit.dstOffsets[0].x = x;
    blit.dstOffsets[0].y = y;
    blit.dstOffsets[1].x = x + (int32_t)width;
    blit.dstOffsets[1].y = y + (int32_t)height;
    blit.dstOffsets[1].z = 1;
    s_present.fn.CmdBlitImage(
        command, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        destination, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &blit, s_present.point_filter ? VK_FILTER_NEAREST
                                         : VK_FILTER_LINEAR);
}

static int provider_present(void *context, const uint32_t *pixels,
                            uint32_t width, uint32_t height,
                            int allow_source, uint64_t frame_id)
{
    BruteforceVulkanPresentSource *source;
    VkExtent2D client;
    VkCommandBufferBeginInfo begin = {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
    };
    VkClearColorValue black = {{0.0f, 0.0f, 0.0f, 1.0f}};
    VkImageSubresourceRange range;
    VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    VkPresentInfoKHR present = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkImage source_image;
    uint32_t source_width;
    uint32_t source_height;
    uint32_t image_index = 0;
    VkResult acquire_result;
    VkResult submit_result;
    VkResult present_result_code;
    VkResult wait_result;
    int recreate_after = 0;
    int swapchain_result;
    int route_native;
    (void)context;

    if (!s_present.initialized ||
        GetCurrentThreadId() != s_present.window_thread)
        return -1;
    source = take_source(frame_id, allow_source);
    if (!client_extent(&client)) {
        release_source(source);
        return 0;
    }
    if (!s_present.swapchain ||
        client.width != s_present.extent.width ||
        client.height != s_present.extent.height) {
        swapchain_result = recreate_swapchain();
        if (swapchain_result <= 0) {
            release_source(source);
            return swapchain_result;
        }
    }
    acquire_result = present_test_failure("acquire-surface-lost")
        ? VK_ERROR_SURFACE_LOST_KHR
        : s_present.fn.AcquireNextImageKHR(
            s_present.device.device, s_present.swapchain, UINT64_MAX,
            s_present.acquired, VK_NULL_HANDLE, &image_index);
    if (acquire_result == VK_ERROR_OUT_OF_DATE_KHR) {
        swapchain_result = recreate_swapchain();
        if (swapchain_result <= 0) {
            release_source(source);
            return swapchain_result;
        }
        acquire_result = s_present.fn.AcquireNextImageKHR(
            s_present.device.device, s_present.swapchain, UINT64_MAX,
            s_present.acquired, VK_NULL_HANDLE, &image_index);
    }
    if (acquire_result != VK_SUCCESS &&
        acquire_result != VK_SUBOPTIMAL_KHR) {
        present_error("acquiring a swap-chain image", acquire_result);
        release_source(source);
        return -1;
    }
    recreate_after = acquire_result == VK_SUBOPTIMAL_KHR;
    if (image_index >= s_present.swap_count) {
        release_source(source);
        return -1;
    }
    if (s_present.fn.ResetCommandBuffer(s_present.command, 0) != VK_SUCCESS ||
        s_present.fn.BeginCommandBuffer(s_present.command, &begin) != VK_SUCCESS) {
        release_source(source);
        return -1;
    }
    route_native = source != NULL;
    if (route_native) {
        source_image = source->image.image;
        source_width = source->image.width;
        source_height = source->image.height;
    } else {
        if (!prepare_cpu_source(
                s_present.command, pixels, width, height)) {
            release_source(source);
            return -1;
        }
        source_image = s_present.cpu_image.image;
        source_width = width;
        source_height = height;
    }
    image_barrier(
        s_present.command, s_present.swap_images[image_index],
        s_present.swap_layouts[image_index],
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        s_present.swap_layouts[image_index] == VK_IMAGE_LAYOUT_UNDEFINED
            ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
            : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
        VK_ACCESS_TRANSFER_WRITE_BIT);
    memset(&range, 0, sizeof range);
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.levelCount = 1;
    range.layerCount = 1;
    s_present.fn.CmdClearColorImage(
        s_present.command, s_present.swap_images[image_index],
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &range);
    fitted_blit(s_present.command, source_image, source_width,
                source_height, s_present.swap_images[image_index]);
    image_barrier(
        s_present.command, s_present.swap_images[image_index],
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT, 0);
    s_present.swap_layouts[image_index] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    if (s_present.fn.EndCommandBuffer(s_present.command) != VK_SUCCESS ||
        s_present.fn.ResetFences(
            s_present.device.device, 1, &s_present.fence) != VK_SUCCESS) {
        release_source(source);
        return -1;
    }
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &s_present.acquired;
    submit.pWaitDstStageMask = &wait_stage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &s_present.command;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &s_present.render_complete[image_index];
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &s_present.render_complete[image_index];
    present.swapchainCount = 1;
    present.pSwapchains = &s_present.swapchain;
    present.pImageIndices = &image_index;

    bruteforce_vulkan_present_queue_lock();
    submit_result = s_present.fn.QueueSubmit(
        s_present.device.queue, 1, &submit, s_present.fence);
    if (submit_result == VK_SUCCESS)
        present_result_code = s_present.fn.QueuePresentKHR(
            s_present.device.queue, &present);
    else
        present_result_code = VK_SUCCESS;
    bruteforce_vulkan_present_queue_unlock();
    if (submit_result != VK_SUCCESS) {
        present_error("submitting presentation work", submit_result);
        release_source(source);
        return -1;
    }
    wait_result = s_present.fn.WaitForFences(
        s_present.device.device, 1, &s_present.fence, VK_TRUE, UINT64_MAX);
    release_source(source);
    if (wait_result != VK_SUCCESS) {
        present_error("waiting for presentation work", wait_result);
        return -1;
    }
    if (present_result_code == VK_ERROR_OUT_OF_DATE_KHR) {
        s_present.extent.width = 0;
        s_present.extent.height = 0;
        return 0;
    }
    if (present_result_code != VK_SUCCESS &&
        present_result_code != VK_SUBOPTIMAL_KHR) {
        present_error("presenting the swap-chain image", present_result_code);
        return -1;
    }
    if (present_result_code == VK_SUBOPTIMAL_KHR || recreate_after) {
        s_present.extent.width = 0;
        s_present.extent.height = 0;
    }
    if (route_native) {
        s_present.native_frames++;
    } else {
        s_present.cpu_frames++;
        if (allow_source)
            s_present.cpu_missing_frames++;
        else
            s_present.cpu_forced_frames++;
    }
    s_present.last_frame_id = frame_id;
    if (((s_present.native_frames + s_present.cpu_frames) % 300) == 0) {
        uint64_t dropped;
        uint64_t coalesced;
        uint64_t forced;
        uint64_t stale;
        AcquireSRWLockShared(&s_source_lock);
        dropped = s_present.dropped_sources;
        coalesced = s_present.coalesced_sources;
        forced = s_present.forced_sources;
        stale = s_present.stale_sources;
        ReleaseSRWLockShared(&s_source_lock);
        fprintf(stderr,
                "[GPU-Vulkan-WSI] %llu presented frames: %llu native,"
                " %llu CPU (%llu forced, %llu no exact source),"
                " %llu dropped sources (%llu coalesced, %llu forced,"
                " %llu stale), last frame %llu\n",
                (unsigned long long)(s_present.native_frames +
                                     s_present.cpu_frames),
                (unsigned long long)s_present.native_frames,
                (unsigned long long)s_present.cpu_frames,
                (unsigned long long)s_present.cpu_forced_frames,
                (unsigned long long)s_present.cpu_missing_frames,
                (unsigned long long)dropped,
                (unsigned long long)coalesced,
                (unsigned long long)forced,
                (unsigned long long)stale,
                (unsigned long long)frame_id);
    }
    return s_present.present_mode == VK_PRESENT_MODE_FIFO_KHR;
}

static void provider_shutdown(void *context)
{
    (void)context;
    AcquireSRWLockExclusive(&s_source_lock);
    s_present.stopping = 1;
    s_present.initialized = 0;
    WakeAllConditionVariable(&s_source_available);
    while (source_busy())
        SleepConditionVariableSRW(
            &s_source_available, &s_source_lock, INFINITE, 0);
    ReleaseSRWLockExclusive(&s_source_lock);
    destroy_provider_resources();
    fprintf(stderr, "[GPU-Vulkan-WSI] host-owned Vulkan presentation stopped\n");
}

BruteforceRendererStartupResult bruteforce_vulkan_present_register(
    const BruteforceVulkanPresentDevice *device)
{
    RecompPresentProvider provider;
    if (!device || !device->instance || !device->physical_device ||
        !device->device || !device->queue ||
        !device->get_instance_proc_addr || !device->get_device_proc_addr)
        return present_result(
            BRUTEFORCE_RENDERER_STARTUP_INVALID_CONFIGURATION,
            "presentation-provider", "incomplete Vulkan presentation device");
    if (s_present.registered)
        return present_result(
            BRUTEFORCE_RENDERER_STARTUP_INITIALIZATION_FAILED,
            "presentation-provider", "Vulkan presentation is already registered");
    memset(&s_present, 0, sizeof s_present);
    s_present.device = *device;
    if (!parse_options() || !load_functions())
        return present_result(
            BRUTEFORCE_RENDERER_STARTUP_INITIALIZATION_FAILED,
            "presentation-provider",
            "cannot initialize Vulkan presentation functions or options");
    memset(&provider, 0, sizeof provider);
    provider.version = RECOMP_PRESENT_PROVIDER_VERSION;
    provider.size = sizeof provider;
    provider.context = &s_present;
    provider.initialize = provider_initialize;
    provider.present = provider_present;
    provider.output_size = provider_output_size;
    provider.shutdown = provider_shutdown;
    if (!xbox_FramebufferWindowSetPresentProvider(&provider)) {
        memset(&s_present, 0, sizeof s_present);
        return present_result(
            BRUTEFORCE_RENDERER_STARTUP_INITIALIZATION_FAILED,
            "presentation-provider",
            "the framebuffer presentation provider is unavailable or active");
    }
    s_present.registered = 1;
    return present_result(
        BRUTEFORCE_RENDERER_STARTUP_SUCCESS,
        "presentation-provider", "Vulkan presentation provider registered");
}

int bruteforce_vulkan_present_unregister(void)
{
    int result;
    if (!s_present.registered)
        return 1;
    if (recomp_present_provider_active()) {
        fprintf(stderr, "[GPU-Vulkan-WSI] cannot destroy the Vulkan device"
                        " while its presentation provider is active\n");
        return 0;
    }
    result = xbox_FramebufferWindowSetPresentProvider(NULL);
    if (result)
        memset(&s_present, 0, sizeof s_present);
    return result;
}

#ifdef BRUTEFORCE_VULKAN_TESTING
int bruteforce_vulkan_present_test_parse_options(void)
{
    return parse_options();
}

void bruteforce_vulkan_present_test_fail(const char *stage)
{
    s_test_fail_stage = stage;
}

void bruteforce_vulkan_present_test_stats(
    uint64_t *native_frames, uint64_t *cpu_frames,
    uint64_t *cpu_forced_frames, uint64_t *cpu_missing_frames,
    uint64_t *dropped_sources, uint64_t *coalesced_sources,
    uint64_t *forced_sources, uint64_t *stale_sources,
    uint64_t *last_frame_id,
    uint64_t *swapchain_generations)
{
    if (native_frames)
        *native_frames = s_present.native_frames;
    if (cpu_frames)
        *cpu_frames = s_present.cpu_frames;
    if (cpu_forced_frames)
        *cpu_forced_frames = s_present.cpu_forced_frames;
    if (cpu_missing_frames)
        *cpu_missing_frames = s_present.cpu_missing_frames;
    if (dropped_sources)
        *dropped_sources = s_present.dropped_sources;
    if (coalesced_sources)
        *coalesced_sources = s_present.coalesced_sources;
    if (forced_sources)
        *forced_sources = s_present.forced_sources;
    if (stale_sources)
        *stale_sources = s_present.stale_sources;
    if (last_frame_id)
        *last_frame_id = s_present.last_frame_id;
    if (swapchain_generations)
        *swapchain_generations = s_present.swapchain_generations;
}
#endif
