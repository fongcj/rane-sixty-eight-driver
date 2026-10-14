/* SPDX-License-Identifier: MIT */
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
namespace sixtyfour {
// Fixed-size telemetry. No logging, allocation, locks or audio data on I/O paths.
// Totals reset before a new USB session; readers receive approximate snapshots.
class DuplexDiagnostics {
public:
    enum Metric { inputBatches,outputBatches,inputFrames,outputFrames,inputScheduleFaults,
        outputScheduleFaults,inputTransferErrors,outputTransferErrors,decodeErrors,
        outputSlotBusy,workerStalls,cleanupFailures,startFailures,writeCalls,writeFailures,
        invalidWriteTimes,maxWriteGapNs,maxInputCallbackGapNs,maxOutputCallbackGapNs,
        lastFailureStage,lastIOReturn,outputDeadlineMisses,maxOutputDeadlineLateBusFrames,
        outputClockFailures,outputPrepareFailures,workerQosResult,count };
    inline static constexpr const char* names[count]={
        "input_batches","output_batches","input_frames","output_frames","input_schedule_faults",
        "output_schedule_faults","input_transfer_errors","output_transfer_errors","decode_errors",
        "output_slot_busy","worker_stalls","cleanup_failures","start_failures","write_calls","write_failures",
        "invalid_write_times","max_write_gap_ns","max_input_callback_gap_ns","max_output_callback_gap_ns",
        "last_failure_stage","last_ioreturn","output_deadline_misses","max_output_deadline_late_bus_frames",
        "output_clock_failures","output_prepare_failures","worker_qos_result"};
    // Failure stages: 1=open/start, 2=input scheduling, 3=input transfer/decode,
    // 4=output scheduling, 5=output transfer, 6=worker stall, 7=cleanup.
    void add(Metric m,uint64_t n=1) {values[m].fetch_add(n,std::memory_order_relaxed);}
    void failure(uint64_t stage,uint32_t code=0) {
        values[lastFailureStage].store(stage,std::memory_order_relaxed);
        values[lastIOReturn].store(code,std::memory_order_relaxed);
    }
    // Each maximum has exactly one producer. Readers do not mutate telemetry.
    void maximum(Metric m,uint64_t n) {auto old=values[m].load(std::memory_order_relaxed);if(n>old)values[m].store(n,std::memory_order_relaxed);}
    void reset() {for(auto& v:values)v.store(0,std::memory_order_relaxed);}
    std::array<uint64_t,count> snapshot() const {
        std::array<uint64_t,count> result{};
        for(unsigned i=0;i<count;++i)result[i]=values[i].load(std::memory_order_relaxed);
        return result;
    }
private:
    static_assert(std::atomic<uint64_t>::is_always_lock_free);
    std::array<std::atomic<uint64_t>,count> values{};
};
constexpr uint32_t diagnosticsSelector=0x72363464; // 'r64d', read-only CFString JSON
}
