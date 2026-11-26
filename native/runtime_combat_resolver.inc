// Discover owners from RTTI/assertion semantics and ELF unwind boundaries first.
// Instruction decoding below validates local data flow, never an image-wide
// byte signature or a per-version address/field table.
bool read_range_ptr(const std::vector<MemoryRange> &,uintptr_t,uintptr_t *);
bool decode_ldr_s_unsigned(uint32_t,int,int,uintptr_t *);
bool decode_ldr_b_unsigned(uint32_t,int,int,uintptr_t *);
bool decode_str_b_unsigned(uint32_t,int,int,uintptr_t *);
bool decode_ldr_x_unsigned(uint32_t,int,int,uintptr_t *);
bool decode_str_x_unsigned(uint32_t,int,int,uintptr_t *);
bool decode_add_immediate_offset(uint32_t,int,int,uintptr_t *);

// Reuse each method's unwind extent through the discovery passes. Imported
// pure-virtual entries may lie outside libapp; the next RTTI pointer ends the
// table. An unreadable or unterminated table is not a partial success.
struct CombatMethod { uintptr_t start=0,end=0; };
std::vector<CombatMethod> combat_virtual_methods(const std::vector<MemoryRange> &r,
        const std::vector<MemoryRange> &x,uintptr_t table) {
    std::vector<CombatMethod> methods;
    if(!table) return methods;
    for(size_t slot=0;slot<2048;slot+=sizeof(uintptr_t)) {
        uintptr_t function=0,end=0;
        if(!read_range_ptr(r,table+slot,&function)) return {};
        if(!range_contains(x,function,4)) {
            if(function && range_contains(r,function,8)) return methods;
            continue;
        }
        if(std::any_of(methods.begin(),methods.end(),[&](const auto &m) { return m.start==function; })) continue;
        if(injection_function_start(r,x,function,&end)!=function) end=0;
        methods.push_back({function,end});
    }
    return {};
}

// Install-time decoding uses the captured image, with a fault-safe fallback for
// isolated resolver tests. No process-memory reads are added to combat hooks.
uint32_t combat_word(uintptr_t address) {
    uint32_t value=0;
    if(const auto *snapshot=active_scan_snapshot()) return snapshot->read(address,&value,sizeof(value)) ? value : 0;
    return vm_read_partial(address,&value,sizeof(value))==sizeof(value) ? value : 0;
}
bool combat_call_target(uintptr_t address,uintptr_t *target) {
    const uint32_t word=combat_word(address);
    if((word&0xFC000000U)!=0x94000000U) return false;
    *target=address+intptr_t(int32_t(word<<6)>>6)*4;
    return true;
}


EncounterLayout resolve_encounter_context(const std::vector<MemoryRange> &r,const std::vector<MemoryRange> &x) {
    const auto owners=context_named_functions(r,x,"bool toybox::FieldPosition::judgeEncount()");
    if(owners.size()!=1) return {};
    const auto owner=owners.front();uintptr_t end=0;
    if(injection_function_start(r,x,owner,&end)!=owner || end-owner>8192) return {};
    std::vector<uintptr_t> thresholds;
    // The native initial guard rejects maps with a zero/negative encounter
    // interval before consulting their road/region data.
    for(uintptr_t pc=owner;pc+12<=std::min(end,owner+128);pc+=4) {
        uintptr_t field=0;const auto load=combat_word(pc);const auto reg=load&31;
        if(decode_ldr_s_unsigned(load,0,-1,&field) && combat_word(pc+4)==(0x1E202008U|(reg<<5)) &&
           (combat_word(pc+8)&0xFF00001FU)==0x54000009U) thresholds.push_back(field);
    }
    if(thresholds.size()!=1) return {};
    std::vector<EncounterLayout> candidates;
    for(uintptr_t pc=owner+4;pc+8<=end;pc+=4) {
        const uint32_t compare=combat_word(pc),load=combat_word(pc-4),branch=combat_word(pc+4);
        if((compare&0xFFE0FC1FU)!=0x1E202000U || // FCMP Sn, Sm
           (load&0xFFC00000U)!=0x2D400000U || // LDP St, St2, [Xn,#offset]
           (branch&0xFF00001FU)!=0x5400000AU) continue; // B.GE
        const auto threshold=load&31,progress=(load>>10)&31,base=(load>>5)&31;
        const auto field=int32_t(load<<10)>>25; // signed imm7
        if(field<0 || uintptr_t(field*4)!=thresholds.front() || threshold==progress ||
           ((compare>>5)&31)!=progress || ((compare>>16)&31)!=threshold) continue;
        const uintptr_t success=pc+4+(intptr_t(int32_t(branch<<8)>>13)*4);
        if(success<owner || success+12>end ||
           combat_word(success)!=(0xAA0003E0U|(base<<16))) continue; // MOV X0, Xself
        const uint32_t store=combat_word(success+4);uintptr_t finish=0;
        if((store&0xFFC003FFU)!=(0xBD000000U|(base<<5)|threshold) ||
           uintptr_t(((store>>10)&4095)*4)!=thresholds.front()+4 ||
           !combat_call_target(success+8,&finish) || !range_contains(x,finish,4)) continue;
        candidates.push_back({owner,pc,threshold,progress,true});
    }
    return candidates.size()==1 ? candidates.front() : EncounterLayout{};
}

