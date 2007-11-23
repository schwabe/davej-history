/* 
   * File...........: linux/include/asm-s390x/idals.h
   * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
   * Bugreports.to..: <Linux390@de.ibm.com>
   * (C) IBM Corporation, IBM Deutschland Entwicklung GmbH, 2000a
   
   * History of changes
   * 07/24/00 new file
 */

#define IDAL_NUMBER_CACHES 7

typedef unsigned long idaw_t;

#ifdef CONFIG_ARCH_S390
extern inline int 
normalize_cpa(ccw1_t * ccw, unsigned long address)
{
	return address;
}
#endif

int idal_alloc ( int nridaws );
void idal_release ( idaw_t *idal );

