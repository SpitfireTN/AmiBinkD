/* bsy.h – AmigaOS 3.x–safe */

#ifndef _BSY_H
#define _BSY_H

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char bsy_t;

#define F_BSY ((bsy_t)'b')
#define F_CSY ((bsy_t)'c')

void bsy_init(void);
int bsy_add(FTN_ADDR *fa, bsy_t bt, BINKD_CONFIG *config);
int bsy_test(FTN_ADDR *fa, bsy_t bt, BINKD_CONFIG *config);
void bsy_remove(FTN_ADDR *fa, bsy_t bt, BINKD_CONFIG *config);
void bsy_remove_all(BINKD_CONFIG *config);
void bsy_touch(BINKD_CONFIG *config);

#define BSY_TOUCH_DELAY 60

#ifdef __cplusplus
}
#endif

#endif /* _BSY_H */
