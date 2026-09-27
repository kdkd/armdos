/* xmstest.c - print the memory state (for the GEM tests: is extended
 * memory still free while GEM is loaded, how much conventional memory is
 * left for a DOS program?) */
#include <armdos.h>
#include <stdio.h>
#include <string.h>
#include <conio.h>

int main(int argc, char **argv)
{
	struct armregs r;
	void *e = armdos_xms_entry();
	unsigned ax;

	printf("XMSTEST entry %p\n", e);
	memset(&r, 0, sizeof r);
	r.r0 = 0x4800; r.r1 = 0xFFFF;
	_armdos_int21(&r);
	printf("XMSTEST largest DOS block %uK, this program's block ends at %p\n",
		(unsigned)((r.r1 & 0xFFFF) >> 6), (void *)_armdos_blockend);
	if (!e)
		return 1;
	memset(&r, 0, sizeof r);
	r.r0 = 0x0800;
	ax = _armdos_farcall(e, &r) & 0xFFFF;
	printf("XMSTEST free largest %uK total %uK bl %02X\n", ax, (unsigned)(r.r3 & 0xFFFF), (unsigned)(r.r1 & 0xFF));
	memset(&r, 0, sizeof r);
	r.r0 = 0x0900; r.r3 = 1024;
	ax = _armdos_farcall(e, &r) & 0xFFFF;
	printf("XMSTEST alloc 1024K ax %u dx %u bl %02X\n", ax, (unsigned)(r.r3 & 0xFFFF), (unsigned)(r.r1 & 0xFF));
	if (ax == 1) {
		unsigned h = r.r3 & 0xFFFF;
		memset(&r, 0, sizeof r);
		r.r0 = 0x0A00; r.r3 = h;
		_armdos_farcall(e, &r);
	}
	if (argc > 1)
		getch();
	return 0;
}
