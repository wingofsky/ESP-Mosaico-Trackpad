#!/usr/bin/env python3
"""Replay deterministic input through the preserved baseline and current C engine.

No USB reports are sent. Synthetic cases provide an offline comparison,
not proof of live gesture quality.
"""
from pathlib import Path
import json
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
BASELINE = 'tests/fixtures/gesture-0.1.0'
HARNESS = r'''
#include "gesture.h"
#include <stdio.h>
static tp_state_t state;
static void report(const tp_report_t *r, void *ctx) {
    printf("%u %d %d %d %d\n",r->buttons,r->dx,r->dy,r->wheel,r->pan);
}
int main(void) {
    unsigned ms,count,id1,id2,valid,button; int x,y,x2,y2;
    while(scanf("%u %u %d %d %u %d %d %u %u %u",&ms,&count,&x,&y,&id1,&x2,&y2,&id2,&valid,&button)==10) {
        tp_input_t in={.ms=ms,.count=count,.p={{x,y,id1},{x2,y2,id2}},.valid=valid,.button=button};
        tp_process(&state,&in,report,NULL);
    }
}
'''
def point(ms, count, x=0, y=0, x2=0, y2=0):
    return [ms, count, x, y, 1, x2, y2, 2, 1, 0]
def end(rows, ms):
    return rows + [point(ms+i, 0) for i in range(0, 41, 8)]
def cases():
    result = {
        'stationary_jitter': end([point(0,1,200,200)] + [point(i*8,1,200+i%5-2,200+i%3-1) for i in range(1,31)],248),
        'slow_1px_motion': end([point(i*8,1,100+i,100) for i in range(61)],500),
        'vertical_scroll_with_drift': end([point(i*8,2,100+i//3,100+i*4,200+i//3,100+i*4) for i in range(61)],500),
        'pinch_without_translation': end([point(0,2,100,100,200,100),point(80,2,80,100,220,100)],120),
        'drag_with_16ms_dropout': end([point(0,1,100,100),point(430,1,100,100),point(440,0),point(456,1,100,100),point(470,1,120,100)],500),
        'slow_scroll_1px': end([point(i*8,2,100,100+i,200,100+i) for i in range(61)],500),
    }
    return result

def run(binary, rows):
    data='\n'.join(' '.join(map(str,row)) for row in rows)+'\n'
    output=subprocess.check_output([str(binary)],input=data,text=True)
    reports=[list(map(int,line.split())) for line in output.splitlines()]
    metric={'reports':len(reports),'pointer_travel_l1':0,'pointer_dx':0,'pointer_dy':0,
            'wheel':0,'pan':0,'left_presses':0,'right_presses':0,'drag_motion_reports':0}
    last=0
    for b,x,y,w,p in reports:
        metric['pointer_travel_l1']+=abs(x)+abs(y)
        metric['pointer_dx']+=x;metric['pointer_dy']+=y
        metric['wheel']+=w;metric['pan']+=p
        metric['left_presses']+=bool(b&1 and not last&1)
        metric['right_presses']+=bool(b&2 and not last&2)
        metric['drag_motion_reports']+=bool(b&1 and (x or y))
        last=b
    assert last==0, 'stuck button after replay'
    return metric

with tempfile.TemporaryDirectory(prefix='mosaico-compare-') as temp:
    tmp=Path(temp)
    bins={}
    for name in ('baseline','optimized'):
        d=tmp/name;d.mkdir()
        for filename in ('gesture.h','gesture.c'):
            content=(ROOT/BASELINE/filename).read_bytes() if name=='baseline' else (ROOT/'main'/filename).read_bytes()
            (d/filename).write_bytes(content)
        (d/'replay.c').write_text(HARNESS)
        bins[name]=d/'replay'
        subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-I',str(d),str(d/'gesture.c'),str(d/'replay.c'),'-o',str(bins[name])],check=True)
    comparison={name:{variant:run(binary,rows) for variant,binary in bins.items()} for name,rows in cases().items()}

assert comparison['stationary_jitter']['optimized']['pointer_travel_l1']==0
assert comparison['slow_1px_motion']['optimized']['pointer_dx']>=comparison['slow_1px_motion']['baseline']['pointer_dx']-4
assert comparison['vertical_scroll_with_drift']['optimized']['pan']==0
assert comparison['pinch_without_translation']['optimized']['right_presses']==0
assert comparison['drag_with_16ms_dropout']['optimized']['drag_motion_reports']>0
assert comparison['slow_scroll_1px']['optimized']['wheel']==comparison['slow_scroll_1px']['baseline']['wheel']
print(json.dumps({'baseline_fixture':BASELINE,'comparison':comparison,'kind':'offline replay; synthetic cases'},indent=2))
