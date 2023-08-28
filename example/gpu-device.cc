#include "gpu-device.h" 

#include <vector>
#include <numeric>
#include "types.h"
#include "helper.h"
#include <algorithm>
#include <vulkan/vulkan_core.h>
#include "capped-array.h"

struct GPUDevice::Impl {
  VkInstance instance;
  VkPhysicalDevice physicalDevice;
  VkSwapchainKHR swapchain;
  VkSurfaceKHR surface;
  int32 graphicsFamily, presentFamily;
  VkQueue graphicsQueue, presentQueue;
  VkFormat swapchainFormat;
  VkExtent2D swapchainExtent;
  VkFormat depthFormat;
  VkDebugUtilsMessengerEXT messenger;
  VkCommandPool commandPool;
  VkDescriptorPool defaultDescriptorPool;
  uint32 maxPushConstantSize;
  uint32 swapchainImageCount;
  CappedArray<VkImage> swapchainImages;
  CappedArray<VkImageView> swapchainImageViews;
};

static void
assertValidationSupport(const std::vector<const char *> &layers)
{
  /* TODO */
}

static VkInstance
makeInstance(bool enableValidation, std::vector<const char *> &layers)
{
  layers.push_back("VK_LAYER_KHRONOS_validation");

  if (enableValidation)
    assertValidationSupport(layers);

  std::vector<const char *> extensions = 
  {
#ifndef NDEBUG
    "VK_EXT_debug_utils",
    "VK_EXT_debug_report",
#endif

#if __APPLE__
    "VK_KHR_portability_enumeration"
#endif
  };

  const char *ext =
#if defined(_WIN32)
    "VK_KHR_win32_surface";
#elif defined(__ANDROID__)
    "VK_KHR_android_surface";
#elif defined(__APPLE__)
    "VK_EXT_metal_surface";
#else
    "VK_KHR_xcb_surface";
#endif

  extensions.push_back(ext);
  extensions.push_back("VK_KHR_surface");

  VkApplicationInfo appInfo = {
    .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
    .pNext = nullptr,
    .pApplicationName = "NULL",
    .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
    .pEngineName = "",
    .engineVersion = VK_MAKE_VERSION(1, 0, 0),
    .apiVersion = VK_API_VERSION_1_1
  };

  VkInstanceCreateInfo instanceInfo = {
    .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    .pNext = nullptr,
#if __APPLE__
    .flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR,
#else
    .flags = 0,
#endif
    .pApplicationInfo = &appInfo,
    .enabledLayerCount = (uint32)layers.size(),
    .ppEnabledLayerNames = layers.data(),
    .enabledExtensionCount = (uint32)extensions.size(),
    .ppEnabledExtensionNames = extensions.data()
  };

  VkInstance instance;
  VK_CHECK(vkCreateInstance(&instanceInfo, nullptr, &instance));

  return instance;
}

static VKAPI_ATTR VkBool32 VKAPI_PTR 
debugMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                       VkDebugUtilsMessageTypeFlagsEXT type,
                       const VkDebugUtilsMessengerCallbackDataEXT *data,
                       void *) 
{
  if (type == VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT ||
      type == VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) 
    printf("Validation layer (%d;%d): %s\n", type, severity, data->pMessage);

  return 0;
}

static VkDebugUtilsMessengerEXT 
makeDebugMessenger(VkInstance instance) 
{
  VkDebugUtilsMessengerCreateInfoEXT messengerInfo = {};
  messengerInfo.sType = 
    VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;

  messengerInfo.pNext = NULL;
  messengerInfo.flags = 0;

  messengerInfo.messageSeverity =
    VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
    VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
    VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;

  messengerInfo.messageType =
    VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
    VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
    VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;

  messengerInfo.pfnUserCallback = &debugMessengerCallback;
  messengerInfo.pUserData = NULL;

  auto ptr_vkCreateDebugUtilsMessenger = (PFN_vkCreateDebugUtilsMessengerEXT)
    vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");

  VkDebugUtilsMessengerEXT messenger;
  VK_CHECK (ptr_vkCreateDebugUtilsMessenger(instance, &messengerInfo, NULL,
                                            &messenger));

  return messenger;
}

