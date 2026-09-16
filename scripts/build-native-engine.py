from pathlib import Path
import re
import argparse, hashlib, json, subprocess
parser=argparse.ArgumentParser(description="Build the browser verifier from the module's production C++ resolver and patch functions")
parser.add_argument('module_repo', type=Path)
args=parser.parse_args()
root=args.module_repo.resolve();out=Path(__file__).resolve().parent.parent/'native'
out.mkdir(exist_ok=True)
source=root/'app/src/main/cpp/template_native.cpp';header=root/'app/src/main/cpp/item_catalog_signatures.h'
s=source.read_text()
(out/'item_catalog_signatures.h').write_bytes(header.read_bytes())
def fn(name):
 m=re.search(r'^(?:[\w:<>]+\s+)+\b'+name+r'\([^;{}]*\)\s*\{',s,re.M)
 if not m:raise RuntimeError(name)
 a=s.index('{',m.start());i=a+1;depth=1
 while depth:
  depth+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[m.start():i]+'\n'
parts=['''#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <mutex>
#include <vector>
#include <string>
#include <unordered_map>
#include <sys/types.h>
#include "'''+'item_catalog_signatures.h'+'''"
#define ALOGI(...) do { printf(__VA_ARGS__); puts(""); } while(0)
#define ALOGW(...) ALOGI(__VA_ARGS__)
struct MemoryRange {uintptr_t start,end;bool executable,writable;};
uintptr_t audit_base=0;size_t writes=0;std::vector<MemoryRange> audit_ranges;
std::unordered_map<std::string,uintptr_t> audit_symbols;
void *resolve_app_symbol(const char *n){return (void*)audit_symbols[n];}
std::vector<MemoryRange> app_exec_ranges(){return audit_ranges;}
std::mutex g_token_purchase_patch_mutex;std::atomic<uintptr_t> g_token_purchase_owned_count_address{0};std::atomic<bool> g_token_purchase_owned_count_patch_enabled{false};
#define mass_trace_log(...) ALOGI(__VA_ARGS__)
std::mutex g_speed_patch_mutex;uintptr_t g_speed_patch_address=0;
std::mutex g_item_layout_mutex;std::atomic<bool> g_item_layout_ready{false};item_catalog::Layout g_item_layout{};
std::atomic<bool> g_object_layouts_ready{false};item_catalog::ObjectLayouts g_object_layouts{};
struct ItemNameRet {uint64_t w0,w1,w2;};
void append_item_dump_log(const char *kind,int,int,int,const char *d){printf("LAYOUT %s %s\\n",kind,d);}
ssize_t vm_read_partial(uintptr_t a,void *p,size_t n){memcpy(p,(void*)a,n);return n;}
template<typename T> void for_each_snapshot_segment(const std::vector<MemoryRange>& rs,T visit){for(auto r:rs)visit(r.start,(const uint8_t*)r.start,r.end-r.start);}
bool patch_memory(uintptr_t a,const void *p,size_t n){++writes;printf("WRITE 0x%llx size=%zu\\n",(unsigned long long)(a-audit_base),n);memcpy((void*)a,p,n);return true;}
struct RuntimeConfig {bool enabled=true,speedy=true,battle_mp=true,all_damage=true,dungeon_skip=true,team_god_mode=true,ad_bypass=true,encounter_freeze=true,encounter_force=false;};
''']
for n in ['checked_address_end','range_contains','push_unique','read_u32','decode_branch_target','memory_matches_mask','memory_equals','encode_branch','encode_cbz_w','append_u32']:parts.append(fn(n))
parts.append('''std::vector<uintptr_t> find_pattern(const std::vector<MemoryRange>& rs,const uint8_t*p,size_t n){std::vector<uintptr_t> h;for(auto r:rs){auto b=(const uint8_t*)r.start,e=(const uint8_t*)r.end;while(b+n<=e){auto q=std::search(b,e,p,p+n);if(q==e)break;h.push_back((uintptr_t)q);b=q+1;}}return h;}
std::vector<uintptr_t> find_masked_pattern(const std::vector<MemoryRange>& rs,const uint8_t*p,const uint8_t*m,size_t n){std::vector<uintptr_t> h;for(auto r:rs)for(uintptr_t a=r.start;a+n<=r.end;++a)if((!m[0]||*(uint8_t*)a==p[0])&&memory_matches_mask(a,p,m,n))h.push_back(a);return h;}
''')
for n in ['code_pattern_mask','find_code_pattern']:parts.append(fn(n))
for m in re.finditer(r'^(?:static )?constexpr uint8_t (?:sig_|mask_)\w*\[\]\s*=\s*\{.*?\};',s,re.S|re.M):parts.append(m[0])
for l in s.splitlines():
 if re.match(r'constexpr .*\*?(?:kAchievementFireEventTypeName|kAchievementGameEventPrefix|kAchievementRepositoryNotFoundText|kAchievementRepositoryEndAssertText)\b',l):parts.append(l)
