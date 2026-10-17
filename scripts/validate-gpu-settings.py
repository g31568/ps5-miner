#!/usr/bin/env python3
"""Exercise native GPU Settings on the combined title without changing pool values."""
from pathlib import Path
import hashlib, importlib.util, io, json, time, urllib.request, urllib.error

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('control', root / 'scripts/combined-control.py')
c = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c)
with c.connect() as f:
    base = c.sandbox(f)
    state = json.loads(c.read(f, base + 'xmrig-native-ui.json'))
    assert not state['editing'] and not state['keyboard'], 'Finish pending edits first'
    cpu_hash = hashlib.sha256(c.read(f, base + 'xmrig-native.json')).hexdigest()
url = state['web_url'].rstrip('/')

def request(path, doc=None):
    req = urllib.request.Request(url + path, data=json.dumps(doc).encode() if doc is not None else None,
        headers={'Content-Type': 'application/json', 'X-XMRig-Control': '1'} if doc is not None else {})
    with urllib.request.urlopen(req, timeout=15) as response:
        return json.load(response)

def ui():
    with c.connect() as f:
        return json.loads(c.read(f, c.sandbox(f) + 'xmrig-native-ui.json'))

def command(value):
    with c.connect() as f:
        f.storbinary('STOR ' + c.sandbox(f) + 'xmrig-native-test.once', io.BytesIO((value + '\n').encode()))
    time.sleep(2.2)

def wait_ui(predicate):
    # IME completion and the once-per-second UI snapshot are asynchronous.
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        state = ui()
        if predicate(state):
            return state
        time.sleep(.3)
    raise AssertionError('Expected UI state not reached')

def until(predicate, seconds=90):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        status = request('/status')
        if predicate(status):
            return status
        time.sleep(1)
    raise AssertionError('Expected console state not reached')

checks = []
def passed(name):
    checks.append(name)
    print('PASS', name, flush=True)

saved = request('/gpu/config')
request('/cpu/start', {})
request('/gpu/start', {})
until(lambda s: s['mining'] and s['gpu']['running'])
command('BUTTON 8')
command('BUTTON 20')
wait_ui(lambda s: s['settings_engine'] == 'gpu')
command('BUTTON 4000')
wait_ui(lambda s: s['keyboard'])
command('KEYBOARD_CANCEL')
wait_ui(lambda s: not s['keyboard'])
passed('GPU tab and system keyboard open/cancel')
for _ in range(7):
    command('BUTTON 40')
wait_ui(lambda s: s['row'] == 7)
command('BUTTON 40')
wait_ui(lambda s: s['row'] == 0)
passed('GPU fields wrap across both pages')
command('EDIT temporary.invalid')
command('BUTTON 80')
assert ui()['settings_engine'] == 'gpu'
command('BUTTON 2000')
assert request('/gpu/config') == saved
passed('Dirty GPU draft cannot be discarded by switching tabs; cancel preserves saved values')
command('BUTTON 8')
command('BUTTON 20')
command('BUTTON 8')
assert ui()['editing'] and request('/gpu/config') == saved
try:
    request('/gpu/config', saved)
    raise AssertionError('Web save bypassed controller draft')
except urllib.error.HTTPError as error:
    assert error.code == 409
passed('Running GPU and controller draft block conflicting saves')
command('BUTTON 800')
until(lambda s: not s['gpu']['running'] and not s['gpu']['stopping'])
# Edit to the original host so the real save path runs without changing pool identity.
command('EDIT ' + saved['url'])
command('BUTTON 8')
wait_ui(lambda s: not s['editing'])
assert request('/gpu/config') == saved
with c.connect() as f:
    assert hashlib.sha256(c.read(f, c.sandbox(f) + 'xmrig-native.json')).hexdigest() == cpu_hash
passed('Controller GPU save preserves advanced fields and CPU configuration')
command('BUTTON 800')
final = until(lambda s: s['mining'] and s['connected'] and s['hashrate'] > 0 and
    s['gpu']['connected'] and s['gpu']['graphs_per_second'] > 0 and not s['gpu']['faulted'])
passed('GPU restarts beside CPU after saving')
(root / 'logs/ui-grid/gpu-settings-checks.json').write_text(json.dumps({'checks': checks, 'final': final}, indent=2))
print(json.dumps(final), flush=True)
