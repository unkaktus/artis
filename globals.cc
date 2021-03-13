#include "types.h"
#include "sn3d.h"
#include "globals.h"

namespace globals
{

  #if CUDA_ENABLED
  __managed__ curandState curandstates[MCUDATHREADS];
  #endif

  __managed__ double syn_dir[3]; // vector pointing from origin to observer

  //#define NRAYS_SYN 1 // number of rays traced in a syn calculation

  //RAY rays[NRAYS_SYN];
  __managed__ struct time *time_step;

  __managed__ int nsyn_time;
  __managed__ double time_syn[MSYN_TIME];

  __managed__ int emiss_offset;   // the index in the line list of the 1st line for which
                                  // an emissivity estimator is recorded
  __managed__ int emiss_max;      // actual number of frequency points in emissivity grid


  __managed__ modelgrid_t modelgrid[MMODELGRID + 1];

  /// THESE ARE THE GRID BASED ESTIMATORS
  __managed__ float compton_emiss[MMODELGRID+1][EMISS_MAX];  /// Volume estimator for the compton emissivity
  __managed__ double rpkt_emiss[MMODELGRID+1];                /// Volume estimator for the rpkt emissivity


  #if (!NO_LUT_PHOTOION)
    __managed__ double corrphotoionrenorm[MMODELGRID * MELEMENTS * MIONS];
    __managed__ double gammaestimator[MMODELGRID * MELEMENTS * MIONS];
  #endif
  #if (!NO_LUT_BFHEATING)
    __managed__ double bfheatingestimator[MMODELGRID * MELEMENTS * MIONS];
  #endif
  #ifdef FORCE_LTE
    // don't use the variables below in LTE mode, just declare them here so the code compiles
    __managed__ double *ffheatingestimator;
  #else
    __managed__ double ffheatingestimator[MMODELGRID + 1];
    __managed__ double colheatingestimator[MMODELGRID + 1];

    #ifdef DO_TITER
      __managed__ double ffheatingestimator_save[MMODELGRID];
      __managed__ double colheatingestimator_save[MMODELGRID];
      __managed__ double gammaestimator_save[MMODELGRID * MELEMENTS * MIONS];
      __managed__ double bfheatingestimator_save[MMODELGRID * MELEMENTS * MIONS];
    #endif
  #endif

  #ifdef RECORD_LINESTAT
    __managed__ int *ecounter;
    __managed__ int *acounter;
    __managed__ int *linestat_reduced;
  #endif


  __managed__ bool file_set; // 1 if the output files already exist. 0 otherwise.

  __managed__ bool do_comp_est; // 1 = compute compton emissivity estimators. 0 = don't
  __managed__ bool do_r_lc;     // If not set to 1 then the opacity for r-packets is 0.
  __managed__ int do_rlc_est;  // 1 = compute estimators for the r-pkt light curve.
                               // 2 = compute estimators with opacity weights
                               // 3 = compute estimators, but use only for gamma-heating rate


  __managed__ int n_out_it; // # of sets of 1,000,000 photons to run.

  __managed__ double vout_model[MMODELGRID];
  __managed__ double t_model; // time at which densities in input model are correct.
  __managed__ int ncoord_model[3]; // the model.txt input grid dimensions
  __managed__ double dcoord1;
  __managed__ double dcoord2; // spacings of a 2D model grid - must be uniform grid

  __managed__ double CLIGHT_PROP; // Speed of light for ray travel. Physically = CLIGHT but
                                  // can be changed for testing.

  __managed__ double gamma_grey; // set to -ve for proper treatment. If possitive, then
                                 // gamma_rays are treated as grey with this opacity.

  __managed__ double min_den;

  __managed__ double max_path_step;

  __managed__ int opacity_case; // 0 normally, 1 for Fe-grp dependence.
                    ///MK: 2 for Fe-grp dependence and proportional to 1/rho
                    ///MK: 3 combination of 1 & 2 depending on a rho_crit
                    ///MK: 4 non-grey treatment



  /// ATOMIC DATA

  __managed__ int maxion;

  __managed__ int nlines;
  __managed__ int includedions;
  __managed__ elementlist_entry *elements;
  __managed__ linelist_entry *linelist;
  __managed__ bflist_t *bflist;

  __managed__ rpkt_cont_opacity_struct *kappa_rpkt_cont;

  /// Coolinglist
  __managed__ int ncoolingterms;
  __managed__ int importantcoolingterms;

  /// PHIXSLIST

  __managed__ double *allcont_nu_edge;
  __managed__ fullphixslist_t *allcont;
  __managed__ phixslist_t *phixslist;
  __managed__ int nbfcontinua;
  __managed__ int nbfcontinua_ground; ///number of bf-continua
  __managed__ int NPHIXSPOINTS;
  __managed__ double NPHIXSNUINCREMENT;

  __managed__ cellhistory_struct *cellhistory;

  __managed__ int debuglevel;

  __managed__ int ncoordgrid[3]; /// propagration grid dimensions
  __managed__ int ngrid;
  __managed__ int grid_type;
  __managed__ char coordlabel[3];

  __managed__ int nprocs = -1;      /// Global variable which holds the number of MPI processes
  __managed__ int rank_global = -1; /// Global variable which holds the rank of the active MPI process
  __managed__ int npkts = -1;
  __managed__ int nesc = 0; //number of packets that escape during current timestep

  __managed__ double coordmax[3];
  __managed__ double mtot;
  __managed__ double vmax;
  __managed__ double rmax;  /// Total mass and outer velocity/radius
  __managed__ double mfeg;              /// Total mass of Fe group elements in ejecta
  __managed__ double tmax;              /// End time of current simulation
  __managed__ double tmin;              /// Start time of current simulation

  __managed__ int ntstep;       /// Number of timesteps
  __managed__ int itstep;       /// Initial timestep's number
  __managed__ int ftstep;       /// Final timestep's number
  __managed__ int nts_global;   /// Current time step

  __managed__ int nnubins; //number of bins for spectrum
  __managed__ double nu_min_r;
  __managed__ double nu_max_r; //limits on frequency range for r-pkt spectrum

  __managed__ double nusyn_min;
  __managed__ double nusyn_max; //limits on range for syn
  __managed__ int nfake_gam; //# of fake gamma ray lines for syn

  /// New variables for other opacity cases, still grey.
  __managed__ double opcase3_normal;           ///MK: normalisation factor for opacity_case 3
  __managed__ double rho_crit_para;            ///MK: free parameter for the selection of the critical opacity in opacity_case 3
  __managed__ double rho_crit;                 ///MK: critical opacity in opacity_case 3 (could now be declared locally)

  /// New variables for the non-grey case
  __managed__ int debug_packet;                /// activate debug output for this packet if non negative
  __managed__ int n_middle_it;

  __managed__ int total_nlte_levels;            ///total number of nlte levels

  __managed__ bool homogeneous_abundances;

  __managed__ bool simulation_continued_from_saved;
  __managed__ double nu_rfcut;
  __managed__ int n_lte_timesteps;
  __managed__ double cell_is_optically_thick;
  __managed__ int n_grey_timesteps;
  __managed__ int n_titer;
  __managed__ bool initial_iteration;
  __managed__ int max_bf_continua;
  __managed__ int n_kpktdiffusion_timesteps;
  __managed__ float kpktdiffusion_timescale;

}