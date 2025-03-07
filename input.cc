#include "input.h"

#include <gsl/gsl_spline.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#include "atomic.h"
#include "exspec.h"
#include "gammapkt.h"
#include "grid.h"
#include "kpkt.h"
#include "nltepop.h"
#include "rpkt.h"
#include "sn3d.h"
#include "vpkt.h"

const int groundstate_index_in = 1;  // starting level index in the input files

struct transitions {
  int *to;
};

struct transitiontable_entry {
  int lower;
  int upper;
  double A;
  double coll_str;
  bool forbidden;
};  /// only used temporarily during input

const int inputlinecommentcount = 24;
std::string inputlinecomments[inputlinecommentcount] = {
    "pre_zseed: specific random number seed if > 0 or random if negative",
    "globals::ntstep: number of timesteps",
    "itstep ftstep: timestep number range start (inclusive) and stop (not inclusive)",
    "tmin_days tmax_days: start and end times [day]",
    "nusyn_min_mev nusyn_max_mev: lowest and highest frequency to synthesise [MeV]",
    "nsyn_time: number of times for synthesis",
    "start and end times for synthesis",
    "model_type: number of dimensions (1, 2, or 3)",
    "compute r-light curve (1: no estimators, 2: thin cells, 3: thick cells, 4: gamma-ray heating)",
    "n_out_it: UNUSED number of iterations",
    "UNUSED: change speed of light by some factor. Change constants.h CLIGHT_PROP instead",
    "use grey opacity for gammas?",
    "syn_dir: x, y, and z components of unit vector (will be normalised after input or randomised if zero length)",
    "opacity_case: opacity choice",
    "rho_crit_para: free parameter for calculation of rho_crit",
    "UNUSED debug_packet: (>=0: activate debug output for packet id, <0: ignore)",
    "simulation_continued_from_saved: (0: start new simulation, 1: continue from gridsave and packets files)",
    "UNUSED rfcut_angstroms: wavelength (in Angstroms) at which the parameterisation of the radiation field switches "
    "from the nebular approximation to LTE.",
    "num_lte_timesteps",
    "cell_is_optically_thick num_grey_timesteps",
    "UNUSED max_bf_continua: (>0: max bound-free continua per ion, <0 unlimited)",
    "nprocs_exspec: extract spectra for n MPI tasks",
    "do_emission_res: Extract line-of-sight dependent information of last emission for spectrum_res (1: yes, 2: no)",
    "kpktdiffusion_timescale n_kpktdiffusion_timesteps: kpkts diffuse x of a time step's length for the first y time "
    "steps"};

static void read_phixs_data_table(FILE *phixsdata, const int nphixspoints_inputtable, const int element,
                                  const int lowerion, const int lowerlevel, const int upperion, int upperlevel_in,
                                  const double phixs_threshold_ev, long *mem_usage_phixs) {
  if (upperlevel_in >= 0)  // file gives photoionisation to a single target state only
  {
    int upperlevel = upperlevel_in - groundstate_index_in;
    assert_always(upperlevel >= 0);
    assert_always(globals::elements[element].ions[lowerion].levels[lowerlevel].nphixstargets == 0);
    globals::elements[element].ions[lowerion].levels[lowerlevel].nphixstargets = 1;
    *mem_usage_phixs += sizeof(phixstarget_entry);

    assert_always(globals::elements[element].ions[lowerion].levels[lowerlevel].phixstargets == NULL);
    globals::elements[element].ions[lowerion].levels[lowerlevel].phixstargets =
        static_cast<phixstarget_entry *>(calloc(1, sizeof(phixstarget_entry)));
    assert_always(globals::elements[element].ions[lowerion].levels[lowerlevel].phixstargets != NULL);

    if (single_level_top_ion &&
        (upperion == get_nions(element) - 1))  // top ion has only one level, so send it to that level
    {
      upperlevel = 0;
    }
    globals::elements[element].ions[lowerion].levels[lowerlevel].phixstargets[0].levelindex = upperlevel;
    globals::elements[element].ions[lowerion].levels[lowerlevel].phixstargets[0].probability = 1.0;
  } else  // upperlevel < 0, indicating that a table of upper levels and their probabilities will follow
  {
    int in_nphixstargets;
    assert_always(fscanf(phixsdata, "%d\n", &in_nphixstargets) == 1);
    assert_always(in_nphixstargets >= 0);
    // read in a table of target states and probabilities and store them
    if (!single_level_top_ion || upperion < get_nions(element) - 1)  // in case the top ion has nlevelsmax = 1
    {
      globals::elements[element].ions[lowerion].levels[lowerlevel].nphixstargets = in_nphixstargets;
      *mem_usage_phixs += in_nphixstargets * sizeof(phixstarget_entry);

      globals::elements[element].ions[lowerion].levels[lowerlevel].phixstargets =
          static_cast<phixstarget_entry *>(calloc(in_nphixstargets, sizeof(phixstarget_entry)));
      assert_always(globals::elements[element].ions[lowerion].levels[lowerlevel].phixstargets != NULL);

      double probability_sum = 0.;
      for (int i = 0; i < in_nphixstargets; i++) {
        double phixstargetprobability;
        assert_always(fscanf(phixsdata, "%d %lg\n", &upperlevel_in, &phixstargetprobability) == 2);
        const int upperlevel = upperlevel_in - groundstate_index_in;
        assert_always(upperlevel >= 0);
        assert_always(phixstargetprobability > 0);
        globals::elements[element].ions[lowerion].levels[lowerlevel].phixstargets[i].levelindex = upperlevel;
        globals::elements[element].ions[lowerion].levels[lowerlevel].phixstargets[i].probability =
            phixstargetprobability;
        probability_sum += phixstargetprobability;
      }
      if (fabs(probability_sum - 1.0) > 0.01) {
        printout("WARNING: photoionisation table for Z=%d ionstage %d has probabilities that sum to %g",
                 get_element(element), get_ionstage(element, lowerion), probability_sum);
      }
    } else  // file has table of target states and probabilities but our top ion is limited to one level
    {
      globals::elements[element].ions[lowerion].levels[lowerlevel].nphixstargets = 1;
      *mem_usage_phixs += sizeof(phixstarget_entry);
      globals::elements[element].ions[lowerion].levels[lowerlevel].phixstargets =
          static_cast<phixstarget_entry *>(calloc(1, sizeof(phixstarget_entry)));
      assert_always(globals::elements[element].ions[lowerion].levels[lowerlevel].phixstargets != NULL);

      for (int i = 0; i < in_nphixstargets; i++) {
        double phixstargetprobability;
        assert_always(fscanf(phixsdata, "%d %lg\n", &upperlevel_in, &phixstargetprobability) == 2);
      }

      // send it to the ground state of the top ion
      globals::elements[element].ions[lowerion].levels[lowerlevel].phixstargets[0].levelindex = 0;
      globals::elements[element].ions[lowerion].levels[lowerlevel].phixstargets[0].probability = 1.0;
    }
  }

  /// The level contributes to the ionisinglevels if its energy
  /// is below the ionisation potential and the level doesn't
  /// belong to the topmost ion included.
  /// Rate coefficients are only available for ionising levels.
  //  also need (levelenergy < ionpot && ...)?
  if (lowerion < get_nions(element) - 1)  /// thats only an option for pure LTE && level < TAKE_N_BFCONTINUA)
  {
    for (int phixstargetindex = 0; phixstargetindex < get_nphixstargets(element, lowerion, lowerlevel);
         phixstargetindex++) {
      const int upperlevel = get_phixsupperlevel(element, lowerion, lowerlevel, phixstargetindex);
      if (upperlevel > get_maxrecombininglevel(element, lowerion + 1)) {
        globals::elements[element].ions[lowerion + 1].maxrecombininglevel = upperlevel;
      }
    }
  }

  *mem_usage_phixs += globals::NPHIXSPOINTS * sizeof(float);
  globals::elements[element].ions[lowerion].levels[lowerlevel].photoion_xs =
      static_cast<float *>(calloc(globals::NPHIXSPOINTS, sizeof(float)));
  assert_always(globals::elements[element].ions[lowerion].levels[lowerlevel].photoion_xs != NULL);

  if (phixs_threshold_ev > 0) {
    globals::elements[element].ions[lowerion].levels[lowerlevel].phixs_threshold = phixs_threshold_ev * EV;
  } else {
    if (get_nphixstargets(element, lowerion, lowerlevel) > 0) {
      const int lowestupperlevel = get_phixsupperlevel(element, lowerion, lowerlevel, 0);
      const double calced_phixs_threshold =
          (epsilon(element, upperion, lowestupperlevel) - epsilon(element, lowerion, lowerlevel));
      globals::elements[element].ions[lowerion].levels[lowerlevel].phixs_threshold = calced_phixs_threshold;
    }
  }

  if (phixs_file_version == 1) {
    assert_always(get_nphixstargets(element, lowerion, lowerlevel) == 1);
    assert_always(get_phixsupperlevel(element, lowerion, lowerlevel, 0) == 0);

    double nu_edge = (epsilon(element, upperion, 0) - epsilon(element, lowerion, lowerlevel)) / H;

    double *nutable = static_cast<double *>(calloc(nphixspoints_inputtable, sizeof(double)));
    assert_always(nutable != NULL);
    double *phixstable = static_cast<double *>(calloc(nphixspoints_inputtable, sizeof(double)));
    assert_always(phixstable != NULL);

    for (int i = 0; i < nphixspoints_inputtable; i++) {
      double energy = -1.;
      double phixs = -1.;
      assert_always(fscanf(phixsdata, "%lg %lg", &energy, &phixs) == 2);
      nutable[i] = nu_edge + (energy * 13.6 * EV) / H;
      /// the photoionisation cross-sections in the database are given in Mbarn=1e6 * 1e-28m^2
      /// to convert to cgs units multiply by 1e-18
      phixstable[i] = phixs * 1e-18;
    }
    const double nu_max = nutable[nphixspoints_inputtable - 1];

    // Now interpolate these cross-sections
    globals::elements[element].ions[lowerion].levels[lowerlevel].photoion_xs[0] = phixstable[0];

    gsl_interp_accel *acc = gsl_interp_accel_alloc();
    gsl_spline *spline = gsl_spline_alloc(gsl_interp_linear, nphixspoints_inputtable);
    gsl_spline_init(spline, nutable, phixstable, nphixspoints_inputtable);
    for (int i = 1; i < globals::NPHIXSPOINTS; i++) {
      const double nu = nu_edge * (1. + i * globals::NPHIXSNUINCREMENT);
      if (nu > nu_max) {
        const double phixs = phixstable[nphixspoints_inputtable - 1] * pow(nu_max / nu, 3);
        globals::elements[element].ions[lowerion].levels[lowerlevel].photoion_xs[i] = phixs;
      } else {
        const double phixs = gsl_spline_eval(spline, nu, acc);
        globals::elements[element].ions[lowerion].levels[lowerlevel].photoion_xs[i] = phixs;
      }
    }
    gsl_spline_free(spline);
    gsl_interp_accel_free(acc);
    free(nutable);
    free(phixstable);
  } else {
    for (int i = 0; i < globals::NPHIXSPOINTS; i++) {
      float phixs;
      assert_always(fscanf(phixsdata, "%g\n", &phixs) == 1);
      assert_always(phixs >= 0);

      /// the photoionisation cross-sections in the database are given in Mbarn = 1e6 * 1e-28m^2
      /// to convert to cgs units multiply by 1e-18
      globals::elements[element].ions[lowerion].levels[lowerlevel].photoion_xs[i] = phixs * 1e-18;
      // fprintf(database_file,"%g %g\n", nutable[i], phixstable[i]);
    }
  }

  // nbfcontinua++;
  // printout("[debug] element %d, ion %d, level %d: phixs exists %g\n",element,lowerion,lowerlevel,phixs*1e-18);
  globals::nbfcontinua += get_nphixstargets(element, lowerion, lowerlevel);
  if (lowerlevel < get_nlevels_groundterm(element, lowerion)) {
    globals::nbfcontinua_ground += get_nphixstargets(element, lowerion, lowerlevel);
  }
}

