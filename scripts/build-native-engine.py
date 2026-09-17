from pathlib import Path
import re
import argparse, hashlib, json, subprocess, os
from native_staging import NativeStage
parser=argparse.ArgumentParser(description="Build a read-only browser audit from the module's production C++ resolver contracts")
parser.add_argument('module_repo', type=Path)
parser.add_argument('--skip-build', action='store_true', help='Regenerate reviewed sources without compiling WASM')
parser.add_argument('--allow-dirty', action='store_true', help='Permit a development import; provenance will label it')
args=parser.parse_args()
root=args.module_repo.resolve();verifier=Path(__file__).resolve().parent.parent
stage=NativeStage(verifier/'native');out=stage.directory
source=root/'app/src/main/cpp/template_native.cpp';header=root/'app/src/main/cpp/item_catalog_signatures.h'
production_paths = sorted(str(p.relative_to(root)) for directory, extensions in [
 ('app/src/main/cpp', {'.cpp','.h','.inc','.txt'}), ('app/src/main/java',{'.java'}),
 ('third_party/shadowhook',{'.c','.S','.h','.java','.txt','.json','.pro'})]
 for p in (root/directory).rglob('*') if p.is_file() and p.suffix in extensions)
production_paths += ['app/build.gradle.kts','app/src/main/AndroidManifest.xml','app/proguard-rules.pro',
                     'tools/test_item_injection_queue.cpp','tools/lua_registration_model.inc','gradle/libs.versions.toml','gradle/wrapper/gradle-wrapper.properties']
input_hashes={name:hashlib.sha256((root/name).read_bytes()).hexdigest() for name in production_paths}
dirty = bool(subprocess.check_output(['git','-C',str(root),'status','--porcelain','--untracked-files=normal'],text=True).strip())
if dirty and not args.allow_dirty:
 raise SystemExit('Production inputs have uncommitted changes; commit them or explicitly use --allow-dirty.')
s=source.read_text()
(out/'item_catalog_signatures.h').write_bytes(header.read_bytes())
copied_headers=['item_injection_contracts.h','item_injection_cascade_contracts.h','item_injection_queue.h','item_removal_contracts.h',
 'item_pet_contracts.h','item_count_contracts.h','item_creation_contracts.h','mass_shop_contracts.h',
 'director_speed_contracts.h','runtime_context_contracts.h','runtime_feature_signatures.h','elf_unwind_bounds.h',
 'item_fish_contracts.h','item_pet_creation_contracts.h','item_semantics_contracts.h']
for name in copied_headers:
 (out/name).write_bytes((root/'app/src/main/cpp'/name).read_bytes())
(out/'lua_registration_model.inc').write_bytes((root/'tools/lua_registration_model.inc').read_bytes())
pure_files={
 'item_removal_runtime.inc':(root/'app/src/main/cpp/item_removal_runtime.inc').read_text().split('struct HostInstanceVector {')[0],
 'item_count_runtime.inc':(root/'app/src/main/cpp/item_count_runtime.inc').read_text().split('int32_t instance_vector_count(')[0],
 'director_speed.inc':(root/'app/src/main/cpp/director_speed.inc').read_text().split('void hooked_director_calculate_delta(')[0],
}
for name in ['item_pet_runtime.inc','item_creation_runtime.inc','mass_shop_resolver.inc',
 'item_fish_resolver.inc','item_pet_creation_resolver.inc','item_semantics_resolver.inc','item_injection_cascade_resolver.inc']:
 pure_files[name]=(root/'app/src/main/cpp'/name).read_text()
for name,text in pure_files.items():(out/name).write_text(text.rstrip()+'\n')
injection = (root/'app/src/main/cpp/item_injection_runtime.inc').read_text()
# Import the exact pure resolver, excluding hooks, live dispatch and inventory writes.
layout = injection[:injection.index('std::mutex g_injection_mutex;')]
resolver = injection[injection.index('// Function boundaries come'):injection.index('struct InjectionItem {')]
resolver=resolver.replace('#include "item_fish_runtime.inc"','')
for forbidden in ['step_item_injection(', 'injection_amount(', 'hooked_injection_sync(']:
 if forbidden in resolver:
  raise SystemExit('Live game dispatch must not enter the static engine: '+forbidden)
