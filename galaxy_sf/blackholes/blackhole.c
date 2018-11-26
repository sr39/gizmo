#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <gsl/gsl_math.h>
#include <gsl/gsl_rng.h>
#include <gsl/gsl_randist.h>
#include "../../allvars.h"
#include "../../proto.h"
#include "../../kernel.h"


/*! \file blackhole.c
 *  \brief routines for gas accretion onto black holes, and black hole mergers
 */
/*
 * This file is largely written by Phil Hopkins (phopkins@caltech.edu) for GIZMO.
 *   It was based on a similar file in GADGET3 by Volker Springel (volker.springel@h-its.org),
 *   but the physical modules for black hole accretion and feedback have been
 *   replaced, and the algorithm for their coupling is new to GIZMO.  This file was modified
 *   on 1/9/15 by Paul Torrey (ptorrey@mit.edu) for clarity by parsing the existing code into
 *   smaller files and routines.  Some communication and black hole structures were modified
 *   to reduce memory usage. Cleanup, de-bugging, and consolidation of routines by Xiangcheng Ma
 *   (xchma@caltech.edu) followed on 05/15/15; re-integrated by PFH.
 */

#ifdef BLACK_HOLES

extern struct blackhole_temp_particle_data *BlackholeTempInfo;


/*  This is the master routine for the BH physics modules.
 *  It is called in calculate_non_standard_physics in run.c */
void blackhole_accretion(void)
{
    if(All.TimeStep == 0.) return; /* no evolution */
    if(ThisTask == 0)  {printf("Start black-hole operations...\n");}
    long i; for(i = 0; i < NumPart; i++) {P[i].SwallowID = 0;} /* zero out accretion */
    blackhole_start();              /* allocates and cleans BlackholeTempInfo struct */
    
    /* this is the PRE-PASS loop.*/
    blackhole_environment_loop();    /* populates BlackholeTempInfo based on surrounding gas (blackhole_environment.c).
                                      If using gravcap the desired mass accretion rate is calculated and set to BlackholeTempInfo.mass_to_swallow_edd */
#ifdef BH_GRAVACCRETION
    blackhole_environment_second_loop();    /* populates BlackholeTempInfo based on surrounding gas (blackhole_environment.c).
                                               Here we compute quantities that require knowledge of previous environment variables
                                               --> Bulge-Disk kinematic decomposition for gravitational torque accretion  */
#endif
    /*----------------------------------------------------------------------
     Now do a first set of local operations based on BH environment calculation:
     calculate mdot, dynamical friction, and other 'BH-centric' operations.
     No MPI comm necessary.
     ----------------------------------------------------------------------*/
    blackhole_properties_loop();       /* do 'BH-centric' operations such as dyn-fric, mdot, etc. This loop is at the end of this file.  */
    /*----------------------------------------------------------------------
     Now we perform a second pass over the black hole environment.
     Re-evaluate the decision to stochastically swallow gas if we exceed eddington.
     Use the above info to determine the weight functions for feedback
     ----------------------------------------------------------------------*/
    blackhole_feed_loop();       /* BH mergers and gas/star/dm accretion events are evaluated - P[j].SwallowID's are set */
    /*----------------------------------------------------------------------
     Now we do a THIRD pass over the particles, and
     this is where we can do the actual 'swallowing' operations
     (blackhole_evaluate_swallow), and 'kicking' operations
     ----------------------------------------------------------------------*/
    blackhole_swallow_and_kick_loop();
    /*----------------------------------------------------------------------
     Now do final operations on the results from the last pass
     ----------------------------------------------------------------------*/
    blackhole_final_operations(); /* final operations on the BH with tabulated quantities (not a neighbor loop) */
    blackhole_end();            /* frees BlackholeTempInfo; cleans up */
    for(i = 0; i < NumPart; i++) {P[i].SwallowID = 0;} /* re-zero accretion */
}



/* return the eddington accretion-rate = L_edd/(epsilon_r*c*c) */
double bh_eddington_mdot(double bh_mass)
{
    return (4 * M_PI * GRAVITY*C * PROTONMASS / (All.BlackHoleRadiativeEfficiency * C * C * THOMPSON)) * (bh_mass/All.HubbleParam) * All.UnitTime_in_s;
}


/* return the bh luminosity given some accretion rate and mass (allows for non-standard models: radiatively inefficient flows, stellar sinks, etc) */
double bh_lum_bol(double mdot, double mass, long id)
{
    double c_code = C / All.UnitVelocity_in_cm_per_s;
    double lum = All.BlackHoleRadiativeEfficiency * mdot * c_code*c_code;
#ifdef SINGLE_STAR_FORMATION
    lum = calculate_individual_stellar_luminosity(mdot,mass,id);
#endif
    return All.BlackHoleFeedbackFactor * lum;
}


/* calculate escape velocity to use for bounded-ness calculations relative to the BH */
double bh_vesc(int j, double mass, double r_code)
{
    double cs_to_add_km_s = 10.0; /* we can optionally add a 'fudge factor' to v_esc to set a minimum value; useful for galaxy applications */
#if defined(SINGLE_STAR_FORMATION) || defined(BH_SEED_GROWTH_TESTS)
    cs_to_add_km_s = 0.0;
#endif
    cs_to_add_km_s *= 1.e5/All.UnitVelocity_in_cm_per_s;
    double m_eff = mass+P[j].Mass;
    if(P[j].Type==0)
    {
#ifdef BH_SEED_GROWTH_TESTS
        m_eff += 3. * 4.*M_PI/3. * r_code*r_code*r_code * SphP[j].Density;
#endif
    }
    return sqrt(2.0*All.G*(m_eff)/(r_code*All.cf_atime) + cs_to_add_km_s*cs_to_add_km_s);
}

 
/* check whether a particle is sufficiently bound to the BH to qualify for 'gravitational capture' */
int bh_check_boundedness(int j, double vrel, double vesc, double dr_code, double sink_radius) // need to know the sink radius, which can be distinct from both the softening and search radii
{
    /* if pair is a gas particle make sure to account for its thermal pressure */
    double cs = 0; if(P[j].Type==0) {cs=Particle_effective_soundspeed_i(j);}
#if defined(SINGLE_STAR_FORMATION) 
    cs = 0;
#endif
    double v2 = (vrel*vrel+cs*cs)/(vesc*vesc);
    int bound = 0;
    if(v2 < 1) 
    {
        double apocenter = dr_code / (1.0-v2); // NOTE: this is the major axis of the orbit, not the apocenter... - MYG
        double apocenter_max = All.ForceSoftening[5]; // 2.8*epsilon (softening length) //
        if(P[j].Type==5) {apocenter_max += MAX_REAL_NUMBER;} // default is to be unrestrictive for BH-BH mergers //
#ifdef SINGLE_STAR_STRICT_ACCRETION // Bate 1995-style criterion, with a fixed softening radius that is distinct from both the force softening and the search radius
	//	printf("sink radius: %g apo: %g r: %g\n", sink_radius, apocenter, dr_code);
	//	apocenter = dr_code;
	apocenter_max = 2*sink_radius;
	if(dr_code < ForceSoftening[5]) bound = 1; // force `bound' if within the kernel
#else
#if defined(SINGLE_STAR_FORMATION) || defined(BH_SEED_GROWTH_TESTS) || defined(BH_GRAVCAPTURE_GAS) || defined(BH_GRAVCAPTURE_NONGAS)
        double r_j = All.ForceSoftening[P[j].Type];
        if(P[j].Type==0) {r_j = DMAX(r_j , PPP[j].Hsml);}
        apocenter_max = DMAX(10.0*All.ForceSoftening[5],DMIN(50.0*All.ForceSoftening[5],r_j));
        if(P[j].Type==5) {apocenter_max = DMIN(apocenter_max , 1.*All.ForceSoftening[5]);}
#endif
#endif // SINGLE_STAR_STRICT_ACCRETION
        if(apocenter < apocenter_max) {bound = 1;}
    }
    return bound;
}