static void read_phixs_data(int phixs_file_version) {
  globals::nbfcontinua_ground = 0;
  globals::nbfcontinua = 0;
  long mem_usage_phixs = 0;

  printout("readin phixs data from %s\n", phixsdata_filenames[phixs_file_version]);

  FILE *phixsdata = fopen_required(phixsdata_filenames[phixs_file_version], "r");

  if (phixs_file_version == 1) {
    globals::NPHIXSPOINTS = 100;
    globals::NPHIXSNUINCREMENT = .1;
    last_phixs_nuovernuedge = 10;
  } else {
    assert_always(fscanf(phixsdata, "%d\n", &globals::NPHIXSPOINTS) == 1);
    assert_always(globals::NPHIXSPOINTS > 0);
    assert_always(fscanf(phixsdata, "%lg\n", &globals::NPHIXSNUINCREMENT) == 1);
    assert_always(globals::NPHIXSNUINCREMENT > 0.);
    last_phixs_nuovernuedge = (1.0 + globals::NPHIXSNUINCREMENT * (globals::NPHIXSPOINTS - 1));
  }

  int Z = -1;
  int upperionstage = -1;
  int upperlevel_in = -1;
  int lowerionstage = -1;
  int lowerlevel_in = -1;
  double phixs_threshold_ev = -1;
  while (true) {
    int readstatus = -1;
    int nphixspoints_inputtable = 0;
    if (phixs_file_version == 1) {
      readstatus = fscanf(phixsdata, "%d %d %d %d %d %d\n", &Z, &upperionstage, &upperlevel_in, &lowerionstage,
                          &lowerlevel_in, &nphixspoints_inputtable);
    } else {
      readstatus = fscanf(phixsdata, "%d %d %d %d %d %lg\n", &Z, &upperionstage, &upperlevel_in, &lowerionstage,
                          &lowerlevel_in, &phixs_threshold_ev);
      nphixspoints_inputtable = globals::NPHIXSPOINTS;
    }
    if (readstatus == EOF) {
      break;
    }
    assert_always(readstatus == 6);
    assert_always(Z > 0);
    assert_always(upperionstage >= 2);
    assert_always(lowerionstage >= 1);
    bool skip_this_phixs_table = false;
    // printout("[debug] Z %d, upperion %d, upperlevel %d, lowerion %d, lowerlevel,
    // %d\n",Z,upperion,upperlevel,lowerion,lowerlevel);
    /// translate readin anumber to element index
    const int element = get_elementindex(Z);

    /// store only photoionization crosssections for elements that are part of the current model atom
    skip_this_phixs_table = true;  // will be set to false for good data
    if (element >= 0) {
      /// translate readin ionstages to ion indices

      const int upperion = upperionstage - get_ionstage(element, 0);
      const int lowerion = lowerionstage - get_ionstage(element, 0);
      const int lowerlevel = lowerlevel_in - groundstate_index_in;
      assert_always(lowerionstage >= 0);
      assert_always(lowerlevel >= 0);
      /// store only photoionization crosssections for ions that are part of the current model atom
      if (lowerion >= 0 && lowerlevel < get_nlevels(element, lowerion) && upperion < get_nions(element)) {
        read_phixs_data_table(phixsdata, nphixspoints_inputtable, element, lowerion, lowerlevel, upperion,
                              upperlevel_in, phixs_threshold_ev, &mem_usage_phixs);

        skip_this_phixs_table = false;
      }
    }

    if (skip_this_phixs_table)  // for ions or elements that are not part of the current model atom, proceed through the
                                // lines and throw away the data
    {
      if (upperlevel_in < 0)  // a table of target states and probabilities will follow, so read past those lines
      {
        int nphixstargets;
        assert_always(fscanf(phixsdata, "%d\n", &nphixstargets) == 1);
        for (int i = 0; i < nphixstargets; i++) {
          double phixstargetprobability;
          assert_always(fscanf(phixsdata, "%d %lg\n", &upperlevel_in, &phixstargetprobability) == 2);
        }
      }
      for (int i = 0; i < nphixspoints_inputtable; i++)  // skip through cross section list
      {
        float phixs = 0;
        if (phixs_file_version == 1) {
          double energy = 0;
          assert_always(fscanf(phixsdata, "%lg %g\n", &energy, &phixs) == 2);
        } else {
          assert_always(fscanf(phixsdata, "%g\n", &phixs) == 1);
        }
      }
    }
  }

  fclose(phixsdata);

  printout("[info] mem_usage: photoionisation tables occupy %.3f MB\n", mem_usage_phixs / 1024. / 1024.);
}

static void read_ion_levels(FILE *adata, const int element, const int ion, const int nions, const int nlevels,
                            int nlevelsmax, const double energyoffset, const double ionpot,
                            struct transitions *transitions) {
  const int nlevels_used = std::min(nlevels, nlevelsmax);
  // each level contains 0..level elements. seriess sum of 1 + 2 + 3 + 4 + ... + nlevels_used is used here
  const int transitblocksize = nlevels_used * (nlevels_used + 1) / 2;
  transitions[0].to = static_cast<int *>(malloc(transitblocksize * sizeof(int)));

  int transitionblockindex = 0;
  for (int level = 0; level < nlevels; level++) {
    int levelindex_in;
    double levelenergy;
    double statweight;
    int ntransitions;
    assert_always(fscanf(adata, "%d %lg %lg %d%*[^\n]\n", &levelindex_in, &levelenergy, &statweight, &ntransitions) ==
                  4);
    assert_always(levelindex_in == level + groundstate_index_in);

    if (level < nlevelsmax) {
      const double currentlevelenergy = (energyoffset + levelenergy) * EV;
      globals::elements[element].ions[ion].levels[level].epsilon = currentlevelenergy;

      // if (level == 0 && ion == 0) energyoffset = levelenergy;
      globals::elements[element].ions[ion].levels[level].stat_weight = statweight;
      assert_always(statweight > 0.);
      /// Moved to the section with ionising levels below
      // globals::elements[element].ions[ion].levels[level].cont_index = cont_index;
      // cont_index--;
      /// Initialise the metastable flag to true. Set it to false if a downward transition exists.
      globals::elements[element].ions[ion].levels[level].metastable = true;
      // globals::elements[element].ions[ion].levels[level].main_qn = mainqn;

      /// The level contributes to the ionisinglevels if its energy
      /// is below the ionization potential and the level doesn't
      /// belong to the topmost ion included.
      /// Rate coefficients are only available for ionising levels.
      if (levelenergy < ionpot && ion < nions - 1)  /// thats only an option for pure LTE && level < TAKE_N_BFCONTINUA)
      {
        globals::elements[element].ions[ion].ionisinglevels++;
      }

      /// store the possible downward transitions from the current level in following order to memory
      ///     A_level,level-1; A_level,level-2; ... A_level,1
      /// entries which are not explicitly set are zero (the zero is set/initialized by calloc!)
      transitions[level].to = &transitions[0].to[transitionblockindex];
      transitionblockindex += level;
      assert_always((transitionblockindex + level) < transitblocksize);

      /// initialize number of downward transitions to zero
      set_ndowntrans(element, ion, level, 0);

      /// initialize number of upward transitions to zero
      set_nuptrans(element, ion, level, 0);
    } else {
      // globals::elements[element].ions[ion].levels[nlevelsmax - 1].stat_weight += statweight;
    }
  }
}

static void read_ion_transitions(std::istream &ftransitiondata, const int tottransitions_in_file, int *tottransitions,
                                 std::vector<struct transitiontable_entry> &transitiontable,
                                 const int nlevels_requiretransitions, const int nlevels_requiretransitions_upperlevels,
                                 const int Z, const int ionstage) {
  std::string line;

  if (*tottransitions == 0) {
    // we will not read in any transitions, just skip past these lines in the file
    for (int i = 0; i < tottransitions_in_file; i++) {
      assert_always(getline(ftransitiondata, line));
    }
  } else {
    // will be autodetected from first table row. old format had an index column and no collstr or forbidden columns
    bool oldtransitionformat = false;

    int prev_upper = -1;
    int prev_lower = 0;
    for (int i = 0; i < tottransitions_in_file; i++) {
      int lower_in = -1;
      int upper_in = -1;
      double A = 0;
      double coll_str = -1.;
      int intforbidden = 0;
      assert_always(getline(ftransitiondata, line));
      if (i == 0) {
        std::stringstream ss(line);
        std::string word;
        int word_count = 0;
        while (ss >> word) {
          word_count++;
        }
        assert_always(word_count == 4 || word_count == 5);
        oldtransitionformat = (word_count == 4);
      }
      if (!oldtransitionformat) {
        assert_always(sscanf(line.c_str(), "%d %d %lg %lg %d", &lower_in, &upper_in, &A, &coll_str, &intforbidden) ==
                      5);
      } else {
        int transindex = 0;  // not used
        assert_always(sscanf(line.c_str(), "%d %d %d %lg", &transindex, &lower_in, &upper_in, &A) == 4);
      }
      const int lower = lower_in - groundstate_index_in;
      const int upper = upper_in - groundstate_index_in;
      assert_always(lower >= 0);
      assert_always(upper >= 0);

      // this entire block can be removed if we don't want to add in extra collisonal
      // transitions between levels
      if (prev_lower < nlevels_requiretransitions) {
        int stoplevel;
        if (lower == prev_lower && upper > prev_upper + 1) {
          // same lower level, but some upper levels were skipped over
          stoplevel = upper - 1;
          if (stoplevel >= nlevels_requiretransitions_upperlevels) {
            stoplevel = nlevels_requiretransitions_upperlevels - 1;
          }
        } else if ((lower > prev_lower) && prev_upper < (nlevels_requiretransitions_upperlevels - 1)) {
          // we've moved onto another lower level, but the previous one was missing some required transitions
          stoplevel = nlevels_requiretransitions_upperlevels - 1;
        } else {
          stoplevel = -1;
        }

        for (int tmplevel = prev_upper + 1; tmplevel <= stoplevel; tmplevel++) {
          if (tmplevel == prev_lower) {
            continue;
          }
          // printout("+adding transition index %d Z=%02d ionstage %d lower %d upper %d\n", i, Z, ionstage, prev_lower,
          // tmplevel);
          (*tottransitions)++;
          assert_always(prev_lower >= 0);
          assert_always(tmplevel >= 0);
          transitiontable.push_back(
              {.lower = prev_lower, .upper = tmplevel, .A = 0., .coll_str = -2., .forbidden = true});
        }
      }

      transitiontable.push_back(
          {.lower = lower, .upper = upper, .A = A, .coll_str = coll_str, .forbidden = (intforbidden == 1)});
      // printout("index %d, lower %d, upper %d, A %g\n",transitionindex,lower,upper,A);
      //  printout("reading transition index %d lower %d upper %d\n", i, transitiontable[i].lower,
      //  transitiontable[i].upper);
      prev_lower = lower;
      prev_upper = upper;
    }
  }
}

constexpr bool operator<(const linelist_entry &a, const linelist_entry &b)
// sort the lineline in descending frequency
{
  return a.nu > b.nu;
}

constexpr int compare_linelistentry(const void *p1, const void *p2)
/// Helper function to sort the linelist by frequency.
{
  linelist_entry *a1 = (linelist_entry *)(p1);
  linelist_entry *a2 = (linelist_entry *)(p2);

  if (fabs(a2->nu - a1->nu) < (1.e-10 * a1->nu)) {
    if ((a1->elementindex == a2->elementindex) && (a1->ionindex == a2->ionindex) &&
        (a1->lowerlevelindex == a2->lowerlevelindex) && (a1->upperlevelindex == a2->upperlevelindex)) {
      printout("Duplicate transition line? %s\n", a1->nu == a2->nu ? "nu match exact" : "close to nu match");
      printout("a: Z=%d ionstage %d lower %d upper %d nu %g lambda %g\n", get_element(a1->elementindex),
               get_ionstage(a1->elementindex, a1->ionindex), a1->lowerlevelindex, a1->upperlevelindex, a1->nu,
               1e8 * CLIGHT / a1->nu);
      printout("b: Z=%d ionstage %d lower %d upper %d nu %g lambda %g\n", get_element(a2->elementindex),
               get_ionstage(a2->elementindex, a2->ionindex), a2->lowerlevelindex, a2->upperlevelindex, a2->nu,
               1e8 * CLIGHT / a2->nu);
    }

    a2->nu = a1->nu;
    // if ((a1->elementindex == a2->elementindex) && (a1->ionindex == a2->ionindex)) {
    if (a1->lowerlevelindex > a2->lowerlevelindex) {
      return -1;
    } else if (a1->lowerlevelindex < a2->lowerlevelindex) {
      return 1;
    } else if (a1->upperlevelindex > a2->upperlevelindex) {
      return -1;
    } else if (a1->upperlevelindex < a2->upperlevelindex) {
      return 1;
    } else {
      return 0;
    }
    // }
  }

  if ((a1->nu < a2->nu) || (a1->nu == a2->nu))
    return 1;
  else if (a1->nu > a2->nu)
    return -1;
  else
    return 0;
}

