// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "host_api.h"
static int32_t header(const char *,int32_t,int32_t,char *,int32_t);
#define answer_header header
#include "../earthquakes.c"
#undef answer_header
#include "standin.h"
static const char *cache_control, *response_age;
static int32_t header(const char *name,int32_t length,int32_t occurrence,char *out,int32_t size)
{
    (void)length;(void)occurrence;
    const char *value=!strcmp(name,"Cache-Control")?cache_control:!strcmp(name,"Age")?response_age:NULL;
    if(!value)return HOST_ERR_NOT_FOUND;
    int n=(int)strlen(value);if(n>size)return HOST_ERR_NO_SPACE;memcpy(out,value,(size_t)n);return n;
}
static void existing_pacing(void)
{
    standin_reset(); cache_control="public, Max-Age=\"600\""; response_age="120"; app_on_load();
    pace(0,true); assert(standin()->poll_interval[0]==600);
    standin_pass(10000);cache_control="max-age=60";pace(1,true);
    assert(standin()->poll_interval[0]==600);
    save_cache();app_on_load();standin_pass(10000);pace(1,true);
    assert(standin()->poll_interval[0]==600);
    standin_pass(580000);
    state.not_before=0;cache_control=response_age=NULL;pace(0,true);
    assert(standin()->poll_interval[0]==0);
    pace(0,false);assert(standin()->poll_interval[0]==300);
    standin_pass(300000);pace(0,false);assert(standin()->poll_interval[0]==900);
    standin_pass(900000);pace(0,false);assert(standin()->poll_interval[0]==3600);
    cache_control="max-age=172800";pace(0,true);assert(state.pause_until==time_now()+172800);
    char text[16];assert(!app_setting("near",text,sizeof text));
    assert(standin_alarm_left(0)==86400);
    standin_pass(86400000);app_on_alarm(0);assert(!app_setting("near",text,sizeof text));
    standin_pass(86400000);app_on_alarm(0);assert(!state.pause_until);
    assert(app_setting_is("near","earthquake"));
    puts("earthquakes: conservative max-age, independent backoff and provider pauses longer than a day");
}

static void firmware_cooldown(void)
{
    static const char *const VALUES[] = { "1200", "Mon, 23 Nov 2026 19:00:00 GMT", NULL };
    static const int32_t DELAYS[] = { 1200, 3600, 60 };
    for (int32_t i=0; i<3; i++) {
        standin_reset(); cache_control=response_age=NULL;
        standin_handlers=(standin_handlers_t){.on_picked=app_on_picked}; app_on_load();
        standin_pair_t retry={"Retry-After",VALUES[i]};
        if (VALUES[i]) { standin_answer_headers(&retry,1); }
        standin_picked(0,-1,429,"{}",2);
        assert(standin()->poll_cooldown_until_ms==time_uptime_ms()+(int64_t)DELAYS[i]*1000);
        assert(standin()->poll_interval[0]==300 && !state.pause_until);
        assert(state.not_before==time_now()+300); /* error backoff, independent of Retry-After */
        assert(poll_run(1)==HOST_OK && !standin_poll_take(1));
        app_on_load(); standin_clock(time_now()+86400);
        assert(!standin_poll_take(0) && !standin_poll_take(1));
        standin_pass((int64_t)DELAYS[i]*1000);
        assert(standin_poll_take(1));
    }
}

static void manual_worldwide(void)
{
    standin_reset(); cache_control=response_age=NULL; app_on_load();
    assert(!standin_subject.polls[0].manual && standin_subject.polls[1].manual);
    assert(save_query("end",3,"2100-01-01T00:00:00.000",23)==HOST_OK);
    assert(save_query("offset",6,"1",1)==HOST_OK && standin_poll_ready(1,-1));
    for (int32_t day=0;day<3;day++) {
        standin_pass(86400000); standin_settings(); standin_intent(HOST_INTENT_NETWORK_UP);
        assert(standin_poll_take(0) && !standin_poll_take(1));
    }
    scene.nav.screen=scene.scope=1;maybe_browse();
    assert(requesting && standin_poll_take(1));
    static const char empty[]="{\"type\":\"FeatureCollection\",\"features\":[]}";
    standin_handlers.on_picked=app_on_picked;
    standin_picked(1,-1,200,empty,sizeof empty-1);
    assert(!requesting && app_setting_is("offset","1") && standin_poll_ready(1,-1));
    app_on_load(); assert(!requesting && !standin_poll_take(1));
}

