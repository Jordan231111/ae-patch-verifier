const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

test('headers-only negotiation closes a binary response without reading its body', async () => {
  let closed = false;
  const response = { statusCode:302,headers:{location:'https://storage.example/release.apks'},
    destroy(){closed=true;},setEncoding(){throw Error('binary body was read');},on(){throw Error('binary body was buffered');} };
  const request = {setTimeout(){},on(){},end(){},write(){}};
  const https = {request(options,callback){queueMicrotask(()=>callback(response));return request;}};
  const mod = {exports:{}};
  vm.runInNewContext(fs.readFileSync(path.join(__dirname,'../api/_shared/github.js'),'utf8'),{
    module:mod,require:id=>{assert.equal(id,'https');return https;},Promise,JSON,Buffer,Error,Math,Number,Set,String,process:{env:{}}
  });
  const result = await mod.exports.githubRequest({githubToken:'test'}, {method:'GET',apiPath:'/asset',headersOnly:true});
  assert.equal(result.status,302);
  assert.equal(result.body,'');
  assert.equal(closed,true);
});
