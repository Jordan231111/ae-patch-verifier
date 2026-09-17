#include "item_injection_cascade_contracts.h"

struct InjectionCascadeLayout {
    uintptr_t callback=0,eligible=0,manager_slot=0,reset=0,destroy=0;
    bool ready=false;
};
InjectionCascadeLayout g_injection_cascade;

struct CascadeResolver {
    const std::vector<MemoryRange> &r,&x;
    size_t calls_to(uintptr_t owner,uintptr_t target) const {
        size_t count=0;
        for(uintptr_t pc=owner;pc<owner+65536;pc+=4) {
            if(injection_function_start(r,x,pc)!=owner) return count;
            uintptr_t callee=0;
            if((decode_branch_target(pc,true,&callee) || decode_branch_target(pc,false,&callee)) && callee==target) ++count;
        }
        return SIZE_MAX; // do not accept an incompletely inspected owner
    }
    template<size_t N> std::vector<uintptr_t> sites(uintptr_t owner,
            const injection_cascade_contracts::Word (&p)[N]) const {
        item_catalog::Word words[N]{};
        for(size_t i=0;i<N;++i) words[i]={p[i].value,p[i].mask,item_catalog::Role::none};
        std::vector<MemoryRange> scope;
        if(!owner) scope=x;
        else for(const auto &range:x) {
            const auto lo=std::max(range.start,owner),hi=std::min(range.end,owner+65536);
            if(hi>lo) scope.push_back({lo,hi,true,false});
        }
        auto hits=find_item_contract(scope,words);
        hits.erase(std::remove_if(hits.begin(),hits.end(),[&](uintptr_t pc) {
            const auto f=injection_function_start(r,x,pc);
            return !f || (owner && f!=owner) || injection_function_start(r,x,pc+4*(N-1))!=f;
        }),hits.end());
        return hits;
    }
    template<size_t N> uintptr_t one(uintptr_t owner,const injection_cascade_contracts::Word (&p)[N]) const {
        const auto hits=sites(owner,p);return hits.size()==1 ? hits.front() : 0;
    }
    template<size_t N> uintptr_t capture(uintptr_t at,const injection_cascade_contracts::Word (&p)[N],
            injection_cascade_contracts::Role role) const {
        if(at) for(size_t i=0;i<N;++i) if(p[i].role==role) return at+4*i;
        return 0;
    }
    template<size_t N> uintptr_t call(uintptr_t at,const injection_cascade_contracts::Word (&p)[N],
            injection_cascade_contracts::Role role) const {
        uintptr_t fn=0;const auto pc=capture(at,p,role);
        return pc && decode_branch_target(pc,true,&fn) && injection_function_start(r,x,fn)==fn ? fn : 0;
    }
    template<size_t N> uintptr_t slot(uintptr_t at,const injection_cascade_contracts::Word (&p)[N]) const {
        using injection_cascade_contracts::Role;
        const auto a=capture(at,p,Role::slot_page),b=capture(at,p,Role::slot_load);
        const auto address=a && b ? decode_adrp_ldr_global(a,b) : 0;
        return injection_writable_image_slot(r,address) ? address : 0;
    }
};

