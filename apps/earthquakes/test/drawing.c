// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../earthquakes.c"
#include "standin.h"
int main(void)
{
    standin_handlers=(standin_handlers_t){.on_load=app_on_load,.on_focus=app_on_focus,.on_draw=app_on_draw,.on_blur=app_on_blur,.on_back=app_on_back,.on_button=app_on_button};
    standin_reset();standin_load();standin_focus();
    for(int mode=0;mode<7;mode++) {
        placed=mode!=0;state.status=mode==1?-1:200;success[0]=mode==2?time_now()-86400:time_now();
        cache.count[0]=cache.count[1]=8;
        for(int scope=0;scope<2;scope++)for(int i=0;i<8;i++) {
            event_t *e=&cache.events[scope][i];zero(e,sizeof *e);
            snprintf(e->id,sizeof e->id,"draw%d",i);strcpy(e->place,"A very long provider name crossing the international date line");
            e->mag=mode==3?UNKNOWN:mode==6?100:56;e->depth=mode==3?UNKNOWN:15;
            e->lat=mode==4?89999999:34150000;e->lon=mode==5?-179999999:32620000;
            e->time=(time_now()-i*60)*1000;e->updated=e->time+1000;
        }
        for(int screen=0;screen<2;screen++) {
            scene.nav.screen=screen;scene.scope=screen;scene.nav.depth=0;selection();
            standin_draw();assert(!standin()->refused_draws && standin()->draw_calls);
            copy_bytes(&scene.detail,&cache.events[screen][0],sizeof(event_t));app_nav_open(&scene.nav,0);
            standin_draw();assert(!standin()->refused_draws && standin()->draw_calls);
        }
    }
    assert(!standin()->stray_draws);
    puts("earthquakes: map, lists, details, missing values, stale data, poles and dateline draw within the contract");
    return 0;
}
