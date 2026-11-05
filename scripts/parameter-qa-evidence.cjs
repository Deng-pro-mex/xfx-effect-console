'use strict';
// Bounded offline snapshot comparisons, not a native/host compatibility certificate.
// Inputs are parsed data from the owned QA runner. Do not use this module as an
// authentication source or accept an imported JSON report to enable native writes.
const stages = ['baseline', 'translated', 'restored', 'reopened'];
const identityFields = ['layerId', 'effectIndex', 'paramIndex', 'effectMatchName', 'paramMatchName'];
function requireThat(value, message) { if (!value) throw new Error(message); }
function record(value, keys, label) {
    requireThat(value !== null && typeof value === 'object' && !Array.isArray(value), 'Invalid ' + label + ' shape');
    const present = Object.keys(value).sort();
    requireThat(present.length === keys.length && present.every((key, i) => key === [...keys].sort()[i]), 'Unexpected or missing ' + label + ' field/shape');
}
function text(value, limit, label, allowEmpty = false) {
    requireThat(typeof value === 'string' && value.length <= limit && (allowEmpty || value.length > 0) && !value.includes('\0'), 'Invalid ' + label + ' text');
}
function finite(value, label) { requireThat(typeof value === 'number' && Number.isFinite(value), 'Nonfinite ' + label + ' numeric shape'); }
function integer(value, low, high, label) { finite(value, label); requireThat(Number.isInteger(value) && value >= low && value <= high, 'Invalid ' + label + ' integer shape'); }
function array(value, maximum, label) { requireThat(Array.isArray(value) && value.length <= maximum, 'Invalid ' + label + ' array shape'); }
function canonical(value) {
    if (Array.isArray(value)) return '[' + value.map(canonical).join(',') + ']';
    if (value !== null && typeof value === 'object') return '{' + Object.keys(value).sort().map(k => JSON.stringify(k) + ':' + canonical(value[k])).join(',') + '}';
    return JSON.stringify(value);
}
function projectFolder(file) { return file.replace(/\\/g, '/').toLowerCase().replace(/\/[^/]*$/, ''); }
function validLabel(value) {
    text(value, 512, 'label');
    requireThat(!/[\x00-\x1f\x7f-\x9f]/u.test(value), 'Invalid label controls');
    for (const scalar of value) {
        const code = scalar.codePointAt(0);
        requireThat(code < 0xd800 || code > 0xdfff, 'Invalid Unicode label scalar');
    }
    requireThat(Buffer.byteLength(value, 'utf8') <= 31, 'Planned label exceeds 31 UTF8 bytes');
}
function targetKey(row) { return canonical(identityFields.map(key => row[key])); }
function semantic(row) { const copy = {...row}; delete copy.name; return canonical(copy); }
function validateHost(host) {
    record(host, ['pid', 'creationTime', 'image', 'version', 'modules'], 'host');
    integer(host.pid, 1, 4294967295, 'host PID');
    text(host.creationTime, 16, 'host creation time'); requireThat(/^[a-f0-9]{16}$/.test(host.creationTime), 'Invalid host creation identity');
    text(host.image, 2048, 'host image'); text(host.version, 64, 'host version');
    record(host.modules, ['FLT', 'TDB', 'U'], 'host modules');
    for (const name of ['FLT', 'TDB', 'U']) requireThat(typeof host.modules[name] === 'string' && /^[a-f0-9]{64}$/.test(host.modules[name]), 'Invalid host module hash');
}
function validateRow(row) {
    record(row, ['layerId', 'layerIndex', 'effectIndex', 'paramIndex', 'effectMatchName', 'paramMatchName', 'effectPropertyCount',
        'name', 'propertyValueType', 'keys', 'samples', 'expression'], 'parameter');
    integer(row.layerId, 1, 2147483647, 'layer identity');
    requireThat(row.layerIndex === 1 && row.paramIndex === 1, 'Unexpected QA parameter identity');
    integer(row.effectIndex, 1, 4, 'effect identity'); integer(row.effectPropertyCount, 1, 16, 'effect group count');
    requireThat(row.effectMatchName === 'ADBE Slider Control' && row.paramMatchName === 'ADBE Slider Control-0001', 'Unexpected QA stream identity');
    text(row.name, 512, 'parameter name'); text(row.propertyValueType, 128, 'property type');
    array(row.keys, 32, 'keys');
    let previous = -Infinity;
    for (const key of row.keys) {
        record(key, ['time', 'value', 'inInterpolation', 'outInterpolation', 'inEase', 'outEase'], 'key');
        finite(key.time, 'key time'); finite(key.value, 'key value'); requireThat(key.time > previous, 'Unordered key timing shape'); previous = key.time;
        text(key.inInterpolation, 128, 'key interpolation'); text(key.outInterpolation, 128, 'key interpolation');
        for (const field of ['inEase', 'outEase']) {
            array(key[field], 4, 'key ease'); requireThat(key[field].length > 0, 'Missing key ease');
            for (const ease of key[field]) {record(ease, ['speed', 'influence'], 'key ease'); finite(ease.speed, 'ease speed'); finite(ease.influence, 'ease influence');}
        }
    }
    array(row.samples, 3, 'samples'); requireThat(row.samples.length === 3, 'Missing QA samples shape');
    row.samples.forEach((sample, index) => {
        record(sample, ['time', 'beforeExpression', 'value'], 'sample'); requireThat(sample.time === [0, 0.5, 1][index], 'Unexpected sample time shape');
        finite(sample.beforeExpression, 'before-expression sample'); finite(sample.value, 'evaluated sample');
    });
    record(row.expression, ['text', 'enabled', 'error'], 'expression');
    text(row.expression.text, 4096, 'expression', true); text(row.expression.error, 4096, 'expression error', true);
    requireThat(typeof row.expression.enabled === 'boolean', 'Unknown expression enabled state');
    requireThat(row.expression.error === '', 'QA expression evaluation error: ' + row.expression.error);
}
function validateSnapshot(snapshot, stage) {
    record(snapshot, ['kind', 'schemaVersion', 'nonce', 'host', 'project', 'comp', 'parameters'], 'snapshot ' + stage);
    requireThat(snapshot.kind === 'xfx-parameter-qa-snapshot' && snapshot.schemaVersion === 1, 'Unknown snapshot schema');
    requireThat(typeof snapshot.nonce === 'string' && /^[a-f0-9]{32}$/.test(snapshot.nonce), 'Invalid snapshot nonce');
    validateHost(snapshot.host);
    record(snapshot.project, ['file', 'dirty'], 'project');
    requireThat(snapshot.project.dirty === false, 'QA dirty state is true or unknown at ' + stage);
    text(snapshot.project.file, 2048, 'project path');
    const suffix = '/xfx-parameter-qa-' + snapshot.nonce + '/' + (stage === 'reopened' ? 'reopened.aep' : 'fixture.aep');
    requireThat(snapshot.project.file.replace(/\\/g, '/').toLowerCase().endsWith(suffix), 'Not an owned QA filename at ' + stage);
    record(snapshot.comp, ['id', 'name', 'width', 'height', 'duration', 'frameRate'], 'comp');
    integer(snapshot.comp.id, 1, 2147483647, 'comp identity');
    requireThat(snapshot.comp.name === '__XFX_PARAMETER_QA__' + snapshot.nonce && snapshot.comp.width === 320 && snapshot.comp.height === 180 &&
        snapshot.comp.duration === 2 && snapshot.comp.frameRate === 24, 'Changed owned QA comp identity');
    array(snapshot.parameters, 4, 'parameters'); requireThat(snapshot.parameters.length === 4, 'Incomplete QA parameter inventory');
    snapshot.parameters.forEach(validateRow);
    const targets = snapshot.parameters.map(targetKey);
    requireThat(new Set(targets).size === 4, 'Duplicate QA target identity');
}
function expressionSelectors(baseline) {
    const source = baseline.parameters[0];
    baseline.parameters.forEach((row, i) => {
        requireThat(row.effectIndex === i + 1 && row.layerId === source.layerId && row.name === source.name, 'Baseline fixture identity/order or original labels differ');
        requireThat(i === 0 || row.keys.length === 0, 'Unexpected keyframes on baseline reference effect');
    });
    requireThat(source.keys.length === 2 && source.keys[0].time === 0 && source.keys[0].value === 10 && source.keys[1].time === 1 && source.keys[1].value === 30 && !source.expression.enabled && source.expression.text === '', 'Missing source keyframe proof');
    requireThat(source.samples.every((sample, i) => sample.beforeExpression === [10, 20, 30][i] && sample.value === sample.beforeExpression), 'Invalid source evaluated samples');
    const quote = value => JSON.stringify(value).replace(/\u2028/g, '\\u2028').replace(/\u2029/g, '\\u2029');
    const selectors = [quote(baseline.parameters[0].name), quote('ADBE Slider Control-0001'), '1'];
    for (let i = 1; i <= 3; i++) {
        const expression = baseline.parameters[i].expression;
        requireThat(expression.enabled && expression.text === 'effect("XFX Source")(' + selectors[i - 1] + ')', 'Missing exact active expression selector proof at index ' + i);
        requireThat(baseline.parameters[i].samples.every((sample, index) => sample.value === source.samples[index].value), 'Expression did not evaluate the source at index ' + i);
    }
}
function inspectQaRun(bundle) {
    const result = {ok:false, violations:[], runtimeEnabled:false, hostVerified:false, dataOnly:true,
        unverified:['actual save/reopen provenance (filenames are not execution evidence)', 'render pixel equivalence', 'undo-stack behavior', 'native ABI and half-success recovery', 'UI acceptance and non-slider/group coverage']};
    try {
        record(bundle, ['kind', 'schemaVersion', 'changes', 'snapshots'], 'QA run');
        requireThat(bundle.kind === 'xfx-parameter-qa-run' && bundle.schemaVersion === 1, 'Unknown QA run schema');
        record(bundle.snapshots, stages, 'stage snapshots');
        for (const stage of stages) validateSnapshot(bundle.snapshots[stage], stage);
        const before = bundle.snapshots.baseline;
        expressionSelectors(before);
        array(bundle.changes, 4, 'changes'); requireThat(bundle.changes.length > 0, 'No real QA label change');
        const targets = new Map(before.parameters.map(row => [targetKey(row), row])), changes = new Map();
        for (const change of bundle.changes) {
            record(change, [...identityFields, 'before', 'after'], 'change');
            const key = targetKey(change);
            requireThat(targets.has(key), 'Planned change target is missing');
            requireThat(!changes.has(key), 'Duplicate planned change target');
            text(change.before, 512, 'before label'); text(change.after, 512, 'after label');
            requireThat(change.before === targets.get(key).name && change.before !== change.after, 'Invalid change original guard or no-op');
            validLabel(change.after);
            changes.set(key, change);
        }
        for (const stage of stages.slice(1)) {
            const after = bundle.snapshots[stage];
            requireThat(projectFolder(after.project.file) === projectFolder(before.project.file), 'Owned project directory identity changed at ' + stage);
            requireThat(after.nonce === before.nonce && canonical(after.host) === canonical(before.host) && canonical(after.comp) === canonical(before.comp), 'Snapshot host/project identity mismatch at ' + stage);
            after.parameters.forEach((row, index) => {
                const original = before.parameters[index], key = targetKey(original);
                requireThat(targetKey(row) === key, 'Parameter identity/order changed at ' + stage);
                requireThat(semantic(row) === semantic(original), 'Parameter semantic data changed at ' + stage + ' target ' + index);
                const name = stage === 'translated' && changes.has(key) ? changes.get(key).after : original.name;
                requireThat(row.name === name, 'Unexpected or unrestored label at ' + stage + ' target ' + index);
            });
        }
        result.ok = true;
    } catch (error) {
        result.violations.push(error && error.message ? error.message : String(error));
    }
    return result;
}
function inspectQaBaseline(snapshot) {
    const result = {ok:false, violations:[], baselineOnly:true, dataOnly:true, runtimeEnabled:false, hostVerified:false,
        unverified:['translation and restoration', 'actual save/reopen provenance', 'render pixel equivalence', 'undo-stack behavior', 'native ABI and UI/group coverage']};
    try {
        validateSnapshot(snapshot, 'baseline');
        expressionSelectors(snapshot);
        result.ok = true;
    } catch (error) {result.violations.push(error && error.message ? error.message : String(error));}
    return result;
}
module.exports = {inspectQaRun, inspectQaBaseline};
