#include "SDL2/SDL_video.h"
#include "vulkan/vulkan_core.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <assert.h>
#include <errno.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

#define DEBUG 1

#define VK_KHR_VALIDATION_LAYER_NAME "VK_LAYER_KHRONOS_validation"
#define VK_KHR_PORTABILITY_SUBSET_EXT_NAME "VK_KHR_portability_subset"

#define dbg(cformat, ...)                                                                          \
    fprintf(stderr, "%s:%d ", __FILE__, __LINE__);                                                 \
    fprintf(stderr, cformat __VA_OPT__(, ) __VA_ARGS__);

#define vk_checked(result)                                                                         \
    if (result != VK_SUCCESS)                                                                      \
    {                                                                                              \
        dbg("fatal vulkan init error: %d\n", result);                                              \
        exit(1);                                                                                   \
    }

#define sdl_checked(expr)                                                                          \
    if (!(expr))                                                                                   \
    {                                                                                              \
        dbg("fatal sdl init error: %s\n", SDL_GetError());                                         \
        exit(1);                                                                                   \
    }

#define dbg_str_array(list, len, format)                                                           \
    for (uint32_t i = 0; i < len; i++)                                                             \
    {                                                                                              \
        dbg(format, list[i]);                                                                      \
    }

// Required layers for a instance:
#define REQUIRED_INST_LAYERS_LEN 1
const char *required_inst_layers[REQUIRED_INST_LAYERS_LEN] = {VK_KHR_VALIDATION_LAYER_NAME};

// Required extensions for an instance (+ whatever SDL loads):
#define REQUIRED_INST_EXT_LEN 1
const char *required_inst_extensions[REQUIRED_INST_EXT_LEN] = {
    VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME};

// Required layers for a logical device
#define REQUIRED_LOGIC_DEV_LAYERS_LEN 1
const char *required_logic_dev_layers_names[REQUIRED_LOGIC_DEV_LAYERS_LEN] = {
    VK_KHR_VALIDATION_LAYER_NAME};

// Required extensions for a logical device
#define REQUIRED_LOGIC_DEV_EXT_LEN 2
const char *required_logic_dev_ext_names[REQUIRED_LOGIC_DEV_EXT_LEN] = {
    VK_KHR_PORTABILITY_SUBSET_EXT_NAME, VK_KHR_SWAPCHAIN_EXTENSION_NAME};

// Temporary struct used to store graphics & presentation queue indices during device init that
// we can examine to check whether the device supports the queues we need;
typedef struct vk_queue_indices
{
    int32_t graphics;
    int32_t presentation;
} vk_queue_indices;

void vk_queue_indices_init(vk_queue_indices *indices)
{
    indices->graphics = -1;
    indices->presentation = -1;
}

bool vk_queue_indices_is_suitable(vk_queue_indices *indices)
{
    return indices->graphics != -1 && indices->presentation != -1;
}

typedef struct vk_swapchain_support
{
    VkSurfaceCapabilitiesKHR *surface_capabilities;
    uint32_t surface_formats_count;
    VkSurfaceFormatKHR *surface_formats;

    uint32_t present_modes_count;
    VkPresentModeKHR *present_modes;
} vk_swapchain_support;

typedef struct vk_context
{
    SDL_Window *window;
    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkDevice logical_device;
    VkSurfaceKHR surface;
    VkQueue graphics_queue;
    VkQueue presentation_queue;
    // used as scratch space during the is_device_suitable_loop
    vk_queue_indices queue_indices;

    // swap chain support details:
    vk_swapchain_support *swapchain_support;
    VkSwapchainKHR swapchain;
    VkFormat swapchain_image_format;
    VkExtent2D swapchain_extent;

    uint32_t swapchain_image_count;
    VkImage *swapchain_images;
    VkImageView *image_views;
} vk_context;

