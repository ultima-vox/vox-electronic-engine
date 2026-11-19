#pragma once
#include "OfflineRender.h"
#include "SampleDecode.h"
#include "SampleReference.h"
#include <thread>

namespace vox::fx::preview {
// Interleaved immutable frames leased from the same bounded store primitive
// as the granular engine. UI/worker can publish, audio never frees a buffer.
class Bus {
public:
    bool publish(std::span<const float> stereo,std::uint32_t rate){
        if(stereo.size()<4||(stereo.size()&1u))return false;
        std::uint64_t token=0;
        if(!store_.publish(stereo,rate,&token))return false;
        request_.store(token,std::memory_order_release);return true;
    }
    void stop() noexcept { request_.store(0,std::memory_order_release); }
    bool playing() const noexcept {return request_.load(std::memory_order_acquire)!=0;}
    void prepare(double rate) noexcept {
        resetLease();stop();rate_=std::isfinite(rate)&&rate>=8000&&rate<=384000?rate:48000;
        fade_=1.0/std::max(1.0,rate_*.005);
    }
    void mix(float* left,float* right,int count) noexcept {
        const auto requested=request_.load(std::memory_order_acquire);
        if(!lease_.data()&&requested==0)return;
        if(requested!=token_&&(!lease_.data()||gain_==0)){
            resetLease();
            if(requested!=0&&store_.acquire(lease_,requested)){token_=requested;step_=lease_.rate()/rate_;}
        }
        if(!lease_.data())return;
        const auto frames=lease_.frames()/2;
        const auto* data=lease_.data();
        const bool ending=requested!=token_;
        for(int i=0;i<count;++i){
            const auto at=static_cast<std::size_t>(position_);
            if(at>=frames||(ending&&gain_==0)){
                auto expected=token_;
                if(at>=frames)(void)request_.compare_exchange_strong(expected,0,std::memory_order_acq_rel);
                resetLease();break;
            }
            const auto next=std::min(at+1,frames-1);
            const double t=position_-at;
            const auto edge=std::clamp((frames-position_-step_)*fade_/step_,0.,1.);
            gain_=std::clamp(gain_+(ending?-fade_:fade_),0.,edge);
            left[i]=static_cast<float>(left[i]*(1-gain_)+gain_*(data[2*at]+t*(data[2*next]-data[2*at])));
            right[i]=static_cast<float>(right[i]*(1-gain_)+gain_*(data[2*at+1]+t*(data[2*next+1]-data[2*at+1])));
            position_+=step_;
        }
    }
private:
    void resetLease() noexcept {store_.release(lease_);token_=0;position_=gain_=0;}
    sample::Store store_;sample::Store::Lease lease_;
    std::atomic<std::uint64_t> request_{0};
    std::uint64_t token_=0;double position_=0,step_=1,gain_=0,rate_=48000,fade_=1./240;
};
enum class Result { loading,ready,unavailable,cancelled };
class Job {
public:
    static constexpr std::uint32_t rate=48000, maximumFrames=rate*12;
    Job(offline::Capture capture,sample::Reference reference={})
        :worker_([this,capture=std::move(capture),reference=std::move(reference)]() mutable {
            bool ok=false;
            try {ok=render(capture,reference);} catch(...) {}
            result_.store(cancel_.load(std::memory_order_relaxed)?Result::cancelled:(ok?Result::ready:Result::unavailable),std::memory_order_release);
        }){}
    ~Job(){cancel();worker_.join();}
    void cancel() noexcept {cancel_.store(true,std::memory_order_relaxed);}
    Result result() const noexcept { return result_.load(std::memory_order_acquire); }
    bool publish(Bus& bus) const {return result()==Result::ready&&!cancel_.load()&&bus.publish(samples_,rate);}
private:
    bool render(offline::Capture& capture,const sample::Reference& reference){
        const bool needsSample=std::any_of(capture.program.layers.begin(),capture.program.layers.end(),
            [](const auto& l){return l.enabled&&l.family==NoiseFamily::granular;});
        if(needsSample&&reference.kind!=sample::Reference::Kind::builtin){
            if(reference.kind!=sample::Reference::Kind::external)return false;
            const auto path=juce::String::fromUTF8(reference.path.data(),static_cast<int>(reference.path.size()));
            if(!juce::File::isAbsolutePath(path)||!path.endsWithIgnoreCase(".wav"))return false;
            sample::Decoded sample;
            if(!sample::loadWavCancelable(juce::File(path),sample,&cancel_)||sample.sha256!=juce::String(reference.sha256))return false;
            capture.useUserSample=true;capture.sampleRate=sample.rate;capture.sample=std::move(sample.mono);
        }
        capture.settings.sampleRate=rate;capture.settings.format=offline::WavFormat::float32;
        std::vector<std::uint8_t> wav;
        if(!offline::renderWavImpl(capture.program,capture.settings,wav,&cancel_,nullptr,&capture,maximumFrames))return false;
        // Exactly the float32 layout emitted by renderWavImpl above.
        if(wav.size()<64||((wav.size()-56)&7u)!=0)return false;
        samples_.reserve((wav.size()-56)/4);
        for(std::size_t at=56;at<wav.size();at+=4){
            if((at&8191)==56&&cancel_.load(std::memory_order_relaxed))return false;
            std::uint32_t bits=0;for(unsigned i=0;i<4;++i)bits|=static_cast<std::uint32_t>(wav[at+i])<<(8*i);
            samples_.push_back(std::bit_cast<float>(bits));
        }
        return true;
    }
    std::atomic<bool> cancel_{false};std::atomic<Result> result_{Result::loading};
    std::vector<float> samples_;std::thread worker_;
};
} // namespace vox::fx::preview
