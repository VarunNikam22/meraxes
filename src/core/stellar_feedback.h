#ifndef STELLAR_FEEDBACK_H
#define STELLAR_FEEDBACK_H

// These numbers are based on pre-knowledge on the yield and energy tables
#define NMETAL 40
#define MIN_Z 0
#define MAX_Z 39
#define NAGE 2000
#define NELEMENT 2
#define RECYCLING_FRACTION 0
#define TOTAL_METAL 1

static double age[NAGE];
static double yield_tables[NELEMENT][NMETAL*NAGE];
static double yield_tables_working[N_HISTORY_SNAPS][NMETAL][NELEMENT];
static double energy_tables[NMETAL*NAGE];
static double energy_tables_working[N_HISTORY_SNAPS][NMETAL];

#ifdef __cplusplus
extern "C" {
#endif

void read_stellar_feedback_tables(void);
void compute_stellar_feedback_tables(int snapshot);
double get_recycling_fraction(int i_burst, double metals);
double get_metal_yield(int i_burst, double metals);
double get_SN_energy(int i_burst, double metals);
double get_total_SN_energy(void);

#ifdef __cplusplus
}
#endif

#endif