// Allocates and initializes a new vk_context on the heap
vk_context *vk_context_alloc(SDL_Window *window)
{
    vk_context *ctx = (vk_context *)malloc(sizeof(vk_context));
    ctx->window = window;
    ctx->instance = VK_NULL_HANDLE;
    ctx->swapchain_support = NULL;

    ctx->physical_device = VK_NULL_HANDLE;
    ctx->logical_device = VK_NULL_HANDLE;

    ctx->graphics_queue = VK_NULL_HANDLE;
    ctx->presentation_queue = VK_NULL_HANDLE;

    ctx->surface = VK_NULL_HANDLE;
    vk_queue_indices_init(&ctx->queue_indices);

    ctx->swapchain_image_count = 0;
    return ctx;
}

// Initializes the `VkInstance` on the provided `vk_context`.
void vk_init_instance(vk_context *context, const char *app_name, SDL_Window *window)
{
    assert(context && "must call vk_context_alloc before vk_init_instance");
    // Get SDL extensions:
    uint32_t sdl_extension_count;
    sdl_checked(SDL_Vulkan_GetInstanceExtensions(window, &sdl_extension_count, NULL));

    const char *sdl_extension_names[sdl_extension_count];
    sdl_checked(
        SDL_Vulkan_GetInstanceExtensions(window, &sdl_extension_count, sdl_extension_names));

    VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = app_name,
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = NULL,
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_3,
    };

    // extend extension_names with our required extensions:
    uint32_t extensions_count = sdl_extension_count + REQUIRED_INST_EXT_LEN;
    const char *extension_names[extensions_count];
    // copy the string pointers from sdl_extension_names into the complete array:
    memcpy(extension_names, sdl_extension_names, sdl_extension_count * sizeof(char *));
    // set the elements past sdl_extension_count to our remaining required extensions:
    for (uint32_t i = sdl_extension_count; i < extensions_count; i++)
    {
        extension_names[i] = required_inst_extensions[i - sdl_extension_count];
    }
    // print out the full list of extensions:
    dbg_str_array(extension_names, extensions_count, "enabling instance extension: %s\n");

    // get all the available layers:
    uint32_t layer_count;
    vk_checked(vkEnumerateInstanceLayerProperties(&layer_count, NULL));
    VkLayerProperties layer_props[layer_count];
    vk_checked(vkEnumerateInstanceLayerProperties(&layer_count, layer_props));

    // iterate over them to ensure that the required layers are available:
    for (uint32_t i = 0; i < REQUIRED_INST_LAYERS_LEN; i++)
    {
        const char *required_layer = required_inst_layers[i];
        bool found = false;

        // this is one time init code, suck my dick big-o
        for (uint32_t j = 0; j < layer_count; j++)
        {
            VkLayerProperties props = layer_props[j];
            if (strcmp(required_layer, props.layerName) == 0)
            {
                found = true;
            }
        }

        if (!found)
        {
            dbg("fatal: could not find required instance layer: %s\n", required_layer);
            exit(1);
        }
    }
    dbg_str_array(required_inst_layers, REQUIRED_INST_LAYERS_LEN, "enabling layer: %s\n");

    // we now know that we have all required layers & extensions, so we can safely create the
    // instance:
    VkInstanceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app_info,
        // required for macOS, should probably configure this based on build
        .flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR,
        .enabledLayerCount = REQUIRED_INST_LAYERS_LEN,
        .ppEnabledLayerNames = required_inst_layers,
        .enabledExtensionCount = extensions_count,
        .ppEnabledExtensionNames = extension_names,
    };

    VkInstance instance;
    vk_checked(vkCreateInstance(&create_info, NULL, &instance));

    context->instance = instance;
    dbg("successfully enabled Vulkan instance\n");
}

// Creates the SDL vulkan surface:
void vk_init_surface(vk_context *context, SDL_Window *window)
{
    assert(context->instance && "context must have instance set before calling vk_init_surface");
    VkSurfaceKHR surface;
    sdl_checked(SDL_Vulkan_CreateSurface(window, context->instance, &surface));
    context->surface = surface;
    dbg("successfully created vulkan surface\n");
}

