#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "g27.h"

// Native USB fixtures, excluding Windows' synthetic report-ID/filter changes.
static void neutral(uint8_t* p) {
    const uint8_t n[11] = {8,0,0,0,0x80,255,255,255,128,128,0x9c};
    memcpy(p,n,sizeof(n));
}
int main(void) {
    uint8_t p[11]; g29_report_t out = {0}; uint16_t brake = 0;
    neutral(p);
    assert(g27_revision(0x1238) && !g27_revision(0x1228));
    assert(!g27_decode(p,10,&out,&brake));
    assert(g27_decode(p,11,&out,&brake));
    uint8_t padded[16] = {0}; memcpy(padded,p,11);
    assert(g27_decode(padded,sizeof(padded),&out,&brake));
    assert(out.wheel == 32770 && out.clutch == 65535 && brake == 65535);
    p[3]=0; p[4]=0; p[5]=0; p[6]=128; p[7]=0;
    assert(g27_decode(p,11,&out,&brake));
    assert(out.wheel==0 && out.throttle==0 && brake==32896 && out.clutch==0);
    p[3]=252; p[4]=255; p[7]=128;
    g27_decode(p,11,&out,&brake);
    assert(out.wheel==65535 && out.clutch==32896);
    for(unsigned i=0;i<6;i++) {
        neutral(p); p[2]=(uint8_t)(1u<<i);
        g27_decode(p,11,&out,&brake); assert(out.gears==(1u<<i));
    }
    neutral(p); p[10]=0xdc;
    g27_decode(p,11,&out,&brake); assert(out.gears==0); // Neutral, stick pushed down.
    p[8]=0xb4; p[9]=0x12;
    g27_decode(p,11,&out,&brake); assert(out.gears==128);
    neutral(p); p[2]=3;
    g27_decode(p,11,&out,&brake); assert(out.gears==0);
    // Individual native wheel buttons and their independent G29 output controls.
    const uint8_t bytes[]={1,1,3,3,2,2};
    const uint8_t masks[]={8,4,1,2,0x40,0x80};
    const uint8_t extras[]={0,0,8,2,16,4};
    for(unsigned i=0;i<6;i++) {
        neutral(p); p[bytes[i]] |= masks[i]; g27_decode(p,11,&out,&brake);
        assert(out.L2==(i==0) && out.R2==(i==1));
        assert(out.extra_buttons==extras[i] && out.gears==0);
    }
    neutral(p); p[0]=0xf8; p[1]=0xf3;
    g27_decode(p,11,&out,&brake);
    assert(out.cross && out.square && out.circle && out.triangle);
    assert(out.L1 && out.R1 && out.select && out.start && out.L3 && out.R3);
    // Verify packed bytes used on the console, independently of field reads.
    const uint8_t* wire=(const uint8_t*)&out;
    assert(wire[4]==0xf8 && wire[5]==0xf3);
    for(unsigned i=0;i<16;i++) {
        neutral(p); p[0]=(uint8_t)i; g27_decode(p,11,&out,&brake);
        assert(out.dpad==(i<8?i:8));
    }
    neutral(p); g27_decode(p,11,&out,&brake);
    assert(!out.gears && !out.extra_buttons && !out.L2 && !out.select);
    puts("G27 native axes, buttons, gears, neutral and wire layout passed.");
    return 0;
}
