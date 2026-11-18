#include "engine/SampleReference.h"
#include <cstdio>
#include <vector>
int main(){
    using namespace vox::fx::sample;int failures=0;
    auto check=[&](bool b,const char* m){if(!b){std::printf("FAIL: %s\n",m);++failures;}};
    Reference initial;std::vector<std::byte> bytes;Reference decoded;
    check(referencecodec::encode(initial,bytes)&&referencecodec::decode(bytes,decoded)&&decoded==initial,"built-in reference roundtrip");
    Reference external;external.kind=Reference::Kind::external;external.path="/audio/échantillon.wav";
    external.sha256="4ae8cfae71382eeb3df8fb4338a4c13e9dbedba7e1c4bb6ad87fd834c0d17717";
    check(external.valid()&&referencecodec::encode(external,bytes)&&referencecodec::decode(bytes,decoded)&&decoded==external,"external reference roundtrip");
    const auto good=decoded;
    for(std::size_t n=0;n<bytes.size();++n)check(!referencecodec::decode(std::span(bytes).first(n),decoded)&&decoded==good,"truncation transactional");
    auto bad=bytes;bad[0]=std::byte{2};check(!referencecodec::decode(bad,decoded)&&decoded==good,"future schema rejected");
    bad=bytes;bad.push_back(std::byte{});check(!referencecodec::decode(bad,decoded),"trailing bytes rejected");
    auto invalid=external;invalid.sha256[0]='Z';check(!invalid.valid(),"invalid digest rejected");
    invalid=external;invalid.path=std::string("a\xc0\x80",3);check(!invalid.valid(),"overlong UTF-8 rejected");
    invalid=external;invalid.path=std::string("a\0b",3);check(!invalid.valid(),"embedded NUL rejected");
    invalid=initial;invalid.path="stale";check(!invalid.valid(),"built-in reference carries no hidden file");
    invalid=external;invalid.path=std::string(4097,'x');check(!invalid.valid(),"oversize path rejected");
    std::printf("FX sample reference state: %d failure(s)\n",failures);return failures?1:0;
}
