// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MOM_C29_RADV_DISPATCH_H
#define MOM_C29_RADV_DISPATCH_H

// The static PS5 RADV archive exposes an ICD, not a Vulkan loader's public symbols.
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(
  VkInstance instance, const char *name);

#define C29_GLOBAL_COMMANDS(X) \
  X(CreateInstance) \
  X(EnumerateInstanceExtensionProperties)
#define C29_INSTANCE_COMMANDS(X) \
  X(DestroyInstance) \
  X(EnumeratePhysicalDevices) \
  X(GetPhysicalDeviceProperties) \
  X(GetPhysicalDeviceQueueFamilyProperties) \
  X(GetPhysicalDeviceMemoryProperties) \
  X(EnumerateDeviceExtensionProperties) \
  X(CreateDevice) \
  X(GetDeviceProcAddr)
#define C29_DEVICE_COMMANDS(X) \
  X(DestroyDevice) \
  X(GetDeviceQueue) \
  X(CreateBuffer) \
  X(DestroyBuffer) \
  X(GetBufferMemoryRequirements) \
  X(AllocateMemory) \
  X(FreeMemory) \
  X(BindBufferMemory) \
  X(MapMemory) \
  X(UnmapMemory) \
  X(CreateShaderModule) \
  X(DestroyShaderModule) \
  X(CreateDescriptorSetLayout) \
  X(DestroyDescriptorSetLayout) \
  X(CreatePipelineLayout) \
  X(DestroyPipelineLayout) \
  X(CreateComputePipelines) \
  X(DestroyPipeline) \
  X(CreateDescriptorPool) \
  X(DestroyDescriptorPool) \
  X(AllocateDescriptorSets) \
  X(UpdateDescriptorSets) \
  X(CreateCommandPool) \
  X(DestroyCommandPool) \
  X(AllocateCommandBuffers) \
  X(ResetCommandBuffer) \
  X(BeginCommandBuffer) \
  X(EndCommandBuffer) \
  X(CmdBindPipeline) \
  X(CmdBindDescriptorSets) \
  X(CmdPushConstants) \
  X(CmdDispatch) \
  X(CmdFillBuffer) \
  X(CmdCopyBuffer) \
  X(CmdPipelineBarrier) \
  X(CreateQueryPool) \
  X(DestroyQueryPool) \
  X(CmdResetQueryPool) \
  X(CmdWriteTimestamp) \
  X(GetQueryPoolResults) \
  X(CreateFence) \
  X(DestroyFence) \
  X(ResetFences) \
  X(QueueSubmit) \
  X(WaitForFences)

#define C29_DECLARE(name) static PFN_vk##name vk##name;
C29_GLOBAL_COMMANDS(C29_DECLARE)
C29_INSTANCE_COMMANDS(C29_DECLARE)
C29_DEVICE_COMMANDS(C29_DECLARE)
#undef C29_DECLARE

// Missing entry points are fatal before any calls through a partially loaded dispatch table.
static int c29_radv_load_global(void) {
#define C29_LOAD(name) \
  vk##name = (PFN_vk##name)vk_icdGetInstanceProcAddr(VK_NULL_HANDLE, "vk" #name); \
  if (!vk##name) { fprintf(stderr, "Missing RADV entry: vk%s\n", #name); return 0; }
  C29_GLOBAL_COMMANDS(C29_LOAD)
#undef C29_LOAD
  return 1;
}

// Instance lookup also supplies the device-procedure resolver.
static int c29_radv_load_instance(VkInstance instance) {
#define C29_LOAD(name) \
  vk##name = (PFN_vk##name)vk_icdGetInstanceProcAddr(instance, "vk" #name); \
  if (!vk##name) { fprintf(stderr, "Missing RADV entry: vk%s\n", #name); return 0; }
  C29_INSTANCE_COMMANDS(C29_LOAD)
#undef C29_LOAD
  return 1;
}

// Use device lookup for the exact logical device whose resources the probe owns.
static int c29_radv_load_device(VkDevice device) {
#define C29_LOAD(name) \
  vk##name = (PFN_vk##name)vkGetDeviceProcAddr(device, "vk" #name); \
  if (!vk##name) { fprintf(stderr, "Missing RADV entry: vk%s\n", #name); return 0; }
  C29_DEVICE_COMMANDS(C29_LOAD)
#undef C29_LOAD
  return 1;
}
#endif