void vk_init_device_queue_indices(vk_context *context, VkPhysicalDevice device)
{
    assert(context && "context must be initalized before getting device queues");
    vk_queue_indices_init(&context->queue_indices);
    // get queue families supported by the device:
    uint32_t queue_count;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_count, NULL);
    VkQueueFamilyProperties queue_fams[queue_count];
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_count, queue_fams);

    for (uint32_t i = 0; i < queue_count; i++)
    {
        VkQueueFamilyProperties props = queue_fams[i];
        if (props.queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
            context->queue_indices.graphics = i;
        }

        VkBool32 present;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, context->surface, &present);
        if (present)
        {
            context->queue_indices.presentation = i;
        }
    }
}

// Takes physical_device as a parameter because we have to be able to query this for any physical
// device and not just the one we finally settle on.
static vk_swapchain_support *query_swap_chain_support_details(VkPhysicalDevice physical_device,
                                                              VkSurfaceKHR surface)
{
    vk_swapchain_support *support = malloc(sizeof(vk_swapchain_support));
    // query for swap chain support details:
    VkSurfaceCapabilitiesKHR *capabilities = malloc(sizeof(VkSurfaceCapabilitiesKHR));
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, capabilities);

    support->surface_capabilities = capabilities;

    uint32_t format_count;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, NULL);

    VkSurfaceFormatKHR *formats = calloc(format_count, sizeof(VkSurfaceFormatKHR));
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, formats);

    support->surface_formats_count = format_count;
    support->surface_formats = formats;

    uint32_t present_modes_count;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &present_modes_count, NULL);

    VkPresentModeKHR *present_modes = calloc(present_modes_count, sizeof(VkPresentModeKHR));
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &present_modes_count,
                                              present_modes);

    support->present_modes_count = present_modes_count;
    support->present_modes = present_modes;

    return support;
}

// For a given physical device, iterates over device properties and determines whether it supports
// everything we need
static bool is_device_suitable(vk_context *context, VkPhysicalDevice device,
                               VkPhysicalDeviceProperties *props)
{
    assert(context->surface != VK_NULL_HANDLE &&
           "context->surface must be initialized before querying for physical device suitability");
    vk_init_device_queue_indices(context, device);
    if (!vk_queue_indices_is_suitable(&context->queue_indices))
    {
        return false;
    }

    // check for swapchain support:
    uint32_t device_extension_count;
    vk_checked(vkEnumerateDeviceExtensionProperties(device, NULL, &device_extension_count, NULL));
    VkExtensionProperties device_extension_props[device_extension_count];
    vk_checked(vkEnumerateDeviceExtensionProperties(device, NULL, &device_extension_count,
                                                    device_extension_props));

    // query for swapchain support among the device extensions:
    bool found = false;
    for (uint32_t i = 0; i < device_extension_count; i++)
    {
        VkExtensionProperties props = device_extension_props[i];
        if (strcmp(VK_KHR_SWAPCHAIN_EXTENSION_NAME, props.extensionName) == 0)
        {
            found = true;
            break;
        }
    }

    if (!found)
    {
        dbg("device %s does not have swapchain support\n", props->deviceName);
        return false;
    }

    // Note: we leak this struct if we return false.  I think this is fine for now. If the user has
    // GPUs connected they also probably have enough RAM not to care about a few bytes here and
    // there :sunglasses:
    vk_swapchain_support *support = query_swap_chain_support_details(device, context->surface);

    // For our purposes, the support is adequate if there is at least one supported image format
    // and one supported presentation mode given the window surface:
    if (support->surface_formats_count == 0 || support->present_modes_count == 0)
    {
        dbg("swap chain does not have 1 format or present mode for the given surface\n");
        return false;
    }

    context->swapchain_support = support;

    // At this point context->queue indexes will be set to whatever the indexes on the suitable
    // device are.
    return true;
}

// Selects & validates a physical device:
void vk_init_physical_device(vk_context *context)
{
    assert(context->instance &&
           "context must have instance set before calling vk_init_physical_device");

    uint32_t device_count;
    vk_checked(vkEnumeratePhysicalDevices(context->instance, &device_count, NULL));
    VkPhysicalDevice devices[device_count];
    vk_checked(vkEnumeratePhysicalDevices(context->instance, &device_count, devices));

    VkPhysicalDevice the_chosen_one = VK_NULL_HANDLE;
    for (uint32_t i = 0; i < device_count; i++)
    {
        VkPhysicalDevice curr_device = devices[i];
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(curr_device, &props);
        if (is_device_suitable(context, curr_device, &props))
        {
            dbg("selecting device: %s\n", props.deviceName);
            the_chosen_one = curr_device;
            break;
        }
    }

    if (the_chosen_one == VK_NULL_HANDLE)
    {
        dbg("fatal: could not select suitable GPU device\n");
        exit(1);
    }

    context->physical_device = the_chosen_one;
    dbg("succesfully created physical device\n");
}

