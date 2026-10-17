// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MOM_C29_BUCKET_WORKER_H
#define MOM_C29_BUCKET_WORKER_H
#include <time.h>
// Profiling is opt-in; ordinary mining avoids queries and per-dispatch timing logs.
static bool c29_bucket_profile, c29_bucket_ablate;
static float c29_bucket_timestamp_period;
static uint32_t c29_bucket_timestamp_bits;
// Defaults use the fastest verified PS5 layout/workgroup; experimental batching stays disabled.
static uint32_t c29_bucket_layout=5,c29_bucket_trim_threads=1024,c29_bucket_batch_rounds=0;
// Circular tiles and paired seed staging win matched native trials without adding graph storage.
static unsigned c29_bucket_seed_mode=3;
// Seed generation and redistribution have independent register and memory workloads.
static unsigned c29_seed_threads=256,c29_redistribute_threads=256;
// Tune output locality while padding each regular bucket to preserve its usable capacity.
static unsigned c29_stagger_step=32;
// Keep experimental first-trim reservations disabled unless measured faster.
static unsigned c29_output_cache=0;
// Public overrides can reproduce the original baseline and rejected experiment variants.
static unsigned c29_late_threads=1024,c29_late_start=160,c29_trial_rounds=68;
// Retuning after flat CPU search selects 68 rounds, seed batching and preserved next-graph output.
static unsigned c29_seed_batch=1,c29_keep_prefetch=1,c29_retry_overflow=1,c29_inject_overflow;
static bool c29_recent_rate=true;
static bool c29_bucket_set_tuning(unsigned layout,unsigned threads,unsigned batch) {
  // First-trim padding requires staggering; retain layouts 0–3 as exact baselines.
  if(layout>7 || ((layout&4) && !(layout&1)) ||
      (threads!=256 && threads!=512 && threads!=1024) || batch>4) return false;
  c29_bucket_layout=layout;c29_bucket_trim_threads=threads;c29_bucket_batch_rounds=batch;return true;
}
static uint64_t c29_bucket_now_ns(void) {
  struct timespec now; clock_gettime(CLOCK_MONOTONIC,&now);
  return (uint64_t)now.tv_sec*UINT64_C(1000000000)+(uint64_t)now.tv_nsec;
}
static const uint32_t bucket_shader[] =
#include "bucket.spv.inc"
;
typedef struct {
  uint32_t seed[4][2], edge_count, node_mask, bucket_bits, seed_cap, main_cap, split, source, tail_cap;
} BucketParameters;
_Static_assert(sizeof(BucketParameters)==64, "Bucket shader ABI");
typedef struct {
  VkBuffer buffers[4]; VkDeviceMemory memories[4]; uint32_t *maps[4];
  VkShaderModule module; VkDescriptorSetLayout set_layout; VkPipelineLayout layout;
  VkPipeline pipelines[6]; VkDescriptorPool descriptors; VkDescriptorSet set;
  VkCommandPool commands; VkCommandBuffer command; VkFence fence;
  VkQueryPool queries; uint32_t profile_round, profile_count;
  double profile_init_us;
  struct {uint32_t op,round;double kernel_us,gpu_us,host_us,wait_us;} profile[176];
  uint32_t bucket_layout,batch_rounds,trim_round,seed_threads;
  BucketParameters params; bool ready, prefetched;
  uint64_t prefetched_seed[4]; // Reuse speculative work only when the next acquired seed matches.
} C29BucketWorker;
static C29BucketWorker c29_bucket_full;
// Buffers and pipelines persist across pool graphs, avoiding multi-gigabyte allocation per nonce.
static void c29_bucket_destroy(VkDevice device, C29BucketWorker *w) {
  if(w->queries) vkDestroyQueryPool(device,w->queries,NULL);
  if(w->fence) vkDestroyFence(device,w->fence,NULL);
  if(w->commands) vkDestroyCommandPool(device,w->commands,NULL);
  if(w->descriptors) vkDestroyDescriptorPool(device,w->descriptors,NULL);
  for(unsigned i=0;i<6;i++) if(w->pipelines[i]) vkDestroyPipeline(device,w->pipelines[i],NULL);
  if(w->layout) vkDestroyPipelineLayout(device,w->layout,NULL);
  if(w->set_layout) vkDestroyDescriptorSetLayout(device,w->set_layout,NULL);
  if(w->module) vkDestroyShaderModule(device,w->module,NULL);
  for(unsigned i=0;i<4;i++) {
    if(w->maps[i]) vkUnmapMemory(device,w->memories[i]);
    if(w->buffers[i]) vkDestroyBuffer(device,w->buffers[i],NULL);
    if(w->memories[i]) vkFreeMemory(device,w->memories[i],NULL);
  }
  memset(w,0,sizeof(*w));
}
static int c29_bucket_init(VkDevice device,uint32_t family,
    const VkPhysicalDeviceMemoryProperties *memory,C29BucketWorker *w,bool full) {
  if(w->ready) return 0;
  w->bucket_layout=c29_bucket_layout;w->batch_rounds=c29_bucket_batch_rounds;
  w->seed_threads=c29_seed_threads;
  w->params=(BucketParameters){.edge_count=full ? 1u<<29 : 16385,
    .node_mask=full ? (1u<<29)-1 : 4095,.bucket_bits=full ? 12 : 5,
    .seed_cap=full ? 136215 : 2048,.main_cap=full ? 84992 : 2048,
    .split=full ? 3848 : 16,.tail_cap=full ? 1u<<20 : 16385};
  BucketParameters *p=&w->params;
  uint32_t n=1u<<p->bucket_bits;
  // Add a full offset span to the stride; every staggered bucket retains its old capacity.
  if(w->bucket_layout&4) p->main_cap+=(n<256 ? n : 256)*c29_stagger_step;
  VkDeviceSize a1=(VkDeviceSize)p->split*p->seed_cap;
  if(a1<(VkDeviceSize)n*p->main_cap) a1=(VkDeviceSize)n*p->main_cap;
  VkDeviceSize sizes[4]={a1*8,(VkDeviceSize)(n-p->split)*p->seed_cap*8,
    (VkDeviceSize)n*p->main_cap*8,(8208+(VkDeviceSize)p->tail_cap*2+8192*4)*4};
  VkDescriptorBufferInfo infos[4];
  for(unsigned b=0;b<4;b++) {
    VkBufferCreateInfo bi={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=sizes[b],
      .usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT};
    CHECK(vkCreateBuffer(device,&bi,NULL,&w->buffers[b]));
    VkMemoryRequirements req; vkGetBufferMemoryRequirements(device,w->buffers[b],&req);
    uint32_t type=UINT32_MAX;
    for(unsigned pass=0;pass<2 && type==UINT32_MAX;pass++) for(unsigned i=0;i<memory->memoryTypeCount;i++) {
      VkMemoryPropertyFlags flags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
      if(!pass) flags|=VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
      if((req.memoryTypeBits&(1u<<i)) && (memory->memoryTypes[i].propertyFlags&flags)==flags) {type=i;break;}
    }
    if(type==UINT32_MAX) goto cleanup;
    VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=type};
    CHECK(vkAllocateMemory(device,&ai,NULL,&w->memories[b]));
    CHECK(vkBindBufferMemory(device,w->buffers[b],w->memories[b],0));
    CHECK(vkMapMemory(device,w->memories[b],0,sizes[b],0,(void**)&w->maps[b]));
    infos[b]=(VkDescriptorBufferInfo){.buffer=w->buffers[b],.range=sizes[b]};
    if(full) c29_report("bucket memory: buffer=%u bytes=%" PRIu64 " type=%u\n",b,(uint64_t)sizes[b],type);
  }
  VkShaderModuleCreateInfo si={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=sizeof(bucket_shader),.pCode=bucket_shader};
  CHECK(vkCreateShaderModule(device,&si,NULL,&w->module));
  VkDescriptorSetLayoutBinding bindings[4];
  for(unsigned i=0;i<4;i++) bindings[i]=(VkDescriptorSetLayoutBinding){.binding=i,
    .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT};
  VkDescriptorSetLayoutCreateInfo di={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=4,.pBindings=bindings};
  CHECK(vkCreateDescriptorSetLayout(device,&di,NULL,&w->set_layout));
  VkPushConstantRange range={.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT,.size=sizeof(*p)};
  VkPipelineLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    .setLayoutCount=1,.pSetLayouts=&w->set_layout,.pushConstantRangeCount=1,.pPushConstantRanges=&range};
  CHECK(vkCreatePipelineLayout(device,&li,NULL,&w->layout));
  // Recovery retains 256 threads; seed and redistribution widths are tuned independently.
  VkSpecializationMapEntry entries[7];
  for(unsigned i=0;i<7;i++) entries[i]=(VkSpecializationMapEntry){.constantID=i,.offset=i*4,.size=4};
  for(uint32_t i=0;i<6;i++) {
    uint32_t threads=i==0 ? w->seed_threads : i==4 ? c29_redistribute_threads :
      i==5 ? c29_late_threads : i==1 ? c29_bucket_trim_threads : 256;
    uint32_t values[7]={i==5 ? 1 : i,threads,c29_bucket_layout,c29_bucket_batch_rounds ? VK_TRUE : VK_FALSE,c29_bucket_seed_mode,c29_stagger_step,c29_output_cache};
    VkSpecializationInfo spec={.mapEntryCount=7,.pMapEntries=entries,.dataSize=sizeof(values),.pData=values};
    VkComputePipelineCreateInfo pi={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,
        .module=w->module,.pName="main",.pSpecializationInfo=&spec},.layout=w->layout};
    CHECK(vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&pi,NULL,&w->pipelines[i]));
  }
  VkDescriptorPoolSize poolsize={.type=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.descriptorCount=4};
  VkDescriptorPoolCreateInfo dpi={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,.poolSizeCount=1,.pPoolSizes=&poolsize};
  CHECK(vkCreateDescriptorPool(device,&dpi,NULL,&w->descriptors));
  VkDescriptorSetAllocateInfo da={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
    .descriptorPool=w->descriptors,.descriptorSetCount=1,.pSetLayouts=&w->set_layout};
  CHECK(vkAllocateDescriptorSets(device,&da,&w->set));
  VkWriteDescriptorSet writes[4];
  for(unsigned i=0;i<4;i++) writes[i]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
    .dstSet=w->set,.dstBinding=i,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&infos[i]};
  vkUpdateDescriptorSets(device,4,writes,0,NULL);
  VkCommandPoolCreateInfo ci={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    .flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,.queueFamilyIndex=family};
  CHECK(vkCreateCommandPool(device,&ci,NULL,&w->commands));
  VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .commandPool=w->commands,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
  CHECK(vkAllocateCommandBuffers(device,&ca,&w->command));
  VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  CHECK(vkCreateFence(device,&fi,NULL,&w->fence));
  if(c29_bucket_profile && c29_bucket_timestamp_bits) {
    VkQueryPoolCreateInfo qi={.sType=VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
      .queryType=VK_QUERY_TYPE_TIMESTAMP,.queryCount=4};
    CHECK(vkCreateQueryPool(device,&qi,NULL,&w->queries));
  }
  w->ready=true; return 0;