(out/'item_injection_resolver.h').write_text('// Generated verbatim from the production resolver.\n'+layout+resolver)
s = s.replace('#include "item_injection_runtime.inc"', '#include "item_injection_resolver.h"')
s = s.replace('#include "mass_shop_runtime.inc"','')
queue = (root/'tools/test_item_injection_queue.cpp').read_text()
queue = queue.replace('#include "../app/src/main/cpp/item_injection_queue.h"', '#include "item_injection_queue.h"')
queue = queue.replace('int main()', 'bool audit_injection_queue()').replace('assert(', 'AUDIT_REQUIRE(')
queue = re.sub(r'std::cout << "Item Injection[^;]+;', 'return true;', queue)
(out/'item_injection_queue_test.h').write_text(
 '// Generated from the module queue tests; never invokes uploaded game code.\n'
 '#define AUDIT_REQUIRE(value) do { if (!(value)) return false; } while (0)\n'+queue+'\n#undef AUDIT_REQUIRE\n')
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
#include <memory>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <vector>
#include <string>
#include <unordered_map>
#include <sys/types.h>
#include "elf-abi.h"
#include "elf_unwind_bounds.h"
#include "item_injection_contracts.h"
#include "item_injection_queue_test.h"
#include "'''+'item_catalog_signatures.h'+'''"
#define ALOGI(...) do { printf(__VA_ARGS__); puts(""); } while(0)
#define ALOGW(...) ALOGI(__VA_ARGS__)
struct MemoryRange {uintptr_t start,end;bool executable,writable;};
uintptr_t audit_base=0;size_t writes=0;std::vector<MemoryRange> audit_ranges,audit_readable;
constexpr const char *kTargetLib="libapp.so";
uintptr_t find_module_base(const char *){return audit_base;}
uintptr_t decode_adrp_ldr_global(uintptr_t,uintptr_t);
std::unordered_map<std::string,uintptr_t> audit_symbols;
void *resolve_app_symbol(const char *n){return (void*)audit_symbols[n];}
std::vector<MemoryRange> app_exec_ranges(){return audit_ranges;}
std::vector<MemoryRange> app_readable_ranges(){return audit_readable;}
struct ScanSnapshot {bool read(uintptr_t,void*,size_t) const{return false;}};
thread_local ScanSnapshot *t_active_scan_snapshot=nullptr;
std::mutex g_item_layout_mutex;std::atomic<bool> g_item_layout_ready{false};item_catalog::Layout g_item_layout{};
std::atomic<bool> g_object_layouts_ready{false};item_catalog::ObjectLayouts g_object_layouts{};
struct ItemNameRet {uint64_t w0,w1,w2;};
void append_item_dump_log(const char *kind,int,int,int,const char *d){printf("LAYOUT %s %s\\n",kind,d);}
ssize_t vm_read_partial(uintptr_t a,void *p,size_t n){memcpy(p,(void*)a,n);return n;}
template<typename T> void for_each_snapshot_segment(const std::vector<MemoryRange>& rs,T visit){for(auto r:rs)visit(r.start,(const uint8_t*)r.start,r.end-r.start);}
''']
for n in ['checked_address_end','range_contains','push_unique','read_u32','decode_branch_target','memory_matches_mask','memory_equals']:parts.append(fn(n))
parts.append('''std::vector<uintptr_t> find_pattern(const std::vector<MemoryRange>& rs,const uint8_t*p,size_t n){std::vector<uintptr_t> h;if(!p||!n)return h;for(auto r:rs){auto b=(const uint8_t*)r.start,e=(const uint8_t*)r.end;while(b+n<=e){auto q=std::search(b,e,p,p+n);if(q==e)break;h.push_back((uintptr_t)q);b=q+1;}}return h;}
''')
(out/'byte-scan.h').write_text('// Generated verbatim from the production byte-mask scanner.\n#pragma once\n'+fn('find_masked_pattern'))
parts.append('#include "byte-scan.h"\n')
for n in ['code_pattern_mask','find_code_pattern']:parts.append(fn(n))
for m in re.finditer(r'^(?:static )?constexpr uint8_t (?:sig_|mask_)\w*\[\]\s*=\s*\{.*?\};',s,re.S|re.M):parts.append(m[0])
for l in s.splitlines():
 if re.match(r'constexpr .*\*?(?:kAchievementFireEventTypeName|kAchievementGameEventPrefix|kAchievementRepositoryNotFoundText|kAchievementRepositoryEndAssertText)\b',l):parts.append(l)
for typ in ['DialogueNativeResolution','DialoguePageJumpCandidate']:parts.append(re.search(r'struct '+typ+r' \{.*?\n\};',s,re.S)[0])
parts.append('template<size_t N> bool read_item_contract(uintptr_t, const item_catalog::Word (&)[N], uint32_t (&)[N]);\ntemplate<size_t N> std::vector<uintptr_t> find_item_contract(const std::vector<MemoryRange> &, const item_catalog::Word (&)[N]);\ntemplate<size_t N> uint32_t item_role_word(const uint32_t (&)[N], const item_catalog::Word (&)[N], item_catalog::Role);\ntemplate<size_t N> uintptr_t scoped_layout_contract(const std::vector<MemoryRange> &, uintptr_t, const item_catalog::Word (&)[N], uint32_t (&)[N]);\nuintptr_t find_function_start_before_ref(const std::vector<MemoryRange> &, uintptr_t);\nvoid *resolve_app_symbol(const char *);\n\n')
parts.append('template<size_t N> uintptr_t layout_call_target(uintptr_t,const item_catalog::Word (&)[N],item_catalog::Role);\n')
parts.append(s[s.index('uintptr_t resolve_pattern_hook_address('):s.index('void *install_resolved_function_hook(')])
parts.append(fn('decode_adrp_ldr_global'))
a=s.index('template<size_t N>\nbool read_item_contract');b=s.index('// One enumerate+resolve pass over the live item catalog',a);parts.append(s[a:b])
parts.append(s[s.index('template<size_t N>\nuintptr_t bridge_receiver_global'):s.index('uint64_t runtime_patch_signature(')])
parts.append(fn('resolve_currency_token_site'))
features=(root/'app/src/main/cpp/runtime_feature_hooks.inc').read_text()
parts.append(features[:features.index('std::atomic<uint32_t> g_runtime_feature_ready')])
context=(root/'app/src/main/cpp/runtime_context_hooks.inc').read_text()
parts.append(context[:context.index('AdLayout g_ad_context;')])
parts.append(context[context.index('struct TeamLayout {'):context.index('TeamLayout g_team_context;')])
parts.append('#include "injection-audit.h"\n')
parts.append('#include "lua_registration_model.inc"\n')
parts.append('#include "resolver-audit.h"\n')
parts.append('''int main(int argc,char**argv){std::string dir=argv[1];std::ifstream in(dir+"/image.bin",std::ios::binary);std::vector<char> raw((std::istreambuf_iterator<char>(in)),{});void *mem=nullptr;posix_memalign(&mem,4096,raw.size());memcpy(mem,raw.data(),raw.size());uintptr_t base=(uintptr_t)mem;audit_base=base;
std::ifstream rel(dir+"/relocs.txt");uint64_t o,v;while(rel>>o>>v){uintptr_t p=base+v;memcpy((void*)(base+o),&p,8);}
std::ifstream symbols(dir+"/symbols.txt");std::string name;while(symbols>>name>>v)audit_symbols[name]=base+v;
std::vector<MemoryRange> ranges,readable_ranges;std::ifstream segs(dir+"/segments.txt");uint64_t a,z,fl;while(segs>>a>>z>>fl){readable_ranges.push_back({base+a,base+a+z,bool(fl&1),bool(fl&2)});if(fl&1)ranges.push_back(readable_ranges.back());}audit_ranges=ranges;audit_readable=readable_ranges;
std::vector<char> before((char*)mem,(char*)mem+raw.size());
printf("AUDIT_VERSION 3\\n");
auto report=[&](const char*n,uintptr_t a){printf("RESULT %s 0x%llx\\n",n,(unsigned long long)(a?a-base:0));};
''')
seen=set();target_names=[]
for m in re.finditer(r'uintptr_t (\w+) = (resolve_(?:masked_)?pattern_hook_address\(\s*ranges,\s*"([^"]+)".*?\));',s,re.S):
 var,call,label=m.groups()
 if var in seen:continue
 seen.add(var);target_names.append(label);parts.append('uintptr_t '+var+'='+call+';report("'+label+'",'+var+');')
lua_bindings = {}
for name, optional in re.findall(r'resolve_lua_registered_function\(readable_ranges,\s*ranges,\s*"([^"]+)",\s*(true|false)', s):
 lua_bindings[name] = lua_bindings.get(name, True) and optional == 'true'
lua_bindings['changeItemAmount'] = False # Required for the independent grant feature.
for name, optional in lua_bindings.items():
 target_names.append('lua.'+name)
 flag='true' if optional else 'false'
 parts.append('uintptr_t lua_'+name+'=0;bool lua_ok_'+name+'=resolve_lua_registered_function(readable_ranges,ranges,"'+name+'",'+flag+',&lua_'+name+');')
 if optional:
  parts.append('if(lua_ok_'+name+' && !lua_'+name+') printf("OPTIONAL_ABSENT lua.'+name+'\\n");')
 parts.append('report("lua.'+name+'",lua_'+name+');')
parts.append('''uintptr_t add_pc_exp_addr=resolve_add_pc_exp(readable_ranges,ranges);report("battle.addPCExp.patch",add_pc_exp_addr);
uintptr_t cat_scratch_total_addr=resolve_named_integer_setter(readable_ranges,ranges,"stampTotal");report("catScratch.total",cat_scratch_total_addr);
report("catScratch.namedCount",resolve_named_integer_setter(readable_ranges,ranges,"stampCount"));
item_catalog::Layout il{};bool item_ok=resolve_item_catalog_layout(readable_ranges,readable_ranges,ranges,&il);report("item.global",il.global);
uintptr_t ag=resolve_domain_achievement_repository_get(readable_ranges,ranges);report("achievement.get",ag);report("achievement.dispatch",resolve_achievement_fire_event_dispatch(readable_ranges,ranges));
uintptr_t up=resolve_userdata_push_from_contract(readable_ranges,ranges);report("userdata.push",up);
resolve_object_layouts(ranges,item_writer_addr,up,domain_token_shop_set_total_addr,token_shop_purchase_addr,appraisal_exchange_shop_purchase_addr,ag,lua_fireAchievementTrigger);
// Production initialization publishes the catalog layout before resolving injection.
g_item_layout=il;g_item_layout_ready.store(item_ok);
bool injection_ok=audit_item_injection(readable_ranges,ranges,token_shop_purchase_addr,item_writer_addr);
auto dg=resolve_dialogue_native_hooks(ranges);report("dialogue.renderChecker",dg.rendering_checker);
bool bindings_ok=audit_runtime_bindings(readable_ranges,ranges,appraisal_exchange_shop_purchase_addr);
bool models_ok=audit_resolver_models(readable_ranges,ranges);
bool unchanged=memcmp(before.data(),mem,raw.size())==0;
printf("READ_ONLY unchanged=%d\\n",unchanged);
bool complete=item_ok&&injection_ok&&bindings_ok&&models_ok&&unchanged;
printf("AUDIT_COMPLETE ok=%d\\n",complete);
free(mem);return complete?0:1;}