static VkFormat 
findDepthFormat(VkPhysicalDevice gpu, VkFormat *formats, uint32_t formatCount,
                VkImageTiling tiling, VkFormatFeatureFlags features)
{
  for (uint32_t i = 0; i < formatCount; ++i) 
  {
    VkFormatProperties properties;
    vkGetPhysicalDeviceFormatProperties(gpu, formats[i], &properties);

    if (tiling == VK_IMAGE_TILING_LINEAR && 
        (properties.linearTilingFeatures & features) == features) 
      return formats[i];
    else if (tiling == VK_IMAGE_TILING_OPTIMAL && 
             (properties.optimalTilingFeatures & features) == features) 
      return formats[i];
  }

  PANIC_AND_EXIT("Found no depth formats!");

  return VK_FORMAT_MAX_ENUM;
}

static VkDevice
makeDevice(VkInstance instance, VkSurfaceKHR surface,
           const std::vector<const char *> &layers,
           VkPhysicalDevice &physicalDevice,
           int32 &graphicsFamily, int32 &presentFamily,
           VkQueue &graphicsQueue, VkQueue &presentQueue,
           VkFormat &depthFormat)
{
  std::vector<const char *> extensions = 
  {
    VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME,
    VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME,
    VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
    VK_EXT_DEBUG_MARKER_EXTENSION_NAME,
#if defined (__APPLE__)
    "VK_KHR_portability_subset",
    "VK_EXT_shader_viewport_index_layer",
#endif
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
    VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
    VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
    VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
  };

  extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

  // Get physical devices
  std::vector<VkPhysicalDevice> devices;
  {
    uint32 deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, NULL);

    devices.resize(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
  }

  uint32 selectedPhysicalDevice = 0;
  uint32 maxPushConstantSize = 0;

  for (uint32 i = 0; i < devices.size(); ++i) 
  {
    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(devices[i], &deviceProperties);

    if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
    {
      selectedPhysicalDevice = i;

      maxPushConstantSize = deviceProperties.limits.maxPushConstantsSize;

      // Get queue families
      uint32 queueFamilyCount = 0;
      vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &queueFamilyCount, 
                                               nullptr);

      std::vector<VkQueueFamilyProperties> queueProperties;
      queueProperties.resize(queueFamilyCount);

      vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &queueFamilyCount, 
                                               queueProperties.data());

      for (uint32 f = 0; f < queueFamilyCount; ++f) 
      {
        if (queueProperties[f].queueFlags & VK_QUEUE_GRAPHICS_BIT &&
            queueProperties[f].queueCount > 0) 
          graphicsFamily = f;

#if 0
        VkBool32 presentSupport = 0;

        vkGetPhysicalDeviceSurfaceSupportKHR(devices[i], f, surface, &presentSupport);

        if (queueProperties[f].queueCount > 0 && presentSupport) 
          presentFamily = f;
#endif

        VkBool32 presentSupport = 1;

        if (presentFamily >= 0 && graphicsFamily >= 0) 
          break;
      }
    }
  }

  physicalDevice = devices[selectedPhysicalDevice];

  uint32 uniqueQueueFamilyFinder = 0;
  uniqueQueueFamilyFinder |= 1 << graphicsFamily;
  uniqueQueueFamilyFinder |= 1 << presentFamily;
  uint32 uniqueQueueFamilyCount = popCount(uniqueQueueFamilyFinder);

  std::vector<uint32> uniqueFamilyIndices;
  std::vector<VkDeviceQueueCreateInfo> uniqueFamilyInfos;

  for (uint32 bit = 0, set_bits = 0;
       bit < 32 && set_bits < uniqueQueueFamilyCount; ++bit) 
  {
    if (uniqueQueueFamilyFinder & (1 << bit)) 
    {
      uniqueFamilyIndices.push_back(bit);
      ++set_bits;
    }
  }

  float32 priority1 = 1.0f;
  uniqueFamilyInfos.resize(uniqueQueueFamilyCount);
  for (uint32 i = 0; i < uniqueQueueFamilyCount; ++i) 
  {
    VkDeviceQueueCreateInfo queueInfo = {};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.pNext = NULL;
    queueInfo.flags = 0;
    queueInfo.queueFamilyIndex = uniqueFamilyIndices[i];
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority1;

    uniqueFamilyInfos[i] = queueInfo;
  }

  VkPhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeature = 
  {
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES,
    .dynamicRendering = VK_TRUE,
  };

  VkDeviceCreateInfo deviceInfo = {
    .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
    .pNext = &dynamicRenderingFeature,
    .flags = 0,
    .queueCreateInfoCount = uniqueQueueFamilyCount,
    .pQueueCreateInfos = uniqueFamilyInfos.data(),
    .enabledLayerCount = (uint32)layers.size(),
    .ppEnabledLayerNames = layers.data(),
    .enabledExtensionCount = (uint32)extensions.size(),
    .ppEnabledExtensionNames = extensions.data(),
    // .pEnabledFeatures = &requiredFeatures.features
  };

  VkDevice dev;
  VK_CHECK(vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &dev));

  vkGetDeviceQueue(dev, graphicsFamily, 0, &graphicsQueue);
  vkGetDeviceQueue(dev, presentFamily, 0, &presentQueue);

  // Find depth format
  VkFormat formats[] =
  {
    VK_FORMAT_D32_SFLOAT,
    VK_FORMAT_D32_SFLOAT_S8_UINT,
    VK_FORMAT_D24_UNORM_S8_UINT
  };

  depthFormat = findDepthFormat(physicalDevice, formats, 3, VK_IMAGE_TILING_OPTIMAL,
                                VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);

  vkCmdBeginRenderingKHRProc = (PFN_vkCmdBeginRenderingKHR)
    (vkGetDeviceProcAddr(dev, "vkCmdBeginRenderingKHR"));
  vkCmdEndRenderingKHRProc = (PFN_vkCmdEndRenderingKHR)
    (vkGetDeviceProcAddr(dev, "vkCmdEndRenderingKHR"));

  vkGetMemoryFdKHRProc = (PFN_vkGetMemoryFdKHR)
    (vkGetInstanceProcAddr(instance, "vkGetMemoryFdKHR"));
  vkGetSemaphoreFdKHRProc = (PFN_vkGetSemaphoreFdKHR)
    (vkGetDeviceProcAddr(dev, "vkGetSemaphoreFdKHR"));
  vkGetPhysicalDeviceProperties2Proc = (PFN_vkGetPhysicalDeviceProperties2)
    (vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceProperties2"));

  VkPhysicalDeviceIDProperties vkPhysicalDeviceIDProperties = {
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES,
    .pNext = NULL
  };

  VkPhysicalDeviceProperties2 vkPhysicalDeviceProperties2 = {
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
    .pNext = &vkPhysicalDeviceIDProperties
  };

  vkGetPhysicalDeviceProperties2Proc(physicalDevice,
                                     &vkPhysicalDeviceProperties2);

  return dev;
}

