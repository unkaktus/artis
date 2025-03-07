// #include <gsl/gsl_poly.h>
#ifndef __CUDA_ARCH__
#include <gsl/gsl_blas.h>
#endif

#include "boundary.h"
#include "grid.h"
#include "rpkt.h"
#include "sn3d.h"
#include "stats.h"
#include "update_packets.h"
#include "vectors.h"

__host__ __device__ static double get_shellcrossdist(const double pos[3], const double dir[3], const double shellradius,
                                                     const bool isinnerboundary, const double tstart)
// find the closest forward distance to the intersection of a ray with an expanding spherical shell
// return -1 if there are no forward intersections (or if the intersection is tangential to the shell)
{
  assert_always(shellradius > 0);
  const bool debug = false;
  if (debug) {
    printout("get_shellcrossdist isinnerboundary %d\n", isinnerboundary);
    printout("shellradius %g tstart %g len(pos) %g\n", shellradius, tstart, vec_len(pos));
  }
  const double speed = vec_len(dir) * CLIGHT_PROP;
  const double a = dot(dir, dir) - pow(shellradius / tstart / speed, 2);
  const double b = 2 * (dot(dir, pos) - pow(shellradius, 2) / tstart / speed);
  const double c = dot(pos, pos) - pow(shellradius, 2);

  const double discriminant = pow(b, 2) - 4 * a * c;

  if (discriminant < 0) {
    // no intersection
    assert_always(shellradius < vec_len(pos));
    if (debug) printout("no intersection\n");
    return -1;
  } else if (discriminant > 0) {
    // two intersections
    double d1 = (-b + sqrt(discriminant)) / 2 / a;
    double d2 = (-b - sqrt(discriminant)) / 2 / a;

    double posfinal1[3];
    double posfinal2[3];
#ifndef __CUDA_ARCH__
    cblas_dcopy(3, pos, 1, posfinal1, 1);      // posfinal1 = pos
    cblas_daxpy(3, d1, dir, 1, posfinal1, 1);  // posfinal1 += d1 * dir;

    cblas_dcopy(3, pos, 1, posfinal2, 1);
    cblas_daxpy(3, d2, dir, 1, posfinal2, 1);
#else
    for (int d = 0; d < 3; d++) {
      posfinal1[d] = pos[d] + d1 * dir[d];
      posfinal2[d] = pos[d] + d2 * dir[d];
    }
#endif

    const double shellradiusfinal1 = shellradius / tstart * (tstart + d1 / speed);
    const double shellradiusfinal2 = shellradius / tstart * (tstart + d2 / speed);
    // printout("solution1 d1 %g radiusfinal1 %g shellradiusfinal1 %g\n", d1, vec_len(posfinal1), shellradiusfinal1);
    // printout("solution2 d2 %g radiusfinal2 %g shellradiusfinal2 %g\n", d2, vec_len(posfinal2), shellradiusfinal2);
    assert_always(fabs(vec_len(posfinal1) / shellradiusfinal1 - 1.) < 1e-3);
    assert_always(fabs(vec_len(posfinal2) / shellradiusfinal2 - 1.) < 1e-3);

    // invalidate any solutions that require entering the boundary from the wrong radial direction
    if (isinnerboundary) {
      if (dot(posfinal1, dir) > 0.) {
        d1 = -1;
      }
      if (dot(posfinal2, dir) > 0.) {
        d2 = -1;
      }
    } else {
      if (dot(posfinal1, dir) < 0.) {
        d1 = -1;
      }
      if (dot(posfinal2, dir) < 0.) {
        d2 = -1;
      }
    }

    // negative d means in the reverse direction along the ray
    // ignore negative d values, and if two are positive then return the smaller one
    if (d1 < 0 && d2 < 0) {
      return -1;
    } else if (d2 < 0) {
      return d1;
    } else if (d1 < 0) {
      return d2;
    } else {
      return fmin(d1, d2);
    }
  } else {
    // exactly one intersection
    // ignore this and don't change which cell the packet is in
    assert_always(shellradius <= vec_len(pos));
    printout("single intersection\n");
    return -1.;
  }
}