void vk_init_logical_device(vk_context *context)
{
    assert(context->physical_device != VK_NULL_HANDLE &&
           "context physical device must be initialized before initing logical device");

    // handle the case that graphics & presentation queue are different indices:
    uint32_t queue_family_length = 2;
    uint32_t queue_families[] = {context->queue_indices.graphics,
                                 context->queue_indices.presentation};

    if (queue_families[0] == queue_families[1])
    {
        queue_family_length = 1;
    }

    float queue_priority = 1.0;
    VkDeviceQueueCreateInfo queue_create_infos[queue_family_length];
    for (uint32_t i = 0; i < queue_family_length; i++)
    {
        uint32_t family = queue_families[i];
        queue_create_infos[i] = (VkDeviceQueueCreateInfo){
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = family,
            .queueCount = 1,
            .pQueuePriorities = &queue_priority,
        };
    }

    // If we needed specific features like geometry shaders, we would enable them here, but for now
    // just passing an empty struct:
    VkPhysicalDeviceFeatures features;
    // QUESTION: not sure if I need to do this?  the CPP example I'm following uses .{} and I don't
    // want this struct full of random stack memory
    memset(&features, 0, sizeof(VkPhysicalDeviceFeatures));
    VkDeviceCreateInfo device_create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pQueueCreateInfos = queue_create_infos,
        .queueCreateInfoCount = queue_family_length,
        .pEnabledFeatures = &features,
        .enabledLayerCount = REQUIRED_LOGIC_DEV_LAYERS_LEN,
        .ppEnabledLayerNames = required_logic_dev_layers_names,
        .enabledExtensionCount = REQUIRED_LOGIC_DEV_EXT_LEN,
        .ppEnabledExtensionNames = required_logic_dev_ext_names,
    };
    dbg_str_array(required_logic_dev_ext_names, REQUIRED_LOGIC_DEV_EXT_LEN,
                  "enabling logical device extension %s\n");

    VkDevice logical_device;
    vk_checked(
        vkCreateDevice(context->physical_device, &device_create_info, NULL, &logical_device));

    context->logical_device = logical_device;
}

void vk_init_queue_handles(vk_context *context)
{
    assert(context->logical_device != VK_NULL_HANDLE &&
           "context->logical_device must be initialized before getting queue handles");

    if (context->queue_indices.graphics == context->queue_indices.presentation)
    {
        VkQueue queue;
        // We only need to make the call once:
        vkGetDeviceQueue(context->logical_device, context->queue_indices.graphics, 0, &queue);
        context->graphics_queue = queue;
        context->presentation_queue = queue;
    }
    else
    {
        // Otherwise we need to get two separate handles:
        VkQueue graphics;
        vkGetDeviceQueue(context->logical_device, context->queue_indices.graphics, 0, &graphics);
        context->graphics_queue = graphics;

        VkQueue presentation;
        vkGetDeviceQueue(context->logical_device, context->queue_indices.presentation, 0,
                         &presentation);
        context->graphics_queue = presentation;
    }

    dbg("successfully retrieved queue handles for logical device\n");
}

static VkSurfaceFormatKHR choose_swapchain_surface_format(vk_context *context)
{
    vk_swapchain_support *support = context->swapchain_support;
    // choose swap surface format:
    bool found_format = false;
    VkSurfaceFormatKHR chosen_format;
    for (uint32_t i = 0; i < support->surface_formats_count; i++)
    {
        VkSurfaceFormatKHR iter_format = support->surface_formats[i];
        if (iter_format.format == VK_FORMAT_B8G8R8A8_SRGB &&
            iter_format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            chosen_format = iter_format;
            found_format = true;
            break;
        }
    }

    // if we made it through the loop and we didn't find one, just pick the first one:
    // remember that we are guaranteed to have at least one format
    if (!found_format)
    {
        chosen_format = support->surface_formats[0];
    }

    return chosen_format;
}