#if 0
static VkSwapchainKHR 
makeSwapchain(VkDevice dev, VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
              uint32 width, uint32 height, int32 graphicsFam, int32 presentFam,
              VkExtent2D &extent, VkFormat &swapchainFormat)
{
  VkSurfaceCapabilitiesKHR surfaceCapabilities;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface,
                                            &surfaceCapabilities);

  VkSurfaceFormatKHR format = {};
  format.format = VK_FORMAT_B8G8R8A8_SRGB;
  format.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;

  VkExtent2D surfaceExtent = {};
  if (surfaceCapabilities.currentExtent.width != UINT32_MAX) 
    surfaceExtent = surfaceCapabilities.currentExtent;
  else 
  {
    surfaceExtent = { width, height };

    surfaceExtent.width = std::clamp(surfaceExtent.width,
                                     surfaceCapabilities.minImageExtent.width,
                                     surfaceCapabilities.maxImageExtent.width);

    surfaceExtent.height = std::clamp(surfaceExtent.height,
                                      surfaceCapabilities.minImageExtent.height,
                                      surfaceCapabilities.maxImageExtent.height);
  }

  // Present mode
  VkPresentModeKHR presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;

  // add 1 to the minimum images supported in the swapchain
  uint32 imageCount = surfaceCapabilities.minImageCount + 1;

  if (imageCount > surfaceCapabilities.maxImageCount && 
    surfaceCapabilities.maxImageCount)
    imageCount = surfaceCapabilities.maxImageCount;

  uint32 queueFamilyIndices[] = { 
    (uint32)graphicsFam, (uint32)presentFam};

  VkSwapchainCreateInfoKHR swapchainInfo = {
    .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
    .surface = surface,
    .minImageCount = imageCount,
    .imageFormat = format.format,
    .imageColorSpace = format.colorSpace,
    .imageExtent = surfaceExtent,
    .imageArrayLayers = 1,
    .imageUsage = 
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
    .imageSharingMode = (graphicsFam == presentFam) ?
                        VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_CONCURRENT,
    .queueFamilyIndexCount = (graphicsFam == presentFam) ? 0u : 2u,
    .pQueueFamilyIndices = (graphicsFam == presentFam) ? 
                           NULL : queueFamilyIndices,
    .preTransform = surfaceCapabilities.currentTransform,
    .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
    .presentMode = presentMode,
    .clipped = VK_TRUE,
    .oldSwapchain = VK_NULL_HANDLE
  };

  VkSwapchainKHR swapchain;
  VK_CHECK(vkCreateSwapchainKHR(dev, &swapchainInfo, NULL, &swapchain));

  extent.width = surfaceExtent.width;
  extent.height = surfaceExtent.height;
  swapchainFormat = format.format;

  return swapchain;
}

