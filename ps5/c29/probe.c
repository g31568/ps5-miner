// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef MOM_PS5_RADV
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "reference.h"
#include "../manager.h"
#ifdef MOM_PS5_RADV
#include "radv_dispatch.h"
#if defined(__PROSPERO__)
#include <unistd.h>
int ps5_klog_capture_stderr(const char *prefix);
int sceKernelDebugOutText(int channel, const char *text);
int sceSystemServiceLoadExec(const char *path, const char **arguments);
#endif
#endif

// Emit our verdict synchronously on PS5 so process exit cannot drop a queued log line.
static void c29_report(const char *format, ...) {
  char line[512];
  va_list arguments;
  va_start(arguments, format);
  vsnprintf(line, sizeof(line), format, arguments);
  va_end(arguments);
#if defined(MOM_PS5_RADV) && defined(__PROSPERO__)
  char record[544];
  snprintf(record, sizeof(record), "[c29] %s", line);
  sceKernelDebugOutText(0, record);
#else
  fputs(line, stderr);
#endif
}

#if defined(MOM_PS5_RADV) && defined(__PROSPERO__)
// Native titles must ask the shell to exit; the payload libc's exit syscall faults here.
void catchReturnFromMain(int status) {
  c29_report("cleanup complete status=%d; requesting native exit\n", status);
  int result = sceSystemServiceLoadExec("exit", NULL);
  c29_report("native exit request returned=%d\n", result);
  // The shell terminates the title asynchronously; do not fall through to libc exit.
  for (;;) sleep(1);
}
#endif

static const uint32_t shader[] =
#include "seed.spv.inc"
;

enum { MAX_EDGES = 8193, GUARD_EDGES = 16, GUARD_WORD = 0xa5b6c7d8 };
typedef struct {
  uint32_t seed[4][2];
  uint32_t first_nonce, edge_count, output_offset;
} Parameters;
_Static_assert(sizeof(Parameters) == 44, "GLSL push constant ABI");

#define CHECK(call) do { \
  VkResult result_ = (call); \
  if (result_ != VK_SUCCESS) { \
    c29_report("%s failed: %d\n", #call, (int)result_); \
    goto cleanup; \
  } \
} while (0)

#include "solver-probe.h"
#include "trim-probe.h"
#include "bucket-worker.h"
#include "pool-run.h"

