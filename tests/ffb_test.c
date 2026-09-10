#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ffb.h"
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
    puts("FFB byte preservation, ordering, repeats, filtering and overflow passed.");
    return 0;
}