static void
makeSwapchainImages(VkDevice dev, VkSwapchainKHR swapchain, VkFormat format,
                    uint32 &imageCount,
                    CappedArray<VkImage> &images, CappedArray<VkImageView> &views)
{
  vkGetSwapchainImagesKHR(dev, swapchain, &imageCount, NULL);

  images.alloc(imageCount);
  views.alloc(imageCount);

  VK_CHECK(vkGetSwapchainImagesKHR(dev, swapchain, &imageCount, images.data()));

  for (uint32 i = 0; i < imageCount; ++i) 
  {
    VkImageSubresourceRange range = {
      .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
      .baseMipLevel = 0,
      .levelCount = 1,
      .baseArrayLayer = 0,
      .layerCount = 1
    };

    VkImageViewCreateInfo imageViewInfo = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = images[i],
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = format,
      .subresourceRange = range
    };

    VK_CHECK(vkCreateImageView(dev, &imageViewInfo, 
                               nullptr, &views[i]));
  }
}
#endif

static VkCommandPool
makeCommandPool(VkDevice dev, int32 graphicsFam)
{
  VkCommandPoolCreateInfo command_pool_info = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
    .queueFamilyIndex = (uint32)graphicsFam
  };

  VkCommandPool commandPool;
  VK_CHECK(vkCreateCommandPool(dev, &command_pool_info, nullptr, &commandPool));

  return commandPool;
}

static VkDescriptorPool
makeDefaultDescriptorPool(VkDevice dev)
{
  uint32 setCount = 100;

  VkDescriptorPoolSize sizes[] = {
    VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLER },
    VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER },
    VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE },
    VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE },
    VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER },
    VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER },
    VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER },
    VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER },
    VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC },
    VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC },
    VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT }
  };

  for (uint32 i = 0; i < sizeof(sizes)/sizeof(sizes[0]); ++i)
    sizes[i].descriptorCount = setCount;

  uint32 max_sets = setCount * sizeof(sizes)/sizeof(sizes[0]);

  VkDescriptorPoolCreateInfo descriptor_pool_info = {
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
    .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
    .maxSets = max_sets,
    .poolSizeCount = sizeof(sizes)/sizeof(sizes[0]),
    .pPoolSizes = sizes
  };

  VkDescriptorPool pool;
  VK_CHECK(vkCreateDescriptorPool(dev, &descriptor_pool_info, nullptr, &pool));

  return pool;
}

static void
imguiCallback(VkResult res)
{
  (void)res;
}

GPUDevice::~GPUDevice()
{
}

GPUDevice 
GPUDevice::make(const Surface *surface)
{
  VkDevice dev = VK_NULL_HANDLE;
  auto impl = std::make_unique<GPUDevice::Impl>();

  std::vector<const char *> layers;

  impl->instance = makeInstance(true, layers);
  impl->messenger = makeDebugMessenger(impl->instance);

  dev = makeDevice(impl->instance, impl->surface,
                   layers, impl->physicalDevice, 
                   impl->graphicsFamily, impl->presentFamily,
                   impl->graphicsQueue, impl->presentQueue,
                   impl->depthFormat);

#if 0
  impl->swapchain = makeSwapchain(dev, impl->physicalDevice, 
                                  impl->surface, surface.width, surface.height, 
                                  impl->graphicsFamily, impl->presentFamily,
                                  impl->swapchainExtent, impl->swapchainFormat);

  makeSwapchainImages(dev, impl->swapchain, impl->swapchainFormat,
                      impl->swapchainImageCount, impl->swapchainImages,
                      impl->swapchainImageViews);
#endif

  impl->commandPool = makeCommandPool(dev, impl->graphicsFamily);

  impl->defaultDescriptorPool = makeDefaultDescriptorPool(dev);

  return { dev, std::move(impl) };
}

