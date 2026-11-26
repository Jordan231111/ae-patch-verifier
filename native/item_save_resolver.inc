// Save tracking uses the game's named request metadata and its string-based DB
// API. No GameServerRequest, callback-capture, or GameDB field layout is imported.
struct ItemSaveBindings {
    uintptr_t enqueue=0, store=0, get=0, put=0, success=0, success_end=0;
    uintptr_t busy=0,serialize=0;
    bool ready() const { return enqueue && store && get && put && success && success_end && busy && serialize; }
};
ItemSaveBindings g_item_save_bindings;

struct SaveMetadataResolver {
    const std::vector<MemoryRange> &r,&x;
    struct Value {
        enum Kind { Unknown, Constant, Stack, Result } kind=Unknown;
        int64_t value=0;
        bool operator==(const Value &other) const { return kind==other.kind && value==other.value; }
    };
    struct Call {
        uintptr_t owner=0, site=0, getter=0, method=0, constructor=0;
        bool operator==(const Call &b) const {
            return owner==b.owner && site==b.site && getter==b.getter && method==b.method && constructor==b.constructor;
        }
    };
    static void arithmetic(uint32_t w,Value (&v)[32]) {
        const unsigned rd=w&31,rn=(w>>5)&31;
        const auto op=w&0xff000000;
        if(op==0x91000000 || op==0xd1000000) {
            int64_t amount=(w>>10)&4095;
            if(w&(1U<<22)) amount<<=12;
            if(op==0xd1000000) amount=-amount;
            v[rd]=v[rn];
            if(v[rd].kind==Value::Stack || v[rd].kind==Value::Constant) v[rd].value+=amount;
            else v[rd]={};
        } else if((w&0xffe0ffe0)==0xaa0003e0 && rd!=31)
            v[rd]=((w>>16)&31)==31 ? Value{Value::Constant,0} : v[(w>>16)&31];
        else if((w&0xffc003e0)==0xa98003e0 || (w&0xffc003e0)==0xa8c003e0) {
            // Standard AAPCS64 stack adjustment, not a host object field.
            if(v[31].kind==Value::Stack) v[31].value+=item_catalog::signed_bits((w>>15)&127,7)*8;
        }
    }
    std::vector<Call> calls(const char *literal,uintptr_t required_owner=0) const {
        std::vector<Call> found;
        for(auto text:find_pattern(r,reinterpret_cast<const uint8_t *>(literal),std::strlen(literal)+1)) {
            for(auto ref:find_pc_relative_refs(x,text)) {
                uintptr_t end=0;
                const auto owner=injection_function_start(r,x,ref,&end);
                if(!owner || (required_owner && owner!=required_owner)) continue;
                Value frame[32]{};frame[31]={Value::Stack,0};
                // Establish SP/FP coordinates from the actual prologue.
                for(auto pc=owner;pc<std::min(end,owner+128);pc+=4) {
                    uintptr_t target=0;
                    if(decode_branch_target(pc,true,&target)) break;
                    arithmetic(read_u32(pc),frame);
                }
                Value v[32]{};v[31]=frame[31];v[29]=frame[29];
                std::vector<std::pair<Value,uintptr_t>> keys;
                const auto begin=ref-owner>128 ? ref-128 : owner;
                if(begin==owner) {v[31]={Value::Stack,0};v[29]={};}
                for(auto pc=begin;pc<std::min(end,ref+192);pc+=4) {
                    const auto w=read_u32(pc);
                    uintptr_t page=0,target=0;uint32_t reg=0;
                    if(decode_adrp_page(w,pc,&page,&reg)) v[reg]={Value::Constant,static_cast<int64_t>(page)};
                    else if((w&0x9f000000)==0x10000000) {
                        const auto imm=item_catalog::signed_bits(((w>>5)&0x7ffff)*4+((w>>29)&3),21);
                        v[w&31]={Value::Constant,static_cast<int64_t>(pc)+imm};
                    } else if(decode_branch_target(pc,true,&target)) {
                        if(v[0].kind==Value::Stack && v[1].kind==Value::Constant &&
                           static_cast<uintptr_t>(v[1].value)==text)
                            keys.emplace_back(v[0],target);
                        if(v[0].kind==Value::Result && v[1].kind==Value::Stack && v[2].kind==Value::Stack) {
                            for(const auto &key:keys) if(v[1]==key.first &&
                                injection_function_start(r,x,target)==target) {
                                const Call call{owner,pc,static_cast<uintptr_t>(v[0].value),target,key.second};
                                if(std::find(found.begin(),found.end(),call)==found.end()) found.push_back(call);
                            }
                        }
                        for(size_t n=0;n<=18;++n) v[n]={};
                        v[0]={Value::Result,static_cast<int64_t>(target)};
                    } else {
                        arithmetic(w,v);
                        const bool known_arithmetic=(w&0xff000000)==0x91000000 || (w&0xff000000)==0xd1000000 ||
                            (w&0xffe0ffe0)==0xaa0003e0;
                        // A new compiler may reuse a register differently. Lose
                        // provenance on unhandled writes instead of pretending
                        // the old pointer/getter result survived them.
                        if((w&31)!=31 && !known_arithmetic && ((w&0x1c000000)==0x10000000 ||
                            (w&0x0e000000)==0x0a000000 || (w&0xfff00000)==0xd5300000)) v[w&31]={};
                        // Loads replace their destination; do not retain a stale getter.
                        if((w&31)!=31 && ((w&0x3b400000)==0x39400000 || (w&0x3b600000)==0x38400000)) v[w&31]={};
                        if((w&0x3b400000)==0x29400000 && !(w&(1U<<26))) {
                            if((w&31)!=31) v[w&31]={};
                            if(((w>>10)&31)!=31) v[(w>>10)&31]={};
                        }
                        const auto mode=(w>>10)&3,rn=(w>>5)&31;
                        if(rn!=31 && (w&0x3b200000)==0x38000000 && (mode==1 || mode==3)) v[rn]={};
                    }
                }
            }
        }
        return found;
    }
    bool calls_function(uintptr_t owner,uintptr_t target) const {
        uintptr_t end=0;
        if(injection_function_start(r,x,owner,&end)!=owner) return false;
        for(auto pc=owner;pc<end;pc+=4) {
            uintptr_t callee=0;
            if((decode_branch_target(pc,true,&callee)||decode_branch_target(pc,false,&callee)) && callee==target) return true;
        }
        return false;
    }
};

