#include "refkeen_config.h"
#include "be_cross.h"
#ifndef REFKEEN_PLATFORM_ARMDOS /* ARM-DOS: Keen Dreams uses neither EMS nor XMS emulation */
#include "be_cross_emm.h"
#include "be_cross_xmm.h"
#endif
#include "be_gamever.h"
#include "be_st.h"
