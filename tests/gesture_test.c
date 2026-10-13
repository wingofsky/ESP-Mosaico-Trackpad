#include "gesture.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static tp_report_t reports[4096];
static int n;
static tp_state_t s;
static void collect(const tp_report_t *r,void *ctx) { assert(n<4096);reports[n++]=*r; }
static void init(void) { memset(&s,0,sizeof(s));n=0; }
static void input(tp_input_t in) { tp_process(&s,&in,collect,NULL); }
static void step(unsigned ms,unsigned count,int x,int y,int x2,int y2,bool button)
{
    input((tp_input_t){.ms=ms,.count=count,.p={{x,y,1},{x2,y2,2}},.valid=true,.button=button});
}
static void lift(unsigned ms)
{
    step(ms,0,0,0,0,0,0);step(ms+TP_RELEASE_MS,0,0,0,0,0,0);
    assert(!s.buttons);
}
static int sum(char axis)
{
    int total=0;
    for(int i=0;i<n;i++) total+=axis=='x'?reports[i].dx:axis=='y'?reports[i].dy:
                              axis=='v'?reports[i].wheel:reports[i].pan;
    return total;
}
static void taps(void)
{
    init();step(0,1,100,100,0,0,0);lift(100);
    assert(n==2 && reports[0].buttons==1 && reports[1].buttons==0);
    step(180,1,100,100,0,0,0);lift(250);
    assert(n==4 && reports[2].buttons==1 && reports[3].buttons==0);
    // Debounce measures tap duration at the first up, not at confirmation.
    init();step(0,1,100,100,0,0,0);lift(260);assert(n==2);
    init();step(0,1,100,100,0,0,0);lift(261);assert(n==0);
    // A held touch must not turn into a click while its release is pending.
    init();step(0,1,100,100,0,0,0);lift(410);assert(n==0);
    init();step(0,1,100,100,0,0,0);step(30,2,100,100,200,100,0);
    step(100,1,100,100,0,0,0);lift(130);
    assert(n==2 && reports[0].buttons==2 && !reports[1].buttons);
}
static void motion_and_drag(void)
{
    init();step(0,1,100,100,0,0,0);step(16,1,120,90,0,0,0);lift(80);
    assert(n==1 && reports[0].dx==36 && reports[0].dy==-16 && !reports[0].buttons);
    init();step(0,1,100,100,0,0,0);step(430,1,100,100,0,0,0);
    step(450,1,110,105,0,0,0);lift(500);
    assert(n==3 && reports[0].buttons==1 && reports[1].buttons==1 && reports[1].dx==16 && !reports[2].buttons);
    init();step(0,0,0,0,0,0,1);step(40,1,100,100,0,0,1);
    step(60,1,130,120,0,0,1);step(100,0,0,0,0,0,1);step(130,0,0,0,0,0,0);
    assert(n==3 && reports[0].buttons==1 && reports[1].dx==56 && !reports[2].buttons);
}
static void jitter_and_slow_motion(void)
{
    init();step(0,1,200,200,0,0,0);
    for(int i=1;i<=30;i++) step(i*8,1,200+(i%5)-2,200+(i%3)-1,0,0,0);
    assert(n==0);lift(248);assert(n==2); // stable tap, no cursor wobble
    init();step(0,1,100,100,0,0,0);
    for(int i=1;i<=60;i++) step(i*8,1,100+i,100,0,0,0);
    assert(sum('x')==116 && sum('y')==0); // 1 px samples are retained
    int stopped=n;
    for(int i=61;i<=80;i++) step(i*8,1,160,100,0,0,0);
    assert(n==stopped);lift(660);assert(n==stopped);
    // Sampling the same path at 8 ms or 16 ms yields the same displacement.
    init();step(0,1,100,100,0,0,0);
    for(int i=2;i<=60;i+=2) step(i*8,1,100+i,100,0,0,0);
    assert(sum('x')==116);
}
static void scrolling(void)
{
    init();step(0,2,100,100,200,100,0);step(50,2,100,144,200,144,0);
    step(100,1,100,144,0,0,0);step(110,1,150,190,0,0,0);lift(150);
    assert(n==1 && reports[0].wheel==-3 && !reports[0].dx && !reports[0].buttons);
    // The same two contacts can arrive in reverse array order.
    init();step(0,2,100,100,200,100,0);
    input((tp_input_t){.ms=50,.count=2,.p={{244,100,2},{144,100,1}},.valid=true});
    assert(n==1 && reports[0].pan==3);
    // Shallow diagonal drift must not leak horizontal wheel events.
    init();step(0,2,100,100,200,100,0);
    for(int i=1;i<=60;i++) step(i*8,2,100+i/3,100+i*4,200+i/3,100+i*4,0);
    assert(sum('h')==0 && sum('v')<=-16);
    // A deliberate turn enables horizontal scrolling without lifting.
    for(int i=1;i<=30;i++) step(480+i*8,2,120+i*2,340,220+i*2,340,0);
    assert(sum('h')>=3);
    init();step(0,2,100,100,200,100,0);
    for(int i=1;i<=42;i++) step(i*8,2,100+i,100+i,200+i,100+i,0);
    assert(sum('h')==2 && sum('v')==-2); // deliberate diagonal
    init();step(0,2,100,100,200,100,0);
    for(int i=1;i<=60;i++) step(i*8,2,100,100+i,200,100+i,0);
    assert(sum('v')==-4); // fractional wheel remainder survives slow motion
    // Opposite finger motion cannot become a right tap despite a fixed centroid.
    init();step(0,2,100,100,200,100,0);step(80,2,80,100,220,100,0);lift(120);assert(n==0);
    // Movement of the last finger after a two-finger touch also cancels right tap.
    init();step(0,2,100,100,200,100,0);step(80,1,130,100,0,0,0);lift(120);assert(n==0);
}
static void release_debounce(void)
{
    init();step(0,1,100,100,0,0,0);step(430,1,100,100,0,0,0);
    step(440,0,0,0,0,0,0);assert(s.buttons==1);
    step(456,1,100,100,0,0,0);assert(n==1 && s.buttons==1);
    step(470,1,120,100,0,0,0);lift(500);
    assert(n==3 && reports[1].buttons==1 && !reports[2].buttons);
    // Two actual taps remain separate once the 24 ms window has elapsed.
    init();step(0,1,100,100,0,0,0);step(80,0,0,0,0,0,0);
    step(112,1,100,100,0,0,0);lift(180);assert(n==4);
    // A new distant touch does not bridge the old gesture or jump the pointer.
    init();step(0,1,100,100,0,0,0);step(80,0,0,0,0,0,0);
    step(88,1,300,300,0,0,0);lift(160);
    assert(n==4 && sum('x')==0 && sum('y')==0);
    // The debounce timer works over the uint32_t millisecond wrap.
    init();step(UINT_MAX-100,1,100,100,0,0,0);step(UINT_MAX-8,0,0,0,0,0,0);
    step(16,0,0,0,0,0,0);assert(n==2);
}
static void fault_release(void)
{
    init();step(0,1,100,100,0,0,0);step(430,1,100,100,0,0,0);
    input((tp_input_t){.ms=450,.valid=false});
    step(480,1,100,100,0,0,0);lift(550);
    assert(n==2 && reports[0].buttons==1 && reports[1].buttons==0);
    init();step(0,1,10,10,0,0,0);step(30,1,470,470,0,0,0);lift(100);assert(n==0);
    init();step(0,1,100,100,0,0,0);step(430,1,100,100,0,0,0);tp_reset(&s,collect,NULL);
    assert(n==2 && !reports[1].buttons);
    init();step(0,1,100,100,0,0,0);step(430,1,100,100,0,0,0);
    input((tp_input_t){.ms=450,.count=1,.p={{100,100,9}},.valid=true});
    assert(n==2 && !reports[1].buttons && s.suppress);
    init();step(0,1,100,100,0,0,0);step(430,1,100,100,0,0,0);
    step(450,1,4095,4095,0,0,0);assert(n==2 && !s.buttons && s.suppress);
}
int main(void)
{
    taps();motion_and_drag();jitter_and_slow_motion();scrolling();release_debounce();fault_release();
    puts("PASS: taps, drag, physical button, jitter, slow motion, no coast, sample-rate invariance, scroll axes/turns/remainders, contact order, pinch rejection, release debounce, double taps, timer wrap, fault/disconnect release");
}