ItemSaveBindings resolve_item_save(const std::vector<MemoryRange> &r,const std::vector<MemoryRange> &x,uintptr_t sync) {
    const SaveMetadataResolver resolver{r,x};
    const auto fail=[](int line) {
#if defined(AE_TEST_SYMBOL_RESOLVER) || defined(AE_DEVICE_LAB)
        std::fprintf(stderr,"SAVE_UNRESOLVED %d\n",line);
#else
        (void)line;
#endif
        return ItemSaveBindings{};
    };
    const auto enqueue=resolver.calls("user_data/push",sync);
    const auto read=resolver.calls("encryption/aes_iv",sync);
    auto success=resolver.calls("request_last_success");
    if(read.size()==1) success.erase(std::remove_if(success.begin(),success.end(),[&](const auto &call) {
        return call.getter!=read[0].getter || call.method==read[0].method;
    }),success.end());
#if defined(AE_TEST_SYMBOL_RESOLVER) || defined(AE_DEVICE_LAB)
    std::fprintf(stderr,"SAVE_MATCHES enqueue=%zu read=%zu success=%zu\n",enqueue.size(),read.size(),success.size());
#endif
    if(enqueue.size()!=1 || read.size()!=1 || success.size()!=1) return fail(__LINE__);
    if(success[0].getter!=read[0].getter || success[0].constructor!=read[0].constructor ||
       enqueue[0].constructor!=read[0].constructor || success[0].method==read[0].method) return fail(__LINE__);
    const auto sequence=resolver.calls("request_sequence");
    size_t core_count=0;uintptr_t core=0;
    for(const auto &call:sequence) if(call.getter==read[0].getter && call.method==read[0].method &&
        call.constructor==read[0].constructor && resolver.calls_function(enqueue[0].method,call.owner)) {++core_count;core=call.owner;}
    if(core_count!=1) return fail(__LINE__);
    ItemSaveBindings out{enqueue[0].method,read[0].getter,read[0].method,success[0].method,success[0].owner,0};
    if(injection_function_start(r,x,out.success,&out.success_end)!=out.success ||
       injection_function_start(r,x,out.store)!=out.store) return fail(__LINE__);
    // The queue serializes its JSON argument before encrypting /body. Reuse that
    // ABI (std::string return in X8, body in X0, indent=-1 in W1) rather than
    // importing a private JSON layout or parsing the encrypted durable body.
    using W=item_injection_contracts::Word;using R=item_injection_contracts::Role;
    static constexpr W dump[] = {
        {0x910003e8,0xffc003ff,R::none},{0xaa1c03e0,0xffffffff,R::none},
        {0x12800001,0xffffffff,R::none},{0x94000000,0xfc000000,R::assign},
        {0x14000000,0xfc000000,R::none},{0x90000001,0x9f00001f,R::none},
        {0x91000021,0xffc003ff,R::none},{0x910003e0,0xffc003ff,R::none},
        {0x94000000,0xfc000000,R::none},
    };
    uintptr_t core_end=0,constructor=0;
    if(injection_function_start(r,x,core,&core_end)!=core)return fail(__LINE__);
    const auto serializer=injection_contract(x,core,core_end-core,dump);
    bool body_saved=false;
    for(auto pc=core;pc<std::min(core+128,core_end);pc+=4) {
        uintptr_t target=0;
        if(decode_branch_target(pc,true,&target))break;
        if(read_u32(pc)==0xaa0203fcU)body_saved=true; // MOV X28,X2
    }
    if(!serializer || !body_saved ||
       ((read_u32(serializer)>>10U)&0xfffU)!=((read_u32(serializer+28)>>10U)&0xfffU) ||
       !decode_branch_target(serializer+32,true,&constructor) || constructor!=read[0].constructor ||
       !decode_branch_target(serializer+12,true,&out.serialize) ||
       injection_function_start(r,x,out.serialize)!=out.serialize ||
       !resolver.calls_function(out.success,out.serialize))return fail(__LINE__);
    // Use the host's complete queue-busy predicate. Its getter must be the
    // receiver of the resolved enqueue call. All host member displacements are
    // masked; the predicate itself reads its own current layout.
    static constexpr item_catalog::Word busy[] = {
        {0xa9bf7bfd,0xffffffff,item_catalog::Role::none},
        {0x910003fd,0xffffffff,item_catalog::Role::none},
        {0x94000000,0xfc000000,item_catalog::Role::none},
        {0xa9402408,0xffc07fff,item_catalog::Role::none},
        {0xf940000a,0xffc003ff,item_catalog::Role::none},
        {0xeb09011f,0xffffffff,item_catalog::Role::none},
        {0xfa400940,0xffffffff,item_catalog::Role::none},
        {0x1a9f07e0,0xffffffff,item_catalog::Role::none},
        {0xa8c17bfd,0xffffffff,item_catalog::Role::none},
        {0xd65f03c0,0xffffffff,item_catalog::Role::none},
    };
    std::vector<uintptr_t> candidates;
    for(auto pc:find_item_contract(x,busy)) {
        uintptr_t getter=0,end=0;
        if(injection_function_start(r,x,pc,&end)==pc && end==pc+sizeof(busy)/sizeof(busy[0])*4 &&
           decode_branch_target(pc+8,true,&getter) && getter==enqueue[0].getter) push_unique(candidates,pc);
    }
    if(candidates.size()!=1) return fail(__LINE__);
    out.busy=candidates.front();
    return out;
}
