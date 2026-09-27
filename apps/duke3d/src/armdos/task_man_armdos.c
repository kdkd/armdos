/*
 * task_man_armdos.c - the Apogee Sound System's timer task manager
 * (TASK_MAN.C's interface, task_man.h) on the ARM-PC's 8253 PIT.
 *
 * Copyright (C) 1994-1995 James R. Dose (the design and the interface);
 * this implementation (C) 2026 the ARM-DOS project. GPL-2 or later, like the
 * Apogee Sound System release (see audiolib/ and COPYING).
 *
 * As TASK_MAN.C did on a 486: PIT channel 0 is reprogrammed to the rate of
 * the fastest scheduled task, INT 08h is hooked, and every tick each task's
 * count is advanced by the tick's length in PIT counts; a task runs when its
 * count reaches its own period. The BIOS tick (INT 08h: the BDA tick count,
 * INT 1Ch, the DOS clock) is chained whenever 65536 PIT counts have elapsed,
 * so DOS keeps time at 18.2 Hz. Tasks run inside the interrupt (SVC mode,
 * IRQs off), as they did on DOS.
 *
 * Duke Nukem 3D schedules: the game clock (totalclock, 120 Hz, game.c via
 * the engine's inittimer()), the MIDI sequencer (MIDI.C: tempo x division /
 * 60 Hz, i.e. one interrupt per MIDI tick) and the music fade (40 Hz).
 */
#include <stdlib.h>
#include <string.h>
#include <armdos.h>

#include "audiolib/task_man.h"

#define PIT_CLOCK 1193182L
#define MAX_TASKS 16

volatile int TS_InInterrupt = 0;

static task        tasks[MAX_TASKS];
static int         tasks_used[MAX_TASKS];
static int         installed;
static armdos_vect_t old_int08;
static long        tick_counts = 65536;   /* PIT counts per interrupt */
static unsigned long bios_acc;
volatile unsigned long TS_Ticks;           /* interrupts taken */

static void pit_program(long counts)
{
    unsigned d = counts >= 65536 ? 0 : (unsigned)counts;
    armdos_outb(0x43, 0x34);               /* channel 0, lo/hi, mode 2 */
    armdos_outb(0x40, d & 0xFF);
    armdos_outb(0x40, (d >> 8) & 0xFF);
}

static void int08_handler(struct armregs *f)
{
    int i;

    TS_Ticks++;
    if (!TS_InInterrupt)
    {
        TS_InInterrupt = 1;
        for (i = 0; i < MAX_TASKS; i++)
        {
            task *t = &tasks[i];
            if (!tasks_used[i] || !t->active)
                continue;
            t->count += tick_counts;
            while (t->count >= t->rate)
            {
                t->count -= t->rate;
                t->TaskService(t);
                if (!tasks_used[i] || !t->active)
                    break;
            }
        }
        TS_InInterrupt = 0;
    }

    bios_acc += tick_counts;
    if (bios_acc >= 65536)
    {
        bios_acc -= 65536;
        if (old_int08)
        {
            old_int08(f);                  /* the BIOS sends the EOI */
            return;
        }
    }
    armdos_outb(0x20, 0x20);
}

/* Program the PIT for the fastest active task (TS_SetTimerToMaxTaskRate). */
static void set_timer_to_max_rate(void)
{
    long fastest = 65536;
    int i;

    for (i = 0; i < MAX_TASKS; i++)
        if (tasks_used[i] && tasks[i].rate < fastest)
            fastest = tasks[i].rate;
    if (fastest < 400)
        fastest = 400;                     /* ~3 kHz ceiling */
    if (fastest != tick_counts)
    {
        uint32_t cpsr;
        __asm__ volatile("mrs %0, cpsr" : "=r"(cpsr));
        armdos_disable();
        tick_counts = fastest;
        pit_program(fastest);
        if (!(cpsr & ARM_CPSR_I))
            armdos_enable();
    }
}

static void install(void)
{
    if (installed)
        return;
    old_int08 = armdos_getvect(0x08);
    armdos_disable();
    armdos_setvect(0x08, int08_handler);
    installed = 1;
    armdos_enable();
}

void TS_Shutdown(void)
{
    if (!installed)
        return;
    armdos_disable();
    memset(tasks_used, 0, sizeof(tasks_used));
    armdos_outb(0x43, 0x36);               /* mode 3, 65536: 18.2 Hz */
    armdos_outb(0x40, 0);
    armdos_outb(0x40, 0);
    tick_counts = 65536;
    armdos_setvect(0x08, old_int08);
    installed = 0;
    armdos_enable();
}

task *TS_ScheduleTask(void (*Function)(task *), int rate, int priority, void *data)
{
    uint32_t cpsr;
    task *t = NULL;
    int i;

    if (rate <= 0)
        rate = 1;
    __asm__ volatile("mrs %0, cpsr" : "=r"(cpsr));
    armdos_disable();
    for (i = 0; i < MAX_TASKS; i++)
        if (!tasks_used[i])
        {
            t = &tasks[i];
            memset(t, 0, sizeof(*t));
            t->TaskService = Function;
            t->rate = PIT_CLOCK / rate;
            t->count = 0;
            t->priority = priority;
            t->data = data;
            t->active = 0;                 /* until TS_Dispatch */
            tasks_used[i] = 1;
            break;
        }
    if (!(cpsr & ARM_CPSR_I))
        armdos_enable();
    install();
    return t;
}

int TS_Terminate(task *ptr)
{
    int i;
    for (i = 0; i < MAX_TASKS; i++)
        if (tasks_used[i] && &tasks[i] == ptr)
        {
            tasks_used[i] = 0;
            tasks[i].active = 0;
            set_timer_to_max_rate();
            return TASK_Ok;
        }
    return TASK_Warning;
}

void TS_Dispatch(void)
{
    int i;
    for (i = 0; i < MAX_TASKS; i++)
        if (tasks_used[i])
            tasks[i].active = 1;
    set_timer_to_max_rate();
}

void TS_SetTaskRate(task *Task, int rate)
{
    if (rate <= 0)
        rate = 1;
    Task->rate = PIT_CLOCK / rate;
    set_timer_to_max_rate();
}

void TS_UnlockMemory(void) { }
int  TS_LockMemory(void) { return TASK_Ok; }
