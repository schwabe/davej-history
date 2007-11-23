#ifndef _LMC_DEBUG_H_
#define _LMC_DEBUG_H_

#ifdef DEBUG
#ifdef LMC_PACKET_LOG
#define LMC_CONSOLE_LOG(x,y,z) lmcConsoleLog((x), (y), (z))
#else
#define LMC_CONSOLE_LOG(x,y,z)
#endif
#else
#define LMC_CONSOLE_LOG(x,y,z)
#endif

void lmcConsoleLog(char *type, unsigned char *ucData, int iLen);
inline void lmc_trace(struct net_device *dev, char *msg);

#endif