#if defined(BH_PHOTONMOMENTUM) || defined(BH_WIND_CONTINUOUS)
/* weight function for local (short-range) coupling terms from the black hole, including the single-scattering 
    radiation pressure and the bal winds */
double bh_angleweight_localcoupling(int j, double hR, double theta, double r, double H_bh)
{
    // this follows what we do with SNe, and applies a normalized weight based on the fraction of the solid angle subtended by the particle //
    if(r <= 0 || H_bh <= 0) return 0;
    double H_j = PPP[j].Hsml;
    if((r >= H_j && r >= H_bh) || (H_j <= 0) || (P[j].Mass <= 0) || (SphP[j].Density <= 0)) return 0;
    /* now calculate all the kernel quantities we need */
    double hinv=1./H_bh,hinv_j=1./H_j,hinv3_j=hinv_j*hinv_j*hinv_j,wk_j=0,u=r/H_bh;
    double dwk_j=0,u_j=r*hinv_j,hinv4_j=hinv_j*hinv3_j,V_j=P[j].Mass/SphP[j].Density;
    double hinv3=hinv*hinv*hinv,hinv4=hinv*hinv3,wk=0,dwk=0;
    kernel_main(u,hinv3,hinv4,&wk,&dwk,1);
    kernel_main(u_j,hinv3_j,hinv4_j,&wk_j,&dwk_j,1);
    double V_i = 4.*M_PI/3. * H_bh*H_bh*H_bh / (All.DesNumNgb * All.BlackHoleNgbFactor); // this is approximate, will be wrong (but ok b/c just increases weight to neighbors) when not enough neighbors found //
    if(V_i<0 || isnan(V_i)) {V_i=0;}
    if(V_j<0 || isnan(V_j)) {V_j=0;}
    double sph_area = fabs(V_i*V_i*dwk + V_j*V_j*dwk_j); // effective face area //
    wk = 0.5 * (1. - 1./sqrt(1. + sph_area / (M_PI*r*r))); // corresponding geometric weight //
    if((wk <= 0)||(isnan(wk))) return 0; // no point in going further, there's no physical weight here
    return wk;

#if 0
    /* optionally below: Nathan Roth's estimate of angular dependence for the momentum flux vs angle for a torus-type configuration */
    double b0,c0,f;
    // nathans 'B' and 'C' functions //
    b0=8.49403/(1.17286+hR);
    c0=64.4254/(2.5404+hR);
    f=1-(1+c0*exp(-b0*M_PI/2))/(1+c0*exp(-b0*(M_PI/2-theta)));
    return P[j].Hsml*P[j].Hsml * f;
    /* H^2 gives the fraction of the solid angle subtended by the particle (normalized by distance),
     the 'f' function gives the dForce/dOmega weighting from the radiative transfer calculations */
#endif
}


#if defined(BH_PHOTONMOMENTUM)
/* function below is used for long-range black hole radiation fields -- used only in the forcetree routines (where they 
    rely this for things like the long-range radiation pressure and compton heating) */
double bh_angleweight(double bh_lum_input, MyFloat bh_angle[3], double hR, double dx, double dy, double dz)
{
#ifdef SINGLE_STAR_FORMATION
    return bh_lum_input;
#else
    double bh_lum = bh_lum_input;
    if(bh_lum <= 0) return 0;
    if(isnan(hR)) return 0;
    if(hR <= 0) return 0;
    double r2 = dx*dx+dy*dy+dz*dz;
    if(r2 <= 0) return 0;
    if(r2*All.UnitLength_in_cm*All.UnitLength_in_cm*All.cf_atime*All.cf_atime < 9.523e36) return 0; /* no force at < 1pc */
    
    return bh_lum_input;
#if 0
    double cos_theta = (dx*bh_angle[0] + dy*bh_angle[1] + dz*bh_angle[2])/sqrt(r2);
    if(cos_theta<0) cos_theta *= -1;
    if(isnan(cos_theta)) return 0;
    if(cos_theta <= 0) return 0;
    if(cos_theta >= 1) return 1.441 * bh_lum;
    
    double hRe=hR; if(hRe<0.1) hRe=0.1; if(hRe>0.5) hRe=0.5;
    double y;
    y = -1.0 / (0.357-10.839*hRe+142.640*hRe*hRe-713.928*hRe*hRe*hRe+1315.132*hRe*hRe*hRe*hRe);
    y = 1.441 + (-6.42+9.92*hRe) * (exp(cos_theta*cos_theta*y)-exp(y)) / (1-exp(y));  // approximation to nathans fits
    //double A=5.57*exp(-hRe/0.52);double B=19.0*exp(-hRe/0.21);double C0=20.5+20.2/(1+exp((hRe-0.25)/0.035));
    //y = 1.441 + A*((1+C0*exp(-B*1.5708))/(1+C0*exp(-B*(1.5708-acos(cos_theta))))-1);
    // this is nathan's fitting function (fairly expensive with large number of exp calls and arc_cos
    //y=0.746559 - 9.10916*(-0.658128+exp(-0.418356*cos_theta*cos_theta));
    // this is normalized so the total flux is L/(4pi*r*r) and assumed monochromatic IR
    if(y>1.441) y=1.441; if(y<-5.0) y=-5.0;
    y*=2.3026; // so we can take exp, instead of pow //
    return exp(y) * bh_lum;
#endif
#endif
}
#endif
#endif /* end of #if defined(BH_PHOTONMOMENTUM) || defined(BH_WIND_CONTINUOUS) */







void blackhole_properties_loop(void)
{
    int  i, n;
    double dt;
    //double fac, medd, mbulge, r0;

    for(i=0; i<N_active_loc_BHs; i++)
    {
        n = BlackholeTempInfo[i].index;
        
        /* define the timestep */
#ifndef WAKEUP
        dt = (P[n].TimeBin ? (1 << P[n].TimeBin) : 0) * All.Timebase_interval / All.cf_hubble_a;
#else
        dt = P[n].dt_step * All.Timebase_interval / All.cf_hubble_a;
#endif
        
        /* always initialize/default to zero accretion rate */
        BPP(n).BH_Mdot=0;

/*      normalize_temp_info_struct is now done at the end of blackhole_environment_loop()
 *      so that final quantities are available for the second environment loop if needed 
 */
        //normalize_temp_info_struct(i);
        

#if defined(BH_PHOTONMOMENTUM) || defined(BH_WIND_CONTINUOUS)
        set_blackhole_long_range_rp( i,  n);
#endif
        
        set_blackhole_mdot(i, n, dt);
        
#if defined(BH_DRAG) || defined(BH_DYNFRICTION)
        set_blackhole_drag(i, n, dt);
#endif
        
        set_blackhole_new_mass(i, n, dt);

        
        /* results dumped to 'blackhole_details' files at the end of blackhole_final_operations
                so that BH mass is corrected for mass loss to radiation/bal outflows */

    }// for(i=0; i<N_active_loc_BHs; i++)
    
}