cleanup:
  c29_bucket_destroy(device,w); return 1;
}
// A fence publishes only small counters to the CPU; endpoint storage stays on the GPU.
static void c29_bucket_advance(C29BucketWorker *w);
static int c29_bucket_dispatch_n(VkDevice device,VkQueue queue,C29BucketWorker *w,unsigned operation,unsigned steps) {
  uint64_t host_start=c29_bucket_profile ? c29_bucket_now_ns() : 0;
  CHECK(vkResetCommandBuffer(w->command,0));
  CHECK(vkResetFences(device,1,&w->fence));
  VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  CHECK(vkBeginCommandBuffer(w->command,&begin));
  if(w->queries) {
    vkCmdResetQueryPool(w->command,w->queries,0,4);
    vkCmdWriteTimestamp(w->command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,w->queries,0);
  }
  bool seed_sequence=operation==0 && steps==3;
  for(unsigned step=0;step<steps;step++) {
  if(seed_sequence) {
    operation=step==0 ? 0 : 4;
    w->params.source=step==1 ? (1u<<(w->params.bucket_bits<6 ? w->params.bucket_bits : 6))/2 : 0;
  }
  if(operation==0) vkCmdFillBuffer(w->command,w->buffers[3],0,8208*4,0);
  if(operation==1) vkCmdFillBuffer(w->command,w->buffers[3],(w->params.source&3)==2 ? 0 : 4096*4,4096*4,0);
  if(operation==2) vkCmdFillBuffer(w->command,w->buffers[3],8193*4,4,0);
  VkMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
    .srcAccessMask=VK_ACCESS_HOST_WRITE_BIT|VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT,
    .dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT};
  vkCmdPipelineBarrier(w->command,VK_PIPELINE_STAGE_HOST_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&barrier,0,NULL,0,NULL);
  vkCmdBindPipeline(w->command,VK_PIPELINE_BIND_POINT_COMPUTE,w->pipelines[operation==1 && w->trim_round>=c29_late_start ? 5 : operation]);
  vkCmdBindDescriptorSets(w->command,VK_PIPELINE_BIND_POINT_COMPUTE,w->layout,0,1,&w->set,0,NULL);
  vkCmdPushConstants(w->command,w->layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(w->params),&w->params);
  if(w->queries && !step) vkCmdWriteTimestamp(w->command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,w->queries,1);
  if(operation==0 || operation==3) {
    // Each invocation owns one 64-edge block, including when seed width changes.
    uint32_t span=(operation==0 ? w->seed_threads : 256)*64;
    uint32_t groups=(w->params.edge_count+span-1)/span;
    vkCmdDispatch(w->command,groups>32768 ? 32768 : groups ? groups : 1,(groups+32767)/32768 ? (groups+32767)/32768 : 1,1);
  } else if(operation==4) vkCmdDispatch(w->command,(1u<<(w->params.bucket_bits<6 ? w->params.bucket_bits : 6))*16,1,1);
  else vkCmdDispatch(w->command,1u<<w->params.bucket_bits,1,1);
  if(step+1<steps) {
    // Publish counters before transfer clears and the next trim's shader reads.
    VkMemoryBarrier next={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,
      .dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT};
    vkCmdPipelineBarrier(w->command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&next,0,NULL,0,NULL);
    if(operation==1) c29_bucket_advance(w);
  }
  }
  if(w->queries) vkCmdWriteTimestamp(w->command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,w->queries,2);
  if(operation==4 && w->params.source==0) {
    // Source and destination counter planes do not overlap; publish the final fine-bucket counts.
    VkMemoryBarrier copy={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_SHADER_READ_BIT,
      .dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT};
    vkCmdPipelineBarrier(w->command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&copy,0,NULL,0,NULL);
    VkBufferCopy region={.srcOffset=4096*4,.dstOffset=0,.size=(1u<<w->params.bucket_bits)*4};
    vkCmdCopyBuffer(w->command,w->buffers[3],w->buffers[3],1,&region);
  }
  VkMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT;
  barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
  vkCmdPipelineBarrier(w->command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,NULL,0,NULL);
  if(w->queries) vkCmdWriteTimestamp(w->command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,w->queries,3);
  CHECK(vkEndCommandBuffer(w->command));
  VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&w->command};
  CHECK(vkQueueSubmit(queue,1,&submit,w->fence));
  uint64_t wait_start=c29_bucket_profile ? c29_bucket_now_ns() : 0;
  VkResult wait=vkWaitForFences(device,1,&w->fence,VK_TRUE,UINT64_C(60000000000));
  if(wait!=VK_SUCCESS) {c29_report("bucket fence failed: %d\n",wait);return -1;}
  uint64_t host_end=c29_bucket_profile ? c29_bucket_now_ns() : 0;
  if(c29_bucket_profile && w->params.node_mask==(1u<<29)-1) {
    double kernel_us=-1, gpu_us=-1;
    if(w->queries) {
      uint64_t ticks[4];
      CHECK(vkGetQueryPoolResults(device,w->queries,0,4,sizeof(ticks),ticks,sizeof(uint64_t),VK_QUERY_RESULT_64_BIT));
      uint64_t mask=c29_bucket_timestamp_bits==64 ? UINT64_MAX : (UINT64_C(1)<<c29_bucket_timestamp_bits)-1;
      kernel_us=((ticks[2]-ticks[1])&mask)*c29_bucket_timestamp_period/1000.0;
      gpu_us=((ticks[3]-ticks[0])&mask)*c29_bucket_timestamp_period/1000.0;
    }
    // Buffer samples; synchronous kernel-log writes between rounds distort graph wall time.
    if(w->profile_count>=176) goto cleanup;
    unsigned sample=w->profile_count++;
    w->profile[sample].op=operation==0 && w->params.source==3 ? 4 : operation==1 && (w->params.source&16) ? 5 : operation==4 ? 6 : operation;
    w->profile[sample].round=w->profile_round;
    w->profile[sample].kernel_us=kernel_us;w->profile[sample].gpu_us=gpu_us;
    w->profile[sample].host_us=(host_end-host_start)/1000.0;
    w->profile[sample].wait_us=(host_end-wait_start)/1000.0;
  }
  if(w->maps[3][8192]) {c29_report("bucket overflow flags=%u\n",w->maps[3][8192]);return 3;}
  return 0;
