'use strict';
const assert = require('node:assert/strict');
const {inspectQaRun, inspectQaBaseline} = require('./parameter-qa-evidence.cjs');
const clone = x => JSON.parse(JSON.stringify(x));
let checks = 0;
function fixture() {
    const nonce = '0123456789abcdef0123456789abcdef';
    const host = {pid: 123, creationTime: '1'.repeat(16), image: 'C:/Adobe/AfterFX.exe', version: '25.4x86',
        modules: {FLT: '1'.repeat(64), TDB: '2'.repeat(64), U: '3'.repeat(64)}};
    const baseline = {kind:'xfx-parameter-qa-snapshot', schemaVersion:1, nonce, host,
        project:{file:'C:/Temp/XFX-Parameter-QA-' + nonce + '/Fixture.aep', dirty:false},
        comp:{id:42, name:'__XFX_PARAMETER_QA__' + nonce, width:320, height:180, duration:2, frameRate:24},
        parameters:Array.from({length:4}, (_, i) => ({layerId:7, layerIndex:1, effectIndex:i + 1, paramIndex:1,
            effectMatchName:'ADBE Slider Control', paramMatchName:'ADBE Slider Control-0001', effectPropertyCount:2,
            name:'Slider', propertyValueType:'OneD', keys:i ? [] : [{time:0, value:10, inInterpolation:'LINEAR', outInterpolation:'LINEAR',
                inEase:[{speed:0, influence:33}], outEase:[{speed:0, influence:33}]},
                {time:1, value:30, inInterpolation:'LINEAR', outInterpolation:'LINEAR', inEase:[{speed:0, influence:33}], outEase:[{speed:0, influence:33}]}],
            samples:[0, 0.5, 1].map(time => ({time, beforeExpression:i ? 0 : 10 + time * 20, value:10 + time * 20})),
            expression:{text:i ? ['effect("XFX Source")("Slider")', 'effect("XFX Source")("ADBE Slider Control-0001")', 'effect("XFX Source")(1)'][i - 1] : '',
                enabled:!!i, error:''}}))};
    const changes = baseline.parameters.map(row => ({layerId:row.layerId, effectIndex:row.effectIndex, paramIndex:row.paramIndex,
        effectMatchName:row.effectMatchName, paramMatchName:row.paramMatchName, before:row.name, after:'源数值'}));
    const translated = clone(baseline); translated.parameters.forEach(row => {row.name = '源数值';});
    const restored = clone(baseline), reopened = clone(baseline);
    reopened.project.file = reopened.project.file.replace('Fixture.aep', 'Reopened.aep');
    return {kind:'xfx-parameter-qa-run', schemaVersion:1, changes, snapshots:{baseline, translated, restored, reopened}};
}
function test(name, action) { action(); checks++; console.log('PASS ' + name); }
function reject(bundle, pattern) { const result = inspectQaRun(bundle); assert.equal(result.ok, false); assert.match(result.violations.join('\n'), pattern); }

