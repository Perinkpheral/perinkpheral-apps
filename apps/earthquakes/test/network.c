// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../earthquakes.c"
#include "standin.h"

static void setup(void)
{
    standin_reset();
    standin_handlers = (standin_handlers_t){.on_load=app_on_load, .on_focus=app_on_focus,
        .on_blur=app_on_blur, .on_picked=app_on_picked,
        .on_draw=app_on_draw, .on_alarm=app_on_alarm, .on_button=app_on_button, .on_back=app_on_back};
    standin_load(); standin_focus();
}
static void feed(int poll, const char *name, int status)
{
    char bytes[20000], path[256];
    snprintf(path,sizeof path,"../../../tests/fixtures/earthquakes/%s.json",name);
    FILE *f=fopen(path,"rb"); assert(f);
    int n=(int)fread(bytes,1,sizeof bytes,f); fclose(f);
    if(poll==1 && !requesting) { requested_page=cache.browse_page;requesting=true; }
    standin_picked(poll,-1,status,bytes,n);
}
static void atomic_answers(void)
{
    setup(); feed(0,"near",200); feed(1,"world",200);
    assert(cache.count[0]==8 && cache.count[1]==8);
    uint32_t good=hash_bytes(cache.events[0],sizeof cache.events[0]);
    const char *bad[]={"missing","invalid","cut-identity"};
    for(int i=0;i<3;i++) {
        feed(0,bad[i],200);
        assert(hash_bytes(cache.events[0],sizeof cache.events[0])==good);
    }
    const int failures[]={-1,-2,-3,-4,-5,-6,-7,-8,503,404,429};
    for(unsigned i=0;i<sizeof failures/sizeof failures[0];i++) {
        feed(0,"empty",failures[i]);
        assert(hash_bytes(cache.events[0],sizeof cache.events[0])==good);
        feed(0,"near",200);
    }
    feed(0,"worst",200);assert(cache.count[0]==8 && strlen(cache.events[0][0].place)==63);
    feed(0,"near",200);feed(0,"duplicate",200);assert(cache.count[0]==8 && cache.events[0][0].mag==42);
    feed(0,"revision",200);assert(cache.count[0]==8 && !strcmp(cache.events[0][0].id,"revised-id"));
    setup(); feed(0,"null",200); assert(cache.events[0][0].mag==UNKNOWN && cache.events[0][0].depth==UNKNOWN);
    feed(0,"empty",200); assert(cache.count[0]==0);
    feed(0,"empty",204); assert(cache.count[0]==0);
    feed(0,"near",200); assert(cache.count[0]==8);
    feed(1,"world",200);
    int files=standin()->file_writes, changes=standin()->store_changes;
    for(int i=0;i<50;i++) { feed(0,"near",200); feed(1,"world",200); }
    assert(standin()->file_writes==files && standin()->store_changes==changes);
}
static void cache_recovery(void)
{
    setup(); feed(0,"near",200);
    int size; const uint8_t *bytes=standin_file("cache",&size); assert(bytes);
    unsigned char damaged[8192]; memcpy(damaged,bytes,(size_t)size); damaged[size/2]^=1;
    standin_file_put("cache",damaged,size);
    load_cache(); assert(cache.count[0]==0); /* a corrupt sole cache seeds silently */
    state.seed=1;
    standin()->refuse_writes=HOST_ERR_FAILED; feed(0,"new",200); assert(checksum(&cache)!=saved_checksum);
    standin()->refuse_writes=HOST_OK; feed(0,"new",200); assert(checksum(&cache)==saved_checksum);
    load_cache(); assert(cache.count[0]==8 && !strcmp(cache.events[0][0].id,"us6000ucz2"));
    /* A short interrupted write is rejected too. */
    standin_file_put("cache",damaged,size/2);load_cache();assert(!cache.count[0]);

}
static void paging(void)
{
    setup();feed(0,"near",200);
    scene.nav.screen=1;scene.scope=1;maybe_browse();
    char value[32];assert(app_setting_is("offset","1") && requesting);
    feed(1,"world",200);assert(!requesting && shown_count()==8);
    assert(!app_setting("offset",value,sizeof value));
    scene.list.selected=3;int files=standin()->file_writes;
    maybe_browse();assert(!requesting); /* reuse a fresh page without a write */
    assert(standin()->file_writes==files && scene.list.selected==3);
    scene.list.selected=7;history_page(1);
    assert(requesting && requested_page==1 && app_setting_is("offset","1"));
    char boundary[32], expected[32];
    utc_text(expected,cache.events[1][7].time);
    app_setting("end",boundary,sizeof boundary);assert(!strcmp(boundary,expected));
    /* Insertion/removal before this boundary no longer shifts page 2. */
    assert(cache.browse_end==cache.events[1][7].time);
    /* A second navigation cannot retarget an in-flight response. */
    history_page(1);assert(requested_page==1 && app_setting_is("offset","1"));
    standin_leave();standin_load();standin_focus();
    assert(requesting && requested_page==1 && scene.scope==1 && scene.page==1);
    feed(1,"history",200);assert(!requesting && scene.page==1 && shown_count()==8);
    assert(shown_events()[0].time>shown_events()[7].time);
    history_page(-1);maybe_browse();assert(requesting && requested_page==0);
    feed(1,"world",200);assert(!requesting && !scene.page && shown_count()==8);
    /* Returning unchanged data preserves the selected row. */
    scene.list.selected=4;feed(1,"world",200);assert(scene.list.selected==4);
    scene.page=1000;int polls=standin()->polls_run[1];history_page(1);
    assert(standin()->polls_run[1]==polls);
}
static void background_liveness(void)
{
    setup();standin_set("notify","On");settings();feed(0,"near",200);
    standin_leave();
    int files=standin()->file_writes;
    for(int minute=1;minute<=25;minute++) {
        standin_pass(60000);standin_load();feed(0,"near",200);standin_terminate();
        assert(!state.seed);
    }
    /* No minute-by-minute freshness writes, yet a fresh report can notify
       after the old hourly checkpoint is beyond the UI stale threshold. */
    assert(standin()->file_writes==files);
    standin_load();event_t e=cache.events[0][0];
    strcpy(e.id,"late-background");strcpy(e.aliases,",late-background,");
    e.mag=60;e.time=(time_now()-60)*1000;e.updated=e.time;
    char body[1024];
    int size=snprintf(body,sizeof body,
        "{\"type\":\"FeatureCollection\",\"metadata\":{\"count\":1},\"features\":[{\"id\":\"late-background\","
        "\"properties\":{\"ids\":\",late-background,\",\"place\":\"Pánormos, Greece\","
        "\"mag\":6,\"time\":%lld,\"updated\":%lld,\"type\":\"earthquake\"},"
        "\"geometry\":{\"coordinates\":[26.5913,36.86,15]}}]}",
        (long long)e.time,(long long)e.updated);
    standin_picked(0,-1,200,body,size);assert(standin()->notices==1);
    standin_pass(901000);standin_alarm(1);assert(state.seed);
    standin_load();feed(0,"new",200);assert(standin()->notices==1);
}
static void hourly_world_checkpoint(void)
{
    setup();scene.scope=scene.nav.screen=1;maybe_browse();feed(1,"world",200);
    assert(cache.browse_end==HOST_TIME_MAX*1000);int files=standin()->file_writes;
    for(int i=1;i<=3;i++) { standin_pass(900000);maybe_browse();feed(1,"world",200); }
    assert(standin()->file_writes==files);
    standin_pass(900000);feed(1,"world",200);
    assert(standin()->file_writes==files+1);
}
static void model_reply(const event_t *events, int count)
{
    char body[8192];int used=snprintf(body,sizeof body,"{\"type\":\"FeatureCollection\",\"metadata\":{\"status\":200,\"limit\":%d,\"offset\":1},\"features\":[",count); /* as the service answers: no metadata.count */
    for(int i=0;i<count;i++) {
        const event_t *e=&events[i];
        used+=snprintf(body+used,sizeof body-(size_t)used,
            "%s{\"id\":\"%s\",\"properties\":{\"ids\":\",%s,\",\"place\":\"Model\","
            "\"mag\":4.5,\"time\":%lld,\"updated\":%lld,\"type\":\"earthquake\"},"
            "\"geometry\":{\"coordinates\":[26.5913,36.86,15]}}",
            i?",":"",e->id,e->id,(long long)e->time,(long long)e->updated);
    }
    used+=snprintf(body+used,sizeof body-(size_t)used,"]}");
    standin_picked(1,-1,200,body,used);
}
/* Answer each actual query, including overlap scans, from a mutable feed. */
static void model_feed(const event_t *events, int count)
{
    for(int queries=0;requesting;queries++) {
        assert(queries<100);
        int start=0;
        while(start<count && events[start].time>cache.browse_end)start++;
        start+=cache.browse_offset-1;
        int n=start<count?count-start:0;if(n>BATCH)n=BATCH;
        model_reply(events+(start<count?start:count),n);
    }
}
static void changing_equal_time_history(void)
{
    for(int insertion=0;insertion<2;insertion++) {
        setup();feed(1,"world",200);scene.nav.screen=scene.scope=1;
        event_t reports[25];zero(reports,sizeof reports);
        for(int i=0;i<25;i++) {
            snprintf(reports[i].id,sizeof reports[i].id,"tie-%02d",i);
            reports[i].time=reports[i].updated=cache.events[1][0].time;
        }
        requesting=true;requested_page=0;model_reply(reports,8);
        history_page(1);model_feed(reports,24);
        assert(!strcmp(shown_events()[shown_count()-1].id,"tie-15"));
        /* Mutate an earlier page, then unload with the next request pending. */
        if(insertion) {
            memmove(reports+1,reports,24*sizeof *reports);
            strcpy(reports[0].id,"inserted");
        } else { memmove(reports,reports+1,23*sizeof *reports); }
        int count=insertion?25:23;
        /* Refused identity persistence must leave the cursor/page intact. */
        if(insertion) {
            standin()->refuse_writes=HOST_ERR_FAILED;
            history_page(1);assert(!requesting && scene.page==1);
            standin()->refuse_writes=HOST_OK;
        }
        history_page(1);assert(requesting);
        standin_leave();standin_load();standin_focus();
        model_feed(reports,count);
        bool seen[24]={0}, inserted=false;
        for(int page=0;page<4;page++) {
            for(int i=0;i<shown_count();i++) {
                const char *id=shown_events()[i].id;
                if(!strcmp(id,"inserted")) { assert(!inserted);inserted=true;continue; }
                int index=-1;assert(sscanf(id,"tie-%d",&index)==1);
                assert(index>=16 && index<24 && !seen[index]);seen[index]=true;
            }
            if(!shown_count())break;
            history_page(1);model_feed(reports,count);
        }
        for(int i=16;i<24;i++)assert(seen[i]);
        assert(inserted==!!insertion);
    }
}
static void changing_boundary(void)
{
    event_t older[BATCH], inclusive[BATCH];
    setup();feed(1,"history",200);memcpy(older,cache.events[1],sizeof older);
    setup();feed(1,"world",200);scene.nav.screen=scene.scope=1;
    inclusive[0]=cache.events[1][7];memcpy(inclusive+1,older,7*sizeof *older);
    history_page(1);model_reply(inclusive,BATCH);
    assert(shown_count()==7 && !strcmp(shown_events()[0].id,older[0].id));
    /* Inclusive overlap stays removed on a refresh after unloading. */
    standin_leave();standin_load();standin_focus();
    requesting=true;requested_page=1;model_reply(inclusive,BATCH);
    assert(shown_count()==7 && !strcmp(shown_events()[0].id,older[0].id));
    /* A deleted/filtered boundary is absent: E9 must still be first. */
    setup();feed(1,"world",200);scene.nav.screen=scene.scope=1;
    history_page(1);model_reply(older,BATCH);
    assert(shown_count()==8 && !strcmp(shown_events()[0].id,older[0].id));
    /* Eight equal-time reports do not trap the browser on an empty page. */
    setup();feed(1,"world",200);scene.nav.screen=scene.scope=1;
    for(int i=0;i<BATCH;i++)cache.events[1][i].time=cache.events[1][7].time;
    memcpy(inclusive,cache.events[1],sizeof inclusive);
    history_page(1);model_reply(inclusive,BATCH);
    assert(requesting && app_setting_is("offset","9"));
    model_reply(older,BATCH);assert(shown_count()==8 && !requesting);
}
static void equal_time_history(void)
{
    setup();feed(1,"world",200);scene.nav.screen=scene.scope=1;
    event_t reports[24];zero(reports,sizeof reports);
    int64_t at=cache.events[1][0].time;
    for(int i=0;i<24;i++) {
        snprintf(reports[i].id,sizeof reports[i].id,"tie-%02d",i);
        reports[i].time=at-(i<6?i*1000:i<16?6000:(i-9)*1000);
        reports[i].updated=at;
    }
    requesting=true;requested_page=0;model_reply(reports,8);
    for(int page=1,next=8;page<=3;page++) {
        history_page(1);assert(requesting);
        model_feed(reports,24);
        assert(!requesting && shown_count()>0);
        for(int i=0;i<shown_count();i++) {
            assert(next<24 && !strcmp(shown_events()[i].id,reports[next].id));next++;
        }
        /* Refresh uses the same boundary and does not repeat earlier pages. */
        browse(page);model_feed(reports,24);
        standin_leave();standin_load();standin_focus();
    }
    assert(shown_events()[shown_count()-1].time<reports[15].time);
}
static void recovered_coordinates(void)
{
    const char *keys[] = { "lat", "lon" };
    const int32_t errors[] = { HOST_ERR_FAILED, HOST_ERR_NO_SPACE };
    for (int32_t coordinate = 0; coordinate < 2; coordinate++) {
        for (int32_t error = 0; error < 2; error++) {
            setup();
            standin_set(keys[coordinate], "37.010000000000000000");
            standin()->refuse_writes = errors[error];
            app_on_settings();
            assert(query_unsaved & (1u << coordinate));
            standin_draw();
            assert(strstr(standin()->draw_text, "Storage error."));
            standin()->refuse_writes = HOST_OK;
            standin_set(keys[coordinate], "37.01");
            app_on_settings();
            feed(0, "near", 200);
            assert(!query_unsaved && !cache_unsaved);
            standin_draw();
            assert(!strstr(standin()->draw_text, "Storage error."));
        }
    }
    for (int32_t coordinate = 0; coordinate < 2; coordinate++) {
        setup();
        standin_set(keys[coordinate], "37.010000000000000000");
        standin()->refuse_writes = HOST_ERR_FAILED;
        app_on_settings();
        assert(query_unsaved & (1u << coordinate));
        assert(save_query("end", 3, "2026-11-01", 10) == HOST_ERR_FAILED);
        cache_unsaved = true;
        standin()->refuse_writes = HOST_OK;
        standin_set("place", "");
        settings();
        assert(!placed && !(query_unsaved & 3u));
        assert(query_unsaved & (1u << 3));
        assert(cache_unsaved);
        app_on_settings();
        assert(query_unsaved & (1u << 3));
        standin_draw(); assert(strstr(standin()->draw_text, "Storage error."));
        assert(save_query("end", 3, "2026-11-01", 10) == HOST_OK);
        cache_unsaved = false;
        standin_draw(); assert(!strstr(standin()->draw_text, "Storage error."));
    }
}