struct PlayerHpStore {uintptr_t address=0;uint32_t self_reg=0,value_reg=0;};
constexpr size_t kMaxPlayerHpStores=16;
struct PlayerHealthLayout {
    uintptr_t table=0,domain_change=0,user_set=0,secure_get=0,delta_method=0;
    size_t hp_field=0,user_hp_field=0;
    std::vector<PlayerHpStore> stores;
    bool valid=false;
};
PlayerHealthLayout resolve_player_health(const std::vector<MemoryRange> &r,const std::vector<MemoryRange> &x) {
    PlayerHealthLayout out;
    out.table=unique_primary_vtable(r,x,"N6toybox6battle12PCUnitStatusE");
    const auto domain=context_named_functions(r,x,"void toybox::DomainPCStatus::changeHP(int, int)");
    if(!out.table || domain.size()!=1) return {};
    out.domain_change=domain.front();uintptr_t end=0;
    if(injection_function_start(r,x,out.domain_change,&end)!=out.domain_change || end-out.domain_change>4096) return {};
    std::vector<uintptr_t> setters;
    for(uintptr_t at=out.domain_change;at+16<=end;at+=4) {
        uintptr_t field=0,target=0;
        if(decode_ldr_x_unsigned(combat_word(at),19,8,&field) && combat_word(at+4)==0x2A0003E1U &&
           combat_word(at+8)==0xAA0803E0U && combat_call_target(at+12,&target)) push_unique(setters,target);
    }
    if(setters.size()!=1) return {};
    out.user_set=setters.front();
    if(injection_function_start(r,x,out.user_set,&end)!=out.user_set || end-out.user_set>2048) return {};
    size_t secure_links=0;
    for(uintptr_t at=out.user_set;at+32<=end;at+=4) {
        uintptr_t field=0,get=0,set=0;
        // Assignment into this player's secured HP value, through the same
        // host decoder/encoder used for the temporary argument value.
        if(!decode_add_immediate_offset(combat_word(at),20,19,&field) || combat_word(at+4)!=0xEB16029FU ||
           (combat_word(at+8)&0xFF00001FU)!=0x54000000U || combat_word(at+12)!=0x910003E0U ||
           !combat_call_target(at+16,&get) || combat_word(at+20)!=0x2A0003E1U ||
           combat_word(at+24)!=0xAA1403E0U || !combat_call_target(at+28,&set) ||
           !range_contains(x,get,4) || !range_contains(x,set,4)) continue;
        out.user_hp_field=field;out.secure_get=get;++secure_links;
    }
    if(secure_links!=1 || !out.user_hp_field || out.user_hp_field>4096) return {};
    const auto methods=combat_virtual_methods(r,x,out.table);
    std::vector<CombatMethod> mutations;
    for(const auto &method:methods) {
        if(!method.end || method.end-method.start>16384) continue;
        for(uintptr_t at=method.start;at+4<=method.end;at+=4) {
            uintptr_t target=0;
            if(combat_call_target(at,&target) && target==out.domain_change) {
                mutations.push_back(method);break;
            }
        }
    }
    if(mutations.size()!=1) return {};
    const auto mutation=mutations.front();
    out.delta_method=mutation.start;
    std::vector<uintptr_t> fields;
    for(const auto &method:methods) {
        const auto getter=method.start;
        uintptr_t field=0;
        if(!range_contains(x,getter,8) || !decode_ldr_x_unsigned(combat_word(getter),0,0,&field) ||
           combat_word(getter+4)!=0xD65F03C0U || !field || field>8192) continue;
        for(uintptr_t at=mutation.start;at+4<=mutation.end;at+=4) {
            uintptr_t stored=0;
            if(decode_str_x_unsigned(combat_word(at),19,-1,&stored) && stored==field) push_unique(fields,field);
        }
    }
    if(fields.size()!=1) return {};
    out.hp_field=fields.front();
    // Audit every virtual PC-status method for writes to the getter's field.
    // This includes signed deltas and percentage/direct HP changes. Startup
    // construction remains native; enemy-status methods are never candidates.
    for(const auto &method:methods) {
        const auto fn=method.start,end=method.end;
        if(!end || end-fn>16384) continue;
        for(uintptr_t at=fn;at+4<=end;at+=4) {
            uintptr_t field=0;const auto word=combat_word(at);
            if(!decode_str_x_unsigned(word,-1,-1,&field) || field!=out.hp_field) continue;
            bool self_saved=false;
            for(uintptr_t entry=fn;entry<at && entry<fn+128;entry+=4)
                self_saved |= combat_word(entry)==0xAA0003F3U; // MOV X19, X0
            if(!self_saved || ((word>>5)&31)!=19 || (word&31)==19 || (word&31)==31) return {};
            out.stores.push_back({at,19,word&31});
        }
    }
    if(out.stores.size()<2 || out.stores.size()>kMaxPlayerHpStores) return {};
    out.valid=true;return out;
}