static void unchanged_headers(void)
{
    standin_reset();standin_handlers=(standin_handlers_t){.on_picked=app_on_picked};response_age=NULL;cache_control="max-age=60";
    app_on_load();
    const char *empty="{\"type\":\"FeatureCollection\",\"metadata\":{\"count\":0},\"features\":[]}";
    for(int i=0;i<2;i++)standin_picked(0,-1,200,empty,(int)strlen(empty));
    int files=standin()->file_writes, changes=standin()->store_changes;
    for(int minute=1;minute<60;minute++) {
        standin_pass(60000);app_on_load();
        standin_picked(0,-1,200,empty,(int)strlen(empty));
        assert(standin()->file_writes==files && standin()->store_changes==changes);
    }
    standin_pass(60000);standin_picked(0,-1,200,empty,(int)strlen(empty));
    assert(standin()->file_writes==files+1);
}
static void unchanged_long_headers(void)
{
    standin_reset();standin_handlers=(standin_handlers_t){.on_picked=app_on_picked};
    response_age=NULL;cache_control="max-age=600";app_on_load();
    const char *empty="{\"type\":\"FeatureCollection\",\"metadata\":{\"count\":0},\"features\":[]}";
    standin_picked(0,-1,200,empty,(int)strlen(empty));
    requesting=true;cache_control="max-age=900";
    standin_picked(1,-1,200,empty,(int)strlen(empty));
    assert(standin()->poll_interval[0]==600);
    int files=standin()->file_writes, changes=standin()->store_changes;
    for(int i=1;i<6;i++) {
        standin_pass(600000);app_on_load();cache_control="max-age=600";
        standin_picked(0,-1,200,empty,(int)strlen(empty));
        requesting=true;cache_control="max-age=900";
        standin_picked(1,-1,200,empty,(int)strlen(empty));
        assert(standin()->poll_interval[0]==600);
        assert(standin()->file_writes==files && standin()->store_changes==changes);
    }
}
static void world_deadline(void)
{
    const char *empty="{\"type\":\"FeatureCollection\",\"metadata\":{\"count\":0},\"features\":[]}";
    for(int background=0;background<2;background++) {
        standin_reset();response_age=NULL;cache_control="max-age=900";
        standin_handlers=(standin_handlers_t){.on_load=app_on_load,.on_focus=app_on_focus,
            .on_blur=app_on_blur,.on_terminate=app_on_terminate,.on_picked=app_on_picked};
        standin_load();standin_focus();scene.nav.screen=scene.scope=1;maybe_browse();
        standin_picked(1,-1,200,empty,(int)strlen(empty));
        standin_pass(900000);maybe_browse();assert(requesting);
        if(background) { standin_leave();standin_load(); }
        standin_picked(1,-1,200,empty,(int)strlen(empty));
        if(background)standin_terminate();else standin_leave();
        standin_pass(60000);standin_forget_session();standin_load();standin_focus();
        scene.nav.screen=scene.scope=1;
        int polls=standin()->polls_run[1];maybe_browse();
        assert(!requesting && standin()->polls_run[1]==polls);
        standin_pass(840000);maybe_browse();assert(requesting);
    }
}
static void learned_clock(void)
{
    standin_reset(); cache_control=response_age=NULL; standin_clock(0);
    standin_handlers=(standin_handlers_t){.on_load=app_on_load,.on_focus=app_on_focus,
        .on_button=app_on_button,.on_intent=app_on_intent};
    standin_load(); standin_focus();
    standin_press(BTN_DOWN); standin_long_press(BTN_OK);
    assert(scene.scope==1 && !requesting && !standin_poll_take(1));
    standin_clock(standin_subject.at); standin_intent(HOST_INTENT_TIME_CHANGED);
    assert(requesting && standin_poll_take(1));
    /* An offscreen clock correction cannot invent manual Worldwide work. */
    standin_reset(); standin_clock(0); standin_load();
    standin_clock(standin_subject.at); standin_intent(HOST_INTENT_TIME_CHANGED);
    assert(!requesting && !standin_poll_take(1));
}

static void refreshed_picture(void)
{
    standin_reset(); cache_control=response_age=NULL;
    standin_handlers=(standin_handlers_t){.on_picked=app_on_picked}; app_on_load();
    const char *empty="{\"type\":\"FeatureCollection\",\"features\":[]}";
    standin_picked(0,-1,200,empty,(int)strlen(empty));
    standin_pass(901000); assert(stale(0));
    int redraws=standin()->redraws, files=standin()->file_writes;
    standin_picked(0,-1,200,empty,(int)strlen(empty));
    assert(!stale(0) && standin()->redraws>redraws && standin()->file_writes==files);
}

static void refused_worldwide_query(void)
{
    standin_reset(); cache_control=response_age=NULL;
    standin_handlers=(standin_handlers_t){.on_load=app_on_load,.on_focus=app_on_focus,
        .on_picked=app_on_picked,.on_button=app_on_button};
    standin_load(); standin_focus();
    standin()->refuse_writes=HOST_ERR_FAILED;
    standin_press(BTN_DOWN); standin_long_press(BTN_OK);
    assert(scene.scope==1 && query_unsaved && !requesting && !standin_poll_take(1));
    const char *empty="{\"type\":\"FeatureCollection\",\"features\":[]}";
    standin_picked(0,-1,200,empty,(int)strlen(empty));
    assert(query_unsaved && !requesting);
    standin()->refuse_writes=HOST_OK;
    standin_picked(0,-1,200,empty,(int)strlen(empty));
    assert(!query_unsaved && requesting && standin_poll_take(1));
    standin_picked(1,-1,200,empty,(int)strlen(empty));
    assert(!requesting && !cache_unsaved && state.status[1]==200);
}

int main(void) { learned_clock();refreshed_picture();refused_worldwide_query();manual_worldwide();firmware_cooldown();unchanged_headers();unchanged_long_headers();world_deadline();existing_pacing();return 0; }