static void add_transitions_to_linelist(const int element, const int ion, const int nlevelsmax,
                                        const std::vector<struct transitiontable_entry> &transitiontable,
                                        struct transitions *transitions, int *lineindex,
                                        std::vector<struct linelist_entry> &temp_linelist) {
  const int lineindex_initial = *lineindex;
  const int tottransitions = transitiontable.size();
  int totupdowntrans;
  // pass 0 to get transition counts of each level
  // pass 1 to allocate and fill transition arrays
  for (int pass = 0; pass < 2; pass++) {
    *lineindex = lineindex_initial;
    if (pass == 1) {
      int alltransindex = 0;
      struct level_transition *alltransblock = nullptr;

#ifdef MPI_ON
      MPI_Barrier(MPI_COMM_WORLD);
      MPI_Win win;

      int my_rank_trans = totupdowntrans / globals::node_nprocs;
      // rank_in_node 0 gets any remainder
      if (globals::rank_in_node == 0) {
        my_rank_trans += totupdowntrans - (my_rank_trans * globals::node_nprocs);
      }

      MPI_Aint size = my_rank_trans * sizeof(linelist_entry);
      int disp_unit = sizeof(linelist_entry);
      MPI_Win_allocate_shared(size, disp_unit, MPI_INFO_NULL, globals::mpi_comm_node, &alltransblock, &win);

      MPI_Win_shared_query(win, 0, &size, &disp_unit, &alltransblock);
#else
      alltransblock = static_cast<struct level_transition *>(malloc(totupdowntrans * sizeof(struct level_transition)));
#endif

      for (int level = 0; level < nlevelsmax; level++) {
        globals::elements[element].ions[ion].levels[level].downtrans = &alltransblock[alltransindex];
        alltransindex += get_ndowntrans(element, ion, level);

        globals::elements[element].ions[ion].levels[level].uptrans = &alltransblock[alltransindex];
        alltransindex += get_nuptrans(element, ion, level);

        set_ndowntrans(element, ion, level, 0);
        set_nuptrans(element, ion, level, 0);
      }
    }

    for (int level = 0; level < nlevelsmax; level++) {
      std::fill_n(transitions[level].to, level, -99);
    }

    totupdowntrans = 0;
    for (int ii = 0; ii < tottransitions; ii++) {
      const int level = transitiontable[ii].upper;
      const int targetlevel = transitiontable[ii].lower;
      if (pass == 0) {
        assert_always(targetlevel >= 0);
        assert_always(level > targetlevel);
      }

      double nu_trans = -1.;
      if (targetlevel < nlevelsmax && level < nlevelsmax) {
        nu_trans = (epsilon(element, ion, level) - epsilon(element, ion, targetlevel)) / H;
      }
      if (!(nu_trans > 0)) continue;

      /// Make sure that we don't allow duplicate. In that case take only the lines
      /// first occurrence
      const int transitioncheck = transitions[level].to[(level - targetlevel) - 1];

      if (transitioncheck == -99) {
        transitions[level].to[level - targetlevel - 1] = *lineindex;

        const int nupperdowntrans = get_ndowntrans(element, ion, level) + 1;
        set_ndowntrans(element, ion, level, nupperdowntrans);

        const int nloweruptrans = get_nuptrans(element, ion, targetlevel) + 1;
        set_nuptrans(element, ion, targetlevel, nloweruptrans);

        totupdowntrans += 2;

        if (pass == 1 && globals::rank_in_node == 0) {
          const double A_ul = transitiontable[ii].A;
          const float coll_str = transitiontable[ii].coll_str;
          // globals::elements[element].ions[ion].levels[level].transitions[level-targetlevel-1].einstein_A = A_ul;

          const double g = stat_weight(element, ion, level) / stat_weight(element, ion, targetlevel);
          const float f_ul = g * ME * pow(CLIGHT, 3) / (8 * pow(QE * nu_trans * PI, 2)) * A_ul;
          assert_always(std::isfinite(f_ul));
          // f_ul = g * OSCSTRENGTHCONVERSION / pow(nu_trans,2) * A_ul;
          // globals::elements[element].ions[ion].levels[level].transitions[level-targetlevel-1].oscillator_strength =
          // g * ME*pow(CLIGHT,3)/(8*pow(QE*nu_trans*PI,2)) * A_ul;

          // printout("lineindex %d, element %d, ion %d, lower %d, upper %d, nu
          // %g\n",*lineindex,element,ion,level-i-1,level,nu_trans);

          temp_linelist.push_back({
              .nu = nu_trans,
              .einstein_A = static_cast<float>(A_ul),
              .osc_strength = f_ul,
              .coll_str = coll_str,
              .elementindex = element,
              .ionindex = ion,
              .upperlevelindex = level,
              .lowerlevelindex = targetlevel,
              .forbidden = transitiontable[ii].forbidden,
          });

          // the line list has not been sorted yet, so the store the negative level index for now and
          // this will be replaced with the index into the sorted line list later
          globals::elements[element].ions[ion].levels[level].downtrans[nupperdowntrans - 1].lineindex = -targetlevel;
          globals::elements[element].ions[ion].levels[targetlevel].uptrans[nloweruptrans - 1].lineindex = -level;
        }

        /// This is not a metastable level.
        globals::elements[element].ions[ion].levels[level].metastable = false;

        (*lineindex)++;

      } else if (pass == 1 && globals::rank_in_node == 0) {
        // This is a new branch to deal with lines that have different types of transition. It should trip after a
        // transition is already known.
        const int linelistindex = transitions[level].to[level - targetlevel - 1];
        const double A_ul = transitiontable[ii].A;
        const double coll_str = transitiontable[ii].coll_str;
        // globals::elements[element].ions[ion].levels[level].transitions[level-targetlevel-1].einstein_A = A_ul;

        const double g = stat_weight(element, ion, level) / stat_weight(element, ion, targetlevel);
        const double f_ul = g * ME * pow(CLIGHT, 3) / (8 * pow(QE * nu_trans * PI, 2)) * A_ul;
        // f_ul = g * OSCSTRENGTHCONVERSION / pow(nu_trans,2) * A_ul;
        // globals::elements[element].ions[ion].levels[level].transitions[level-targetlevel-1].oscillator_strength =
        // g * ME*pow(CLIGHT,3)/(8*pow(QE*nu_trans*PI,2)) * A_ul;

        if ((temp_linelist[linelistindex].elementindex != element) || (temp_linelist[linelistindex].ionindex != ion) ||
            (temp_linelist[linelistindex].upperlevelindex != level) ||
            (temp_linelist[linelistindex].lowerlevelindex != targetlevel)) {
          printout("[input.c] Failure to identify level pair for duplicate bb-transition ... going to abort now\n");
          printout("[input.c]   element %d ion %d targetlevel %d level %d\n", element, ion, targetlevel, level);
          printout("[input.c]   transitions[level].to[level-targetlevel-1]=linelistindex %d\n",
                   transitions[level].to[level - targetlevel - 1]);
          printout("[input.c]   A_ul %g, coll_str %g\n", A_ul, coll_str);
          printout(
              "[input.c]   globals::linelist[linelistindex].elementindex %d, "
              "globals::linelist[linelistindex].ionindex %d, globals::linelist[linelistindex].upperlevelindex "
              "%d, globals::linelist[linelistindex].lowerlevelindex %d\n",
              temp_linelist[linelistindex].elementindex, temp_linelist[linelistindex].ionindex,
              temp_linelist[linelistindex].upperlevelindex, temp_linelist[linelistindex].lowerlevelindex);
          abort();
        }
        temp_linelist[linelistindex].einstein_A += A_ul;
        temp_linelist[linelistindex].osc_strength += f_ul;
        if (coll_str > temp_linelist[linelistindex].coll_str) {
          temp_linelist[linelistindex].coll_str = coll_str;
        }
      }
    }
  }
#ifdef MPI_ON
  MPI_Barrier(MPI_COMM_WORLD);
#endif
}

static int get_lineindex(const int lelement, const int lion, const int llowerlevel, const int lupperlevel) {
  for (int lineindex = 0; lineindex < globals::nlines; lineindex++) {
    const int element = globals::linelist[lineindex].elementindex;
    const int ion = globals::linelist[lineindex].ionindex;
    const int lowerlevel = globals::linelist[lineindex].lowerlevelindex;
    const int upperlevel = globals::linelist[lineindex].upperlevelindex;

    if (lelement == element && lion == ion && llowerlevel == lowerlevel && lupperlevel == upperlevel) {
      return lineindex;
    }
  }
  assert_always(false);
  return -1;
}

static int calculate_nlevels_groundterm(int element, int ion) {
  const int nlevels = get_nlevels(element, ion);
  if (nlevels == 1) {
    return 1;
  }

  int nlevels_groundterm = 1;
  // detect single-level ground term
  const double endiff10 = epsilon(element, ion, 1) - epsilon(element, ion, 0);
  const double endiff21 = epsilon(element, ion, 2) - epsilon(element, ion, 1);
  if (endiff10 > 2. * endiff21) {
    nlevels_groundterm = 1;
  } else {
    for (int level = 1; level < nlevels - 2; level++) {
      const double endiff1 = epsilon(element, ion, level) - epsilon(element, ion, level - 1);
      const double endiff2 = epsilon(element, ion, level + 1) - epsilon(element, ion, level);
      if (endiff2 > 2. * endiff1) {
        nlevels_groundterm = level + 1;
        break;
      }
    }
  }

  // there should be no duplicate stat weights within the ground term
  // limit the ground multiplet to nnnnlowest levels below the first duplicated stat weight
  for (int level_a = 1; level_a < nlevels_groundterm; level_a++) {
    const float g_a = stat_weight(element, ion, level_a);

    for (int level_b = 0; level_b < level_a; level_b++) {
      const float g_b = stat_weight(element, ion, level_b);
      if (fabs(g_a - g_b) < 0.4) {
        // level_a is outside the ground term because of duplicate stat weight
        // highest ground level index is level_a - 1, so nlevels_groundterm == level_a
        return level_a;
      }
    }
  }

  return nlevels_groundterm;
}

