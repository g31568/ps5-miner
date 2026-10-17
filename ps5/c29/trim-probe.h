// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MOM_C29_TRIM_PROBE_H
#define MOM_C29_TRIM_PROBE_H

static const uint32_t trim_shader[] =
#include "trim.spv.inc"
;
typedef struct {
  uint32_t seed[4][2], first_nonce, edge_count, node_mask, phase;
} TrimParameters;
_Static_assert(sizeof(TrimParameters) == 48, "Trimming push constant ABI");

// Compare every GPU round with an independent scalar degree-counting oracle.
static int c29_trim_probe(VkDevice device, VkQueue queue, uint32_t queue_family,
                          const VkPhysicalDeviceMemoryProperties *memory_properties,
                          uint32_t full_rounds, C29SolveContext *solve) {
  uint64_t timing_start = mom_pool_now_ms();
  enum { LIMIT = 16385, NODE_LIMIT = 4096, ROUNDS = 12, GUARD = 16 };
  const uint32_t alive_capacity = full_rounds ? (1u << 24) : (LIMIT + 31) / 32;
  const uint32_t capacity = alive_capacity + 2 * (full_rounds ? (1u << 24) : NODE_LIMIT / 32) + 4 * GUARD;
  // Two bounded 800 MiB caches permit early switching and GPU survivor compaction.
  const uint32_t cache_blocks = full_rounds ? (1u << 23) : (LIMIT + 63) / 64;
  const uint32_t cache_limit = full_rounds ? (1u << 26) : LIMIT;
  const VkDeviceSize cache_bytes = ((VkDeviceSize)cache_blocks + 1 + (VkDeviceSize)cache_limit * 3) * 4;
  VkBuffer cache_buffers[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
  VkDeviceMemory cache_memories[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
  uint32_t *cache_maps[2] = {NULL, NULL};
  VkBuffer buffer = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkShaderModule module = VK_NULL_HANDLE;
  VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
  VkPipelineLayout layout = VK_NULL_HANDLE;
  VkPipeline pipeline = VK_NULL_HANDLE, cached_pipeline = VK_NULL_HANDLE;
  VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
  VkCommandPool command_pool = VK_NULL_HANDLE;
  VkFence fence = VK_NULL_HANDLE;
  uint32_t *mapped = NULL;
  uint32_t (*edges)[2] = NULL, *degrees = NULL, *expected = NULL;
  int status = 1;
  // Full runs retain only a previous-alive bitmap on the CPU, never a 4 GiB edge array.
  if (!full_rounds) {
    edges = malloc(LIMIT * sizeof(*edges));
    degrees = calloc(NODE_LIMIT, sizeof(*degrees));
  }
  expected = malloc(alive_capacity * sizeof(*expected));
  if ((!full_rounds && (!edges || !degrees)) || !expected) goto cleanup;
  VkBufferCreateInfo buffer_info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    .size = capacity * sizeof(uint32_t), .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
  CHECK(vkCreateBuffer(device, &buffer_info, NULL, &buffer));
  VkMemoryRequirements requirements;
  vkGetBufferMemoryRequirements(device, buffer, &requirements);
  uint32_t memory_type = UINT32_MAX;
  // Prefer coherent device-local memory; PS5 GTT allocations otherwise incur a full CPU zero/flush.
  for (unsigned pass = 0; pass < 2 && memory_type == UINT32_MAX; ++pass) {
    for (uint32_t i = 0; i < memory_properties->memoryTypeCount; ++i) {
    VkMemoryPropertyFlags flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    if (pass == 0) flags |= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    if ((requirements.memoryTypeBits & (1u << i)) &&
        (memory_properties->memoryTypes[i].propertyFlags & flags) == flags) { memory_type = i; break; }
    }
  }
  if (memory_type == UINT32_MAX) goto cleanup;
  if (full_rounds) c29_report("trim memory: type=%u heap=%u flags=%u cache_bytes=%" PRIu64 "\n",
    memory_type, memory_properties->memoryTypes[memory_type].heapIndex,
    memory_properties->memoryTypes[memory_type].propertyFlags, (uint64_t)cache_bytes * 2);
  VkMemoryAllocateInfo allocation = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .allocationSize = requirements.size, .memoryTypeIndex = memory_type};
  CHECK(vkAllocateMemory(device, &allocation, NULL, &memory));
  CHECK(vkBindBufferMemory(device, buffer, memory, 0));
  CHECK(vkMapMemory(device, memory, 0, buffer_info.size, 0, (void **)&mapped));
  // Prefixes are host-written only after fences; endpoints are generated on the GPU.
  buffer_info.size = cache_bytes;
  for (unsigned i = 0; i < 2; ++i) {
    CHECK(vkCreateBuffer(device, &buffer_info, NULL, &cache_buffers[i]));
    vkGetBufferMemoryRequirements(device, cache_buffers[i], &requirements);
    if (!(requirements.memoryTypeBits & (1u << memory_type))) goto cleanup;
    allocation.allocationSize = requirements.size;
    CHECK(vkAllocateMemory(device, &allocation, NULL, &cache_memories[i]));
    CHECK(vkBindBufferMemory(device, cache_buffers[i], cache_memories[i], 0));
    CHECK(vkMapMemory(device, cache_memories[i], 0, cache_bytes, 0, (void **)&cache_maps[i]));
  }
  VkShaderModuleCreateInfo shader_info = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    .codeSize = sizeof(trim_shader), .pCode = trim_shader};
  CHECK(vkCreateShaderModule(device, &shader_info, NULL, &module));
  VkDescriptorSetLayoutBinding bindings[3] = {
    {.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
     .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT},
    {.binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
     .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT},
    {.binding = 2, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
     .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT}};
  VkDescriptorSetLayoutCreateInfo set_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    .bindingCount = 3, .pBindings = bindings};
  CHECK(vkCreateDescriptorSetLayout(device, &set_info, NULL, &set_layout));
  VkPushConstantRange range = {.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .size = sizeof(TrimParameters)};
  VkPipelineLayoutCreateInfo layout_info = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    .setLayoutCount = 1, .pSetLayouts = &set_layout, .pushConstantRangeCount = 1, .pPushConstantRanges = &range};
  CHECK(vkCreatePipelineLayout(device, &layout_info, NULL, &layout));
  VkComputePipelineCreateInfo pipeline_info = {.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
    .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = module, .pName = "main"}, .layout = layout};
  CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &pipeline));
  // Compile cached trimming independently so the dense SipHash path cannot reserve its registers.
  VkBool32 cached_specialization = VK_TRUE;
  VkSpecializationMapEntry specialization_entry = {.constantID = 0, .offset = 0, .size = sizeof(VkBool32)};
  VkSpecializationInfo specialization = {.mapEntryCount = 1, .pMapEntries = &specialization_entry,
    .dataSize = sizeof(cached_specialization), .pData = &cached_specialization};
  pipeline_info.stage.pSpecializationInfo = &specialization;
  CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &cached_pipeline));
  VkDescriptorPoolSize pool_size = {.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 6};
  VkDescriptorPoolCreateInfo pool_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
    .maxSets = 2, .poolSizeCount = 1, .pPoolSizes = &pool_size};
  CHECK(vkCreateDescriptorPool(device, &pool_info, NULL, &descriptor_pool));
  VkDescriptorSetLayout set_layouts[2] = {set_layout, set_layout};
  VkDescriptorSetAllocateInfo set_allocation = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
    .descriptorPool = descriptor_pool, .descriptorSetCount = 2, .pSetLayouts = set_layouts};
  VkDescriptorSet descriptor_sets[2];
  CHECK(vkAllocateDescriptorSets(device, &set_allocation, descriptor_sets));
  // Swapping descriptor sets changes source/destination without rewriting in-flight descriptors.
  for (unsigned i = 0; i < 2; ++i) {
    VkDescriptorBufferInfo buffers[3] = {
      {.buffer = buffer, .range = capacity * sizeof(uint32_t)},
      {.buffer = cache_buffers[i], .range = cache_bytes},
      {.buffer = cache_buffers[1-i], .range = cache_bytes}};
    VkWriteDescriptorSet writes[3];
    for (unsigned b = 0; b < 3; ++b) writes[b] = (VkWriteDescriptorSet){
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = descriptor_sets[i], .dstBinding = b,
      .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &buffers[b]};
    vkUpdateDescriptorSets(device, 3, writes, 0, NULL);
  }
  VkCommandPoolCreateInfo pool = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = queue_family};
  CHECK(vkCreateCommandPool(device, &pool, NULL, &command_pool));
  VkCommandBufferAllocateInfo command_allocation = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .commandPool = command_pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
  VkCommandBuffer command;
  CHECK(vkAllocateCommandBuffers(device, &command_allocation, &command));
  VkFenceCreateInfo fence_info = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  CHECK(vkCreateFence(device, &fence_info, NULL, &fence));
  const uint64_t seeds[][4] = {{0, 0, 0, 0}, {UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX},
    {UINT64_C(0x0123456789abcdef), UINT64_C(0xfedcba9876543210), 1, UINT64_C(0x8000000000000000)}};
  // Cover partial words/blocks, idle invocations, dense atomic contention and sparse graphs.
  const uint32_t cases[][4] = {{0,0,31,0}, {0,1,31,0}, {0,63,31,0}, {0,64,31,0},
    {0,65,31,0}, {64,4097,4095,0}, {0,16385,4095,0}, {(1u<<29)-64,64,31,0},
    {0,8193,255,1}, {0,8193,4095,2}, {0,8193,4095,3}};
  uint32_t checked_rounds = 0;
  uint64_t survivors = 0, gpu_wait_ms = 0, validation_ms = 0;
  uint64_t regeneration_gpu_ms = 0, cached_gpu_ms = 0;
  for (unsigned s = 0; s < (full_rounds ? 1 : sizeof(seeds)/sizeof(seeds[0])); ++s) {
    for (unsigned t = 0; t < (full_rounds ? 1 : sizeof(cases)/sizeof(cases[0])); ++t) {
      TrimParameters params = {.first_nonce = cases[t][0], .edge_count = cases[t][1], .node_mask = cases[t][2]};
      if (full_rounds) {
        params.edge_count = 1u << 29;
        params.node_mask = (1u << 29) - 1;
      }
      uint32_t words = (params.edge_count + 31) / 32, node_count = params.node_mask + 1;
      uint32_t degree_words = node_count / 32, seen_start = GUARD + words + GUARD;
      uint32_t twice_start = seen_start + degree_words + GUARD;
      if ((params.first_nonce & 63) || (!full_rounds && (params.edge_count > LIMIT || node_count > NODE_LIMIT)) ||
          node_count < 32 || (node_count & (node_count-1)) || params.first_nonce >= (1u<<29) ||
          params.edge_count > (1u<<29)-params.first_nonce) goto cleanup;
      for (unsigned k = 0; k < 4; ++k) {
        uint64_t seed = solve ? solve->work.seed[k] : seeds[s][k];
        params.seed[k][0] = (uint32_t)seed; params.seed[k][1] = (uint32_t)(seed >> 32);
      }
      for (uint32_t i = 0; i < capacity; ++i) mapped[i] = GUARD_WORD;
      memset(expected, 0, alive_capacity * sizeof(*expected));
      if (full_rounds) {
        memset(expected, 255, alive_capacity * sizeof(*expected));
        c29_report("full graph start: edges=%u bitmap_bytes=%u rounds=%u\n",
          params.edge_count, capacity*4, full_rounds);
      }
      for (uint32_t base = 0; !full_rounds && base < params.edge_count; base += 64) {
        uint32_t block[64][2]; c29_reference_block(seeds[s], params.first_nonce+base, block);
        for (uint32_t i = 0; i < 64 && base+i < params.edge_count; ++i) {
          edges[base+i][0] = block[i][0] & params.node_mask;
          edges[base+i][1] = block[i][1] & params.node_mask;
          // Seed sparse, alternating and empty alive sets independently of edge generation.
          if (cases[t][3] == 3 || (cases[t][3] == 1 && (base+i)%3) ||
              (cases[t][3] == 2 && (base+i)%2)) continue;
          expected[(base+i)/32] |= 1u << ((base+i)&31);
        }
      }
      memcpy(mapped+GUARD, expected, words*sizeof(*expected));
      uint32_t cached_edges = 0, cache_index = 0;
      bool cached = false;
      for (uint32_t round = 0; round < (full_rounds ? full_rounds : ROUNDS); ++round) {
        // The previous fence has drained, so a replacement pool job can release this graph safely.
        if (solve && solve->pool && c29_solve_cancelled(solve)) { status = 2; goto cleanup; }
        // Small differential cases switch early so all seeds and sparse layouts exercise caching.
        bool build_cache = !cached && round >= (full_rounds ? 4u : 2u);
        uint32_t blocks = (params.edge_count + 63) / 64;
        if (build_cache) {
          uint64_t count = 0;
          for (uint32_t block = 0; block < blocks; ++block) {
            cache_maps[cache_index][block] = (uint32_t)count;
            count += (uint32_t)__builtin_popcount(mapped[GUARD + block*2]);
            if (block*2 + 1 < words) count += (uint32_t)__builtin_popcount(mapped[GUARD + block*2 + 1]);
          }
          build_cache = count <= cache_limit;
          if (build_cache) {
            cached_edges = (uint32_t)count;
            cache_maps[cache_index][blocks] = cached_edges;
            c29_report("endpoint cache: round=%u edges=%u bytes=%" PRIu64 "\n",
              round+1, cached_edges, (uint64_t)cache_bytes);
          }
        }
        if (!full_rounds) memset(degrees, 0, node_count*sizeof(*degrees));
        for (uint32_t i = 0; !full_rounds && i < params.edge_count; ++i)
          if (expected[i/32] & (1u << (i&31))) ++degrees[edges[i][round&1]];
        for (uint32_t i = 0; !full_rounds && i < params.edge_count; ++i)
          if (degrees[edges[i][round&1]] < 2) expected[i/32] &= ~(1u << (i&31));
        CHECK(vkResetCommandBuffer(command, 0));
        VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
          .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
        CHECK(vkBeginCommandBuffer(command, &begin));
        // Clear on the GPU instead of writing 128 MiB from the CPU every full round.
        vkCmdFillBuffer(command, buffer, (VkDeviceSize)seen_start * 4, (VkDeviceSize)degree_words * 4, 0);
        vkCmdFillBuffer(command, buffer, (VkDeviceSize)twice_start * 4, (VkDeviceSize)degree_words * 4, 0);
        VkMemoryBarrier clear_barrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
          .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
          .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &clear_barrier, 0, NULL, 0, NULL);
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &descriptor_sets[cache_index], 0, NULL);
        // Fence completion separates host clears; the barrier separates GPU counting from pruning.
        uint32_t groups = (params.edge_count + 4095) / 4096;
        // Force row crossings and padded invocations even in these small differential cases.
        uint32_t dispatch_x = full_rounds ? 32768 : (groups > 2 ? 2 : (groups ? groups : 1));
        uint32_t dispatch_y = groups ? (groups + dispatch_x - 1) / dispatch_x : 1;
        VkMemoryBarrier cache_barrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
          .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_SHADER_READ_BIT};
        if (build_cache) {
          params.phase = 0x80000000u;
          vkCmdPushConstants(command, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(params), &params);
          vkCmdDispatch(command, dispatch_x, dispatch_y, 1);
          vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &cache_barrier, 0, NULL, 0, NULL);
          cached = true;
        }
        if (cached) {
          vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, cached_pipeline);
          groups = (cached_edges + 63) / 64;
          dispatch_x = full_rounds ? (groups > 32768 ? 32768 : (groups ? groups : 1)) :
            (groups > 2 ? 2 : (groups ? groups : 1));
          dispatch_y = groups ? (groups + dispatch_x - 1) / dispatch_x : 1;
        }
        params.phase = round*2 | (cached ? 0x40000000u : 0);
        vkCmdPushConstants(command, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(params), &params);
        vkCmdDispatch(command, dispatch_x, dispatch_y, 1);
        VkMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
          .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_SHADER_READ_BIT};
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, NULL, 0, NULL);
        ++params.phase;
        vkCmdPushConstants(command, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(params), &params);
        vkCmdDispatch(command, dispatch_x, dispatch_y, 1);
        if (cached) {
          // Prune publishes tombstones before compaction; each output record remains bounded by its input count.
          vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, NULL, 0, NULL);
          vkCmdFillBuffer(command, cache_buffers[1-cache_index], (VkDeviceSize)blocks * 4, 4, 0);
          vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &clear_barrier, 0, NULL, 0, NULL);
          params.phase = 0x20000000u;
          vkCmdPushConstants(command, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(params), &params);
          vkCmdDispatch(command, dispatch_x, dispatch_y, 1);
        }
        barrier.srcAccessMask |= VK_ACCESS_TRANSFER_WRITE_BIT;
        // Publish alive bits to both host validation and the next round's shader reads.
        barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
          VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, NULL, 0, NULL);
        CHECK(vkEndCommandBuffer(command));
        CHECK(vkResetFences(device, 1, &fence));
        VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
          .commandBufferCount = 1, .pCommandBuffers = &command};
        uint64_t gpu_start = mom_pool_now_ms();
        CHECK(vkQueueSubmit(queue, 1, &submit, fence));
        VkResult wait = vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_C(30000000000));
        if (wait != VK_SUCCESS) {
          c29_report("Trim GPU completion failed: %d; abort without freeing in-flight resources\n", (int)wait);
          return -1;
        }
        // Separate the serial SipHash-heavy start from bandwidth-bound cached rounds.
        uint64_t round_gpu_ms = mom_pool_now_ms() - gpu_start;
        gpu_wait_ms += round_gpu_ms;
        if (cached) cached_gpu_ms += round_gpu_ms;
        else regeneration_gpu_ms += round_gpu_ms;
        if (cached) {
          cache_index = 1-cache_index;
          uint32_t compacted = cache_maps[cache_index][blocks];
          if (compacted > cached_edges) { c29_report("Cache compaction count overflow\n"); goto cleanup; }
          cached_edges = compacted;
        }
        uint64_t validation_start = mom_pool_now_ms();
        // Offline probes validate every round; pool work checks checkpoints and the final bitmap.
        if (full_rounds && (!(solve && solve->pool) || (round+1)%16 == 0 || round+1 == full_rounds)) {
          uint64_t remaining = 0;
          for (uint32_t i = 0; i < words; ++i) {
            uint32_t live = mapped[GUARD+i];
            if (live & ~expected[i]) { c29_report("Full trim resurrected an edge\n"); goto cleanup; }
            expected[i] = live;
            remaining += (uint32_t)__builtin_popcount(live);
          }
          for (uint32_t i = 0; i < degree_words; ++i) {
            if (mapped[twice_start+i] & ~mapped[seen_start+i]) {
              c29_report("Full trim degree planes disagree\n"); goto cleanup;
            }
          }
          const uint32_t guards[] = {0, GUARD+words, seen_start+degree_words, twice_start+degree_words};
          for (unsigned g = 0; g < sizeof(guards)/sizeof(guards[0]); ++g)
            for (uint32_t i = 0; i < GUARD; ++i)
              if (mapped[guards[g]+i] != GUARD_WORD) { c29_report("Full trim guard overwritten\n"); goto cleanup; }
          if (cached && remaining != cached_edges) {
            c29_report("Compacted cache disagrees with alive bitmap\n"); goto cleanup;
          }
          c29_report("full graph round=%u side=%c survivors=%" PRIu64 "\n", round+1, (round&1) ? 'V':'U', remaining);
          if (round+1 == full_rounds) survivors = remaining;
        }
        for (uint32_t i = 0; !full_rounds && i < capacity; ++i) {
          uint32_t value = GUARD_WORD;
          if (i >= GUARD && i < GUARD+words) value = expected[i-GUARD];
          else if ((i >= seen_start && i < seen_start+degree_words) ||
                   (i >= twice_start && i < twice_start+degree_words)) {
            int twice = i >= twice_start;
            uint32_t base = (i-(twice ? twice_start : seen_start))*32;
            value = 0;
            for (uint32_t bit = 0; bit < 32; ++bit)
              if (degrees[base+bit] >= (twice ? 2u : 1u)) value |= 1u<<bit;
          }
          if (mapped[i] != value) {
            c29_report("Trim mismatch seed=%u case=%u round=%u word=%u expected=%08x got=%08x\n",
              s,t,round,i,value,mapped[i]); goto cleanup;
          }
        }
        if (!full_rounds && cached) {
          uint32_t expected_count = 0;
          for (uint32_t i = 0; i < words; ++i) expected_count += (uint32_t)__builtin_popcount(expected[i]);
          if (cached_edges != expected_count) { c29_report("Reduced cache count mismatch\n"); goto cleanup; }
        }
        validation_ms += mom_pool_now_ms() - validation_start;
        ++checked_rounds;
      }
      for (uint32_t i = 0; !full_rounds && i < params.edge_count; ++i)
        survivors += (expected[i/32] >> (i&31)) & 1u;
      // The GPU fence has completed; the bitmap remains mapped throughout synchronous recovery.
      if (solve) {
        uint64_t recovery_start = mom_pool_now_ms();
        solve->trim_ms = recovery_start - timing_start;
        bool recovered = c29_solve_finish(solve, mapped + GUARD, words);
        solve->recovery_ms = mom_pool_now_ms() - recovery_start;
        if (!recovered) goto cleanup;
      }
    }
  }
  if (full_rounds) c29_report("trim stages: gpu_wait_ms=%" PRIu64 " validation_ms=%" PRIu64 " total_ms=%" PRIu64 "\n",
    gpu_wait_ms, validation_ms, mom_pool_now_ms() - timing_start);
  if (full_rounds) c29_report("trim GPU stages: regeneration_ms=%" PRIu64 " cached_ms=%" PRIu64 "\n",
    regeneration_gpu_ms, cached_gpu_ms);
  if (full_rounds) c29_report("PASS c29 full trim: %u rounds, %" PRIu64 " survivors, bitmap invariants and guards intact\n",
    checked_rounds, survivors);
  else c29_report("PASS c29 trim: %u rounds, %" PRIu64 " final survivors, degree planes and guards intact\n",
    checked_rounds, survivors);
  status = 0;