for typ in ['DialogueNativeResolution','DialoguePageJumpCandidate','BytePatch']:parts.append(re.search(r'struct '+typ+r' \{.*?\n\};',s,re.S)[0])
parts.append('template<size_t N> bool read_item_contract(uintptr_t, const item_catalog::Word (&)[N], uint32_t (&)[N]);\ntemplate<size_t N> std::vector<uintptr_t> find_item_contract(const std::vector<MemoryRange> &, const item_catalog::Word (&)[N]);\ntemplate<size_t N> uint32_t item_role_word(const uint32_t (&)[N], const item_catalog::Word (&)[N], item_catalog::Role);\ntemplate<size_t N> uintptr_t scoped_layout_contract(const std::vector<MemoryRange> &, uintptr_t, const item_catalog::Word (&)[N], uint32_t (&)[N]);\nuintptr_t find_function_start_before_ref(const std::vector<MemoryRange> &, uintptr_t);\nvoid *resolve_app_symbol(const char *);\n\n')
parts.append(s[s.index('uintptr_t resolve_pattern_hook_address('):s.index('void *install_resolved_function_hook(')])
parts.append(fn('decode_adrp_ldr_global'))
a=s.index('template<size_t N>\nbool read_item_contract');b=s.index('// One enumerate+resolve pass over the live item catalog',a);parts.append(s[a:b])
parts.append(s[s.index('template<size_t N>\nuintptr_t bridge_receiver_global'):s.index('void apply_ad_bypass_patch(')])
for n in ['apply_token_purchase_owned_count_zero_patch','apply_byte_patch','apply_present_byte_patch','apply_team_god_patch','apply_ad_bypass_patch','patch_speed_constants','apply_encounter_judge_patch','apply_runtime_byte_patches']:parts.append(fn(n))
parts.append('''int main(int argc,char**argv){std::string dir=argv[1];std::ifstream in(dir+"/image.bin",std::ios::binary);std::vector<char> raw((std::istreambuf_iterator<char>(in)),{});void *mem=nullptr;posix_memalign(&mem,4096,raw.size());memcpy(mem,raw.data(),raw.size());uintptr_t base=(uintptr_t)mem;audit_base=base;
std::ifstream rel(dir+"/relocs.txt");uint64_t o,v;while(rel>>o>>v){uintptr_t p=base+v;memcpy((void*)(base+o),&p,8);}
std::ifstream symbols(dir+"/symbols.txt");std::string name;while(symbols>>name>>v)audit_symbols[name]=base+v;
std::vector<MemoryRange> ranges,readable_ranges;std::ifstream segs(dir+"/segments.txt");uint64_t a,z,fl;while(segs>>a>>z>>fl){readable_ranges.push_back({base+a,base+a+z,bool(fl&1),bool(fl&2)});if(fl&1)ranges.push_back(readable_ranges.back());}audit_ranges=ranges;
auto report=[&](const char*n,uintptr_t a){printf("RESULT %s 0x%llx\\n",n,(unsigned long long)(a?a-base:0));};
''')
seen=set();names={}
for m in re.finditer(r'uintptr_t (\w+) = (resolve_(?:masked_)?pattern_hook_address\(\s*ranges,\s*"([^"]+)".*?\));',s,re.S):
 var,call,label=m.groups()
 if var in seen:continue
 seen.add(var);parts.append('uintptr_t '+var+'='+call+';report("'+label+'",'+var+');')
