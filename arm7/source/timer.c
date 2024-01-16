#include "timer.h"
#include <nds.h>

static volatile u64 tickCount = 0;

static void timerIrq() {
    tickCount++;
}

void initTimer() {
    timerStart(0, ClockDivider_64, 0, &timerIrq);
}

u64 getTick() {
    int prev = enterCriticalSection();

    u16 low = TIMER_DATA(0);
    u64 high = tickCount & 0xFFFFFFFFFFFFULL;

    if ((REG_IF & IRQ_TIMER0) != 0 && (low & 0x8000) == 0) {
        high++;
    }

    leaveCriticalSection(prev);
    return (high << 16) | low;
}