static VkAccessFlags 
findAccessFlagsFor(VkPipelineStageFlags stage)
{
  switch (stage) 
  {
  case VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT:
    return VK_ACCESS_MEMORY_WRITE_BIT;

  case VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT:
  case VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT:
    return VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;

  case VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT:
    return VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;

  case VK_PIPELINE_STAGE_VERTEX_INPUT_BIT:
    return VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;

  case VK_PIPELINE_STAGE_VERTEX_SHADER_BIT:
  case VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT:
  case VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT:
    return VK_ACCESS_UNIFORM_READ_BIT;

  case VK_PIPELINE_STAGE_TRANSFER_BIT:
    return VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;

  case VK_PIPELINE_STAGE_ALL_COMMANDS_BIT:
    return VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;

  default: 
  {
    printf("Didn't handle stage for finding access flags %d!", stage);
    PANIC_AND_EXIT("Vulkan error");
  } return 0;
  }
}

static VkAccessFlags 
findAccessFlagsFor(VkImageLayout layout)
{
  switch (layout) 
  {
  case VK_IMAGE_LAYOUT_UNDEFINED: case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
    return 0;

  case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
    return VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;

  case VK_IMAGE_LAYOUT_GENERAL:
    return VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;

  case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
    return VK_ACCESS_TRANSFER_WRITE_BIT;

  case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
    return VK_ACCESS_SHADER_READ_BIT;

  case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
    return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

  case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
    return VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

  default: 
  {
    printf("Didn't handle image layout for finding access flags!");
    PANIC_AND_EXIT("Vulkan error");
  } return 0;
  }
}

VkImageMemoryBarrier 
GPUDevice::makeBarrier(VkImage image, VkImageAspectFlags aspect,
                       VkImageLayout oldLayout, VkImageLayout newLayout,
                       uint32 levelCount, uint32 layerCount)
{
  VkImageSubresourceRange range = {
    .aspectMask = aspect,
    .baseMipLevel = 0,
    .levelCount = 1,
    .baseArrayLayer = 0,
    .layerCount = 1
  };

  VkImageMemoryBarrier imageBarrier = {
    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
    .srcAccessMask = findAccessFlagsFor(oldLayout),
    .dstAccessMask = findAccessFlagsFor(newLayout),
    .oldLayout = oldLayout,
    .newLayout = newLayout,
    .image = image,
    .subresourceRange = range
  };

  return imageBarrier;
}

VkBufferMemoryBarrier 
GPUDevice::makeBarrier(VkBuffer buffer,
                       uint32 offset,
                       uint32 size,
                       VkPipelineStageFlags src,
                       VkPipelineStageFlags dst)
{
  VkBufferMemoryBarrier bufferBarrier = {
    .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
    .srcAccessMask = findAccessFlagsFor(src),
    .dstAccessMask = findAccessFlagsFor(dst),
    .buffer = buffer,
    .offset = offset,
    .size = size
  };

  return bufferBarrier;
}

void 
GPUDevice::waitIdle() const
{
  vkDeviceWaitIdle(dev);
}

VkCommandBuffer 
GPUDevice::makeCommandBuffer() const
{
  VkCommandBuffer commandBuffer;

  VkCommandBufferAllocateInfo allocInfo = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .commandPool = impl->commandPool,
    .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
    .commandBufferCount = 1
  };

  vkAllocateCommandBuffers(dev, &allocInfo, &commandBuffer);
  return commandBuffer;
}

void 
GPUDevice::freeCommandBuffer(VkCommandBuffer cmdbuf) const
{
  vkFreeCommandBuffers(dev, impl->commandPool, 1, &cmdbuf);
}

void 
GPUDevice::beginCommandBuffer(VkCommandBuffer cmdbuf) const
{
  VkCommandBufferBeginInfo beginInfo = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
  };

  vkBeginCommandBuffer(cmdbuf, &beginInfo);
}

void 
GPUDevice::beginSingleUseCommandBuffer(VkCommandBuffer cmdbuf) const
{
  VkCommandBufferBeginInfo beginInfo = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
  };

  vkBeginCommandBuffer(cmdbuf, &beginInfo);
}

void 
GPUDevice::endCommandBuffer(VkCommandBuffer cmdbuf) const
{
  vkEndCommandBuffer(cmdbuf);
}

