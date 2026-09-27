/*
 * launch.c - leaving the Shell's screen to run something.
 *
 * Transient mode (/TRAN with SHELLB loaded, as DOSSHELL.BAT runs it): the
 * command lines go into SHELLB's block and SHELLC exits, so the program gets
 * all the memory but SHELLB's few KB; SHELLB starts SHELLC again afterwards
 * and SHELLC comes back to the same screen (the state is kept in the block).
 * Without SHELLB (SHELLC started by hand, or no /TRAN) SHELLC stays in memory
 * and runs the lines itself.
 */
#include "shell.h"

extern void mouse_init(void);
extern void set_handlers(void);
extern void wait_enter_prompt(void);

static void leave_screen(int attr_row0, const char *row0)
{
    struct armregs r = {0};
    mouse_show(0);
    if (mouse_ok) {                     /* hand the programs a reset mouse */
        r.r0 = 0;
        _armdos_int33(&r);
    }
    s_raw_clear(0x07);
    if (row0) {
        volatile uint16_t *v = S_VRAM;
        for (int c = 0; c < 80; c++) v[c] = s_cell((uint16_t)((attr_row0 << 8) | ' '));
        for (int c = 0; row0[c]; c++) v[c] = s_cell((uint16_t)((attr_row0 << 8) | (uint8_t)row0[c]));
    }
    s_done();
    s_cursor(row0 ? 1 : 0, 0);
}

/* restore the handlers DOS gave us (a child must not inherit ours) */
static void parent_handlers(void)
{
    extern struct psp *_armdos_psp;
    const uint8_t *p = (const uint8_t *)_armdos_psp;
    for (int v = 0x23; v <= 0x24; v++) {
        struct armregs r = {0};
        uint32_t h;
        memcpy(&h, p + (v == 0x23 ? 0x0E : 0x12), 4);
        r.r0 = 0x2500 | v;
        r.r3 = h;
        _armdos_int21(&r);
    }
}

static void come_back(int from_fs)
{
    set_handlers();
    mouse_init();
    s_init();
    if (from_fs) wait_enter_prompt();
    if (st.cwd[0]) {
        dos_setdrive(st.cwd[0] - 'A');
        dos_chdir(st.cwd);
    }
}

void launch(const char *cmds, int from_fs)
{
    dos_getcwd(dos_curdrive(), st.cwd);
    st.press_enter = from_fs;
    leave_screen(0, 0);
    if (sblk) {
        int n = 0;
        while (cmds[n]) n += strlen(cmds + n) + 1;
        if (n + 1 > SB_CMDSIZE) n = SB_CMDSIZE - 1;
        memcpy(sblk->cmds, cmds, n);
        sblk->cmds[n] = 0;
        sblk->action = SB_RUN;
        state_save();
        exit(0);
    }
    parent_handlers();
    for (const char *c = cmds; *c; c += strlen(c) + 1)
        run_line(c);
    st.press_enter = 0;
    come_back(from_fs);
}

void command_prompt(void)
{
    dos_getcwd(dos_curdrive(), st.cwd);
    leave_screen(CLR(C_PROMPT), " When ready to return to the DOS Shell, type EXIT then press enter.");
    if (sblk) {
        sblk->action = SB_PROMPT;
        state_save();
        exit(0);
    }
    parent_handlers();
    run_comspec("");
    come_back(0);
}

void shell_exit(void)
{
    leave_screen(0, 0);
    if (sblk) sblk->action = SB_EXIT;
    exit(0);
}
