#include "hal/max2871.h"

#include <math.h>

#include "hardware/gpio.h"
#include "pico/stdlib.h"

#include "sna.h"

// Register map/formulas below are transcribed directly from the MAX2871
// datasheet (Maxim/Analog Devices, "23.5MHz to 6000MHz Fractional/Integer-N
// Synthesizer/VCO", Register and Bit Descriptions section, Tables 3-8) --
// not guessed or reconstructed from memory. Board-specific constants (fREF,
// RSET, which RFOUT is wired up) are from the "RF Source" Altium schematic
// sheet (Altium/SNAPCBVersion-INT's exported PDF), read directly, 2026-10-01.

// U302 (LMK6CE10000CDLFR) feeds REF_IN, labeled "100MHz REF Clk." on the
// schematic. R divider = 1, no doubler/div-by-2 (DBR=0, RDIV2=0) -- so the
// phase-detector frequency equals the reference frequency exactly: 100MHz,
// under both the frac-N (125MHz) and int-N (140MHz) PFD limits.
#define MAX2871_FREF_HZ 100000000ULL
#define MAX2871_FPFD_HZ MAX2871_FREF_HZ

// RSET = R305 = 5.1kOhm -- confirmed by both the schematic's own design note
// ("Rset = 5.1kOhm as per datasheet") and R305's value next to the RSET pin.
// User-confirmed design: fPFD=100 MHz, ICP=5.11 mA, loop BW=450 kHz.
// ICP = 1.63/5100 x (1+15) = 5.1137 mA. Bandwidth is set by the
// external loop-filter components, not a programmable MAX2871 BW field.
#define MAX2871_CP_DEFAULT 15u

// Fractional modulus, fixed at the max for finest resolution: output step
// size is FPFD/MODULUS (=~24.4kHz at the VCO) divided by the output divider
// below, i.e. finer still at lower output frequencies -- plenty for a
// sweep/scalar-VNA's purposes.
#define MAX2871_MODULUS 4095u

// Cached from the last max2871_set_freq() call (0 before the first one),
// so max2871_read_die_id() below can temporarily overlay the MUX=1100
// ("Read SPI registers 06") bits onto registers 2/5 and restore exactly
// what was there before afterward, without disturbing N/FRAC/DIVA/etc
// (registers 0/1/3/4, untouched by the readback procedure) or needing to
// re-derive what those two registers should otherwise contain.
static uint32_t last_reg2 = 0;
static uint32_t last_reg3 = 0;
static uint32_t last_reg5 = 0;

void max2871_init(void) {
    gpio_init(GPIO_RF_SRC_CLK);
    gpio_set_dir(GPIO_RF_SRC_CLK, GPIO_OUT);
    gpio_init(GPIO_RF_SRC_DATA);
    gpio_set_dir(GPIO_RF_SRC_DATA, GPIO_OUT);
    gpio_init(GPIO_RF_SRC_LE);
    gpio_set_dir(GPIO_RF_SRC_LE, GPIO_OUT);
    gpio_init(GPIO_RF_SRC_CE);
    gpio_set_dir(GPIO_RF_SRC_CE, GPIO_OUT);
    gpio_init(GPIO_RF_SRC_RFOUT_EN);
    gpio_set_dir(GPIO_RF_SRC_RFOUT_EN, GPIO_OUT);

    // Readback / lock-detect, inputs
    gpio_init(GPIO_RF_SRC_MUX);
    gpio_set_dir(GPIO_RF_SRC_MUX, GPIO_IN);
    gpio_init(GPIO_RF_SRC_LD);
    gpio_set_dir(GPIO_RF_SRC_LD, GPIO_IN);
}

