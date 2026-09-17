#pragma once

bool audit_runtime_bindings(const std::vector<MemoryRange> &r,const std::vector<MemoryRange> &x,uintptr_t appraisal) {
    bool ok=true;
    const auto check=[&](const char *name,bool valid,uintptr_t address=0) {
        printf("CHECK %s %d RVA=0x%llx\n",name,valid,(unsigned long long)(address ? address-audit_base : 0));ok &= valid;
    };
    const auto targets=resolve_runtime_feature_targets(x);
    check("runtime.mp_cost",targets.mp_cost,targets.mp_cost);
    check("runtime.mp_delta",targets.mp_delta,targets.mp_delta);
    check("runtime.mp_current",targets.mp_current,targets.mp_current);
    check("runtime.mp_max",targets.mp_max,targets.mp_max);
    check("runtime.damage",targets.damage,targets.damage);
    check("runtime.dungeon",targets.dungeon,targets.dungeon);
    check("runtime.encounter",targets.encounter,targets.encounter);
    const auto ad=resolve_ad_context(r,x);check("runtime.ad_context",ad.valid,ad.owner);
    const auto team=resolve_team_context(x);check("runtime.team_context",team.valid,team.helper);
    const auto token=resolve_currency_token_site(x);check("mass.currency_token",token!=0,token);
    const auto shop=resolve_mass_shop(r,x);
    const auto exchange=resolve_mass_appraisal(r,x,appraisal,shop);
    check("mass.token_shop",shop.valid,shop.binder);
    check("mass.appraisal_shop",exchange.valid,exchange.binder);
    check("mass.canonical_label",shop.valid && exchange.valid && shop.commodity_label==exchange.commodity_label,shop.commodity_label);
    check("mass.state_lifetime",shop.valid && exchange.valid && shop.destructors[0] && shop.destructors[1] &&
          exchange.destructors[0] && exchange.destructors[1],shop.destructors[0]);
    const auto speed=resolve_director_speed(r,x);check("director.delta",speed.valid,speed.function);
    return ok;
}

// These models never call any address recovered from an uploaded ARM64 file.
bool audit_resolver_models(const std::vector<MemoryRange> &r,const std::vector<MemoryRange> &x) {
    bool ok=true;
    const auto check=[&](const char *name,bool valid) {printf("CHECK %s %d\n",name,valid);ok &= valid;};
    check("models.missing_capture",injection_capture(0,item_injection_contracts::base_set_core,
        item_injection_contracts::Role::notify_slot)==0 && item_role_address(0,item_catalog::purchase_token_read,item_catalog::Role::bridge_receiver_load)==0);
    constexpr size_t n=sizeof(sig_token_shop_purchase),gap=16;
    std::vector<uint32_t> storage((2*n+gap+3)/4,0);
    auto *bytes=reinterpret_cast<uint8_t *>(storage.data());const auto base=reinterpret_cast<uintptr_t>(bytes);
    const std::vector<MemoryRange> scope{{base,base+storage.size()*4,true,false}};
    const auto resolve=[&] {return resolve_masked_pattern_hook_address(scope,"model",sig_token_shop_purchase,mask_token_shop_purchase,n);};
    check("models.zero_match",resolve()==0);
    std::memcpy(bytes,sig_token_shop_purchase,n);const bool unique=resolve()==base;
    std::memcpy(bytes+n+gap,sig_token_shop_purchase,n);check("models.ambiguous_match",unique && resolve()==0);
    std::memset(bytes,0,n);check("models.moved_target",resolve()==base+n+gap);
    const auto *elf=reinterpret_cast<const Elf64_Ehdr *>(audit_base);
    const auto read=[&](uintptr_t address,void *out,size_t length) {
        if(!range_contains(r,address,length)) return false;std::memcpy(out,reinterpret_cast<void *>(address),length);return true;
    };
    size_t extents=0,gaps=0;bool valid=true;
    for(size_t i=0;i<elf->e_phnum;++i) {
        Elf64_Phdr ph{};if(!read(audit_base+elf->e_phoff+i*sizeof(ph),&ph,sizeof(ph))) {valid=false;break;}
        if(ph.p_type!=PT_GNU_EH_FRAME) continue;
        const auto header=audit_base+ph.p_vaddr;uint32_t count=0;
        if(!read(header+8,&count,4) || ph.p_memsz<12 || count>(ph.p_memsz-12)/8) {valid=false;break;}
        uintptr_t previous_end=0;
        for(size_t index=0;index<count;++index) {
            int32_t entry[2]{};
            if(!read(header+12+index*8,entry,8)) {valid=false;break;}
            const auto start=header+static_cast<intptr_t>(entry[0]),fde=header+static_cast<intptr_t>(entry[1]);uintptr_t end=0;
            if(!elf_unwind::fde_contains(read,fde,start,start,&end) || elf_unwind::fde_contains(read,fde,start,end) ||
               (previous_end && previous_end>start)) {valid=false;break;}
            if(previous_end && start-previous_end>=4) {valid &= injection_function_start(r,x,previous_end)==0;++gaps;}
            previous_end=end;++extents;
        }
    }
    printf("UNWIND_EXTENTS %zu GAPS_REJECTED %zu\n",extents,gaps);
    check("models.unwind_extents",valid && extents>0);
    check("models.unwind_gaps",valid && gaps>0);
    bool same=true;uint64_t random=0x6432a55eULL;
    for(size_t i=0;i<200000;++i) {
        random^=random<<13;random^=random>>7;random^=random<<17;
        const int64_t us=i<32 ? static_cast<int64_t>(i)-16 : static_cast<int64_t>(random%(uint64_t(INT64_MAX)/1000));
        for(float cap:{0.0f,-1.0f,1.0f/120.0f,0.05f,1.0f,1000.0f}) {
            float original=std::fmax(float(us)/1000000.0f,0.0f),expected=std::fmax(float(us)/31250.0f,0.0f);
            if(cap>0) {original=std::min(original,cap);expected=std::min(expected,cap);}
            const auto actual=scaled_director_delta(original,cap);same &= std::memcmp(&actual,&expected,4)==0;
        }
    }
    check("models.speed_math",same);
    check("models.lua_registration_rows",audit_lua_registration_rows());
    return ok;
}