static void read_atomicdata_files(void) {
  int totaluptrans = 0;
  int totaldowntrans = 0;

  FILE *compositiondata = fopen_required("compositiondata.txt", "r");

  FILE *adata = fopen_required("adata.txt", "r");

  printout("single_level_top_ion: %s\n", single_level_top_ion ? "true" : "false");
  printout("single_ground_level: %s\n", single_ground_level ? "true" : "false");
  /// initialize atomic data structure to number of elements
  int nelements_in;
  assert_always(fscanf(compositiondata, "%d", &nelements_in) == 1);
  set_nelements(nelements_in);
  globals::elements = static_cast<elementlist_entry *>(calloc(get_nelements(), sizeof(elementlist_entry)));
  assert_always(globals::elements != NULL);

  /// Initialize the linelist
  std::vector<struct linelist_entry> temp_linelist;

  /// temperature to determine relevant ionstages
  int T_preset;
  assert_always(fscanf(compositiondata, "%d", &T_preset) == 1);
  int homogeneous_abundances_in;
  assert_always(fscanf(compositiondata, "%d", &homogeneous_abundances_in) == 1);
  globals::homogeneous_abundances = (homogeneous_abundances_in != 0);
  if (globals::homogeneous_abundances)
    printout("[info] read_atomicdata: homogeneous abundances as defined in compositiondata.txt are active\n");

  /// open transition data file
  std::ifstream ftransitiondata("transitiondata.txt");
  assert_always(ftransitiondata.is_open());

  int lineindex = 0;         /// counter to determine the total number of lines
  int uniqueionindex = 0;    // index into list of all ions of all elements
  int uniquelevelindex = 0;  // index into list of all levels of all ions of all elements
  int nbfcheck = 0;
  for (int element = 0; element < get_nelements(); element++) {
    /// read information about the next element which should be stored to memory
    int Z;
    int nions;
    int lowermost_ionstage;
    int uppermost_ionstage;
    int nlevelsmax_readin;
    double abundance;
    double mass_amu;
    assert_always(fscanf(compositiondata, "%d %d %d %d %d %lg %lg", &Z, &nions, &lowermost_ionstage,
                         &uppermost_ionstage, &nlevelsmax_readin, &abundance, &mass_amu) == 7);
    printout("readin compositiondata: next element Z %d, nions %d, lowermost %d, uppermost %d, nlevelsmax %d\n", Z,
             nions, lowermost_ionstage, uppermost_ionstage, nlevelsmax_readin);
    assert_always(Z > 0);
    assert_always(nions >= 0);
    assert_always(nions == 0 || (nions == uppermost_ionstage - lowermost_ionstage + 1));
    assert_always(abundance >= 0);
    assert_always(mass_amu >= 0);

    update_max_nions(nions);
    assert_always(nions <= get_max_nions());

    /// write this element's data to memory
    globals::elements[element].anumber = Z;
    globals::elements[element].nions = nions;
    globals::elements[element].abundance = abundance;  /// abundances are expected to be given by mass
    globals::elements[element].initstablemeannucmass = mass_amu * MH;
    increase_includedions(nions);

    /// Initialize the elements ionlist
    globals::elements[element].ions = static_cast<ionlist_entry *>(calloc(nions, sizeof(ionlist_entry)));
    assert_always(globals::elements[element].ions != NULL);

    /// now read in data for all ions of the current element. before doing so initialize
    /// energy scale for the current element (all level energies are stored relative to
    /// the ground level of the neutral ion)
    double energyoffset = 0.;
    double ionpot = 0.;
    for (int ion = 0; ion < nions; ion++) {
      int nlevelsmax = nlevelsmax_readin;
      // printout("element %d ion %d\n", element, ion);
      /// calculate the current levels ground level energy
      assert_always(ionpot >= 0);
      energyoffset += ionpot;

      /// read information for the elements next ionstage
      int adata_Z_in = -1;
      int ionstage = -1;
      int nlevels = 0;
      // fscanf(adata,"%d %d %d %lg\n",&adata_Z_in,&ionstage,&nlevels,&ionpot);
      while (adata_Z_in != Z || ionstage != lowermost_ionstage + ion)  // skip over this ion block
      {
        if (adata_Z_in == Z) {
          printout("increasing energyoffset by ionpot %g\n", ionpot);
          energyoffset += ionpot;
        }
        for (int i = 0; i < nlevels; i++) {
          double levelenergy;
          double statweight;
          int levelindex;
          int ntransitions;
          assert_always(
              fscanf(adata, "%d %lg %lg %d%*[^\n]\n", &levelindex, &levelenergy, &statweight, &ntransitions) == 4);
        }

        assert_always(fscanf(adata, "%d %d %d %lg\n", &adata_Z_in, &ionstage, &nlevels, &ionpot) == 4);
      }

      printout("adata header matched: Z %d, ionstage %d, nlevels %d\n", adata_Z_in, ionstage, nlevels);

      if (single_level_top_ion && ion == nions - 1)  // limit the top ion to one level and no transitions
      {
        nlevelsmax = 1;
      }

      // if (adata_Z_in == 26 && ionstage == 1)
      // {
      //   nlevelsmax = 5;
      // }
      // else if (adata_Z_in == 26 && ionstage == 2)
      // {
      //   nlevelsmax = 5;
      // }

      if (nlevelsmax < 0) {
        nlevelsmax = nlevels;
      } else if (nlevels >= nlevelsmax) {
        printout("[info] read_atomicdata: reduce number of levels from %d to %d for Z %2d ionstage %d\n", nlevels,
                 nlevelsmax, adata_Z_in, ionstage);
      } else {
        printout(
            "[warning] read_atomicdata: requested nlevelsmax=%d > nlevels=%d for ion %d of element %d ... reduced "
            "nlevelsmax to nlevels\n",
            nlevelsmax, nlevels, ion, element);
        nlevelsmax = nlevels;
      }

      /// and proceed through the transitionlist till we match this ionstage (if it was not the neutral one)
      int transdata_Z_in = -1;
      int transdata_ionstage_in = -1;
      int tottransitions_in_file = 0;
      std::string line;
      while (transdata_Z_in != Z || transdata_ionstage_in != ionstage) {
        // skip over table
        for (int i = 0; i < tottransitions_in_file; i++) {
          assert_always(getline(ftransitiondata, line));
        }
        assert_always(get_noncommentline(ftransitiondata, line));
        assert_always(
            sscanf(line.c_str(), "%d %d %d", &transdata_Z_in, &transdata_ionstage_in, &tottransitions_in_file) == 3);
      }

      printout("transdata header matched: transdata_Z_in %d, transdata_ionstage_in %d, tottransitions %d\n",
               transdata_Z_in, transdata_ionstage_in, tottransitions_in_file);
      assert_always(tottransitions_in_file >= 0);

      int tottransitions = tottransitions_in_file;

      if (single_level_top_ion && ion == nions - 1)  // limit the top ion to one level and no transitions
      {
        tottransitions = 0;
      }

      assert_always(transdata_Z_in == Z);
      assert_always(transdata_ionstage_in == ionstage);

      /// read in the level and transition data for this ion
      std::vector<struct transitiontable_entry> transitiontable;
      transitiontable.reserve(tottransitions);

      /// load transition table for the CURRENT ion to temporary memory

      // first <nlevels_requiretransitions> levels will be collisionally
      // coupled to the first <nlevels_requiretransitions_upperlevels> levels (assumed forbidden)
      // use 0 to disable adding extra transitions

      int nlevels_requiretransitions = NLEVELS_REQUIRETRANSITIONS(Z, ionstage);
      int nlevels_requiretransitions_upperlevels = nlevelsmax;  // no effect if previous line is zero

      nlevels_requiretransitions = std::min(nlevelsmax, nlevels_requiretransitions);
      nlevels_requiretransitions_upperlevels = std::min(nlevelsmax, nlevels_requiretransitions_upperlevels);

      read_ion_transitions(ftransitiondata, tottransitions_in_file, &tottransitions, transitiontable,
                           nlevels_requiretransitions, nlevels_requiretransitions_upperlevels, Z, ionstage);

      /// store the ions data to memory and set up the ions zeta and levellist
      globals::elements[element].ions[ion].ionstage = ionstage;
      globals::elements[element].ions[ion].nlevels = nlevelsmax;
      globals::elements[element].ions[ion].ionisinglevels = 0;
      globals::elements[element].ions[ion].maxrecombininglevel = 0;
      globals::elements[element].ions[ion].ionpot = ionpot * EV;
      globals::elements[element].ions[ion].nlevels_groundterm = -1;
      globals::elements[element].ions[ion].uniqueionindex = uniqueionindex;

      globals::elements[element].ions[ion].Alpha_sp = static_cast<float *>(calloc(TABLESIZE, sizeof(float)));
      assert_always(globals::elements[element].ions[ion].Alpha_sp != NULL);
      globals::elements[element].ions[ion].levels =
          static_cast<struct levellist_entry *>(calloc(nlevelsmax, sizeof(struct levellist_entry)));
      assert_always(globals::elements[element].ions[ion].levels != NULL);

      /// now we need to readout the data for all those levels, write them to memory
      /// and set up the list of possible transitions for each level
      struct transitions *transitions =
          static_cast<struct transitions *>(calloc(nlevelsmax, sizeof(struct transitions)));
      assert_always(transitions != NULL);

      read_ion_levels(adata, element, ion, nions, nlevels, nlevelsmax, energyoffset, ionpot, transitions);

      add_transitions_to_linelist(element, ion, nlevelsmax, transitiontable, transitions, &lineindex, temp_linelist);

      free(transitions[0].to);
      free(transitions);
      transitions = nullptr;
      transitiontable.clear();

      for (int level = 0; level < nlevelsmax; level++) {
        globals::elements[element].ions[ion].levels[level].uniquelevelindex = uniquelevelindex;
        globals::elements[element].ions[ion].levels[level].nphixstargets = 0;
        globals::elements[element].ions[ion].levels[level].phixstargets = NULL;
        globals::elements[element].ions[ion].levels[level].photoion_xs = NULL;
        uniquelevelindex++;
        totaldowntrans += get_ndowntrans(element, ion, level);
        totaluptrans += get_nuptrans(element, ion, level);
      }

      if (ion < nions - 1) {
        nbfcheck += globals::elements[element].ions[ion].ionisinglevels;  // nlevelsmax;
      }
      uniqueionindex++;
    }
  }
  fclose(adata);
  ftransitiondata.close();
  fclose(compositiondata);
  printout("nbfcheck %d\n", nbfcheck);

  /// Save the linecounters value to the global variable containing the number of lines
  globals::nlines = lineindex;
  printout("nlines %d\n", globals::nlines);
  if (globals::rank_in_node == 0) {
    assert_always(globals::nlines == static_cast<int>(temp_linelist.size()));
  }

  if (T_preset > 0) abort();

  /// Set up the list of allowed upward transitions for each level
  printout("total uptrans %d\n", totaluptrans);
  printout("total downtrans %d\n", totaldowntrans);

  printout("[info] mem_usage: transition lists occupy %.3f MB (this rank) and %.3f MB (shared on node)\n",
           2 * uniquelevelindex * sizeof(struct level_transition *) / 1024. / 1024.,
           (totaluptrans + totaldowntrans) * sizeof(struct level_transition) / 1024. / 1024.);

  /// then sort the linelist by decreasing frequency
  if (globals::rank_in_node == 0) {
    qsort(temp_linelist.data(), globals::nlines, sizeof(linelist_entry), compare_linelistentry);
    // std::sort(globals::linelist, globals::linelist + globals::nlines);

    for (int i = 0; i < globals::nlines - 1; i++) {
      const double nu = temp_linelist[i].nu;
      const double nu_next = temp_linelist[i + 1].nu;
      if (fabs(nu_next - nu) < (1.e-10 * nu)) {
        auto a1 = &temp_linelist[i];
        auto a2 = &temp_linelist[i + 1];

        if ((a1->elementindex == a2->elementindex) && (a1->ionindex == a2->ionindex) &&
            (a1->lowerlevelindex == a2->lowerlevelindex) && (a1->upperlevelindex == a2->upperlevelindex)) {
          printout("Duplicate transition line? %s\n", a1->nu == a2->nu ? "nu match exact" : "close to nu match");
          printout("a: Z=%d ionstage %d lower %d upper %d nu %g lambda %g\n", get_element(a1->elementindex),
                   get_ionstage(a1->elementindex, a1->ionindex), a1->lowerlevelindex, a1->upperlevelindex, a1->nu,
                   1e8 * CLIGHT / a1->nu);
          printout("b: Z=%d ionstage %d lower %d upper %d nu %g lambda %g\n", get_element(a2->elementindex),
                   get_ionstage(a2->elementindex, a2->ionindex), a2->lowerlevelindex, a2->upperlevelindex, a2->nu,
                   1e8 * CLIGHT / a2->nu);
        }

        // clamp close lines to exact overlaps
        // temp_linelist[i + 1].nu = temp_linelist[i].nu;
      }
    }

    // printout("Preventing duplicate transition lines...\n");
    // int pass = 0;
    // int numduplicates = 0;
    // do {
    //   pass++;

    //   qsort(temp_linelist, globals::nlines, sizeof(linelist_entry), compare_linelistentry_simple);

    //   numduplicates = 0;
    //   for (int i = 0; i < globals::nlines - 1; i++) {
    //     const double nu = temp_linelist[i].nu;
    //     const double nu_next = temp_linelist[i + 1].nu;

    //     if (nu == nu_next) {
    //       numduplicates++;
    //       const double lambda_prev = 1e8 * CLIGHT / temp_linelist[i].nu;
    //       const double lambda_new = lambda_prev + 0.01 * ((i % 10) + 1);
    //       temp_linelist[i].nu = 1e8 * CLIGHT / lambda_new;
    //     }
    //   }

    //   printout("pass %d found %d duplicate atomic lines...resorting...\n", pass, numduplicates);
    // } while (numduplicates > 0);
  }

  // create a linelist shared on node and then copy data across, freeing the local copy
  struct linelist_entry *nonconstlinelist;
#ifdef MPI_ON
  MPI_Win win;

  int my_rank_lines = globals::nlines / globals::node_nprocs;
  // rank_in_node 0 gets any remainder
  if (globals::rank_in_node == 0) {
    my_rank_lines += globals::nlines - (my_rank_lines * globals::node_nprocs);
  }

  MPI_Aint size = my_rank_lines * sizeof(linelist_entry);
  int disp_unit = sizeof(linelist_entry);
  MPI_Win_allocate_shared(size, disp_unit, MPI_INFO_NULL, globals::mpi_comm_node, &nonconstlinelist, &win);

  MPI_Win_shared_query(win, 0, &size, &disp_unit, &nonconstlinelist);
#else
  nonconstlinelist = static_cast<struct linelist_entry *>(malloc(globals::nlines * sizeof(linelist_entry)));
#endif

  if (globals::rank_in_node == 0) {
    memcpy(static_cast<void *>(nonconstlinelist), temp_linelist.data(), globals::nlines * sizeof(linelist_entry));
    temp_linelist.clear();
  }

#ifdef MPI_ON
  MPI_Barrier(MPI_COMM_WORLD);
#endif
  globals::linelist = nonconstlinelist;
  nonconstlinelist = nullptr;
  printout("[info] mem_usage: linelist occupies %.3f MB (node shared memory)\n",
           globals::nlines * sizeof(struct linelist_entry) / 1024. / 1024);

  /// Save sorted linelist into a file
  // if (rank_global == 0)
  // {
  //   FILE *linelist_file = fopen_required("linelist.dat", "w");
  //   fprintf(linelist_file,"%d\n",nlines);
  //   for (int i = 0; i < nlines; i++)
  //   {
  //     fprintf(linelist_file,"%d %d %d %d %d %lg %lg %lg %lg %d\n",
  //             i, globals::linelist[i].elementindex, globals::linelist[i].ionindex,
  //             globals::linelist[i].upperlevelindex, globals::linelist[i].lowerlevelindex,
  //             globals::linelist[i].nu, globals::linelist[i].einstein_A, globals::linelist[i].osc_strength,
  //             globals::linelist[i].coll_str, globals::linelist[i].forbidden);
  //   }
  //   fclose(linelist_file);
  // }

  /// Establish connection between transitions and sorted linelist
  // printout("[debug] init line counter list\n");
  printout("establish connection between transitions and sorted linelist...");
  time_t time_start_establish_linelist_connections = time(NULL);
  for (int lineindex = 0; lineindex < globals::nlines; lineindex++) {
    const int element = globals::linelist[lineindex].elementindex;
    const int ion = globals::linelist[lineindex].ionindex;
    const int lowerlevel = globals::linelist[lineindex].lowerlevelindex;
    const int upperlevel = globals::linelist[lineindex].upperlevelindex;

    const int nupperdowntrans = get_ndowntrans(element, ion, upperlevel);
    for (int ii = 0; ii < nupperdowntrans; ii++) {
      // negative indicates a level instead of a lineindex
      if (globals::elements[element].ions[ion].levels[upperlevel].downtrans[ii].lineindex == -lowerlevel) {
        globals::elements[element].ions[ion].levels[upperlevel].downtrans[ii].lineindex = lineindex;
        break;  // should be safe to end here if there is max. one transition per pair of levels
      }
    }

    const int nloweruptrans = get_nuptrans(element, ion, lowerlevel);
    for (int ii = 0; ii < nloweruptrans; ii++) {
      // negative indicates a level instead of a lineindex
      if (globals::elements[element].ions[ion].levels[lowerlevel].uptrans[ii].lineindex == -upperlevel) {
        globals::elements[element].ions[ion].levels[lowerlevel].uptrans[ii].lineindex = lineindex;
        break;  // should be safe to end here if there is max. one transition per pair of levels
      }
    }
  }
  printout("took %ds\n", time(NULL) - time_start_establish_linelist_connections);

  for (int element = 0; element < get_nelements(); element++) {
    const int nions = get_nions(element);
    for (int ion = 0; ion < nions; ion++) {
      if (globals::elements[element].ions[ion].nlevels_groundterm <= 0) {
        if (single_ground_level) {
          globals::elements[element].ions[ion].nlevels_groundterm = 1;
        } else {
          globals::elements[element].ions[ion].nlevels_groundterm = calculate_nlevels_groundterm(element, ion);
        }
      }
    }
  }

  /// Photoionisation cross-sections
  ///======================================================
  /// finally read in photoionisation cross sections and store them to the atomic data structure
  const bool phixs_v1_exists = std::ifstream(phixsdata_filenames[1]).good();
  const bool phixs_v2_exists = std::ifstream(phixsdata_filenames[2]).good();
  assert_always(phixs_v1_exists ^ phixs_v2_exists);  // XOR: one of the the two files must exist but not both

  phixs_file_version = phixs_v2_exists ? 2 : 1;

  read_phixs_data(phixs_file_version);

  int cont_index = -1;
  for (int element = 0; element < get_nelements(); element++) {
    const int nions = get_nions(element);
    for (int ion = 0; ion < nions; ion++) {
      for (int level = 0; level < get_ionisinglevels(element, ion); level++) {
        globals::elements[element].ions[ion].levels[level].cont_index = cont_index;
        cont_index -= get_nphixstargets(element, ion, level);
      }

      // below is just an extra warning consistency check
      const int nlevels_groundterm = globals::elements[element].ions[ion].nlevels_groundterm;

      // all levels in the ground term should be photoionisation targets from the lower ground state
      if (ion > 0 && ion < get_nions(element) - 1) {
        const int nphixstargets = get_nphixstargets(element, ion - 1, 0);
        if (nphixstargets > 0 && get_phixsupperlevel(element, ion - 1, 0, 0) == 0) {
          const int phixstargetlevels = get_phixsupperlevel(element, ion - 1, 0, nphixstargets - 1) + 1;

          if (nlevels_groundterm != phixstargetlevels) {
            printout("WARNING: Z=%d ion_stage %d nlevels_groundterm %d phixstargetlevels(ion-1) %d.\n",
                     get_element(element), get_ionstage(element, ion), nlevels_groundterm, phixstargetlevels);
            // if (nlevels_groundterm < phixstargetlevels)
            // {
            //   printout("  -> setting to %d\n", phixstargetlevels);
            //   globals::elements[element].ions[ion].nlevels_groundterm = phixstargetlevels;
            // }
          }
        }
      }
    }
  }

  printout("cont_index %d\n", cont_index);
}