void max2871_enable(bool en) {
#ifdef SNA_BRINGUP_NO_RF_SOURCE
    // Bring-up safety gate (CMakeLists.txt option of the same name, ON by
    // default): keeps CE/RFOUT_EN low no matter what the caller asks for.
    // See feedback_sna_bringup_priority in project memory -- the MAX2871 is
    // the one RF block considered risky enough to bring up in isolation,
    // not fold into general testing. Register programming (max2871_set_freq
    // below) still works while this is set -- the datasheet's own power
    // mode section says the SPI block stays alive with CE low, only the
    // synthesizer itself is shut down -- so the bit-bang/register-math path
    // is exercisable without the chip ever actually putting out RF.
    (void) en;
    gpio_put(GPIO_RF_SRC_CE, 0);
    gpio_put(GPIO_RF_SRC_RFOUT_EN, 0);
#else
    gpio_put(GPIO_RF_SRC_CE, en);
    gpio_put(GPIO_RF_SRC_RFOUT_EN, en);
#endif
}

bool max2871_locked(void) {
    return gpio_get(GPIO_RF_SRC_LD);
}

// Bit-banged, MSB first, latched on LE -- the standard MAX2871 programming
// interface (Figure 1, SPI Timing Diagram).
static void max2871_write_reg(uint32_t reg32) {
    for (int i = 31; i >= 0; i--) {
        gpio_put(GPIO_RF_SRC_DATA, (reg32 >> i) & 1);
        gpio_put(GPIO_RF_SRC_CLK, 1);
        sleep_us(1);
        gpio_put(GPIO_RF_SRC_CLK, 0);
        sleep_us(1);
    }
    gpio_put(GPIO_RF_SRC_LE, 1);
    sleep_us(1);
    gpio_put(GPIO_RF_SRC_LE, 0);
}

