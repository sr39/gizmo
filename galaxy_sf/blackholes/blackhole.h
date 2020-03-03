/*! \file blackhole.h
 *  \brief routine declarations for gas accretion onto black holes, and black hole mergers
 */
/*
* This file is largely written by Phil Hopkins (phopkins@caltech.edu) for GIZMO.
* see notes in blackhole.c for details on code history.
*/

#ifndef gizmo_blackhole_h
#define gizmo_blackhole_h


#if defined(BLACK_HOLES)


#ifndef BH_CSND_FRAC_BH_MERGE
/* Relative velocity fraction (in units of soundspeed) for merging blackholes, default=1.0 */
#define BH_CSND_FRAC_BH_MERGE 1.0
#endif


#define BHPOTVALUEINIT 1.0e30
extern int N_active_loc_BHs;    /*!< number of active black holes on the LOCAL processor */

extern struct blackhole_temp_particle_data       // blackholedata_topass
{
    MyIDType index;
    MyFloat BH_InternalEnergy, Mgas_in_Kernel, Mstar_in_Kernel, Malt_in_Kernel;
    MyFloat Jgas_in_Kernel[3], Jstar_in_Kernel[3], Jalt_in_Kernel[3]; // mass/angular momentum for GAS/STAR/TOTAL components computed always now
    MyLongDouble accreted_Mass, accreted_BH_Mass, accreted_BH_Mass_alphadisk;
#ifdef GRAIN_FLUID
    MyFloat accreted_dust_Mass;
#endif    
#ifdef BH_ALPHADISK_ACCRETION
    MyFloat mdot_alphadisk;             /*!< gives mdot of mass going into alpha disk */
#endif
#if defined(BH_OUTPUT_MOREINFO)
    MyFloat Sfr_in_Kernel;
#endif
#if defined(BH_GRAVACCRETION) && (BH_GRAVACCRETION == 0)
    MyFloat MgasBulge_in_Kernel, MstarBulge_in_Kernel;
#endif
#if defined(BH_PHOTONMOMENTUM) || defined(BH_WIND_CONTINUOUS)
    MyFloat GradRho_in_Kernel[3], BH_angle_weighted_kernel_sum;
#endif
#ifdef BH_DYNFRICTION
    MyFloat DF_rms_vel, DF_mean_vel[3], DF_mmax_particles;
#endif
#if defined(BH_BONDI) || defined(BH_DRAG) || (BH_GRAVACCRETION >= 5) || defined(SINGLE_STAR_SINK_DYNAMICS)
    MyFloat BH_SurroundingGasVel[3];
#endif
#if (BH_GRAVACCRETION == 8)
    MyFloat hubber_mdot_vr_estimator, hubber_mdot_disk_estimator, hubber_mdot_bondi_limiter;
#endif
#if defined(BH_FOLLOW_ACCRETED_MOMENTUM)
    MyLongDouble accreted_momentum[3];        /*!< accreted linear momentum */
#endif
#if defined(BH_FOLLOW_ACCRETED_COM)
    MyLongDouble accreted_centerofmass[3];    /*!< accreted center-of-mass */
#endif
#if defined(BH_FOLLOW_ACCRETED_ANGMOM)
    MyLongDouble accreted_J[3];               /*!< accreted angular momentum */
#endif
#if defined(BH_GRAVCAPTURE_GAS)
    MyFloat mass_to_swallow_edd;        /*!< gives the mass we want to swallow that contributes to eddington */
#endif
#if defined(BH_RETURN_ANGMOM_TO_GAS)
    MyFloat angmom_prepass_sum_for_passback[3]; /*!< Normalization term for angular momentum feedback kicks, see denominator of Eq 22 of Hubber 2013 */
    MyFloat angmom_norm_topass_in_swallowloop;  /*!< corresponding scalar normalization calculated from the vector above */
#endif
}
*BlackholeTempInfo;


/* blackhole_utils.c */
void blackhole_start(void);
void blackhole_end(void);
void blackhole_properties_loop(void);
double bh_eddington_mdot(double bh_mass);
double bh_lum_bol(double mdot, double mass, long id);

/* blackholes.c */
void blackhole_final_operations(void);

/* blackhole_environment.c */
void blackhole_environment_loop(void);
#ifdef BH_GRAVACCRETION
void blackhole_environment_second_loop(void);
#endif

/* blackhole_swallow_and_kick.c */
void blackhole_swallow_and_kick_loop(void);

/* blackhole_feed.c */
void blackhole_feed_loop(void);

//void check_for_bh_merger(int j, MyIDType id);
int bh_check_boundedness(int j, double vrel, double vesc, double dr_code, double sink_radius);
double bh_vesc(int j, double mass, double r_code, double bh_softening);
void set_blackhole_mdot(int i, int n, double dt);
void set_blackhole_new_mass(int i, int n, double dt);
#if defined(BH_DRAG) || defined(BH_DYNFRICTION)
void set_blackhole_drag(int i, int n, double dt);
#endif
#if defined(BH_PHOTONMOMENTUM) || defined(BH_WIND_CONTINUOUS)
void set_blackhole_long_range_rp(int i, int n);
#endif



#endif
#endif
