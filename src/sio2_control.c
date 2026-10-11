#include "common.h"
#include "libunk/irq.h"
#include "main.h"
#include "sio2.h"

extern const u8 _080DE17C[];
void sub_802E0D4(const void* config);
void sub_802D708(void);
void irq_setHandler(int id, IrqHandler handler);
/* agbcc emits an extra read for volatile structure members. The matching
 * byte access uses the original compiler's register-constrained form. */
typedef struct { u8 low; u8 high; } SioControlBytes;
int sub_802E4DC(void);

void sio2_start(void)
{
    sub_802E0D4(_080DE17C);
    sub_802D708();
}

void sio2_stop(void)
{
    SioControlBytes* control = (SioControlBytes*)REG_ADDR_RCNT;
    register int high asm("r0") = control->high;
    register int mask asm("r1") = 0xC0;
    control->high = high | mask;
    irq_updateMask(IRQ_UPDATE_MODE_AND, ~(IRQ_MASK_SIO | IRQ_MASK_TIMER3));
    REG_IF = IRQ_MASK_SIO | IRQ_MASK_TIMER3;
    irq_setHandler(IRQ_ID_SIO, main_dummyIrqHandler);
    irq_setHandler(IRQ_ID_TIMER3, main_dummyIrqHandler);
    sub_802D708();
}

bool32 sio2_isConnected(void)
{
    int state = sub_802E4DC();
    bool32 result = FALSE;
    if (state == 0)
        result = TRUE;
    return result;
}
