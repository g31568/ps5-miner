// SPDX-License-Identifier: GPL-3.0-or-later
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "native-engine.h"
#include "reference.h"
#include "radv_dispatch.h"
// All diagnostics are bounded and omit pool credentials.
static void c29_report(const char *format, ...) {
  char line[512]; va_list args; va_start(args, format);
  vsnprintf(line, sizeof(line), format, args); va_end(args); ps5_gpu_log(line);
}
#define CHECK(call) do { VkResult r=(call); if(r!=VK_SUCCESS) { \
  c29_report("%s failed: %d\n", #call, (int)r); goto cleanup; } } while(0)
// The shared differential helpers retain their sentinel even in the mining adapter.
enum { GUARD_WORD=0xa5b6c7d8 };
#include "solver-probe.h"
#include "trim-probe.h"
#include "bucket-worker.h"
#include "pool-run.h"
// One worker owns the instance, queue and persistent graph buffers through shutdown.
int ps5_gpu_run(const char *config_path) {
  int result=1; VkInstance instance=VK_NULL_HANDLE; VkDevice device=VK_NULL_HANDLE;
  VkPhysicalDevice *physical=NULL; VkQueueFamilyProperties *families=NULL;
  if(!c29_radv_load_global()) return 1;
  VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,
    /* Match the combined app name in Vulkan diagnostics. */
    .pApplicationName="XMR Miner",.apiVersion=VK_API_VERSION_1_0};
  VkInstanceCreateInfo ii={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};
  CHECK(vkCreateInstance(&ii,NULL,&instance));
  if(!c29_radv_load_instance(instance)) goto cleanup;
  uint32_t count=0; CHECK(vkEnumeratePhysicalDevices(instance,&count,NULL));
  if(!count) goto cleanup;
  physical=calloc(count,sizeof(*physical)); if(!physical) goto cleanup;
  CHECK(vkEnumeratePhysicalDevices(instance,&count,physical));
  VkPhysicalDeviceProperties properties; vkGetPhysicalDeviceProperties(physical[0],&properties);
  if(properties.limits.maxComputeWorkGroupInvocations<1024 || properties.limits.maxComputeWorkGroupSize[0]<1024) goto cleanup;
  uint32_t family_count=0, family=UINT32_MAX;
  vkGetPhysicalDeviceQueueFamilyProperties(physical[0],&family_count,NULL);
  families=calloc(family_count?family_count:1,sizeof(*families)); if(!families) goto cleanup;
  vkGetPhysicalDeviceQueueFamilyProperties(physical[0],&family_count,families);
  for(uint32_t i=0;i<family_count;i++) if(families[i].queueCount && (families[i].queueFlags&VK_QUEUE_COMPUTE_BIT)){family=i;break;}
  if(family==UINT32_MAX) goto cleanup;
  c29_bucket_timestamp_period=properties.limits.timestampPeriod;
  c29_bucket_timestamp_bits=families[family].timestampValidBits;
  mom_c29_set_flat_dictionary(true);
  float priority=1;
  VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
    .queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};
  VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&qi};
  CHECK(vkCreateDevice(physical[0],&di,NULL,&device));
  if(!c29_radv_load_device(device)) goto cleanup;
  VkQueue queue; vkGetDeviceQueue(device,family,0,&queue);
  VkPhysicalDeviceMemoryProperties memory; vkGetPhysicalDeviceMemoryProperties(physical[0],&memory);
  if(ps5_gpu_cancelled()){result=0;goto cleanup;}
  result=c29_pool_run(config_path,device,queue,family,&memory);
  // A timed-out submission has uncertain ownership: retain resources and forbid restart.
  if(result<0){free(families);free(physical);return -1;}
cleanup:
  if(device){c29_bucket_destroy(device,&c29_bucket_full);vkDestroyDevice(device,NULL);}
  if(instance) vkDestroyInstance(instance,NULL);
  free(families);free(physical);return result;
}