static void refused_state(void)
{
    static const int32_t REFUSALS[] = { HOST_ERR_NO_SPACE, HOST_ERR_FAILED };
    for (size_t index = 0; index < sizeof REFUSALS / sizeof REFUSALS[0]; index++) {
        setup(); feed(0, "near", 200);
        standin()->refuse_writes = REFUSALS[index];
        feed(0, "new", 200);
        assert(cache_unsaved);
        standin_draw();
        assert(strstr(standin()->draw_text, "Storage error."));
        standin()->refuse_writes = HOST_OK;
        assert(save_cache() && !cache_unsaved);
        load_cache(); assert(!strcmp(cache.events[0][0].id, "us6000ucz2"));
    }
    setup();
    standin()->refuse_writes = HOST_ERR_FAILED;
    assert(save_query("end", 3, "2026-11-01", 10) == HOST_ERR_FAILED && query_unsaved);
    standin_draw(); assert(strstr(standin()->draw_text, "Storage error."));
    standin()->refuse_writes = HOST_OK;
    assert(save_query("end", 3, "2026-11-01", 10) == HOST_OK && !query_unsaved);
    setup();
    uint64_t diagnostics = standin()->diagnostics;
    standin_refuse_session(HOST_ERR_NO_SPACE);
    app_on_blur();
    assert(standin_refused_sessions() == 1 && standin()->diagnostics != diagnostics);
    diagnostics = standin()->diagnostics;
    standin()->refuse_alarms = HOST_ERR_NO_SPACE;
    schedule_alarm(1, 60);
    assert(standin()->diagnostics != diagnostics);
    standin()->refuse_alarms = HOST_OK;
    schedule_alarm(1, 60);
    assert(standin_alarm_left(1) == 60);

    /* The service omits metadata.count from a query with a limit: no events, not no answer. */
    static const char quiet[] = "{\"type\":\"FeatureCollection\",\"metadata\":{\"status\":200,\"limit\":8,\"offset\":1},\"features\":[]}";
    setup(); standin_picked(0, -1, 200, quiet, (int32_t)sizeof quiet - 1);
    assert(state.status[0] == 200 && cache.count[0] == 0 && scope_status(0) == STR_NO_EVENTS);

    setup(); feed(0, "near", 200);
    uint32_t good = hash_bytes(cache.events[0], sizeof cache.events[0]);
    const char *bad[] = {
        "{\"type\":\"FeatureCollection\",\"metadata\":{\"count\":8},\"features\":[]}",
        "{\"type\":\"FeatureCollection\",\"metadata\":{\"count\":1},\"features\":[{}]}"
    };
    for (size_t index = 0; index < sizeof bad / sizeof bad[0]; index++) {
        standin_picked(0, -1, 200, bad[index], (int32_t)strlen(bad[index]));
        assert(state.status[0] == HOST_FAILED_NO_ANSWER);
        assert(hash_bytes(cache.events[0], sizeof cache.events[0]) == good);
        load_cache(); assert(hash_bytes(cache.events[0], sizeof cache.events[0]) == good);
    }
}

int main(void)
{
    recovered_coordinates(); refused_state(); changing_equal_time_history(); equal_time_history(); atomic_answers(); cache_recovery(); paging(); background_liveness(); hourly_world_checkpoint(); changing_boundary();
    puts("earthquakes: atomic extraction, nulls, failures, unchanged data, cache recovery and history");
    return 0;
}