for name in dict.fromkeys(re.findall(r'resolve_lua_registered_function\(readable_ranges, ranges, "([^"]+)"',s)):
 parts.append('uintptr_t lua_'+name+'=0;resolve_lua_registered_function(readable_ranges,ranges,"'+name+'",false,&lua_'+name+');report("lua.'+name+'",lua_'+name+');')
parts.append('''uintptr_t add_pc_exp_addr=resolve_add_pc_exp(readable_ranges,ranges);report("battle.addPCExp.patch",add_pc_exp_addr);
uintptr_t cat_scratch_total_addr=resolve_named_integer_setter(readable_ranges,ranges,"stampTotal");report("catScratch.total",cat_scratch_total_addr);
report("catScratch.namedCount",resolve_named_integer_setter(readable_ranges,ranges,"stampCount"));
item_catalog::Layout il{};bool item_ok=resolve_item_catalog_layout(readable_ranges,readable_ranges,ranges,&il);report("item.global",il.global);
uintptr_t ag=resolve_domain_achievement_repository_get(readable_ranges,ranges);report("achievement.get",ag);report("achievement.dispatch",resolve_achievement_fire_event_dispatch(readable_ranges,ranges));
uintptr_t up=resolve_userdata_push_from_contract(readable_ranges,ranges);report("userdata.push",up);
resolve_object_layouts(ranges,item_writer_addr,up,domain_token_shop_set_total_addr,token_shop_purchase_addr,appraisal_exchange_shop_purchase_addr,ag,lua_fireAchievementTrigger);
auto dg=resolve_dialogue_native_hooks(ranges);report("dialogue.renderChecker",dg.rendering_checker);
std::vector<char> before((char*)mem,(char*)mem+raw.size());RuntimeConfig cfg;
uint32_t ow[std::size(item_catalog::owned_count_read)]{};
auto oa=scoped_layout_contract(ranges,token_shop_purchase_addr,item_catalog::owned_count_read,ow);report("mass.ownedCount",oa);g_token_purchase_owned_count_address.store(oa);
apply_runtime_byte_patches(cfg);apply_token_purchase_owned_count_zero_patch(true,"audit");auto apply_writes=writes;
apply_runtime_byte_patches(cfg);apply_token_purchase_owned_count_zero_patch(true,"repeat-audit");bool repeat_on=writes==apply_writes;
cfg.enabled=false;apply_runtime_byte_patches(cfg);apply_token_purchase_owned_count_zero_patch(false,"audit");auto undone=writes;
apply_runtime_byte_patches(cfg);apply_token_purchase_owned_count_zero_patch(false,"repeat-audit");bool repeat_off=writes==undone;
printf("IDEMPOTENT on=%d off=%d owned=%d\\n",repeat_on,repeat_off,oa!=0);
bool restored=memcmp(before.data(),mem,raw.size())==0;printf("ROUNDTRIP item=%d applyWrites=%zu undoWrites=%zu restored=%d\\n",item_ok,apply_writes,writes-apply_writes,restored);
free(mem);return restored&&item_ok&&repeat_on&&repeat_off&&oa!=0?0:1;}
''')
(out/'engine.cpp').write_text('// Generated from the production module by scripts/build-native-engine.py.\n'+'\n'.join(parts))
provenance={"uncommittedSource":bool(subprocess.check_output(['git','-C',str(root),'status','--porcelain','--',str(source),str(header)],text=True).strip()),"moduleCommit":subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD'],text=True).strip(),"sourceSha256":hashlib.sha256(source.read_bytes()).hexdigest(),"contractsSha256":hashlib.sha256(header.read_bytes()).hexdigest()}
(out/'provenance.json').write_text(json.dumps(provenance,indent=2)+'\n')
subprocess.run(['bash',str(out.parent/'scripts/build-web-engine.sh')],check=True)