using CombatFlow=arm64_flow::CallFlow;

struct CombatNegation {uintptr_t site=0;unsigned source=0;};
CombatNegation combat_negative_argument(uintptr_t owner,uintptr_t call) {
    unsigned wanted=1;
    // Follow MOV aliases back to the NEG defining argument X1. A branch/call or
    // any intervening write to that value invalidates the chain.
    for(auto at=call;at>=owner+4 && call-at<32;) {
        at-=4;const auto w=combat_word(at);unsigned rd=0,rn=0;
        if(CombatFlow::branch(w))return {};
        if(CombatFlow::move(w,&rd,&rn) && rd==wanted) {wanted=rn;continue;}
        if((w&0xffe0ffe0)==0xcb0003e0 && (w&31)==wanted)
            return {at,(w>>16)&31}; // NEG Xd, Xn (64-bit, unshifted)
        CombatFlow flow;for(auto &v:flow.v)v={};flow.v[wanted]={CombatFlow::Delta};
        flow.step(w);if(flow.v[wanted].kind!=CombatFlow::Delta)return {};
    }
    return {};
}

bool combat_returns_constant(const std::vector<MemoryRange> &x,uintptr_t function,unsigned value) {
    if(!range_contains(x,function,8))return false;
    const auto w=combat_word(function);
    return (w==(0x52800000U|(value<<5)) || (!value && w==0x2a1f03e0)) &&
        combat_word(function+4)==0xd65f03c0;
}

bool combat_hp_delta_helper(const std::vector<MemoryRange> &r,const std::vector<MemoryRange> &x,
        uintptr_t helper,const PlayerHealthLayout &health) {
    uintptr_t end=0;
    if(!health.valid || injection_function_start(r,x,helper,&end)!=helper || end-helper>4096)return false;
    CombatFlow flow;unsigned mutations=0;
    for(auto at=helper;at<end;at+=4) {
        const auto w=combat_word(at);
        if((w&0xfffffc1f)==0xd63f0000) {
            const auto method=flow.v[(w>>5)&31];uintptr_t target=0;
            if(method.kind==CombatFlow::Method && flow.v[0].kind==CombatFlow::Self &&
               flow.v[1].kind==CombatFlow::Delta && method.slot<2048 &&
               read_range_ptr(r,health.table+method.slot,&target) && target==health.delta_method)++mutations;
        } else if(CombatFlow::branch(w) && (w&0xfc000000)!=0x94000000)break;
        flow.step(w);
    }
    return mutations==1;
}

