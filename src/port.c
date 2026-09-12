/* Message ports the Kickstart 1.3 way (no CreateMsgPort). */
#include <exec/memory.h>
#include <exec/ports.h>
#include <proto/exec.h>

#include "kll.h"

struct MsgPort *port_create(void)
{
	struct MsgPort *mp;
	BYTE sig;

	mp = AllocMem(sizeof(*mp), MEMF_PUBLIC | MEMF_CLEAR);
	if (!mp)
		return NULL;
	sig = AllocSignal(-1);
	if (sig == -1) {
		FreeMem(mp, sizeof(*mp));
		return NULL;
	}
	mp->mp_Node.ln_Type = NT_MSGPORT;
	mp->mp_Flags = PA_SIGNAL;
	mp->mp_SigBit = sig;
	mp->mp_SigTask = FindTask(NULL);
	mp->mp_MsgList.lh_Head = (struct Node *)&mp->mp_MsgList.lh_Tail;
	mp->mp_MsgList.lh_Tail = NULL;
	mp->mp_MsgList.lh_TailPred = (struct Node *)&mp->mp_MsgList.lh_Head;
	return mp;
}

void port_delete(struct MsgPort *mp)
{
	FreeSignal(mp->mp_SigBit);
	FreeMem(mp, sizeof(*mp));
}
