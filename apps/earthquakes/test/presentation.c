// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "host_api.h"
static int32_t spy_text(int32_t,int32_t,int32_t,const char *,int32_t,int32_t);
static int32_t map_draw(const host_map_t *);
static int32_t spy_icon(int32_t,int32_t,int32_t,int32_t);
__attribute__((unused)) static int32_t spy_line(int32_t,int32_t,int32_t,int32_t);
__attribute__((unused)) static int32_t spy_ring(const host_map_t *,int32_t,int32_t,int32_t);
#define ui_draw_text spy_text
#define gfx_draw_map map_draw
#define gfx_draw_icon spy_icon
#define gfx_draw_line spy_line
#define gfx_draw_map_radius spy_ring
#include "../earthquakes.c"
#undef ui_draw_text
#undef gfx_draw_map
#undef gfx_draw_icon
#undef gfx_draw_line
#undef gfx_draw_map_radius
#include "standin.h"
static bool offline, loading, unavailable, no_data, you;
static int home_icons, dashes, solid_rings, map_top, source_bottom;
static int32_t spy_text(int32_t x,int32_t y,int32_t style,const char *s,int32_t n,int32_t width)
{
    if(n==(int)strlen(word(STR_OFFLINE)) && !strncmp(s,word(STR_OFFLINE),(size_t)n))offline=true;
    if(n==(int)strlen(word(STR_WAIT)) && !strncmp(s,word(STR_WAIT),(size_t)n))loading=true;
    if(n==9 && !strncmp(s,"No answer",9))unavailable=true;
    if(n==7 && !strncmp(s,"No data",7))no_data=true;
    if(n==3 && !strncmp(s,"You",3))you=true;
    /* Jersey's caption line extends below y=216 at the previous y=204. */
    if(scene.nav.depth && n==4 && !strncmp(s,"USGS",4))source_bottom=y+24;
    return ui_draw_text(x,y,style,s,n,width);
}
static int32_t map_draw(const host_map_t *m) { map_top=m->y;return gfx_draw_map(m); }
static int32_t spy_icon(int32_t x,int32_t y,int32_t id,int32_t cell)
{ if(id==HOST_ICON_HOME)home_icons++;return gfx_draw_icon(x,y,id,cell); }
__attribute__((unused)) static int32_t spy_line(int32_t x,int32_t y,int32_t xx,int32_t yy)
{ dashes++;return gfx_draw_line(x,y,xx,yy); }
__attribute__((unused)) static int32_t spy_ring(const host_map_t *m,int32_t lat,int32_t lon,int32_t r)
{ solid_rings++;return gfx_draw_map_radius(m,lat,lon,r); }
static void setup(void)
{
    standin_reset();standin_handlers=(standin_handlers_t){.on_load=app_on_load,.on_focus=app_on_focus,.on_draw=app_on_draw,.on_picked=app_on_picked};
    standin_load();standin_focus();
}
static void status_scopes(void)
{
    setup();scene.scope=scene.nav.screen=1;maybe_browse();
    loading=false;standin_draw();assert(loading);
    app_on_picked(1,HOST_FAILED_NO_NETWORK);
    const char *empty="{\"type\":\"FeatureCollection\",\"metadata\":{\"count\":0},\"features\":[]}";
    standin_picked(0,-1,200,empty,(int)strlen(empty));
    offline=loading=false;standin_draw();assert(offline && !loading);
    /* HTTP, extraction and transport failures with connectivity are not Offline. */
    const int failures[]={503,429,200,HOST_FAILED_NO_ANSWER,HOST_FAILED_TIMEOUT};
    for(unsigned i=0;i<sizeof failures/sizeof failures[0];i++) {
        requesting=true;app_on_picked(1,failures[i]);
        offline=loading=unavailable=false;standin_draw();
        assert(unavailable && !offline && !loading);
    }
    /* The reverse order must preserve Nearby's failure as well. */
    app_on_picked(0,HOST_FAILED_NO_NETWORK);requesting=true;
    standin_picked(1,-1,200,empty,(int)strlen(empty));
    scene.scope=0;offline=loading=false;standin_draw();assert(offline && !loading);
    setup();scene.scope=scene.nav.screen=1;
    no_data=loading=false;standin_draw();assert(no_data && !loading);
}
static void map_markers(void)
{
    setup();event_t e={.lat=36860000,.lon=26591300,.mag=60};
    cache.events[0][0]=e;cache.count[0]=1;
    you=false;home_icons=dashes=solid_rings=0;standin_draw();
    assert(you && home_icons && dashes>=16 && !solid_rings && map_top==0);
    scene.nav.depth=1;scene.detail=e;
    you=false;home_icons=0;standin_draw();assert(you && home_icons);
}
static void source_clearance(void)
{
    setup();scene.nav.depth=1;
    const int latitudes[]={36860000,0};
    for(int i=0;i<2;i++) {
        scene.detail=(event_t){.lat=latitudes[i],.lon=26591300,.mag=45,.time=time_now()*1000};
        source_bottom=0;standin_draw();assert(source_bottom && source_bottom<=216);
    }
}
int main(void) { status_scopes();map_markers();source_clearance();puts("earthquakes: independent scope status, labeled home, dashed radius, map area and source clearance");return 0; }