void normalize_temp_info_struct(int i)
{
    /* for new quantities, divide out weights and convert to physical units */
    int k; k=0;
    if(BlackholeTempInfo[i].Mgas_in_Kernel > 0)
    {
        BlackholeTempInfo[i].BH_InternalEnergy /= BlackholeTempInfo[i].Mgas_in_Kernel;
#if defined(BH_BONDI) || defined(BH_DRAG) || (BH_GRAVACCRETION == 5)
        for(k=0;k<3;k++)
            BlackholeTempInfo[i].BH_SurroundingGasVel[k] /= BlackholeTempInfo[i].Mgas_in_Kernel * All.cf_atime;
#endif
    }
    else
    {
        BlackholeTempInfo[i].BH_InternalEnergy = 0;
    }

    // DAA: add GAS/STAR mass/angular momentum to the TOTAL mass/angular momentum in kernel
    BlackholeTempInfo[i].Malt_in_Kernel += (BlackholeTempInfo[i].Mgas_in_Kernel + BlackholeTempInfo[i].Mstar_in_Kernel);
    for(k=0;k<3;k++)
        BlackholeTempInfo[i].Jalt_in_Kernel[k] += (BlackholeTempInfo[i].Jgas_in_Kernel[k] + BlackholeTempInfo[i].Jstar_in_Kernel[k]);

#ifdef BH_DYNFRICTION
    // DAA: normalize by the appropriate MASS in kernel depending on selected option
    double Mass_in_Kernel;
#if (BH_DYNFRICTION == 1)    // DAA: dark matter + stars
    Mass_in_Kernel = BlackholeTempInfo[i].Malt_in_Kernel - BlackholeTempInfo[i].Mgas_in_Kernel;
#elif (BH_DYNFRICTION == 2)  // DAA: stars only
    Mass_in_Kernel = BlackholeTempInfo[i].Mstar_in_Kernel;
#else
    Mass_in_Kernel = BlackholeTempInfo[i].Malt_in_Kernel;
#endif
    if(Mass_in_Kernel > 0)
    {
#if (BH_REPOSITION_ON_POTMIN == 2)
        Mass_in_Kernel = BlackholeTempInfo[i].DF_rms_vel;
#else
        BlackholeTempInfo[i].DF_rms_vel /= Mass_in_Kernel;
        BlackholeTempInfo[i].DF_rms_vel = sqrt(BlackholeTempInfo[i].DF_rms_vel) / All.cf_atime;
#endif
        for(k=0;k<3;k++) {BlackholeTempInfo[i].DF_mean_vel[k] /= Mass_in_Kernel * All.cf_atime;}
    }
#endif
    
}


