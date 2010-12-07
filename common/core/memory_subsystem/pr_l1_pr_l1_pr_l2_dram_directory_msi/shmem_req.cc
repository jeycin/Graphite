#include "shmem_req.h"
#include "log.h"

namespace PrL1PrL1PrL2DramDirectoryMSI
{
   ShmemReq::ShmemReq(ShmemMsg* shmem_msg, UInt64 time):
      m_time(time)
   {
      // Make a local copy of the shmem_msg
      m_shmem_msg = new ShmemMsg(shmem_msg);
      LOG_ASSERT_ERROR(shmem_msg->getDataBuf() == NULL, 
            "Shmem Reqs should not have data payloads");
   }

   ShmemReq::~ShmemReq()
   {
      delete m_shmem_msg;
   }
}
