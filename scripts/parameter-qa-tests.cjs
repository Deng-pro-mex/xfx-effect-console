'use strict';
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const assert = require('node:assert/strict');
const code = fs.readFileSync(path.join(__dirname, 'parameter-qa.jsx'), 'utf8');
const nonce = '0123456789abcdef0123456789abcdef';
const directory = '/tmp/XFX-Parameter-QA-' + nonce;
let checks = 0;
const json = value => JSON.parse(JSON.stringify(value));
const hostIdentity = () => ({pid: 123, creationTime: '0123456789abcdef', image: 'C:/Adobe/AfterFX.exe',
    version: '25.4x86', modules: {FLT: '1'.repeat(64), TDB: '2'.repeat(64), U: '3'.repeat(64)}});

function fixture(options = {}) {
    const files = new Map([[directory + '/ownership.txt', 'XFX-PARAMETER-QA\t1\t' + nonce + '\n']]);
    const calls = [];
    function File(file) { this.fsName = String(file).replace(/\\/g, '/'); this.fullName = this.fsName; }
    Object.defineProperties(File.prototype, {exists: {get() { return files.has(this.fsName); }},
        length: {get() { return String(files.get(this.fsName) || '').length; }}});
    File.prototype.open = function(mode) { calls.push('file:' + mode); return mode === 'r' && this.exists; };
    File.prototype.read = function(size) { return files.get(this.fsName).slice(0, size); };
    File.prototype.close = function() {};
    function Folder(folder) { this.fsName = String(folder); }
    Folder.temp = new Folder('/tmp');
    const parameters = [];
    class Param {
        constructor() { this.name = options.original || 'Slider'; this.matchName = 'ADBE Slider Control-0001';
            this.expression = ''; this.expressionEnabled = false; this.expressionError = ''; this.keys = [];
            this.propertyValueType = 'OneD'; this.canSetExpression = true; this.isSpatial = false; }
        setValueAtTime(time, value) { calls.push('set-key'); this.keys.push({time, value}); }
        get numKeys() { return this.keys.length; }
        keyTime(i) { return this.keys[i - 1].time; }
        keyValue(i) { return this.keys[i - 1].value; }
        keyInInterpolationType() { return 'LINEAR'; }
        keyOutInterpolationType() { return 'LINEAR'; }
        keyInTemporalEase() { return [{speed: 0, influence: 33.333}]; }
        keyOutTemporalEase() { return [{speed: 0, influence: 33.333}]; }
        valueAtTime(time, preExpression) {
            if (preExpression || !this.expressionEnabled) {
                if (!this.keys.length) return 0;
                return this.keys[0].value + Math.min(1, Math.max(0, time)) * 20;
            }
            const source = parameters[0];
            const quotedOriginal = JSON.stringify(source.name);
            if (this.expression.includes('XFX Source') && !this.expression.includes('ADBE Slider Control-0001') &&
                !this.expression.endsWith('(1)') && !this.expression.includes(quotedOriginal)) {
                this.expressionError = 'Missing named parameter'; return 0;
            }
            return source.valueAtTime(time, true);
        }
    }
    const effects = {entries: [], canAddProperty(name) { return name === 'ADBE Slider Control'; },
        addProperty(matchName) { calls.push('add-effect'); const param = new Param(); parameters.push(param);
            const effect = {matchName, name: 'Slider Control', numProperties: 1, property(index) { assert.equal(index, 1); return param; }};
            this.entries.push(effect); return effect; },
        property(index) { return typeof index === 'number' ? this.entries[index - 1] : this.entries.find(x => x.name === index); },
        get numProperties() { return this.entries.length; }};
    const layer = {index: 1, id: 7, name: 'XFX Parameter QA', property(name) { assert.equal(name, 'ADBE Effect Parade'); return effects; }};
    const comp = {id: 42, name: '', width: 320, height: 180, duration: 2, frameRate: 24,
        layers: {addSolid() { calls.push('add-solid'); return layer; }}, layer(index) { assert.equal(index, 1); return layer; }, numLayers: 1};
    const items = [];
    const project = {file: null, dirty: false,
        items: {addComp(name) { calls.push('add-comp'); comp.name = name; items.push(comp); return comp; }},
        get numItems() { return items.length; }, item(index) { return items[index - 1]; },
        save(file) { calls.push('save'); if (options.copyNoBind && file.fsName.endsWith('/Reopened.aep')) return; if (options.saveFails) throw Error('disk full'); this.file = file; this.dirty = false; files.set(file.fsName, 'owned fixture'); },
        close() { throw Error('Must never close a project in source-stage QA'); }};
    if (options.userProject) { project.file = new File('/home/user/important.aep'); items.push({name: 'User Comp'}); }
    if (options.unsavedProject) items.push({name: 'Unsaved User Comp'});
    const app = {version: '25.4x86', project, newProject() { throw Error('Must never replace a project'); },
        open() { throw Error('Source-stage QA cannot switch projects'); }, executeCommand() { throw Error('No menu automation'); }};
    const api = vm.runInNewContext(code, {app, File, Folder, Date, Error, String, Number, Math, isFinite});
    const provider = {identity: hostIdentity};
    function request(action, extra = {}) { return {schemaVersion: 1, action, nonce, expires: Date.now() + 60000,
        expectedHost: hostIdentity(), ...extra}; }
    function run(action, extra) { return json(api.run(request(action, extra), provider)); }
    return {api, app, files, calls, project, parameters, effects, comp, layer, provider, request, run};
}
function test(name, fn) { fn(); checks++; console.log('PASS ' + name); }
function refused(result, pattern) { assert.equal(result.ok, false); assert.match(result.error, pattern); }
function create(f) { const result = f.run('create'); assert.equal(result.ok, true, result.error); return result; }

