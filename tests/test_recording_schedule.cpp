/* SPDX-License-Identifier: MIT */
#include "../src/transport/RecordingSchedule.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
static void check(bool value) {
    if (!value) { std::fputs("Recording schedule test failed\n", stderr); std::exit(1); }
}
int main() {
    RecordingSchedule s;
    RecordingSchedule::Ticket first[RecordingSchedule::slots], next{};
    check(!s.reserve(0, 100, next));
    check(s.begin(100));
    for (unsigned i=0;i<s.slots;++i) {
        check(s.reserve(i,100,first[i])); check(first[i].frame==110+i);
    }
    check(s.pending()==s.slots && !s.begin(100));
    check(!s.reserve(0,100,next));
    // Completions may arrive out of order, but new USB frame ranges may not overlap.
    check(s.completed(first[4]));
    check(s.reserve(4,115,next) && next.frame==110+s.slots);
    check(!s.completed(first[4])); // A duplicate cannot release the new transfer.
    auto invalid=next; ++invalid.frame; check(!s.completed(invalid));
    s.stop();
    check(!s.reserve(4,115,invalid) && !s.begin(200));
    check(s.completed(next));
    for(unsigned i=0;i<s.slots;++i) if(i!=4) check(s.completed(first[i]));
    check(!s.pending() && s.begin(200));
    check(!s.completed(first[0])); // Stale completion from a prior session.
    check(s.reserve(0,200,next) && next.frame==210);
    check(s.failed(next) && !s.running() && s.faults()==1 && !s.pending());
    check(s.begin(300));
    check(!s.reserve(0,310,next) && !s.running() && s.faults()==1);
    check(!s.begin(std::numeric_limits<uint64_t>::max()-9));
    check(s.begin(400));
    check(!s.reserve(s.slots,400,next) && s.pending()==0);
    s.stop();
    // Exercise sustained completion/resubmission without gaps in bus frame numbers.
    check(s.begin(1000));
    uint64_t expected=1010;
    for(unsigned i=0;i<s.slots;++i) { check(s.reserve(i,1000,first[i])); check(first[i].frame==expected++); }
    for(unsigned i=0;i<100000;++i) {
        unsigned slot=i%s.slots; uint64_t now=first[slot].frame+1;
        check(s.completed(first[slot])); check(s.reserve(slot,now,first[slot]));
        check(first[slot].frame==expected++);
    }
    s.stop(); for(auto t:first) check(s.completed(t));
    check(!s.pending() && !s.faults());
    std::puts("Recording scheduler tests passed; no hardware access");
}
