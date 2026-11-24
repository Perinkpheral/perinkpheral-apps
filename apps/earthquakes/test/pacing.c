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
static int32_t retry;
static const char *retry_text, *cache_control, *response_age;
static char retry_digits[32];
static int32_t header(const char *name,int32_t length,int32_t occurrence,char *out,int32_t size)
{
    (void)length;(void)occurrence;
    const char *value=!strcmp(name,"Retry-After")?retry_text:!strcmp(name,"Cache-Control")?cache_control:!strcmp(name,"Age")?response_age:NULL;
    if(!strcmp(name,"Retry-After") && !value && retry) { snprintf(retry_digits,sizeof retry_digits,"%d",retry);value=retry_digits; }
    if(!value)return HOST_ERR_NOT_FOUND;
    int n=(int)strlen(value);if(n>size)return HOST_ERR_NO_SPACE;memcpy(out,value,(size_t)n);return n;
}
static void existing_pacing(void)
{
    standin_reset();app_on_load();int64_t now=time_now();
    retry=1200;pace(0,false);assert(standin()->poll_interval[0]==1200 && state.not_before==now+1200);
    retry=0;retry_text="Mon, 23 Nov 2026 19:00:00 GMT";
    pace(0,false);assert(state.not_before==1795460400LL);
    standin_pass((1795460400LL-time_now())*1000);retry_text=NULL;cache_control="public, Max-Age=\"600\"";response_age="120";
    pace(0,true);assert(standin()->poll_interval[0]==600);
    standin_pass(10000);cache_control="max-age=60";pace(1,true);
    assert(standin()->poll_interval[0]==600);
    save_cache();app_on_load();standin_pass(10000);pace(1,true);
    assert(standin()->poll_interval[0]==600);
    standin_pass(580000);
    state.not_before=0;cache_control=response_age=NULL;state.not_before=0;pace(0,true);assert(standin()->poll_interval[0]==0);state.not_before=0;
    pace(0,false);assert(standin()->poll_interval[0]==300);
    standin_pass(300000);pace(0,false);assert(standin()->poll_interval[0]==900);
    standin_pass(900000);pace(0,false);assert(standin()->poll_interval[0]==3600);
    retry=1209600;assert(header_seconds("Retry-After", "")==1209600);
    retry=172800;pace(0,false);assert(state.pause_until==time_now()+172800);
    char text[16];assert(!app_setting("near",text,sizeof text));
    assert(standin_alarm_left(0)==86400);
    standin_pass(86400000);app_on_alarm(0);assert(!app_setting("near",text,sizeof text));
    standin_pass(86400000);app_on_alarm(0);assert(!state.pause_until);
    assert(app_setting_is("near","earthquake"));
    puts("earthquakes: numeric/date Retry-After, conservative max-age, backoff and provider pauses longer than a day");
}

static void unchanged_headers(void)
{
    standin_reset();standin_handlers=(standin_handlers_t){.on_picked=app_on_picked};retry=0;retry_text=response_age=NULL;cache_control="max-age=60";
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
    retry=0;retry_text=response_age=NULL;cache_control="max-age=600";app_on_load();
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
        standin_reset();retry=0;retry_text=response_age=NULL;cache_control="max-age=900";
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
int main(void) { unchanged_headers();unchanged_long_headers();world_deadline();existing_pacing();return 0; }
