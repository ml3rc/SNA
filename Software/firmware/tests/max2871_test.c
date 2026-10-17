#include <assert.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hal/max2871.h"
#include "sna.h"
static bool data_bit;
static unsigned bits, writes;
static uint32_t shift, registers[6];
void gpio_init(uint pin) { (void)pin; }
void gpio_set_dir(uint pin, bool out) { (void)pin; (void)out; }
bool gpio_get(uint pin) { (void)pin; return false; }
void gpio_put(uint pin, bool value) {
    if(pin==GPIO_RF_SRC_DATA) data_bit=value;
    if(pin==GPIO_RF_SRC_CLK && value) { shift=(shift<<1)|data_bit; bits++; }
    if(pin==GPIO_RF_SRC_LE && value) {
        assert(bits==32); assert((shift&7)==5-writes);
        registers[shift&7]=shift; writes++; bits=0; shift=0;
    }
}
void sleep_us(uint64_t us) { (void)us; }
int main(void) {
    hal_max2871_init();
    const uint64_t frequencies[]={23500000,46875000,93750000,187500000,375000000,
        750000000,1500000000,2354335000ULL,3000000000ULL,6000000000ULL};
    for(unsigned int i=0;i<count_of(frequencies);i++) {
        writes=0; assert(hal_max2871_set_freq(frequencies[i])); assert(writes==6);
        assert(((registers[2]>>9)&15)==15); // 5.11 mA with board RSET=5.1 kohm.
        assert(((registers[2]>>14)&1023)==1); // 100 MHz PFD.
        assert(((registers[1]>>3)&4095)==4095);
        assert(((registers[4]>>3)&3)==3 && (registers[4]&(1u<<5))); // +5 dBm RFOUTA enabled.
        assert(((registers[1]>>29)&3)==2); // CP20 linearity mode (from LibreVNA).
    }
    assert(!hal_max2871_set_freq(23499999)); assert(!hal_max2871_set_freq(6000000001ULL));
    puts("PASS: actual PLL register output, write order, frequency range and CP20 linearity mode");
    return 0;
}