void set_blackhole_mdot(int i, int n, double dt)
{
    double mdot=0;
    int k; k=0;
#ifdef BH_GRAVACCRETION
    double m_tmp_for_bhar, mdisk_for_bhar, mbulge_for_bhar, bh_mass, fac;
    double rmax_for_bhar,fgas_for_bhar,f_disk_for_bhar;
    double f0_for_bhar;
#endif
#ifdef BH_SUBGRIDBHVARIABILITY
    long nsubgridvar;
    int jsub;
    double varsg1,varsg2;
    double omega_ri,n0_sgrid_elements,norm_subgrid,time_var_subgridvar;
    gsl_rng *random_generator_forbh;
#endif
#ifdef BH_BONDI
    double  soundspeed, bhvel, rho;
#endif
#ifdef BH_ENFORCE_EDDINGTON_LIMIT
    double meddington = bh_eddington_mdot(BPP(n).BH_Mass);
#endif
    
    
    
#ifdef BH_GRAVACCRETION
    /* calculate mdot: gravitational instability accretion rate from Hopkins & Quataert 2011 */
    if(BlackholeTempInfo[i].Mgas_in_Kernel > 0)
    {
        bh_mass = BPP(n).BH_Mass;
#ifdef BH_ALPHADISK_ACCRETION
        bh_mass += BPP(n).BH_Mass_AlphaDisk;
#endif
        rmax_for_bhar = PPP[n].Hsml * All.cf_atime; /* convert to physical units */

#if (BH_GRAVACCRETION == 1)
        /* DAA: Jalt_in_Kernel is now the TOTAL angular momentum (need to subtract Jgas here) */
        m_tmp_for_bhar = BlackholeTempInfo[i].Malt_in_Kernel; double j_tmp_for_bhar=0;
        for(k=0;k<3;k++)
        {
            j_tmp_for_bhar += (BlackholeTempInfo[i].Jalt_in_Kernel[k] - BlackholeTempInfo[i].Jgas_in_Kernel[k]) * 
                              (BlackholeTempInfo[i].Jalt_in_Kernel[k] - BlackholeTempInfo[i].Jgas_in_Kernel[k]);
        }
        j_tmp_for_bhar=sqrt(j_tmp_for_bhar); /* jx,y,z, is independent of 'a_scale' b/c ~ m*r*v, vphys=v/a, rphys=r*a */
        fgas_for_bhar = BlackholeTempInfo[i].Mgas_in_Kernel / m_tmp_for_bhar;

        fac = m_tmp_for_bhar * rmax_for_bhar * sqrt(All.G*(m_tmp_for_bhar+bh_mass)/rmax_for_bhar); /* All.G is G in code (physical) units */

        f_disk_for_bhar = fgas_for_bhar + (1.75*j_tmp_for_bhar/fac);
        if(f_disk_for_bhar>1) {f_disk_for_bhar=1;}
        mdisk_for_bhar = m_tmp_for_bhar * f_disk_for_bhar;
#else
        /* DAA: default torque rate based on kinematic B/D decomposition as in Angles-Alcazar et al. */
        m_tmp_for_bhar = BlackholeTempInfo[i].Mgas_in_Kernel + BlackholeTempInfo[i].Mstar_in_Kernel;
        mbulge_for_bhar = BlackholeTempInfo[i].MstarBulge_in_Kernel; 
        if(mbulge_for_bhar>BlackholeTempInfo[i].Mstar_in_Kernel) {mbulge_for_bhar=BlackholeTempInfo[i].Mstar_in_Kernel;}
        mdisk_for_bhar = m_tmp_for_bhar - mbulge_for_bhar;
        f_disk_for_bhar = mdisk_for_bhar / m_tmp_for_bhar;
#endif  // if(BH_GRAVACCRETION == 1)
        if(mdisk_for_bhar>0) {fgas_for_bhar=BlackholeTempInfo[i].Mgas_in_Kernel/mdisk_for_bhar;} else {fgas_for_bhar=0;}
        
        if((bh_mass <=0)||(fgas_for_bhar<=0)||(m_tmp_for_bhar<=0))
        {
            mdot = 0;
        } else {
            double menc_all, omega_dyn, mgas_in_racc, mdisk_for_bhar_units, bh_mass_units, rmax_for_bhar_units, r0_accretion;
            menc_all = m_tmp_for_bhar + P[n].Mass; // total enclosed mass in kernel (note P[n].Mass can be large if BH_INCREASE_DYNAMIC_MASS is set large)
            omega_dyn = sqrt(All.G * menc_all / (rmax_for_bhar*rmax_for_bhar*rmax_for_bhar)); // 1/t_dyn for all mass inside maximum kernel radius
            mdisk_for_bhar_units = mdisk_for_bhar * (All.UnitMass_in_g/(All.HubbleParam * 1.0e9*SOLAR_MASS)); /* mdisk/1e9msun */
            bh_mass_units = bh_mass * All.UnitMass_in_g / (All.HubbleParam * 1.0e8*SOLAR_MASS); /* mbh/1e8msun */
            rmax_for_bhar_units = rmax_for_bhar * All.UnitLength_in_cm/(All.HubbleParam * 3.086e20); /* r0/100pc */
            f0_for_bhar = 0.31*f_disk_for_bhar*f_disk_for_bhar*pow(mdisk_for_bhar_units,-1./3.); /* dimensionless factor for equations */
            mgas_in_racc = BlackholeTempInfo[i].Mgas_in_Kernel; r0_accretion = rmax_for_bhar; // -total- gas mass inside of search radius [rmax_for_bhar]
#if (BH_GRAVACCRETION >= 1)
            r0_accretion = 3.*All.ForceSoftening[5]*All.cf_atime; // set to some fixed multiple of the force softening, which is effectively not changing below low redshifts for the BH, and gives a constant physical anchor point
#endif
            mgas_in_racc = (4.*M_PI/3.) * (BPP(n).DensAroundStar*All.cf_a3inv) * r0_accretion*r0_accretion*r0_accretion; // use -local- estimator of gas mass in accretion radius //

            fac = All.BlackHoleAccretionFactor * (5.0*(SOLAR_MASS/All.UnitMass_in_g)/(SEC_PER_YEAR/All.UnitTime_in_s)); // basic normalization (use alpha=5, midpoint of values alpha=[1,10] from Hopkins and Quataert 2011 //
            fac *= pow(f_disk_for_bhar, 3./2.) * pow(bh_mass_units,1./6.) / (1. + f0_for_bhar/fgas_for_bhar); // dimensionless dependence on f_disk and m_bh (latter is weak)
            mdot = fac * mdisk_for_bhar_units * pow(rmax_for_bhar_units,-3./2.); // these are the quantities which scale strongly with the scale where we evaluate the BHAR
            mdot *= pow(r0_accretion/rmax_for_bhar , 3./2.); // scales to estimator value at specified 'accretion radius' r0_accretion, assuming f_disk, etc constant and M_enclosed is a constant-density profile //
            
#if (BH_GRAVACCRETION == 2) // gas accreted at a constant rate per free-fall time from the specified 'accretion radius'
            mdot = All.BlackHoleAccretionFactor * mgas_in_racc * omega_dyn;
#endif
#if (BH_GRAVACCRETION == 3) // gravito-turbulent estimator: accrete at constant fraction per free-fall from 'accretion radius' with that fraction being f_disk^2
            mdot = All.BlackHoleAccretionFactor * (mdisk_for_bhar / menc_all) * (mdisk_for_bhar / menc_all) * mgas_in_racc * omega_dyn;
#endif
#if (BH_GRAVACCRETION == 4) // accrete constant fraction per free-fall time from accretion radius set to minimum of BH radius of gravitational dominance over Vc or cs (basically where gas more tightly bound to BH) - has Bondi-like form
            double soundspeed = GAMMA*GAMMA_MINUS1 * BlackholeTempInfo[i].BH_InternalEnergy; // this is in physical units now
            mdot = All.BlackHoleAccretionFactor * 4.*M_PI * All.G*All.G * BPP(n).BH_Mass*menc_all * (BPP(n).DensAroundStar*All.cf_a3inv) / pow(soundspeed + All.G*menc_all/rmax_for_bhar, 1.5);
#endif
#if (BH_GRAVACCRETION == 5) // use default torques estimator, but then allow gas to accrete as Bondi-Hoyle when its circularization radius is inside the BH radius of influence
            double j_tmp_for_bhar=0,jcirc_crit=0; for(k=0;k<3;k++) {j_tmp_for_bhar+=BlackholeTempInfo[i].Jgas_in_Kernel[k]*BlackholeTempInfo[i].Jgas_in_Kernel[k];}
            j_tmp_for_bhar=sqrt(j_tmp_for_bhar); jcirc_crit = BlackholeTempInfo[i].Mgas_in_Kernel * rmax_for_bhar*rmax_for_bhar*omega_dyn;
            jcirc_crit *= pow(bh_mass/m_tmp_for_bhar,2./3.);
            if(j_tmp_for_bhar < jcirc_crit) /* circularization within BH-dominated region, Bondi accretion valid */
            {
                double bhvel=0; for(k=0;k<3;k++) {bhvel += BlackholeTempInfo[i].BH_SurroundingGasVel[k]*BlackholeTempInfo[i].BH_SurroundingGasVel[k];}
                double rho = BPP(n).DensAroundStar*All.cf_a3inv; /* we want all quantities in physical units */
                double soundspeed = GAMMA*GAMMA_MINUS1 * BlackholeTempInfo[i].BH_InternalEnergy; // this is in physical units now
                double vcs_fac = pow(soundspeed+bhvel, 1.5);
                mdot = All.BlackHoleAccretionFactor * 4.*M_PI * All.G*All.G * BPP(n).BH_Mass*BPP(n).BH_Mass * rho / vcs_fac;
            } /* otherwise, circularization outside BH-dominated region, efficiency according to usual [above] */
#endif
            
            
#ifndef IO_REDUCED_MODE
            printf("BH GravAcc Eval :: mdot %g BHaccFac %g Norm %g fdisk %g bh_8 %g fgas %g f0 %g mdisk_9 %g rmax_100 %g \n\n",
                   mdot,All.BlackHoleAccretionFactor,fac,
                   f_disk_for_bhar,bh_mass,fgas_for_bhar,f0_for_bhar,mdisk_for_bhar,rmax_for_bhar_units);
#endif
        } // if(f_disk_for_bhar<=0)

    } // if(BlackholeTempInfo[i].Mgas_in_Kernel > 0)
#endif // ifdef BH_GRAVACCRETION
    
    
    
#ifdef BH_BONDI
    /* heres where we calculate the Bondi accretion rate, if that's going to be used */
    bhvel = 0;
#if (BH_BONDI != 1)
    for(k=0;k<3;k++) bhvel += BlackholeTempInfo[i].BH_SurroundingGasVel[k]*BlackholeTempInfo[i].BH_SurroundingGasVel[k];
#endif
    rho = BPP(n).DensAroundStar * All.cf_a3inv; /* we want all quantities in physical units */
    soundspeed = GAMMA*GAMMA_MINUS1 * BlackholeTempInfo[i].BH_InternalEnergy; // this is in physical units now
    double fac = pow(soundspeed+bhvel, 1.5);
    if(fac > 0)
    {
        double AccretionFactor = All.BlackHoleAccretionFactor;
#if (BH_BONDI == 2)
        /* variable-alpha model (Booth&Schaye 2009): now All.BlackHoleAccretionFactor is the slope of the density dependence */
        AccretionFactor = 1.0;
        if(rho > All.PhysDensThresh)
            AccretionFactor = pow(rho/All.PhysDensThresh, All.BlackHoleAccretionFactor);
#endif
        mdot = 4. * M_PI * AccretionFactor * All.G * All.G * BPP(n).BH_Mass * BPP(n).BH_Mass * rho / fac;
    }
#endif // ifdef BH_BONDI
    
    
/* DAA: note that we should have mdot=0 here 
 *      otherwise the mass accreted is counted twice 
 *      -->  mdot*dt in set_blackhole_new_mass
 *      -->  accreted_BH_mass in blackhole_swallow_and_kick
 */
#ifdef BH_GRAVCAPTURE_GAS
    mdot = 0; /* force mdot=0 despite any earlier settings here.  If this is set, we have to wait to swallow step to eval mdot. */
    //mdot = BlackholeTempInfo[i].mass_to_swallow_edd / dt;       /* TODO: this can still greatly exceed eddington... */
#endif //ifdef BH_GRAVCAPTURE_GAS


#ifdef BH_ALPHADISK_ACCRETION
    /* use the mass in the accretion disk from the previous timestep to determine the BH accretion rate */
    /* note that if the alpha-disk is self-gravitating, it limits its mass, so we limit accretion into it beyond a certain point */
    double x_MdiskSelfGravLimiter = BPP(n).BH_Mass_AlphaDisk / (BPP(n).BH_Mass_AlphaDisk + BPP(n).BH_Mass);
    if(x_MdiskSelfGravLimiter > 20.) {mdot=0;} else {mdot *= exp(-0.5*x_MdiskSelfGravLimiter*x_MdiskSelfGravLimiter);}
    BlackholeTempInfo[i].mdot_alphadisk = mdot;     /* if BH_GRAVCAPTURE_GAS is off, this gets the accretion rate */
    mdot = 0;
    if(BPP(n).BH_Mass_AlphaDisk > 0)
    {
        /* this below is a more complicated expression using the outer-disk expression from Shakura & Sunyaev. Simpler expression
            below captures the same physics with considerably less potential to extrapolate to rather odd scalings in extreme regimes */
        
        mdot = All.BlackHoleAccretionFactor *
            (2.45 * (SOLAR_MASS/All.UnitMass_in_g)/(SEC_PER_YEAR/All.UnitTime_in_s)) * // normalization
            pow( 0.1 , 8./7.) * // viscous disk 'alpha'
            pow( BPP(n).BH_Mass*All.UnitMass_in_g / (All.HubbleParam * 1.0e8*SOLAR_MASS) , -5./14. ) * // mbh dependence
            pow( BPP(n).BH_Mass_AlphaDisk*All.UnitMass_in_g / (All.HubbleParam * 1.0e8*SOLAR_MASS) , 10./7. ) * // m_disk dependence
            pow( DMIN(0.2,DMIN(PPP[n].Hsml,All.ForceSoftening[5])*All.cf_atime*All.UnitLength_in_cm/(All.HubbleParam * 3.086e18)) , -25./14. ); // r_disk dependence
        
        double t_yr = SEC_PER_YEAR / (All.UnitTime_in_s / All.HubbleParam);
        mdot = BPP(n).BH_Mass_AlphaDisk / (4.2e7 * t_yr) * pow(BPP(n).BH_Mass_AlphaDisk/(BPP(n).BH_Mass_AlphaDisk+BPP(n).BH_Mass), 0.4);
        
#ifdef SINGLE_STAR_FORMATION
	//mdot = All.BlackHoleAccretionFactor * 1.0e-5 * BPP(n).BH_Mass_AlphaDisk / (SEC_PER_YEAR/All.UnitTime_in_s) * pow(BPP(n).BH_Mass_AlphaDisk/(BPP(n).BH_Mass_AlphaDisk+BPP(n).BH_Mass),2);
	//double t_acc_bh=(1.0e5 * t_yr); //accretion timescale set to 100kyr
	//double t_acc_bh= 1.11072 * pow((BPP(n).BH_Mass_AlphaDisk+BPP(n).BH_Mass)*All.GravityConstantInternal, -0.5)*pow(All.ForceSoftening[5],1.5)/All.UnitTime_in_s; /* Accretion timescale is the freefall time */
	double t_acc_bh= 18.006* pow((BPP(n).BH_Mass_AlphaDisk+BPP(n).BH_Mass)*All.GravityConstantInternal*All.ForceSoftening[5], 0.5)*pow(3.0e4/All.UnitVelocity_in_cm_per_s,-2.0)/All.UnitTime_in_s; /* Accretion timescale is 1/alpha*(t_cross/t_orb)^2*t_orb where t_orb is roughly the 2 freefall time. For t_cross=2R/cs we will use cs=300m/s (T=20 K gas) and for alpha we will use 0.1 (the uncertainty in alpha means it does not really matter if cs is incorrect)*/
	double sink_dt = (BPP(n).TimeBin ? (1 << BPP(n).TimeBin) : 0) * All.Timebase_interval; // sink timestep
	t_acc_bh=DMAX(10*sink_dt,t_acc_bh); /* Make sure that the accretion timescale is at least 10 times the particle's timestep */
	mdot = All.BlackHoleAccretionFactor * BPP(n).BH_Mass_AlphaDisk / (t_acc_bh) * pow(BPP(n).BH_Mass_AlphaDisk/(BPP(n).BH_Mass_AlphaDisk+BPP(n).BH_Mass), 0.4);
#endif

    }
#endif
    
    
    
#ifdef BH_SUBGRIDBHVARIABILITY
    /* account for sub-grid accretion rate variability */
    if((mdot>0)&&(dt>0)&&(P[n].DensAroundStar>0))
    {
        omega_ri=sqrt(All.G*P[n].DensAroundStar*All.cf_a3inv); /* dynamical frequency in physical units */
        n0_sgrid_elements=10.0; norm_subgrid=0.55*3.256/sqrt(n0_sgrid_elements);
        nsubgridvar=(long)P[n].ID + (long)(All.Time/((All.TimeMax-All.TimeBegin)/1000.));
        /* this line just allows 'resetting' the time constants every so often, while generally keeping them steady */
        double fac;
        if(All.ComovingIntegrationOn)
            fac=omega_ri * (evaluate_stellar_age_Gyr(0.001)/(0.001*All.UnitTime_in_Megayears/All.HubbleParam));
        else
            fac=omega_ri * All.Time; /* All.Time is physical time, this is good */
        random_generator_forbh=gsl_rng_alloc(gsl_rng_ranlxd1);
        gsl_rng_set(random_generator_forbh,nsubgridvar);
        if(n0_sgrid_elements >= 1) {
            for(jsub=1;jsub<=n0_sgrid_elements;jsub++) {
                varsg1=gsl_rng_uniform(random_generator_forbh);
                varsg2=gsl_ran_ugaussian(random_generator_forbh);
                time_var_subgridvar=fac*pow(omega_ri*dt,-((float)jsub)/n0_sgrid_elements) + 2.*M_PI*varsg1;
                mdot *= exp( norm_subgrid*cos(time_var_subgridvar)*varsg2 );
            }}
        gsl_rng_free(random_generator_forbh);
    } // if(mdot > 0)
#endif



#ifdef BH_ALPHADISK_ACCRETION
    /* if there -is- an alpha-disk, protect the alpha-disk not to be over-depleted (i.e. overshooting into negative alpha-disk masses) */
    if(dt>0)
    {
#if defined(BH_WIND_CONTINUOUS) || defined(BH_WIND_SPAWN)
        if(mdot > BPP(n).BH_Mass_AlphaDisk/dt*All.BAL_f_accretion) mdot = BPP(n).BH_Mass_AlphaDisk/dt*All.BAL_f_accretion;
#else 
        if(mdot > BPP(n).BH_Mass_AlphaDisk/dt) mdot = BPP(n).BH_Mass_AlphaDisk/dt;
#endif
    }
#ifdef BH_WIND_KICK
    /* DAA: correct the mdot into the accretion disk for the mass loss in "kick" winds 
       Note that for BH_WIND_CONTINUOUS the wind mass is removed in the final loop */
    BlackholeTempInfo[i].mdot_alphadisk *= All.BAL_f_accretion;
#endif

#else // BH_ALPHADISK_ACCRETION

#if defined(BH_WIND_CONTINUOUS) || defined(BH_WIND_KICK) || defined(BH_WIND_SPAWN)
    /* if there is no alpha-disk, the BHAR defined above is really an mdot into the accretion disk. the rate -to the hole- should be corrected for winds */
    mdot *= All.BAL_f_accretion;
#endif
#endif // BH_ALPHADISK_ACCRETION


    
#ifdef BH_ENFORCE_EDDINGTON_LIMIT
    /* cap the maximum at the Eddington limit */
    if(mdot > All.BlackHoleEddingtonFactor * meddington) {mdot = All.BlackHoleEddingtonFactor * meddington;}
#endif
    
    /* alright, now we can FINALLY set the BH accretion rate */
    if(isnan(mdot)) {mdot=0;}
    BPP(n).BH_Mdot = DMAX(mdot,0);
}