void 
GPUDevice::submitCommandBuffer(VkCommandBuffer cmdbuf, 
                               VkSemaphore wait, 
                               VkSemaphore signal,
                               VkPipelineStageFlags waitStage,
                               VkFence fence) const
{
  VkSubmitInfo submitInfo = {
    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
    .waitSemaphoreCount = (wait == VK_NULL_HANDLE) ? 0u : 1u,
    .pWaitSemaphores = &wait,
    .pWaitDstStageMask = &waitStage,
    .commandBufferCount = 1,
    .pCommandBuffers = &cmdbuf,
    .signalSemaphoreCount = (signal == VK_NULL_HANDLE) ? 0u : 1u,
    .pSignalSemaphores = &signal
  };

  VK_CHECK(vkQueueSubmit(impl->graphicsQueue, 1, &submitInfo, fence));
}

void 
GPUDevice::submitCommandBuffer(VkCommandBuffer cmdbuf, 
                               uint32 waitCount,
                               VkSemaphore *wait, 
                               VkSemaphore signal,
                               VkPipelineStageFlags *waitStage,
                               VkFence signalFence) const
{
  VkSubmitInfo submitInfo = {
    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
    .waitSemaphoreCount = waitCount,
    .pWaitSemaphores = wait,
    .pWaitDstStageMask = waitStage,
    .commandBufferCount = 1,
    .pCommandBuffers = &cmdbuf,
    .signalSemaphoreCount = (signal == VK_NULL_HANDLE) ? 0u : 1u,
    .pSignalSemaphores = &signal
  };

  VK_CHECK(vkQueueSubmit(impl->graphicsQueue, 1, &submitInfo, signalFence));
}

uint32 
GPUDevice::acquireNextImage(VkSemaphore semaphore) const
{
  uint32 imageIndex;
  VK_CHECK(vkAcquireNextImageKHR(dev, impl->swapchain, UINT64_MAX, 
                                 semaphore, VK_NULL_HANDLE, &imageIndex));

  return imageIndex;
}

void 
GPUDevice::present(VkSemaphore wait, uint32 imageIndex) const
{
  VkPresentInfoKHR presentInfo = {
    .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
    .waitSemaphoreCount = 1,
    .pWaitSemaphores = &wait,
    .swapchainCount = 1,
    .pSwapchains = &impl->swapchain,
    .pImageIndices = &imageIndex,
  };

  vkQueuePresentKHR(impl->presentQueue, &presentInfo);
}

VkImage 
GPUDevice::getSwapchainImage(uint32 index) const
{
  return impl->swapchainImages[index];
}

VkImageView 
GPUDevice::getSwapchainImageView(uint32 index) const
{
  return impl->swapchainImageViews[index];
}

VkExtent2D 
GPUDevice::getSwapchainExtent() const
{
  return impl->swapchainExtent;
}

static uint32 
findMemoryType(VkPhysicalDevice physicalDevice,
               VkMemoryPropertyFlags properties, 
               VkMemoryRequirements &memoryRequirements) 
{
  VkPhysicalDeviceMemoryProperties memProperties;
  vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

  for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) 
  {
    if (memoryRequirements.memoryTypeBits & (1 << i) &&
        (memProperties.memoryTypes[i].propertyFlags & properties) == 
          properties)
      return i;
  }

  printf("Unable to find memory type!");
  PANIC_AND_EXIT("Vulkan error");

  return 0;
}

static VkDeviceMemory 
allocateBufferMemory(VkDevice dev,
                     VkPhysicalDevice physicalDevice,
                     VkBuffer buffer, 
                     VkMemoryPropertyFlags properties,
                     bool shouldExport = false) 
{
  VkMemoryRequirements requirements = {};
  vkGetBufferMemoryRequirements(dev, buffer, &requirements);

  VkExportMemoryAllocateInfoKHR exportInfo = {};

  VkMemoryAllocateInfo allocInfo = {
    .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .allocationSize = requirements.size,
    .memoryTypeIndex = findMemoryType(physicalDevice, properties, requirements)
  };

  if (shouldExport)
  {
    exportInfo.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO_KHR;
    exportInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT_KHR;
    
    allocInfo.pNext = &exportInfo;
  }

  VkDeviceMemory memory;
  vkAllocateMemory(dev, &allocInfo, nullptr, &memory);
  vkBindBufferMemory(dev, buffer, memory, 0);

  return memory;
}