InjectionCascadeLayout resolve_injection_cascade(const std::vector<MemoryRange> &r,
        const std::vector<MemoryRange> &x,const InjectionLayout &items) {
    using namespace injection_cascade_contracts;
    const CascadeResolver c{r,x};InjectionCascadeLayout out;
    const auto failed=[&](int line) {
#if defined(AE_TEST_SYMBOL_RESOLVER) || defined(AE_DEVICE_LAB)
        std::fprintf(stderr,"CASCADE_UNRESOLVED %d\n",line);
#endif
        return InjectionCascadeLayout{};
    };
    const auto cb=c.one(0,callback);
    if(!cb) return failed(__LINE__);
    out.callback=injection_function_start(r,x,cb);
    out.eligible=c.call(cb,callback,Role::eligible);
    const auto complete=c.call(cb,callback,Role::complete);
    const auto ec=complete ? c.one(complete,eligible_call) : 0;
    const auto end=complete ? c.one(complete,completion) : 0;
    const auto reward=c.call(end,completion,Role::reward);
    if(!out.eligible || !ec || c.call(ec,eligible_call,Role::eligible)!=out.eligible || !reward)
        return failed(__LINE__);
    const auto state=c.one(reward,reward_state),assign=c.one(reward,reward_assign);
    const auto currency=c.one(reward,reward_currency),notify=c.one(reward,reward_notify);
    if(!state || !assign || !currency || !notify || !(state<assign && assign<currency && currency<notify) ||
       c.call(state,reward_state,Role::repository)!=items.token_repository ||
       c.call(assign,reward_assign,Role::assign)!=items.token_assign ||
       c.calls_to(reward,items.token_assign)!=1 || c.calls_to(out.callback,complete)!=1 ||
       item_catalog::unsigned_offset(read_u32(notify+6*4),3)!=items.change_slot)
        return failed(__LINE__);
    const auto page_pc=c.capture(currency,reward_currency,Role::text_page);
    uintptr_t page=0,text=0;uint32_t reg=0;
    if(!decode_adrp_page(read_u32(page_pc),page_pc,&page,&reg) ||
       !decode_add_immediate(read_u32(c.capture(currency,reward_currency,Role::text_add)),reg,page,&text))
        return failed(__LINE__);
    static constexpr char honor[]="currency.achievement_point";
    char literal[sizeof(honor)]{};
    if(!text || vm_read_partial(text,literal,sizeof(literal))!=sizeof(literal) ||
       std::memcmp(literal,honor,sizeof(honor))) return failed(__LINE__);
    // Verify the capture's strong owner, raw manager and owning string copy.
    // These are ABI contracts, not fabricated DomainAchievement objects.
    const auto copy=c.one(0,capture_copy);
    if(!copy || injection_function_start(r,x,copy)!=copy) return failed(__LINE__);
    const auto set=injection_anchored_function(r,x,
        "void toybox::AchievementTriggerManager::setTriggers(const std::vector<std::shared_ptr<DomainAchievement>> &)");
    if(!set) return failed(__LINE__);
    std::vector<uintptr_t> getters;
    for(auto pc:c.sites(0,manager_caller)) if(c.call(pc,manager_caller,Role::set_triggers)==set)
        push_unique(getters,c.call(pc,manager_caller,Role::manager));
    if(getters.size()!=1 || !getters[0]) return failed(__LINE__);
    const auto accessor=c.one(getters[0],accessor_slot);
    out.manager_slot=c.slot(accessor,accessor_slot);
    if(!out.manager_slot) return failed(__LINE__);
    std::vector<uintptr_t> resets,destroys;
    for(auto pc:c.sites(0,manager_reset)) if(c.call(pc,manager_reset,Role::set_triggers)==set)
        push_unique(resets,injection_function_start(r,x,pc));
    for(auto pc:c.sites(0,manager_destroy)) if(c.slot(pc,manager_destroy)==out.manager_slot) {
        const auto store=c.capture(pc,manager_destroy,Role::slot_store);
        if(item_catalog::unsigned_offset(read_u32(store),3)!=
           item_catalog::unsigned_offset(read_u32(c.capture(pc,manager_destroy,Role::slot_load)),3))
            return failed(__LINE__);
        push_unique(destroys,injection_function_start(r,x,pc));
    }
    for(auto pc:c.sites(0,manager_destroy_inline)) if(c.slot(pc,manager_destroy_inline)==out.manager_slot) {
        if(c.call(pc,manager_destroy_inline,Role::destroy)!=c.call(pc,manager_destroy_inline,Role::clear) ||
           item_catalog::unsigned_offset(read_u32(c.capture(pc,manager_destroy_inline,Role::slot_store)),3)!=
           item_catalog::unsigned_offset(read_u32(c.capture(pc,manager_destroy_inline,Role::slot_load)),3))
            return failed(__LINE__);
        push_unique(destroys,injection_function_start(r,x,pc));
    }
    if(resets.size()!=1 || destroys.size()!=1 || !resets[0] || !destroys[0]) return failed(__LINE__);
    out.reset=resets[0];out.destroy=destroys[0];out.ready=true;return out;
}