void set_blackhole_new_mass(int i, int n, double dt)
{
#ifdef BH_ALPHADISK_ACCRETION
    double dm_alphadisk;
#endif
    
    /* Update the BH_Mass and the BH_Mass_AlphaDisk
     TODO: in principle, when using gravitational capture, we should NOT update the mass here 
     DAA: note that this is fine now because mdot=0 (or mdot_alphadisk=0 if BH_ALPHADISK_ACCRETION) for BH_GRAVCAPTURE_GAS above */
    if(BPP(n).BH_Mdot <= 0) {BPP(n).BH_Mdot=0;}

/* DAA:
 for BH_WIND_CONTINUOUS or BH_WIND_SPAWN
    - we accrete the winds first, either explicitly to the BH or implicitly into the disk -
    - then we remove the wind mass in the final loop
 for BH_WIND_KICK
    - the BH grows according to the mdot set above (including the mass loss in winds)
    - if there is an alpha-disk, the mass going out in winds has been subtracted from mdot_alphadisk  
 for BH_WIND_KICK + BH_GRAVCAPTURE_GAS 
    - the ratio of BH/disk growth-to-outflow rate is enforced explicitly in blackhole_swallow_and_kick */ 

#ifdef BH_ALPHADISK_ACCRETION

    BPP(n).BH_Mass += BPP(n).BH_Mdot * dt;   // mdot comes from the disk - no mass loss here regarless of BAL model -

    dm_alphadisk = ( BlackholeTempInfo[i].mdot_alphadisk - BPP(n).BH_Mdot ) * dt;

    if(dm_alphadisk < -BPP(n).BH_Mass_AlphaDisk) {BPP(n).BH_Mass_AlphaDisk=0;} else {BPP(n).BH_Mass_AlphaDisk += dm_alphadisk;}
    if(BPP(n).BH_Mass_AlphaDisk<0) {BPP(n).BH_Mass_AlphaDisk=0;}
    if(P[n].Mass<0) {P[n].Mass=0;}

#else // #ifdef BH_ALPHADISK_ACCRETION

#if defined(BH_WIND_CONTINUOUS) || defined(BH_WIND_SPAWN)
    // accrete the winds first, then remove the wind mass in the final loop
    BPP(n).BH_Mass += BPP(n).BH_Mdot * dt / All.BAL_f_accretion; 
#else
    BPP(n).BH_Mass += BPP(n).BH_Mdot * dt;
#endif

#endif // #else BH_ALPHADISK_ACCRETION

}



