#pragma once


#include <memory>
#include "types.h"
#include <vulkan/vulkan.h>

#define VK_CHECK(call) \
  if (call != VK_SUCCESS) { printf("Vulkan: %s failed\n", #call); \
    PANIC_AND_EXIT("Vulkan call failed"); }

struct Surface;
struct GPUDevice;

struct StagingBuffer {
  VkBuffer hdl;
  VkDeviceMemory mem;
  void *ptr;

  ~StagingBuffer();

private:
  const GPUDevice *mDev;

  friend class GPUDevice;
};

struct DeviceBuffer {
  VkBuffer hdl;
  VkDeviceMemory mem;
  uint64 size;

private:
  const GPUDevice *mDev;

  friend class GPUDevice;
};

struct DeviceImage {
  VkImage image;
  VkImageView view;
  VkDeviceMemory memory;
  VkExtent3D extent;
  uint64 memorySize;
};

struct ComputePipeline {
  VkPipeline hdl;
  VkPipelineLayout layout;
};

/* It's expected for the purposes of this program that the binding numbers 
 * are just the order in which the bindings appear in the makeDescriptorSetLayout 
 * function. */
struct BindingDesc {
  VkDescriptorType descriptorType;
  uint32 descriptorCount;
};

struct GPUDevice {
  struct Impl;

  VkDevice dev;
  std::unique_ptr<Impl> impl;

  static GPUDevice make(const Surface *surface);
  static VkImageMemoryBarrier makeBarrier(VkImage image, 
                                          VkImageAspectFlags aspect,
                                          VkImageLayout oldLayout, 
                                          VkImageLayout newLayout,
                                          uint32 levelCount = 1, 
                                          uint32 layerCount = 1);
  static VkBufferMemoryBarrier makeBarrier(VkBuffer buffer,
                                           uint32 offset,
                                           uint32 size,
                                           VkPipelineStageFlags src,
                                           VkPipelineStageFlags dst);

  void waitIdle() const;
  VkCommandBuffer makeCommandBuffer() const;
  void freeCommandBuffer(VkCommandBuffer cmdbuf) const;
  void beginCommandBuffer(VkCommandBuffer cmdbuf) const;
  void beginSingleUseCommandBuffer(VkCommandBuffer cmdbuf) const;
  void endCommandBuffer(VkCommandBuffer cmdbuf) const;
  void submitCommandBuffer(VkCommandBuffer cmdbuf, 
                           VkSemaphore wait, 
                           VkSemaphore signal,
                           VkPipelineStageFlags waitStage,
                           VkFence signalFence) const;
  void submitCommandBuffer(VkCommandBuffer cmdbuf, 
                           uint32 waitCount,
                           VkSemaphore *wait, 
                           VkSemaphore signal,
                           VkPipelineStageFlags *waitStage,
                           VkFence signalFence) const;
  uint32 acquireNextImage(VkSemaphore semaphore) const;
  void present(VkSemaphore wait, uint32 imageIndex) const;
  VkImage getSwapchainImage(uint32 index) const;
  VkImageView getSwapchainImageView(uint32 index) const;
  VkExtent2D getSwapchainExtent() const;
  StagingBuffer makeStagingBuffer(uint64 size) const;
  DeviceBuffer makeDeviceBuffer(uint64 size, bool shouldExport = false) const;
  VkSemaphore makeExportSemaphore() const;
  DeviceImage make2DSampledColorDeviceImage(VkFormat format, 
                                            VkExtent2D extent,
                                            bool shouldExport = false) const;
  VkDescriptorSetLayout makeDescriptorSetLayoutImpl(VkDescriptorSetLayoutBinding *bindings,
                                                    uint32 numBindings) const;
  VkDescriptorSet makeDescriptorSet(VkDescriptorSetLayout layout) const;
  ComputePipeline makeComputePipeline(void *spirvCode,
                                      uint32 codeSize,
                                      uint32 pushConstantSize,
                                      uint32 setLayoutCount,
                                      VkDescriptorSetLayout *layouts) const;

  /* BindingT has to be of type BindingDesc */
  template <typename ...BindingT>
  VkDescriptorSetLayout makeDescriptorSetLayout(BindingT ...bindings);

  /* External functionality. */
  int getSemaphoreHandle(VkExternalSemaphoreHandleTypeFlagBitsKHR type,
                         VkSemaphore semaphore) const;
  int getMemoryHandle(VkExternalMemoryHandleTypeFlagsKHR type,
                      VkDeviceMemory mem) const;

  ~GPUDevice();
};

extern PFN_vkCmdBeginRenderingKHR vkCmdBeginRenderingKHRProc;
extern PFN_vkCmdEndRenderingKHR vkCmdEndRenderingKHRProc;
extern PFN_vkGetMemoryFdKHR vkGetMemoryFdKHRProc;
extern PFN_vkGetSemaphoreFdKHR vkGetSemaphoreFdKHRProc;
extern PFN_vkGetPhysicalDeviceProperties2 vkGetPhysicalDeviceProperties2Proc;

template <typename ...BindingT>
VkDescriptorSetLayout GPUDevice::makeDescriptorSetLayout(BindingT ...bindingsIn)
{
  auto makeDescriptorBindingInfo = [](BindingDesc desc, uint32 id)
  {
    return VkDescriptorSetLayoutBinding {
      .binding = id,
      .descriptorType = desc.descriptorType,
      .descriptorCount = desc.descriptorCount,
      .stageFlags = VK_SHADER_STAGE_ALL,
      .pImmutableSamplers = nullptr
    };
  };

  uint32 id = 0;
  VkDescriptorSetLayoutBinding bindings[] = {
    makeDescriptorBindingInfo(bindingsIn, id++)...
  };

  return makeDescriptorSetLayoutImpl(bindings, sizeof...(BindingT));
}
