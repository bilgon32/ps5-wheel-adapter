#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ffb.h"
#include "profile_store.h"
int main(void) {
    ffb_queue_t q={0};
    uint8_t spring[]={5,0x21,0x0b,0x7f,0x80,0x23,0xff,0xa5};
    uint8_t damper[]={0x41,0x0c,0x12,0x34,0x56,0x78,0x9a};
    assert(ffb_enqueue_report(&q,0,spring,sizeof(spring)));
    assert(ffb_enqueue_report(&q,5,damper,sizeof(damper)));
    assert(ffb_enqueue_report(&q,5,damper,sizeof(damper))); // Repeats are significant.
    assert(!memcmp(ffb_front(&q),spring+1,7)); ffb_pop(&q);
    assert(!memcmp(ffb_front(&q),damper,7)); ffb_pop(&q);
    assert(!memcmp(ffb_front(&q),damper,7)); ffb_pop(&q);
    assert(!ffb_front(&q) && q.sent==3);
    uint8_t legacy[]={0x7f,0x51,0x08,0x40,0x80,0,0,0};
    assert(ffb_enqueue_report(&q,0,legacy,sizeof(legacy)));
    assert(!memcmp(ffb_front(&q),legacy+1,7)); ffb_pop(&q);
    uint8_t mode[]={0xf8,0x09,4,1,0,0,0};
    const uint8_t blocked[]={1,9,10,16,17};
    for(unsigned i=0;i<sizeof(blocked);i++) {
        mode[1]=blocked[i]; assert(!ffb_enqueue_report(&q,5,mode,7));
    }
    mode[1]=0x12; assert(ffb_enqueue_report(&q,5,mode,7)); ffb_pop(&q);
    mode[1]=0x81; assert(ffb_enqueue_report(&q,5,mode,7)); ffb_pop(&q);
    assert(!ffb_enqueue_report(&q,5,damper,6));
    assert(!ffb_enqueue_report(&q,3,damper,7));
    for(unsigned cycle=0;cycle<3;cycle++) {
        for(unsigned i=0;i<FFB_QUEUE_CAPACITY;i++) {
            damper[2]=(uint8_t)i; assert(ffb_enqueue_report(&q,5,damper,7));
        }
        assert(!ffb_enqueue_report(&q,5,damper,7));
        for(unsigned i=0;i<FFB_QUEUE_CAPACITY;i++) {
            assert(ffb_front(&q)[2]==i); ffb_pop(&q);
        }
    }
    assert(q.overflows==3 && q.high_water==FFB_QUEUE_CAPACITY);
    ffb_clear(&q); assert(!ffb_front(&q));

    // Nominal constant-force packets change only selected slot-level bytes.
    uint8_t constant[]={0x51,0x00,129,64,200,128,0xa5};
    ffb_set_profile(&q,FFB_PROFILE_MINIMUM_12);
    ffb_enable_profiles(&q,true);
    assert(ffb_enqueue_report(&q,5,constant,7));
    const uint8_t* shaped=ffb_front(&q);
    assert(shaped[2]==143); // Slot 1: smallest positive command becomes 12%.
    assert(shaped[3]==64);  // Slot 2 is not selected.
    assert(shaped[4]>200);  // Slot 3 is selected and remains in the same direction.
    assert(shaped[5]==128 && shaped[6]==0xa5);
    assert(q.transformed==1); ffb_pop(&q);
    // Linux and the proven bench command use stationary variable force (0x08).
    const uint8_t stationary_variable[]={0x11,0x08,129,128,0,0,0};
    assert(ffb_enqueue_report(&q,5,stationary_variable,7));
    shaped=ffb_front(&q);
    assert(shaped[2]==143 && shaped[3]==128); ffb_pop(&q);
    const uint8_t actual_variable[]={0x11,0x08,129,128,0x01,0,0};
    assert(ffb_enqueue_report(&q,5,actual_variable,7));
    assert(!memcmp(ffb_front(&q),actual_variable,7)); ffb_pop(&q);
    const uint8_t untouched_spring[]={0x51,0x0b,129,64,200,128,0xa5};
    assert(ffb_enqueue_report(&q,5,untouched_spring,7));
    assert(!memcmp(ffb_front(&q),untouched_spring,7)); ffb_pop(&q);

    // Both protocol center values, directions, endpoints and monotonicity.
    const ffb_profile_t profiles[]={FFB_PROFILE_MINIMUM_12,FFB_PROFILE_MINIMUM_18,FFB_PROFILE_PROGRESSIVE,FFB_PROFILE_G27_MEASURED};
    for(unsigned p=0;p<sizeof(profiles)/sizeof(profiles[0]);p++) {
        ffb_set_profile(&q,profiles[p]);
        uint8_t previous_positive=128, previous_negative=127;
        for(unsigned magnitude=0;magnitude<=127;magnitude++) {
            uint8_t positive=(uint8_t)(128+magnitude);
            uint8_t negative=(uint8_t)(127-magnitude);
            uint8_t pair[]={0x31,0,positive,negative,127,0,0};
            assert(ffb_enqueue_report(&q,5,pair,7));
            shaped=ffb_front(&q);
            assert(shaped[2]>=previous_positive && shaped[3]<=previous_negative && shaped[4]==127);
            previous_positive=shaped[2]; previous_negative=shaped[3];
            if(!magnitude) assert(shaped[2]==128 && shaped[3]==127);
            if(magnitude==127) assert(shaped[2]==255 && shaped[3]==0);
            ffb_pop(&q);
        }
    }
    assert(ffb_next_profile(FFB_PROFILE_PROGRESSIVE)==FFB_PROFILE_G27_MEASURED);
    assert(ffb_next_profile(FFB_PROFILE_G27_MEASURED)==FFB_PROFILE_LINEAR);
    assert(!strcmp(ffb_profile_name(FFB_PROFILE_MINIMUM_18),"Minimum force 18%"));
    profile_store_t store;
    assert(profile_store_init(&store,FFB_PROFILE_MINIMUM_12)==FFB_PROFILE_MINIMUM_12);
    profile_store_schedule(&store,FFB_PROFILE_PROGRESSIVE,100);
    profile_store_task(&store,2099); assert(store.pending);
    profile_store_task(&store,2100); assert(!store.pending && store.profile==FFB_PROFILE_PROGRESSIVE);
    puts("FFB byte preservation, ordering, repeats, filtering and overflow passed.");
    return 0;
}
