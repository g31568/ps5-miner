// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MOM_C29_POOL_RUN_H
#define MOM_C29_POOL_RUN_H
static void c29_pool_log(const char *message) { c29_report("%s\n", message); }

// Keep sockets and job ownership on this thread; GPU stages poll network updates between fences.
static int c29_pool_run(const char *config_path, VkDevice device, VkQueue queue,
                         uint32_t family, const VkPhysicalDeviceMemoryProperties *memory) {
  MomPoolConfig config;
  if (!mom_pool_config_read(config_path, &config)) {
    c29_report("Pool configuration rejected\n");
    return 1;
  }
  C29SolveContext context = {0};
  mom_manager_init(&context.manager);
  context.pool = mom_pool_create(&config, &context.manager, c29_pool_log);
  if (!context.pool) return 1;
  uint64_t start = mom_pool_now_ms();
  bool failed = false;
  uint64_t completed_ms = 0;
  uint64_t mining_start = 0;
  uint64_t recent_ms[60]={0},attempt_ms=0;unsigned recent_count=0,overflows=0;
  c29_report("pool worker started; verified C29 only\n");
  while (!context.manager.stopped) {
#ifdef PS5_COMBINED_MINER
    // Publish pool replies even during idle/reconnect periods.
    ps5_gpu_publish(&context.manager, -1);
    if (ps5_gpu_cancelled()) { mom_manager_stop(&context.manager); break; }
#endif
    if (config.runtime_seconds && mom_pool_now_ms() - start >= (uint64_t)config.runtime_seconds * 1000) break;
    // Ready work must never wait for a quiet socket before feeding the GPU.
    bool ready = context.manager.has_job && context.manager.connected && !context.manager.paused && !context.manager.exhausted;
    mom_pool_pump(context.pool, ready && (!config.max_graphs || context.manager.stats.graphs < config.max_graphs) ? 0 : 100);
    if (config.max_graphs && context.manager.stats.graphs >= config.max_graphs) {
      if (!mom_pool_pending(context.pool)) break;
      continue;
    }
    if (!mom_manager_acquire(&context.manager, &context.work)) continue;
    if (!mining_start) mining_start = mom_pool_now_ms();
    context.prefetch = (!config.max_graphs || context.manager.stats.graphs + 1 < config.max_graphs) &&
      (!config.runtime_seconds || mom_pool_now_ms() - start + 1000 < (uint64_t)config.runtime_seconds * 1000);
    uint64_t graph_start = mom_pool_now_ms(), graphs_before = context.manager.stats.graphs;
    context.trim_ms = context.recovery_ms = context.prefetch_ms = 0;
    int result = c29_bucket_run(device, queue, family, memory, c29_trial_rounds, &context);
    if (result < 0) {
      // Device ownership is uncertain after a timeout; the process must exit without Vulkan cleanup.
      mom_pool_destroy(context.pool);
      return -1;
    }
    if(result==3) {
      c29_bucket_full.prefetched=false;
      // Capacity failure is recoverable after a completed fence; persistent failures remain bounded.
      if(c29_retry_overflow && ++overflows<=3 && mom_manager_discard(&context.manager,&context.work)) {
        attempt_ms+=mom_pool_now_ms()-graph_start;
        c29_report("capacity graph discarded; retry=%u\n",overflows);continue;
      }
      result=1;
    } else if(result==0) overflows=0;
    if (context.manager.busy) mom_manager_complete(&context.manager, &context.work, result == 0 || result == 2);
    uint64_t graph_ms = mom_pool_now_ms() - graph_start;
    bool completed = context.manager.stats.graphs > graphs_before;
    if (completed) completed_ms += graph_ms;
    attempt_ms+=graph_ms;
    if(completed) {recent_ms[recent_count++%60]=attempt_ms;attempt_ms=0;}
    // Include graph scheduling and speculative seed time, but exclude initial connection setup.
    uint64_t mining_ms = mom_pool_now_ms() - mining_start;
    if (completed && context.manager.stats.graphs >= 20 && context.manager.stats.graphs % 20 == 0 && mining_ms && !c29_bucket_profile)
    {
      // Recent graph work includes stale attempts but excludes disconnected idle time.
      uint64_t elapsed=0;unsigned samples=recent_count<60 ? recent_count : 60;
      for(unsigned i=0;i<samples;i++) elapsed+=recent_ms[i];
      double gps=c29_recent_rate && elapsed ? samples*1000.0/elapsed : context.manager.stats.graphs*1000.0/mining_ms;
      mom_pool_record_performance(context.pool,gps,context.manager.stats.graphs);
      c29_report("measured performance: graphs=%u elapsed_ms=%" PRIu64 " recent=%u\n",samples,elapsed,c29_recent_rate ? 1u : 0u);
    }
    // Count only complete current-job graphs; stale attempts remain visible in wall throughput.
    c29_report("graph timing: completed=%u total_ms=%" PRIu64 " trim_ms=%" PRIu64
      " recovery_ms=%" PRIu64 " gps=%.6f prefetch_ms=%" PRIu64 " seed_reused=%u\n", completed ? 1u : 0u, graph_ms,
      context.trim_ms, context.recovery_ms, completed && graph_ms ? 1000.0 / graph_ms : 0.0,
      context.prefetch_ms, context.seed_reused ? 1u : 0u);
#ifdef PS5_COMBINED_MINER
    ps5_gpu_publish(&context.manager, completed && graph_ms ? 1000.0 / graph_ms : 0.0);
#endif
    // Flush optional GPU samples after graph timing so diagnostic logging is excluded.
    c29_bucket_profile_flush(&c29_bucket_full);
    if (result == 1) { failed = true; break; }
  }
  c29_report("pool worker finished: graphs=%" PRIu64 " stale=%" PRIu64
    " shares=%" PRIu64 " accepted=%" PRIu64 " rejected=%" PRIu64 "\n",
    context.manager.stats.graphs, context.manager.stats.stale_graphs,
    context.manager.stats.shares, context.manager.stats.accepted, context.manager.stats.rejected);
  uint64_t wall_ms = mom_pool_now_ms() - start;
  c29_report("pool throughput: graphs=%" PRIu64 " completed_ms=%" PRIu64
    " wall_ms=%" PRIu64 " active_gps=%.6f wall_gps=%.6f\n",
    context.manager.stats.graphs, completed_ms, wall_ms,
    completed_ms ? context.manager.stats.graphs * 1000.0 / completed_ms : 0.0,
    wall_ms ? context.manager.stats.graphs * 1000.0 / wall_ms : 0.0);
#ifdef PS5_COMBINED_MINER
  ps5_gpu_publish(&context.manager, 0);
#endif
  bool progressed = context.manager.stats.graphs != 0;
  mom_pool_destroy(context.pool);
  return failed || !progressed ? 1 : 0;
}
#endif
