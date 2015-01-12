/*! \file blackhole.h
 *  \brief routine declarations for gas accretion onto black holes, and black hole mergers
 */
/*
 * This file is largely written by Phil Hopkins (phopkins@caltech.edu) for GIZMO.
 *   It was based on a similar file in GADGET3 by Volker Springel (volker.springel@h-its.org),
 *   but the physical modules for black hole accretion and feedback have been
 *   replaced, and the algorithm for their coupling is new to GIZMO.  This file was modified
 *   on 1/9/15 by Paul Torrey (ptorrey@mit.edu) for clairity by parsing the existing code into
 *   smaller files and routines.
 */

#ifndef gizmo_blackhole_h
#define gizmo_blackhole_h


#ifndef BH_CSND_FRAC_BH_MERGE
/* Relative velocity fraction (in units of soundspeed) for merging blackholes, default=1.0 */
#define BH_CSND_FRAC_BH_MERGE 1.0
#endif



#if defined(BLACK_HOLES)
/* blackhole_utils.c */
void blackhole_start(void);
void blackhole_end(void);

/* blackholes.c */
void blackhole_properties_loop(void);
void blackhole_final_loop(void);

/* blackhole_environment.c */
void blackhole_environment_loop(void);
int blackhole_environment_evaluate(int target, int mode, int *nexport, int *nSend_local);

/* blackhole_swallow_and_kick.c */
void blackhole_swallow_and_kick_loop(void);
int blackhole_swallow_and_kick_evaluate(int target, int mode, int *nexport, int *nSend_local);

/* blackhole_feed.c */
void blackhole_feed_loop(void);
int blackhole_feed_evaluate(int target, int mode, int *nexport, int *nSend_local);


void out2particle_blackhole(struct blackhole_temp_particle_data *out, int target, int mode);

//void check_for_bh_merger(int j, MyIDType id);
void normalize_temp_info_struct(int i);
void set_blackhole_mdot(int i, int n, double dt);
void set_blackhole_new_mass(int i, int n, double dt);
#if defined(BH_DRAG) || defined(BH_DYNFRICTION)
void set_blackhole_drag(int i, int n, double dt);
#endif
#if defined(BH_PHOTONMOMENTUM) || defined(BH_BAL_WINDS)
void set_blackhole_long_range_rp(int i, int n);
#endif


#endif


#endif