__host__ __device__ double boundary_cross(struct packet *const pkt_ptr, int *snext)
/// Basic routine to compute distance to a cell boundary.
{
  const double tstart = pkt_ptr->prop_time;

  // There are six possible boundary crossings. Each of the three
  // cartesian coordinates may be taken in turn. For x, the packet
  // trajectory is
  // x = x0 + (dir.x) * c * (t - tstart)
  // the boundries follow
  // x+/- = x+/-(tmin) * (t/tmin)
  // so the crossing occurs when
  // t = (x0 - (dir.x)*c*tstart)/(x+/-(tmin)/tmin - (dir.x)c)

  // Modified so that it also returns the distance to the closest cell
  // boundary, regardless of direction.

  // d is used to loop over the coordinate indicies 0,1,2 for x,y,z

  const int cellindex = pkt_ptr->where;

  // the following four vectors are in grid coordinates, so either x,y,z or r
  const int ndim = grid::get_ngriddimensions();
  assert_testmodeonly(ndim <= 3);
  double initpos[3] = {0};  // pkt_ptr->pos converted to grid coordinates
  double cellcoordmax[3] = {0};
  double vel[3] = {0};  // pkt_ptr->dir * CLIGHT_PROP converted to grid coordinates

  if (GRID_TYPE == GRID_UNIFORM) {
    // XYZ coordinates
    for (int d = 0; d < ndim; d++) {
      initpos[d] = pkt_ptr->pos[d];
      cellcoordmax[d] = grid::get_cellcoordmax(cellindex, d);
      vel[d] = pkt_ptr->dir[d] * CLIGHT_PROP;
    }
  } else if (GRID_TYPE == GRID_SPHERICAL1D) {
    // the only coordinate is radius from the origin
    initpos[0] = vec_len(pkt_ptr->pos);
    cellcoordmax[0] = grid::get_cellcoordmax(cellindex, 0);
    vel[0] = dot(pkt_ptr->pos, pkt_ptr->dir) / vec_len(pkt_ptr->pos) * CLIGHT_PROP;  // radial velocity
  } else {
    assert_always(false);
  }

  // for (int d = 0; d < ndim; d++)
  // {
  //   if (initpos[d] < grid::get_cellcoordmin(cellindex, d) || initpos[d] > cellcoordmax[d])
  //   {
  //     printout("WARNING: packet should have already escaped.\n");
  //     *snext = -99;
  //     return 0;
  //   }
  // }

  // printout("boundary.c: x0 %g, y0 %g, z0 %g\n", initpos[0] initpos[1] initpos[2]);
  // printout("boundary.c: vx %g, vy %g, vz %g\n",vel[0],vel[1],vel[2]);
  // printout("boundary.c: cellxmin %g, cellymin %g, cellzmin %g\n",grid::get_cellcoordmin(cellindex,
  // 0),grid::get_cellcoordmin(cellindex, 1),grid::get_cellcoordmin(cellindex, 2)); printout("boundary.c: cellxmax %g,
  // cellymax %g, cellzmax %g\n",cellcoordmax[0],cellcoordmax[1],cellcoordmax[2]);

  enum cell_boundary last_cross = pkt_ptr->last_cross;
  enum cell_boundary negdirections[3] = {NEG_X, NEG_Y, NEG_Z};  // 'X' might actually be radial coordinate
  enum cell_boundary posdirections[3] = {POS_X, POS_Y, POS_Z};

  // printout("checking inside cell boundary\n");
  for (int d = 0; d < ndim; d++) {
    // flip is either zero or one to indicate +ve and -ve boundaries along the selected axis
    for (int flip = 0; flip < 2; flip++) {
      enum cell_boundary direction = flip ? posdirections[d] : negdirections[d];
      enum cell_boundary invdirection = !flip ? posdirections[d] : negdirections[d];
      const int cellindexstride = flip ? -grid::get_coordcellindexincrement(d) : grid::get_coordcellindexincrement(d);

      bool isoutside_thisside;
      if (flip) {
        isoutside_thisside = initpos[d] < (grid::get_cellcoordmin(cellindex, d) / globals::tmin * tstart -
                                           10.);  // 10 cm accuracy tolerance
      } else {
        isoutside_thisside = initpos[d] > (cellcoordmax[d] / globals::tmin * tstart + 10.);
      }

      if (isoutside_thisside && (last_cross != direction)) {
        // for (int d2 = 0; d2 < ndim; d2++)
        const int d2 = d;
        {
          printout(
              "[warning] packet %d outside coord %d %c%c boundary of cell %d. pkttype %d initpos(tmin) %g, vel %g, "
              "cellcoordmin %g, cellcoordmax %g\n",
              pkt_ptr->number, d, flip ? '-' : '+', grid::coordlabel[d], cellindex, pkt_ptr->type, initpos[d2], vel[d2],
              grid::get_cellcoordmin(cellindex, d2) / globals::tmin * tstart,
              cellcoordmax[d2] / globals::tmin * tstart);
        }
        printout("globals::tmin %g tstart %g tstart/globals::tmin %g tdecay %g\n", globals::tmin, tstart,
                 tstart / globals::tmin, pkt_ptr->tdecay);
        // printout("[warning] pkt_ptr->number %d\n", pkt_ptr->number);
        if (flip) {
          printout("[warning] delta %g\n",
                   (initpos[d] * globals::tmin / tstart) - grid::get_cellcoordmin(cellindex, d));
        } else {
          printout("[warning] delta %g\n", cellcoordmax[d] - (initpos[d] * globals::tmin / tstart));
        }

        printout("[warning] dir [%g, %g, %g]\n", pkt_ptr->dir[0], pkt_ptr->dir[1], pkt_ptr->dir[2]);
        if ((vel[d] - (initpos[d] / tstart)) > 0) {
          if ((grid::get_cellcoordpointnum(cellindex, d) == (grid::ncoordgrid[d] - 1) && cellindexstride > 0) ||
              (grid::get_cellcoordpointnum(cellindex, d) == 0 && cellindexstride < 0)) {
            printout("escaping packet\n");
            *snext = -99;
            return 0;
          } else {
            *snext = pkt_ptr->where + cellindexstride;
            pkt_ptr->last_cross = invdirection;
            printout("[warning] swapping packet cellindex from %d to %d and setting last_cross to %d\n", pkt_ptr->where,
                     *snext, pkt_ptr->last_cross);
            return 0;
          }
        } else {
          printout("pretending last_cross is %d\n", direction);
          last_cross = direction;
        }
      }
    }
  }

  // printout("pkt_ptr->number %d\n", pkt_ptr->number);
  // printout("delta1x %g delta2x %g\n",  (initpos[0] * globals::tmin/tstart)-grid::get_cellcoordmin(cellindex, 0),
  // cellcoordmax[0] - (initpos[0] * globals::tmin/tstart)); printout("delta1y %g delta2y %g\n",  (initpos[1] *
  // globals::tmin/tstart)-grid::get_cellcoordmin(cellindex, 1), cellcoordmax[1] - (initpos[1] * globals::tmin/tstart));
  // printout("delta1z %g delta2z %g\n",  (initpos[2] * globals::tmin/tstart)-grid::get_cellcoordmin(cellindex, 2),
  // cellcoordmax[2] - (initpos[2] * globals::tmin/tstart)); printout("dir [%g, %g, %g]\n",
  // pkt_ptr->dir[0],pkt_ptr->dir[1],pkt_ptr->dir[2]);

  double t_coordmaxboundary[3];  // time to reach the cell's upper boundary on each coordinate
  double t_coordminboundary[3];  // likewise, the lower boundaries (smallest x,y,z or radius value in the cell)
  if (GRID_TYPE == GRID_SPHERICAL1D) {
    last_cross = NONE;  // we will handle this separately by setting d_inner and d_outer negative for invalid directions
    const double r_inner = grid::get_cellcoordmin(cellindex, 0) * tstart / globals::tmin;

    const double d_inner = (r_inner > 0.) ? get_shellcrossdist(pkt_ptr->pos, pkt_ptr->dir, r_inner, true, tstart) : -1.;
    t_coordminboundary[0] = d_inner / CLIGHT_PROP;

    const double r_outer = cellcoordmax[0] * tstart / globals::tmin;
    const double d_outer = get_shellcrossdist(pkt_ptr->pos, pkt_ptr->dir, r_outer, false, tstart);
    t_coordmaxboundary[0] = d_outer / CLIGHT_PROP;

    // printout("cell %d\n", pkt_ptr->where);
    // printout("initradius %g: velrad %g\n", initpos[0], vel[0]);
    // printout("d_outer %g d_inner %g \n", d_outer, d_inner);
    // printout("t_plus %g t_minus %g \n", t_coordmaxboundary[0], t_coordminboundary[0]);
    // printout("cellrmin %g cellrmax %g\n",
    //          grid::get_cellcoordmin(cellindex, 0) * tstart / globals::tmin, cellcoordmax[0] * tstart /
    //          globals::tmin);
    // printout("tstart %g\n", tstart);
  } else {
    // const double overshoot = grid::wid_init(cellindex) * 2e-7;
    constexpr double overshoot = 0.;
    for (int d = 0; d < 3; d++) {
      t_coordmaxboundary[d] = ((initpos[d] - (vel[d] * tstart)) /
                               ((cellcoordmax[d] + overshoot) - (vel[d] * globals::tmin)) * globals::tmin) -
                              tstart;

      t_coordminboundary[d] =
          ((initpos[d] - (vel[d] * tstart)) /
           ((grid::get_cellcoordmin(cellindex, d) - overshoot) - (vel[d] * globals::tmin)) * globals::tmin) -
          tstart;
    }
  }

  // printout("comparing distances. last_cross = %d\n", last_cross);
  // We now need to identify the shortest +ve time - that's the one we want.
  int choice = 0;  /// just a control variable to
  double time = 1.e99;
  // close = 1.e99;
  // printout("bondary.c check value of last_cross = %d\n",last_cross);
  for (int d = 0; d < ndim; d++) {
    if ((t_coordmaxboundary[d] > 0) && (t_coordmaxboundary[d] < time) && (last_cross != negdirections[d])) {
      choice = posdirections[d];
      time = t_coordmaxboundary[d];
      // equivalently if (nxyz[d] == (grid::ncoordgrid[d] - 1))
      // if (grid::get_cellcoordmin(cellindex, d) + 1.5 * grid::wid_init > coordmax[d])
      if (grid::get_cellcoordpointnum(cellindex, d) == (grid::ncoordgrid[d] - 1)) {
        *snext = -99;
      } else {
        *snext = pkt_ptr->where + grid::get_coordcellindexincrement(d);
        pkt_ptr->last_cross = posdirections[d];
      }
    }

    if ((t_coordminboundary[d] > 0) && (t_coordminboundary[d] < time) && (last_cross != posdirections[d])) {
      choice = negdirections[d];
      time = t_coordminboundary[d];
      // equivalently if (nxyz[d] == 0)
      // if (grid::get_cellcoordmin(cellindex, d) < - coordmax[d] + 0.5 * grid::wid_init)
      if (grid::get_cellcoordpointnum(cellindex, d) == 0) {
        *snext = -99;
      } else {
        *snext = pkt_ptr->where - grid::get_coordcellindexincrement(d);
        pkt_ptr->last_cross = negdirections[d];
      }
    }
  }

  if (choice == 0) {
    printout("Something wrong in boundary crossing - didn't find anything.\n");
    printout("packet %d cell %d or %d\n", pkt_ptr->number, cellindex, pkt_ptr->where);
    printout("choice %d\n", choice);
    printout("globals::tmin %g tstart %g\n", globals::tmin, tstart);
    printout("last_cross %d, type %d\n", last_cross, pkt_ptr->type);
    for (int d2 = 0; d2 < 3; d2++) {
      printout("coord %d: initpos %g dir %g\n", d2, pkt_ptr->pos[d2], pkt_ptr->dir[d2]);
    }
    printout("|initpos| %g |dir| %g |pos.dir| %g\n", vec_len(pkt_ptr->pos), vec_len(pkt_ptr->dir),
             dot(pkt_ptr->pos, pkt_ptr->dir));
    for (int d2 = 0; d2 < ndim; d2++) {
      printout("coord %d: txyz_plus %g txyz_minus %g \n", d2, t_coordmaxboundary[d2], t_coordminboundary[d2]);
      printout("coord %d: cellcoordmin %g cellcoordmax %g\n", d2,
               grid::get_cellcoordmin(cellindex, d2) * tstart / globals::tmin,
               cellcoordmax[d2] * tstart / globals::tmin);
    }
    printout("tstart %g\n", tstart);

    // abort();
  }

  // Now we know what happens. The distance to crossing is....
  double distance = CLIGHT_PROP * time;
  // printout("boundary_cross: time %g distance %g\n", time, distance);
  // closest = close;

  return distance;
}

