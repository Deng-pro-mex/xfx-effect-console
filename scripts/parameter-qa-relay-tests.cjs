'use strict';
const assert = require('node:assert/strict');
const vm = require('node:vm');
const {parseHostIdentity, buildQaRelay} = require('./parameter-qa-relay.cjs');
let checks = 0;
const escaped = value => Array.from({length:value.length}, (_, i) => '\\u' + value.charCodeAt(i).toString(16).padStart(4, '0')).join('');
const host = {pid:123, creationTime:'1'.repeat(16), image:'C:/Adobe/中\\AfterFX.exe', version:'25.4x86', modules:{FLT:'1'.repeat(64), TDB:'2'.repeat(64), U:'3'.repeat(64)}};
function wire(h = host) {return '{"pid":' + h.pid + ',"creationTime":"' + h.creationTime + '","image":"' + escaped(h.image) + '","version":"' + h.version + '","modules":{"FLT":"' + h.modules.FLT + '","TDB":"' + h.modules.TDB + '","U":"' + h.modules.U + '"}}';}
const nonce = '0123456789abcdef0123456789abcdef';
function options() {return {request:{schemaVersion:1,action:'snapshot',nonce,expires:Date.now() + 60000,expectedHost:host},
    helperPath:'C:/Tools/XFXQA/qa_host_identity.dll', tempRoot:'C:/Temp'};}