bool max2871_set_freq(uint64_t freq_hz) {
    // RFOUT_ output divider (DIVA, register 4 bits 22:20, Table 4/7): keeps
    // the VCO itself always within its native 3000-6000MHz fundamental
    // range. Bands are contiguous per the datasheet's own table, so a
    // simple descending threshold ladder picks the right one.
    uint32_t diva, divisor;
    if (freq_hz > 6000000000ULL) {
        return false;
    } else if (freq_hz >= 3000000000ULL) { diva = 0; divisor = 1; }
    else if (freq_hz >= 1500000000ULL)   { diva = 1; divisor = 2; }
    else if (freq_hz >=  750000000ULL)   { diva = 2; divisor = 4; }
    else if (freq_hz >=  375000000ULL)   { diva = 3; divisor = 8; }
    else if (freq_hz >=  187500000ULL)   { diva = 4; divisor = 16; }
    else if (freq_hz >=   93750000ULL)   { diva = 5; divisor = 32; }
    else if (freq_hz >=   46875000ULL)   { diva = 6; divisor = 64; }
    else if (freq_hz >=   23500000ULL)   { diva = 7; divisor = 128; }
    else { return false; }

    // FB=1 ("Fundamental" mode, register 4 bit 23 -- DIVA outside the PLL
    // feedback loop): the N-counter always locks the raw 3-6GHz VCO
    // frequency directly, so one formula (N+F/M = fVCO/fPFD) covers every
    // DIVA setting -- the alternative, FB=0 ("Divided"), needs a second,
    // different formula once DIVA's divisor exceeds 16, which this avoids.
    double fvco = (double) freq_hz * (double) divisor;
    double n_plus_frac = fvco / (double) MAX2871_FPFD_HZ;

    uint32_t n = (uint32_t) n_plus_frac; // floor
    double frac = n_plus_frac - (double) n;
    uint32_t m = MAX2871_MODULUS;
    uint32_t f = (uint32_t) lround(frac * (double) m);
    if (f >= m) { // rounded up into the next integer
        f = 0;
        n += 1;
    }

    // Frac-N mode valid N range is 19-4091 ("Int, Frac, Mod, and R Counter
    // Relationship" section) -- always true here (fvco/fPFD is in [30,60]
    // for any freq_hz accepted above), checked defensively rather than
    // silently programming an out-of-spec value if that ever stops holding.
    if (n < 19 || n > 4091) return false;

    // Band-select clock divider (BS, register 4 bits 25:24 + 19:12):
    // BS = fPFD/50kHz, clamped to 1023 if larger ("VCO Autoselect (VAS)
    // State Machine" section) -- fPFD is fixed here, so this is a constant.
    uint32_t pfd_div_50k = (uint32_t) (MAX2871_FPFD_HZ / 50000ULL);
    uint32_t bs = pfd_div_50k > 1023 ? 1023 : pfd_div_50k;

    // Register 5 (addr 101): LD=01 (digital lock detect out on the LD pin,
    // matching max2871_locked() below). Everything else left at its
    // power-up default (0) -- MUX/ADC features aren't used.
    uint32_t reg5 = (0b01u << 22) | 0b101u;

    // Register 4 (addr 100): reserved bits 31:29 must be 011 per the
    // datasheet. RFA_EN=1 (RFOUTA is the output actually wired to the RF
    // chain -- RFOUTB's pins are marked no-connect on the schematic) at
    // max output power (APWR=11); the external RFOUT_EN pin -- gated by
    // max2871_enable()/the bring-up flag above -- is still the deciding
    // factor on whether RF actually comes out, same as CE gates the rest of
    // the chip. DIVA is double-buffered by register 0 (REG4DB=1, set in
    // register 2 below) so a divider change only takes effect glitch-free
    // together with the N/FRAC update in register 0.
    uint32_t reg4 = (0b011u << 29)
                   | (((bs >> 8) & 0x3u) << 24)  // BS[9:8]
                   | (1u << 23)                   // FB = Fundamental
                   | (diva << 20)                 // DIVA
                   | ((bs & 0xFFu) << 12)          // BS[7:0]
                   | (1u << 5)                     // RFA_EN = 1
                   | (0b11u << 3)                  // APWR = +5dBm (max)
                   | 0b100u;

    // Register 3 (addr 011): VAS (VCO autoselect) enabled -- simpler than
    // the manual VCO lookup-table path in Applications Information, at the
    // cost of extra lock time (~10/fBS) that doesn't matter for a swept
    // measurement (it would matter for the phase-sync use case, which this
    // board doesn't need -- only one MAX2871 on it).
    uint32_t reg3 = 0b011u;

    // Register 2 (addr 010): LDS=1 (fPFD > 32MHz); R=1/DBR=0/RDIV2=0 (fPFD
    // = fREF exactly, see above); REG4DB=1 (see register 4); CP = the
    // RSET-derived default above; LDF=0 (frac-N lock-detect mode, matches
    // INT=0 in register 0); PDP=1 (positive phase-detector polarity, the
    // datasheet's own default, matching a non-inverting loop filter).
    uint32_t reg2 = (1u << 31)                 // LDS
                   | (1u << 14)                 // R = 1 (field is bits 23:14, LSB at bit 14 -- NOT bit 23, which is that field's MSB; a prior version of this had R=512, not 1)
                   | (1u << 13)                 // REG4DB = 1
                   | (MAX2871_CP_DEFAULT << 9)
                   | (1u << 6)                  // PDP = positive
                   | 0b010u;

    // Register 1 (addr 001): P=1 (recommended phase value, no phase offset
    // needed -- single synth, not phase-synchronized to another MAX2871);
    // M = the fixed modulus above.
    uint32_t reg1 = (1u << 15)   // P = 1
                   | (m << 3)
                   | 0b001u;
#ifdef SNA_PLL_CP20_EXPERIMENT
    // Isolated comparison with LibreVNA's CP20 linearity mode. CP current,
    // reference/PFD, modulus and VCO selection remain unchanged.
    reg1 |= (2u << 29);
#endif

    // Register 0 (addr 000): INT=0 (frac-N mode), N/FRAC computed above.
    // Written LAST -- several fields in the other registers are double-
    // buffered and only take effect once register 0 is written, which is
    // also what triggers a new VCO autoselect/lock cycle. Programming
    // order (0x05,0x04,0x03,0x02,0x01,0x00) matches the datasheet exactly.
    uint32_t reg0 = (n << 15) | (f << 3) | 0b000u;

    last_reg2 = reg2;
    last_reg3 = reg3;
    last_reg5 = reg5;

    max2871_write_reg(reg5);
    max2871_write_reg(reg4);
    max2871_write_reg(reg3);
    max2871_write_reg(reg2);
    max2871_write_reg(reg1);
    max2871_write_reg(reg0);

    // TODO: the datasheet additionally recommends writing the full register
    // set TWICE with >=20ms between writes, specifically right after power-
    // up (CE low->high): "the first write ensures the device is enabled,
    // the second starts VCO selection". Not done automatically in here --
    // a caller doing a fresh rf_max2871_enable(true) should call this
    // function twice with a >=20ms gap itself, rather than every retune
    // during a sweep paying a cost that's only needed once per power-up.
    return true;
}

