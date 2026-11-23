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
        .on_draw=app_on_draw, .on_button=app_on_button, .on_back=app_on_back};
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
    assert(requesting && requested_page==1 && app_setting_is("offset","9"));
    /* A second navigation cannot retarget an in-flight response. */
    history_page(1);assert(requested_page==1 && app_setting_is("offset","9"));
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
int main(void)
{
    atomic_answers(); cache_recovery(); paging();
    puts("earthquakes: atomic extraction, nulls, failures, unchanged data, cache recovery and history");
    return 0;
}