#if (!NO_LUT_PHOTOION || !NO_LUT_BFHEATING)
static int search_groundphixslist(double nu_edge, int *index_in_groundlevelcontestimator, int el, int in, int ll)
/// Return the closest ground level continuum index to the given edge
/// frequency. If the given edge frequency is redder than the reddest
/// continuum return -1.
/// NB: groundphixslist must be in ascending order.
{
  int index;

  if (nu_edge < globals::groundcont[0].nu_edge) {
    index = -1;
    *index_in_groundlevelcontestimator = -1;
  } else {
    int i;
    int element;
    int ion;
    for (i = 1; i < globals::nbfcontinua_ground; i++) {
      if (nu_edge < globals::groundcont[i].nu_edge) break;
    }
    /*    if (i == nbfcontinua_ground)
        {
          printout("[fatal] search_groundphixslist: i %d, nu_edge %g, globals::groundcont[i-1].nu_edge %g ...
       abort\n",i,nu_edge,globals::groundcont[i-1].nu_edge); printout("[fatal] search_groundphixslist: this is
       element %d, ion %d, level %d in groundphixslist at i-1\n",el,in,ll);
          //printout("[fatal] search_groundphixslist: this is element %d, ion %d, level %d in groundphixslist at
       i-1\n",globals::groundcont[i-1].element,globals::groundcont[i-1].ion,groundphixslist[i-1].level); abort();
        }*/
    if (i == globals::nbfcontinua_ground) {
      element = globals::groundcont[i - 1].element;
      ion = globals::groundcont[i - 1].ion;
      int level = globals::groundcont[i - 1].level;
      if (element == el && ion == in && level == ll) {
        index = i - 1;
      } else {
        printout(
            "[fatal] search_groundphixslist: element %d, ion %d, level %d has edge_frequency %g equal to the "
            "bluest ground-level continuum\n",
            el, in, ll, nu_edge);
        printout(
            "[fatal] search_groundphixslist: bluest ground level continuum is element %d, ion %d, level %d at "
            "nu_edge %g\n",
            element, ion, level, globals::groundcont[i - 1].nu_edge);
        printout("[fatal] search_groundphixslist: i %d, nbfcontinua_ground %d\n", i, globals::nbfcontinua_ground);
        printout(
            "[fatal] This shouldn't happen, is hoewever possible if there are multiple levels in the adata file at "
            "energy=0\n");
        for (int looplevels = 0; looplevels < get_nlevels(el, in); looplevels++) {
          printout("[fatal]   element %d, ion %d, level %d, energy %g\n", el, in, looplevels,
                   epsilon(el, in, looplevels));
        }
        printout("[fatal] Abort omitted ... MAKE SURE ATOMIC DATA ARE CONSISTENT\n");
        index = i - 1;
        // abort();
      }
    } else {
      const double left_diff = nu_edge - globals::groundcont[i - 1].nu_edge;
      const double right_diff = globals::groundcont[i].nu_edge - nu_edge;
      index = (left_diff <= right_diff) ? i - 1 : i;
      element = globals::groundcont[index].element;
      ion = globals::groundcont[index].ion;
    }
    *index_in_groundlevelcontestimator = element * get_max_nions() + ion;
  }

  return index;
}
#endif

static void setup_cellhistory(void) {
  /// SET UP THE CELL HISTORY
  ///======================================================
  /// Stack which holds information about population and other cell specific data
  /// ===> move to update_packets
  globals::cellhistory = static_cast<struct cellhistory *>(malloc(get_max_threads() * sizeof(struct cellhistory)));
  assert_always(globals::cellhistory != NULL);

#ifdef _OPENMP
#pragma omp parallel
  {
#endif
    long mem_usage_cellhistory = 0;
    mem_usage_cellhistory += sizeof(struct cellhistory);

    printout("[info] input: initializing cellhistory for thread %d ...\n", tid);

    globals::cellhistory[tid].cellnumber = -99;

    mem_usage_cellhistory += globals::ncoolingterms * sizeof(double);
    globals::cellhistory[tid].cooling_contrib = static_cast<double *>(calloc(globals::ncoolingterms, sizeof(double)));

    for (int element = 0; element < get_nelements(); element++) {
      for (int ion = 0; ion < get_nions(element); ion++) {
        globals::cellhistory[tid].cooling_contrib[kpkt::get_coolinglistoffset(element, ion)] = COOLING_UNDEFINED;
      }
    }

    printout("[info] mem_usage: cellhistory coolinglist contribs for thread %d occupies %.3f MB\n", tid,
             globals::ncoolingterms * sizeof(double) / 1024. / 1024.);

    mem_usage_cellhistory += get_nelements() * sizeof(struct chelements);
    if ((globals::cellhistory[tid].chelements =
             static_cast<struct chelements *>(malloc(get_nelements() * sizeof(struct chelements)))) == NULL) {
      printout("[fatal] input: not enough memory to initialize cellhistory's elementlist ... abort\n");
      abort();
    }

    long chlevelblocksize = 0;
    long chphixsblocksize = 0;
    int chtransblocksize = 0;
    for (int element = 0; element < get_nelements(); element++) {
      const int nions = get_nions(element);
      for (int ion = 0; ion < nions; ion++) {
        const int nlevels = get_nlevels(element, ion);
        chlevelblocksize += nlevels * sizeof(struct chlevels);

        for (int level = 0; level < nlevels; level++) {
          const int nphixstargets = get_nphixstargets(element, ion, level);
          chphixsblocksize += nphixstargets * sizeof(struct chphixstargets);

          const int ndowntrans = get_ndowntrans(element, ion, level);
          const int nuptrans = get_nuptrans(element, ion, level);
          chtransblocksize += (2 * ndowntrans + nuptrans);
        }
      }
    }
    assert_always(chlevelblocksize > 0);
    globals::cellhistory[tid].ch_all_levels = static_cast<struct chlevels *>(malloc(chlevelblocksize));
    struct chphixstargets *chphixstargetsblock =
        chphixsblocksize > 0 ? static_cast<struct chphixstargets *>(malloc(chphixsblocksize)) : nullptr;
    mem_usage_cellhistory += chlevelblocksize + chphixsblocksize;

    mem_usage_cellhistory += chtransblocksize * sizeof(double);
    double *const chtransblock =
        chtransblocksize > 0 ? static_cast<double *>(malloc(chtransblocksize * sizeof(double))) : nullptr;

    int alllevelindex = 0;
    int allphixstargetindex = 0;
    int chtransindex = 0;
    for (int element = 0; element < get_nelements(); element++) {
      const int nions = get_nions(element);
      mem_usage_cellhistory += nions * sizeof(struct chions);
      globals::cellhistory[tid].chelements[element].chions =
          static_cast<struct chions *>(malloc(nions * sizeof(struct chions)));
      assert_always(globals::cellhistory[tid].chelements[element].chions != NULL);

      for (int ion = 0; ion < nions; ion++) {
        const int nlevels = get_nlevels(element, ion);
        globals::cellhistory[tid].chelements[element].chions[ion].chlevels =
            &globals::cellhistory[tid].ch_all_levels[alllevelindex];

        assert_always(alllevelindex == get_uniquelevelindex(element, ion, 0));
        alllevelindex += nlevels;

        for (int level = 0; level < nlevels; level++) {
          struct chlevels *chlevel = &globals::cellhistory[tid].chelements[element].chions[ion].chlevels[level];
          const int nphixstargets = get_nphixstargets(element, ion, level);
          chlevel->chphixstargets = chphixsblocksize > 0 ? &chphixstargetsblock[allphixstargetindex] : nullptr;
          allphixstargetindex += nphixstargets;
        }

        for (int level = 0; level < nlevels; level++) {
          struct chlevels *chlevel = &globals::cellhistory[tid].chelements[element].chions[ion].chlevels[level];
          const int ndowntrans = get_ndowntrans(element, ion, level);

          chlevel->individ_rad_deexc = &chtransblock[chtransindex];
          chtransindex += ndowntrans;
        }

        for (int level = 0; level < nlevels; level++) {
          struct chlevels *chlevel = &globals::cellhistory[tid].chelements[element].chions[ion].chlevels[level];
          const int ndowntrans = get_ndowntrans(element, ion, level);
          chlevel->individ_internal_down_same = &chtransblock[chtransindex];
          chtransindex += ndowntrans;
        }

        for (int level = 0; level < nlevels; level++) {
          struct chlevels *chlevel = &globals::cellhistory[tid].chelements[element].chions[ion].chlevels[level];
          const int nuptrans = get_nuptrans(element, ion, level);
          chlevel->individ_internal_up_same = &chtransblock[chtransindex];
          chtransindex += nuptrans;
        }
      }
    }
    assert_always(chtransindex == chtransblocksize);

    assert_always(globals::nbfcontinua >= 0);
    globals::cellhistory[tid].ch_allcont_departureratios =
        static_cast<double *>(malloc(globals::nbfcontinua * sizeof(double)));
    mem_usage_cellhistory += globals::nbfcontinua * sizeof(double);

    printout("[info] mem_usage: cellhistory for thread %d occupies %.3f MB\n", tid,
             mem_usage_cellhistory / 1024. / 1024.);
#ifdef _OPENMP
  }
#endif
}

static void write_bflist_file(int includedphotoiontransitions) {
  globals::bflist = static_cast<struct bflist_t *>(malloc(includedphotoiontransitions * sizeof(struct bflist_t)));
  assert_always(globals::bflist != NULL);

  FILE *bflist_file = NULL;
  if (globals::rank_global == 0) {
    bflist_file = fopen_required("bflist.dat", "w");
    fprintf(bflist_file, "%d\n", includedphotoiontransitions);
  }
  int i = 0;
  for (int element = 0; element < get_nelements(); element++) {
    const int nions = get_nions(element);
    for (int ion = 0; ion < nions; ion++) {
      const int nlevels = get_ionisinglevels(element, ion);
      for (int level = 0; level < nlevels; level++) {
        for (int phixstargetindex = 0; phixstargetindex < get_nphixstargets(element, ion, level); phixstargetindex++) {
          const int upperionlevel = get_phixsupperlevel(element, ion, level, phixstargetindex);
          globals::bflist[i].elementindex = element;
          globals::bflist[i].ionindex = ion;
          globals::bflist[i].levelindex = level;
          globals::bflist[i].phixstargetindex = phixstargetindex;

          if (globals::rank_global == 0) {
            fprintf(bflist_file, "%d %d %d %d %d\n", i, element, ion, level, upperionlevel);
          }

          assert_always(-1 - i == get_continuumindex(element, ion, level, upperionlevel));

          assert_always(
              i !=
              9999999 - 1);  // would cause the same packet emission type as the special value for free-free scattering
          i++;
        }
      }
    }
  }
  assert_always(i == includedphotoiontransitions);
  if (globals::rank_global == 0) {
    fclose(bflist_file);
  }
}

static bool operator<(const fullphixslist &a, const fullphixslist &b)
/// Helper function to sort the phixslist by ascending threshold frequency.
{
  return a.nu_edge < b.nu_edge;
}

static bool operator<(const groundphixslist &a, const groundphixslist &b)
/// Helper function to sort the groundphixslist by ascending threshold frequency.
{
  return a.nu_edge < b.nu_edge;
}