// Register 6 readback via the MUX pin used as MISO (datasheet "Read
// Sequence" + Figure 2, "Initiating Readback" -- verified against the
// actual timing diagram, not just the text description, since the two
// didn't fully agree on their own). Caller must already have MUX=1100
// ("Read SPI registers 06", register 5 bit 18 + register 2 bits 28:26)
// programmed via a prior max2871_write_reg() before calling this -- see
// max2871_read_die_id() below for a full example. Returns register 6's
// bits [31:3] (the 29 bits actually shifted out) left-aligned into a
// uint32_t at the same bit positions as the datasheet's own numbering,
// bits [2:0] left 0 (those are the fixed address bits, not part of the
// readback stream).
static uint32_t max2871_read_reg6(void) {
    // Step 1: LE low, shift out 32 bits where the last 3 are 1,1,0 (selects
    // register 6 -- the address bits, same position as any other register
    // write); the first 29 "don't care" bits are sent as 0.
    gpio_put(GPIO_RF_SRC_LE, 0);
    for (int i = 0; i < 32; i++) {
        bool bit = (i == 29) || (i == 30); // bits 29,30 = 1; bit 31 (i==31) = 0
        gpio_put(GPIO_RF_SRC_DATA, bit);
        gpio_put(GPIO_RF_SRC_CLK, 1);
        sleep_us(1);
        gpio_put(GPIO_RF_SRC_CLK, 0);
        sleep_us(1);
    }

    // Step 2: LE high -- latches address 110 (register 6), which (being
    // read-only) puts the device into readback mode instead of writing data.
    gpio_put(GPIO_RF_SRC_LE, 1);
    sleep_us(1);

    // Step 3: one more clock period (datasheet: "Finally, send 1 period of
    // the clock").
    gpio_put(GPIO_RF_SRC_CLK, 1);
    sleep_us(1);
    gpio_put(GPIO_RF_SRC_CLK, 0);
    sleep_us(1);

    // Step 4: 29 more clocks; MUX presents the next bit (MSB first, i.e.
    // register 6 bit 31 down to bit 3) shortly after each rising edge.
    uint32_t bits31to3 = 0;
    for (int i = 0; i < 29; i++) {
        gpio_put(GPIO_RF_SRC_CLK, 1);
        sleep_us(1); // >= tDOT (MUX output valid shortly after the edge)
        bits31to3 = (bits31to3 << 1) | (gpio_get(GPIO_RF_SRC_MUX) ? 1u : 0u);
        gpio_put(GPIO_RF_SRC_CLK, 0);
        sleep_us(1);
    }
    return bits31to3 << 3;
}

uint8_t max2871_read_die_id(void) {
    if (last_reg2 == 0) return 0; // no cached register set to safely restore yet
    // MUX pin is routed on this board specifically to support this (suited
    // for a MISO role), confirmed by the user directly, 2026-10-01 --
    // previously just wired as a plain input with no particular use.
    //
    // Temporarily overlay MUX=1100 onto the last-written registers 2/5 (see
    // the `last_reg2`/`last_reg5` comment above), read register 6, then
    // restore exactly what was there before. DIE[3:0] (register 6 bits
    // 31:28) is always valid, no ADC conversion/settling needed -- unlike
    // the ADC[6:0]/temperature fields also in register 6, which this
    // doesn't read (would need the CDIV/ADCM/ADCS dance from the
    // datasheet's "Tune ADC"/"Temperature Sensor" sections first).
    const uint32_t mux_read_reg6 = 0b1100;
    uint32_t reg2_read = (last_reg2 & ~(0x7u << 26)) | ((mux_read_reg6 & 0x7u) << 26);
    uint32_t reg5_read = (last_reg5 & ~(1u << 18)) | (((mux_read_reg6 >> 3) & 1u) << 18);

    max2871_write_reg(reg5_read);
    max2871_write_reg(reg2_read);

    uint32_t reg6 = max2871_read_reg6();

    max2871_write_reg(last_reg5); // restore normal operation
    max2871_write_reg(last_reg2);

    return (uint8_t) ((reg6 >> 28) & 0xFu); // 0111 = MAX2871, 0110 = MAX2870
}

