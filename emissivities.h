#ifndef EMISSIVITIES_H
#define EMISSIVITIES_H

#include "types.h"

__host__ __device__ void compton_emiss_cont(const PKT *pkt_ptr, double dist, double t_current);
__host__ __device__ void pp_emiss_cont(const PKT *pkt_ptr, double dist, double t_current);
void zero_estimators(void);
void normalise_compton_estimators(int nts);
void write_compton_estimators(int nts);
bool estim_switch(int nts);

#endif //EMISSIVITIES_H