// Execute bounded native compute work and check all output and untouched guard words.
int main(void) {
#ifdef MOM_PS5_RADV
#if defined(__PROSPERO__)
  // Native titles have no terminal; route diagnostics and the final verdict to klog.
  if (ps5_klog_capture_stderr("[c29] ") != 0) return 1;
#endif
  if (!c29_radv_load_global()) return 1;
#endif
  // Check the same native lifecycle code before allocating any GPU resources.
  if (mom_manager_selftest() != 0) return 1;
  c29_report("PASS native management lifecycle and proof checks\n");
  if (mom_c29_solver_selftest() != 0) return 1;
  c29_report("PASS c29 bitmap solver adapter checks\n");
  int status = 1;
  VkInstance instance = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkBuffer buffer = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkShaderModule module = VK_NULL_HANDLE;
  VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
  VkPipelineLayout layout = VK_NULL_HANDLE;
  VkPipeline pipeline = VK_NULL_HANDLE;
  VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
  VkCommandPool command_pool = VK_NULL_HANDLE;
  VkFence fence = VK_NULL_HANDLE;
  uint32_t *mapped = NULL;
  VkPhysicalDevice *physical_devices = NULL;
  VkQueueFamilyProperties *families = NULL;
  VkExtensionProperties *extensions = NULL;
  const VkDeviceSize byte_count = (MAX_EDGES + 2 * GUARD_EDGES) * 8;
  const char *instance_extensions[1];
  uint32_t extension_count = 0, enabled_count = 0;
  VkInstanceCreateInfo instance_info = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                          .pApplicationName = "C29 edge probe", .apiVersion = VK_API_VERSION_1_0};
  instance_info.pApplicationInfo = &app;
  CHECK(vkEnumerateInstanceExtensionProperties(NULL, &extension_count, NULL));
  extensions = calloc(extension_count ? extension_count : 1, sizeof(*extensions));
  if (!extensions) goto cleanup;
  CHECK(vkEnumerateInstanceExtensionProperties(NULL, &extension_count, extensions));
  for (uint32_t i = 0; i < extension_count; ++i) {
    if (strcmp(extensions[i].extensionName, "VK_KHR_portability_enumeration") == 0) {
      instance_extensions[enabled_count++] = "VK_KHR_portability_enumeration";
      instance_info.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
  }
  instance_info.enabledExtensionCount = enabled_count;
  instance_info.ppEnabledExtensionNames = instance_extensions;
  CHECK(vkCreateInstance(&instance_info, NULL, &instance));
#ifdef MOM_PS5_RADV
  if (!c29_radv_load_instance(instance)) return 1;
#endif
  uint32_t physical_count = 0;
  CHECK(vkEnumeratePhysicalDevices(instance, &physical_count, NULL));
  if (!physical_count) {
    c29_report("No Vulkan physical device\n");
    goto cleanup;
  }
  physical_devices = calloc(physical_count, sizeof(*physical_devices));
  if (!physical_devices) goto cleanup;
  CHECK(vkEnumeratePhysicalDevices(instance, &physical_count, physical_devices));
  VkPhysicalDevice physical = physical_devices[0];
  VkPhysicalDeviceProperties properties;
  vkGetPhysicalDeviceProperties(physical, &properties);
  c29_report("device=%s push_bytes=%zu\n", properties.deviceName, sizeof(Parameters));
  uint32_t family_count = 0, queue_family = UINT32_MAX;
  vkGetPhysicalDeviceQueueFamilyProperties(physical, &family_count, NULL);
  families = calloc(family_count ? family_count : 1, sizeof(*families));
  if (!families) goto cleanup;
  vkGetPhysicalDeviceQueueFamilyProperties(physical, &family_count, families);
  for (uint32_t i = 0; i < family_count; ++i) {
    if (families[i].queueCount && (families[i].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
      queue_family = i;
      break;
    }
  }
  if (queue_family == UINT32_MAX) goto cleanup;
  // Timestamp counters use the selected compute queue's advertised precision and period.
  c29_bucket_timestamp_period=properties.limits.timestampPeriod;
  c29_bucket_timestamp_bits=families[queue_family].timestampValidBits;
#if defined(MOM_PS5_RADV) && defined(__PROSPERO__)
  FILE *profile_request=fopen("/app0/profile-c29.txt","r");
  if(profile_request) {
    unsigned mode=1; (void)fscanf(profile_request,"%u",&mode);
    c29_bucket_profile=true;c29_bucket_ablate=mode==2;fclose(profile_request);
  }
#else
  const char *profile_request=getenv("MOM_C29_PROFILE");
  c29_bucket_profile=profile_request && (strcmp(profile_request,"1")==0 || strcmp(profile_request,"2")==0);
  c29_bucket_ablate=profile_request && strcmp(profile_request,"2")==0;
#endif
  if(c29_bucket_profile) c29_report("bucket profiling enabled: timestamp_bits=%u period_ns=%.6f\n",
    c29_bucket_timestamp_bits,c29_bucket_timestamp_period);
  // Public tuning values never contain pool credentials and are validated before pipeline creation.
  unsigned tune_layout=c29_bucket_layout,tune_threads=c29_bucket_trim_threads,tune_batch=c29_bucket_batch_rounds;
  unsigned tune_seed=c29_bucket_seed_mode;
  unsigned tune_cpu=1; // Matched native trials select the flat table; field five restores the baseline.
#if defined(MOM_PS5_RADV) && defined(__PROSPERO__)
  FILE *tune_request=fopen("/app0/tune-c29.txt","r");
  if(tune_request) {
    int fields=fscanf(tune_request,"%u %u %u %u %u %u %u %u %u",&tune_layout,&tune_threads,&tune_batch,&tune_seed,&tune_cpu,&c29_seed_threads,&c29_redistribute_threads,&c29_stagger_step,&c29_output_cache);fclose(tune_request);
    if(fields<3) goto cleanup;
  }
#else
  const char *tune_request=getenv("MOM_C29_TUNE");
  if(tune_request && sscanf(tune_request,"%u %u %u %u %u %u %u %u %u",&tune_layout,&tune_threads,&tune_batch,&tune_seed,&tune_cpu,&c29_seed_threads,&c29_redistribute_threads,&c29_stagger_step,&c29_output_cache)<3) goto cleanup;
#endif
  if(c29_output_cache>2 || tune_cpu>1 || tune_seed>3 || !c29_bucket_set_tuning(tune_layout,tune_threads,tune_batch) ||
     (c29_stagger_step!=16 && c29_stagger_step!=24 && c29_stagger_step!=32 &&
      c29_stagger_step!=40 && c29_stagger_step!=48 && c29_stagger_step!=64) ||
     (c29_seed_threads!=128 && c29_seed_threads!=256) ||
     (c29_redistribute_threads!=128 && c29_redistribute_threads!=256) ||
     c29_seed_threads>properties.limits.maxComputeWorkGroupInvocations ||
     c29_seed_threads>properties.limits.maxComputeWorkGroupSize[0] ||
     c29_redistribute_threads>properties.limits.maxComputeWorkGroupInvocations ||
     c29_redistribute_threads>properties.limits.maxComputeWorkGroupSize[0] ||
     tune_threads>properties.limits.maxComputeWorkGroupInvocations ||
     tune_threads>properties.limits.maxComputeWorkGroupSize[0]) goto cleanup;
  c29_bucket_seed_mode=tune_seed;
  mom_c29_set_flat_dictionary(tune_cpu!=0);
  c29_report("CPU dictionary: flat=%u\n",tune_cpu);
  c29_report("first-trim output cache: mode=%u\n",c29_output_cache);
  c29_report("bucket stagger: edges=%u\n",c29_stagger_step);
  c29_report("seed workgroups: generation=%u redistribution=%u\n",c29_seed_threads,c29_redistribute_threads);
  c29_report("bucket tuning: layout=%u trim_threads=%u batch_rounds=%u seed_mode=%u\n",tune_layout,tune_threads,tune_batch,tune_seed);
  // Trial controls are public integers; absent controls preserve the baseline.
  unsigned recent=c29_recent_rate ? 1u : 0u;
  c29_late_threads=tune_threads; // An unused late pipeline must respect the selected device limits.
#if defined(MOM_PS5_RADV) && defined(__PROSPERO__)
  FILE *trial=fopen("/app0/experiment-c29.txt","r");
  if(trial) {
    int fields=fscanf(trial,"%u %u %u %u %u %u %u %u",&c29_late_threads,&c29_late_start,
      &c29_trial_rounds,&c29_seed_batch,&c29_keep_prefetch,&c29_retry_overflow,&recent,&c29_inject_overflow);
    fclose(trial);if(fields!=8) goto cleanup;
  }
#else
  const char *trial=getenv("MOM_C29_EXPERIMENT");
  if(trial && sscanf(trial,"%u %u %u %u %u %u %u %u",&c29_late_threads,&c29_late_start,
      &c29_trial_rounds,&c29_seed_batch,&c29_keep_prefetch,&c29_retry_overflow,&recent,&c29_inject_overflow)!=8) goto cleanup;
#endif
  if((c29_late_threads!=128 && c29_late_threads!=256 && c29_late_threads!=512 && c29_late_threads!=1024) ||
      c29_late_start>160 || c29_trial_rounds<32 || c29_trial_rounds>160 || (c29_trial_rounds&1) ||
      c29_seed_batch>1 || c29_keep_prefetch>1 || c29_retry_overflow>1 || recent>1 || c29_inject_overflow>2 ||
      c29_late_threads>properties.limits.maxComputeWorkGroupInvocations ||
      c29_late_threads>properties.limits.maxComputeWorkGroupSize[0]) goto cleanup;
  c29_recent_rate=recent!=0;
  c29_report("bucket experiment: late_threads=%u late_start=%u rounds=%u seed_batch=%u keep_prefetch=%u retry=%u recent_rate=%u inject=%u\n",
    c29_late_threads,c29_late_start,c29_trial_rounds,c29_seed_batch,c29_keep_prefetch,c29_retry_overflow,recent,c29_inject_overflow);
  float priority = 1;
  VkDeviceQueueCreateInfo queue_info = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
    .queueFamilyIndex = queue_family, .queueCount = 1, .pQueuePriorities = &priority};
  VkDeviceCreateInfo device_info = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
    .queueCreateInfoCount = 1, .pQueueCreateInfos = &queue_info};
  // MoltenVK requires its portability subset when advertised; PS5 needs no extension here.
  free(extensions);
  extensions = NULL;
  CHECK(vkEnumerateDeviceExtensionProperties(physical, NULL, &extension_count, NULL));
  extensions = calloc(extension_count ? extension_count : 1, sizeof(*extensions));
  if (!extensions) goto cleanup;
  CHECK(vkEnumerateDeviceExtensionProperties(physical, NULL, &extension_count, extensions));
  const char *device_extension = "VK_KHR_portability_subset";
  for (uint32_t i = 0; i < extension_count; ++i) {
    if (strcmp(extensions[i].extensionName, device_extension) == 0) {
      device_info.enabledExtensionCount = 1;
      device_info.ppEnabledExtensionNames = &device_extension;
    }
  }
  CHECK(vkCreateDevice(physical, &device_info, NULL, &device));
