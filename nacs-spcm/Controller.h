//

#ifndef _NACS_SPCM_CONTROLLER_H
#define _NACS_SPCM_CONTROLLER_H

#include <nacs-utils/thread.h>
#include <nacs-utils/mem.h>
#include <nacs-utils/log.h>

#include <nacs-spcm/clock.h>

#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>

#include <nacs-spcm/Config.h>
#include <nacs-spcm/spcm.h>
#include <nacs-spcm/Stream.h>
#include <nacs-spcm/StreamManager.h>
#include <nacs-spcm/FileStreamManager.h>
//#include <nacs-spcm/Server.h>

using namespace NaCs;

namespace Spcm{
      class Server;

      class Controller {
      public:
          struct TrigInfo {
              uint64_t trigger_t = 0;
              uint64_t len = 0;
          };
          Controller(Server &serv, Config &conf, FileCache& fcache, std::vector<uint8_t> out_chns)
              : m_serv(serv),
                m_conf(conf),
                m_fcache(fcache)
          {
              /* StreamManager(uint32_t n_streams, uint32_t max_per_stream,
                  double step_t, std::atomic<uint64_t> &cmd_underflow,
                  std::atomic<uint64_t> &underflow, bool startStream = false,
                  bool startWorker = false) */
              auto phys_chn = out_chns.size();
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
                      m_stm_mngrs.emplace_back(new StreamManager(*this,m_conf, num_streams, 6, 1, cmd_underflow, underflow, false));
                      m_fstm_mngrs.emplace_back(new FileStreamManager(*this, fcache, m_conf, 1, 100, fcmd_underflow,funderflow, false));
                      max_chns.push_back(16);
                      ptr_map.emplace(i, std::vector<const int16_t*>{num_streams, nullptr});
                      fptr_map.emplace(i, std::vector<const int16_t*>{1, nullptr});
                  }
              }
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
              // end state is a working stream
              // check if vectors match
              if (out_chns == m_out_chns) {
                  startWorker();
                  return;
              }
              auto phys_chn = out_chns.size();
              if (phys_chn != 1 && phys_chn != 2)
              {
                  throw std::runtime_error("Only supports 1 or 2 physical channels");
              }
              else {
                  reqWait();
                  stopWorker();
                  // TODO: Restart worker?? HANDLE CARD COMMANDS (inside of stopWorker?)
                  //for (int i = 0; i < n_card_chn; i++) {
                  //    m_stm_mngrs[i].reset(new StreamManager(4, 4, 1, cmd_underflow, cmd_underflow, false));
                  //}
                  //printf("Calling startWorker\n");
                  n_phys_chn = phys_chn;
                  for (int i = 0; i < phys_chn; i++) {
                      if (out_chns[i] > 3 || out_chns[i] < 0) {
                          throw std::runtime_error("Invalid output channel");
                      }
                  }
                  m_out_chns = out_chns;
                  resetStmManagers(); // ensure stream managers and streams are reset for the new streams being used
                  startWorker();
                  //printf("After startWorker\n");
              }
          }
          inline void restartCard(bool from_worker) {
              reqWait(); // stop sequences from proceeding
              if (!from_worker)
                  stopWorker();
              else {
                  for (int i = 0; i < n_phys_chn; ++i) {
                      //printf("Stopping stream %u\n", m_out_chns[i]);
                      (*m_stm_mngrs[m_out_chns[i]]).stop_streams();
                      (*m_fstm_mngrs[m_out_chns[i]]).stop_streams();
                      //(*m_stm_mngrs[m_out_chns[i]]).stop_worker();
                      (*m_stm_mngrs[m_out_chns[i]]).reset_streams_out();
                      (*m_fstm_mngrs[m_out_chns[i]]).reset_streams_out();
                      //(*m_stm_mngrs[m_out_chns[i]]).reset_out();
                  }
              }
              stopCard();
              resetStmManagers();
              m_output_cnt = 0;
              n_restarts++; // Incrementing this is the key for letting all players know of a card restart.
              printf("Attempting card restart %u...\n", n_restarts);
              std::this_thread::sleep_for(std::chrono::milliseconds(1000));
              if (!from_worker) {
                  startWorker();
              }
              else {
                  ensureInit();
                  initChnsAndBuffer();
                  for (int i = 0; i < n_phys_chn; ++i) {
                      (*m_stm_mngrs[m_out_chns[i]]).start_streams();
                      (*m_fstm_mngrs[m_out_chns[i]]).start_streams();
                      //(*m_stm_mngrs[m_out_chns[i]]).start_worker();
                  }
              }
              printf("Done with card restart\n");
          }
          inline void startDataTransfer() {
              first_start.store(true, std::memory_order_relaxed);
          }
          inline void resetStmManagers() {
              for (int i = 0; i < n_phys_chn; i++) {
                  m_stm_mngrs[m_out_chns[i]]->reset();
                  m_fstm_mngrs[m_out_chns[i]]->reset();
              }
          }
          inline uint32_t get_start_id() {
              return ++m_start_trigger_cnt;
          }
          inline uint32_t get_end_id() {
              return ++m_end_trigger_cnt;
          }
          inline void set_start_trigger(uint32_t v, uint64_t t) {
              //printf("setting start trigger %u for time %lu\n", v, t);
              TrigInfo &info = m_trig_map[v];
              info.trigger_t = t;
              printf("Setting trigger %u time to %lu\n", v, t);
              for (int i = 0; i < n_phys_chn; i++) {
                  m_stm_mngrs[m_out_chns[i]]->set_start_trigger(v,t); // may not be useful, but can keep here.
                  m_fstm_mngrs[m_out_chns[i]]->set_start_trigger(v,t);
              }
          }
          inline uint32_t copy_cmds(uint32_t idx, Cmd *cmds, uint32_t sz)
          {
              return m_stm_mngrs[idx]->copy_cmds(cmds, sz);
          }
          inline uint32_t copy_cmdsf(uint32_t idx, FCmd *cmds, uint32_t sz)
          {
              return m_fstm_mngrs[idx]->copy_cmds(cmds, sz);
          }
          inline bool try_add_cmd(uint32_t idx, Cmd cmd)
          {
              return m_stm_mngrs[idx]->try_add_cmd(cmd);
          }
          inline bool try_add_cmdf(uint32_t idx, FCmd cmd)
          {
              return m_fstm_mngrs[idx]->try_add_cmd(cmd);
          }
          inline void add_cmd(uint32_t idx, Cmd cmd)
          {
              m_stm_mngrs[idx]->add_cmd(cmd);
          }
          inline void add_cmdf(uint32_t idx, FCmd cmd)
          {
              m_fstm_mngrs[idx]->add_cmd(cmd);
          }
          inline void flush_cmd(uint32_t idx)
          {
              m_stm_mngrs[idx]->flush_cmd();
          }
          void distribute_cmds(uint32_t idx)
          {
              m_stm_mngrs[idx]->distribute_cmds();
              m_fstm_mngrs[idx]->distribute_cmds();
          }
          inline uint32_t getMaxChn(uint32_t idx) {
              return max_chns[idx];
          }
          inline uint64_t check_avail()
          {
              return card_avail.load(std::memory_order_relaxed);
          }
          inline bool check_error()
          {
              try {
                  hdl.check_error();
              }
              catch (const NaCs::Spcm::Error &err) {
                  if (err.code == ERR_FIFOHWOVERRUN) {
                      // buffer overrun
                      return true;
                  }
                  else {
                      // for non buffer overrun, for now, do not handle and just throw
                      // TODO add code maybe to deal with all waiters on the server before exiting
                      throw;
                  }
              }
              return false;
          }
          void force_trigger();
          void runSeq(uint32_t idx, Cmd *p, size_t sz, bool wait=true); //TODO FILESTREAM NOT INCORPORATED HERE, Not used in Server.cpp
          inline void reqWait()
          {
              m_wait_req.store(true, std::memory_order_relaxed);
          }
          inline bool reqRestart(uint32_t trig_id)
          {
              if (trig_id == restart_id)
              {
                  // restart already requested
                  return false;
              }
              restart_id = trig_id;
              m_worker_req.store(WorkerRequest::RestartCard, std::memory_order_relaxed);
              return true;
          }
          inline bool waitPending() const {
              return m_wait_req.load(std::memory_order_relaxed);
              //return false;
          }
          inline uint32_t get_end_triggered() {
              uint32_t min_end_triggered = UINT_MAX;
              for (int i = 0; i < n_phys_chn; i++) {
                  min_end_triggered = std::min(min_end_triggered, (*(m_stm_mngrs[m_out_chns[i]])).get_end_triggered());
                  min_end_triggered = std::min(min_end_triggered, (*(m_fstm_mngrs[m_out_chns[i]])).get_end_triggered());
              }
              return min_end_triggered;
          }
          inline bool get_end_triggered(uint32_t v) {
              // v = 0 is non-triggered, in which case we don't need to delete the map entry
              // and can't use the trigger id to see if previously had finished.
              if (v && v <= last_trig_id) {
                  return true;
              }
              TrigInfo &info = m_trig_map[v];
              if (m_output_cnt >= info.trigger_t + info.len) {
                  if (v) {
                      m_trig_map.erase(v);
                      printf("Erasing trigger %u\n", v);
                      last_trig_id = v;
                  }
                  return true;
              }
              return false;
          }
          inline uint32_t get_restarts() {
              return n_restarts;
          }
          inline uint32_t add_restarts(uint32_t n = 1) {
              n_restarts = n_restarts + n;
              return n_restarts;
          }
          inline void set_len(uint32_t v, uint64_t n) {
              printf("Setting trigger %u length to %lu\n", v, n);
              TrigInfo &info = m_trig_map[v];
              info.len = n;
          }
          inline void set_trig_to_now(uint32_t v, uint64_t delay) {
              TrigInfo &info = m_trig_map[v];
              info.trigger_t = m_output_cnt + delay;
          }
      private:
          enum class WorkerRequest : uint8_t {
              None = 0,
              Stop,
              Unlock,
              RestartCard
          };
          void init();
          bool checkRequest();
          void tryUnlock();
          void workerFunc();
          void startDMA(uint64_t sz);
          std::vector<std::unique_ptr<StreamManager>> m_stm_mngrs;
          std::vector<std::unique_ptr<FileStreamManager>> m_fstm_mngrs;
          std::vector<uint32_t> max_chns;
          Config &m_conf;
          FileCache &m_fcache;

          bool m_initialized{false};

          std::thread m_worker; // worker for relaying data to card
          int16_t* buff_ptr; // buffer pointer for spcm
          size_t buff_pos; // position for the output buffer
          uint64_t buff_sz_nele{ 16*1024ll * 1024ll / 2}; // 2/2 //4 factor of 4 1 channel output latency of 6.71 ms. Software buffer size
          uint64_t hw_buff_sz_nele{32 * 1024ll * 1024ll/2}; // 1 //2 1 channel output latency of 1.67 ms. Hardware buffer size number of elements
          bool DMA_started{false};

          NaCs::Spcm::Spcm hdl{"/dev/spcm0"}; //Spcm handle

          std::atomic<WorkerRequest> m_worker_req{WorkerRequest::None};
          std::mutex m_worker_lock;

          std::atomic<uint64_t> cmd_underflow{0};
          std::atomic<uint64_t> underflow{0};
          std::atomic<uint64_t> fcmd_underflow{0};
          std::atomic<uint64_t> funderflow{0};
          std::atomic<uint64_t> card_avail{4 * 1024ll * 1024ll * 1024ll};
          std::atomic<bool> first_start = false;

          uint8_t n_phys_chn{1}; // only supports 1 or 2.
          std::vector<uint8_t> m_out_chns; // output channel when n_phys_chn = 1, otherwise ignored
          const uint8_t n_card_chn = 4; // number of channels on card. hard coded
          uint32_t m_start_trigger_cnt{0};
          uint32_t m_end_trigger_cnt{0};
          uint64_t counter = 0;
          uint64_t not_ready_counter = 0;
          uint64_t not_counter = 0;
          uint64_t loop_counter = 0;
          uint64_t notif_size = 4096 * 16; // in bytes

          uint32_t n_restarts = 0;
          Server &m_serv;
          std::atomic<bool> m_wait_req{true};
          uint64_t m_output_cnt = 0;
          std::map<uint32_t, TrigInfo> m_trig_map;
          uint32_t restart_id = 0; // keeps track of start_trigger that requested a restart (for triggers that are noticed too late)
          uint32_t last_trig_id = 0; // last finished trig id

          std::map<uint32_t, std::vector<const int16_t*>> ptr_map;
          std::map<uint32_t, std::vector<const int16_t*>> fptr_map;
          uint32_t num_streams = 3;
      };
}
#endif
