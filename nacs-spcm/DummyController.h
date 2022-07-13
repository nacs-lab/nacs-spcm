//

#ifndef _NACS_SPCM_DUMMYCONTROLLER_H
#define _NACS_SPCM_DUMMYCONTROLLER_H

#include <nacs-utils/thread.h>
#include <nacs-utils/mem.h>
#include <nacs-utils/log.h>

#include <thread>
#include <atomic>
#include <mutex>

#include <nacs-spcm/spcm.h>
#include <nacs-spcm/DummyStream.h>
#include <nacs-spcm/DummyStreamManager.h>

using namespace NaCs;
namespace Spcm{
      class DummyController {
      public:
          DummyController(std::vector<uint8_t> out_chns)
          {
              /* StreamManager(uint32_t n_streams, uint32_t max_per_stream,
                  double step_t, std::atomic<uint64_t> &cmd_underflow,
                  std::atomic<uint64_t> &underflow, bool startStream = false,
                  bool startWorker = false) */
              auto phys_chn = out_chns.size();
              //printf("phys_chn: %u", phys_chn);
              if (phys_chn != 1 && phys_chn != 2)
              {
                  throw std::runtime_error("Only supports 1 or 2 physical channels");
              }
              else {
                  n_phys_chn = phys_chn;
                  for (int i = 0; i < phys_chn; i++) {
                      if (out_chns[i] > 3 || out_chns[i] < 0) {
                          throw std::runtime_error("Invalid output channel");
                      }
                  }
                  m_out_chns = out_chns;
                  for (int i = 0; i < n_card_chn; i++) {
                      m_stm_mngrs.emplace_back(new DummyStreamManager(8, 4, 1, cmd_underflow, cmd_underflow, false, false, i));
                      max_chns.push_back(32);
                  }
              }
              if (n_phys_chn == 1) {
                  printf("Initial out chns: %u\n", m_out_chns[0]);
                  printf("StreamManagers: %u\n", m_stm_mngrs.size());
              }
              else if (n_phys_chn == 2) {
                  printf("Initial out chns: %u, %u\n", m_out_chns[0], m_out_chns[1]);
              }
          }
          ~DummyController()
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
          void initChnsAndBuffer();
          void stopCard();
          std::vector <uint8_t> getOutChn() {
              return m_out_chns;
          }
          bool workerRunning() const
          {
              return m_worker.joinable();
          }
          // StreamManager commands
          inline void setPhysChn(std::vector<uint8_t> &out_chns) {
              auto phys_chn = out_chns.size();
              printf("phys_chn: %u", phys_chn);
              if (phys_chn != 1 && phys_chn != 2)
              {
                  throw std::runtime_error("Only supports 1 or 2 physical channels");
              }
              else {
                  n_phys_chn = phys_chn;
                  for (int i = 0; i < phys_chn; i++) {
                      if (out_chns[i] > 3 || out_chns[i] < 0) {
                          throw std::runtime_error("Invalid output channel");
                      }
                  }
                  stopWorker();
                  m_out_chns = out_chns;
                  // TODO: Restart worker?? HANDLE CARD COMMANDS (inside of stopWorker?)
                  if (n_phys_chn == 1) {
                  printf("Changed out chns: %u\n", m_out_chns[0]);
              }
              else if (n_phys_chn == 2) {
                  printf("Changed out chns: %u, %u\n", m_out_chns[0], m_out_chns[1]);
              }
                  startWorker();
              }
          }
          inline uint32_t get_start_id() {
              return ++m_start_trigger_cnt;
          }
          inline uint32_t get_end_id() {
              return ++m_end_trigger_cnt;
          }
          inline void set_start_trigger(uint32_t v, uint64_t t) {
              for (int i = 0; i < n_phys_chn; i++) {
                  m_stm_mngrs[m_out_chns[i]]->set_start_trigger(v,t);
              }
          }
          inline uint32_t copy_cmds(uint32_t idx, Cmd *cmds, uint32_t sz)
          {
              return m_stm_mngrs[idx]->copy_cmds(cmds, sz);
          }
          inline bool try_add_cmd(uint32_t idx, Cmd cmd)
          {
              return m_stm_mngrs[idx]->try_add_cmd(cmd);
          }
          inline void add_cmd(uint32_t idx, Cmd cmd)
          {
              m_stm_mngrs[idx]->add_cmd(cmd);
          }
          inline void flush_cmd(uint32_t idx)
          {
              m_stm_mngrs[idx]->flush_cmd();
          }
          void distribute_cmds(uint32_t idx)
          {
              m_stm_mngrs[idx]->distribute_cmds();
          }
          inline uint32_t getMaxChn(uint32_t idx) {
              return max_chns[idx];
          }
          // inline uint64_t check_avail()
          // {
          //     return card_avail.load(std::memory_order_relaxed);
          // }
          void force_trigger();
          void runSeq(uint32_t idx, Cmd *p, size_t sz, bool wait=true);
          bool waitPending() {
              return false;
          }
          inline uint32_t get_end_triggered() {
              uint32_t min_end_triggered = UINT32_MAX;
              for (int i = 0; i < n_phys_chn; i++) {
                  min_end_triggered = std::min(min_end_triggered, (*(m_stm_mngrs[m_out_chns[i]])).get_end_triggered());
              }
              return min_end_triggered;
          }
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
          std::vector<std::unique_ptr<DummyStreamManager>> m_stm_mngrs;
          std::vector<uint32_t> max_chns;
          //Config &m_conf;

          bool m_initialized{false};

          std::thread m_worker; // worker for relaying data to card
          //int16_t* buff_ptr; // buffer pointer for spcm
          //size_t buff_pos; // position for the output buffer
          //uint64_t buff_sz_nele{4 * 1024ll * 1024ll * 1024ll / 2};
          bool DMA_started{false};

          //NaCs::Spcm::Spcm hdl{"/dev/spcm0"}; //Spcm handle

          std::atomic<WorkerRequest> m_worker_req{WorkerRequest::None};
          std::mutex m_worker_lock;

          std::atomic<uint64_t> cmd_underflow{0};
          std::atomic<uint64_t> underflow{0};
          //std::atomic<uint64_t> card_avail{4 * 1024ll * 1024ll * 1024ll};
          uint8_t n_phys_chn{1}; // only supports 1 or 2.
          std::vector<uint8_t> m_out_chns; // output channel when n_phys_chn = 1, otherwise ignored
          const uint8_t n_card_chn = 4; // number of channels on card. hard coded
          uint32_t m_start_trigger_cnt{0};
          uint32_t m_end_trigger_cnt{0};
      };
}

#endif