static void setup_phixs_list(void) {
  // set up the photoionisation transition lists
  // and temporary gamma/kappa lists for each thread

  printout("[info] read_atomicdata: number of bfcontinua %d\n", globals::nbfcontinua);
  printout("[info] read_atomicdata: number of ground-level bfcontinua %d\n", globals::nbfcontinua_ground);

  globals::phixslist = static_cast<struct phixslist *>(malloc(get_max_threads() * sizeof(struct phixslist)));
  assert_always(globals::phixslist != NULL);

  /// MK: 2012-01-19
  /// To fix the OpenMP problem on BlueGene machines this parallel section was removed and replaced by
  /// a serial loop which intializes the phixslist data structure for all threads in a loop. I'm still
  /// not sure why this causes a problem at all and on BlueGene architectures in particular. However,
  /// it seems to fix the problem.
  // #ifdef _OPENMP
  //   #pragma omp parallel private(i,element,ion,level,nions,nlevels,epsilon_upper,E_threshold,nu_edge)
  //   {
  // #endif
  for (int itid = 0; itid < get_max_threads(); itid++) {
    /// Number of ground level bf-continua equals the total number of included ions minus the number
    /// of included elements, because the uppermost ionisation stages can't ionise.
#if (!NO_LUT_PHOTOION || !NO_LUT_BFHEATING)
    globals::phixslist[itid].groundcont_gamma_contr =
        static_cast<double *>(malloc(globals::nbfcontinua_ground * sizeof(double)));
    assert_always(globals::phixslist[itid].groundcont_gamma_contr != NULL)

        for (int groundcontindex = 0; groundcontindex < globals::nbfcontinua_ground; groundcontindex++) {
      globals::phixslist[tid].groundcont_gamma_contr[groundcontindex] = 0.;
    }
#endif

    globals::phixslist[itid].kappa_bf_sum = static_cast<double *>(malloc(globals::nbfcontinua * sizeof(double)));
    assert_always(globals::phixslist[itid].kappa_bf_sum != NULL);

#if (DETAILED_BF_ESTIMATORS_ON)
    globals::phixslist[itid].gamma_contr = static_cast<double *>(malloc(globals::nbfcontinua * sizeof(double)));
    assert_always(globals::phixslist[itid].gamma_contr != NULL);
#endif

    for (int allcontindex = 0; allcontindex < globals::nbfcontinua; allcontindex++) {
      globals::phixslist[itid].kappa_bf_sum[allcontindex] = 0.;

#if (DETAILED_BF_ESTIMATORS_ON)
      globals::phixslist[itid].gamma_contr[allcontindex] = 0.;
#endif
    }

    printout("[info] mem_usage: phixslist[tid].kappa_bf_contr for thread %d occupies %.3f MB\n", itid,
             globals::nbfcontinua * sizeof(double) / 1024. / 1024.);
  }

#if (!NO_LUT_PHOTOION || !NO_LUT_BFHEATING)
  globals::groundcont =
      static_cast<struct groundphixslist *>(malloc(globals::nbfcontinua_ground * sizeof(struct groundphixslist)));
  assert_always(globals::groundcont != NULL);
#endif

#if (!NO_LUT_PHOTOION || !NO_LUT_BFHEATING)
  int groundcontindex = 0;
  for (int element = 0; element < get_nelements(); element++) {
    const int nions = get_nions(element);
    for (int ion = 0; ion < nions - 1; ion++) {
      const int nlevels_groundterm = get_nlevels_groundterm(element, ion);
      for (int level = 0; level < nlevels_groundterm; level++) {
        const int nphixstargets = get_nphixstargets(element, ion, level);
        for (int phixstargetindex = 0; phixstargetindex < nphixstargets; phixstargetindex++) {
          // const int upperlevel = get_phixsupperlevel(element, ion,level, 0);
          // const double E_threshold = epsilon(element, ion + 1, upperlevel) - epsilon(element, ion, level);
          const double E_threshold = get_phixs_threshold(element, ion, level, phixstargetindex);
          const double nu_edge = E_threshold / H;
          assert_always(groundcontindex < globals::nbfcontinua_ground);
          globals::groundcont[groundcontindex].element = element;
          globals::groundcont[groundcontindex].ion = ion;
          globals::groundcont[groundcontindex].level = level;
          globals::groundcont[groundcontindex].nu_edge = nu_edge;
          globals::groundcont[groundcontindex].phixstargetindex = phixstargetindex;
          groundcontindex++;
        }
      }
    }
  }
  assert_always(groundcontindex == globals::nbfcontinua_ground);
  std::sort(globals::groundcont, globals::groundcont + globals::nbfcontinua_ground);
#endif

  struct fullphixslist *nonconstallcont =
      static_cast<struct fullphixslist *>(malloc(globals::nbfcontinua * sizeof(struct fullphixslist)));
  printout("[info] mem_usage: photoionisation list occupies %.3f MB\n",
           globals::nbfcontinua * (sizeof(fullphixslist)) / 1024. / 1024.);
  int nbftables = 0;
  int allcontindex = 0;
  for (int element = 0; element < get_nelements(); element++) {
    const int nions = get_nions(element);
    for (int ion = 0; ion < nions - 1; ion++) {
      const int nlevels = get_ionisinglevels(element, ion);
      for (int level = 0; level < nlevels; level++) {
        const int nphixstargets = get_nphixstargets(element, ion, level);

        if (nphixstargets > 0) nbftables++;

        for (int phixstargetindex = 0; phixstargetindex < nphixstargets; phixstargetindex++) {
          // const int upperlevel = get_phixsupperlevel(element, ion,level, 0);
          // const double E_threshold = epsilon(element, ion + 1, upperlevel) - epsilon(element, ion, level);
          const double E_threshold = get_phixs_threshold(element, ion, level, phixstargetindex);
          const double nu_edge = E_threshold / H;

          assert_always(allcontindex < globals::nbfcontinua);
          nonconstallcont[allcontindex].nu_edge = nu_edge;
          nonconstallcont[allcontindex].element = element;
          nonconstallcont[allcontindex].ion = ion;
          nonconstallcont[allcontindex].level = level;
          nonconstallcont[allcontindex].phixstargetindex = phixstargetindex;
          nonconstallcont[allcontindex].probability = get_phixsprobability(element, ion, level, phixstargetindex);
          nonconstallcont[allcontindex].upperlevel = get_phixsupperlevel(element, ion, level, phixstargetindex);

#if (!NO_LUT_PHOTOION || !NO_LUT_BFHEATING)
          int index_in_groundlevelcontestimator;
          nonconstallcont[allcontindex].index_in_groundphixslist =
              search_groundphixslist(nu_edge, &index_in_groundlevelcontestimator, element, ion, level);

          globals::elements[element].ions[ion].levels[level].closestgroundlevelcont = index_in_groundlevelcontestimator;
#endif
          allcontindex++;
        }
      }
    }
  }

  assert_always(allcontindex == globals::nbfcontinua);
  assert_always(globals::nbfcontinua >= 0);  // was initialised as -1 before startup

  if (globals::nbfcontinua > 0) {
    // indicies above were temporary only. continum index should be to the sorted list
    std::sort(nonconstallcont, nonconstallcont + globals::nbfcontinua);

    globals::allcont_nu_edge = static_cast<double *>(malloc(globals::nbfcontinua * sizeof(double)));

// copy the photoionisation tables into one contiguous block of memory
#ifdef MPI_ON
    float *allphixsblock;
    MPI_Win win_allphixsblock;
    MPI_Aint size = (globals::rank_in_node == 0) ? nbftables * globals::NPHIXSPOINTS * sizeof(float) : 0;
    int disp_unit = sizeof(linelist_entry);

    MPI_Win_allocate_shared(size, disp_unit, MPI_INFO_NULL, globals::mpi_comm_node, &allphixsblock, &win_allphixsblock);
    MPI_Win_shared_query(win_allphixsblock, MPI_PROC_NULL, &size, &disp_unit, &allphixsblock);

    MPI_Barrier(MPI_COMM_WORLD);
#else
    float *allphixsblock = static_cast<float *>(malloc(nbftables * globals::NPHIXSPOINTS * sizeof(float)));
#endif

    assert_always(allphixsblock != NULL);
    int nbftableschanged = 0;
    for (int i = 0; i < globals::nbfcontinua; i++) {
      globals::allcont_nu_edge[i] = nonconstallcont[i].nu_edge;

      const int element = nonconstallcont[i].element;
      const int ion = nonconstallcont[i].ion;
      const int level = nonconstallcont[i].level;
      const int phixstargetindex = nonconstallcont[i].phixstargetindex;

      // different targets share the same cross section table, so don't repeat this process
      if (phixstargetindex == 0) {
        if (globals::rank_in_node == 0) {
          memcpy(allphixsblock, globals::elements[element].ions[ion].levels[level].photoion_xs,
                 globals::NPHIXSPOINTS * sizeof(float));
        }

        free(globals::elements[element].ions[ion].levels[level].photoion_xs);
        globals::elements[element].ions[ion].levels[level].photoion_xs = allphixsblock;

        allphixsblock += globals::NPHIXSPOINTS;
        nbftableschanged++;
      }
    }
    assert_always(nbftableschanged == nbftables);
#ifdef MPI_ON
    MPI_Barrier(MPI_COMM_WORLD);
#endif
    for (int i = 0; i < globals::nbfcontinua; i++) {
      const int element = nonconstallcont[i].element;
      const int ion = nonconstallcont[i].ion;
      const int level = nonconstallcont[i].level;
      nonconstallcont[i].photoion_xs = globals::elements[element].ions[ion].levels[level].photoion_xs;
    }
  }
  globals::allcont = nonconstallcont;
  nonconstallcont = nullptr;

  long mem_usage_photoionluts = 2 * TABLESIZE * globals::nbfcontinua * sizeof(double);
  globals::spontrecombcoeff = static_cast<double *>(malloc(TABLESIZE * globals::nbfcontinua * sizeof(double)));
  assert_always(globals::spontrecombcoeff != NULL);

#if (!NO_LUT_PHOTOION)
  globals::corrphotoioncoeff = static_cast<double *>(malloc(TABLESIZE * globals::nbfcontinua * sizeof(double)));
  assert_always(globals::corrphotoioncoeff != NULL);
  mem_usage_photoionluts += TABLESIZE * globals::nbfcontinua * sizeof(double);
#endif
#if (!NO_LUT_BFHEATING)
  globals::bfheating_coeff = static_cast<double *>(malloc(TABLESIZE * globals::nbfcontinua * sizeof(double)));
  assert_always(globals::bfheating_coeff != NULL);
  mem_usage_photoionluts += TABLESIZE * globals::nbfcontinua * sizeof(double);
#endif

  globals::bfcooling_coeff = static_cast<double *>(malloc(TABLESIZE * globals::nbfcontinua * sizeof(double)));
  assert_always(globals::bfcooling_coeff != NULL);

  printout(
      "[info] mem_usage: lookup tables derived from photoionisation (spontrecombcoeff, bfcooling and "
      "corrphotoioncoeff/bfheating if enabled) occupy %.3f MB\n",
      mem_usage_photoionluts / 1024. / 1024.);
}

static void read_atomicdata(void)
/// Subroutine to read in input parameters.
{
  read_atomicdata_files();

  printout("included ions %d\n", get_includedions());

/// INITIALISE THE ABSORPTION/EMISSION COUNTERS ARRAYS
///======================================================
#ifdef RECORD_LINESTAT
  globals::ecounter = static_cast<int *>(malloc(globals::nlines * sizeof(int)));
  assert_always(globals::ecounter != NULL);

  globals::acounter = static_cast<int *>(malloc(globals::nlines * sizeof(int)));
  assert_always(globals::acounter != NULL);

  globals::linestat_reduced = static_cast<int *>(malloc(globals::nlines * sizeof(int)));
  assert_always(globals::linestat_reduced != NULL);
#endif

  kpkt::setup_coolinglist();

  setup_cellhistory();

  /// Printout some information about the read-in model atom

  int includedlevels = 0;
  int includedionisinglevels = 0;
  int includedphotoiontransitions = 0;
  printout("[input.c] this simulation contains\n");
  printout("----------------------------------\n");
  for (int element = 0; element < get_nelements(); element++) {
    printout("[input.c]   element %d (Z=%2d)\n", element, get_element(element));
    const int nions = get_nions(element);
    for (int ion = 0; ion < nions; ion++) {
      int photoiontransitions = 0;
      for (int level = 0; level < get_nlevels(element, ion); level++)
        photoiontransitions += get_nphixstargets(element, ion, level);
      printout(
          "[input.c]     ion_stage %d with %4d levels (%d in groundterm, %4d ionising) and %6d photoionisation "
          "transitions (epsilon_ground %7.2f eV)\n",
          get_ionstage(element, ion), get_nlevels(element, ion), get_nlevels_groundterm(element, ion),
          get_ionisinglevels(element, ion), photoiontransitions, epsilon(element, ion, 0) / EV);
      includedlevels += get_nlevels(element, ion);
      includedionisinglevels += get_ionisinglevels(element, ion);
      includedphotoiontransitions += photoiontransitions;
    }
  }
  assert_always(includedphotoiontransitions == globals::nbfcontinua);

  printout("[input.c]   in total %d ions, %d levels (%d ionising), %d lines, %d photoionisation transitions\n",
           get_includedions(), includedlevels, includedionisinglevels, globals::nlines, globals::nbfcontinua);

  write_bflist_file(globals::nbfcontinua);

  setup_phixs_list();

  /// set-up/gather information for nlte stuff

  globals::total_nlte_levels = 0;
  int n_super_levels = 0;

  if (NLTE_POPS_ON) {
    for (int element = 0; element < get_nelements(); element++) {
      const int nions = get_nions(element);
      for (int ion = 0; ion < nions; ion++) {
        globals::elements[element].ions[ion].first_nlte = globals::total_nlte_levels;
        const int nlevels = get_nlevels(element, ion);
        int fullnlteexcitedlevelcount = 0;
        for (int level = 1; level < nlevels; level++) {
          if (is_nlte(element, ion, level)) {
            fullnlteexcitedlevelcount++;
            globals::total_nlte_levels++;
          }
        }

        const bool has_superlevel = (nlevels > (fullnlteexcitedlevelcount + 1));
        if (has_superlevel) {
          // If there are more levels that the ground state + the number of NLTE levels then we need an extra
          // slot to store data for the "superlevel", which is a representation of all the other levels that
          // are not treated in detail.
          globals::total_nlte_levels++;
          n_super_levels++;
        }

        globals::elements[element].ions[ion].nlevels_nlte = fullnlteexcitedlevelcount;

        assert_always(has_superlevel == ion_has_superlevel(element, ion));

        printout("[input.c]  element %2d Z=%2d ion_stage %2d has %5d NLTE excited levels%s. Starting at %d\n", element,
                 get_element(element), get_ionstage(element, ion), fullnlteexcitedlevelcount,
                 has_superlevel ? " plus a superlevel" : "", globals::elements[element].ions[ion].first_nlte);
      }
    }
  }

  printout("[input.c] Total NLTE levels: %d, of which %d are superlevels\n", globals::total_nlte_levels,
           n_super_levels);
}