#ifdef MOM_PS5_RADV
  if (!c29_radv_load_device(device)) return 1;
#endif
  VkQueue queue;
  vkGetDeviceQueue(device, queue_family, 0, &queue);
  VkBufferCreateInfo buffer_info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    .size = byte_count, .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
    .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
  CHECK(vkCreateBuffer(device, &buffer_info, NULL, &buffer));
  VkMemoryRequirements requirements;
  vkGetBufferMemoryRequirements(device, buffer, &requirements);
  VkPhysicalDeviceMemoryProperties memory_properties;
  vkGetPhysicalDeviceMemoryProperties(physical, &memory_properties);
  // Record actual allocation limits for the later full-graph memory layout.
  c29_report("max_storage_buffer=%u max_workgroup_invocations=%u\n",
    properties.limits.maxStorageBufferRange, properties.limits.maxComputeWorkGroupInvocations);
  for (uint32_t i = 0; i < memory_properties.memoryHeapCount; ++i) {
    c29_report("heap=%u bytes=%" PRIu64 " flags=%u\n", i,
      (uint64_t)memory_properties.memoryHeaps[i].size, memory_properties.memoryHeaps[i].flags);
  }
  uint32_t memory_type = UINT32_MAX;
  for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i) {
    VkMemoryPropertyFlags required = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    if ((requirements.memoryTypeBits & (1u << i)) &&
        (memory_properties.memoryTypes[i].propertyFlags & required) == required) {
      memory_type = i;
      break;
    }
  }
  if (memory_type == UINT32_MAX) {
    c29_report("Probe requires host-visible coherent storage memory\n");
    goto cleanup;
  }
  VkMemoryAllocateInfo allocation = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .allocationSize = requirements.size, .memoryTypeIndex = memory_type};
  CHECK(vkAllocateMemory(device, &allocation, NULL, &memory));
  CHECK(vkBindBufferMemory(device, buffer, memory, 0));
  CHECK(vkMapMemory(device, memory, 0, byte_count, 0, (void **)&mapped));
  VkShaderModuleCreateInfo shader_info = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    .codeSize = sizeof(shader), .pCode = shader};
  CHECK(vkCreateShaderModule(device, &shader_info, NULL, &module));
  VkDescriptorSetLayoutBinding binding = {.binding = 0,
    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1,
    .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT};
  VkDescriptorSetLayoutCreateInfo set_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    .bindingCount = 1, .pBindings = &binding};
  CHECK(vkCreateDescriptorSetLayout(device, &set_info, NULL, &set_layout));
  VkPushConstantRange push_range = {.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
    .offset = 0, .size = sizeof(Parameters)};
  VkPipelineLayoutCreateInfo layout_info = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    .setLayoutCount = 1, .pSetLayouts = &set_layout,
    .pushConstantRangeCount = 1, .pPushConstantRanges = &push_range};
  CHECK(vkCreatePipelineLayout(device, &layout_info, NULL, &layout));
  VkComputePipelineCreateInfo pipeline_info = {.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
    .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = module, .pName = "main"}, .layout = layout};
  CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &pipeline));
  VkDescriptorPoolSize pool_size = {.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1};
  VkDescriptorPoolCreateInfo pool_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
    .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &pool_size};
  CHECK(vkCreateDescriptorPool(device, &pool_info, NULL, &descriptor_pool));
  VkDescriptorSetAllocateInfo set_allocation = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
    .descriptorPool = descriptor_pool, .descriptorSetCount = 1, .pSetLayouts = &set_layout};
  VkDescriptorSet descriptor_set;
  CHECK(vkAllocateDescriptorSets(device, &set_allocation, &descriptor_set));
  VkDescriptorBufferInfo descriptor_buffer = {.buffer = buffer, .offset = 0, .range = byte_count};
  VkWriteDescriptorSet write = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
    .dstSet = descriptor_set, .dstBinding = 0, .descriptorCount = 1,
    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &descriptor_buffer};
  vkUpdateDescriptorSets(device, 1, &write, 0, NULL);
  VkCommandPoolCreateInfo command_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = queue_family};
  CHECK(vkCreateCommandPool(device, &command_info, NULL, &command_pool));
  VkCommandBufferAllocateInfo command_allocation = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .commandPool = command_pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
  VkCommandBuffer command;
  CHECK(vkAllocateCommandBuffers(device, &command_allocation, &command));
  VkFenceCreateInfo fence_info = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  CHECK(vkCreateFence(device, &fence_info, NULL, &fence));
  const uint64_t seeds[][4] = {
    {0, 0, 0, 0}, {UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX},
    {UINT64_C(0x0123456789abcdef), UINT64_C(0xfedcba9876543210),
     UINT64_C(0x736f6d6570736575), UINT64_C(0x646f72616e646f6d)},
    {UINT64_C(0xffffffff), UINT64_C(0xffffffff00000000), 1, UINT64_C(0x8000000000000000)}
  };
  const uint32_t ranges[][2] = {
    {0, 0}, {0, 1}, {0, 63}, {0, 64}, {1, 64}, {63, 2},
    {64, 128}, {4095, MAX_EDGES}, {(1u << 29) - 65, 65}, {(1u << 29) - 1, 1}
  };
  uint64_t checked = 0;
  unsigned cases = 0;
  for (unsigned s = 0; s < sizeof(seeds) / sizeof(seeds[0]); ++s) {
    for (unsigned r = 0; r < sizeof(ranges) / sizeof(ranges[0]); ++r) {
      Parameters params = {.first_nonce = ranges[r][0], .edge_count = ranges[r][1],
                           .output_offset = GUARD_EDGES};
      // Validate the public shader contract before computing dispatch sizes.
      if (params.first_nonce >= (1u << 29) || params.edge_count > MAX_EDGES ||
          params.edge_count > (1u << 29) - params.first_nonce) goto cleanup;
      for (unsigned k = 0; k < 4; ++k) {
        params.seed[k][0] = (uint32_t)seeds[s][k];
        params.seed[k][1] = (uint32_t)(seeds[s][k] >> 32);
      }
      for (size_t i = 0; i < byte_count / 4; ++i) mapped[i] = GUARD_WORD;
      CHECK(vkResetCommandBuffer(command, 0));
      VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
      CHECK(vkBeginCommandBuffer(command, &begin));
      vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
      vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, layout,
                              0, 1, &descriptor_set, 0, NULL);
      vkCmdPushConstants(command, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(params), &params);
      uint32_t blocks = ((params.first_nonce & 63) + params.edge_count + 63) / 64;
      vkCmdDispatch(command, blocks ? (blocks + 63) / 64 : 1, 1, 1);
      VkMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
      vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, NULL, 0, NULL);
      CHECK(vkEndCommandBuffer(command));
      CHECK(vkResetFences(device, 1, &fence));
      VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1, .pCommandBuffers = &command};
      CHECK(vkQueueSubmit(queue, 1, &submit, fence));
      VkResult wait = vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_C(30000000000));
      if (wait != VK_SUCCESS) {
        // Never destroy resources still owned by a timed-out GPU submission.
        c29_report("GPU completion failed: %d; probe aborted\n", (int)wait);
        return 1;
      }
      uint32_t reference[64][2], previous_block = UINT32_MAX;
      for (uint32_t i = 0; i < params.edge_count; ++i) {
        uint32_t nonce = params.first_nonce + i, base = nonce & ~63u;
        if (base != previous_block) {
          c29_reference_block(seeds[s], base, reference);
          previous_block = base;
        }
        for (unsigned side = 0; side < 2; ++side) {
          uint32_t actual = mapped[(GUARD_EDGES + i) * 2 + side];
          if (actual != reference[nonce & 63][side]) {
            c29_report("Mismatch seed=%u nonce=%u side=%u expected=%08x got=%08x\n",
              s, nonce, side, reference[nonce & 63][side], actual);
            goto cleanup;
          }
        }
      }
      for (uint32_t i = 0; i < byte_count / 4; ++i) {
        if (i >= GUARD_EDGES * 2 && i < (GUARD_EDGES + params.edge_count) * 2) continue;
        if (mapped[i] != GUARD_WORD) {
          c29_report("Guard overwritten seed=%u case=%u word=%u\n", s, r, i);
          goto cleanup;
        }
      }
      checked += params.edge_count;
      ++cases;
    }
  }
  c29_report("PASS c29 seed: %u cases, %" PRIu64 " edges, all guards intact\n", cases, checked);
  status = 0;
  // Reuse the verified device/queue to validate the bounded-memory trimming stage.
  status = c29_trim_probe(device, queue, queue_family, &memory_properties, 0, NULL);
  if (!status) status = c29_bucket_selftest(device, queue, queue_family, &memory_properties);
  if (status < 0) return 1;
  // Pool mode is opt-in through a separate configuration file, outside the redistributable app.
  const char *pool_config = NULL;