static VkPresentModeKHR choose_presentation_mode(vk_context *context)
{
    vk_swapchain_support *support = context->swapchain_support;
    bool found_format = false;
    VkPresentModeKHR chosen_mode;

    for (uint32_t i = 0; i < support->present_modes_count; i++)
    {
        VkPresentModeKHR available = support->present_modes[i];
        if (available == VK_PRESENT_MODE_MAILBOX_KHR)
        {
            dbg("chose presentation mode: VK_PRESENT_MODE_MAILBOX_KHR\n");
            found_format = true;
            chosen_mode = available;
        }
    }

    // only VK_PRESENT_MODE_FIFO_KHR is guaranteed to be available, but we will try to select
    // VK_PRESENT_MODE_MAILFIX_KHR if we can
    if (!found_format)
    {
        dbg("chose presentation mode: VK_PRESENT_MODE_FIFO_KHR\n");
        chosen_mode = VK_PRESENT_MODE_FIFO_KHR;
    }

    return chosen_mode;
}

uint32_t clamp(uint32_t value, uint32_t lb, uint32_t ub)
{
    // cmp w0, w1
    // csel w8, w0, w1, HI
    // etc
    uint32_t t = value < lb ? lb : value;
    return t > ub ? ub : value;
}

VkExtent2D choose_swap_extent(vk_context *context)
{
    VkSurfaceCapabilitiesKHR *capabilities = context->swapchain_support->surface_capabilities;
    // If the window manager allows us to not specify the current extent as the resolution of the
    // window, then it will set currentExtent to UINT32_MAX, and then it's on us to set the width
    // and height in pixels based on the minimum and maximum image extent
    if (capabilities->currentExtent.width != UINT32_MAX)
    {
        return capabilities->currentExtent;
    }

    int width, height;
    SDL_Vulkan_GetDrawableSize(context->window, &width, &height);

    VkExtent2D extent = {
        .width = (uint32_t)width,
        .height = (uint32_t)height,
    };

    extent.width =
        clamp(extent.width, capabilities->minImageExtent.width, capabilities->maxImageExtent.width);
    extent.height = clamp(extent.height, capabilities->minImageExtent.height,
                          capabilities->maxImageExtent.height);

    return extent;
}

void vk_init_swap_chain(vk_context *context)
{
    assert(context->swapchain_support &&
           "context->swapchain_support must be initialized before initing swap chain");
    VkSurfaceFormatKHR surface_format = choose_swapchain_surface_format(context);
    VkPresentModeKHR present_mode = choose_presentation_mode(context);
    VkExtent2D extent = choose_swap_extent(context);

    VkSurfaceCapabilitiesKHR *capabilities = context->swapchain_support->surface_capabilities;
    // choose how many images we want to have in the swap chain (1 more than the minimum):
    uint32_t image_count = capabilities->minImageCount + 1;

    // make sure we are not exceeding the maximum (where 0 means there is no maximum)
    if (capabilities->maxImageCount > 0 && image_count > capabilities->maxImageCount)
    {
        image_count = capabilities->maxImageCount;
    }
    dbg("creating swapchain with %d min images\n", image_count);

    VkSwapchainCreateInfoKHR create_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = context->surface,
        .minImageCount = image_count,
        .imageFormat = surface_format.format,
        .imageColorSpace = surface_format.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        // No pre-transformation of images:
        .preTransform = capabilities->currentTransform,
        // the alpha channel used for blending with other windows (ignored here):
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = present_mode,
        // we don't care about the color of pixels that are obscured:
        .clipped = VK_TRUE,
        // for now leave this null...eventually we will need to handle this to re-create the
        // swapchain
        .oldSwapchain = VK_NULL_HANDLE,
    };

    // set up queue sharing modes:
    uint32_t queue_family_indices[] = {context->queue_indices.graphics,
                                       context->queue_indices.presentation};

    if (context->queue_indices.graphics != context->queue_indices.presentation)
    {
        create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        create_info.queueFamilyIndexCount = 2;
        create_info.pQueueFamilyIndices = queue_family_indices;
    }
    else
    {
        create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        create_info.queueFamilyIndexCount = 0;
        create_info.pQueueFamilyIndices = NULL;
    }

    VkSwapchainKHR swapchain;
    vk_checked(vkCreateSwapchainKHR(context->logical_device, &create_info, NULL, &swapchain));
    context->swapchain = swapchain;

    dbg("sucessfully created swapchain \n");

    // get handles to the swap chain images:
    uint32_t actual_image_count;
    vkGetSwapchainImagesKHR(context->logical_device, context->swapchain, &actual_image_count, NULL);

    VkImage *images = calloc(actual_image_count, sizeof(VkImage));
    vkGetSwapchainImagesKHR(context->logical_device, context->swapchain, &actual_image_count,
                            images);

    context->swapchain_images = images;
    context->swapchain_image_count = actual_image_count;
    context->swapchain_image_format = surface_format.format;
    context->swapchain_extent = extent;

    dbg("retrieved swapchain image handles with count = %d\n", actual_image_count);
}