cleanup: return 1;
}
// Offline/profile runs keep one round per submission for exact count and timestamp comparisons.
static int c29_bucket_dispatch(VkDevice device,VkQueue queue,C29BucketWorker *w,unsigned operation) {
  return c29_bucket_dispatch_n(device,queue,w,operation,1);
}
// Coarse seeding borrows existing buffers; high-first redistribution prevents source/destination aliasing.
static int c29_bucket_seed(VkDevice device,VkQueue queue,C29BucketWorker *w) {
  w->params.source=0;
  if(c29_seed_batch && c29_bucket_seed_mode>=2 && !c29_bucket_profile)
    return c29_bucket_dispatch_n(device,queue,w,0,3);
  int status=c29_bucket_dispatch(device,queue,w,0); if(status || c29_bucket_seed_mode<2) return status;
  w->params.source=(1u<<(w->params.bucket_bits<6 ? w->params.bucket_bits : 6))/2;
  status=c29_bucket_dispatch(device,queue,w,4); if(status) return status;
  w->params.source=0;
  return c29_bucket_dispatch(device,queue,w,4);
}
// Emit after the timed graph, preserving sample order without introducing per-round log stalls.
static void c29_bucket_profile_flush(C29BucketWorker *w) {
  if(!c29_bucket_profile || !w->profile_count) return;
  c29_report("bucket profile init_us=%.3f\n",w->profile_init_us);
  for(unsigned i=0;i<w->profile_count;i++)
    c29_report("bucket profile: op=%u round=%u kernel_us=%.3f gpu_us=%.3f host_us=%.3f wait_us=%.3f\n",
      w->profile[i].op,w->profile[i].round,w->profile[i].kernel_us,w->profile[i].gpu_us,
      w->profile[i].host_us,w->profile[i].wait_us);
  w->profile_count=0;
}
static uint32_t c29_bucket_hash(uint32_t u,uint32_t v) {
  return ((u*0x9e3779b9u)^(v*0x85ebca6bu))&8191u;
}
static int c29_bucket_compare_u64(const void *a,const void *b) {
  uint64_t x=*(const uint64_t*)a,y=*(const uint64_t*)b; return (x>y)-(x<y);
}
static int c29_bucket_compare_u32(const void *a,const void *b) {
  uint32_t x=*(const uint32_t*)a,y=*(const uint32_t*)b; return (x>y)-(x<y);
}
// Recover original nonces only for actual cycles and independently verify every proof.
typedef struct {
  VkDevice device; VkQueue queue; C29BucketWorker *worker; C29SolveContext *context; int status;
} C29Prefetch;
static void c29_bucket_prefetch(void *opaque) {
  C29Prefetch *p=opaque; C29BucketWorker *w=p->worker;
  if(c29_solve_cancelled(p->context)) return;
  // Peek the next nonce using a private manager copy; the real lease stays active for proof submission.
  MomManager peek=p->context->manager; peek.busy=false;
  MomWork next;
  if(!mom_manager_acquire(&peek,&next)) return;
  for(unsigned i=0;i<4;i++) {w->params.seed[i][0]=(uint32_t)next.seed[i];w->params.seed[i][1]=(uint32_t)(next.seed[i]>>32);}
  w->params.source=0;
  uint64_t start=mom_pool_now_ms();
  p->status=c29_bucket_seed(p->device,p->queue,w);
  p->context->prefetch_ms=mom_pool_now_ms()-start;
  if(!p->status) {w->prefetched=true;memcpy(w->prefetched_seed,next.seed,sizeof(next.seed));}
}
static int c29_bucket_finish(VkDevice device,VkQueue queue,C29BucketWorker *w,C29SolveContext *context) {
  uint32_t cycles[64*42*2]; size_t count=0;
  unsigned length=context->work.job.proof_size;
  uint32_t *m=w->maps[3],base=8208+2*w->params.tail_cap;
  uint32_t survivors=m[8193],seed[4][2]; memcpy(seed,w->params.seed,sizeof(seed));
  C29Prefetch prefetch={device,queue,w,context,0};
  if(mom_c29_find_endpoint_cycles_overlap(m+8208,survivors,length,cycles,64,&count,
      context->pool && context->prefetch && !c29_bucket_profile ? c29_bucket_prefetch : NULL,&prefetch)) return 1;
  if(prefetch.status) return prefetch.status;
  // Process replacements received while the seed fence and CPU search were in flight.
  if(c29_solve_cancelled(context)) return 2;
  if(count) {
    // Recovery owns a disjoint table; the experiment preserves the next seed and its counters.
    if(!c29_keep_prefetch) w->prefetched=false;
    memcpy(w->params.seed,seed,sizeof(seed));
    for(unsigned i=0;i<8192;i++) {m[base+3*i+2]=UINT32_MAX;m[base+8192*3+i]=1u<<29;}
    for(size_t i=0;i<count*length;i++) {
      uint32_t u=cycles[i*2],v=cycles[i*2+1],slot=c29_bucket_hash(u,v);
      while(m[base+slot*3+2]!=UINT32_MAX && (m[base+slot*3]!=u || m[base+slot*3+1]!=v)) slot=(slot+1)&8191;
      m[base+slot*3]=u;m[base+slot*3+1]=v;m[base+slot*3+2]=slot;
    }
    int status=c29_bucket_dispatch(device,queue,w,3); if(status) return status;
    for(size_t c=0;c<count;c++) {
      if(c29_solve_cancelled(context)) return 2;
      uint32_t nonces[42]; uint8_t hash[32];
      for(unsigned i=0;i<length;i++) {
        uint32_t u=cycles[(c*length+i)*2],v=cycles[(c*length+i)*2+1],slot=c29_bucket_hash(u,v);
        while(m[base+slot*3]!=u || m[base+slot*3+1]!=v) slot=(slot+1)&8191;
        nonces[i]=m[base+8192*3+slot];
      }
      qsort(nonces,length,4,c29_bucket_compare_u32);
      if(!mom_c29_verify(context->work.seed,nonces,length,hash)) return 1;
      if(!c29_solve_proof(context,nonces,length,hash)) return 2;
    }
  }
  c29_report("bucket cycle search: survivors=%u cycles=%zu verified=%zu\n",survivors,count,count);
  bool passed=context->pool || (count && context->matched);
  passed=mom_manager_complete(&context->manager,&context->work,passed) && passed;
  if(!context->pool) {
    mom_manager_stop(&context->manager);
    if(passed) c29_report("PASS c29 solve: MOM reference hash matched, proofs independently verified\n");
  }
  return passed ? 0 : 1;
}
// Track staggered output from the first trim too when padded layout bit 2 is selected.
static void c29_bucket_advance(C29BucketWorker *w) {
  uint32_t old=w->params.source&3;
  w->params.source=(old==2 ? 1 : 2)|((w->bucket_layout&1) && (old!=0 || (w->bucket_layout&4)) ? 4 : 0);
}
static int c29_bucket_run(VkDevice device,VkQueue queue,uint32_t family,
    const VkPhysicalDeviceMemoryProperties *memory,uint32_t rounds,C29SolveContext *context) {
  uint64_t start=mom_pool_now_ms();
  C29BucketWorker *w=&c29_bucket_full;
  uint64_t init_start=c29_bucket_profile ? c29_bucket_now_ns() : 0;
  int status=c29_bucket_init(device,family,memory,w,true); if(status) return status;
  w->profile_count=0;
  if(c29_bucket_profile) w->profile_init_us=(c29_bucket_now_ns()-init_start)/1000.0;
  w->profile_round=0;
  for(unsigned i=0;i<4;i++) {w->params.seed[i][0]=(uint32_t)context->work.seed[i];w->params.seed[i][1]=(uint32_t)(context->work.seed[i]>>32);}
  // Ablations run only for an explicitly profiled offline fixture, never real pool work.
  if(c29_bucket_ablate && !context->pool) {
    w->params.source=3;
    for(unsigned repeat=0;repeat<3;repeat++) {
      status=c29_bucket_dispatch(device,queue,w,0); if(status) return status;
    }
  }
  w->params.source=0;
  bool reuse=w->prefetched && !memcmp(w->prefetched_seed,context->work.seed,sizeof(w->prefetched_seed));
  context->seed_reused=reuse;
  w->prefetched=false;
  if(!reuse) {status=c29_bucket_seed(device,queue,w); if(status) return status;}
  if(c29_bucket_ablate && !context->pool) {
    w->params.source=16;
    for(unsigned repeat=0;repeat<3;repeat++) {
      status=c29_bucket_dispatch(device,queue,w,1); if(status) return status;
    }
    w->params.source=0;
  }
  // Fault injection exercises a completed but unusable graph without touching GPU ownership.
  if(context->pool && c29_inject_overflow) {--c29_inject_overflow;return 3;}
  for(unsigned r=0;r<rounds;) {
    if(c29_solve_cancelled(context)) return 2;
    w->profile_round=r+1;w->trim_round=r;
    if(r<w->batch_rounds) w->params.source|=8;
    // Eight-round batches bound cancellation latency while avoiding 160 host fence round trips.
    unsigned steps=context->pool && !c29_bucket_profile && !w->batch_rounds ? 8 : 1;
    if(steps>rounds-r) steps=rounds-r;
    if(r<c29_late_start && steps>c29_late_start-r) steps=c29_late_start-r;
    status=c29_bucket_dispatch_n(device,queue,w,1,steps); if(status) return status;
    c29_bucket_advance(w);
    r+=steps;
    // Live mining checks overflow/cancellation at batch boundaries; exhaustive counts stay diagnostic-only.
    if(!context->pool || c29_bucket_profile) {
      uint64_t survivors=0; uint32_t base=(w->params.source&3)==2 ? 4096 : 0;
      for(unsigned b=0;b<4096;b++) survivors+=w->maps[3][base+b];
      c29_report("full graph round=%u survivors=%" PRIu64 "\n",r,survivors);
    }
  }
  status=c29_bucket_dispatch(device,queue,w,2); if(status) return status;
  // Odd trim counts swap the bipartite sides; normalize before the forest and recovery lookup.
  if(rounds&1) for(unsigned i=0;i<w->maps[3][8193];i++) {
    uint32_t *edge=w->maps[3]+8208+2*i,t=edge[0];edge[0]=edge[1];edge[1]=t;
  }
  context->trim_ms=mom_pool_now_ms()-start;
  c29_report("PASS c29 bucket trim: %u rounds, %u survivors\n",rounds,w->maps[3][8193]);
  start=mom_pool_now_ms(); status=c29_bucket_finish(device,queue,w,context);
  context->recovery_ms=mom_pool_now_ms()-start;
  if(!context->pool) c29_bucket_profile_flush(w);
  return status;
}
// Small graphs compare exact endpoint multisets after every round, including dense collisions.
static int c29_bucket_selftest(VkDevice device,VkQueue queue,uint32_t family,
    const VkPhysicalDeviceMemoryProperties *memory) {
  C29BucketWorker w={0}; int status=c29_bucket_init(device,family,memory,&w,false);
  if(status) return status;
  uint64_t expected[16385],actual[16385]; uint32_t degrees[4096];
  const uint32_t counts[]={0,1,63,64,65,4097,16385};
  unsigned cases=sizeof(counts)/sizeof(counts[0]);
  for(unsigned test=0;test<cases*2;test++) {
    uint64_t seed[4]={UINT64_C(0x0123456789abcdef),UINT64_MAX,1,test};
    for(unsigned i=0;i<4;i++) {w.params.seed[i][0]=(uint32_t)seed[i];w.params.seed[i][1]=(uint32_t)(seed[i]>>32);}
    w.params.edge_count=counts[test%cases];w.params.source=0;
    uint32_t count=counts[test%cases];
    for(unsigned b=0;b<(count+63)/64;b++) {
      uint32_t edges[64][2];c29_reference_block(seed,b*64,edges);
      for(unsigned i=0;i<64 && b*64+i<count;i++) expected[b*64+i]=
        (uint64_t)(edges[i][0]&4095)|((uint64_t)(edges[i][1]&4095)<<32);
    }
    status=c29_bucket_seed(device,queue,&w);if(status) goto cleanup;
    for(unsigned round=0;round<=12;) {
      status=c29_bucket_dispatch(device,queue,&w,2);if(status) goto cleanup;
      if(w.maps[3][8193]!=count) {status=1;goto cleanup;}
      memcpy(actual,w.maps[3]+8208,(size_t)count*8);
      qsort(expected,count,8,c29_bucket_compare_u64);qsort(actual,count,8,c29_bucket_compare_u64);
      if(memcmp(expected,actual,(size_t)count*8)) {c29_report("bucket mismatch case=%u round=%u\n",test,round);status=1;goto cleanup;}
      if(round==12) break;
      // Exercise command batching against the independent endpoint oracle, including partial groups.
      unsigned steps=test>=cases && !w.batch_rounds ? 4 : 1;
      for(unsigned step=0;step<steps;step++) {
      memset(degrees,0,sizeof(degrees));
      for(unsigned i=0;i<count;i++) degrees[(uint32_t)expected[i]]++;
      uint32_t next=0;
      for(unsigned i=0;i<count;i++) if(degrees[(uint32_t)expected[i]]>=2)
        expected[next++]=(expected[i]>>32)|(expected[i]<<32);
      count=next;
      }
      if(round<w.batch_rounds) w.params.source|=8;
      status=c29_bucket_dispatch_n(device,queue,&w,1,steps);if(status) goto cleanup;
      c29_bucket_advance(&w);
      round+=steps;
    }
  }
  // Dense input deliberately overfills the last destination bucket, including its padding boundary.
  memset(w.maps[3],0,8208*4);w.params.source=0;
  for(unsigned bucket=0;bucket<2;bucket++) {
    w.maps[3][bucket]=w.params.seed_cap;
    for(unsigned slot=0;slot<w.params.seed_cap;slot++) {
      size_t index=((size_t)bucket*w.params.seed_cap+slot)*2;
      w.maps[0][index]=bucket;w.maps[0][index+1]=31;
    }
  }
  status=c29_bucket_dispatch(device,queue,&w,1);
  if(status!=3 || !(w.maps[3][8192]&1)) {
    c29_report("bucket overflow boundary not detected\n");status=1;goto cleanup;
  }
  status=0;
  c29_report("PASS c29 bucket differential: 14 graphs, 168 rounds, exact endpoint multisets\n");
cleanup:
  if(status>=0) c29_bucket_destroy(device,&w);
  return status;
}
#endif