__host__ __device__ void change_cell(struct packet *pkt_ptr, int snext)
/// Routine to take a packet across a boundary.
{
  if (false) {
    const int cellindex = pkt_ptr->where;
    printout("[debug] cellnumber %d nne %g\n", cellindex, grid::get_nne(grid::get_cell_modelgridindex(cellindex)));
    printout("[debug] snext %d\n", snext);
  }

  if (snext == -99) {
    // Then the packet is exiting the grid. We need to record
    // where and at what time it leaves the grid.
    pkt_ptr->escape_type = pkt_ptr->type;
    pkt_ptr->escape_time = pkt_ptr->prop_time;
    pkt_ptr->type = TYPE_ESCAPE;
    safeincrement(globals::nesc);
  } else {
    // Just need to update "where".
    // const int cellnum = pkt_ptr->where;
    // const int old_mgi = grid::get_cell_modelgridindex(cellnum);
    pkt_ptr->where = snext;
    // const int mgi = grid::get_cell_modelgridindex(snext);

    stats::increment(stats::COUNTER_CELLCROSSINGS);
  }
}

// static int locate(const struct packet *pkt_ptr, double t_current)
// /// Routine to return which grid cell the packet is in.
// {
//   // Cheap and nasty version for now - assume a uniform grid.
//   int xx = (pkt_ptr->pos[0] - (globals::cell[0].pos_min[0]*t_current/globals::tmin)) /
//   (grid::wid_init*t_current/globals::tmin); int yy = (pkt_ptr->pos[1] -
//   (globals::cell[0].pos_min[1]*t_current/globals::tmin)) / (grid::wid_init*t_current/globals::tmin); int zz =
//   (pkt_ptr->pos[2] - (globals::cell[0].pos_min[2]*t_current/globals::tmin)) /
//   (grid::wid_init*t_current/globals::tmin);
//
//   return xx + (grid::ncoordgrid[0] * yy) + (grid::ncoordgrid[0] * grid::ncoordgrid[1] * zz);
// }