function test(name, run) {run(); checks++; console.log('PASS ' + name);}
function renameInMemory(File, files) {
    File.prototype.rename=function(name){const old=this.fsName;const next=old.slice(0,old.lastIndexOf('/')+1)+name;if(files.has(next))return false;files.set(next,files.get(old));files.delete(old);this.fsName=next;return true;};
}
test('strict legacy identity parser reads fixed native shape without JSON or eval', () => {
    const context = vm.createContext({JSON:undefined});
    assert.deepEqual(JSON.parse(JSON.stringify(vm.runInContext('(' + parseHostIdentity.toString() + ')(' + JSON.stringify(wire()) + ')', context))), host);
});
test('identity response rejects alternate encoding extra fields malformed hashes and coercion', () => {
    for (const raw of ['', '{}', wire().replace('123,', '0,'), wire().replace('25.4x86', '25.4x86\\n'),
        wire().replace('\\u0043', 'C'), wire().replace('"U":"333', '"U":"zzz'), wire().slice(0, -1) + ',"enabled":true}',
        wire().replace('"pid":123', '"pid":"123"')]) assert.throws(() => parseHostIdentity(raw));
});
test('relay embeds only validated serialized request data and deterministic output', () => {
    const o = options(); const result = buildQaRelay(o);
    assert.equal(result.outputPath, 'C:/Temp/XFX-Parameter-QA-' + nonce + '/result-snapshot.json');
    assert(result.source.includes('new ExternalObject(')); assert(result.source.includes('qa_host_identity.dll'));
    assert(result.source.includes('identity()')); assert(result.source.includes('runJson(request, provider)'));
    assert(!result.source.includes('app.open(')); assert(!result.source.includes('app.newProject('));
    assert(result.source.includes('finally')); assert(result.source.includes('.unload()'));
});
test('snapshot sample IDs make unique fixed receipts but cannot repeat mutation actions', () => {
    const o=options();o.request.sampleId='a'.repeat(32);
    const relay=buildQaRelay(o);assert.equal(relay.outputPath,'C:/Temp/XFX-Parameter-QA-'+nonce+'/result-snapshot-'+o.request.sampleId+'.json');
    assert(relay.source.includes('result-snapshot-'+o.request.sampleId+'.json.pending'));
    for(const action of ['create','saveCopy']){o.request.action=action;assert.throws(()=>buildQaRelay(o),/sample|field/);}
    o.request.action='snapshot';o.request.sampleId='../escape';assert.throws(()=>buildQaRelay(o),/sample/);
});
test('caller cannot inject source paths actions outputs or extra host properties', () => {
    const changes = [o => {o.extra = 'eval()';}, o => {o.request.action = 'close';}, o => {o.request.output = 'C:/User.aep';},
        o => {o.request.nonce = '../escape';}, o => {o.helperPath = 'https://example.com/a.dll';}, o => {o.helperPath = 'C:/Tools/../AE.dll';},
        o => {o.helperPath = 'C:/Tools/qa_host_identity.dll:ads';}, o => {o.tempRoot = 'C:/Temp/../User';}, o => {o.request.expectedHost = {...host, enabled:true};}];
    for (const change of changes) {const o = options(); change(o); assert.throws(() => buildQaRelay(o));}
});
test('generated relay evaluates the real QA factory and unloads helper on refusal', () => {
    const o = options(), relay = buildQaRelay(o), files = new Map(), calls = [];
    files.set('C:/Temp/XFX-Parameter-QA-' + nonce + '/ownership.txt', 'XFX-PARAMETER-QA\t1\t' + nonce + '\n');
    function File(name) {this.fsName = name;}
    Object.defineProperties(File.prototype, {exists:{get(){return files.has(this.fsName);}}, length:{get(){return (files.get(this.fsName) || '').length;}}});
    File.prototype.open = function(mode) {calls.push(mode); return true;};
    File.prototype.read = function(size) {return files.get(this.fsName).slice(0, size);};
    File.prototype.write = function(text) {files.set(this.fsName, text); return true;};
    File.prototype.close = function() {return true;};
    function ExternalObject() {return {identity:() => wire(), unload:() => calls.push('unload')};}
    const app = {version:host.version, project:{file:{fsName:'C:/User/Important.aep'}, numItems:1}};
    renameInMemory(File,files);
    vm.runInNewContext(relay.source, {File, Folder:{temp:{fsName:'C:/Temp'}}, app, ExternalObject, Date});
    const result = JSON.parse(files.get(relay.outputPath));
    assert.equal(result.ok, false); assert.match(result.error, /owned/);
    assert.equal(result.runtimeEnabled, false); assert.equal(calls.filter(x => x === 'unload').length, 1);
    assert.equal(app.project.file.fsName, 'C:/User/Important.aep');
});
test('helper load errors still produce a diagnostic result without any project access', () => {
    const o = options(), relay = buildQaRelay(o), files = new Map();
    function File(name) {this.fsName = name;}
    Object.defineProperty(File.prototype, 'exists', {get(){return files.has(this.fsName);}});
    File.prototype.open = function(){return true;}; File.prototype.write = function(value){files.set(this.fsName, value); return true;}; File.prototype.close = function(){return true;};
    function ExternalObject(){throw Error('Cannot load helper');}
    const app = {version:host.version};
    Object.defineProperty(app, 'project', {get(){throw Error('Must not access project after failed identity');}});
    renameInMemory(File,files);
    vm.runInNewContext(relay.source, {File, Folder:{temp:{fsName:'C:/Temp'}}, app, ExternalObject, Date});
    const result = JSON.parse(files.get(relay.outputPath)); assert.equal(result.ok, false); assert.match(result.error, /Cannot load helper/);
});
test('existing or unopenable receipt refuses before loading any helper or touching a project', () => {
    for(const mode of ['exists','pendingExists','openFailed']){
        const relay=buildQaRelay(options());let loads=0,reads=0;
        function File(name){this.fsName=name;}
        Object.defineProperty(File.prototype,'exists',{get(){return mode==='exists'||(mode==='pendingExists'&&this.fsName.endsWith('.pending'));}});
        File.prototype.open=function(){return false;};File.prototype.close=function(){return true;};
        function ExternalObject(){loads++;throw Error('Must not load');}
        const app={version:host.version};Object.defineProperty(app,'project',{get(){reads++;throw Error('Must not read project');}});
        assert.throws(()=>vm.runInNewContext(relay.source,{File,Folder:{temp:{fsName:'C:/Temp'}},ExternalObject,app,Date}), /output already exists|Cannot create owned QA result/);
        assert.equal(loads,0);assert.equal(reads,0);
    }
});
test('serializer failure has an independent receipt and never retries factory execution', () => {
    const o=options(), relay=buildQaRelay(o), files=new Map(); let identityCalls=0, projectReads=0;
    function File(name){this.fsName=name;}
    Object.defineProperty(File.prototype,'exists',{get(){return files.has(this.fsName);}});
    File.prototype.open=function(){return true;}; File.prototype.write=function(value){files.set(this.fsName,value);return true;}; File.prototype.close=function(){return true;};
    function ExternalObject(){return {identity(){identityCalls++;return wire();},unload(){}};}
    const app={version:host.version}; Object.defineProperty(app,'project',{get(){projectReads++;throw Error('Do not read project');}});
    renameInMemory(File,files);
    const context=vm.createContext({File,Folder:{temp:{fsName:'C:/Temp'}},ExternalObject,app,Date,JSON:undefined});
    vm.runInContext('Object.prototype.hasOwnProperty.call=undefined;',context);
    vm.runInContext(relay.source,context);
    const result=JSON.parse(files.get(relay.outputPath));
    assert.equal(result.ok,false); assert.equal(result.stage,'execute'); assert.equal(result.requestMayHaveRun,true);
    assert.equal(identityCalls,0); assert.equal(projectReads,0);
});
test('hostile error getter and unload failure get bounded independent receipts', () => {
    for(const failure of ['load','unload']){
        const o=options(),relay=buildQaRelay(o),files=new Map();let loads=0;
        function File(name){this.fsName=name;}
        Object.defineProperty(File.prototype,'exists',{get(){return false;}});
        File.prototype.open=function(){return true;}; File.prototype.write=function(value){files.set(this.fsName,value);return true;}; File.prototype.close=function(){return true;};
        function ExternalObject(){loads++;if(failure==='load')throw {get message(){throw Error('message getter');}};return {identity(){return '{}';},unload(){throw Error('unload failed');}};}
        renameInMemory(File,files);
        vm.runInNewContext(relay.source,{File,Folder:{temp:{fsName:'C:/Temp'}},ExternalObject,app:{version:host.version},Date,JSON:undefined});
        const result=JSON.parse(files.get(relay.outputPath));assert.equal(result.ok,false);assert.equal(result.stage,failure);assert.equal(loads,1);
        assert(result.error.length<=1024);assert.equal(result.runtimeEnabled,false);assert.equal(result.hostVerified,false);
    }
});
test('result write or close failure cannot look like completed transport', () => {
    for (const failure of ['write', 'close']) {
        const o = options(), relay = buildQaRelay(o), files=new Map();
        function File(name) {this.fsName=name;}
        Object.defineProperty(File.prototype, 'exists', {get(){return files.has(this.fsName);}});
        File.prototype.open=function(){files.set(this.fsName,'');return true;}; File.prototype.write=function(value){files.set(this.fsName,value);return failure!=='write';}; File.prototype.close=function(){return failure!=='close';};
        renameInMemory(File,files);
        function ExternalObject(){throw Error('No helper');}
        assert.throws(()=>vm.runInNewContext(relay.source,{File,Folder:{temp:{fsName:'C:/Temp'}},app:{version:host.version},ExternalObject,Date}), /QA result (write|close) failed/);
        assert.equal(files.has(relay.outputPath),false,'failed output must not publish even a complete success-looking JSON');
        assert.equal(files.has(relay.outputPath+'.pending'),true,'pending attempt must remain to prevent replay');
    }
});
test('publish failure retains pending and never overwrites a result that appeared meanwhile', () => {
    for(const failure of ['rename','appeared']){
        const relay=buildQaRelay(options()),files=new Map();let renames=0;
        function File(name){this.fsName=name;}
        Object.defineProperty(File.prototype,'exists',{get(){return files.has(this.fsName);}});
        File.prototype.open=function(){files.set(this.fsName,'');return true;};
        File.prototype.write=function(value){files.set(this.fsName,value);if(failure==='appeared')files.set(relay.outputPath,'original other result');return true;};
        File.prototype.close=function(){return true;};File.prototype.rename=function(){renames++;return false;};
        function ExternalObject(){throw Error('No helper');}
        assert.throws(()=>vm.runInNewContext(relay.source,{File,Folder:{temp:{fsName:'C:/Temp'}},app:{version:host.version},ExternalObject,Date}), /publish failed|appeared during/);
        assert.equal(files.has(relay.outputPath+'.pending'),true);
        assert.equal(renames,failure==='rename'?1:0);
        assert.equal(files.get(relay.outputPath),failure==='appeared'?'original other result':undefined);
    }
});
test('JSON serialization keeps script metacharacters as inert escaped data', () => {
    const o = options(); o.helperPath = 'C:/Tools/odd"\\u2028/qa_host_identity.dll';
    const result = buildQaRelay(o); assert(result.source.includes('odd\\"'));
    assert(!result.source.includes('\u2028'));
    const chinese = options(); chinese.helperPath = 'C:/工具/qa_host_identity.dll';
    assert(!/[^\x00-\x7f]/.test(buildQaRelay(chinese).source), 'relay source must be ASCII even on Chinese paths');
});
console.log('Parameter QA relay groups passed: ' + checks + ' (source generation only)');