test('a real baseline is independently inspectable without inventing later stages', () => {
    const baseline=fixture().snapshots.baseline, before=clone(baseline),result=inspectQaBaseline(baseline);
    assert.equal(result.ok,true,result.violations.join('\n')); assert.equal(result.baselineOnly,true);
    assert.equal(result.runtimeEnabled,false);assert.equal(result.hostVerified,false);assert.equal(result.dataOnly,true);
    assert.deepEqual(baseline,before);assert(result.unverified.some(x=>x.includes('translation')));
    const corrupt=clone(baseline);corrupt.parameters[1].samples[0].value=99;
    assert.equal(inspectQaBaseline(corrupt).ok,false);
});
test('baseline must actually be one ordered four-effect fixture with no extra reference keys', () => {
    for(const change of [b=>b.parameters.reverse(),b=>{b.parameters[1].layerId++;},b=>{b.parameters[1].keys=clone(b.parameters[0].keys);},b=>{b.parameters[3].name='Unexpected';}]){
        const baseline=fixture().snapshots.baseline;change(baseline);assert.equal(inspectQaBaseline(baseline).ok,false);
    }
});
test('valid four-stage semantic comparison is not an enabling certificate', () => {
    const bundle = fixture(), before = clone(bundle); const result = inspectQaRun(bundle);
    assert.equal(result.ok, true, result.violations.join('\n'));
    assert.equal(result.runtimeEnabled, false); assert.equal(result.hostVerified, false);
    assert.deepEqual(bundle, before);
});
test('all stages and a real label change are required', () => {
    for (const stage of ['baseline', 'translated', 'restored', 'reopened']) { const b = fixture(); delete b.snapshots[stage]; reject(b, /snapshot|stage/); }
    const b = fixture(); b.changes = []; reject(b, /change/);
    const noOp = fixture(); noOp.changes[0].after = noOp.changes[0].before; reject(noOp, /change|no-op/);
});
test('snapshot host and project identities cannot be mixed across runs', () => {
    for (const change of [b => {b.snapshots.translated.host.pid++;}, b => {b.snapshots.restored.host.modules.U = 'f'.repeat(64);},
        b => {b.snapshots.reopened.nonce = 'f'.repeat(32);}, b => {b.snapshots.translated.project.file = 'C:/Users/me/real.aep';},
        b => {b.snapshots.translated.project.file = b.snapshots.translated.project.file.replace('C:/Temp', 'D:/Other');}]) {
        const b = fixture(); change(b); reject(b, /identity|owned|nonce|host/);
    }
});
test('dirty unknown status and unexpected shape are never accepted as false', () => {
    for (const dirty of [true, null, undefined, 'false']) {const b = fixture(); b.snapshots.translated.project.dirty = dirty; reject(b, /dirty/);}
    const b = fixture(); delete b.snapshots.baseline.parameters[0].keys; reject(b, /keys|shape/);
});
test('numeric values and interpolation/ease/key timing must remain exact', () => {
    for (const change of [r => {r.samples[0].value++;}, r => {r.samples[1].beforeExpression++;}, r => {r.keys[0].time = 0.25;},
        r => {r.keys[0].value++;}, r => {r.keys[0].inEase[0].speed++;}, r => {r.keys[0].outInterpolation = 'HOLD';}]) {
        const b = fixture(); change(b.snapshots.translated.parameters[0]); reject(b, /semantic/);
    }
});
test('expression text enabled error and evaluated samples are independently checked', () => {
    for (const change of [r => {r.expression.text += ';';}, r => {r.expression.enabled = false;}, r => {r.expression.error = 'Missing named parameter';},
        r => {r.samples[1].value = -1;}]) {
        const b = fixture(); change(b.snapshots.translated.parameters[1]); reject(b, /expression|semantic/);
    }
});
test('stable identity property type group count and order cannot change', () => {
    for (const change of [r => {r.paramMatchName += '-changed';}, r => {r.effectMatchName += 'Other';}, r => {r.layerId++;},
        r => {r.propertyValueType = 'Color';}, r => {r.effectPropertyCount++;}]) {
        const b = fixture(); change(b.snapshots.translated.parameters[0]); reject(b, /identity|semantic/);
    }
    const b = fixture(); b.snapshots.translated.parameters.reverse(); reject(b, /identity/);
});
test('no unexpected rename or missing planned target can look like a pass', () => {
    const b = fixture(); b.snapshots.translated.parameters[0].name = 'Different'; reject(b, /label/);
    const missing = fixture(); missing.changes.pop(); reject(missing, /label/);
    const orphan = fixture(); orphan.changes[0].effectIndex = 99; reject(orphan, /target/);
    const duplicate = fixture(); duplicate.changes[1] = clone(duplicate.changes[0]); reject(duplicate, /[Dd]uplicate/);
});
test('restore and reopened copy must preserve every original label and semantic field', () => {
    for (const stage of ['restored', 'reopened']) {
        const b = fixture(); b.snapshots[stage].parameters[0].name = '源数值'; reject(b, /label/);
        const c = fixture(); c.snapshots[stage].parameters[0].keys[0].value = 100; reject(c, /semantic/);
    }
});
test('nonfinite scalars missing samples and malformed hashes are refused', () => {
    const a = fixture(); a.snapshots.baseline.parameters[0].samples[0].value = NaN; reject(a, /finite|shape/);
    const b = fixture(); b.snapshots.translated.parameters[0].samples.pop(); reject(b, /samples|shape/);
    const c = fixture(); c.snapshots.baseline.host.modules.FLT = 'missing'; reject(c, /hash|host/);
});
test('baseline must contain all three distinct active expression selectors', () => {
    const a = fixture(); a.snapshots.baseline.parameters[1].expression.enabled = false; reject(a, /selector|expression/);
    const b = fixture(); b.snapshots.baseline.parameters[2].expression.text = ''; reject(b, /selector|expression/);
});
test('commented selectors and constant evaluated values are not expression evidence', () => {
    const comment = fixture();
    for (const snapshot of Object.values(comment.snapshots)) for (let i = 1; i <= 3; i++) {
        snapshot.parameters[i].expression.text = '// ' + snapshot.parameters[i].expression.text + '\n0';
        snapshot.parameters[i].samples.forEach(sample => {sample.value = 0;});
    }
    reject(comment, /selector|expression|source/);
    const constant = fixture();
    for (const snapshot of Object.values(constant.snapshots)) snapshot.parameters[1].samples.forEach(sample => {sample.value = 9;});
    reject(constant, /source|expression/);
});
test('planned label controls lone surrogates and byte overflow are refused', () => {
    for (const label of ['bad\nlabel', 'bad\tlabel', '\ud800', '\udfff', '中'.repeat(11)]) {
        const b = fixture(); b.changes[0].after = label; b.snapshots.translated.parameters[0].name = label;
        reject(b, /label|Unicode|UTF8/);
    }
});
test('this offline contract explicitly leaves renderer undo native ABI and UI acceptance unverified', () => {
    const result = inspectQaRun(fixture());
    for (const missing of ['render', 'undo', 'native', 'UI']) assert(result.unverified.some(x => x.includes(missing)));
    assert.equal(result.ok, true);
});
console.log('Parameter QA evidence groups passed: ' + checks + ' (offline data only)');