void vk_init_image_views(vk_context *context)
{
    VkImageView *image_views = calloc(context->swapchain_image_count, sizeof(VkImageView));
    for (uint32_t i = 0; i < context->swapchain_image_count; i++)
    {
        VkImageViewCreateInfo create_info = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                             .image = context->swapchain_images[i],
                                             .viewType = VK_IMAGE_VIEW_TYPE_2D,
                                             .format = context->swapchain_image_format,
                                             .components =
                                                 (VkComponentMapping){
                                                     .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                                                     .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                                                     .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                                                     .a = VK_COMPONENT_SWIZZLE_IDENTITY,
                                                 },
                                             .subresourceRange =
                                                 (VkImageSubresourceRange){
                                                     .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                                     .baseMipLevel = 0,
                                                     .levelCount = 1,
                                                     .baseArrayLayer = 0,
                                                     .layerCount = 1,
                                                 }

        };

        vk_checked(vkCreateImageView(context->logical_device, &create_info, NULL, &image_views[i]));
    }

    context->image_views = image_views;
    dbg("successfully initialized image views\n");
}

typedef struct shader_read_result
{
    char *code;
    size_t size;
    size_t cap;
} shader_read_result;

shader_read_result read_shader_code(const char *path)
{
    shader_read_result result = {
        // I'm not sure I actually need to do this...maybe I could just malloc()...but the spec
        // says that malloc only guarantees alignment for objects of <type>, so like, it could
        // return a 2-aligned address that would break my uint32_t* cast later on right?
        .code = aligned_alloc(alignof(void *), 1024),
        .size = 0,
        .cap = 1024,
    };

    if (result.code == NULL)
    {
        dbg("could not allocate %d bytes of memory with alignment %d: %s\n", 1024,
            alignof(uint32_t), strerror(errno));
        exit(1);
    }

    FILE *file = fopen(path, "rb");
    if (file == NULL)
    {
        dbg("could not read shader file at path %s: %s\n", path, strerror(errno));
        exit(1);
    }

    for (;;)
    {
        // potentially resize code buffer:
        if (result.size == result.cap)
        {
            size_t new_size = result.cap * 2;
            dbg("(%s): resizing code buffer from %lu to %lu\n", path, result.cap, new_size);
            result.code = realloc(result.code, new_size);

            if (result.code == NULL)
            {
                dbg("unable to realloc code buffer: OOM\n");
                exit(1);
            }

            result.cap = new_size;
        }

        size_t requested_bytes = result.cap - result.size;
        size_t bytes_read = fread(result.code + result.size, 1, requested_bytes, file);
        result.size = result.size + bytes_read;

        if (bytes_read < requested_bytes)
        {
            break;
        }
    }

    // If the read that broke the loop was an error, crash:
    if (ferror(file))
    {
        dbg("error reading from file stream: %s\n", strerror(errno));
        exit(1);
    }

    dbg("successfully read %lu bytes of shader bytecode from %s\n", result.size, path);
    return result;
}

