#include "SDL2/SDL_error.h"
#include "SDL2/SDL_video.h"
#include "vulkan/vulkan_core.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <assert.h>
#include <complex.h>
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
#define REQUIRED_LOGIC_DEV_EXT_LEN 1
const char *required_logic_dev_ext_names[REQUIRED_LOGIC_DEV_EXT_LEN] = {
    VK_KHR_PORTABILITY_SUBSET_EXT_NAME};

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

typedef struct vk_context
{
    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkDevice logical_device;
    VkSurfaceKHR surface;
    VkQueue graphics_queue;
    VkQueue presentation_queue;
    // used as scratch space during the is_device_suitable_loop
    vk_queue_indices queue_indices;
} vk_context;

// Allocates and initializes a new vk_context on the heap
vk_context *vk_context_alloc()
{
    vk_context *ctx = (vk_context *)malloc(sizeof(vk_context));
    ctx->instance = VK_NULL_HANDLE;

    ctx->physical_device = VK_NULL_HANDLE;
    ctx->logical_device = VK_NULL_HANDLE;

    ctx->graphics_queue = VK_NULL_HANDLE;
    ctx->presentation_queue = VK_NULL_HANDLE;

    ctx->surface = VK_NULL_HANDLE;
    vk_queue_indices_init(&ctx->queue_indices);
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
    dbg_str_array(extension_names, extensions_count, "enabling extension: %s\n");

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

// For a given physical device, iterates over device properties and determines whether it supports
// everything we need
bool is_device_suitable(vk_context *context, VkPhysicalDevice device,
                        VkPhysicalDeviceProperties *props)
{
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
    uint32_t queue_families[2] = {context->queue_indices.graphics,
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

int main()
{
    SDL_Window *window =
        SDL_CreateWindow("vulkan demo", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 640, 480,
                         SDL_WINDOW_SHOWN | SDL_WINDOW_VULKAN);

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

    vk_context *ctx = vk_context_alloc();
    vk_init_instance(ctx, "vulkan demo", window);
    vk_init_surface(ctx, window);
    vk_init_physical_device(ctx);
    vk_init_logical_device(ctx);
    vk_init_queue_handles(ctx);

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