static VkDeviceMemory 
allocateImageMemory(VkDevice dev,
                      VkPhysicalDevice physicalDevice,
                      VkImage image, 
                      VkMemoryPropertyFlags properties,
                      uint64 *size,
                      bool shouldExport = false) 
{
  VkMemoryRequirements requirements = {};
  vkGetImageMemoryRequirements(dev, image, &requirements);

  VkExportMemoryAllocateInfoKHR exportInfo = {};

  VkMemoryAllocateInfo allocInfo = {
    .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .allocationSize = requirements.size,
    .memoryTypeIndex = findMemoryType(physicalDevice, properties, requirements),
  };

  if (shouldExport)
  {
    exportInfo.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO_KHR;
    exportInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT_KHR;

    allocInfo.pNext = &exportInfo;
  }

  VkDeviceMemory memory;
  vkAllocateMemory(dev, &allocInfo, nullptr, &memory);

  if (size)
    *size = requirements.size;

  return memory;
}

static VkBuffer
makeBuffer(VkDevice dev, uint32 size, VkBufferUsageFlags usage, 
           bool shouldExport = false)
{
  VkExternalMemoryBufferCreateInfo externalInfo = {};

  VkBufferCreateInfo bufferInfo = {
    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    .size = size,
    .usage = usage,
    .sharingMode = VK_SHARING_MODE_EXCLUSIVE
  };

  if (shouldExport)
  {
    externalInfo.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO;
    externalInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT_KHR;

    bufferInfo.pNext = &externalInfo;
  }

  VkBuffer buffer;
  VK_CHECK(vkCreateBuffer(dev, &bufferInfo, nullptr, &buffer));

  return buffer;
}

