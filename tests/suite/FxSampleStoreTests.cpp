#include "engine/SampleStore.h"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <limits>
#include <thread>
#include <vector>
#ifdef VOX_TEST_INTERPOSE_MALLOC
#include "AllocationProbe.h"
#endif
int main(){
    using namespace vox::fx::sample;
    int failures=0;auto check=[&](bool ok,const char* message){if(!ok){std::printf("FAIL: %s\n",message);++failures;}};
    Store store;Store::Lease a,b,c,d;
    check(!store.acquire(a),"empty store has no implicit fallback");
    const std::vector<float> first(128,.1f),second(64,.2f),third(256,.3f),fourth(42,.4f);
    check(store.publish(first,48000)&&store.acquire(a),"publish/acquire first");
    check(!store.acquire(a),"cannot overwrite a live lease");
    check(a.frames()==first.size()&&a.rate()==48000&&a.data()[0]==.1f,"first lease contents");
    check(store.publish(second,44100)&&store.acquire(b),"publish/acquire second");
    check(store.publish(third,96000)&&store.acquire(c),"publish/acquire third");
    check(!store.publish(fourth,48000),"all versions leased: publish fails rather than evicting");
    check(a.data()[0]==.1f&&b.data()[0]==.2f&&c.data()[0]==.3f,"old leases remain immutable");
#ifdef VOX_TEST_INTERPOSE_MALLOC
    voxProbeBegin();
#endif
    store.release(a);const bool acquired=store.acquire(d);store.release(d);
#ifdef VOX_TEST_INTERPOSE_MALLOC
    const auto allocations=voxProbeEnd();if(voxProbeAvailable())check(allocations==0,"audio acquire and release allocate nothing");
#endif
    check(acquired&&store.publish(fourth,48000),"released slot reusable off audio thread");
    const auto nan=std::vector<float>{std::numeric_limits<float>::quiet_NaN()};
    const std::vector<float> over{1.1f};
    check(!store.publish(nan,48000)&&!store.publish(first,0)&&!store.publish(over,48000)&&
          !store.publish(std::span<const float>{},48000),"invalid sample rejected");
    check(store.acquire(d)&&d.data()[0]==.4f,"invalid publication leaves current unchanged");
    store.clear();Store::Lease missing;check(!store.acquire(missing),"clear disallows new acquisitions");
    check(d.data()[0]==.4f&&b.data()[0]==.2f&&c.data()[0]==.3f,"clear keeps outstanding leases alive");
    store.release(b);store.release(c);store.release(d);
    {
        Store concurrent;std::atomic<bool> done{false};std::atomic<int> errors{0},ready{0};
        const std::vector<float> x(1024,.125f),y(1024,-.625f);
        check(concurrent.publish(x,48000),"stress initial sample");
        const auto reader=[&]{ready.fetch_add(1,std::memory_order_release);
            while(!done.load(std::memory_order_acquire)){
                Store::Lease lease;if(concurrent.acquire(lease)){
                    const auto v=lease.data()[0];const auto rate=lease.rate();
                    if(!((v==.125f&&rate==48000)||(v==-.625f&&rate==44100))||lease.frames()!=1024)++errors;
                    for(std::size_t i=1;i<lease.frames();i+=31)if(lease.data()[i]!=v)++errors;
                    concurrent.release(lease);
                }
            }};
        std::thread readerOne(reader),readerTwo(reader);
        std::thread writer([&]{while(ready.load(std::memory_order_acquire)<2)std::this_thread::yield();
            for(int n=0;n<20000;++n)(void)concurrent.publish((n&1)?x:y,(n&1)?48000:44100);
            done.store(true,std::memory_order_release);});
        writer.join();readerOne.join();readerTwo.join();
        check(errors.load()==0,"concurrent leases never observe mixed or freed samples");
    }
    std::printf("FX sample ownership: %d failure(s)\n",failures);return failures?1:0;
}
