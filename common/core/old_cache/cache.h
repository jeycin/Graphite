// Jonathan Eastep (eastep@mit.edu)
// 09.24.08
// Modified cache sets to actually store the data.
//
//
// It's been significantly modified, but original code by Artur Klauser of
// Intel and modified by Rodric Rabbah. My changes enable the cache to be
// dynamically resized (size and associativity) as well as some new statistics
// tracking
//
// RMR (rodric@gmail.com) {
//   - temporary work around because decstr()
//     casts 64 bit ints to 32 bit ones
//   - use safe_fdiv to avoid NaNs in output


#ifndef CACHE_H
#define CACHE_H

#include <iostream>
#include <cassert>

#include "utils.h"
#include "cache_state.h"
#include "random.h"
#include "config.h"

#define k_KILO 1024
#define k_MEGA (k_KILO*k_KILO)
#define k_GIGA (k_KILO*k_MEGA)

// type of cache hit/miss counters
typedef UInt64 CacheStats;

namespace CACHE_ALLOC
{
typedef enum
{
   k_STORE_ALLOCATE,
   k_STORE_NO_ALLOCATE
} StoreAllocation;
};


// Cache tag - self clearing on creation
//TODO change the name of this:
//it isn't a cachtag, its a CacheLineInfo
class CacheTag
{
   private:
      IntPtr m_tag;
      CacheState::cstate_t m_cstate;

   public:
      CacheTag(IntPtr tag = ~0, CacheState::cstate_t cstate = CacheState::INVALID) :
            m_tag(tag), m_cstate(cstate) {}

      bool operator==(const CacheTag& right) const
      {
         return (m_tag == right.m_tag);
      }

      bool isValid() const { return (m_tag != ((IntPtr) ~0)); }

      //BUG FIXME i think this needs to be left shifted if you actually want the address
      operator IntPtr() const { return m_tag; }

      IntPtr getTag() { return m_tag; }

      CacheState::cstate_t getCState() { return m_cstate; }

      void setCState(CacheState::cstate_t cstate) { m_cstate = cstate; }
};



// Everything related to cache sets

namespace CACHE_SET
{

// Cache set direct mapped
class DirectMapped
{
   private:
      CacheTag m_tag;

   public:
      DirectMapped(UInt32 assoc = 1)
      {
         assert(assoc == 1);
         m_tag = CacheTag();
      }

      //FIXME: this should be private. should only be used during instantiation
      void setAssociativity(UInt32 assoc) { assert(assoc == 1); }

      UInt32 getAssociativity(UInt32 assoc) { return 1; }

      UInt32 find(CacheTag& tag) { return(m_tag == tag); }

      void replace(CacheTag& tag) { m_tag = tag; }

      void modifyAssociativity(UInt32 assoc) { assert(assoc == 1); }

      void print()
      {
         //cout << m_tag << endl;
      }

      bool invalidateTag(CacheTag& tag)
      {
         if (tag == m_tag)
         {
            m_tag = CacheTag();
            return true;
         }
         return false;
      }
};


// Cache set with round robin replacement
template <UInt32 k_MAX_ASSOCIATIVITY = 8,
UInt32 k_MAX_BLOCKSIZE = 128>
class RoundRobin
{
   private:
      CacheTag m_tags[k_MAX_ASSOCIATIVITY];
      UInt32 tags_last_index;
      UInt32 next_replace_index;
      char m_blocks[k_MAX_ASSOCIATIVITY*k_MAX_BLOCKSIZE];
      UInt32 blocksize;

   public:

      RoundRobin(UInt32 assoc = k_MAX_ASSOCIATIVITY, UInt32 blksize = k_MAX_BLOCKSIZE):
            tags_last_index(assoc - 1), blocksize(blksize)
      {
         assert(assoc <= k_MAX_ASSOCIATIVITY);
         assert(blocksize <= k_MAX_BLOCKSIZE);
         assert(tags_last_index < k_MAX_ASSOCIATIVITY);

         next_replace_index = tags_last_index;

         for (SInt32 index = tags_last_index; index >= 0; index--)
         {
            m_tags[index] = CacheTag();
         }
         memset(&m_blocks[0], 0x00, k_MAX_BLOCKSIZE * k_MAX_ASSOCIATIVITY);
      }

      void setBlockSize(UInt32 blksize) { blocksize = blksize; }

      UInt32 getBlockSize() { return blocksize; }