StagingBuffer 
GPUDevice::makeStagingBuffer(uint64 size) const
{
  StagingBuffer ret = {};
  ret.hdl = makeBuffer(dev, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
  ret.mem = allocateBufferMemory(dev, impl->physicalDevice, ret.hdl, 
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | 
                                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  vkMapMemory(dev, ret.mem, 0, size, 0, &ret.ptr);
  ret.mDev = this;
  return ret;
}

DeviceBuffer
GPUDevice::makeDeviceBuffer(uint64 size, bool shouldExport) const
{
  DeviceBuffer ret = {};
  ret.hdl = makeBuffer(dev, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, shouldExport);
  ret.mem = allocateBufferMemory(dev, impl->physicalDevice, ret.hdl, 
                                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                 shouldExport);
  ret.size = size;

  ret.mDev = this;
  return ret;
}

StagingBuffer::~StagingBuffer()
{
  vkUnmapMemory(mDev->dev, mem);

  vkFreeMemory(mDev->dev, mem, nullptr);
  vkDestroyBuffer(mDev->dev, hdl, nullptr);
}

VkSemaphore 
GPUDevice::makeExportSemaphore() const
{
  VkExportSemaphoreCreateInfoKHR exportInfo = {
    .sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO_KHR,
    .handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT
  };

  VkSemaphoreCreateInfo semaphoreInfo = {
    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    .pNext = &exportInfo
  };

  VkSemaphore semaphore;
  VK_CHECK(vkCreateSemaphore(dev, &semaphoreInfo, nullptr, &semaphore));

  return semaphore;
}

DeviceImage
GPUDevice::make2DSampledColorDeviceImage(VkFormat format, 
                                         VkExtent2D extent,
                                         bool shouldExport) const
{
  VkExternalMemoryImageCreateInfo externalInfo = {};

  VkExtent3D extent3D = {
    .width = extent.width,
    .height = extent.height,
    .depth = 1
  };

  VkImageCreateInfo imageInfo = {
    .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
    .imageType = VK_IMAGE_TYPE_2D,
    .format = format,
    .extent = extent3D,
    .mipLevels = 1,
    .arrayLayers = 1,
    .samples = VK_SAMPLE_COUNT_1_BIT,
    .tiling = VK_IMAGE_TILING_OPTIMAL,
    .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
             VK_IMAGE_USAGE_SAMPLED_BIT,
    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
  };

  if (shouldExport)
  {
    externalInfo.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
    externalInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT_KHR;

    imageInfo.pNext = &externalInfo;
  }

  VkImage image;
  VK_CHECK(vkCreateImage(dev, &imageInfo, nullptr, &image));

  uint64 size;
  VkDeviceMemory memory = allocateImageMemory(dev, impl->physicalDevice, image, 
                                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                              &size,
                                              shouldExport);

  vkBindImageMemory(dev, image, memory, 0);

  VkImageSubresourceRange range = {
    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
    .baseMipLevel = 0,
    .levelCount = 1,
    .baseArrayLayer = 0,
    .layerCount = 1
  };

  VkImageViewCreateInfo viewInfo = {
    .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
    .image = image,
    .viewType = VK_IMAGE_VIEW_TYPE_2D,
    .format = format,
    .subresourceRange = range
  };

  VkImageView view;
  VK_CHECK(vkCreateImageView(dev, &viewInfo, nullptr, &view));

  return {
    image, view, memory, extent3D, size
  };
}

int 
GPUDevice::getSemaphoreHandle(VkExternalSemaphoreHandleTypeFlagBitsKHR type,
                              VkSemaphore semaphore) const
{
  if (type != VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT)
    return -1;

  VkSemaphoreGetFdInfoKHR getInfo = {
    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR,
    .pNext = nullptr,
    .semaphore = semaphore,
    .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT_KHR
  };

  int fd;
  vkGetSemaphoreFdKHRProc(dev, &getInfo, &fd);

  return fd;
}

int 
GPUDevice::getMemoryHandle(VkExternalMemoryHandleTypeFlagsKHR type,
                           VkDeviceMemory mem) const
{
  if (type != VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT_KHR)
    return -1;

  VkMemoryGetFdInfoKHR getInfo = {
    .sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR,
    .pNext = nullptr,
    .memory = mem,
    .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT_KHR
  };

  int fd;
  vkGetMemoryFdKHRProc(dev, &getInfo, &fd);

  return fd;
}

VkDescriptorSetLayout GPUDevice::makeDescriptorSetLayoutImpl(VkDescriptorSetLayoutBinding *bindings,
                                                             uint32 numBindings) const
{
  VkDescriptorSetLayout ret;

  VkDescriptorSetLayoutCreateInfo info = {
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    .bindingCount = numBindings,
    .pBindings = bindings
  };

  vkCreateDescriptorSetLayout(dev, &info, nullptr, &ret);

  return ret;
}

ComputePipeline GPUDevice::makeComputePipeline(void *spirvCode,
                                               uint32 codeSize,
                                               uint32 pushConstantSize,
                                               uint32 setLayoutCount,
                                               VkDescriptorSetLayout *layouts) const
{
  VkShaderModuleCreateInfo moduleInfo = {
    .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    .codeSize = codeSize,
    .pCode = (uint32 *)spirvCode
  };

  VkShaderModule module;
  vkCreateShaderModule(dev, &moduleInfo, nullptr, &module);

  VkPushConstantRange range = {
    .stageFlags = VK_SHADER_STAGE_ALL,
    .offset=  0,
    .size = pushConstantSize
  };

  VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    .setLayoutCount = setLayoutCount,
    .pSetLayouts = layouts,
    .pushConstantRangeCount = 1,
    .pPushConstantRanges = &range
  };

  VkPipelineLayout pipelineLayout;
  vkCreatePipelineLayout(dev, &pipelineLayoutInfo, nullptr, &pipelineLayout);

  VkPipelineShaderStageCreateInfo stageCreateInfo = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
    .stage = VK_SHADER_STAGE_COMPUTE_BIT,
    .module = module,
    .pName = "main"
  };

  VkComputePipelineCreateInfo info = {
    .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
    .stage = stageCreateInfo,
    .layout = pipelineLayout
  };

  VkPipeline pipeline;
  vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);

  return { pipeline, pipelineLayout };
}

VkDescriptorSet GPUDevice::makeDescriptorSet(VkDescriptorSetLayout layout) const
{
  VkDescriptorSetAllocateInfo info = {
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
    .descriptorPool = impl->defaultDescriptorPool,
    .descriptorSetCount = 1,
    .pSetLayouts = &layout
  };

  VkDescriptorSet set;
  vkAllocateDescriptorSets(dev, &info, &set);

  return set;
}

PFN_vkCmdBeginRenderingKHR vkCmdBeginRenderingKHRProc;
PFN_vkCmdEndRenderingKHR vkCmdEndRenderingKHRProc;
PFN_vkGetMemoryFdKHR vkGetMemoryFdKHRProc;
PFN_vkGetSemaphoreFdKHR vkGetSemaphoreFdKHRProc;
PFN_vkGetPhysicalDeviceProperties2 vkGetPhysicalDeviceProperties2Proc;
