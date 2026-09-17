// Pure byte-image model. No uploaded instructions or host objects are executed.
static bool audit_lua_registration_rows() {
    alignas(16) uint8_t data[512]{};
    alignas(16) uint32_t code[16]{};
    code[0]=0xd65f03c0U;code[4]=0xd65f03c0U;
    constexpr char name[]="modelLuaEntry";
    std::memcpy(data,name,sizeof(name));
    const auto base=reinterpret_cast<uintptr_t>(data),a=reinterpret_cast<uintptr_t>(code),b=a+16;
    const std::vector<MemoryRange> readable{{base,base+sizeof(data),false,true}};
    const std::vector<MemoryRange> executable{{a,a+sizeof(code),true,false}};
    const auto pointer=[&](size_t offset,uintptr_t value) {std::memcpy(data+offset,&value,sizeof(value));};
    const auto row=[&](size_t offset,uintptr_t value) {pointer(offset,base);pointer(offset+sizeof(uintptr_t),value);};
    const auto resolve=[&](uintptr_t expected,bool success=true) {
        uintptr_t result=UINTPTR_MAX;
        return resolve_lua_registered_function(readable,executable,name,false,&result)==success && result==expected;
    };
    row(64,a);row(96,base+64+sizeof(uintptr_t));
    if(!resolve(a)) return false;
    row(129,b); // unaligned pointer bytes cannot represent a native row
    if(!resolve(a)) return false;
    row(160,base+224);pointer(224,b); // indirect slot without the matching name
    if(!resolve(a)) return false;
    row(256,b); // a second genuine function is ambiguous
    if(!resolve(0,false)) return false;
    std::memset(data+256,0,2*sizeof(uintptr_t));
    row(320,a);std::memset(data+64,0,2*sizeof(uintptr_t)); // moved direct row
    if(!resolve(a)) return false;
    std::memset(data+320,0,2*sizeof(uintptr_t));
    if(!resolve(0,false)) return false;
    uintptr_t absent=UINTPTR_MAX;
    return resolve_lua_registered_function(readable,executable,"absentModelLua",true,&absent) && absent==0;
}