#if defined(BH_DRAG) || defined(BH_DYNFRICTION)
void set_blackhole_drag(int i, int n, double dt)
{
    
    int k;
    double meddington;
    
    meddington = bh_eddington_mdot(BPP(n).BH_Mass);
    
#ifdef BH_DRAG
    /* add a drag force for the black-holes, accounting for the accretion */
    if((dt>0)&&(BPP(n).BH_Mass>0))
    {
        double fac = BPP(n).BH_Mdot * dt / BPP(n).BH_Mass;
#if (BH_DRAG == 2)
        /* make the force stronger to keep the BH from wandering */
        fac = meddington * dt / BPP(n).BH_Mass;
#endif
        if(fac>1) fac=1;
        for(k = 0; k < 3; k++)
            P[n].GravAccel[k] += All.cf_atime*All.cf_atime * fac * BlackholeTempInfo[i].BH_SurroundingGasVel[k] / dt;
    } // if((dt>0)&&(BPP(n).BH_Mass>0))
#endif
    
    
    
#ifdef BH_DYNFRICTION
    double bh_mass, x;
    
    if(BlackholeTempInfo[i].DF_mmax_particles>0) /* found something in the kernel, we can proceed */
    {
        /* averaged value for colomb logarithm and integral over the distribution function */
        /* fac_friction = log(lambda) * [erf(x) - 2*x*exp(-x^2)/sqrt(pi)]                  */
        /*       lambda = b_max * v^2 / G / (M+m)                                          */
        /*        b_max = Size of system (e.g. Rvir)                                       */
        /*            v = Relative velocity of BH with respect to the environment          */
        /*            M = Mass of BH                                                       */
        /*            m = individual mass elements composing the large system (e.g. m<<M)  */
        /*            x = v/sqrt(2)/sigma                                                  */
        /*        sigma = width of the max. distr. of the host system                      */
        /*                (e.g. sigma = v_disp / 3                                         */
        bh_mass = BPP(n).BH_Mass;
#ifdef BH_ALPHADISK_ACCRETION
        bh_mass += BPP(n).BH_Mass_AlphaDisk;
#endif
        double bhvel_df=0; for(k=0;k<3;k++) bhvel_df += BlackholeTempInfo[i].DF_mean_vel[k]*BlackholeTempInfo[i].DF_mean_vel[k];
        double fac, fac_friction;
        /* First term is approximation of the error function */
        fac = 8 * (M_PI - 3) / (3 * M_PI * (4. - M_PI));
        x = sqrt(bhvel_df) / (sqrt(2) * BlackholeTempInfo[i].DF_rms_vel);
        fac_friction =  x / fabs(x) * sqrt(1 - exp(-x * x * (4 / M_PI + fac * x * x) / (1 + fac * x * x))) - 2 * x / sqrt(M_PI) * exp(-x * x);
        /* now the Coulomb logarithm */
        fac = 50. * 3.086e21 / (All.UnitLength_in_cm/All.HubbleParam); /* impact parameter */
        fac_friction *= log(1. + fac * bhvel_df / (All.G * bh_mass));        
        /* now we add a correction to only apply this force if M_BH is not >> <m_particles> */
        fac_friction *= 1 / (1 + bh_mass / (5.*BlackholeTempInfo[i].DF_mmax_particles));
        /* now the dimensional part of the force */
        double Mass_in_Kernel = BlackholeTempInfo[i].Malt_in_Kernel;
#if (BH_DYNFRICTION == 1)    // DAA: dark matter + stars
        Mass_in_Kernel = BlackholeTempInfo[i].Malt_in_Kernel - BlackholeTempInfo[i].Mgas_in_Kernel;
#elif (BH_DYNFRICTION == 2)  // DAA: stars only
        Mass_in_Kernel = BlackholeTempInfo[i].Mstar_in_Kernel;
#endif
#if (BH_DYNFRICTION > 2)
        Mass_in_Kernel *= BH_DYNFRICTION;
#endif
        //fac = BlackholeTempInfo[i].Malt_in_Kernel / ( (4*M_PI/3) * pow(PPP[n].Hsml*All.cf_atime,3) ); /* mean density of all mass inside kernel */
        fac = Mass_in_Kernel / ( (4*M_PI/3) * pow(PPP[n].Hsml*All.cf_atime,3) ); /* mean density of all mass inside kernel */
        fac_friction *= 4*M_PI * All.G * All.G * fac * bh_mass / (bhvel_df*sqrt(bhvel_df));
        /* now apply this to the actual acceleration */
        if(fac_friction<0) fac_friction=0; if(isnan(fac_friction)) fac_friction=0;
#if (BH_REPOSITION_ON_POTMIN == 2)
        /* ok, here we have a special catch - the friction isn't standard dynamical friction, but rather we are already moving
            towards a potential mininum and want to be sure that we don't overshoot or retain large velocities that will
            launch us out, so we want the BH to 'relax' towards moving with the local flow */
        if(bhvel_df > 0 && dt > 0)
        {
            double dv_magnitude=sqrt(bhvel_df)*All.cf_atime, fac_vel=0, afac_vel=0; // physical velocity difference between 'target' and BH
            afac_vel = All.G * Mass_in_Kernel / pow(PPP[n].Hsml*All.cf_atime,2); // GMenc/r^2 estimate of local acceleration //
            afac_vel = DMIN(dv_magnitude/(3.155e13/(All.UnitTime_in_s/All.HubbleParam)) , DMAX( DMIN(DMAX(-2.*BPP(n).BH_MinPot/(PPP[n].Hsml*All.cf_atime*All.cf_atime), 0), 10.*dv_magnitude/dt), afac_vel)); // free-fall-acceleration [checked-to-zero], limited to multiple of actual vel difference in timestep
            fac_vel = afac_vel * dt / dv_magnitude; // rate at which de-celeration/damping occurs
            if(fac_vel > 1.e-4) {fac_vel = 1.-exp(-fac_vel);}
            for(k = 0; k < 3; k++) {P[n].Vel[k] += BlackholeTempInfo[i].DF_mean_vel[k]*All.cf_atime * fac_vel;}
        }
#else
        for(k = 0; k < 3; k++)
            P[n].GravAccel[k] += All.cf_atime*All.cf_atime * fac_friction * BlackholeTempInfo[i].DF_mean_vel[k];
#endif
    }
#endif
    
    
}
#endif


#if defined(BH_PHOTONMOMENTUM) || defined(BH_WIND_CONTINUOUS)
void set_blackhole_long_range_rp(int i, int n)
{
    /* pre-set quantities needed for long-range radiation pressure terms */
    int k; double fac; P[n].BH_disk_hr=1/3; P[n].GradRho[0]=P[n].GradRho[1]=0; P[n].GradRho[2]=1;
    if(BlackholeTempInfo[i].Mgas_in_Kernel > 0)
    {
        /* estimate h/R surrounding the BH from the gas density gradients */
        fac=0; for(k=0;k<3;k++) {fac += BlackholeTempInfo[i].GradRho_in_Kernel[k]*BlackholeTempInfo[i].GradRho_in_Kernel[k];}
        P[n].BH_disk_hr = P[n].DensAroundStar / (PPP[n].Hsml * sqrt(fac)) * 1.3;
        /* 1.3 factor from integrating exponential disk with h/R=const over gaussian kernel, for width=1/3 (quintic kernel); everything here is in code units, comes out dimensionless */
        
        /* use the gradrho vector as a surrogate to hold the orientation of the angular momentum 
          (this is done because the long-range radiation routines for the BH require the angular momentum vector for non-isotropic emission) */
        fac=0; for(k=0;k<3;k++) {fac += BlackholeTempInfo[i].Jgas_in_Kernel[k]*BlackholeTempInfo[i].Jgas_in_Kernel[k];}
        fac=sqrt(fac); if(fac>0) {for(k=0;k<3;k++) {P[n].GradRho[k] = BlackholeTempInfo[i].Jgas_in_Kernel[k]/fac;}}
        /* now, the P[n].GradRho[k] field for the BH holds the orientation of the UNIT angular momentum vector
         NOTE it is important that HARD-WIRED into the code, this blackhole calculation comes after the density calculation
         but before the forcetree update and walk; otherwise, this won't be used correctly there */
    }
}
#endif // if defined(BH_PHOTONMOMENTUM) || defined(BH_WIND_CONTINUOUS)








