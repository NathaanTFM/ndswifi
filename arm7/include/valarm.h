#include <stdbool.h>
#include "shared.h"

#ifndef VALARM_H
#define VALARM_H

typedef struct VAlarm {
    void (*handler)(void*); // function to call
    void* arg; // any arg to the function
    u16 count;
    u16 delay;
    u32 frame; // frame to fire on
    bool periodic;
    struct VAlarm *prev, *next; // next valarm in the queue
} VAlarm;

void createVAlarm(VAlarm *valarm);
void setVAlarm(VAlarm *valarm, u16 count, u16 delay, void (*handler)(void *), void *arg);
void setPeriodicVAlarm(VAlarm *valarm, u16 count, u16 delay, void (*handler)(void *), void *arg);
void cancelVAlarm(VAlarm *valarm);

#endif