      //FIXME: this should be private. should only be used during instantiation
      void setAssociativity(UInt32 assoc)
      {
         //FIXME: possible evictions when shrinking
         assert(assoc <= k_MAX_ASSOCIATIVITY);
         tags_last_index = assoc - 1;
         next_replace_index = tags_last_index;
      }

      UInt32 getAssociativity() { return tags_last_index + 1; }


      pair<bool, CacheTag*> find(CacheTag& tag, UInt32* set_index = NULL)
      {
         // useful for debugging
         //print();

         assert(tags_last_index < k_MAX_ASSOCIATIVITY);

         for (SInt32 index = tags_last_index; index >= 0; index--)
         {
            if (m_tags[index] == tag)
            {
               if (set_index != NULL)
                  *set_index = index;
               return make_pair(true, &m_tags[index]);
            }
         }
         return make_pair(false, (CacheTag*) NULL);
      }

      void read_line(UInt32 index, UInt32 offset, char *out_buff, UInt32 bytes)
      {
         assert(offset + bytes <= blocksize);

         if ((out_buff != NULL) && (bytes != 0))
            memcpy(out_buff, &m_blocks[index * blocksize + offset], bytes);
      }

      void write_line(UInt32 index, UInt32 offset, char *buff, UInt32 bytes)
      {
//    cerr << "WriteLine " << endl;
//    cerr << "Offset    : " << offset << endl;
//    cerr << "bytes     : " << bytes << endl;
//    cerr << "blocksize : " << blocksize << endl;
         assert(offset + bytes <= blocksize);

         if ((buff != NULL) && (bytes != 0))
            memcpy(&m_blocks[index * blocksize + offset], buff, bytes);
      }

      bool invalidateTag(CacheTag& tag)
      {
         for (SInt32 index = tags_last_index; index >= 0; index--)
         {
            assert(0 <= index && (UInt32)index < k_MAX_ASSOCIATIVITY);

            if (m_tags[index] == tag)
            {
               m_tags[index] = CacheTag();
               return true;
            }
         }
         return false;
      }

      void replace(CacheTag& tag, char* fill_buff, bool* eviction, CacheTag* evict_tag, char* evict_buff)
      {
         const UInt32 index = next_replace_index;
         //cout << "*** replacing index " << index << " ***" << endl;

         if (m_tags[index].isValid())
         {
            if (eviction != NULL)
               *eviction = true;
            if (evict_tag != NULL)
               *evict_tag = m_tags[index];
            if (evict_buff != NULL)
               memcpy(evict_buff, &m_blocks[index * blocksize], blocksize);
         }

         assert(index < k_MAX_ASSOCIATIVITY);
         m_tags[index] = tag;

         if (fill_buff != NULL)
         {
            memcpy(&m_blocks[index * blocksize], fill_buff, blocksize);
         }

         // condition typically faster than modulo
         next_replace_index = (index == 0 ? tags_last_index : index - 1);
      }

      void modifyAssociativity(UInt32 assoc)
      {
         //FIXME: potentially need to evict

         assert(assoc != 0 && assoc <= k_MAX_ASSOCIATIVITY);
         UInt32 associativity = getAssociativity();

         if (assoc > associativity)
         {
            for (UInt32 i = tags_last_index + 1; i < assoc; i++)
            {
               assert(i < k_MAX_ASSOCIATIVITY);
               m_tags[i] = CacheTag();
            }
            tags_last_index = assoc - 1;
            next_replace_index = tags_last_index;
         }
         else
         {
            if (assoc < associativity)
            {
               // FIXME: if cache model ever starts including data in addition to just tags
               // need to perform evictions here. Also if we have shared mem?

               assert(!Config::getSingleton()->isSimulatingSharedMemory());

               for (UInt32 i = tags_last_index; i >= assoc; i--)
               {
                  assert(i < k_MAX_ASSOCIATIVITY);
                  m_tags[i] = CacheTag();
               }

               tags_last_index = assoc - 1;
               if (next_replace_index > tags_last_index)
               {
                  next_replace_index = tags_last_index;
               }
            }
         }
      }

      void print()
      {
         /*
         cout << "associativity = " << tags_last_index + 1 << "; next set to replace = "
              << next_replace_index << "     tags = ";
         for (UInt32 i = 0; i < getAssociativity(); i++)
         {
         cout << hex << m_tags[i] << " ";
         }
         cout << endl;
         */
      }

};


};
// end namespace CACHE_SET


