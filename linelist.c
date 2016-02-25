#include "sn3d.h"

/* To construct an energy ordered gamma ray line list. */

int get_gam_ll()
{
  FILE *line_list;
  int identify_gam_line();

  /* Start by setting up the grid of fake lines and their energies. */
  fakeg_spec.nlines = nfake_gam;

  double deltanu = ( (nusyn_max) - (nusyn_min) ) / (fakeg_spec.nlines-3);

  for (int i = 0; i < fakeg_spec.nlines; i++)
  {
    fakeg_spec.energy[i] = ((nusyn_min) + deltanu * (i-1)) * H;
    fakeg_spec.probability[i]= 0.0;
  }

  /* No do the sorting. */

  int total_lines = cobalt_spec.nlines + nickel_spec.nlines + fakeg_spec.nlines + cr48_spec.nlines + v48_spec.nlines;
  gam_line_list.total = total_lines;

  double energy_last = 0.0;
  int next = -99;
  int next_type = -99;

  for (int i = 0; i < total_lines; i++)
  {
    double energy_try = 1.e50;
    /* Ni first. */
    for (int j = 0; j < nickel_spec.nlines; j++)
    {
      if (nickel_spec.energy[j] > energy_last && nickel_spec.energy[j] < energy_try)
      {
        next_type = NI_GAM_LINE_ID;
        next = j;
        energy_try = nickel_spec.energy[j];
      }
    }
    /* Now Co. */
    for (int j = 0; j < cobalt_spec.nlines; j++)
    {
      if (cobalt_spec.energy[j] > energy_last && cobalt_spec.energy[j] < energy_try)
      {
        next_type = CO_GAM_LINE_ID;
        next = j;
        energy_try = cobalt_spec.energy[j];
      }
    }
    /* Now fake */
    for (int j = 0; j < fakeg_spec.nlines; j++)
    {
      if (fakeg_spec.energy[j] > energy_last && fakeg_spec.energy[j] < energy_try)
      {
        next_type = FAKE_GAM_LINE_ID;
        next = j;
        energy_try = fakeg_spec.energy[j];
      }
    }

    /* Now 48Cr */
    for (int j = 0; j < cr48_spec.nlines; j++)
    {
      if (cr48_spec.energy[j] > energy_last && cr48_spec.energy[j] < energy_try)
      {
        next_type = CR48_GAM_LINE_ID;
        next = j;
        energy_try = cr48_spec.energy[j];
      }
    }

    /* Now 48V */
    for (int j = 0; j < v48_spec.nlines; j++)
    {
      if (v48_spec.energy[j] > energy_last && v48_spec.energy[j] < energy_try)
      {
        next_type = V48_GAM_LINE_ID;
        next = j;
        energy_try = v48_spec.energy[j];
      }
    }

    gam_line_list.type[i] = next_type;
    gam_line_list.index[i] = next;
    energy_last = energy_try;
  }

  if ((line_list = fopen("line_list.txt", "w+")) == NULL){
    printout("Cannot open line_list.txt.\n");
    exit(0);
  }

  for (int i = 0; i < total_lines; i++)
  {
    double a, b;
    identify_gam_line(gam_line_list.type[i], gam_line_list.index[i], &a, &b);
    fprintf(line_list, "%d %d %d %g %g \n", i, gam_line_list.type[i], gam_line_list.index[i], a/MEV, b);
  }
  fclose(line_list);

  return 0;
}


/********************************************************************************************/
int identify_gam_line(int ele_type, int ele_index, double *eret, double *pret)
{
  if (ele_type == NI_GAM_LINE_ID)
  {
    *eret = nickel_spec.energy[ele_index];
    *pret = nickel_spec.probability[ele_index];
  }
  else if (ele_type == CO_GAM_LINE_ID)
  {
    *eret = cobalt_spec.energy[ele_index];
    *pret = cobalt_spec.probability[ele_index];
  }
  else if (ele_type == FAKE_GAM_LINE_ID)
  {
    *eret = fakeg_spec.energy[ele_index];
    *pret = fakeg_spec.probability[ele_index];
  }
  else if (ele_type == CR48_GAM_LINE_ID)
  {
    *eret = cr48_spec.energy[ele_index];
    *pret = cr48_spec.probability[ele_index];
  }
  else if (ele_type == V48_GAM_LINE_ID)
  {
    *eret = v48_spec.energy[ele_index];
    *pret = v48_spec.probability[ele_index];
  }
  else
  {
    printout("Identify_gam_line failed. Abort.\n");
    exit(0);
  }

  return 0;
}
