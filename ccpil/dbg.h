#ifndef DBG_H
#define DBG_H 1

extern int dbg_init(void);
extern int dbg_mass_erase(void);
extern int dbg_writepage(uint8_t page, const uint8_t *buf);
extern int dbg_readpage(uint8_t page, const uint8_t *buf);
extern void dbg_reset(void);

#endif

