const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { validatedReady } = require('../api/relevel/_ready.js');
const cfg = { packageName: 'work.ponix.relevel', architecture: 'arm64-v8a', certificateSha256: 'c'.repeat(64), githubOwner: 'owner', githubRepo: 'builder', readyTag:'relevel-ready' };
const moduleInfo = { sha: 'a'.repeat(40), prebuilt: true };
function state() {
  return { schema: 1, packageName: cfg.packageName, architecture: cfg.architecture, state: 'ready', checkedAt: new Date().toISOString(),
    observed: { versionCode: 44, versionName: '1.1.3', moduleSha: moduleInfo.sha },
    ready: { validated: true, moduleSha: moduleInfo.sha, versionCode: 44, versionName: '1.1.3',
      certificateSha256: cfg.certificateSha256, bundleSha256: 'd'.repeat(64), cacheKey: 'e'.repeat(64),
      filename: 'ReLevel-1.1.3-arm64-lspatched.apks', sizeBytes: 1024, releaseAssetId: 123 } };
}
test('only the current verified source, module and signing identity are ready', () => {
  assert.equal(validatedReady(state(), cfg, moduleInfo, {versionName:'1.1.3'}).versionCode, 44);
  for (const change of [s => s.state = 'preparing', s => s.ready.validated = false,
    s => s.ready.moduleSha = 'b'.repeat(40), s => s.observed.versionCode++,
    s => s.ready.certificateSha256 = 'f'.repeat(64), s => s.ready.filename = '../wrong.apks',
    s => s.ready.sizeBytes = 0, s => s.ready.releaseAssetId = 0]) {
    const s = state(); change(s); assert.equal(validatedReady(s, cfg, moduleInfo, {versionName:'1.1.3'}), null);
  }
  assert.equal(validatedReady(state(), cfg, moduleInfo, {versionName:'1.1.4'}), null);
});
test('an unavailable Play listing cannot make stale observation metadata current', () => {
  const s = state(); s.checkedAt = new Date(Date.now() - 7200000).toISOString();
  assert.equal(validatedReady(s, cfg, moduleInfo, null), null);
  assert.ok(validatedReady(state(), cfg, moduleInfo, null));
  s.checkedAt = new Date(Date.now() + 3600000).toISOString();
  assert.equal(validatedReady(s, cfg, moduleInfo, {versionName:'1.1.3'}), null);
});
test('web requests consume the verified public index without private module access', async () => {
  const mod={exports:{}},paths=[];
  const dependencies={
    '../_shared/github.js':{githubJson:async(_,method,path)=>{paths.push(path);return {draft:false,body:JSON.stringify(state())};}},
    '../_shared/googleplay.js':{resolveLatestPlayListing:async()=>({versionName:'1.1.3'})}
  };
  vm.runInNewContext(fs.readFileSync(path.join(__dirname,'../api/relevel/_ready.js'),'utf8'),{
    module:mod,require:id=>dependencies[id],Date,Number,Promise,JSON
  });
  const result=await mod.exports.resolveReady(cfg);
  assert.equal(result.ready.moduleSha,moduleInfo.sha);
  assert.deepEqual(paths,['/repos/owner/builder/releases/tags/relevel-ready']);
});
function handler(ready, request) {
  const mod = {exports:{}};
  const dependencies = { './_config.js': {relevelConfig:()=>cfg}, './_ready.js': {resolveReady:async()=>({ready})},
    '../_shared/github.js': {githubRequest:request} };
  vm.runInNewContext(fs.readFileSync(path.join(__dirname,'../api/relevel/download.js'),'utf8'),{module:mod,require:id=>dependencies[id],URL,Date,Number,JSON});
  return mod.exports;
}
function response(){return {headers:{},setHeader(k,v){this.headers[k]=v;},end(body){this.body=body;}};}
test('downloads use headers-only artifact negotiation with a durable fallback',async()=>{
  const ready=state().ready;ready.artifactId=456;ready.artifactExpiresAt=new Date(Date.now()+3600000).toISOString();
  const calls=[];
  const run=handler(ready,async(_,options)=>{calls.push(options);return calls.length===1?{status:404,headers:{}}:{status:302,headers:{location:'https://storage.example/package.apks'}};});
  const res=response();await run({method:'GET',url:'/api/relevel/download?id='+ready.cacheKey},res);
  assert.equal(res.statusCode,302);assert.equal(calls.length,2);
  assert.equal(calls[0].apiPath,'/repos/owner/builder/actions/artifacts/456/zip');
  assert.equal(calls[1].apiPath,'/repos/owner/builder/releases/assets/123');
  assert.ok(calls.every(x=>x.headersOnly===true));
});
test('changed and malformed download identities never negotiate a file',async()=>{
  let calls=0;const run=handler(state().ready,async()=>{calls++;throw Error('unexpected');});
  for(const [id,status] of [['not-a-hash',400],['f'.repeat(64),409]]){
    const res=response();await run({method:'GET',url:'/api/relevel/download?id='+id},res);assert.equal(res.statusCode,status);
  }
  assert.equal(calls,0);
});
test('nonredirect and insecure storage responses cannot be presented as a download',async()=>{
  for(const result of [{status:200,headers:{}},{status:302,headers:{location:'http://wrong.example/file'}}]){
    const run=handler(state().ready,async()=>result),res=response();
    await run({method:'GET',url:'/api/relevel/download?id='+state().ready.cacheKey},res);
    assert.equal(res.statusCode,502);assert.equal(res.headers.location,undefined);
  }
});
