// SPDX-License-Identifier: MIT
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../earthquakes.c"
#include "standin.h"
static double radians(double degrees) { return degrees*3.14159265358979323846/180; }
static void geography(void)
{
    int32_t cases[][4]={{0,0,0,0},{0,179900000,0,-179900000},{90000000,0,90000000,180000000},
        {-90000000,-180000000,-90000000,180000000},{34680000,33040000,34150000,32620000},
        {90000000,0,-90000000,0},{0,0,0,180000000},{70000000,179000000,65000000,-175000000}};
    for(unsigned i=0;i<sizeof cases/sizeof cases[0];i++) {
        double a=radians(cases[i][0]/1e6), b=radians(cases[i][2]/1e6);
        double d=radians((cases[i][3]-cases[i][1])/1e6);
        double h=pow(sin((b-a)/2),2)+cos(a)*cos(b)*pow(sin(d/2),2);
        if(h<0) h=0; if(h>1) h=1;
        double expected=2*6371009*asin(sqrt(h));
        int32_t actual=distance_m(cases[i][0],cases[i][1],cases[i][2],cases[i][3]);
        assert(fabs(actual-expected)<(expected>20000000 ? 500 : 50));
    }
    assert(relevance(39)==0 && relevance(40)==100 && relevance(49)==100 && relevance(50)==300);
    assert(relevance(60)==1000 && relevance(70)==2000 && relevance(UNKNOWN)==0);
    standin_reset(); app_on_load(); placed=true; latitude=longitude=0; radius_km=2000; minimum=30;
    for(int mag=40;mag<=70;mag+=10) {
        event_t e={.mag=mag};
        e.lat=(int32_t)((relevance(mag)*1000-100)*180000000LL/20015114);
        assert(relevant(&e)); e.lat=(int32_t)((relevance(mag)*1000+100)*180000000LL/20015114);
        assert(!relevant(&e));
    }
}
static void identity_and_dates(void)
{
    event_t a={.id="us-a",.aliases=",us-a,ci-b,"}, b={.id="ci-b",.aliases=",ci-b,"};
    assert(same_event(&a,&b)); strcpy(b.id,"new");strcpy(b.aliases,",ci-b,new,"); assert(same_event(&a,&b));
    strcpy(b.aliases,",ci-bb,new,"); assert(!same_event(&a,&b));
    assert(!token(",prefix-us-a,", "us-a"));
    assert(retry_date("Mon, 23 Nov 2026 18:00:00 GMT")==1795456800);
    assert(retry_date("malformed")==0);
    assert(retry_date("Sun, 01 Mar 2026 00:00:00 GMT")==1772323200LL);
    assert(retry_date("Fri, 01 Jan 2027 00:00:00 GMT")==1798761600LL);
    assert(retry_date("Mon, 32 Nov 2026 18:00:00 GMT")==0);

}
static void coordinates(void)
{
    standin_reset();
    standin_set("lat","-000000000000000000059.9400000000000000");
    standin_set("lon","0000000000000000000180.0000000000000000");
    app_on_load();assert(placed && latitude==-59940000 && longitude==180000000);
    assert(app_setting_is("lat","-59.940000") && app_setting_is("lon","180.000000"));
}
int main(void) { geography(); identity_and_dates(); coordinates(); puts("earthquakes: sphere distances, relevance boundaries, aliases and UTC cursors"); return 0; }