struct TeamLayout {
    uintptr_t call=0,lethal=0,helper=0,owner=0;
    size_t kind_slot=0;
    uint32_t damage_reg=0,actor_reg=19;
    bool valid=false;
};
TeamLayout resolve_team_context(const std::vector<MemoryRange> &r,const std::vector<MemoryRange> &x,
        const PlayerHealthLayout &health) {
    const auto table=unique_primary_vtable(r,x,"N6toybox6battle4UnitE");
    const auto enemy=unique_primary_vtable(r,x,"N6toybox6battle9EnemyUnitE");
    const auto methods=combat_virtual_methods(r,x,table);
    if(!health.valid || !enemy || methods.empty())return {};
    std::vector<size_t> kind_slots;
    for(size_t slot=0;slot<2048;slot+=8) {
        uintptr_t base=0,other=0;
        if(!read_range_ptr(r,table+slot,&base))return {};
        if(!range_contains(x,base,8)) {
            if(base && range_contains(r,base,8))break;
            continue;
        }
        if(read_range_ptr(r,enemy+slot,&other) &&
           combat_returns_constant(x,base,0) && combat_returns_constant(x,other,2))kind_slots.push_back(slot);
    }
    std::vector<TeamLayout> candidates;
    std::unordered_map<uintptr_t,bool> helpers;
    for(const auto &method:methods) {
        const auto owner=method.start,end=method.end;
        if(!end || end-owner>16384)continue;
        std::vector<size_t> used_kind_slots;
        CombatFlow owner_flow;
        for(auto at=owner;at<end;at+=4) {
            const auto w=combat_word(at);
            if((w&0xfffffc1f)==0xd63f0000 && owner_flow.v[0].kind==CombatFlow::Self) {
                const auto value=owner_flow.v[(w>>5)&31];
                if(value.kind==CombatFlow::Method &&
                   std::find(kind_slots.begin(),kind_slots.end(),value.slot)!=kind_slots.end() &&
                   std::find(used_kind_slots.begin(),used_kind_slots.end(),value.slot)==used_kind_slots.end())
                    used_kind_slots.push_back(value.slot);
            }
            owner_flow.step(w);
        }
        if(used_kind_slots.size()!=1)continue;
        for(auto pc=owner;pc<end;pc+=4) {
            uintptr_t helper=0;if(!combat_call_target(pc,&helper))continue;
            const auto commit=combat_negative_argument(owner,pc);
            if(!commit.site || commit.source<19 || commit.source>28)continue;
            auto cached=helpers.find(helper);
            if(cached==helpers.end())cached=helpers.emplace(helper,combat_hp_delta_helper(r,x,helper,health)).first;
            if(!cached->second)continue;
            for(auto sum_call=owner;sum_call+20<=commit.site;sum_call+=4) {
                uintptr_t sum=0;
                if(!combat_call_target(sum_call,&sum) || !range_contains(x,sum,4) ||
                   combat_word(sum_call+4)!=0xf100001f ||
                   (combat_word(sum_call+8)&0xff00001f)!=0x5400000c)continue;
                const auto prediction=combat_negative_argument(owner,sum_call);
                if(!prediction.site || prediction.source!=commit.source)continue;
                const auto one=combat_word(sum_call+12),store=combat_word(sum_call+16);
                const auto actor=(store>>5)&31;uintptr_t dead=0;
                if(actor<19 || actor>28 || actor==commit.source || (one&0xffffffe0)!=0x52800020 ||
                   !decode_str_b_unsigned(store,actor,one&31,&dead))continue;
                const auto branch=sum_call+8+(intptr_t(int32_t(combat_word(sum_call+8)<<8)>>13)*4);
                if(branch!=sum_call+20)continue;
                CombatFlow self;
                for(auto at=owner;at<prediction.site;at+=4)self.step(combat_word(at));
                if(self.v[actor].kind!=CombatFlow::Self)continue;
                // The same damage value must survive prediction until commit.
                CombatFlow damage;for(auto &v:damage.v)v={};damage.v[commit.source]={CombatFlow::Delta};
                for(auto at=prediction.site;at<commit.site;at+=4)damage.step(combat_word(at));
                if(damage.v[commit.source].kind!=CombatFlow::Delta)continue;
                unsigned guards=0;
                for(auto at=owner;at+8<=std::min(end,owner+128);at+=4) {
                    uintptr_t field=0;const auto load=combat_word(at),test=combat_word(at+4);
                    if(!decode_ldr_b_unsigned(load,actor,-1,&field) || field!=dead ||
                       (test&0xff00001f)!=(0x35000000U|(load&31)))continue;
                    const auto target=at+4+(intptr_t(int32_t(test<<8)>>13)*4);
                    if(target>pc && target<end)++guards;
                }
                if(guards==1)candidates.push_back({commit.site,prediction.site,helper,owner,
                    used_kind_slots.front(),commit.source,actor,true});
            }
        }
    }
    return candidates.size()==1?candidates.front():TeamLayout{};
}
TeamLayout resolve_team_context(const std::vector<MemoryRange> &r,const std::vector<MemoryRange> &x) {
    return resolve_team_context(r,x,resolve_player_health(r,x));
}