// Generic cache base class; no allocate specialization, no cache set specialization

class CacheBase
{
   public:
      // types, constants
      typedef enum
      {
         k_ACCESS_TYPE_LOAD,
         k_ACCESS_TYPE_STORE,
         k_ACCESS_TYPE_NUM
      } AccessType;

      typedef enum
      {
         k_CACHE_TYPE_ICACHE,
         k_CACHE_TYPE_DCACHE,
         k_CACHE_TYPE_NUM
      } CacheType;

   protected:
      //1 counter for hit==true, 1 counter for hit==false
      CacheStats access[k_ACCESS_TYPE_NUM][2];

   protected:
      // input params
      const string name;
      UInt32 cache_size;
      const UInt32 line_size;
      UInt32 associativity;

      // computed params
      const UInt32 line_shift;
      const UInt32 set_index_mask;

   private:
      CacheStats sumAccess(bool hit) const
      {
         CacheStats sum = 0;

         for (UInt32 access_type = 0; access_type < k_ACCESS_TYPE_NUM; access_type++)
         {
            sum += access[access_type][hit];
         }

         return sum;
      }

   public:
      // constructors/destructors
      CacheBase(string name, UInt32 size, UInt32 line_bytes, UInt32 assoc);

      // accessors
      UInt32 getCacheSize() const { return cache_size; }
      UInt32 getLineSize() const { return line_size; }
      UInt32 getNumWays() const { return associativity; }
      UInt32 getNumSets() const { return set_index_mask + 1; }

      // stats
      CacheStats getHits(AccessType access_type) const
      {
         assert(access_type < k_ACCESS_TYPE_NUM);
         return access[access_type][true];
      }
      CacheStats getMisses(AccessType access_type) const
      {
         assert(access_type < k_ACCESS_TYPE_NUM);
         return access[access_type][false];
      }
      CacheStats getAccesses(AccessType access_type) const
      { return getHits(access_type) + getMisses(access_type); }
      CacheStats getHits() const { return sumAccess(true); }
      CacheStats getMisses() const { return sumAccess(false); }
      CacheStats getAccesses() const { return getHits() + getMisses(); }

      // utilities
      IntPtr tagToAddress(CacheTag& tag)
      {
         return tag.getTag() << line_shift;
      }

      void splitAddress(const IntPtr addr, CacheTag& tag, UInt32& set_index) const
      {
         tag = CacheTag(addr >> line_shift);
         set_index = tag & set_index_mask;
      }

      // FIXME: change the name?  (adddressToTag?)
      void splitAddress(const IntPtr addr, CacheTag& tag, UInt32& set_index,
                        UInt32& line_index) const
      {
         const UInt32 line_mask = line_size - 1;
         line_index = addr & line_mask;
         splitAddress(addr, tag, set_index);
      }

      string statsLong(string prefix = "",
                       CacheType cache_type = k_CACHE_TYPE_DCACHE) const;
};

//  Templated cache class with specific cache set allocation policies
//  All that remains to be done here is allocate and deallocate the right
//  type of cache sets.

template <class SET_t, UInt32 k_MAX_SETS, UInt32 k_MAX_SEARCH, UInt32 k_STORE_ALLOCATION>
class Cache : public CacheBase
{
   private:
      SET_t  sets[k_MAX_SETS];
      UInt64 accesses[k_MAX_SETS];
      UInt64 misses[k_MAX_SETS];
      UInt64 total_accesses[k_MAX_SETS];
      UInt64 total_misses[k_MAX_SETS];
      UInt32 set_ptrs[k_MAX_SETS+1];
      UInt32 max_search;
      Random rand;

   public:
      void resetCounters()
      {
         assert(getNumSets() <= k_MAX_SETS);
         for (UInt32 i = 0; i < getNumSets(); i++)
         {
            accesses[i] = misses[i] = 0;
         }
      }

      UInt32 getSearchDepth() const { return max_search; }
      UInt32 getSetPtr(UInt32 set_index)
      {
         assert(set_index < getNumSets());
         assert(getNumSets() <= k_MAX_SETS);
         return set_ptrs[set_index];
      }
      void setSetPtr(UInt32 set_index, UInt32 value)
      {
         assert(set_index < k_MAX_SETS);
         assert((value < getNumSets()) || (value == k_MAX_SETS));
         set_ptrs[set_index] = value;
      }

