//

#ifndef _NACS_SPCM_CONTROLLER_H
#define _NACS_SPCM_CONTROLLER_H

#include <nacs-utils/thread.h>
#include <nacs-utils/mem.h>
#include <nacs-utils/log.h>

#include <thread>
#include <atomic>
#include <mutex>

#include <nacs-spcm/spcm.h>
#include <nacs-spcm/Stream.h>
#include <nacs-spcm/StreamManager.h>

using namespace NaCs;
namespace Spcm{
      class Controller {
      public:
          Controller() :
              m_stm_mngr(8, 4, 1, cmd_underflow, cmd_underflow, false)
          {
          }
          ~Controller()
          {
              stopWorker();
          }
          void ensureInit()
          {
              if (!m_initialized) {
                  init();
              }
          }
          void startWorker();
          void stopWorker();
          bool workerRunning() const
          {
              return m_worker.joinable();
          }
          // StreamManager commands
          inline uint32_t copy_cmds(Cmd *cmds, uint32_t sz)
          {
              return m_stm_mngr.copy_cmds(cmds, sz);
          }
          inline bool try_add_cmd(Cmd &cmd)
          {
              return m_stm_mngr.try_add_cmd(cmd);
          }
          inline void add_cmd(Cmd &cmd)
          {
              m_stm_mngr.add_cmd(cmd);
          }
          inline void flush_cmd()
          {
              m_stm_mngr.flush_cmd();
          }
          void distribute_cmds()
          {
              m_stm_mngr.distribute_cmds();
          }
          inline uint64_t check_avail()
          {
              return card_avail.load(std::memory_order_relaxed);
          }
          void force_trigger();
          void runSeq(Cmd *p, size_t sz, bool wait=true);
      private:
          enum class WorkerRequest : uint8_t {
              None = 0,
              Stop,
              Unlock,
          };
          void init();
          bool checkRequest();
          void tryUnlock();
          void workerFunc();
          void startDMA(uint64_t sz);
          StreamManager m_stm_mngr;
          //Config &m_conf;

          bool m_initialized{false};

          std::thread m_worker; // worker for relaying data to card
          int16_t* buff_ptr; // buffer pointer for spcm
          size_t buff_pos; // position for the output buffer
          uint64_t buff_sz_nele{4 * 1024ll * 1024ll * 1024ll / 2};
          bool DMA_started{false};

          NaCs::Spcm::Spcm hdl{"/dev/spcm0"}; //Spcm handle

          std::atomic<WorkerRequest> m_worker_req{WorkerRequest::None};
          std::mutex m_worker_lock;

          std::atomic<uint64_t> cmd_underflow{0};
          std::atomic<uint64_t> underflow{0};
          std::atomic<uint64_t> card_avail{4 * 1024ll * 1024ll * 1024ll};
      };
}

#endif
