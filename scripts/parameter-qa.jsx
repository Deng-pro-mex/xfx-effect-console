/* Experimental owned-fixture QA factory. Evaluation alone has no side effects.
   Not installed/called by the production console. Source-stage actions only:
   create, snapshot, saveCopy. No label writer, ExternalObject, eval, close/open.
   A future native runner must independently verify process/module identity and
   create a private, non-reparse temp directory + ownership marker before use.
   The provider below is a trusted native-runner boundary, NOT caller JSON. */
(function () {
    function fail(message) { throw new Error(message); }
    function text(value, limit, label, multiline) {
        var controls = multiline ? /[\x00-\x08\x0b\x0c\x0e-\x1f\x7f]/ : /[\x00-\x1f\x7f]/;
        if (typeof value !== 'string' || value.length > limit || controls.test(value)) fail('Invalid ' + label);
        return value;
    }
    function finite(value) {
        if (typeof value !== 'number' || !isFinite(value)) fail('Nonfinite QA numeric value');
        return value;
    }
    function integer(value, minimum, maximum) {
        if (finite(value) !== Math.floor(value) || value < minimum || value > maximum) fail('QA integer out of range');
        return value;
    }
    function keysOnly(value, names, label) {
        if (!value || typeof value !== 'object' || value instanceof Array) fail('Invalid ' + label);
        for (var key in value) if (Object.prototype.hasOwnProperty.call(value, key) && ('|' + names + '|').indexOf('|' + key + '|') < 0) fail('Unexpected field in ' + label);
    }
    function normalized(path) { return String(path).replace(/\\/g, '/').toLowerCase(); }
    function identity(value) {
        keysOnly(value, 'pid|creationTime|image|version|modules', 'host identity');
        keysOnly(value.modules, 'FLT|TDB|U', 'host modules');
        integer(value.pid, 1, 4294967295);
        if (typeof value.creationTime !== 'string' || !/^[a-f0-9]{16}$/.test(value.creationTime)) fail('Invalid host creation time');
        text(value.image, 2048, 'host image'); text(value.version, 64, 'host version');
        if (!value.image || !value.version) fail('Invalid host identity');
        for (var i = 0, modules = ['FLT', 'TDB', 'U']; i < modules.length; i++)
            if (typeof value.modules[modules[i]] !== 'string' || !/^[a-f0-9]{64}$/.test(value.modules[modules[i]])) fail('Invalid host module hash');
        return {pid:value.pid, creationTime:value.creationTime, image:value.image, version:value.version,
            modules:{FLT:value.modules.FLT, TDB:value.modules.TDB, U:value.modules.U}};
    }
    function sameHost(left, right) {
        return left.pid === right.pid && left.creationTime === right.creationTime && normalized(left.image) === normalized(right.image) &&
            left.version === right.version && left.modules.FLT === right.modules.FLT && left.modules.TDB === right.modules.TDB && left.modules.U === right.modules.U;
    }
    function validate(request, provider) {
        keysOnly(request, 'schemaVersion|action|nonce|expires|expectedHost|sampleId', 'request');
        if (Object.prototype.hasOwnProperty.call(request, 'sampleId') && (request.action !== 'snapshot' || typeof request.sampleId !== 'string' || !/^[a-f0-9]{32}$/.test(request.sampleId))) fail('Invalid snapshot sample ID');
        if (request.schemaVersion !== 1 || typeof request.nonce !== 'string' || !/^[a-f0-9]{32}$/.test(request.nonce)) fail('Invalid QA request');
        var now = new Date().getTime();
        if (typeof request.expires !== 'number' || !isFinite(request.expires) || request.expires <= now || request.expires > now + 300000) fail('Expired or invalid QA request');
        if (request.action !== 'create' && request.action !== 'snapshot' && request.action !== 'saveCopy') fail('Unsupported QA action');
        if (!provider || typeof provider.identity !== 'function') fail('Missing trusted host provider');
        var expected = identity(request.expectedHost), actual = identity(provider.identity());
        if (!sameHost(expected, actual) || String(app.version) !== actual.version) fail('Actual host does not match request');
        if (!app.project) fail('No project in trusted host');
        var root = Folder.temp.fsName + '/XFX-Parameter-QA-' + request.nonce;
        var marker = new File(root + '/ownership.txt'), expectedMarker = 'XFX-PARAMETER-QA\t1\t' + request.nonce + '\n';
        if (!marker.exists || marker.length > 256) fail('Missing or oversized QA ownership marker');
        marker.encoding = 'UTF-8';
        if (!marker.open('r')) fail('Cannot read QA ownership marker');
        var content;
        try { content = marker.read(256); } finally { marker.close(); }
        if (content !== expectedMarker) fail('Invalid QA ownership marker');
        return {root:root, nonce:request.nonce, host:actual, fixture:root + '/Fixture.aep', copy:root + '/Reopened.aep', marker:'__XFX_PARAMETER_QA__' + request.nonce};
    }
    function owned(context) {
        var file = app.project.file;
        if (!file || (normalized(file.fsName) !== normalized(context.fixture) && normalized(file.fsName) !== normalized(context.copy))) fail('Not an owned QA project');
        var comp = null, matches = 0;
        for (var i = 1; i <= app.project.numItems; i++) {
            var item = app.project.item(i);
            if (item.name === context.marker) { comp = item; matches++; }
        }
        if (matches !== 1 || !comp || comp.numLayers !== 1 || !comp.layer || comp.width !== 320 || comp.height !== 180 || comp.frameRate !== 24 || comp.duration !== 2) fail('Missing or changed owned QA fixture');
        return comp;
    }
    function quote(value) {
        return '"' + String(value).replace(/\\/g, '\\\\').replace(/"/g, '\\"').replace(/[\x00-\x1f\u2028\u2029]/g, function (c) {
            return '\\u' + ('0000' + c.charCodeAt(0).toString(16)).slice(-4);
        }) + '"';
    }
    function create(context) {
        if (app.project.file || app.project.numItems !== 0) fail('QA creation requires an empty unsaved project');
        if (new File(context.fixture).exists || new File(context.copy).exists) fail('QA fixture already exists');
        var comp = app.project.items.addComp(context.marker, 320, 180, 1, 2, 24);
        var layer = comp.layers.addSolid([0.25, 0.5, 0.75], 'XFX Parameter QA', 320, 180, 1, 2);
        var effects = layer.property('ADBE Effect Parade');
        if (!effects || !effects.canAddProperty('ADBE Slider Control')) fail('Required QA Slider Control unavailable');
        var source = effects.addProperty('ADBE Slider Control'); source.name = 'XFX Source';
        var parameter = source.property(1);
        var originalName = String(parameter.name);
        parameter.setValueAtTime(0, 10); parameter.setValueAtTime(1, 30);
        // Adding effects may invalidate earlier Property handles: build the
        // references as strings now and reacquire everything in snapshot().
        var selectors = [quote(originalName), quote(String(parameter.matchName)), '1'];
        var names = ['XFX By Name', 'XFX By Match', 'XFX By Index'];
        for (var i = 0; i < names.length; i++) {
            var effect = effects.addProperty('ADBE Slider Control'); effect.name = names[i];
            var value = effect.property(1);
            value.expression = 'effect("XFX Source")(' + selectors[i] + ')';
            value.expressionEnabled = true;
        }
        app.project.save(new File(context.fixture));
        if (!app.project.file || normalized(app.project.file.fsName) !== normalized(context.fixture)) fail('QA save did not bind its owned filename');
    }
    function eases(values) {
        if (!values || values.length > 4) fail('Invalid QA key ease');
        var result = [];
        for (var i = 0; i < values.length; i++) result.push({speed:finite(values[i].speed), influence:finite(values[i].influence)});
        return result;
    }
    function row(parameter, layer, effect, effectIndex) {
        var keys = [], samples = [];
        integer(parameter.numKeys, 0, 32);
        for (var i = 1; i <= parameter.numKeys; i++) keys.push({time:finite(parameter.keyTime(i)), value:finite(parameter.keyValue(i)),
            inInterpolation:String(parameter.keyInInterpolationType(i)), outInterpolation:String(parameter.keyOutInterpolationType(i)),
            inEase:eases(parameter.keyInTemporalEase(i)), outEase:eases(parameter.keyOutTemporalEase(i))});
        for (var j = 0, times = [0, 0.5, 1]; j < times.length; j++) samples.push({time:times[j],
            beforeExpression:finite(parameter.valueAtTime(times[j], true)), value:finite(parameter.valueAtTime(times[j], false))});
        return {layerId:integer(layer.id, 1, 2147483647), layerIndex:1, effectIndex:effectIndex, paramIndex:1,
            effectPropertyCount:integer(effect.numProperties, 1, 16),
            effectMatchName:text(String(effect.matchName), 256, 'effect match name'),
            paramMatchName:text(String(parameter.matchName), 256, 'parameter match name'),
            name:text(String(parameter.name), 512, 'parameter name'), propertyValueType:String(parameter.propertyValueType),
            keys:keys, samples:samples, expression:{text:text(String(parameter.expression), 4096, 'expression', true),
                enabled:parameter.expressionEnabled === true, error:text(String(parameter.expressionError), 4096, 'expression error', true)}};
    }
    function snapshot(context) {
        var comp = owned(context), layer = comp.layer(1), effects = layer.property('ADBE Effect Parade');
        if (!effects || effects.numProperties !== 4 || layer.name !== 'XFX Parameter QA') fail('Changed owned QA effect inventory');
        var names = ['XFX Source', 'XFX By Name', 'XFX By Match', 'XFX By Index'], parameters = [];
        for (var i = 1; i <= 4; i++) {
            var effect = effects.property(i);
            if (!effect || effect.name !== names[i - 1] || effect.matchName !== 'ADBE Slider Control' || !effect.numProperties || effect.property(1).matchName !== 'ADBE Slider Control-0001') fail('Changed owned QA effect identity');
            parameters.push(row(effect.property(1), layer, effect, i));
        }
        if (typeof app.project.dirty !== 'boolean') fail('Project dirty status is unavailable');
        return {kind:'xfx-parameter-qa-snapshot', schemaVersion:1, nonce:context.nonce, host:context.host,
            project:{file:app.project.file.fsName, dirty:app.project.dirty},
            comp:{id:integer(comp.id, 1, 2147483647), name:comp.name, width:comp.width, height:comp.height, duration:comp.duration, frameRate:comp.frameRate},
            parameters:parameters};
    }
    function run(request, provider) {
        try {
            var context = validate(request, provider);
            if (request.action === 'create') create(context);
            if (request.action === 'saveCopy') {
                owned(context);
                if (new File(context.copy).exists) fail('QA copy already exists');
                app.project.save(new File(context.copy));
                if (!app.project.file || normalized(app.project.file.fsName) !== normalized(context.copy)) fail('QA save copy did not bind its owned filename');
            }
            var result = {ok:true, snapshot:snapshot(context), runtimeEnabled:false, hostVerified:false};
            if (request.action === 'snapshot' && request.sampleId) result.sampleId = request.sampleId;
            return result;
        } catch (error) {
            return {ok:false, error:String(error && error.message ? error.message : error), runtimeEnabled:false, hostVerified:false};
        }
    }
    // ExtendScript does not always provide JSON. Fixed objects/data only; never eval.
    function encode(value) {
        if (value === null) return 'null';
        if (typeof value === 'string') return quote(value);
        if (typeof value === 'number') return String(finite(value));
        if (typeof value === 'boolean') return value ? 'true' : 'false';
        var result = [], i, key;
        if (value instanceof Array) {
            for (i = 0; i < value.length; i++) result.push(encode(value[i]));
            return '[' + result.join(',') + ']';
        }
        if (!value || typeof value !== 'object') fail('Unsupported QA JSON value');
        for (key in value) if (Object.prototype.hasOwnProperty.call(value, key)) result.push(quote(key) + ':' + encode(value[key]));
        return '{' + result.join(',') + '}';
    }
    return {run:run, runJson:function (request, provider) { return encode(run(request, provider)); }};
})()