test('script evaluation alone performs no host or file operation', () => {
    const f = fixture(); assert.equal(typeof f.api.run, 'function'); assert.deepEqual(f.calls, []);
});
test('saved and unsaved user projects are refused without any mutation', () => {
    for (const options of [{userProject: true}, {unsavedProject: true}]) {
        const f = fixture(options); const file = f.project.file; const count = f.project.numItems;
        refused(f.run('create'), /empty|空工程/); assert.strictEqual(f.project.file, file);
        assert.equal(f.project.numItems, count); assert(!f.calls.some(x => ['add-comp', 'add-solid', 'save'].includes(x)));
    }
});
test('only an unexpired nonce and exact host tuple can reach fixture actions', () => {
    for (const extra of [{nonce: '../escape'}, {expires: Date.now() - 1}, {expires: Date.now() + 3600000}, {schemaVersion: 2},
        {expectedHost: {...hostIdentity(), pid: 124}}, {expectedHost: {...hostIdentity(), creationTime: 'f'.repeat(16)}},
        {expectedHost: {...hostIdentity(), modules: {...hostIdentity().modules, U: 'f'.repeat(64)}}}]) {
        const f = fixture(); refused(f.run('create', extra), /request|host|请求|宿主/);
        assert(!f.calls.includes('add-comp'));
    }
});
test('unknown actions and caller paths cannot become project/file operations', () => {
    for (const action of ['close', 'open', 'switch', 'evaluate', 'applyLabels']) {
        const f = fixture(); refused(f.run(action), /action|操作/); assert(!f.calls.includes('add-comp'));
    }
    const f = fixture(); refused(f.run('create', {output: '/home/user/important.aep'}), /field|字段/);
});
test('ownership marker must be exact bounded read-only data', () => {
    for (const marker of [null, 'wrong', 'XFX-PARAMETER-QA\t1\t' + 'f'.repeat(32) + '\n', 'x'.repeat(257)]) {
        const f = fixture(); if (marker === null) f.files.delete(directory + '/ownership.txt');
        else f.files.set(directory + '/ownership.txt', marker);
        refused(f.run('create'), /ownership|所有权/); assert(!f.calls.includes('add-comp'));
        assert(f.calls.every(x => !x.startsWith('file:') || x === 'file:r'));
    }
});
test('create owns fixed names and prepares three actual expression selector styles', () => {
    const f = fixture({original: '滑块"\\值'}); const result = create(f);
    assert.equal(result.snapshot.project.file, directory + '/Fixture.aep');
    assert.equal(result.snapshot.parameters.length, 4);
    assert.equal(result.snapshot.project.dirty, false);
    assert.equal(f.comp.name, '__XFX_PARAMETER_QA__' + nonce);
    assert.equal(f.parameters[0].numKeys, 2);
    assert(f.parameters[1].expression.includes(JSON.stringify('滑块"\\值')));
    assert(f.parameters[2].expression.includes('ADBE Slider Control-0001'));
    assert(f.parameters[3].expression.endsWith('(1)'));
    assert(f.parameters.slice(1).every(x => x.expressionEnabled));
});
test('snapshot retains values keys expressions samples and stable IDs without edits', () => {
    const f = fixture(); create(f); const calls = f.calls.length;
    const result = f.run('snapshot'); assert.equal(result.ok, true, result.error);
    assert(!f.calls.slice(calls).some(x => ['save', 'set-key', 'add-effect', 'add-comp'].includes(x)));
    const row = result.snapshot.parameters[0];
    assert.equal(row.effectMatchName, 'ADBE Slider Control'); assert.equal(row.paramMatchName, 'ADBE Slider Control-0001');
    assert.equal(row.layerId, 7); assert.equal(row.keys.length, 2);
    assert.equal(row.keys[1].time, 1); assert.equal(row.keys[1].value, 30);
    assert.deepEqual(row.keys[0].inEase, [{speed: 0, influence: 33.333}]);
    assert.deepEqual(row.samples.map(x => x.time), [0, 0.5, 1]);
    assert.deepEqual(row.samples.map(x => x.beforeExpression), [10, 20, 30]);
    assert.equal(result.snapshot.parameters[1].expression.text, f.parameters[1].expression);
});
test('read-only snapshots echo a bounded sample challenge but mutations cannot use one', () => {
    const f=fixture();create(f);const sampleId='a'.repeat(32),before=f.calls.length;
    const result=f.run('snapshot',{sampleId});assert.equal(result.ok,true,result.error);assert.equal(result.sampleId,sampleId);
    assert(!f.calls.slice(before).some(x=>['save','set-key','add-effect','add-comp'].includes(x)));
    for(const bad of ['../escape','A'.repeat(32),'a'.repeat(31),null,123])refused(f.run('snapshot',{sampleId:bad}),/sample/);
    for(const action of ['create','saveCopy'])refused(f.run(action,{sampleId}),/sample/);
});
test('built-in extra effect properties do not invalidate the first slider identity', () => {
    const f = fixture(); create(f);
    f.effects.entries[0].numProperties = 2; // AE can append compositing-options groups.
    const result = f.run('snapshot'); assert.equal(result.ok, true, result.error);
    assert.equal(result.snapshot.parameters[0].effectPropertyCount, 2);
    f.parameters[0].matchName = 'Different plugin parameter';
    refused(f.run('snapshot'), /identity/);
});
test('multiline expression text is preserved byte-for-byte in JSON rather than executed', () => {
    const f = fixture(); create(f);
    const expression = '// 注释\n' + f.parameters[2].expression + ';\r\n';
    f.parameters[2].expression = expression;
    const result = JSON.parse(f.api.runJson(f.request('snapshot'), f.provider));
    assert.equal(result.ok, true, result.error);
    assert.equal(result.snapshot.parameters[2].expression.text, expression);
});
test('snapshot exposes named-expression errors instead of hiding the failure', () => {
    const f = fixture(); create(f); f.parameters[0].name = '源数值';
    const result = f.run('snapshot'); assert.equal(result.ok, true, result.error);
    assert.equal(result.snapshot.parameters[0].name, '源数值');
    assert.match(result.snapshot.parameters[1].expression.error, /Missing/);
});
test('unknown dirty status or nonfinite values cannot look like successful proof', () => {
    for (const change of [f => {f.project.dirty = undefined;}, f => {f.parameters[0].keys[0].value = Infinity;}]) {
        const f = fixture(); create(f); change(f); refused(f.run('snapshot'), /dirty|finite|状态|数值/);
    }
});
test('missing ownership identity or another project refuses snapshot and save copy', () => {
    for (const action of ['snapshot', 'saveCopy']) {
        const f = fixture(); create(f); f.project.file = {fsName: '/home/user/important.aep'};
        refused(f.run(action), /owned|自有/);
        f.project.file = {fsName: directory + '/Fixture.aep'}; f.comp.name = 'Changed by someone else';
        refused(f.run(action), /owned|自有/);
    }
});
test('save copy only targets the reserved nonexisting QA copy and never closes AE', () => {
    const f = fixture(); create(f); const result = f.run('saveCopy'); assert.equal(result.ok, true, result.error);
    assert.equal(f.project.file.fsName, directory + '/Reopened.aep');
    refused(f.run('saveCopy'), /exists|已有/);
    assert(f.files.has(directory + '/Fixture.aep'));
});
test('save copy silent no-op is never reported as successful binding', () => {
    const f = fixture({copyNoBind: true}); create(f);
    refused(f.run('saveCopy'), /bind|save|保存/);
});
test('save failure reports retained QA state rather than closing or overwriting anything', () => {
    const f = fixture({saveFails: true}); refused(f.run('create'), /disk full/);
    assert.equal(f.project.file, null); assert.equal(f.project.numItems, 1);
    assert(!f.files.has(directory + '/Fixture.aep'));
});
test('no JSON global or custom values can cause eval or provider-code execution', () => {
    const f = fixture(); create(f); const result = JSON.parse(f.api.runJson(f.request('snapshot'), f.provider));
    assert.equal(result.ok, true, result.error); assert.equal(result.snapshot.host.pid, 123);
    assert.equal(result.runtimeEnabled, false); assert.equal(result.hostVerified, false);
});
console.log('Parameter QA VM groups passed: ' + checks + ' (no AE process/project used)');