      // constructors/destructors
      Cache(string name, UInt32 size, UInt32 line_bytes,
            UInt32 assoc, UInt32 max_search_depth) :
            CacheBase(name, size, line_bytes, assoc)
      {
         assert(getNumSets() <= k_MAX_SETS);
         assert(max_search_depth < k_MAX_SEARCH);

         // caches are initialized during instrumentation, which is
         // single-threaded, so it is safe to use a static var to seed
         // the random number generators
         static Random::value_t cache_number = 0;
         rand.seed(++cache_number);

         max_search = max_search_depth;

         for (UInt32 i = 0; i < getNumSets(); i++)
         {
            total_accesses[i] = total_misses[i] = 0;
            sets[i].setAssociativity(assoc);
            sets[i].setBlockSize(line_bytes);
            set_ptrs[i] = k_MAX_SETS;
         }
         resetCounters();
      }


      //JME: added for dynamically resizing a cache
      void resize(UInt32 assoc)
      {
         // new configuration written out overly explicitly; basically nothing
         // but the cache size changes
         //    _newNumSets = getNumSets();
         //    _newLineSize = line_size;
         //    _newLineShift = line_shift;
         //    _newSetIndexMask = set_index_mask;

         assert(getNumSets() <= k_MAX_SETS);
         cache_size = getNumSets() * assoc * line_size;
         associativity = assoc;

         // since the number of sets stays the same, no lines need to be relocated
         // internally; instead space for blocks within each set needs to be added
         // or removed (possibly causing evictions in the real world)

         for (UInt32 i = 0; i < getNumSets(); i++)
         {
            sets[i].modifyAssociativity(assoc);
         }
      }


      // functions for accessing the cache

      // External interface for invalidating a cache line. Returns whether or not line was in cache
      bool invalidateLine(IntPtr addr)
      {
         CacheTag tag;
         UInt32 index;

         splitAddress(addr, tag, index);
         assert(index < k_MAX_SETS);
         return sets[index].invalidateTag(tag);
      }