void blackhole_final_operations(void)
{
    int i, k, n, bin;
    double  dt;
    double mass_disk, mdot_disk, MgasBulge, MstarBulge, r0;
#ifdef SINGLE_STAR_PROMOTION
    int count_bhelim=0, tot_bhelim;
#endif

#ifdef BH_REPOSITION_ON_POTMIN
    for(n = FirstActiveParticle; n >= 0; n = NextActiveParticle[n])
        if(P[n].Type == 5)
            if(BPP(n).BH_MinPot < 0.5 * BHPOTVALUEINIT)
            {
                double fac_bh_shift=0;
#if (BH_REPOSITION_ON_POTMIN == 2)
                dt = (P[n].TimeBin ? (1 << P[n].TimeBin) : 0) * All.Timebase_interval / All.cf_hubble_a;
                double dr_min=0; for(k=0;k<3;k++) {dr_min+=(BPP(n).BH_MinPotPos[k]-P[n].Pos[k])*(BPP(n).BH_MinPotPos[k]-P[n].Pos[k]);}
                if(dr_min > 0 && dt > 0)
                {
                    dr_min=sqrt(dr_min)*All.cf_atime; // offset to be covered
                    // in general don't let the shift be more than 0.5 of the distance in a single timestep, but let it move at reasonable ~few km/s speeds minimum, and cap at the free-fall velocity //
                    double dv_shift = sqrt(DMAX(-2.*BPP(n).BH_MinPot/All.cf_atime , 0)); // free-fall velocity, in [physical] code units, capped zero
                    dv_shift = DMAX(DMIN(dv_shift, dr_min/dt), 10. * 1.e5/All.UnitVelocity_in_cm_per_s); // set minimum at ~10 km/s, max at speed which 'jumps' full distance
                    fac_bh_shift = dv_shift * dt / dr_min; // dimensionless shift factor
                    if(fac_bh_shift > 1.e-4) {fac_bh_shift = 1.-exp(-fac_bh_shift);} // make sure we can't overshoot by using this smooth interpolation function
                }
#elif (BH_REPOSITION_ON_POTMIN == 1)
                fac_bh_shift = 0.5; // jump a bit more smoothly, still instantly but not the whole way
#else
                fac_bh_shift = 1.0; // jump all the way
#endif
                for(k = 0; k < 3; k++) {P[n].Pos[k] += (BPP(n).BH_MinPotPos[k]-P[n].Pos[k]) * fac_bh_shift;}
            }
#endif
    
    for(n = 0; n < TIMEBINS; n++) {if(TimeBinActive[n]) {TimeBin_BH_mass[n] = 0; TimeBin_BH_dynamicalmass[n] = 0; TimeBin_BH_Mdot[n] = 0; TimeBin_BH_Medd[n] = 0;}}
    
    for(i=0; i<N_active_loc_BHs; i++)
    {
        n = BlackholeTempInfo[i].index;
        if(((BlackholeTempInfo[i].accreted_Mass>0)||(BlackholeTempInfo[i].accreted_BH_Mass>0)) && P[n].Mass > 0)
        {
            for(k = 0; k < 3; k++)
            {
                P[n].Vel[k] = (P[n].Vel[k]*P[n].Mass + BlackholeTempInfo[i].accreted_momentum[k]) / (BlackholeTempInfo[i].accreted_Mass + P[n].Mass);
            } //for(k = 0; k < 3; k++)
            P[n].Mass += BlackholeTempInfo[i].accreted_Mass;
            BPP(n).BH_Mass += BlackholeTempInfo[i].accreted_BH_Mass;
        } // if(((BlackholeTempInfo[n].accreted_Mass>0)||(BlackholeTempInfo[n].accreted_BH_Mass>0)) && P[n].Mass > 0)
#ifdef SINGLE_STAR_STRICT_ACCRETION
	//        P[n].SinkRadius = DMAX(DMAX(P[n].SinkRadius, All.G * P[n].Mass / (1.0e12 / (All.UnitVelocity_in_cm_per_s * All.UnitVelocity_in_cm_per_s))), All.ForceSoftening[5]);        // accretion radius is at least the Bondi radius for c_s = 100km/s
	P[n].SinkRadius = DMAX(P[n].SinkRadius, All.ForceSoftening[5]);
#ifdef SINGLE_STAR_TIDAL_ACCRETION // This is a novel determination of the sink radius, scaled to the sink particle's Hill sphere - for scale-free simulations, not necessary full-physics star formation sims - MYG
        double tidal_field = sqrt(P[n].tidal_tensorps[0][0]*P[n].tidal_tensorps[0][0] + P[n].tidal_tensorps[1][1]*P[n].tidal_tensorps[1][1] + P[n].tidal_tensorps[2][2]*P[n].tidal_tensorps[2][2]);
        P[n].SinkRadius = DMAX(All.SofteningTable[5], pow(All.G*P[n].Mass/tidal_field, 1./3)/10); // /10 here is arbitrary - set to taste
#endif
#endif

        /* Correct for the mass loss due to radiation and BAL winds */
#ifndef WAKEUP
        dt = (P[n].TimeBin ? (1 << P[n].TimeBin) : 0) * All.Timebase_interval / All.cf_hubble_a;
#else
        dt = P[n].dt_step * All.Timebase_interval / All.cf_hubble_a;
#endif //ifndef WAKEUP
        
        /* always substract the radiation energy from BPP(n).BH_Mass && P[n].Mass */
        double dm = BPP(n).BH_Mdot * dt;
        double radiation_loss = All.BlackHoleRadiativeEfficiency * dm;
        if(radiation_loss > DMIN(P[n].Mass,BPP(n).BH_Mass)) radiation_loss = DMIN(P[n].Mass,BPP(n).BH_Mass);
        P[n].Mass -= radiation_loss;
        BPP(n).BH_Mass -= radiation_loss;
        
        /* subtract the BAL wind mass from P[n].Mass && (BPP(n).BH_Mass || BPP(n).BH_Mass_AlphaDisk) */
        // DAA: note that the mass loss in winds for BH_WIND_KICK has already been taken into account
#ifdef BH_WIND_CONTINUOUS
        double dm_wind = (1.-All.BAL_f_accretion) / All.BAL_f_accretion * dm;
        if(dm_wind > P[n].Mass) {dm_wind = P[n].Mass;}
        
#ifdef BH_ALPHADISK_ACCRETION
        if(dm_wind > BPP(n).BH_Mass_AlphaDisk) {dm_wind = BPP(n).BH_Mass_AlphaDisk;}
        P[n].Mass -= dm_wind;
        BPP(n).BH_Mass_AlphaDisk -= dm_wind;
#else
        if(dm_wind > BPP(n).BH_Mass) {dm_wind = BPP(n).BH_Mass;}
        P[n].Mass -= dm_wind;
        BPP(n).BH_Mass -= dm_wind;
#endif
#endif // ifdef BH_WIND_CONTINUOUS
        
#ifdef BH_WIND_SPAWN
        /* DAA: for wind spawning, we only need to subtract the BAL wind mass from BH_Mass (or BH_Mass_AlphaDisk)
            --> wind mass subtracted from P.Mass in blackhole_spawn_particle_wind_shell()  */
        double dm_wind = (1.-All.BAL_f_accretion) / All.BAL_f_accretion * dm;
        if(dm_wind > P[n].Mass) {dm_wind = P[n].Mass;}
#if defined(BH_ALPHADISK_ACCRETION)
        if(dm_wind > BPP(n).BH_Mass_AlphaDisk) {dm_wind = BPP(n).BH_Mass_AlphaDisk;}
        BPP(n).BH_Mass_AlphaDisk -= dm_wind;
#else
        if(dm_wind > BPP(n).BH_Mass) {dm_wind = BPP(n).BH_Mass;}
        BPP(n).BH_Mass -= dm_wind;
#endif
        BPP(n).unspawned_wind_mass += dm_wind;
#endif
        
        /* dump the results to the 'blackhole_details' files */
        mass_disk=0; mdot_disk=0; MgasBulge=0; MstarBulge=0;
        r0 = PPP[n].Hsml * All.cf_atime;
#ifdef BH_ALPHADISK_ACCRETION
        mass_disk = BPP(n).BH_Mass_AlphaDisk;
        mdot_disk = BlackholeTempInfo[i].mdot_alphadisk;
#endif
#ifdef BH_GRAVACCRETION
        MgasBulge = BlackholeTempInfo[i].MgasBulge_in_Kernel;
        MstarBulge = BlackholeTempInfo[i].MstarBulge_in_Kernel;
#endif

//#ifndef IO_REDUCED_MODE   DAA-IO: BH_OUTPUT_MOREINFO overrides IO_REDUCED_MOD
#if defined(BH_OUTPUT_MOREINFO)
        fprintf(FdBlackHolesDetails, "%2.12f %u  %g %g %g %g %g %g  %g %g %g %g %g %g %g %g  %2.10f %2.10f %2.10f  %2.7f %2.7f %2.7f  %g %g %g  %g %g %g\n",
                All.Time, P[n].ID,  P[n].Mass, BPP(n).BH_Mass, mass_disk, BPP(n).BH_Mdot, mdot_disk, dt,
                BPP(n).DensAroundStar*All.cf_a3inv, BlackholeTempInfo[i].BH_InternalEnergy, BlackholeTempInfo[i].Sfr_in_Kernel,
                BlackholeTempInfo[i].Mgas_in_Kernel, BlackholeTempInfo[i].Mstar_in_Kernel, MgasBulge, MstarBulge, r0,
                P[n].Pos[0], P[n].Pos[1], P[n].Pos[2],  P[n].Vel[0], P[n].Vel[1], P[n].Vel[2], 
                BlackholeTempInfo[i].Jgas_in_Kernel[0], BlackholeTempInfo[i].Jgas_in_Kernel[1], BlackholeTempInfo[i].Jgas_in_Kernel[2],
                BlackholeTempInfo[i].Jstar_in_Kernel[0], BlackholeTempInfo[i].Jstar_in_Kernel[1], BlackholeTempInfo[i].Jstar_in_Kernel[2] );
#else
#ifndef IO_REDUCED_MODE
        fprintf(FdBlackHolesDetails, "BH=%u %g %g %g %g %g %g %g %g   %2.7f %2.7f %2.7f\n",
                P[n].ID, All.Time, BPP(n).BH_Mass, mass_disk, P[n].Mass, BPP(n).BH_Mdot, mdot_disk,              
                P[n].DensAroundStar*All.cf_a3inv, BlackholeTempInfo[i].BH_InternalEnergy,             // DAA: DensAroundStar is actually not defined in BHP->BPP...
                P[n].Pos[0], P[n].Pos[1], P[n].Pos[2]);
#endif
#endif
        
        bin = P[n].TimeBin;
        TimeBin_BH_mass[bin] += BPP(n).BH_Mass;
        TimeBin_BH_dynamicalmass[bin] += P[n].Mass;
        TimeBin_BH_Mdot[bin] += BPP(n).BH_Mdot;
        if(BPP(n).BH_Mass > 0) {TimeBin_BH_Medd[bin] += BPP(n).BH_Mdot / BPP(n).BH_Mass;}
        

#ifdef SINGLE_STAR_PROMOTION
        double m_initial = DMAX(1.e-37 , (BPP(n).BH_Mass - dm)); // mass before the accretion
        double mu = DMAX(0, dm/m_initial); // relative mass accreted
        //double m_initial_msun = m_initial * (All.UnitMass_in_g/(All.HubbleParam * SOLAR_MASS));
        //double t_premainseq = 50.0e6 / pow(m_initial_msun,2.5); // lifetime at previous mass
        //t_premainseq /= (All.UnitTime_in_s/(All.HubbleParam * SEC_PER_YEAR));
        ///* compute evolution of 'tracker' [here modeled on self-similar contraction along Hayashi line on Kelvin-Helmholtz timescale, with accretion 'puffing up' the star */
        //BPP(n).PreMainSeq_Tracker = (BPP(n).PreMainSeq_Tracker * exp(-dt/t_premainseq) + mu) / (1. + mu);
        
        double m_solar = BPP(n).BH_Mass * (All.UnitMass_in_g/(All.HubbleParam * SOLAR_MASS)); // mass in solar units
        double T4000_4 = pow(m_solar, 0.55); // (temperature/4000K)^4 along Hayashi track
        double lum_sol = 0.0; // get the main-sequence luminosity
        if(m_solar > 0.012)
        {
            if(m_solar < 0.43) {lum_sol = 0.185 * m_solar*m_solar;}
            else if(m_solar < 2.) {lum_sol = m_solar*m_solar*m_solar*m_solar;}
            else if(m_solar < 53.9) {lum_sol = 1.5 * m_solar*m_solar*m_solar * sqrt(m_solar);}
            else {lum_sol = 32000. * m_solar;}
        }
        double R_Hayashi_Henyey = 2.1 * sqrt(lum_sol / T4000_4); // size below which, at the temperature above, contraction must occur along the Henyey track at constant luminosity
        double t_R_evol = 0, contraction_factor = 0; // timescale for contraction
        if(BPP(n).ProtoStellar_Radius <= R_Hayashi_Henyey)
        {
            // currently on Henyey track, contracting at constant Luminosity
            t_R_evol = 1.815e7 * m_solar*m_solar / (BPP(n).ProtoStellar_Radius * lum_sol) / (All.UnitTime_in_s/(All.HubbleParam * SEC_PER_YEAR)); // contraction timescale
            contraction_factor = 1. / (1. + dt/t_R_evol);
        } else {
            // currently on Hayashi track, contracting at constant Temperature
            t_R_evol = 8.021e7 * m_solar*m_solar / (BPP(n).ProtoStellar_Radius*BPP(n).ProtoStellar_Radius*BPP(n).ProtoStellar_Radius * T4000_4) / (All.UnitTime_in_s/(All.HubbleParam * SEC_PER_YEAR)); // contraction timescale
            contraction_factor = 1. / pow(1 + 3.*dt/t_R_evol, 1./3.);
        }
        double r_new = 100. * m_solar; // size if newly-formed protostar
        BPP(n).ProtoStellar_Radius = (BPP(n).ProtoStellar_Radius * contraction_factor + r_new * mu) / (1. + mu); // new size (contraction + accretion both accounted for)
        double R_main_sequence_ignition; // main sequence radius - where contraction should halt
        if(m_solar <= 1) {R_main_sequence_ignition = pow(m_solar,0.8);} else {R_main_sequence_ignition = pow(m_solar,0.57);}
        
        //if(BPP(n).PreMainSeq_Tracker < 0.36787944117144233) // if drops below 1/e [one t_premainseq timescale, in the absence of accretion], promote //
        if(BPP(n).ProtoStellar_Radius <= R_main_sequence_ignition)
        {
            P[n].Type = 4; // convert type
            count_bhelim++; // note one fewer BH-type particle
            P[n].StellarAge = All.Time; // mark the new ZAMS age according to the current time
            P[n].Mass = DMAX(P[n].Mass , BPP(n).BH_Mass + BPP(n).BH_Mass_AlphaDisk);
        }
#endif
        
    } // for(i=0; i<N_active_loc_BHs; i++)
    
    
#ifdef SINGLE_STAR_PROMOTION
    MPI_Allreduce(&count_bhelim, &tot_bhelim, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
    All.TotBHs -= tot_bhelim; 
#endif
    
}


#endif // BLACK_HOLES