bool max2871_read_temp_c(double *temp_c_out) {
    if (last_reg2 == 0) return false;
    // Datasheet "Temperature Sensor" procedure: set CDIV (register 3) to
    // clock the on-chip ADC at fPFD/100kHz, select the temperature sensor
    // and start a conversion (register 5's ADCM/ADCS), wait >=100us, then
    // read register 6 the same way as max2871_read_die_id() (needs the
    // same MUX=1100 overlay -- the datasheet's procedure doesn't mention
    // this explicitly for the temperature read, but register 6 can only
    // ever be read out via the MUX pin, same as everywhere else in this
    // file, so it must still apply). Restores registers 2/3/5 to whatever
    // max2871_set_freq() last programmed, same pattern as read_die_id().
    const uint32_t mux_read_reg6 = 0b1100;

    uint32_t cdiv = (uint32_t) (MAX2871_FPFD_HZ / 100000ULL); // = 1000 at 100MHz
    uint32_t reg3_temp = (last_reg3 & ~(0xFFFu << 3)) | ((cdiv & 0xFFFu) << 3);

    uint32_t reg2_temp = (last_reg2 & ~(0x7u << 26)) | ((mux_read_reg6 & 0x7u) << 26);

    uint32_t reg5_temp = (last_reg5 & ~(1u << 18) & ~(0x7u << 3) & ~(1u << 6))
                        | (((mux_read_reg6 >> 3) & 1u) << 18) // MUX[3]
                        | (0b001u << 3)                         // ADCM = temperature sensor
                        | (1u << 6);                            // ADCS = start conversion

    // Write order matters here, unlike the restore below: the datasheet's
    // own numbered procedure writes CDIV (register 3) BEFORE triggering
    // ADCS (register 5) -- "1) Set CDIV ... 2) Set ADCM ... 3) Set ADCS=1
    // to start the ADC conversion process. 4) Wait 100us." Confirmed on
    // real hardware, 2026-10-01: with the two writes the other way around
    // (as this used to be), src_temp_read returned a frozen, non-live value
    // across many reads spanning minutes of continuous operation --
    // consistent with the conversion starting against whatever CDIV
    // max2871_set_freq() last left in register 3 (0, "unused" per the
    // datasheet's own CDIV range note) rather than the fPFD/100kHz value
    // this function computes, since that value hadn't been written yet at
    // the moment ADCS's 0->1 edge landed.
    max2871_write_reg(reg3_temp);
    max2871_write_reg(reg5_temp);
    max2871_write_reg(reg2_temp);
    sleep_us(100);

    uint32_t reg6 = max2871_read_reg6();

    max2871_write_reg(last_reg5); // restore normal operation (also clears ADCM/ADCS)
    max2871_write_reg(last_reg3);
    max2871_write_reg(last_reg2);

    bool valid = (reg6 >> 15) & 1u; // ADCV, register 6 bit 15
    if (!valid) return false;

    uint32_t adc = (reg6 >> 16) & 0x7Fu; // ADC[6:0], register 6 bits 22:16
    // Datasheet formula; "most accurate when the VCO is enabled and RFOUTA
    // is enabled at full output power" -- still returned otherwise, just
    // less trustworthy (not flagged separately, same as every other
    // approximate figure in this driver).
    *temp_c_out = 95.0 - 1.14 * (double) adc;
    return true;
}