cleanup:
  // All submitted work is complete before releasing the trimmer's resources.
  if (fence) vkDestroyFence(device, fence, NULL);
  if (command_pool) vkDestroyCommandPool(device, command_pool, NULL);
  if (descriptor_pool) vkDestroyDescriptorPool(device, descriptor_pool, NULL);
  if (cached_pipeline) vkDestroyPipeline(device, cached_pipeline, NULL);
  if (pipeline) vkDestroyPipeline(device, pipeline, NULL);
  if (layout) vkDestroyPipelineLayout(device, layout, NULL);
  if (set_layout) vkDestroyDescriptorSetLayout(device, set_layout, NULL);
  if (module) vkDestroyShaderModule(device, module, NULL);
  for (unsigned i = 0; i < 2; ++i) {
    if (cache_maps[i]) vkUnmapMemory(device, cache_memories[i]);
    if (cache_buffers[i]) vkDestroyBuffer(device, cache_buffers[i], NULL);
    if (cache_memories[i]) vkFreeMemory(device, cache_memories[i], NULL);
  }
  if (mapped) vkUnmapMemory(device, memory);
  if (buffer) vkDestroyBuffer(device, buffer, NULL);
  if (memory) vkFreeMemory(device, memory, NULL);
  free(expected); free(degrees); free(edges);
  return status;
}
#endif