void input(int rank)
/// To govern the input. For now hardwire everything.
{
  globals::homogeneous_abundances = false;

  globals::npkts = MPKTS;
  /*  #ifdef FORCE_LTE
      n_titer = 1;
    #else
      n_titer = 6;
    #endif*/
  globals::n_titer = 1;
  globals::initial_iteration = false;

  printout("[info] input: do n_titer %d iterations per timestep\n", globals::n_titer);
  if (globals::n_titer > 1) {
#ifndef DO_TITER
    printout("[fatal] input: n_titer > 1, but DO_TITER not defined ... abort\n");
    abort();
#endif
  } else if (globals::n_titer == 1) {
#ifdef DO_TITER
    printout("[warning] input: n_titer = 1 but DO_TITER defined, remove DO_TITER to save memory\n");
#endif
  } else {
    printout("[fatal] input: no valid value for n_titer selected\n");
    abort();
  }

  globals::nu_min_r = NU_MIN_R;  /// lower frequency boundary for UVOIR spectra and BB sampling
  globals::nu_max_r = NU_MAX_R;  /// upper frequency boundary for UVOIR spectra and BB sampling

  /// Lightcurve setting
  globals::do_r_lc = false;  /// default to no lc = gamma-ray spectrum
  globals::do_rlc_est = 0;   /// ^^

  globals::nfake_gam = 1;  /// # of fake gamma ray lines for syn

  /// Read in parameters from input.txt
  read_parameterfile(rank);

/// Read in parameters from vpkt.txt
#ifdef VPKT_ON
  read_parameterfile_vpkt();
#endif

  read_atomicdata();

#ifdef MPI_ON
  const time_t time_before_barrier = time(NULL);
  printout("barrier after read_atomicdata(): time before barrier %d, ", (int)time_before_barrier);
  MPI_Barrier(MPI_COMM_WORLD);
  printout("time after barrier %d (waited %d seconds)\n", (int)time(NULL), (int)(time(NULL) - time_before_barrier));
#endif

  grid::read_ejecta_model();

  /// Now that the list exists use it to find values for spectral synthesis
  /// stuff.
  const int lindex_max = gammapkt::get_nul(globals::nusyn_max);
  const int lindex_min = gammapkt::get_nul(globals::nusyn_min);
  printout("lindex_max %d, lindex_min %d\n", lindex_max, lindex_min);

  globals::emiss_offset = lindex_min;
  globals::emiss_max = lindex_max - lindex_min + 1;
  printout("emiss_max using %d of a possible %d\n", globals::emiss_max, EMISS_MAX);

  if (globals::emiss_max > EMISS_MAX) {
    printout("Too many points needed for emissivities. Use smaller frequency range or increase EMISS_MAX. Abort.\n");
    abort();
  }
}

static bool getline(std::istream &input, std::string &line)
// return true if line read, false if not (EOF)
{
  return !(!std::getline(input, line));
}

bool get_noncommentline(std::istream &input, std::string &line)
// read the next line, skipping any comment lines beginning with '#'
{
  while (true) {
    bool linefound = getline(input, line);
    // printout("LINE: >%s<  linefound: %s commentonly: %s \n", line.c_str(), linefound ? "true" : "false",
    // lineiscommentonly(line) ? "true" : "false");
    if (!linefound) {
      return false;
    } else if (!lineiscommentonly(line)) {
      return true;
    }
  }
}

#if CUDA_ENABLED
__global__ static void kernel_setupcurand(unsigned long int pre_zseed, int rank) {
  const int tid = threadIdx.x + blockDim.x * blockIdx.x;
  if (tid < MCUDATHREADS) {
    unsigned long int zseed = pre_zseed + (13 * rank);
    curand_init(zseed, tid, 0, &globals::curandstates[tid]);
    // const double zrand = curand_uniform_double(&curandstates[tid]);
    // printf("kernel_setupcurand tidx %d %g\n", tid, zrand);
  }
}

static void init_curand(unsigned long int pre_zseed, int rank) {
  dim3 threadsPerBlock(256, 1, 1);
  dim3 numBlocks((MCUDATHREADS + threadsPerBlock.x - 1) / threadsPerBlock.x, 1, 1);

  kernel_setupcurand<<<numBlocks, threadsPerBlock>>>(pre_zseed, rank);

  // Check for any errors launching the kernel
  checkCudaErrors(cudaGetLastError());

  // cudaDeviceSynchronize waits for the kernel to finish, and returns any errors encountered during the launch.
  checkCudaErrors(cudaDeviceSynchronize());
}
#endif

#ifndef __CUDA_ARCH__
void read_parameterfile(int rank)
/// Subroutine to read in input parameters from input.txt.
{
  unsigned long int pre_zseed;

  std::ifstream file("input.txt");
  assert_always(file.is_open());

  std::string line;

  assert_always(get_noncommentline(file, line));

  int dum1;
  std::stringstream(line) >> dum1;

  if (dum1 > 0) {
    pre_zseed = dum1;  // random number seed
    printout("[debug] using specified random number seed of %lu\n", pre_zseed);
  } else {
    pre_zseed = time(NULL);
    printout("[debug] randomly-generated random number seed is %lu\n", pre_zseed);
  }

#if CUDA_ENABLED
  init_curand(pre_zseed, rank);
#endif

#ifdef _OPENMP
#pragma omp parallel
  {
#endif
    /// For MPI parallelisation, the random seed is changed based on the rank of the process
    /// For OpenMP parallelisation rng is a threadprivate variable and the seed changed according
    /// to the thread-ID tid.
    unsigned long int zseed = pre_zseed + (13 * rank) + (17 * tid); /* rnum generator seed */
    printout("rank %d: thread %d has zseed %lu\n", rank, tid, zseed);
    /// start by setting up the randon number generator
    rng = gsl_rng_alloc(gsl_rng_ran3);
    gsl_rng_set(rng, zseed);
    /// call it a few times to get it in motion.
    for (int n = 0; n < 100; n++) {
      gsl_rng_uniform(rng);
    }
    printout("rng is a '%s' generator\n", gsl_rng_name(rng));
#ifdef _OPENMP
  }
#endif

  assert_always(get_noncommentline(file, line));
  std::stringstream(line) >> globals::ntstep;  // number of time steps
  assert_always(globals::ntstep > 0);

  assert_always(get_noncommentline(file, line));
  // printout("line %s\n", line.c_str());
  std::stringstream(line) >> globals::itstep >> globals::ftstep;  // number of start and end time step
  printout("input: itstep %d ftstep %d\n", globals::itstep, globals::ftstep);
  assert_always(globals::itstep < globals::ntstep);
  assert_always(globals::itstep <= globals::ftstep);
  assert_always(globals::ftstep <= globals::ntstep);

  double tmin_days = 0.;
  double tmax_days = 0.;
  assert_always(get_noncommentline(file, line));
  std::stringstream(line) >> tmin_days >> tmax_days;  // start and end times
  assert_always(tmin_days > 0);
  assert_always(tmax_days > 0);
  assert_always(tmin_days < tmax_days);
  globals::tmin = tmin_days * DAY;
  globals::tmax = tmax_days * DAY;

  float dum2 = 0.;
  float dum3 = 0.;
  assert_always(get_noncommentline(file, line));
  std::stringstream(line) >> dum2 >> dum3;
  globals::nusyn_min = dum2 * MEV / H;  // lowest frequency to synthesise
  globals::nusyn_max = dum3 * MEV / H;  // highest frequency to synthesise

  assert_always(get_noncommentline(file, line));
  std::stringstream(line) >> globals::nsyn_time;  // number of times for synthesis

  assert_always(get_noncommentline(file, line));
  std::stringstream(line) >> dum2 >> dum3;  // start and end times for synthesis
  for (int i = 0; i < globals::nsyn_time; i++) {
    globals::time_syn[i] = exp(log(dum2) + (dum3 * i)) * DAY;
  }

  assert_always(get_noncommentline(file, line));
  std::stringstream(line) >> dum1;  // model type
  if (dum1 == 1) {
    set_model_type(grid::RHO_1D_READ);
  } else if (dum1 == 2) {
    set_model_type(grid::RHO_2D_READ);
  } else if (dum1 == 3) {
    set_model_type(grid::RHO_3D_READ);
  }

  assert_always(get_noncommentline(file, line));
  std::stringstream(line) >> dum1;  // compute the r-light curve?
  // 1: lc no estimators
  // 2: lc case with thin cells
  // 3: lc case with thick cells
  // 4: gamma-ray heating case
  globals::do_r_lc = (dum1 != 0);
  if (dum1 > 0) {
    globals::do_rlc_est = dum1 - 1;
  }
  assert_always(dum1 >= 0);
  assert_always(dum1 <= 4);

  assert_always(get_noncommentline(file, line));
  int n_out_it;
  std::stringstream(line) >> n_out_it;  // UNUSED number of iterations

  assert_always(get_noncommentline(file, line));
  std::stringstream(line) >> dum2;        // change speed of light
  assert_always(fabs(dum2 - 1.) < 1e-3);  // no longer in use. Change CLIGHT_PROP constant instead

  assert_always(get_noncommentline(file, line));
  std::stringstream(line) >> globals::gamma_grey;  // use grey opacity for gammas?

  float syn_dir_in[3];
  assert_always(get_noncommentline(file, line));
  std::stringstream(line) >> syn_dir_in[0] >> syn_dir_in[1] >> syn_dir_in[2];  // components of syn_dir

  const double rr = (syn_dir_in[0] * syn_dir_in[0]) + (syn_dir_in[1] * syn_dir_in[1]) + (syn_dir_in[2] * syn_dir_in[2]);
  // ensure that this vector is normalised.
  if (rr > 1.e-6) {
    globals::syn_dir[0] = syn_dir_in[0] / sqrt(rr);
    globals::syn_dir[1] = syn_dir_in[1] / sqrt(rr);
    globals::syn_dir[2] = syn_dir_in[2] / sqrt(rr);
  } else {
    const double z1 = 1. - (2 * gsl_rng_uniform(rng));
    const double z2 = gsl_rng_uniform(rng) * 2.0 * PI;
    globals::syn_dir[2] = z1;
    globals::syn_dir[0] = sqrt((1. - (z1 * z1))) * cos(z2);
    globals::syn_dir[1] = sqrt((1. - (z1 * z1))) * sin(z2);
  }

  assert_always(get_noncommentline(file, line));
  std::stringstream(line) >> globals::opacity_case;  // opacity choice

  assert_always(get_noncommentline(file, line));
  std::stringstream(line) >> globals::rho_crit_para;  // free parameter for calculation of rho_crit
  printout("input: rho_crit_para %g\n", globals::rho_crit_para);
  /// he calculation of rho_crit itself depends on the time, therfore it happens in grid_init and update_grid

  assert_always(get_noncommentline(file, line));
  std::stringstream(line) >> globals::debug_packet;  // activate debug output for packet
  // select a negative value to deactivate

  // Do we start a new simulation or, continue another one?
  int continue_flag;
  assert_always(get_noncommentline(file, line));
  std::stringstream(line) >> continue_flag;
  globals::simulation_continued_from_saved = (continue_flag == 1);
  if (globals::simulation_continued_from_saved) {
    printout("input: resuming simulation from saved point\n");
  } else {
    printout("input: starting a new simulation\n");
    assert_always(globals::itstep == 0);
  }

  /// Wavelength (in Angstroms) at which the parameterisation of the radiation field
  /// switches from the nebular approximation to LTE.
  assert_always(get_noncommentline(file, line));
  std::stringstream(line) >> dum2;  // free parameter for calculation of rho_crit
  globals::nu_rfcut = CLIGHT / (dum2 * 1e-8);
  printout("input: nu_rfcut %g\n", globals::nu_rfcut);

  /// Sets the number of initial LTE timesteps for NLTE runs
  assert_always(get_noncommentline(file, line));
  std::stringstream(line) >> globals::num_lte_timesteps;
#ifdef FORCE_LTE
  printout("input: this is a pure LTE run\n");
#else
  printout("input: this is a NLTE run\n");
  printout("input: do the first %d timesteps in LTE\n", globals::num_lte_timesteps);
#endif

  if (NT_ON) {
    if (NT_SOLVE_SPENCERFANO)
      printout("input: Non-thermal ionisation with a Spencer-Fano solution is switched on for this run.\n");
    else
      printout("input: Non-thermal ionisation with the work function approximation is switched on for this run.\n");
#ifdef FORCE_LTE
    printout(
        "input: Non-thermal ionisation requires the code to run in non-LTE mode. Remove macro FORCE_LTE and "
        "recompile!\n");
    abort();
#endif
  } else
    printout("input: No non-thermal ionisation is used in this run.\n");

  if (NO_LUT_PHOTOION) {
    printout(
        "Corrphotoioncoeff is calculated from the radiation field at each timestep in each modelgrid cell (no "
        "LUT).\n");
  } else {
    printout(
        "Corrphotoioncoeff is calculated from LTE lookup tables (ratecoeff.dat) and corrphotoionrenorm estimator.\n");
  }

  if (NO_LUT_BFHEATING) {
    printout(
        "bfheating coefficients are calculated from the radiation field at each timestep in each modelgrid cell (no "
        "LUT).\n");
  } else {
    printout("bfheating coefficients are calculated from LTE lookup tables (ratecoeff.dat) and bfheatingestimator.\n");
  }

  /// Set up initial grey approximation?
  assert_always(get_noncommentline(file, line));
  std::stringstream(line) >> globals::cell_is_optically_thick >> globals::num_grey_timesteps;
  printout(
      "input: cells with Thomson optical depth > %g are treated in grey approximation for the first %d timesteps\n",
      globals::cell_is_optically_thick, globals::num_grey_timesteps);

  /// Limit the number of bf-continua
  assert_always(get_noncommentline(file, line));
  std::stringstream(line) >> globals::max_bf_continua;
  if (globals::max_bf_continua == -1) {
    printout("input: use all bf-continua\n");
    globals::max_bf_continua = 1e6;
  } else {
    printout("input: use only %d bf-continua per ion\n", globals::max_bf_continua);
  }

  /// The following parameters affect the do_exspec mode only /////////////////
  /// Read number of MPI tasks for exspec
  assert_always(get_noncommentline(file, line));
  std::stringstream(line) >> globals::nprocs_exspec;
  if (do_exspec) {
    printout("input: do_exspec ... extract spectra for %d MPI tasks\n", globals::nprocs_exspec);
    printout("input: do_exspec ... and %d packets per task\n", globals::npkts);
  }

  /// Extract line-of-sight dependent information of last emission for spectrum_res
  ///   if 1, yes
  ///   if 0, no
  /// Only affects runs with do_exspec. But then it needs huge amounts of memory!!!
  /// Thus be aware to reduce MNUBINS for this mode of operation!
  assert_always(get_noncommentline(file, line));
  std::stringstream(line) >> dum1;
  if (do_exspec) {
    globals::do_emission_res = dum1;
    if (globals::do_emission_res == 1) printout("input: do_exspec ... extract LOS dependent emission information\n");
  }

  /// To reduce the work imbalance between different MPI tasks I introduced a diffusion
  /// for kpkts, since it turned out that this work imbalance was largely dominated
  /// by continuous collisional interactions. By introducing a diffusion time for kpkts
  /// this loop is broken. The following two parameters control this approximation.
  /// Parameter one (a float) gives the relative fraction of a time step which individual
  /// kpkts live. Parameter two (an int) gives the number of time steps for which we
  /// want to use this approximation
  assert_always(get_noncommentline(file, line));
  std::stringstream(line) >> globals::kpktdiffusion_timescale >> globals::n_kpktdiffusion_timesteps;
  printout("input: kpkts diffuse %g of a time step's length for the first %d time steps\n",
           globals::kpktdiffusion_timescale, globals::n_kpktdiffusion_timesteps);

  file.close();

  if (rank == 0 && !globals::simulation_continued_from_saved) {
    // back up original input file, adding comments to each line
    update_parameterfile(-1);
  }
}
#endif

