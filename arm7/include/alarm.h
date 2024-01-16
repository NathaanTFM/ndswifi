#include "shared.h"

#ifndef ALARM_H
#define ALARM_H

typedef struct Alarm {
    void (*handler)(void*); // function to call
    void* arg; // any arg to the function
    u64 period; // if non-zero, repeat period
    u64 start; // start tick
    u64 fire; // next fire tick
    struct Alarm *prev, *next; // next alarm in the queue
} Alarm;

void createAlarm(Alarm *alarm);
void setAlarm(Alarm *alarm, u64 tick, void (*handler)(void *), void *arg);
void setPeriodicAlarm(Alarm *alarm, u64 start, u64 period, void (*handler)(void *), void *arg);
void cancelAlarm(Alarm *alarm);

#endif