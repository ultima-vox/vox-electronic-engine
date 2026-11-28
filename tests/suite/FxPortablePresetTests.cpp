#include "engine/ComparisonState.h"
#include "engine/PortablePreset.h"
#include <cstdio>

int main(){
    using namespace vox;
    using namespace fx;
    int errors=0;const auto check=[&](bool pass,const char* what){if(!pass){std::printf("FAIL: %s\n",what);++errors;}};
    portable::Sound a,b;
    a.state.program.layers[0].transpose=11;
    a.state.program.layers[1].grains={95,.8f,true};
    a.state.program.layers[0].familyControls={.55f,-.45f,.65f};
    a.state.program.layers[2].grains.travel=-.6f;
    a.state.macros.event={{.3f,.2f,.4f,.5f,.3f,.7f,.8f,1.f},true,.03f,.27f,.43f,.511f};
    a.state.pattern.steps[0]={true,53,97,71,4};
    a.state.effects=effects::pack({effects::Type::reverb,.22f,.71f});
    a.state.macros.setGainRoute(0,-.3f,.02f);
    std::vector<std::byte> bytes,roundtrip;
    check(portable::encode(a,bytes)&&portable::decode(bytes,b)&&portable::encode(b,roundtrip)&&bytes==roundtrip,"deterministic portable round trip");
    check(b.state.program.layers[0].transpose==11&&b.state.program.layers[1].grains==a.state.program.layers[1].grains&&b.state.pattern.steps[0].ratchets==4&&b.state.macros==a.state.macros,"portable authored sections restored");
    // FPRG10 travels nested inside the portable document and roundtrips.
    check(b.state.program.layers[0].familyControls==a.state.program.layers[0].familyControls&&
          b.state.program.layers[2].grains.travel==-.6f,
          "portable preset carries FPRG10 family controls and travel");
    {
        auto document=preset::decode(bytes,portable::product,portable::version);
        const auto* fprg=document.ok()?document.document.find(preset::makeTag("FPRG")):nullptr;
        check(fprg&&!fprg->data.empty()&&fprg->data.front()==std::byte{10},
              "nested FPRG section inside the portable document is v10");
    }
    check(effects::unpack(static_cast<std::uint16_t>(b.state.effects)).type==effects::Type::reverb&&bytes.size()>0,"effect topology retained without automated values");
    for(std::size_t n=0;n<bytes.size();++n)check(!portable::decode(std::span(bytes).first(n),b),"reject every truncation");
    if(!bytes.empty())bytes.back()^=std::byte{1};
    check(!portable::decode(bytes,b)&&b.state.program.layers[0].transpose==11,"bad checksum rejects without mutation");
    {
        a.state.effects=effects::pack({effects::Type::tempoDelay,.42f,.28f});
        portable::Tags tags{"Quarter Past","Progressive","Airy","Timed tail",3,true},read;
        for(const auto* tagged:{static_cast<const portable::Tags*>(nullptr),static_cast<const portable::Tags*>(&tags)}){
            check(portable::encode(a,bytes,tagged)&&portable::decode(bytes,b,&read)&&effects::includesTempoDelay(b.state.effects)&&
                  read.name==(tagged?tags.name:""),"portable v3 tempo delay decodes with optional tags");
            auto document=preset::decode(bytes,portable::product,portable::version);
            check(document.ok()&&document.document.schema==3,"tempo delay uses explicit portable schema v3");
            document.document.schema=tagged?2:1;
            auto mislabeled=preset::encode(document.document);
            check(mislabeled&&!portable::decode(*mislabeled,b)&&effects::includesTempoDelay(b.state.effects),
                  "legacy portable schema rejects the new type without changing output");
        }
    }
    auto valid=preset::encode({compare::productTag,1,{{compare::uiTag,std::vector<std::byte>(8)}}});
    if(!valid)return 1;
    auto bank=compare::Bank{*valid,*valid};auto section=compare::encode(bank);compare::Bank loaded;
    check(compare::decode(section.data,loaded)&&bank==loaded,"two independent slot records");
    auto nested=preset::encode({compare::productTag,1,{{compare::uiTag,std::vector<std::byte>(8)},section}});
    if(!nested)return 1;
    check(!compare::decode(compare::encode({*valid,*nested}).data,loaded)&&loaded==bank,"reject nested banks without mutating output");
    check(!compare::decode(compare::encode({*valid,std::vector<std::byte>(compare::maxSlotBytes+1)}).data,loaded)&&loaded==bank,"reject oversized slots before copying");
    std::printf("FX portable preset and comparison codecs: %d failure(s)\n",errors);return errors?1:0;
}