VkShaderModule create_shader_module(vk_context *context, size_t code_size, char *code)
{
    VkShaderModule mod;
    VkShaderModuleCreateInfo create_info = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                            .codeSize = code_size,
                                            .pCode = (uint32_t *)code};
    vk_checked(vkCreateShaderModule(context->logical_device, &create_info, NULL, &mod));

    return mod;
}

void vk_init_graphics_pipeline(vk_context *context)
{
    // load our shaders:
    shader_read_result vert_shader = read_shader_code("shaders/shader.vert.spv");
    shader_read_result frag_shader = read_shader_code("shaders/shader.frag.spv");

    // create shader modules:
    VkShaderModule vert_mod = create_shader_module(context, vert_shader.size, vert_shader.code);
    VkShaderModule frag_mod = create_shader_module(context, frag_shader.size, frag_shader.code);

    // create shader stages:
    VkPipelineShaderStageCreateInfo vertex_shader_stage_create = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = vert_mod,
        .pName = "main",
        // Init any shader constants here:
        .pSpecializationInfo = NULL,
    };

    VkPipelineShaderStageCreateInfo frag_shader_stage_create = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = frag_mod,
        .pName = "main",
        // Init any shader constants here:
        .pSpecializationInfo = NULL,
    };

    VkPipelineShaderStageCreateInfo shader_stage_create_infos[] = {vertex_shader_stage_create,
                                                                   frag_shader_stage_create};

    // configure fixed-function operations:

    // describe the foramt of the vertex data passed to vetex shader:
    VkPipelineVertexInputStateCreateInfo vertex_input_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 0,
        .pVertexBindingDescriptions = NULL,
        .vertexAttributeDescriptionCount = 0,
        .pVertexAttributeDescriptions = NULL,
    };

    // describe the kind of geometry drawn from the vertices and if primitive restart should be
    // enabled
    VkPipelineInputAssemblyStateCreateInfo input_assembly_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE, // not required for non-strip topologies
    };

    VkViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = context->swapchain_extent.width,
        .height = context->swapchain_extent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };

    VkRect2D scissor = {
        .offset =
            (VkOffset2D){
                .x = 0,
                .y = 0,
            },
        .extent = context->swapchain_extent,
    };

    // VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    // VkPipelineDynamicStateCreateInfo dynamic_state = {
    //     .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
    //     .dynamicStateCount = 2,
    //     .pDynamicStates = dynamic_states,
    // };

    // Opting out of dynamic viewport and scissor state by specifying them directly:
    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports = &viewport,
        .scissorCount = 1,
        .pScissors = &scissor,
    };

    VkPipelineRasterizationStateCreateInfo rasterizer_create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        // VK_TRUE = fragments beyond the near and far plans are clamped to them instead of
        // discarding them (GPU feature required).
        .depthClampEnable = VK_FALSE,
        // VK_TRUE = geometry never passes through the rasterizer stage, disabling all output to
        // the framebuffer
        .rasterizerDiscardEnable = VK_FALSE,
        // determines how fragments are generated for geometry (fill, line, or point)
        .polygonMode = VK_POLYGON_MODE_FILL,
        .lineWidth = 1.0f,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        // Sometimes adjusted for shadow mapping:
        .depthBiasConstantFactor = 0.0f,
        .depthBiasClamp = 0.0f,
        .depthBiasSlopeFactor = 0.0f,
    };

    // keep multisampling disabled for now:
    VkPipelineMultisampleStateCreateInfo multi_sampling = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .sampleShadingEnable = VK_FALSE,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .minSampleShading = 1.0f,
        .pSampleMask = NULL,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable = VK_FALSE,
    };

    // TODO: configure depth and stencil buffers:

    // color blending: turn off both modes so that fragment colors are passed through to the final
    // image unmodified
    // configure color blending for our 1 framebuffer:
    VkPipelineColorBlendAttachmentState color_blend_attachment = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
        .blendEnable = false,
        .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
    };

    // set constants to be used in the operations described by the blend attachment:
    VkPipelineColorBlendStateCreateInfo color_blend_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY,
        .attachmentCount = 1,
        .pAttachments = &color_blend_attachment,
        .blendConstants = {0.0f, 0.0f, 0.0f, 0.0f},
    };

    // create the pipeline
    // specify uniform values for the pipeline via the pipeline layout:
    VkPipelineLayout pipeline_layout;
    VkPipelineLayoutCreateInfo pipeline_layout_create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 0,
        .pSetLayouts = NULL,
        .pushConstantRangeCount = 0,
        .pPushConstantRanges = NULL,
    };
    vk_checked(vkCreatePipelineLayout(context->logical_device, &pipeline_layout_create_info, NULL,
                                      &pipeline_layout));

    // Create an attachment for our color buffer:
    VkAttachmentDescription color_attachment = {
        .format = context->swapchain_image_format,
        // update for multi-sampling
        .samples = VK_SAMPLE_COUNT_1_BIT,
        // clear the data in the attachment before and after rendering
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        // keep the rendered contents in memory so we can read them later
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        // we are not doing anything with the stencil buffer:
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        // images to be presented in the swap chain:
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    };

    VkAttachmentReference color_attachment_ref = {
        .attachment = 0, // this is the index referenced by the `layout(location)` in the shaders
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };

    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_attachment_ref,
    };

    VkRenderPass render_pass;
    VkRenderPassCreateInfo render_pass_info = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
                                               .attachmentCount = 1,
                                               .pAttachments = &color_attachment,
                                               .subpassCount = 1,
                                               .pSubpasses = &subpass};

    vk_checked(vkCreateRenderPass(context->logical_device, &render_pass_info, NULL, &render_pass));

    VkGraphicsPipelineCreateInfo pipeline_create_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = shader_stage_create_infos,
        .pVertexInputState = &vertex_input_info,
        .pInputAssemblyState = &input_assembly_info,
        .pViewportState = &viewport_state,
        .pRasterizationState = &rasterizer_create_info,
        .pMultisampleState = &multi_sampling,
        .pDepthStencilState = NULL,
        .pColorBlendState = &color_blend_state,
        .pDynamicState = NULL, // is this required?  I'm trying to opt out of dynamic state here
        .layout = pipeline_layout,
        .renderPass = render_pass,
        .subpass = 0,
        // so we can use these to set up another pipeline that is derived from this one
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex = -1,
    };

    VkPipeline pipeline;
    vk_checked(vkCreateGraphicsPipelines(context->logical_device, VK_NULL_HANDLE, 1,
                                         &pipeline_create_info, NULL, &pipeline));

    // Because after the graphics pipeline has finished being created all this will have been
    // compiled to machine code, we can safely free / de-init all our shader code & modules
    // at the end of pipeline creation:
    vkDestroyShaderModule(context->logical_device, vert_mod, NULL);
    vkDestroyShaderModule(context->logical_device, frag_mod, NULL);
    free(vert_shader.code);
    free(frag_shader.code);

    dbg("successfully enabled graphics pipeline\n");
}