''')
assert not any(name in resolver for name in ['PetRemovalPlan', 'create_injection_instance(', 'InstanceSnapshot'])
(out/'engine.cpp').write_text('// Generated from the production module by scripts/build-native-engine.py.\n'+'\n'.join(parts))
dialogue_struct = re.search(r'struct DialogueNativeResolution \{(.*?)\n\};', s, re.S)[1]
dialogue_members = re.findall(r'uintptr_t (\w+) = 0;', dialogue_struct)
dialogue_fields = [n for n in dialogue_members if n.endswith('_offset')]
dialogue_targets = [n for n in dialogue_members if n not in dialogue_fields]
dialogue_reports = '\n'.join('report("dialogue.native.'+n+'",dg.'+n+');' for n in dialogue_targets)
dialogue_reports += '\nprintf("CHECK dialogue.layout_fields %d '+ ' '.join(n+'=%zu' for n in dialogue_fields) + '\\n",' + \
                    ' && '.join('dg.'+n+'!=0' for n in dialogue_fields) + ',' + \
                    ','.join('static_cast<size_t>(dg.'+n+')' for n in dialogue_fields) + ');'
engine = (out/'engine.cpp').read_text().replace('report("dialogue.renderChecker",dg.rendering_checker);',
                                             'report("dialogue.renderChecker",dg.rendering_checker);\n'+dialogue_reports)
(out/'engine.cpp').write_text(engine.rstrip()+'\n')
target_names += ['dialogue.native.'+n for n in dialogue_targets]
target_names += ['battle.addPCExp.patch','catScratch.total','catScratch.namedCount','item.global',
                 'achievement.get','achievement.dispatch','userdata.push','dialogue.renderChecker']
checks = list(dict.fromkeys(re.findall(r'"((?:injection|runtime|mass|director|models)\.[A-Za-z0-9_]+)"',
    (out/'injection-audit.h').read_text()+(out/'resolver-audit.h').read_text())))
checks.append('dialogue.layout_fields')
coverage = {'schemaVersion':3,'targets':target_names,'checks':checks,
            'optionalTargets':['lua.'+name for name, optional in lua_bindings.items() if optional],
            'runtimeChecks':['Live objects and native ABI calls','Actual additions/removals and instance identity',
             'Resource supply and memory pressure','Save acknowledgement and persistence',
             'Android lifecycle and feature isolation','ShadowHook install/disable/unhook',
             'Shop ownership and restoration']}
(out/'coverage.js').write_text('(function(s){const value='+json.dumps(coverage,indent=2)+';s.AENativeCoverage=value;if(typeof module!=="undefined")module.exports=value;})(globalThis);\n')
generated_names = ['engine.cpp','byte-scan.h','item_catalog_signatures.h','item_injection_resolver.h',
                   'item_injection_queue_test.h','lua_registration_model.inc','coverage.js',*copied_headers,*pure_files]
provenance={"schemaVersion":3,"uncommittedSource":dirty,"moduleCommit":subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD'],text=True).strip(),"sourceSha256":hashlib.sha256(source.read_bytes()).hexdigest(),"contractsSha256":hashlib.sha256(header.read_bytes()).hexdigest(),
 "productionFiles":input_hashes,
 "generatedFiles":{name:hashlib.sha256((out/name).read_bytes()).hexdigest() for name in generated_names},
 "verifierNativeFiles":{name:hashlib.sha256((out/name).read_bytes()).hexdigest() for name in ['elf-abi.h','injection-audit.h','resolver-audit.h']},
 "staticCoverage":checks,
 "runtimeOnly":["Live inventory objects and metadata", "Actual grants and before/after counts", "Resource replenishment and reconnect", "Adaptive instance timing and memory pressure", "Save acknowledgement and persistence", "Game-thread scheduling and existing-feature isolation"]}
(out/'provenance.json').write_text(json.dumps(provenance,indent=2)+'\n')
if not args.skip_build:
 subprocess.run(['bash',str(verifier/'scripts/build-web-engine.sh')],check=True,
                env=dict(os.environ,AE_NATIVE_DIR=str(out)))
else:
 for name in ['engine.js','engine.wasm','build.json']:
  (out/name).unlink(missing_ok=True)
if any(hashlib.sha256((root/name).read_bytes()).hexdigest()!=digest for name,digest in input_hashes.items()):
 raise SystemExit('Module inputs changed during compilation; no native files were published.')
stage.publish()
