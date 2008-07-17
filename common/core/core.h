// Harshad Kasture
//

#ifndef CORE_H
#define CORE_H

#include <iostream>
#include <fstream>
#include <string.h>

// JME: not entirely sure why this is needed...
class Network;

#include "pin.H"
#include "chip.h"
#include "network.h"
#include "perfmdl.h"
#include "ocache.h"


// externally defined vars

extern LEVEL_BASE::KNOB<bool> g_knob_enable_performance_modeling;
extern LEVEL_BASE::KNOB<bool> g_knob_enable_dcache_modeling;
extern LEVEL_BASE::KNOB<bool> g_knob_enable_icache_modeling;

extern LEVEL_BASE::KNOB<UINT32> g_knob_cache_size;
extern LEVEL_BASE::KNOB<UINT32> g_knob_line_size;
extern LEVEL_BASE::KNOB<UINT32> g_knob_associativity;
extern LEVEL_BASE::KNOB<UINT32> g_knob_mutation_interval;
extern LEVEL_BASE::KNOB<UINT32> g_knob_dcache_threshold_hit;
extern LEVEL_BASE::KNOB<UINT32> g_knob_dcache_threshold_miss;
extern LEVEL_BASE::KNOB<UINT32> g_knob_dcache_size;
extern LEVEL_BASE::KNOB<UINT32> g_knob_dcache_associativity;
extern LEVEL_BASE::KNOB<UINT32> g_knob_dcache_max_search_depth;
extern LEVEL_BASE::KNOB<UINT32> g_knob_icache_threshold_hit;
extern LEVEL_BASE::KNOB<UINT32> g_knob_icache_threshold_miss;
extern LEVEL_BASE::KNOB<UINT32> g_knob_icache_size;
extern LEVEL_BASE::KNOB<UINT32> g_knob_icache_associativity;
extern LEVEL_BASE::KNOB<UINT32> g_knob_icache_max_search_depth; 


class Core
{
   private:
      Chip *the_chip;
      int core_tid;
      int core_num_mod;
      Network *network;
      PerfModel *perf_model;
      OCache *ocache;

   public:

      int coreInit(Chip *chip, int tid, int num_mod);

      int coreSendW(int sender, int receiver, char *buffer, int size);

      int coreRecvW(int sender, int receiver, char *buffer, int size);

      VOID fini(int code, VOID *v, ofstream& out);


      //performance model wrappers

      inline VOID perfModelRun(PerfModelIntervalStat *interval_stats)
      { perf_model->run(interval_stats); }

      inline VOID perfModelRun(PerfModelIntervalStat *interval_stats, REG *reads, 
                               UINT32 num_reads)
      { perf_model->run(interval_stats, reads, num_reads); }

      inline VOID perfModelRun(PerfModelIntervalStat *interval_stats, bool dcache_load_hit, 
                               REG *writes, UINT32 num_writes)
      { perf_model->run(interval_stats, dcache_load_hit, writes, num_writes); }

      inline PerfModelIntervalStat* perfModelAnalyzeInterval(const string& parent_routine, 
                                                             const INS& start_ins, 
                                                             const INS& end_ins)
      { return perf_model->analyzeInterval(parent_routine, start_ins, end_ins); }

      inline VOID perfModelLogICacheLoadAccess(PerfModelIntervalStat *stats, bool hit)
      { perf_model->logICacheLoadAccess(stats, hit); }

      inline VOID perfModelLogDCacheStoreAccess(PerfModelIntervalStat *stats, bool hit)
      { perf_model->logDCacheStoreAccess(stats, hit); }

      inline VOID perfModelLogBranchPrediction(PerfModelIntervalStat *stats, bool correct)
      { perf_model->logBranchPrediction(stats, correct); }
      

      // organic cache wrappers

      inline bool icacheRunLoadModel(ADDRINT i_addr, UINT32 size)
      { return ocache->runICacheLoadModel(i_addr, size); }

      inline bool dcacheRunLoadModel(ADDRINT d_addr, UINT32 size)
      { return ocache->runDCacheLoadModel(d_addr, size); }

      inline bool dcacheRunStoreModel(ADDRINT d_addr, UINT32 size)
      { return ocache->runDCacheStoreModel(d_addr, size); }

};

#endif