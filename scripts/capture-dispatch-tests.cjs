// Supplemental source-boundary guard, not an AE-host or native timing test.
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const source = fs.readFileSync(path.join(__dirname, '../native/plugin/XFXNativeConsole.cpp'), 'utf8');
const start = source.indexOf('if (LOWORD(wparam) == kCaptureId)');
const end = source.indexOf('if (LOWORD(wparam) == kUpdateId)', start);
assert(start >= 0 && end > start, 'native camera command must remain identifiable');
const command = source.slice(start, end);
assert(!/save_current_snapshot\s*\(|capture_comp_png\s*\(|current_capture_comp_name\s*\(/.test(command),
    'Native button must not query/render AE inside raw WM_COMMAND; queue a host Idle action');
assert(/queue_native_snapshot\s*\(/.test(command), 'native camera must enqueue its capture');
console.log('PASS native camera source boundary only queues; no direct AE screenshot call');
