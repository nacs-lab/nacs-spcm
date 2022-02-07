//

#ifndef _NACS_SPCM_CONTROLLERTEXT_H
#define _NACS_SPCM_CONTROLLERTEXT_H

#include <nacs-utils/thread.h>
#include <nacs-utils/mem.h>
#include <nacs-utils/log.h>

#include <nacs-spcm/clock.h>

#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <yaml-cpp/yaml.h>

#include <nacs-spcm/Config.h>
#include <nacs-spcm/spcm.h>
#include <nacs-spcm/Stream.h>
#include <nacs-spcm/StreamManager.h>
//#include <nacs-spcm/Server.h>

using namespace NaCs;

namespace Spcm{
      class ControllerText {
      public:
          ControllerText(std::vector<uint8_t> out_chns, uint32_t nstreams)
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
                      m_stm_mngrs.emplace_back(new StreamManager(*this, nstreams, 100, 1, cmd_underflow, cmd_underflow, false));
                      max_chns.push_back(16);
                  }
              }
          }
          ~ControllerText()
          {
          }
          void reqRestart(uint32_t) {
          }
          std::vector <uint8_t> getOutChn() {
              return m_out_chns;
          }
          // StreamManager commands
          inline void setPhysChn(std::vector<uint8_t> &out_chns) {
          // end state is a working stream
          // check if vectors match
              if (out_chns == m_out_chns) {
                  return;
              }
              auto phys_chn = out_chns.size();
              if (phys_chn != 1 && phys_chn != 2)
              {
                  throw std::runtime_error("Only supports 1 or 2 physical channels");
              }
              else {
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
                  //printf("After startWorker\n");
              }
          }
          inline void restart() {
              for (int i = 0; i < n_phys_chn; ++i) {
                  //printf("Stopping stream %u\n", m_out_chns[i]);
                  (*m_stm_mngrs[m_out_chns[i]]).stop_streams();
                  (*m_stm_mngrs[m_out_chns[i]]).stop_worker();
                  (*m_stm_mngrs[m_out_chns[i]]).reset_streams_out();
                  (*m_stm_mngrs[m_out_chns[i]]).reset_out();
              }
              resetStmManagers();
              n_restarts++; // Incrementing this is the key for letting all players know of a card restart.
              printf("Attempting card restart %u...\n", n_restarts);
              std::this_thread::sleep_for(std::chrono::milliseconds(1000));
              for (int i = 0; i < n_phys_chn; ++i) {
                  (*m_stm_mngrs[m_out_chns[i]]).start_streams();
                  (*m_stm_mngrs[m_out_chns[i]]).start_worker();
              }
              printf("Done with card restart\n");
          }
          inline void startDataTransfer() {
              first_start.store(true, std::memory_order_relaxed);
          }
          inline void resetStmManagers() {
              for (int i = 0; i < n_phys_chn; i++) {
                  m_stm_mngrs[m_out_chns[i]]->reset();
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
              for (int i = 0; i < n_phys_chn; i++) {
                  m_stm_mngrs[m_out_chns[i]]->set_start_trigger(v,t); // may not be useful, but can keep here.
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
          void loadSeq(uint32_t idx, Cmd *p, size_t sz);
          YAML::Node testCompute(size_t nele, size_t buff_sz_nele);
          inline void reqWait()
          {
          }
          inline uint32_t get_end_triggered() {
              uint32_t min_end_triggered = UINT_MAX;
              for (int i = 0; i < n_phys_chn; i++) {
                  min_end_triggered = std::min(min_end_triggered, (*(m_stm_mngrs[m_out_chns[i]])).get_end_triggered());
              }
              return min_end_triggered;
          }
          inline bool get_end_triggered(uint32_t v) {
              // v = 0 is non-triggered, in which case we don't need to delete the map entry
              // and can't use the trigger id to see if previously had finished.
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
          }
          inline void set_trig_to_now(uint32_t v, uint64_t delay) {
          }
      private:
          std::vector<std::unique_ptr<StreamManager>> m_stm_mngrs;
          std::vector<uint32_t> max_chns;

          std::atomic<uint64_t> cmd_underflow{0};
          std::atomic<uint64_t> underflow{0};
          std::atomic<bool> first_start = false;

          uint8_t n_phys_chn{1}; // only supports 1 or 2.
          std::vector<uint8_t> m_out_chns; // output channel when n_phys_chn = 1, otherwise ignored
          const uint8_t n_card_chn = 4; // number of channels on card. hard coded
          uint32_t m_start_trigger_cnt{0};
          uint32_t m_end_trigger_cnt{0};

          uint32_t n_restarts = 0;
          uint32_t restart_id = 0; // keeps track of start_trigger that requested a restart (for triggers that are noticed too late)
          uint32_t last_trig_id = 0; // last finished trig id
      };
}
#endif