      // Single line cache access at addr
      pair<bool, CacheTag*> accessSingleLine(IntPtr addr, AccessType access_type,
                                             bool* fail_need_fill = NULL, char* fill_buff = NULL,
                                             char* buff = NULL, UInt32 bytes = 0,
                                             bool* eviction = NULL, IntPtr* evict_addr = NULL, char* evict_buff = NULL)
      {

         /*
                   Usage:
                      fail_need_fill gets set by this function. indicates whether or not a fill buff is required.
                      If you get one, retry specifying a valid fill_buff containing the line from DRAM.
                      For read accesses, bytes and buff are for retrieving data from a cacheline.
                      The var called bytes specifies how many bytes to copy into buff.
                      Writes work similarly, but bytes specifies how many bytes to write into the cacheline.
                      The var called eviction indicates whether or not an eviction occured.
                      The vars called evict_addr and evict_buff contain the evict address and the cacheline
         */

         // set these to correspond with each other
         assert((buff == NULL) == (bytes == 0));

         // these should be opposite. they signify whether or not to service a fill
         assert((fail_need_fill == NULL) || ((fail_need_fill == NULL) != (fill_buff == NULL)));

         // You might have an eviction at any time. If you care about evictions, all three parameters should be non-NULL
         assert(((eviction == NULL) == (evict_addr == NULL)) && ((eviction == NULL) == (evict_buff == NULL)));

         UInt32 history[k_MAX_SEARCH];
         /*
         =======
                 const IntPtr high_addr = addr + size;
                 bool all_hit = true;

                 const IntPtr line_bytes = getLineSize();
                 const IntPtr not_line_mask = ~(line_bytes - 1);

                 UInt32 history[k_MAX_SEARCH];

                 do
                 {
                     CacheTag tag;
                     UInt32 set_index;

                     splitAddress(addr, tag, set_index);

                     UInt32 index = set_index;
                     UInt32 depth = 0;
                     bool local_hit;

                     do
              {
                        //if ( depth > 0)
                 //cout << "index = " << index << endl;
                        history[depth] = index;
                        SET_t &set = sets[index];
                        local_hit = set.find(tag);
                        index = set_ptrs[index];
                     } while ( !local_hit && ((++depth) < max_search) && (index < k_MAX_SETS));

                     all_hit &= local_hit;

                     // on miss, loads always allocate, stores optionally
                     if ( (! local_hit) && ((access_type == k_ACCESS_TYPE_LOAD) ||
                                           (k_STORE_ALLOCATION == CACHE_ALLOC::k_STORE_ALLOCATE)) )
                     {
                        UInt32 which = history[r_num];
                        assert(which < k_MAX_SETS);
                        sets[which].replace(tag);
                        //if ( depth > 1 )
                 //cout << "which = " << which << endl;
                     }

                    // start of next cache line
                    addr = (addr & not_line_mask) + line_bytes;
                 }
                 while (addr < high_addr);

                 assert(access_type < k_ACCESS_TYPE_NUM);
                 access[access_type][all_hit]++;

                 return all_hit;
              }

              // Single line cache access at addr
              bool accessSingleLine(IntPtr addr, AccessType access_type)
              {
                 UInt32 history[k_MAX_SEARCH];

         */
         CacheTag tag;
         UInt32 set_index;

         splitAddress(addr, tag, set_index);

         UInt32 index = set_index;
         UInt32 next_index = index;
         UInt32 depth = 0;
         bool hit;

         pair<bool, CacheTag*> res;
         CacheTag *tagptr = (CacheTag*) NULL;
         UInt32 line_index = -1;

         do
         {
            index = next_index;
            history[depth] = index;
            SET_t &set = sets[index];
            //set.print();
            res = set.find(tag, &line_index);
            hit = res.first;
            next_index = set_ptrs[index];
         }
         while (!hit && ((++depth) < max_search) && (index < k_MAX_SETS));


         if (fail_need_fill != NULL)
         {
            if ((fill_buff == NULL) && !hit)
            {
               *fail_need_fill = true;

               if (eviction != NULL)
                  *eviction = false;

               return make_pair(false, (CacheTag*) NULL);
            }
            else
            {
               *fail_need_fill = false;
            }
         }

         if (hit)
         {
            tagptr = res.second;

            if (access_type == k_ACCESS_TYPE_LOAD)
               sets[index].read_line(line_index, addr & (line_size - 1), buff, bytes);
            else
            {
//     cerr << "ADDR: " << hex << addr << endl;
//     cerr << "ln_sz: " << dec << line_size << endl;
//     cerr << "Hit -> strt write_line: offset= " << dec << (addr & (line_size -1)) << " bytes= " << bytes << endl;
               sets[index].write_line(line_index, addr & (line_size - 1), buff, bytes);
//     cerr << "Hit -> end  write_line" << endl;
            }

         }

         // on miss, loads always allocate, stores optionally
         if ((! hit) && ((access_type == k_ACCESS_TYPE_LOAD) ||
                         (k_STORE_ALLOCATION == CACHE_ALLOC::k_STORE_ALLOCATE)))
         {
            UInt32 r_num = rand.next(depth);
            UInt32 which = history[r_num];
            //evict_tag is set by replace, need to translate tag to address
            CacheTag evict_tag;
            sets[which].replace(tag, fill_buff, eviction, &evict_tag, evict_buff);
            if (evict_addr != NULL)
               *evict_addr = tagToAddress(evict_tag);
            tagptr = sets[which].find(tag, &line_index).second;

            if (depth > 1)
            {
               // cout << "which = " << dec << which << endl;
            }

            if (access_type == k_ACCESS_TYPE_LOAD)
               sets[which].read_line(line_index, addr & (line_size - 1), buff, bytes);
            else
            {
//     cerr << "!Hit -> strt write_line: offset= " << (addr & (line_size -1)) << " bytes= " << bytes << endl;
               sets[which].write_line(line_index, addr & (line_size - 1), buff, bytes);
//     cerr << "!Hit -> end  write_line" << endl;
            }

         }
         else
         {
            if (eviction != NULL)
               *eviction = false;
         }

         access[access_type][hit]++;

         return make_pair(hit, tagptr);
      }


      // Single line cache access at addr
      pair<bool, CacheTag*> accessSingleLinePeek(IntPtr addr)
      {
         CacheTag tag;
         UInt32 set_index;

         splitAddress(addr, tag, set_index);

         UInt32 index = set_index;
         UInt32 depth = 0;
         bool hit;

         pair<bool, CacheTag*> res;
         CacheTag *tagptr = (CacheTag*) NULL;

         do
         {
            SET_t &set = sets[index];
            //set.print();
            res = set.find(tag);
            hit = res.first;
            index = set_ptrs[index];
         }
         while (!hit && ((++depth) < max_search) && (index < k_MAX_SETS));

         if (hit)
            tagptr = res.second;

         return make_pair(hit, tagptr);
      }


};

#endif