void update_parameterfile(int nts)
/// Subroutine to read in input parameters from input.txt.
{
  assert_always(globals::rank_global == 0);
  if (nts >= 0) {
    printout("Update input.txt for restart at timestep %d...", nts);
  } else {
    printout("Copying input.txt to input-newrun.txt...");
  }

  std::ifstream file("input.txt");
  assert_always(file.is_open());

  std::ofstream fileout("input.txt.tmp");
  assert_always(fileout.is_open());

  std::string line;

  // FILE *input_file = fopen_required("input.txt", "r+");
  // setvbuf(input_file, NULL, _IOLBF, 0);

  char c_line[1024];
  int noncomment_linenum = -1;
  while (std::getline(file, line)) {
    if (!lineiscommentonly(line)) {
      noncomment_linenum++;  // line number starting from 0, ignoring comment and blank lines (that start with '#')

      // if (!preceeding_comment && noncomment_linenum < inputlinecommentcount - 1)
      // {
      //   fileout << '#' << inputlinecomments[noncomment_linenum] << '\n';
      // }

      // overwrite particular lines to enable restarting from the current timestep
      if (nts >= 0) {
        if (noncomment_linenum == 2) {
          /// Number of start and end time step
          snprintf(c_line, 1024, "%d %d", nts, globals::ftstep);
          // line.assign(c_line);
          line.replace(line.begin(), line.end(), c_line);
        } else if (noncomment_linenum == 16) {
          /// resume from gridsave file
          snprintf(c_line, 1024, "%d", 1);  /// Force continuation
          line.assign(c_line);
        }
      }

      if (noncomment_linenum == 21) {
        /// by default, exspec should use all available packet files
        globals::nprocs_exspec = globals::nprocs;
        snprintf(c_line, 1024, "%d", globals::nprocs_exspec);
        line.assign(c_line);
      }

      if (noncomment_linenum < inputlinecommentcount) {
        const int commentstart = 25;

        // truncate any existing comment on the line
        if (line.find("#") != std::string::npos) {
          line.resize(line.find("#"));
        }

        line.resize(commentstart, ' ');
        line.append("# ");
        line.append(inputlinecomments[noncomment_linenum]);
      }
    }

    fileout << line << '\n';
  }

  fileout.close();
  file.close();

  if (nts < 0) {
    std::rename("input.txt.tmp", "input-newrun.txt");  // back up the original for starting a new simulation
  } else {
    std::remove("input.txt");
    std::rename("input.txt.tmp", "input.txt");
  }

  printout("done\n");
}

void time_init(void)
// Subroutine to define the time steps.
{
  /// t=globals::tmin is the start of the calcualtion. t=globals::tmax is the end of the calculation.
  /// globals::ntstep is the number of time steps wanted.

  globals::time_step = static_cast<struct time *>(malloc((globals::ntstep + 1) * sizeof(struct time)));

  /// Now set the individual time steps
  switch (TIMESTEP_SIZE_METHOD) {
    case TIMESTEP_SIZES_LOGARITHMIC: {
      for (int n = 0; n < globals::ntstep; n++) {  // For logarithmic steps, the logarithmic inverval will be
        const double dlogt = (log(globals::tmax) - log(globals::tmin)) / globals::ntstep;
        globals::time_step[n].start = globals::tmin * exp(n * dlogt);
        globals::time_step[n].mid = globals::tmin * exp((n + 0.5) * dlogt);
        globals::time_step[n].width = (globals::tmin * exp((n + 1) * dlogt)) - globals::time_step[n].start;
      }
      break;
    }

    case TIMESTEP_SIZES_CONSTANT: {
      for (int n = 0; n < globals::ntstep; n++) {
        // for constant timesteps
        const double dt = (globals::tmax - globals::tmin) / globals::ntstep;
        globals::time_step[n].start = globals::tmin + n * dt;
        globals::time_step[n].width = dt;
        globals::time_step[n].mid = globals::time_step[n].start + 0.5 * globals::time_step[n].width;
      }
      break;
    }

    case TIMESTEP_SIZES_LOGARITHMIC_THEN_CONSTANT: {
      // First part log, second part fixed timesteps
      const double t_transition = TIMESTEP_TRANSITION_TIME * DAY;  // transition from logarithmic to fixed timesteps
      const double maxtsdelta = FIXED_TIMESTEP_WIDTH * DAY;        // maximum timestep width in fixed part
      assert_always(t_transition > globals::tmin);
      assert_always(t_transition < globals::tmax);
      const int nts_fixed = ceil((globals::tmax - t_transition) / maxtsdelta);
      const double fixed_tsdelta = (globals::tmax - t_transition) / nts_fixed;
      assert_always(nts_fixed > 0);
      assert_always(nts_fixed < globals::ntstep);
      const int nts_log = globals::ntstep - nts_fixed;
      assert_always(nts_log > 0);
      assert_always(nts_log < globals::ntstep);
      assert_always((nts_log + nts_fixed) == globals::ntstep);
      for (int n = 0; n < globals::ntstep; n++) {
        if (n < nts_log) {
          // For logarithmic steps, the logarithmic inverval will be
          const double dlogt = (log(t_transition) - log(globals::tmin)) / nts_log;
          globals::time_step[n].start = globals::tmin * exp(n * dlogt);
          globals::time_step[n].mid = globals::tmin * exp((n + 0.5) * dlogt);
          globals::time_step[n].width = (globals::tmin * exp((n + 1) * dlogt)) - globals::time_step[n].start;
        } else {
          // for constant timesteps
          const double prev_start =
              n > 0 ? (globals::time_step[n - 1].start + globals::time_step[n - 1].width) : globals::tmin;
          globals::time_step[n].start = prev_start;
          globals::time_step[n].width = fixed_tsdelta;
          globals::time_step[n].mid = globals::time_step[n].start + 0.5 * globals::time_step[n].width;
        }
      }
      break;
    }

    case TIMESTEP_SIZES_CONSTANT_THEN_LOGARITHMIC: {
      // /// First part fixed timesteps, second part log timesteps
      const double t_transition = TIMESTEP_TRANSITION_TIME * DAY;  // transition from fixed to logarithmic timesteps
      const double maxtsdelta = FIXED_TIMESTEP_WIDTH * DAY;        // timestep width of fixed timesteps
      assert_always(t_transition > globals::tmin);
      assert_always(t_transition < globals::tmax);
      const int nts_fixed = ceil((t_transition - globals::tmin) / maxtsdelta);
      const double fixed_tsdelta = (t_transition - globals::tmin) / nts_fixed;
      assert_always(nts_fixed > 0);
      assert_always(nts_fixed < globals::ntstep);
      const int nts_log = globals::ntstep - nts_fixed;
      assert_always(nts_log > 0);
      assert_always(nts_log < globals::ntstep);
      assert_always((nts_log + nts_fixed) == globals::ntstep);
      for (int n = 0; n < globals::ntstep; n++) {
        if (n < nts_fixed) {
          // for constant timesteps
          globals::time_step[n].start = globals::tmin + n * fixed_tsdelta;
          globals::time_step[n].width = fixed_tsdelta;
          globals::time_step[n].mid = globals::time_step[n].start + 0.5 * globals::time_step[n].width;
        } else {
          // For logarithmic time steps, the logarithmic interval will be
          const double dlogt = (log(globals::tmax) - log(t_transition)) / nts_log;
          const double prev_start =
              n > 0 ? (globals::time_step[n - 1].start + globals::time_step[n - 1].width) : globals::tmin;
          globals::time_step[n].start = prev_start;
          globals::time_step[n].width = (t_transition * exp((n - nts_fixed + 1) * dlogt)) - globals::time_step[n].start;
          globals::time_step[n].mid = globals::time_step[n].start + 0.5 * globals::time_step[n].width;
        }
      }
      break;
    }

    default:
      assert_always(false);
      abort();
  }

  // to limit the timestep durations
  // const double maxt = 0.5 * DAY;
  // for (int n = globals::ntstep - 1; n > 0; n--)
  // {
  //   if (globals::time_step[n].width > maxt)
  //   {
  //     const double boundaryshift = globals::time_step[n].width - maxt;
  //     globals::time_step[n].width -= boundaryshift;
  //     globals::time_step[n].start += boundaryshift;
  //     globals::time_step[n - 1].width += boundaryshift;
  //   }
  //   else if (n < globals::ntstep - 1 && globals::time_step[n + 1].width > maxt)
  //   {
  //     printout("TIME: Keeping logarithmic durations for timesteps <= %d\n", n);
  //   }
  // }
  // assert_always(globals::time_step[0].width <= maxt); // no solution is possible with these constraints!

  /// and add a dummy timestep which contains the endtime
  /// of the calculation
  globals::time_step[globals::ntstep].start = globals::tmax;
  globals::time_step[globals::ntstep].mid = globals::tmax;
  globals::time_step[globals::ntstep].width = 0.;

  // check consistency of start + width = start_next
  for (int n = 1; n < globals::ntstep; n++) {
    assert_always(
        fabs((globals::time_step[n - 1].start + globals::time_step[n - 1].width) / globals::time_step[n].start) - 1 <
        0.001);
  }
  assert_always(fabs((globals::time_step[globals::ntstep - 1].start + globals::time_step[globals::ntstep - 1].width) /
                     globals::tmax) -
                    1 <
                0.001);

  for (int n = 0; n < globals::ntstep; n++) {
    globals::time_step[n].positron_dep = 0.;
    globals::time_step[n].eps_positron_ana_power = 0.;
    globals::time_step[n].electron_dep = 0.;
    globals::time_step[n].electron_emission = 0.;
    globals::time_step[n].eps_electron_ana_power = 0.;
    globals::time_step[n].alpha_dep = 0.;
    globals::time_step[n].alpha_emission = 0.;
    globals::time_step[n].eps_alpha_ana_power = 0.;
    globals::time_step[n].gamma_dep = 0.;
    globals::time_step[n].gamma_dep_pathint = 0.;
    globals::time_step[n].qdot_betaminus = 0.;
    globals::time_step[n].qdot_alpha = 0.;
    globals::time_step[n].qdot_total = 0.;
    globals::time_step[n].gamma_emission = 0.;
    globals::time_step[n].cmf_lum = 0.0;
    globals::time_step[n].pellet_decays = 0;
  }
}

void write_timestep_file(void) {
  FILE *timestepfile = fopen_required("timesteps.out", "w");
  fprintf(timestepfile, "#timestep tstart_days tmid_days twidth_days\n");
  for (int n = 0; n < globals::ntstep; n++) {
    fprintf(timestepfile, "%d %lg %lg %lg\n", n, globals::time_step[n].start / DAY, globals::time_step[n].mid / DAY,
            globals::time_step[n].width / DAY);
  }
  fclose(timestepfile);
}
