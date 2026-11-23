// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "host_api.h"
static int32_t queue_post(const host_notice_t *notice);
#define notification_post queue_post
#include "../earthquakes.c"
#undef notification_post
#include "standin.h"
static bool full;
static int32_t queue_post(const host_notice_t *notice)
{ return full ? HOST_ERR_NO_SPACE : notification_post(notice); }
static void setup(void)
{
    standin_reset(); standin_set("notify","On"); app_on_load();
    full=false; state.seed=0;
}
static event_t event(int id, int mag)
{
    event_t e={.lat=36860000,.lon=26591300,.mag=mag,.depth=15};
    snprintf(e.id,sizeof e.id,"notice%d",id);
    snprintf(e.aliases,sizeof e.aliases,",notice%d,",id);
    strcpy(e.place,"Dodecanese, Greece"); e.time=(time_now()?time_now()-60:1795456740LL)*1000; e.updated=e.time+1000;
    return e;
}
static void post(int id, int mag, bool seed)
{ event_t e=event(id,mag); observe(&e,seed); try_notice(); }
static void suppression(void)
{
    setup(); post(1,50,true); assert(standin()->notices==0);
    post(2,50,false); assert(standin()->notices==1);
    post(2,70,false); assert(standin()->notices==1); /* revision */
    event_t alias=event(20,70);strcpy(alias.aliases,",notice2,notice20,");observe(&alias,false);try_notice();
    assert(standin()->notices==1);
    post(3,59,false);assert(standin()->notices==1);
    post(4,60,false);assert(standin()->notices==2);
    post(5,70,false);assert(standin()->notices==2);
    standin_pass((COOLDOWN+1)*1000LL);post(6,50,false);assert(standin()->notices==3);
    assert(strstr(standin()->last_notice.detail,"USGS"));
}
static void spatial_suppression(void)
{
    setup();event_t a=event(1,50);observe(&a,false);try_notice();
    event_t b=event(2,60);b.lon+=1000000;observe(&b,false);try_notice();
    assert(standin()->notices==2);
    event_t c=event(3,70);c.lon-=1000000;observe(&c,false);try_notice();
    event_t d=event(4,70);d.lon+=2000000;observe(&d,false);try_notice();
    assert(standin()->notices==2);
}
static void queue_and_storage(void)
{
    setup();full=true;post(1,50,false);assert(state.pending==1 && standin()->notices==0);
    load_cache();assert(state.pending==1);full=false;try_notice();assert(state.pending==0 && standin()->notices==1);
    try_notice();assert(standin()->notices==1);
    setup();full=true;post(1,50,false);post(2,70,false);
    assert(state.notice.mag==70 && state.pending==1);full=false;try_notice();assert(standin()->notices==1);
    setup();standin()->refuse_writes=HOST_ERR_FAILED;post(1,50,false);assert(!standin()->notices);
    standin()->refuse_writes=HOST_OK;try_notice();assert(standin()->notices==1);
    setup();full=true;post(1,50,false);standin_pass((FRESH+1)*1000LL);full=false;try_notice();assert(!standin()->notices && !state.pending);
    setup();full=true;post(1,50,false);standin_restart();app_on_load();app_on_intent(HOST_INTENT_STARTED);
    full=false;try_notice();assert(!standin()->notices && state.seed);
    setup();full=true;post(1,50,false);
    event_t revised=state.notice;revised.mag=35;revised.updated++;
    revise_notice(&revised);full=false;try_notice();assert(!standin()->notices && !state.pending);
    setup();state.pending=2;assert(save_cache());app_on_load();assert(!state.pending);
    setup();standin_clock(0);post(1,50,false);assert(!standin()->notices);
    setup();standin_set("notify","Off");settings();post(1,50,false);assert(!standin()->notices);
    setup();for(int i=0;i<LEDGER;i++) {
        cache.ledger[i].until=time_now()+COOLDOWN;
    }
    post(99,70,false);assert(!standin()->notices); /* saturation is silent */
}
static void late_publication(void)
{
    setup();
    event_t minor=event(1,35);minor.time=(time_now()-60)*1000;
    observe(&minor,false);try_notice();assert(!standin()->notices);
    event_t late=event(2,60);late.time=(time_now()-300)*1000;
    observe(&late,false);try_notice();assert(standin()->notices==1);
    /* Equal origin timestamps are distinct reports, even after a reload. */
    setup();minor=event(1,35);observe(&minor,false);save_cache();app_on_load();
    late=event(2,60);late.time=minor.time;
    observe(&late,false);try_notice();assert(standin()->notices==1);
    /* A revision remains silent after its report leaves both cached pages. */
    standin_pass((COOLDOWN+1)*1000LL);late.time=(time_now()-10)*1000;
    observe(&late,true);late.updated++;late.mag=70;
    observe(&late,false);try_notice();assert(standin()->notices==1);
}
static void rejected_windows(void)
{
    setup();full=true;post(1,50,false);
    standin_pass((FRESH+1)*1000LL);try_notice();
    full=false;post(2,50,false);assert(standin()->notices==1);
    setup();post(1,50,false);full=true;post(2,60,false);
    standin_pass((FRESH+1)*1000LL);try_notice();
    full=false;post(3,60,false);assert(standin()->notices==2);
}
int main(void) { suppression();spatial_suppression();queue_and_storage();late_publication();rejected_windows();puts("earthquakes: baseline, revisions, aliases, cooldown, escalation, queue, crash intent, clock and saturation");return 0; }
