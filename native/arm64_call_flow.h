#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

// Install-time provenance only. Unknown writes discard their destination;
// AAPCS64 calls discard caller-saved registers. Stack denotes an address derived
// from SP, not a particular stack offset or a value loaded from the stack.
namespace arm64_flow {
struct CallFlow {
    enum Kind { Unknown, Self, Delta, Table, Method, Stack };
    struct Value {
        Kind kind=Unknown;
        size_t slot=0;
        bool operator==(const Value &) const = default;
    };
    Value v[32]{};
    CallFlow() {v[0]={Self};v[1]={Delta};}
    static bool move(uint32_t w,unsigned *rd,unsigned *rn) {
        if((w&0xffe0ffe0)!=0xaa0003e0) return false;
        *rd=w&31;*rn=(w>>16)&31;return *rd!=31;
    }
    static bool branch(uint32_t w) {
        return (w&0x7c000000)==0x14000000 || (w&0xff000010)==0x54000000 ||
            (w&0x7e000000)==0x34000000 || (w&0x7e000000)==0x36000000 ||
            (w&0xfe000000)==0xd6000000;
    }
    void volatile_reset() {for(unsigned i=0;i<=18;++i)v[i]={};v[30]={};}
    bool step(uint32_t w) {
        unsigned rd=0,rn=0;
        if(move(w,&rd,&rn)) {v[rd]=rn==31?Value{}:v[rn];return true;}
        if((w&0xbf000000)==0x91000000) { // 64-bit ADD/SUB immediate, no flags
            rd=w&31;rn=(w>>5)&31;
            if(rd!=31)v[rd]=rn==31 || v[rn].kind==Stack ? Value{Stack} : Value{};
            return true;
        }
        if((w&0xffc00000)==0xf9400000) { // LDR Xt, [Xn,#unsigned offset]
            rd=w&31;rn=(w>>5)&31;const auto base=v[rn];
            const size_t field=((w>>10)&4095)*8;
            if(rd!=31)v[rd]=base.kind==Self && !field ? Value{Table} :
                base.kind==Table ? Value{Method,field} : Value{};
            return true;
        }
        if(branch(w)) {volatile_reset();return true;}
        const auto mode=(w>>10)&3,base=(w>>5)&31;
        if((w&0x3a000000)==0x38000000) { // single-register loads/stores and LSE
            const auto opc=(w>>22)&3;
            // Sign-extending loads use opc=2/3. LSE RMW operations with opc=0
            // also write Rt; ordinary stores preserve their source register.
            if(!(w&(1U<<26)) && (opc || ((w&(1U<<21)) && mode==0)))v[w&31]={};
            if((w&0x3b200000)==0x38000000 && (mode==1 || mode==3))v[base]={};
            v[31]={};return true;
        }
        if((w&0x3a000000)==0x28000000) { // pairs, including LDPSW
            if((w&(1U<<22)) && !(w&(1U<<26))) {v[w&31]={};v[(w>>10)&31]={};}
            const auto pair_mode=(w>>23)&3;
            if(pair_mode==1 || pair_mode==3)v[base]={};
            v[31]={};return true;
        }
        if((w&0x3f000000)==0x08000000) { // exclusive loads, status results and CAS
            v[w&31]={};v[(w>>10)&31]={};v[(w>>16)&31]={};
            v[31]={};return true;
        }
        if((w&0x3b000000)==0x18000000) { // literal loads
            if(!(w&(1U<<26)))v[w&31]={};
            v[31]={};return true;
        }
        // Data processing (including W writes), system-register reads and
        // SIMD-to-GPR transfers. Discarding a SIMD destination's GPR alias is
        // conservative; it cannot manufacture pointer provenance.
        if((w&0x1c000000)==0x10000000 || (w&0x0e000000)==0x0a000000 ||
           (w&0xffe00000)==0xd5200000 || (w&0x1e000000)==0x1e000000 ||
           (w&0x1e000000)==0x0e000000) {
            v[w&31]={};v[31]={};return true;
        }
        if(w==0xd503201f || (w&0xffffff3f)==0xd503241f)return true; // NOP / BTI
        for(auto &value:v)value={};
        return false; // unsupported instruction semantics
    }
    bool merge(const CallFlow &other) {
        bool changed=false;
        for(size_t i=0;i<32;++i) if(v[i].kind!=Unknown && v[i]!=other.v[i]) {
            v[i]={};changed=true;
        }
        return changed;
    }
};

struct VirtualCall {size_t word=0,slot=0;};

// Follow reachable control flow to a fixed point before accepting a call. A
// receiver, method or X8 result pointer must agree on every incoming path.
// Direct branches do not clobber registers; calls obey the ABI. No game code runs.
inline std::vector<VirtualCall> self_sret_calls(const std::vector<uint32_t> &code) {
    if(code.empty() || code.size()>1024)return {};
    struct State {CallFlow flow;bool reached=false;};
    std::vector<State> states(code.size());
    std::vector<size_t> work{0};
    states[0].reached=true;states[0].flow.v[1]={};
    auto propagate=[&](size_t to,const CallFlow &flow) {
        auto &s=states[to];
        if(!s.reached) {s.flow=flow;s.reached=true;work.push_back(to);}
        else if(s.flow.merge(flow))work.push_back(to);
    };
    auto signed_value=[](uint32_t value,unsigned bits) {
        return int64_t(value)-(int64_t(value>>(bits-1))<<bits);
    };
    size_t steps=0;
    while(!work.empty()) {
        if(++steps>code.size()*64)return {};
        const size_t at=work.back();work.pop_back();
        const auto w=code[at];auto flow=states[at].flow;
        const bool jump=(w&0xfc000000)==0x14000000;
        const bool cond=(w&0xff000010)==0x54000000 || (w&0x7e000000)==0x34000000;
        const bool test=(w&0x7e000000)==0x36000000;
        if(jump || cond || test) {
            const int64_t delta=jump ? signed_value(w&0x3ffffff,26) :
                test ? signed_value((w>>5)&0x3fff,14) : signed_value((w>>5)&0x7ffff,19);
            const int64_t target=int64_t(at)+delta;
            if(target>=0 && target<int64_t(code.size()))propagate(size_t(target),flow);
            else if(!jump)return {}; // unsupported cross-owner conditional edge
            if(jump)continue;
        } else if((w&0xfffffc1f)==0xd65f0000 || (w&0xfffffc1f)==0xd61f0000) {
            continue; // RET or tail BR
        } else {
            // An unrecognized register branch (e.g. pointer authentication)
            // cannot be interpreted as ordinary fall-through.
            if(CallFlow::branch(w) && (w&0xfc000000)!=0x94000000 &&
               (w&0xfffffc1f)!=0xd63f0000)return {};
            if(!flow.step(w))return {};
        }
        if(at+1<code.size())propagate(at+1,flow);
    }
    std::vector<VirtualCall> calls;
    for(size_t at=0;at<code.size();++at) {
        const auto w=code[at];const auto &s=states[at];
        if(!s.reached || (w&0xfffffc1f)!=0xd63f0000)continue;
        const auto method=s.flow.v[(w>>5)&31];
        if(method.kind==CallFlow::Method && s.flow.v[0].kind==CallFlow::Self &&
           s.flow.v[8].kind==CallFlow::Stack && method.slot && method.slot<4096 && method.slot%8==0)
            calls.push_back({at,method.slot});
    }
    return calls;
}
} // namespace arm64_flow
