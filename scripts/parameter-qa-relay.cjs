'use strict';
const fs = require('node:fs');
const path = require('node:path');
// This function is also embedded into old ExtendScript; keep ES3 syntax and
// avoid JSON.parse/eval. Accept only the helper's exact fixed-order wire form.
function parseHostIdentity(raw) {
    if (typeof raw !== 'string' || raw.length > 200000) throw new Error('Invalid identity response');
    var match = /^\{"pid":([1-9][0-9]{0,9}),"creationTime":"([a-f0-9]{16})","image":"((?:\\u[0-9a-f]{4}){1,32768})","version":"([0-9]{1,3}\.[0-9]{1,3}(?:\.[0-9]{1,5})?x[0-9]{1,5})","modules":\{"FLT":"([a-f0-9]{64})","TDB":"([a-f0-9]{64})","U":"([a-f0-9]{64})"\}\}$/.exec(raw);
    if (!match || Number(match[1]) > 4294967295) throw new Error('Invalid native identity shape');
    var image = match[3].replace(/\\u([0-9a-f]{4})/g, function (_, hex) {return String.fromCharCode(parseInt(hex, 16));});
    if (/[\x00-\x1f\x7f]/.test(image)) throw new Error('Invalid native image');
    for (var i = 0; i < image.length; i++) {
        var code = image.charCodeAt(i);
        if (code >= 0xd800 && code <= 0xdbff) {
            var next = image.charCodeAt(++i);
            if (!(next >= 0xdc00 && next <= 0xdfff)) throw new Error('Invalid native Unicode path');
        } else if (code >= 0xdc00 && code <= 0xdfff) throw new Error('Invalid native Unicode path');
    }
    return {pid:Number(match[1]), creationTime:match[2], image:image, version:match[4], modules:{FLT:match[5], TDB:match[6], U:match[7]}};
}
// Independent ES3 failure encoder. Never re-enters the factory/provider/project
// when factory execution or its serializer fails, including hostile error getters.
function failureJson(error, stage) {
    var message = 'Unreportable QA error';
    try { message = String(error && error.message ? error.message : error); } catch (_) {}
    message = message.slice(0, 1024);
    function quote(value) {
        return '"' + value.replace(/["\\\x00-\x1f\u007f-\uffff]/g, function (unit) {
            return '\\u' + ('0000' + unit.charCodeAt(0).toString(16)).slice(-4);
        }) + '"';
    }
    return '{"ok":false,"error":' + quote(message) + ',"stage":' + quote(stage) +
        ',"requestMayHaveRun":' + (stage === 'execute' || stage === 'unload' ? 'true' : 'false') +
        ',"runtimeEnabled":false,"hostVerified":false}';
}
function fields(value, keys, label) {
    if (!value || typeof value !== 'object' || Array.isArray(value) || Object.keys(value).sort().join('|') !== [...keys].sort().join('|')) throw Error('Invalid ' + label + ' fields');
}
function safePath(value, label) {
    if (typeof value !== 'string' || value.length > 2048 || /[\x00-\x1f\x7f]/.test(value)) throw Error('Invalid ' + label);
    const normalized = value.replace(/\\/g, '/');
    if (!/^[A-Za-z]:\//.test(normalized) || normalized.slice(2).includes(':') || normalized.split('/').some(piece => piece === '..' || piece === '.') || normalized.includes('//')) throw Error('Invalid absolute ' + label);
    return normalized.replace(/\/$/, '');
}
function encode(value) {return JSON.stringify(value).replace(/[\u007f-\uffff]/g, unit => '\\u' + unit.charCodeAt(0).toString(16).padStart(4, '0'));}
function buildQaRelay(options) {
    fields(options, ['request', 'helperPath', 'tempRoot'], 'relay');
    const request = options.request;
    const hasSample = request && Object.prototype.hasOwnProperty.call(request, 'sampleId');
    fields(request, ['schemaVersion', 'action', 'nonce', 'expires', 'expectedHost', ...(hasSample ? ['sampleId'] : [])], 'request');
    if (hasSample && (request.action !== 'snapshot' || typeof request.sampleId !== 'string' || !/^[a-f0-9]{32}$/.test(request.sampleId))) throw Error('Invalid snapshot sample ID');
    if (request.schemaVersion !== 1 || !['create', 'snapshot', 'saveCopy'].includes(request.action) || !/^[a-f0-9]{32}$/.test(request.nonce) ||
        !Number.isFinite(request.expires) || request.expires <= Date.now() || request.expires > Date.now() + 300000) throw Error('Invalid bounded QA request');
    fields(request.expectedHost, ['pid', 'creationTime', 'image', 'version', 'modules'], 'expected host');
    fields(request.expectedHost.modules, ['FLT', 'TDB', 'U'], 'expected modules');
    // Reuse the fixed native parser as schema validation without trusting input
    // source snippets. Only serialize validated plain JSON data below.
    const host = request.expectedHost;
    const image = typeof host.image === 'string' ? Array.from({length:host.image.length}, (_, i) => '\\u' + host.image.charCodeAt(i).toString(16).padStart(4, '0')).join('') : '';
    parseHostIdentity('{"pid":' + encode(host.pid) + ',"creationTime":' + encode(host.creationTime) + ',"image":"' + image + '","version":' + encode(host.version) + ',"modules":' + encode(host.modules) + '}');
    safePath(host.image, 'host image');
    const helper = safePath(options.helperPath, 'helper path');
    if (!helper.endsWith('/qa_host_identity.dll')) throw Error('Unexpected QA helper basename');
    const root = safePath(options.tempRoot, 'temporary root') + '/XFX-Parameter-QA-' + request.nonce;
    const outputName = 'result-' + request.action + (hasSample ? '-' + request.sampleId : '') + '.json';
    const outputPath = root + '/' + outputName;
    const factory = fs.readFileSync(path.join(__dirname, 'parameter-qa.jsx'), 'utf8');
    const source = '(function(){\nvar helper=null,result=null,stage="factory";\nvar request=' + encode(request) + ';\n' +
        'var published=new File(' + encode(outputPath) + '),output=new File(' + encode(outputPath + '.pending') + ');\n' +
        'if(published.exists||output.exists)throw Error("QA output already exists or is pending; do not repeat request");\n' +
        'output.encoding="UTF-8";if(!output.open("w"))throw Error("Cannot create owned QA result");\ntry {\n' +
        'var parseIdentity=' + parseHostIdentity.toString() + ';\n' +
        'var failJson=' + failureJson.toString() + ';\n' +
        'try {\nvar api=' + factory + ';\n' +
        'stage="load";helper=new ExternalObject(' + encode('lib:' + helper) + ');\n' +
        'var provider={identity:function(){return parseIdentity(String(helper.identity()));}};\n' +
        'stage="execute";result=api.runJson(request, provider);\n' +
        '} catch(operationError) {result=failJson(operationError,stage);}\n' +
        'finally {if(helper) {try {helper.unload();} catch(unloadError) {result=failJson(unloadError,"unload");} helper=null;}}\n' +
        'if(output.write(result)!==true)throw Error("QA result write failed");\n' +
        '} finally {if(output.close()!==true)throw Error("QA result close failed");}\n' +
        'if(published.exists)throw Error("QA output appeared during request; do not overwrite");\n' +
        'if(output.rename(' + encode(outputName) + ')!==true)throw Error("QA result publish failed; pending attempt retained");\n})();\n';
    return {source, outputPath};
}
module.exports = {parseHostIdentity, buildQaRelay};
