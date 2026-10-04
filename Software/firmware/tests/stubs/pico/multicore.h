#ifndef TEST_MULTICORE_H
#define TEST_MULTICORE_H
void multicore_launch_core1(void (*entry)(void));
unsigned int get_core_num(void);
#endif