int main()
{
    SDL_Window *window =
        SDL_CreateWindow("vulkan demo", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 640, 480,
                         SDL_WINDOW_SHOWN | SDL_WINDOW_VULKAN | SDL_WINDOW_ALLOW_HIGHDPI);

    if (window == NULL)
    {
        dbg("could not create window: %s\n", SDL_GetError());
        exit(1);
    }

    if (0 != SDL_Vulkan_LoadLibrary(NULL))
    {
        dbg("sdl could not load required vulkan functions: %s", SDL_GetError());
        exit(1);
    }

    vk_context *ctx = vk_context_alloc(window);
    vk_init_instance(ctx, "vulkan demo", window);
    vk_init_surface(ctx, window);
    vk_init_physical_device(ctx);
    vk_init_logical_device(ctx);
    vk_init_queue_handles(ctx);
    vk_init_swap_chain(ctx);
    vk_init_image_views(ctx);
    vk_init_graphics_pipeline(ctx);

    // If we get to a black screen without crashing or the validation layer yelling at us I'm
    // calling it a success.
    dbg("succesfully initialized vulkan\n");

    bool running = true;
    SDL_Event event;
    while (running)
    {
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT)
            {
                dbg("received SDL_QUIT event\n");
                running = false;
                break;
            }
        }
    }
}
