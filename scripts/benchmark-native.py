#!/usr/bin/env python3
"""Measure native PS5 profiles through its live API and restore initial settings."""
import argparse,json,statistics,subprocess,time,urllib.request
from pathlib import Path
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--threads',default='8,12,16,8,12,16');p.add_argument('--tunes',default='');p.add_argument('--warmup',type=int,default=20);p.add_argument('--seconds',type=int,default=60);p.add_argument('--output',default='native-tuning-threads.json');args=p.parse_args()
threads_list=[int(n) for n in args.threads.split(',')];assert all(1<=n<=16 for n in threads_list)
tunes=[tuple(map(int,t.split(':'))) for t in args.tunes.split(',')] if args.tunes else []
assert all(len(t)==2 and 0<=t[0]<4 and t[1] in (0,1) for t in tunes)
profiles=[(n,t) for n in threads_list for t in (tunes or [None])]
def debug(action,value=''):
    subprocess.run(['python3',str(root/'scripts/debug-native-ui.py'),action,value],cwd=root,check=True,stdout=subprocess.DEVNULL)
def state():
    debug('report');return json.loads((root/'logs/native-options-state.json').read_text())
def api(path):
    url=(root/'logs/native-http-url.txt').read_text().replace('/summary','/'+path);start=time.monotonic()
    with urllib.request.urlopen(url,timeout=10) as r:data=json.load(r)
    return data,time.monotonic()-start
initial=state();assert initial['mining'] and not initial['editing'] and not initial['keyboard'],'Start mining and close Options first'
results=[];output=root/'logs'/args.output
try:
    for threads,tune in profiles:
        if tune:
            debug('command',f'TUNE {tune[0]} {tune[1]}');time.sleep(2)
            current=state();assert current['prefetch']==tune[0] and current['yield']==bool(tune[1])
        debug('command','START '+str(threads));time.sleep(2)
        before,_=api('summary');backend,_=api('backends');assert len(backend[0]['threads'])==threads
        print(f'Measuring {threads} threads, tune={tune}: warmup {args.warmup}s, sample {args.seconds}s',flush=True)
        time.sleep(args.warmup);rates=[];latencies=[];excluded=0;end=time.monotonic()+max(180,args.seconds*5)
        while len(rates)*5<args.seconds and time.monotonic()<end:
            sample,latency=api('summary');rate=sample['hashrate']['total'][0]
            # Exclude the ten-second rate window around pool reconnections.
            if rate is not None and rate>0 and sample['connection']['uptime']>=15:
                rates.append(rate);latencies.append(latency)
            else:excluded+=1
            time.sleep(5)
        assert len(rates)*5>=args.seconds,'Too few uninterrupted mining samples'
        after,_=api('summary');backend,_=api('backends');assert len(backend[0]['threads'])==threads
        row={'threads':threads,'tune':tune,'median_hps':statistics.median(rates),'mean_hps':statistics.mean(rates),'stdev_hps':statistics.pstdev(rates),'rate_60s':after['hashrate']['total'][1],'samples':rates,'excluded_samples':excluded,'pool_reconnects':after['connection']['failures']-before['connection']['failures'],'max_api_seconds':max(latencies),'accepted':after['connection']['accepted']-before['connection']['accepted'],'rejected':after['connection']['rejected']-before['connection']['rejected']}
        results.append(row);output.write_text(json.dumps({'initial_threads':initial['threads'],'initial_prefetch':initial.get('prefetch',1),'initial_yield':initial.get('yield',True),'warmup_seconds':args.warmup,'sample_seconds':args.seconds,'results':results},indent=2));print(json.dumps(row),flush=True)
finally:
    # Never leave an automatic Start queued in an unresponsive app.
    try:
        api('summary')
        if tunes:
            debug('command',f"TUNE {initial.get('prefetch',1)} {int(initial.get('yield',True))}");time.sleep(2)
        debug('command','START '+str(initial['threads']));time.sleep(2)
        restored=state();assert restored['threads']==initial['threads'] and restored['mining'],'Worker restoration not confirmed'
        api('summary')
    except Exception:
        debug('command','NOOP')
        print('Could not confirm restoration; cleared any queued Start.',flush=True)
        raise
print(output,flush=True)