#if defined(MOM_PS5_RADV) && defined(__PROSPERO__)
  FILE *pool_request = fopen("/app0/c29-pool.json", "r");
  if (pool_request) { fclose(pool_request); pool_config = "/app0/c29-pool.json"; }
#else
  pool_config = getenv("MOM_C29_POOL_CONFIG");
#endif
  if (!status && pool_config) {
    status = c29_pool_run(pool_config, device, queue, queue_family, &memory_properties);
    if (status < 0) return 1;
    goto cleanup;
  }
  if (status == 0) {
    // Full-size allocation/execution is opt-in; normal launches retain bounded differential cases.
    unsigned full_rounds = 0;
    bool solve_requested = false;
#if defined(MOM_PS5_RADV) && defined(__PROSPERO__)
    FILE *request = fopen("/app0/full-graph-rounds.txt", "r");
    if (request) {
      if (fscanf(request, "%u", &full_rounds) != 1) full_rounds = UINT32_MAX;
      fclose(request);
    }
    request = fopen("/app0/solve-c29.txt", "r");
    if (request) { solve_requested = true; fclose(request); }
#else
    const char *request = getenv("MOM_C29_FULL_ROUNDS");
    if (request && sscanf(request, "%u", &full_rounds) != 1) full_rounds = UINT32_MAX;
    const char *solve_request = getenv("MOM_C29_SOLVE");
    solve_requested = solve_request && strcmp(solve_request, "1") == 0;
#endif
    if (solve_requested) full_rounds = 160;
    if (full_rounds && (full_rounds < 2 || full_rounds > 160 || (full_rounds & 1))) {
      c29_report("Full graph rounds must be even and between 2 and 160\n");
      status = 1;
    } else if (full_rounds) {
      if (properties.limits.maxStorageBufferRange < (192u << 20) + 256u) {
        c29_report("Full graph storage-buffer range unsupported\n");
        status = 1;
      } else {
        C29SolveContext solve;
        if (solve_requested && !c29_solve_start(&solve)) goto cleanup;
        // Keep the bitmap backend available for short diagnostic runs.
        status = solve_requested ? c29_bucket_run(device, queue, queue_family, &memory_properties, full_rounds, &solve) :
          c29_trim_probe(device, queue, queue_family, &memory_properties, full_rounds, NULL);
        if (status < 0) return 1;
      }
    }
  }

cleanup:
  // Submissions reaching cleanup have completed; release Vulkan owners in reverse order.
  if (device) {
    if (fence) vkDestroyFence(device, fence, NULL);
    if (command_pool) vkDestroyCommandPool(device, command_pool, NULL);
    if (descriptor_pool) vkDestroyDescriptorPool(device, descriptor_pool, NULL);
    if (pipeline) vkDestroyPipeline(device, pipeline, NULL);
    if (layout) vkDestroyPipelineLayout(device, layout, NULL);
    if (set_layout) vkDestroyDescriptorSetLayout(device, set_layout, NULL);
    if (module) vkDestroyShaderModule(device, module, NULL);
    if (mapped) vkUnmapMemory(device, memory);
    if (buffer) vkDestroyBuffer(device, buffer, NULL);
    if (memory) vkFreeMemory(device, memory, NULL);
    c29_bucket_destroy(device, &c29_bucket_full);
    vkDestroyDevice(device, NULL);
  }
  if (instance) vkDestroyInstance(instance, NULL);
  free(extensions);
  free(families);
  free(physical_devices);
  return status;
}
