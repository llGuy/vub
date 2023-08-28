#include "gpu-device.h"

#define NUM_INPUTS (2048*2048)

int main(int argc, char **argv)
{
  /* Initialize Vulkan instance, device, etc. */
  GPUDevice gpu = GPUDevice::make(nullptr);

  VkDescriptorSetLayout layout = gpu.makeDescriptorSetLayout(
    BindingDesc{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}
  );

  StagingBuffer inputStaging = gpu.makeStagingBuffer(NUM_INPUTS * sizeof(uint32));
  DeviceBuffer inputBuffer = gpu.makeDeviceBuffer(NUM_INPUTS * sizeof(uint32));


}
