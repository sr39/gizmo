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
 *   It was based on a similar file in GADGET3 by Volker Springel,
 *   but the physical modules for black hole accretion and feedback have been
 *   replaced, and the algorithms for their coupling are new to GIZMO.  This file was modified
 *   on 1/9/15 by Paul Torrey (ptorrey@mit.edu) for clarity by parsing the existing code into
 *   smaller files and routines.  Some communication and black hole structures were modified
 *   to reduce memory usage. Cleanup, de-bugging, and consolidation of routines by Xiangcheng Ma
 *   (xchma@caltech.edu) followed on 05/15/15; re-integrated by PFH. Massive updates in 2019
 *   from David Guszejnov and Mike Grudic to incorporate and develop single star modules, and
 *   from PFH to integrate those and re-write the parallelism entirely to conform to the newer
 *   code standards and be properly multi-threaded. 
 */

#ifdef BLACK_HOLES


/*  This is the master routine for the BH physics modules.
 *  It is called in calculate_non_standard_physics in run.c */
void blackhole_accretion(void)
{
    if(All.TimeStep == 0.) return; /* no evolution */
    PRINT_STATUS("Black-hole operations begin...");
    //     long i; for(i = 0; i < NumPart; i++) {P[i].SwallowID = 0;} /* zero out accretion */ //  This zero-out loop is effectively performed in density.c now, only on gas particles that are actually going to be looked at this timestep, to reduce overhead when only a few particles are active
    blackhole_start();              /* allocates and cleans BlackholeTempInfo struct */
    
    /* this is the PRE-PASS loop.*/
    blackhole_environment_loop();    /* populates BlackholeTempInfo based on surrounding gas (blackhole_environment.c).
                                      If using gravcap the desired mass accretion rate is calculated and set to BlackholeTempInfo.mass_to_swallow_edd */
#if defined(BH_GRAVACCRETION) && (BH_GRAVACCRETION == 0)
    blackhole_environment_second_loop();    /* Here we compute quantities that require knowledge of previous environment variables --> Bulge-Disk kinematic decomposition for gravitational torque accretion  */
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
    PRINT_STATUS(" ..closing black-hole operations");
    //    for(i = 0; i < NumPart; i++) {P[i].SwallowID = 0;P[i].SwallowEnergy = MAX_REAL_NUMBER;} /* re-zero accretion */
}





/* calculate escape velocity to use for bounded-ness calculations relative to the BH */
double bh_vesc(int j, double mass, double r_code, double bh_softening)
{
    double cs_to_add_km_s = 10.0; /* we can optionally add a 'fudge factor' to v_esc to set a minimum value; useful for galaxy applications */
#if defined(SINGLE_STAR_SINK_DYNAMICS) || defined(BH_SEED_GROWTH_TESTS)
    cs_to_add_km_s = 0.0;
#endif
    cs_to_add_km_s *= 1.e5/All.UnitVelocity_in_cm_per_s;
    double m_eff = mass+P[j].Mass;
    if(P[j].Type==0)
    {
#if defined(BH_SEED_GROWTH_TESTS) || defined(SINGLE_STAR_SINK_DYNAMICS)
        m_eff += 4.*M_PI * r_code*r_code*r_code * SphP[j].Density; // assume an isothermal sphere interior, for Shu-type solution
#endif
    }
#ifdef SINGLE_STAR_SINK_DYNAMICS
    double hinv; if(P[j].Type==0) hinv=1/All.ForceSoftening[5];
    if(r_code < 1/hinv) {return sqrt(-2*All.G*m_eff*kernel_gravity(r_code*hinv, hinv, hinv*hinv*hinv, -1));}
#endif    
    return sqrt(2.0*All.G*(m_eff)/(r_code*All.cf_atime) + cs_to_add_km_s*cs_to_add_km_s);
}



/* check whether a particle is sufficiently bound to the BH to qualify for 'gravitational capture' */
int bh_check_boundedness(int j, double vrel, double vesc, double dr_code, double sink_radius) // need to know the sink radius, which can be distinct from both the softening and search radii
{
    /* if pair is a gas particle make sure to account for its thermal pressure */
    double cs = 0; if(P[j].Type==0) {cs=Particle_effective_soundspeed_i(j);}
#if defined(SINGLE_STAR_SINK_DYNAMICS) && defined(MAGNETIC)
    double bmag=0; for(int k=0;k<3;k++) {bmag+=Get_Particle_BField(j,k)*Get_Particle_BField(j,k);}
    cs = sqrt(cs*cs + bmag/SphP[j].Density);
#endif    
#ifdef SINGLE_STAR_SINK_DYNAMICS
    if(Get_Particle_Size(j) > sink_radius * 1.396263) return 0; // particle volume should be less than sink volume, enforcing a minimum spatial resolution around the sink
#endif
#if defined(COOLING) && defined(SINGLE_STAR_SINK_DYNAMICS)
    double nHcgs = HYDROGEN_MASSFRAC * (SphP[j].Density * All.cf_a3inv * All.UnitDensity_in_cgs * All.HubbleParam * All.HubbleParam) / PROTONMASS;
    if(nHcgs > 1e13 && cs > 0.1 * vrel) { // we're probably sitting at the bottom of a quasi-hydrostatic Larson core
        double m_eff = 4. * M_PI * dr_code * dr_code * dr_code * SphP[j].Density; // assume an isothermal sphere interior, for Shu-type solution
        vesc = DMAX(sqrt(2*All.G * m_eff / dr_code), vesc); // re-estimate vesc using self-gravity of the gas
    }
#endif    
    double v2 = (vrel*vrel+cs*cs)/(vesc*vesc); int bound = 0;
    if(v2 < 1) 
    {
        double apocenter = dr_code / (1.0-v2); // NOTE: this is the major axis of the orbit, not the apocenter... - MYG
        double apocenter_max = 2*All.ForceSoftening[5]; // 2.8*epsilon (softening length) //
#ifdef BH_GRAVCAPTURE_FIXEDSINKRADIUS // Bate 1995-style criterion, with a fixed sink/accretion radius that is distinct from both the force softening and the search radius
        //double eps = DMIN(2*Get_Particle_Size(j),sink_radius); // in the unresolved limit there's no need to force it to actually get within r_sink
        if(dr_code>sink_radius) {return 0;} else {return 1;}
#endif
#if !defined(SINGLE_STAR_SINK_DYNAMICS) && (defined(BH_SEED_GROWTH_TESTS) || defined(BH_GRAVCAPTURE_GAS) || defined(BH_GRAVCAPTURE_NONGAS))
        double r_j = All.ForceSoftening[P[j].Type];
        if(P[j].Type==0) {r_j = DMAX(r_j , PPP[j].Hsml);}
        apocenter_max = DMAX(10.0*All.ForceSoftening[5],DMIN(50.0*All.ForceSoftening[5],r_j));
        if(P[j].Type==5) {apocenter_max = DMIN(apocenter_max , 1.*All.ForceSoftening[5]);}
#endif
#if defined(BH_REPOSITION_ON_POTMIN)
        if(P[j].Type==5) {return 1;} // default is to be unrestrictive for BH-BH mergers //
#endif
        if(apocenter < apocenter_max) {bound = 1;}
    }
    return bound;
}



#if defined(BH_PHOTONMOMENTUM) || defined(BH_WIND_CONTINUOUS)
/* weight function for local (short-range) coupling terms from the black hole, including the single-scattering radiation pressure and the bal winds */
double bh_angleweight_localcoupling(int j, double hR, double cos_theta, double r, double H_bh)
{
    // this follows what we do with SNe, and applies a normalized weight based on the fraction of the solid angle subtended by the particle //
    if(r <= 0 || H_bh <= 0) return 0;
    double H_j = PPP[j].Hsml;
    if((r >= H_j && r >= H_bh) || (H_j <= 0) || (P[j].Mass <= 0) || (SphP[j].Density <= 0)) return 0;
    /* now calculate all the kernel quantities we need */
    double hinv=1./H_bh,hinv_j=1./H_j,hinv3_j=hinv_j*hinv_j*hinv_j,wk_j=0,u=r/H_bh; /* note these lines and many below assume 3D sims! */
    double dwk_j=0,u_j=r*hinv_j,hinv4_j=hinv_j*hinv3_j,V_j=P[j].Mass/SphP[j].Density;
    double hinv3=hinv*hinv*hinv,hinv4=hinv*hinv3,wk=0,dwk=0;
    if(u<1) {kernel_main(u,hinv3,hinv4,&wk,&dwk,0);} else {wk=dwk=0;}
    if(u_j<1) {kernel_main(u_j,hinv3_j,hinv4_j,&wk_j,&dwk_j,0);} else {wk_j=dwk_j=0;}
    double V_i = 4.*M_PI/3. * H_bh*H_bh*H_bh / (All.DesNumNgb * All.BlackHoleNgbFactor); // this is approximate, will be wrong (but ok b/c just increases weight to neighbors) when not enough neighbors found //
    if(V_i<0 || isnan(V_i)) {V_i=0;}
    if(V_j<0 || isnan(V_j)) {V_j=0;}
    double sph_area = fabs(V_i*V_i*dwk + V_j*V_j*dwk_j); // effective face area //
    wk = 0.5 * (1. - 1./sqrt(1. + sph_area / (M_PI*r*r))); // corresponding geometric weight //
#if !defined(BH_PUSHAREA)
    wk = 0.5 * (V_j/V_i) * (V_i*wk + V_j*wk_j); // weight in the limit N_particles >> 1 for equal-mass particles (accounts for self-shielding if some in dense disk)
#endif
#if defined(BH_FB_COLLIMATED)
    double costheta2=cos_theta*cos_theta, eps_width_jet=0.35; eps_width_jet*=eps_width_jet; /* eps_width^2 is approximate with in radians of 'core' of jet */
    wk *= eps_width_jet * (eps_width_jet + costheta2) / ((eps_width_jet + 1) * (eps_width_jet + (1-costheta2)));
#endif
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



/* function below is used for long-range black hole radiation fields -- used only in the forcetree routines (where they
    rely this for things like the long-range radiation pressure and compton heating) */
double bh_angleweight(double bh_lum_input, MyFloat bh_angle[3], double hR, double dx, double dy, double dz)
{
#ifdef SINGLE_STAR_SINK_DYNAMICS
    return bh_lum_input;
#endif
    if(bh_lum_input <= 0 || isnan(hR) || hR <= 0) return 0;
    double r2 = dx*dx+dy*dy+dz*dz; if(r2 <= 0) return 0;
    if(r2*All.UnitLength_in_cm*All.UnitLength_in_cm*All.cf_atime*All.cf_atime < 9.523e36) return 0; /* no force at < 1pc */
#if defined(BH_FB_COLLIMATED)
    double cos_theta = fabs((dx*bh_angle[0] + dy*bh_angle[1] + dz*bh_angle[2])/sqrt(r2*(bh_angle[0]*bh_angle[0]+bh_angle[1]*bh_angle[1]+bh_angle[2]*bh_angle[2]))); if(!isfinite(cos_theta)) {cos_theta=1;}
    double wt_normalized = 0.0847655*exp(4.5*cos_theta*cos_theta); // ~exp(-x^2/2*hR^2), normalized appropriately to give the correct total flux, for hR~0.3
    //double costheta2=cos_theta*cos_theta, eps2=0.25; /* eps_width^2 is approximate with in radians of 'core' of jet */
    //double wt_normalized = 5.33709 * eps_width_jet * (eps_width_jet + costheta2) / ((eps_width_jet + 1) * (eps_width_jet + (1-costheta2)));
    return bh_lum_input * wt_normalized; // ~exp(-x^2/2*hR^2), normalized appropriately to give the correct total flux, for hR~0.3
#endif
    return bh_lum_input;

#if 0
    double hRe=hR, y; if(hRe<0.1) hRe=0.1; if(hRe>0.5) hRe=0.5;
    y = -1.0 / (0.357-10.839*hRe+142.640*hRe*hRe-713.928*hRe*hRe*hRe+1315.132*hRe*hRe*hRe*hRe); y = 1.441 + (-6.42+9.92*hRe) * (exp(cos_theta*cos_theta*y)-exp(y)) / (1-exp(y));  // approximation to nathans fits
    //double A=5.57*exp(-hRe/0.52);double B=19.0*exp(-hRe/0.21);double C0=20.5+20.2/(1+exp((hRe-0.25)/0.035));
    //y = 1.441 + A*((1+C0*exp(-B*1.5708))/(1+C0*exp(-B*(1.5708-acos(cos_theta))))-1); // this is nathan's fitting function (fairly expensive with large number of exp calls and arc_cos
    //y=0.746559 - 9.10916*(-0.658128+exp(-0.418356*cos_theta*cos_theta)); // this is normalized so the total flux is L/(4pi*r*r) and assumed monochromatic IR
    if(y>1.441) y=1.441; if(y<-5.0) y=-5.0; y*=2.3026; // so we can take exp, instead of pow //
    return exp(y) * bh_lum_input;
#endif
}

#endif /* end of #if defined(BH_PHOTONMOMENTUM) || defined(BH_WIND_CONTINUOUS) */






void set_blackhole_mdot(int i, int n, double dt)
{
    double mdot=0; int k; k=0;
    double soundspeed2 = convert_internalenergy_soundspeed2(n,BlackholeTempInfo[i].BH_InternalEnergy);
#ifdef BH_GRAVACCRETION
    double m_tmp_for_bhar, mdisk_for_bhar, bh_mass, fac;
    double rmax_for_bhar,fgas_for_bhar,f_disk_for_bhar, f0_for_bhar;
#endif
#ifdef BH_SUBGRIDBHVARIABILITY
    long nsubgridvar; int jsub; double varsg1,varsg2,omega_ri,n0_sgrid_elements,norm_subgrid,time_var_subgridvar;
    gsl_rng *random_generator_forbh;
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

#if defined(BH_GRAVACCRETION) && (BH_GRAVACCRETION == 0)
        /* DAA: torque rate based on kinematic B/D decomposition as in Angles-Alcazar et al. [here in brackets because it requires an extra pass] */
        m_tmp_for_bhar = BlackholeTempInfo[i].Mgas_in_Kernel + BlackholeTempInfo[i].Mstar_in_Kernel;
        double mbulge_for_bhar = BlackholeTempInfo[i].MstarBulge_in_Kernel;
        if(mbulge_for_bhar>BlackholeTempInfo[i].Mstar_in_Kernel) {mbulge_for_bhar=BlackholeTempInfo[i].Mstar_in_Kernel;}
        mdisk_for_bhar = m_tmp_for_bhar - mbulge_for_bhar;
        f_disk_for_bhar = mdisk_for_bhar / m_tmp_for_bhar;
#else
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

            fac = (5.0*(SOLAR_MASS/All.UnitMass_in_g)/(SEC_PER_YEAR/All.UnitTime_in_s)); // basic normalization (use alpha=5, midpoint of values alpha=[1,10] from Hopkins and Quataert 2011 //
            fac *= pow(f_disk_for_bhar, 3./2.) * pow(bh_mass_units,1./6.) / (1. + f0_for_bhar/fgas_for_bhar); // dimensionless dependence on f_disk and m_bh (latter is weak)
            mdot = fac * mdisk_for_bhar_units * pow(rmax_for_bhar_units,-3./2.); // these are the quantities which scale strongly with the scale where we evaluate the BHAR
            mdot *= pow(r0_accretion/rmax_for_bhar , 3./2.); // scales to estimator value at specified 'accretion radius' r0_accretion, assuming f_disk, etc constant and M_enclosed is a constant-density profile //
            
#if (BH_GRAVACCRETION == 2) // gas accreted at a constant rate per free-fall time from the specified 'accretion radius'
            mdot = mgas_in_racc * omega_dyn;
#endif
#if (BH_GRAVACCRETION == 3) // gravito-turbulent estimator: accrete at constant fraction per free-fall from 'accretion radius' with that fraction being f_disk^2
            mdot = (mdisk_for_bhar / menc_all) * (mdisk_for_bhar / menc_all) * mgas_in_racc * omega_dyn;
#endif
#if (BH_GRAVACCRETION == 4) || (BH_GRAVACCRETION == 6) || (BH_GRAVACCRETION == 7) // accrete constant fraction per free-fall time from accretion radius set to minimum of BH radius of gravitational dominance over Vc or cs (basically where gas more tightly bound to BH) - has Bondi-like form
            double Vc2_rmax = All.G * menc_all / rmax_for_bhar; // this is in physical units now
            mdot = 4.*M_PI * All.G*All.G * BPP(n).BH_Mass*menc_all * (BPP(n).DensAroundStar*All.cf_a3inv) / pow(soundspeed2 + Vc2_rmax, 1.5);
#if (BH_GRAVACCRETION == 6) || (BH_GRAVACCRETION == 7)
            double bhvel2=0; for(k=0;k<3;k++) {bhvel2 += BlackholeTempInfo[i].BH_SurroundingGasVel[k]*BlackholeTempInfo[i].BH_SurroundingGasVel[k];}
            double veldisp2_eff = bhvel2/3. + soundspeed2, masscorrfac = pow( menc_all/(1.e-10*menc_all + BPP(n).BH_Mass) , 0.25 );
            mdot = masscorrfac * 4.*M_PI * All.G*All.G * BPP(n).BH_Mass*menc_all * (BPP(n).DensAroundStar*All.cf_a3inv) / pow(1.e-5*soundspeed2 + Vc2_rmax, 1.5);
            mdot /= 1 + sqrt(veldisp2_eff/Vc2_rmax) * DMIN( veldisp2_eff/Vc2_rmax , masscorrfac );
#if (BH_GRAVACCRETION == 7)
            mdot = 4.*M_PI * All.G*All.G * menc_all*menc_all * (BPP(n).DensAroundStar*All.cf_a3inv) / pow(veldisp2_eff + Vc2_rmax, 1.5);
#endif
#endif
#endif
#if (BH_GRAVACCRETION == 5) // use default torques estimator, but then allow gas to accrete as Bondi-Hoyle when its circularization radius is inside the BH radius of influence
            double j_tmp_for_bhar=0,jcirc_crit=0; for(k=0;k<3;k++) {j_tmp_for_bhar+=BlackholeTempInfo[i].Jgas_in_Kernel[k]*BlackholeTempInfo[i].Jgas_in_Kernel[k];}
            j_tmp_for_bhar=sqrt(j_tmp_for_bhar); jcirc_crit = BlackholeTempInfo[i].Mgas_in_Kernel * rmax_for_bhar*rmax_for_bhar*omega_dyn;
            jcirc_crit *= pow(bh_mass/m_tmp_for_bhar,2./3.);
            if(j_tmp_for_bhar < jcirc_crit) /* circularization within BH-dominated region, Bondi accretion valid */
            {
                double bhvel2=0; for(k=0;k<3;k++) {bhvel2 += BlackholeTempInfo[i].BH_SurroundingGasVel[k]*BlackholeTempInfo[i].BH_SurroundingGasVel[k];}
                double rho = BPP(n).DensAroundStar*All.cf_a3inv; /* we want all quantities in physical units */
                double vcs_fac = pow(soundspeed2+bhvel2, 1.5);
                mdot = 4.*M_PI * All.G*All.G * BPP(n).BH_Mass*BPP(n).BH_Mass * rho / vcs_fac;
            } /* otherwise, circularization outside BH-dominated region, efficiency according to usual [above] */
#endif
#if (BH_GRAVACCRETION == 8)
            double hubber_mdot_from_vr_estimator=MIN_REAL_NUMBER, hubber_mdot_disk_estimator=MIN_REAL_NUMBER; /* our computed 'hubber_mdot_vr_estimator' is their estimate of the radial inflow time from a Bondi flow: but care is needed, for any non-Bondi flow this can give unphysical or negative answers, so we need to limit it and be very cautious using it */
            if(BlackholeTempInfo[i].hubber_mdot_vr_estimator > 0) { hubber_mdot_from_vr_estimator = BlackholeTempInfo[i].hubber_mdot_vr_estimator; }
            if(BlackholeTempInfo[i].hubber_mdot_disk_estimator > 0) { hubber_mdot_disk_estimator = 0.01 * BlackholeTempInfo[i].Mgas_in_Kernel / (sqrt(All.G * P[n].Mass) * BlackholeTempInfo[i].hubber_mdot_disk_estimator);}
            double j_eff=0,m_eff=BlackholeTempInfo[i].Malt_in_Kernel; for(k=0;k<3;k++) {j_eff+=BlackholeTempInfo[i].Jalt_in_Kernel[k]*BlackholeTempInfo[i].Jalt_in_Kernel[k];}
            double facc_which_hubber_mdot = DMIN(1, 1.75*sqrt(j_eff)/(m_eff*sqrt(All.G*(m_eff+P[n].Mass)*rmax_for_bhar))); /* disk fraction estimator */
            mdot = DMAX( BlackholeTempInfo[i].hubber_mdot_bondi_limiter , pow(hubber_mdot_from_vr_estimator,1-facc_which_hubber_mdot)*pow(hubber_mdot_disk_estimator,facc_which_hubber_mdot));
#endif
#ifdef BH_SIGMAMULTIPLIER
            double sigma_crit = 3000. * (2.09e-4 / (All.UnitMass_in_g*All.HubbleParam/(All.UnitLength_in_cm*All.UnitLength_in_cm))); // from MG's fit to isolated cloud sims [converts from Msun/pc^2 to code units]
            double sigma_enc = (BlackholeTempInfo[i].Malt_in_Kernel + P[n].Mass) / (M_PI*rmax_for_bhar*rmax_for_bhar); // effective surface density [total gravitating mass / area]
            mdot *= sigma_enc / (sigma_enc + sigma_crit);
#endif
#ifndef IO_REDUCED_MODE
            printf(" ..BH accretion kernel :: mdot %g Norm %g fdisk %g bh_8 %g fgas %g f0 %g mdisk_9 %g rmax_100 %g \n",
                   mdot,fac,f_disk_for_bhar,bh_mass_units,fgas_for_bhar,f0_for_bhar,mdisk_for_bhar_units,rmax_for_bhar_units);
#endif
        } // if(fgas_for_bhar<=0)
    } // if(BlackholeTempInfo[i].Mgas_in_Kernel > 0)
    mdot *= All.BlackHoleAccretionFactor; // this is a pure normalization multiplier here
#endif // ifdef BH_GRAVACCRETION
    
    
    
#ifdef BH_BONDI /* heres where we calculate the Bondi accretion rate, if that's going to be used */
    double bhvel2 = 0, rho = BPP(n).DensAroundStar * All.cf_a3inv; /* we want all quantities in physical units */
#if (BH_BONDI != 1)
    for(k=0;k<3;k++) bhvel2 += BlackholeTempInfo[i].BH_SurroundingGasVel[k]*BlackholeTempInfo[i].BH_SurroundingGasVel[k];
#endif
    double fac = pow(soundspeed2+bhvel2, 1.5);
    if(fac > 0)
    {
        double AccretionFactor = All.BlackHoleAccretionFactor;
#if (BH_BONDI == 2) /* variable-alpha model (Booth&Schaye 2009): now All.BlackHoleAccretionFactor is the slope of the density dependence */
        AccretionFactor = 1.0; if(rho > All.PhysDensThresh) {AccretionFactor = pow(rho/All.PhysDensThresh, All.BlackHoleAccretionFactor);}
#endif
        mdot = 4. * M_PI * AccretionFactor * All.G * All.G * BPP(n).BH_Mass * BPP(n).BH_Mass * rho / fac;
    }
#endif // ifdef BH_BONDI
    
    
    
    
/* DAA: note that we should have mdot=0 here , otherwise the mass accreted is counted twice [mdot*dt in set_blackhole_new_mass, accreted_BH_mass in blackhole_swallow_and_kick] */
#ifdef BH_GRAVCAPTURE_GAS
    mdot = 0; /* force mdot=0 despite any earlier settings here.  If this is set, we have to wait to swallow step to eval mdot. */
    //mdot = BlackholeTempInfo[i].mass_to_swallow_edd / dt;       /* TODO: this can still greatly exceed eddington... */
#endif //ifdef BH_GRAVCAPTURE_GAS

    
#ifdef BH_ALPHADISK_ACCRETION
    /* use the mass in the accretion disk from the previous timestep to determine the BH accretion rate */
    double x_MdiskSelfGravLimiter = BPP(n).BH_Mass_AlphaDisk / (BH_ALPHADISK_ACCRETION * BPP(n).BH_Mass);
    if(x_MdiskSelfGravLimiter > 20.) {mdot=0;} else {mdot *= exp(-0.5*x_MdiskSelfGravLimiter*x_MdiskSelfGravLimiter);}
    BlackholeTempInfo[i].mdot_alphadisk = mdot;     /* if BH_GRAVCAPTURE_GAS is off, this gets the accretion rate */
    mdot = 0;
    if(BPP(n).BH_Mass_AlphaDisk > 0)
    {
        /* this below is a more complicated expression using the outer-disk expression from Shakura & Sunyaev. Simpler expression
            below captures the same physics with considerably less potential to extrapolate to rather odd scalings in extreme regimes */
        
        /* mdot = (2.45 * (SOLAR_MASS/All.UnitMass_in_g)/(SEC_PER_YEAR/All.UnitTime_in_s)) * // normalization
            pow( 0.1 , 8./7.) * // viscous disk 'alpha'
            pow( BPP(n).BH_Mass*All.UnitMass_in_g / (All.HubbleParam * 1.0e8*SOLAR_MASS) , -5./14. ) * // mbh dependence
            pow( BPP(n).BH_Mass_AlphaDisk*All.UnitMass_in_g / (All.HubbleParam * 1.0e8*SOLAR_MASS) , 10./7. ) * // m_disk dependence
            pow( DMIN(0.2,DMIN(PPP[n].Hsml,All.ForceSoftening[5])*All.cf_atime*All.UnitLength_in_cm/(All.HubbleParam * 3.086e18)) , -25./14. ); // r_disk dependence */
        
        double t_yr = SEC_PER_YEAR / (All.UnitTime_in_s / All.HubbleParam);
        double t_acc_disk = 4.2e7 * t_yr * pow((BPP(n).BH_Mass_AlphaDisk+BPP(n).BH_Mass) / BPP(n).BH_Mass_AlphaDisk, 0.4); /* shakura-sunyaev disk, integrated out to Q~1 radius, approximately */

#ifdef SINGLE_STAR_SINK_DYNAMICS
        double Gm_i=1./(All.G*P[n].Mass);
#ifdef BH_GRAVCAPTURE_FIXEDSINKRADIUS
        double reff = P[n].SinkRadius; // Assuming this is scaled to the nominal minimum resolved Jeans length
#else	
        double reff=DMAX(All.SofteningTable[5], Get_Particle_Size(n)); // Note that using dynamic particle size for this can do weird stuff - e.g. accretion rate will vary according to the local interparticle spacing even for a disk that isn't accreting and would physically be in a steady state
#endif
        double t_dyn_eff=sqrt(reff*reff*reff*Gm_i);
        t_acc_disk = t_dyn_eff;// dynamical time at radius "H" where the neighbor gas is located
#if defined(BH_FOLLOW_ACCRETED_ANGMOM)
        double j=0; for(k=0;k<3;k++) {j+=P[n].BH_Specific_AngMom[k]*P[n].BH_Specific_AngMom[k];}
        if(j>0) {j=sqrt(j);} else {j=0;} // calculate magnitude of specific ang mom
#ifdef BH_RETURN_ANGMOM_TO_GAS /* in this particular case, we will assume AM transfer occurs 'first' to the disk, then to the ambient medium, instead of simply assuming specific AM of accreted material is conserved */
        j *= P[n].Mass / (MIN_REAL_NUMBER + BPP(n).BH_Mass_AlphaDisk); // accounting for the fact that the disk contains all the angular momentum, it specific angulkar momentum is j_disk=m_disk/m_tot*j
#else
        j *= 1. + 1./BH_ALPHADISK_ACCRETION; // correction assuming a ratio of accretion disk to sink mass ~BH_ALPHADISK_ACCRETION [max allowed], with the material in the sink having given its angular momentum to the sink [which is what should happen]
#endif
        //t_acc_disk = j*j*j*Gm_i*Gm_i; // dynamical time at circularization radius of the alpha-disk
#ifdef SLOPE2_SINKS
        //t_acc_disk = DMAX(sqrt(pow(0.033*All.SofteningTable[5],3)/(All.G*P[n].BH_Mass)) , t_acc_disk); // catch against un-resolvably small j [since accreted particles are extended, there is always material at non-zero "j" even if accreted at zero impact parameter; 1/30th is conservative estimate for perfect impact parameter]
        //t_acc_disk = t_dyn_eff; //use the dynamical time as the orbital time (equivalent to circular orbit at sink radius)
        t_acc_disk = DMAX(10. * 2.*M_PI*t_acc_disk, t_dyn_eff); // 10 orbits at circularization radius to spiral all the way in (very fast), but should be no less than the resolution-scale dynamical time	
#else
        //if(j*j*Gm_i < 6.957e11 / All.UnitLength_in_cm) {t_acc_disk = 0;} // when angular momentum is low enough, we're falling straight onto the protostellar surface, here taking 10R_solar as a rough number
        //t_acc_disk = DMAX(100. * t_acc_disk * (1 / (Gm_i * DMIN(reff, j*j*Gm_i))) / soundspeed2, t_dyn_eff); // Shakura-Sunyaev prescription with alpha=0.01, using minimum of sink and circularization radius
#endif // SLOPE2_SINKS
#endif // BH_FOLLOW_ACCRETED_ANGMOM
#endif // SINGLE_STAR_SINK_DYNAMICS
        
#if defined(BH_GRAVCAPTURE_GAS)
        t_acc_disk /= All.BlackHoleAccretionFactor; // when using GRAVCAPTURE, this won't multiply the continuous mdot, but rather mdot from disk to BH
#endif
        t_acc_disk = DMAX(t_acc_disk , 3.*dt); /* make sure accretion timescale is at least a few timesteps to avoid over-shoot, etc */
        mdot = BPP(n).BH_Mass_AlphaDisk / t_acc_disk;
    }
#endif //ifdef BH_ALPHADISK_ACCRETION
    
    
    
#ifdef BH_SUBGRIDBHVARIABILITY /* account for sub-grid accretion rate variability */
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
#ifdef BH_WIND_KICK /* DAA: correct the mdot into the accretion disk for the mass loss in "kick" winds Note that for BH_WIND_CONTINUOUS the wind mass is removed in the final loop */
    BlackholeTempInfo[i].mdot_alphadisk *= All.BAL_f_accretion;
#endif

#else // BH_ALPHADISK_ACCRETION

#if defined(BH_WIND_CONTINUOUS) || defined(BH_WIND_KICK) || defined(BH_WIND_SPAWN)
    mdot *= All.BAL_f_accretion; /* if there is no alpha-disk, the BHAR defined above is really an mdot into the accretion disk. the rate -to the hole- should be corrected for winds */
#endif
#endif // BH_ALPHADISK_ACCRETION

    
#ifdef BH_ENFORCE_EDDINGTON_LIMIT /* cap the maximum at the Eddington limit */
    if(mdot > All.BlackHoleEddingtonFactor * meddington) {mdot = All.BlackHoleEddingtonFactor * meddington;}
#endif
    
#if defined(BH_RETURN_ANGMOM_TO_GAS) /* pre-calculate some quantities for 'angular momentum feedback' here, these have to be based on the mdot estimator above */
    double jmag=0,lmag=0,mdot_eff=mdot; BlackholeTempInfo[i].angmom_norm_topass_in_swallowloop=0;
/*
#if defined(BH_GRAVCAPTURE_GAS)
    mdot_eff = BlackholeTempInfo[i].mass_to_swallow_edd / (MIN_REAL_NUMBER + dt);
#elif defined(BH_ALPHADISK_ACCRETION)
    mdot_eff = BlackholeTempInfo[i].mdot_alphadisk;
#endif
*/
    mdot_eff = mdot; /* even if we are using gravcapture, and alpha-disk, then it's actually this we want to use [the rate of material depleting out of the disk, which is the rate of total angular momentum loss in the disk ] */
    for(k=0;k<3;k++) {jmag+=BlackholeTempInfo[i].angmom_prepass_sum_for_passback[k]*BlackholeTempInfo[i].angmom_prepass_sum_for_passback[k]; lmag+=BPP(n).BH_Specific_AngMom[k]*BPP(n).BH_Specific_AngMom[k];}
    if(jmag>0 && lmag>0) {BlackholeTempInfo[i].angmom_norm_topass_in_swallowloop = P[n].Mass * sqrt(lmag) * mdot_eff * dt / sqrt(jmag);} /* this should be in units such that, times CODE radius+(code=physical) ang-mom, gives CODE velocity: looks ok at present */
#endif
    
    /* alright, now we can FINALLY set the BH accretion rate */
    if(isnan(mdot)) {mdot=0;}
    BPP(n).BH_Mdot = DMAX(mdot,0);
}



/* Update the BH_Mass and the BH_Mass_AlphaDisk */
void set_blackhole_new_mass(int i, int n, double dt)
{
    if(BPP(n).BH_Mdot <= 0) {BPP(n).BH_Mdot=0;} /* check unphysical values */

    /* before mass update, track angular momentum in disk for 'smoothed' accretion case [using continuous accretion rate and specific AM of all material in kernel around BH] */
#if defined(BH_FOLLOW_ACCRETED_ANGMOM) && (BH_FOLLOW_ACCRETED_ANGMOM == 1)
    double dm_acc_for_j = BPP(n).BH_Mdot * dt, m_tot_for_j = BPP(n).BH_Mass;
#ifdef BH_ALPHADISK_ACCRETION
    dm_acc_for_j = BlackholeTempInfo[i].mdot_alphadisk * dt; m_tot_for_j = BPP(n).BH_Mass + BPP(n).BH_Mass_AlphaDisk;
#endif
    int k; for(k=0;k<3;k++) {BPP(n).BH_Specific_AngMom[k] = (m_tot_for_j*BPP(n).BH_Specific_AngMom[k] + dm_acc_for_j*BlackholeTempInfo[i].Jgas_in_Kernel[k]/(MIN_REAL_NUMBER + BlackholeTempInfo[i].Mgas_in_Kernel)) / (m_tot_for_j + dm_acc_for_j);}
#endif

/*  for BH_WIND_CONTINUOUS or BH_WIND_SPAWN
        - we accrete the winds first, either explicitly to the BH or implicitly into the disk -
        - then we remove the wind mass in the final loop
    for BH_WIND_KICK
        - the BH grows according to the mdot set above (including the mass loss in winds)
        - if there is an alpha-disk, the mass going out in winds has been subtracted from mdot_alphadisk
    for BH_WIND_KICK + BH_GRAVCAPTURE_GAS
        - the ratio of BH/disk growth-to-outflow rate is enforced explicitly in blackhole_swallow_and_kick */

#ifdef BH_ALPHADISK_ACCRETION
    BPP(n).BH_Mass += BPP(n).BH_Mdot * dt;   // mdot comes from the disk - no mass loss here regarless of BAL model -
    double dm_alphadisk = ( BlackholeTempInfo[i].mdot_alphadisk - BPP(n).BH_Mdot ) * dt;
    if(dm_alphadisk < -BPP(n).BH_Mass_AlphaDisk) {BPP(n).BH_Mass_AlphaDisk=0;} else {BPP(n).BH_Mass_AlphaDisk += dm_alphadisk;}
    if(BPP(n).BH_Mass_AlphaDisk<0) {BPP(n).BH_Mass_AlphaDisk=0;}
    if(P[n].Mass<0) {P[n].Mass=0;}
#else // #ifdef BH_ALPHADISK_ACCRETION
#if defined(BH_WIND_CONTINUOUS) || defined(BH_WIND_SPAWN)
    BPP(n).BH_Mass += BPP(n).BH_Mdot * dt / All.BAL_f_accretion; // accrete the winds first, then remove the wind mass in the final loop
#else
    BPP(n).BH_Mass += BPP(n).BH_Mdot * dt;
#endif
#endif // #else BH_ALPHADISK_ACCRETION
#ifdef JET_DIRECTION_FROM_KERNEL_AND_SINK //store Mgas_in_Kernel and Jgas_in_Kernel
    BPP(n).Mgas_in_Kernel=BlackholeTempInfo[i].Mgas_in_Kernel;
    {int jk; for(jk=0;jk<3;jk++) {BPP(n).Jgas_in_Kernel[jk]=BlackholeTempInfo[i].Jgas_in_Kernel[jk];}}
#endif

}



#if defined(BH_DRAG) || defined(BH_DYNFRICTION)
void set_blackhole_drag(int i, int n, double dt)
{
    int k;
#ifdef BH_DRAG /* add a drag force for the black-holes, accounting for the accretion */
    if((dt>0)&&(BPP(n).BH_Mass>0))
    {
        double fac = BPP(n).BH_Mdot * dt / BPP(n).BH_Mass;
#if (BH_DRAG == 2)
        double meddington = bh_eddington_mdot(BPP(n).BH_Mass);
        fac = meddington * dt / BPP(n).BH_Mass; /* make the force stronger to keep the BH from wandering */
#endif
        if(fac>1) fac=1;
        for(k = 0; k < 3; k++) {P[n].GravAccel[k] += All.cf_atime*All.cf_atime * fac * BlackholeTempInfo[i].BH_SurroundingGasVel[k] / dt;}
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
        double bhvel2_df=0; for(k=0;k<3;k++) bhvel2_df += BlackholeTempInfo[i].DF_mean_vel[k]*BlackholeTempInfo[i].DF_mean_vel[k];
        double fac, fac_friction;
        /* First term is approximation of the error function */
        fac = 8 * (M_PI - 3) / (3 * M_PI * (4. - M_PI));
        x = sqrt(bhvel2_df) / (sqrt(2) * BlackholeTempInfo[i].DF_rms_vel);
        fac_friction =  x / fabs(x) * sqrt(1 - exp(-x * x * (4 / M_PI + fac * x * x) / (1 + fac * x * x))) - 2 * x / sqrt(M_PI) * exp(-x * x);
        /* now the Coulomb logarithm */
        fac = 50. * 3.086e21 / (All.UnitLength_in_cm/All.HubbleParam); /* impact parameter */
        fac_friction *= log(1. + fac * bhvel2_df / (All.G * bh_mass));
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
        fac_friction *= 4*M_PI * All.G * All.G * fac * bh_mass / (bhvel2_df*sqrt(bhvel2_df));
        /* now apply this to the actual acceleration */
        if(fac_friction<0) fac_friction=0; if(isnan(fac_friction)) fac_friction=0;
#if (BH_REPOSITION_ON_POTMIN == 2)
        /* ok, here we have a special catch - the friction isn't standard dynamical friction, but rather we are already moving
            towards a potential mininum and want to be sure that we don't overshoot or retain large velocities that will
            launch us out, so we want the BH to 'relax' towards moving with the local flow */
        if(bhvel2_df > 0 && dt > 0)
        {
            double dv_magnitude=sqrt(bhvel2_df)*All.cf_atime, fac_vel=0, afac_vel=0; // physical velocity difference between 'target' and BH
            afac_vel = All.G * Mass_in_Kernel / pow(PPP[n].Hsml*All.cf_atime,2); // GMenc/r^2 estimate of local acceleration //
            afac_vel = DMIN(dv_magnitude/(3.155e13/(All.UnitTime_in_s/All.HubbleParam)) , DMAX( DMIN(DMAX(-2.*BPP(n).BH_MinPot/(PPP[n].Hsml*All.cf_atime*All.cf_atime), 0), 10.*dv_magnitude/dt), afac_vel)); // free-fall-acceleration [checked-to-zero], limited to multiple of actual vel difference in timestep
            fac_vel = afac_vel * dt / dv_magnitude; // rate at which de-celeration/damping occurs
            if(fac_vel > 1.e-4) {fac_vel = 1.-exp(-fac_vel);}
            for(k = 0; k < 3; k++) {P[n].Vel[k] += BlackholeTempInfo[i].DF_mean_vel[k]*All.cf_atime * fac_vel;}
        }
#else
        for(k = 0; k < 3; k++) {P[n].GravAccel[k] += All.cf_atime*All.cf_atime * fac_friction * BlackholeTempInfo[i].DF_mean_vel[k];}
#endif
    }
#endif
    
    
}
#endif




#if defined(BH_PHOTONMOMENTUM) || defined(BH_WIND_CONTINUOUS)
void set_blackhole_long_range_rp(int i, int n) /* pre-set quantities needed for long-range radiation pressure terms */
{
    int k; double fac; P[n].BH_disk_hr=1/3;
    if(BlackholeTempInfo[i].Mgas_in_Kernel > 0)
    {   /* estimate h/R surrounding the BH from the gas density gradients */
        fac=0; for(k=0;k<3;k++) {fac += BlackholeTempInfo[i].GradRho_in_Kernel[k]*BlackholeTempInfo[i].GradRho_in_Kernel[k];}
        P[n].BH_disk_hr = P[n].DensAroundStar / (PPP[n].Hsml * sqrt(fac)) * 1.3; /* 1.3 factor from integrating exponential disk with h/R=const over gaussian kernel, for width=1/3 (quintic kernel); everything here is in code units, comes out dimensionless */
    }
#if !defined(BH_FOLLOW_ACCRETED_ANGMOM)
    /* use the gradrho vector as a surrogate to hold the orientation of the angular momentum if we aren't evolving it explicitly
     (this is done because the long-range radiation routines for the BH require the angular momentum vector for non-isotropic emission) */
    P[n].GradRho[0]=P[n].GradRho[1]=0; P[n].GradRho[2]=1;
    if(BlackholeTempInfo[i].Mgas_in_Kernel > 0) {
        fac=0; for(k=0;k<3;k++) {fac += BlackholeTempInfo[i].Jgas_in_Kernel[k]*BlackholeTempInfo[i].Jgas_in_Kernel[k];}
        fac=sqrt(fac); if(fac>0) {for(k=0;k<3;k++) {P[n].GradRho[k] = BlackholeTempInfo[i].Jgas_in_Kernel[k]/fac;}}}
        /* now, the P[n].GradRho[k] field for the BH holds the orientation of the UNIT angular momentum vector
         NOTE it is important that HARD-WIRED into the code, this blackhole calculation comes after the density calculation
         but before the forcetree update and walk; otherwise, this won't be used correctly there */
#endif
}
#endif // if defined(BH_PHOTONMOMENTUM) || defined(BH_WIND_CONTINUOUS)





void blackhole_final_operations(void)
{
    int i, k, n, bin; double dt, mass_disk, mdot_disk, MgasBulge, MstarBulge, r0;
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
                dt = (P[n].TimeBin ? (((integertime) 1) << P[n].TimeBin) : 0) * All.Timebase_interval / All.cf_hubble_a;
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
        if(((BlackholeTempInfo[i].accreted_Mass>0)||(BlackholeTempInfo[i].accreted_BH_Mass>0)||(BlackholeTempInfo[i].accreted_BH_Mass_alphadisk>0)) && P[n].Mass > 0)
        {
            
            double m_new = P[n].Mass + BlackholeTempInfo[i].accreted_Mass;
#if (BH_FOLLOW_ACCRETED_ANGMOM == 1) /* in this case we are only counting this if its coming from BH particles */
            m_new = P[n].Mass + BlackholeTempInfo[i].accreted_BH_Mass + BlackholeTempInfo[i].accreted_BH_Mass_alphadisk;
#endif
#if defined(BH_FOLLOW_ACCRETED_MOMENTUM) && !defined(BH_REPOSITION_ON_POTMIN)
            for(k=0;k<3;k++) {P[n].Vel[k] = (P[n].Vel[k]*m_new+ BlackholeTempInfo[i].accreted_momentum[k]) / m_new;} 
#ifdef HERMITE_INTEGRATION
            for(k=0;k<3;k++) {P[n].OldVel[k] = (P[n].OldVel[k]*m_new + BlackholeTempInfo[i].accreted_momentum[k]) / m_new;}
#endif	    
#endif
#if defined(BH_FOLLOW_ACCRETED_COM) && !defined(BH_REPOSITION_ON_POTMIN)
            for(k=0;k<3;k++) {P[n].Pos[k] = (P[n].Pos[k]*m_new + BlackholeTempInfo[i].accreted_centerofmass[k]) / m_new;}
#ifdef HERMITE_INTEGRATION
            for(k=0;k<3;k++) {P[n].OldPos[k] = (P[n].OldPos[k]*m_new + BlackholeTempInfo[i].accreted_centerofmass[k]) / m_new;}
#endif	    	    
#endif
#if defined(BH_FOLLOW_ACCRETED_ANGMOM)
            for(k=0;k<3;k++) {BPP(n).BH_Specific_AngMom[k] = (BPP(n).BH_Specific_AngMom[k]*P[n].Mass + BlackholeTempInfo[i].accreted_J[k]) / m_new;}
            BPP(n).BH_Specific_AngMom[0] -= (BlackholeTempInfo[i].accreted_centerofmass[1]*BlackholeTempInfo[i].accreted_momentum[2] - BlackholeTempInfo[i].accreted_centerofmass[2]*BlackholeTempInfo[i].accreted_momentum[1]) / (m_new*m_new);
            BPP(n).BH_Specific_AngMom[1] -= (BlackholeTempInfo[i].accreted_centerofmass[2]*BlackholeTempInfo[i].accreted_momentum[0] - BlackholeTempInfo[i].accreted_centerofmass[0]*BlackholeTempInfo[i].accreted_momentum[2]) / (m_new*m_new);
            BPP(n).BH_Specific_AngMom[2] -= (BlackholeTempInfo[i].accreted_centerofmass[0]*BlackholeTempInfo[i].accreted_momentum[1] - BlackholeTempInfo[i].accreted_centerofmass[1]*BlackholeTempInfo[i].accreted_momentum[0]) / (m_new*m_new);
#endif
            P[n].Mass += BlackholeTempInfo[i].accreted_Mass;
            BPP(n).BH_Mass += BlackholeTempInfo[i].accreted_BH_Mass;
#ifdef BH_ALPHADISK_ACCRETION
            BPP(n).BH_Mass_AlphaDisk += BlackholeTempInfo[i].accreted_BH_Mass_alphadisk;
#endif
#ifdef GRAIN_FLUID
            BPP(n).BH_Dust_Mass += BlackholeTempInfo[i].accreted_dust_Mass;
#endif            
        } // if(masses > 0) check
#ifdef BH_GRAVCAPTURE_FIXEDSINKRADIUS
	P[n].SinkRadius = DMAX(P[n].SinkRadius, All.ForceSoftening[5]);
#endif

        /* Correct for the mass loss due to radiation and BAL winds */
#ifndef WAKEUP
        dt = (P[n].TimeBin ? (((integertime) 1) << P[n].TimeBin) : 0) * All.Timebase_interval / All.cf_hubble_a;
#else
        dt = P[n].dt_step * All.Timebase_interval / All.cf_hubble_a;
#endif //ifndef WAKEUP
        
        
        /* always substract the radiation energy from BPP(n).BH_Mass && P[n].Mass */
        double dm = BPP(n).BH_Mdot * dt;
        double radiation_loss = All.BlackHoleRadiativeEfficiency * dm;
        if(radiation_loss > DMIN(P[n].Mass,BPP(n).BH_Mass)) radiation_loss = DMIN(P[n].Mass,BPP(n).BH_Mass);
#ifndef BH_DEBUG_FIX_MASS
        P[n].Mass -= radiation_loss; BPP(n).BH_Mass -= radiation_loss;
#endif         
        /* subtract the BAL wind mass from P[n].Mass && (BPP(n).BH_Mass || BPP(n).BH_Mass_AlphaDisk) // DAA: note that the mass loss in winds for BH_WIND_KICK has already been taken into account */
#ifdef BH_WIND_CONTINUOUS
        double dm_wind = (1.-All.BAL_f_accretion) / All.BAL_f_accretion * dm;
        if(dm_wind > P[n].Mass) {dm_wind = P[n].Mass;}
#ifdef BH_ALPHADISK_ACCRETION
        if(dm_wind > BPP(n).BH_Mass_AlphaDisk) {dm_wind = BPP(n).BH_Mass_AlphaDisk;}
        P[n].Mass -= dm_wind;
        BPP(n).BH_Mass_AlphaDisk -= dm_wind;
#else
        if(dm_wind > BPP(n).BH_Mass) {dm_wind = BPP(n).BH_Mass;}
#ifndef BH_DEBUG_FIX_MASS
        P[n].Mass -= dm_wind; BPP(n).BH_Mass -= dm_wind;
#endif 
#endif
#endif // ifdef BH_WIND_CONTINUOUS
        
	/* save local effective signal velocity of gas for sink particle CFL-like timestep criterion */
#ifdef SINGLE_STAR_SINK_DYNAMICS
	P[n].BH_SurroundingGasVel = 0;
	for(k=0; k<3; k++) {P[n].BH_SurroundingGasVel += BlackholeTempInfo[i].BH_SurroundingGasVel[k]*BlackholeTempInfo[i].BH_SurroundingGasVel[k];}
	P[n].BH_SurroundingGasVel += convert_internalenergy_soundspeed2(n,BlackholeTempInfo[i].BH_InternalEnergy);
	P[n].BH_SurroundingGasVel = sqrt(P[n].BH_SurroundingGasVel);
#endif

#ifdef BH_WIND_SPAWN
        /* DAA: for wind spawning, we only need to subtract the BAL wind mass from BH_Mass (or BH_Mass_AlphaDisk)
            --> wind mass subtracted from P.Mass in blackhole_spawn_particle_wind_shell()  */        
        double dm_wind = (1.-All.BAL_f_accretion) / All.BAL_f_accretion * dm;
#ifdef SINGLE_STAR_FB_JETS
        if((P[n].BH_Mass * All.UnitMass_in_g / (All.HubbleParam*SOLAR_MASS) < 0.01) || P[n].Mass < 7*All.MinMassForParticleMerger) dm_wind = 0; // no jets launched yet if <0.01msun or if we haven't accreted enough to get a reliable jet direction
#endif
        if(dm_wind > P[n].Mass) {dm_wind = P[n].Mass;}
#if defined(BH_ALPHADISK_ACCRETION)
        if(dm_wind > BPP(n).BH_Mass_AlphaDisk) {dm_wind = BPP(n).BH_Mass_AlphaDisk;}
        BPP(n).BH_Mass_AlphaDisk -= dm_wind;
#else
        if(dm_wind > BPP(n).BH_Mass) {dm_wind = BPP(n).BH_Mass;}
#ifndef BH_DEBUG_FIX_MASS
        BPP(n).BH_Mass -= dm_wind;
#endif 
#endif
        BPP(n).unspawned_wind_mass += dm_wind;
        if(BPP(n).unspawned_wind_mass>MaxUnSpanMassBH) {MaxUnSpanMassBH=BPP(n).unspawned_wind_mass;}
#endif
        
        /* dump the results to the 'blackhole_details' files */
        mass_disk=0; mdot_disk=0; MgasBulge=0; MstarBulge=0; r0 = PPP[n].Hsml * All.cf_atime;
#ifdef BH_ALPHADISK_ACCRETION
        mass_disk = BPP(n).BH_Mass_AlphaDisk;
        mdot_disk = BlackholeTempInfo[i].mdot_alphadisk;
#endif
#if (defined(BH_GRAVACCRETION) && (BH_GRAVACCRETION == 0))
        MgasBulge = BlackholeTempInfo[i].MgasBulge_in_Kernel;
        MstarBulge = BlackholeTempInfo[i].MstarBulge_in_Kernel;
#endif
    
#if defined(BH_OUTPUT_MOREINFO)
#ifdef SINGLE_STAR_SINK_DYNAMICS_MG_DG_TEST_PACKAGE
        fprintf(FdBlackHolesDetails, "%2.12f %u  %g %g %g %g %g  %g %g %g %g %g %g  %2.10f %2.10f %2.10f  %2.7f %2.7f %2.7f  %g %g %g  %g %g %g\n",
                All.Time, P[n].ID,  P[n].Mass, BPP(n).BH_Mass, mass_disk, BPP(n).BH_Mdot, mdot_disk, dt, BPP(n).DensAroundStar*All.cf_a3inv, BlackholeTempInfo[i].BH_InternalEnergy,
                BlackholeTempInfo[i].Mgas_in_Kernel, BlackholeTempInfo[i].Mstar_in_Kernel, r0, P[n].Pos[0], P[n].Pos[1], P[n].Pos[2],  P[n].Vel[0], P[n].Vel[1], P[n].Vel[2],
                BlackholeTempInfo[i].Jgas_in_Kernel[0], BlackholeTempInfo[i].Jgas_in_Kernel[1], BlackholeTempInfo[i].Jgas_in_Kernel[2], BPP(n).BH_Specific_AngMom[0]*P[n].Mass, BPP(n).BH_Specific_AngMom[1]*P[n].Mass, BPP(n).BH_Specific_AngMom[2]*P[n].Mass );
#else
        fprintf(FdBlackHolesDetails, "%2.12f %u  %g %g %g %g %g %g  %g %g %g %g %g %g %g %g  %2.10f %2.10f %2.10f  %2.7f %2.7f %2.7f  %g %g %g  %g %g %g\n",
                All.Time, P[n].ID,  P[n].Mass, BPP(n).BH_Mass, mass_disk, BPP(n).BH_Mdot, mdot_disk, dt, BPP(n).DensAroundStar*All.cf_a3inv, BlackholeTempInfo[i].BH_InternalEnergy, BlackholeTempInfo[i].Sfr_in_Kernel,
                BlackholeTempInfo[i].Mgas_in_Kernel, BlackholeTempInfo[i].Mstar_in_Kernel, MgasBulge, MstarBulge, r0, P[n].Pos[0], P[n].Pos[1], P[n].Pos[2],  P[n].Vel[0], P[n].Vel[1], P[n].Vel[2],
                BlackholeTempInfo[i].Jgas_in_Kernel[0], BlackholeTempInfo[i].Jgas_in_Kernel[1], BlackholeTempInfo[i].Jgas_in_Kernel[2], BlackholeTempInfo[i].Jstar_in_Kernel[0], BlackholeTempInfo[i].Jstar_in_Kernel[1], BlackholeTempInfo[i].Jstar_in_Kernel[2] );
#endif
#else

#ifndef IO_REDUCED_MODE
        fprintf(FdBlackHolesDetails, "BH=%u %g %g %g %g %g %g %g %g   %2.7f %2.7f %2.7f\n", P[n].ID, All.Time, BPP(n).BH_Mass, mass_disk, P[n].Mass, BPP(n).BH_Mdot, mdot_disk,
                P[n].DensAroundStar*All.cf_a3inv, BlackholeTempInfo[i].BH_InternalEnergy, P[n].Pos[0], P[n].Pos[1], P[n].Pos[2]);            // DAA: DensAroundStar is actually not defined in BHP->BPP...
#endif
#endif
        
        bin = P[n].TimeBin;
        TimeBin_BH_mass[bin] += BPP(n).BH_Mass;
        TimeBin_BH_dynamicalmass[bin] += P[n].Mass;
        TimeBin_BH_Mdot[bin] += BPP(n).BH_Mdot;
        if(BPP(n).BH_Mass > 0) {TimeBin_BH_Medd[bin] += BPP(n).BH_Mdot / BPP(n).BH_Mass;}
        

#if defined(SINGLE_STAR_PROTOSTELLAR_EVOLUTION) && (SINGLE_STAR_PROTOSTELLAR_EVOLUTION==0) // initially we want to do this protostellar evolution, but not the promotion
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
        if(BPP(n).ProtoStellarRadius_inSolar <= R_Hayashi_Henyey)
        {
            // currently on Henyey track, contracting at constant Luminosity
            t_R_evol = 1.815e7 * m_solar*m_solar / (BPP(n).ProtoStellarRadius_inSolar * lum_sol) / (All.UnitTime_in_s/(All.HubbleParam * SEC_PER_YEAR)); // contraction timescale
            contraction_factor = 1. / (1. + dt/t_R_evol);
        } else {
            // currently on Hayashi track, contracting at constant Temperature
            t_R_evol = 8.021e7 * m_solar*m_solar / (BPP(n).ProtoStellarRadius_inSolar*BPP(n).ProtoStellarRadius_inSolar*BPP(n).ProtoStellarRadius_inSolar * T4000_4) / (All.UnitTime_in_s/(All.HubbleParam * SEC_PER_YEAR)); // contraction timescale
            contraction_factor = 1. / pow(1 + 3.*dt/t_R_evol, 1./3.);
        }
        double r_new = 100. * m_solar; // size of newly-formed protostar
	    if (m_solar < 0.012) {r_new =  5.24 * pow(m_solar, 1./3);} // constant density for Jupiter-type objects
        BPP(n).ProtoStellarRadius_inSolar = (BPP(n).ProtoStellarRadius_inSolar * contraction_factor + r_new * mu) / (1. + mu); // new size (contraction + accretion both accounted for)
        double R_main_sequence_ignition; // main sequence radius - where contraction should halt
        if(m_solar <= 1) {R_main_sequence_ignition = pow(m_solar,0.8);} else {R_main_sequence_ignition = pow(m_solar,0.57);}
        
        //if(BPP(n).PreMainSeq_Tracker < 0.36787944117144233) // if drops below 1/e [one t_premainseq timescale, in the absence of accretion], promote //
        if(BPP(n).ProtoStellarRadius_inSolar <= R_main_sequence_ignition)
        {
	        BPP(n).ProtoStellarRadius_inSolar = R_main_sequence_ignition;
#ifdef SINGLE_STAR_PROMOTION		    
            P[n].Type = 4; // convert type
            count_bhelim++; // note one fewer BH-type particle
            P[n].StellarAge = All.Time; // mark the new ZAMS age according to the current time
            P[n].Mass = DMAX(P[n].Mass , BPP(n).BH_Mass + BPP(n).BH_Mass_AlphaDisk);
#endif		    
        }
#elif (SINGLE_STAR_PROTOSTELLAR_EVOLUTION == 1)
    /*Protostellar evolution model based on the ORION version, see Offner 2009 Appendix B*/
    const double frad = 0.33; //limit for forming radiative barrier
    const double fk = 0.5; //fraction of kinetic energy that is radiated away in the inner disk before reaching the surface, using default ORION value here as it is not a GIZMO input parameter
    
    double mass = BPP(n).BH_Mass; //mass of star/protostar
    double mdot = BPP(n).BH_Mdot; //accretion rate, shorter to write it this way
    double mdot_m_solar_per_year = mdot * (All.UnitMass_in_g/(All.HubbleParam * SOLAR_MASS))/All.UnitTime_in_s*SEC_PER_YEAR; // accretion rate in msolar/yr
    double m_solar = mass * (All.UnitMass_in_g / SOLAR_MASS); // mass in units of Msun
    double m_initial = DMAX(1.e-37 , (mass - dm)); // mass before accretion
    double mu = DMAX(0, dm/m_initial); // relative mass accreted
    int stage = BPP(n).ProtoStellarStage; /*what stage of stellar evolution the particle is in 0: pre collapse, 1: no burning, 2: fixed Tc burnig, 3: variable Tc burning, 4: shell burning, 5: main sequence, see Offner 2009 Appendix B*/
    double r_solar = BPP(n).ProtoStellarRadius_inSolar; //star radius in R_solar
    double r = r_solar * SOLAR_RADIUS/All.UnitLength_in_cm; // same but in code units
    int stage_increase = 0; 
    double lum_Hayashi = ps_lum_Hayashi_BB(mass, r); //blackbody radiation assuming the star follows the Hayashi track 
    double lum_MS = ps_lum_MS(mass); //luminosity of main sequence star of m mass
    double lum_int = DMAX(lum_Hayashi, lum_MS); //luminosity from the stellar interior
    if (stage < 5){ //not a main sequence star
        if (stage >= 1){ //We only evolve those that are beyond the pre-collapse phase
            //Get properties for stellar evolution
            double n_ad = ps_adiabatic_index(stage, mdot); //get adiabatic index. Note: ORION does not seem to update this, but I think it is worthwhile as mdot can vary over time
            double ag = 3.0/(5.0-n_ad); //shorthand
            double rhoc = ps_rhoc(mass, n_ad, r); //central density
            double Pc = ps_Pc(mass, n_ad, r); //central pressure
            double Tc = ps_Tc(rhoc,Pc); //central temperature
            double beta = ps_beta(mass, n_ad, rhoc, Pc); //mean ratio of gas pressure to total pressure
            double dlogbeta_dlogm = ps_dlogbeta_dlogm(mass, r, n_ad, beta, rhoc, Pc); // d log beta/ d log m
            double lum_I = ps_lum_I(mdot); //luminosity needed to ionize the accreted material
            //Calculate luminosity from D burning
            double lum_D = 0; //luminosity from D burning
            double dm_D = dm; //by default we burn no D (stage 1)
            if (stage==2){ //burning at fixed Tc, lum_D set to keep the central temperature constant
                double dlogbetaperbetac_dlogm = ps_dlogbetaperbetac_dlogm(mass, r, n_ad, beta, rhoc, Pc, Tc); // ratio of gas pressure to total pressure at the center
                lum_D = lum_int + lum_I + (All.G*mass*mdot/r) * ( 1.-fk-0.5*ag*beta * (1.+dlogbetaperbetac_dlogm) ); // Eq B8 of Offner 2009
                //Change in available deuterium mass
                dm_D = dm - dt * lum_D / (15.*SOLAR_LUM / (All.UnitEnergy_in_cgs / All.UnitTime_in_s)) * (1e-5) / ((All.UnitMass_in_g/(All.HubbleParam * SOLAR_MASS))/All.UnitTime_in_s*SEC_PER_YEAR) ;
            }
            else{ if (stage>2){
                //burning all accreted D for stages above 2
                lum_D = 15.*SOLAR_LUM / (All.UnitEnergy_in_cgs / All.UnitTime_in_s) * (mdot_m_solar_per_year/(1e-5));
                dm_D = 0; //all new D is burned
                }
            }
            //Evolve D content
            if (dm_D!=0){ BPP(n).Mass_D += dm_D;} //change in D content
            //Let's evolve the stellar radius
            double rel_dr = 2 * ( mu * (1.-(1.-fk)/(ag*beta)+0.5*dlogbeta_dlogm) - dt/(ag*beta)*r/(All.G*mass*mass) * (lum_int+lum_I-lum_D) ); //Eq B4 of Offner 2009 divided by r
            BPP(n).ProtoStellarRadius_inSolar *= (1.0+rel_dr);
            printf("sink ID %u mass %g radius_solar %g stage %d mdot_m_solar_per_year %g mD %g rel_dr %g dm %g dm_D %g Tc %g beta %g dt %g n_ad %g lum_int %g lum_I %g lum_D %g age %g Myr\n",P[n].ID,mass,r_solar,stage, mdot_m_solar_per_year, (BPP(n).Mass_D-dm_D),rel_dr,dm, dm_D, Tc, beta, dt, n_ad, lum_int / (SOLAR_LUM / (All.UnitEnergy_in_cgs / All.UnitTime_in_s)), lum_I/ (SOLAR_LUM / (All.UnitEnergy_in_cgs / All.UnitTime_in_s)), lum_D/ (SOLAR_LUM / (All.UnitEnergy_in_cgs / All.UnitTime_in_s)), (All.Time-P[n].ProtoStellarAge)*All.UnitTime_in_Megayears );
            //Check whether the star can progress to the next state
            //Move from "no burn" to "burning at fixed Tc" phase when central temperature gets high enough for D ignition
            if ( (stage==1) && (Tc >= 1.5e6) ){ 
                stage_increase = 1;//particle qualifies to the "fixed Tc burn" phase
            }
            //Move from "burning at fixed Tc" to "variable Tc burn" phase when D runs out
            if ( (stage==2) && (BPP(n).Mass_D <= 0) ){ 
                BPP(n).Mass_D = 0;
                stage_increase = 1;//particle qualifies to the "variable Tc burn" phase
            }
            //Move from "variable Tc burn" to "shell burn" phase when radiation becomes strong enough to form a radiative zone
            if ( (stage==3) && ((lum_D/lum_MS) < frad) ){ 
                stage_increase = 1;//particle qualifies to the "shell burn" phase
                BPP(n).ProtoStellarRadius_inSolar *= 2.1; //star swells due to the formation of radiative barrier
            }
            //Move from "shell burn" to "main sequence" phase when the radius reaches the main sequence radius
            if ( (stage==4) && (BPP(n).ProtoStellarRadius_inSolar <= ps_radius_MS_in_solar(mass)) ){ 
                stage_increase = 1;//particle qualifies to become a ZAMS star
                BPP(n).ProtoStellarRadius_inSolar = ps_radius_MS_in_solar(mass);
            }
        }
        else{ //the protostar is in the "pre-collapse" state, no internal evolution, just check if it can be promoted to the next stage
            BPP(n).Mass_D = BPP(n).BH_Mass; //no D burned so far
            if (m_solar >= 0.01){ stage_increase = 1;} //particle qualifies to the "no burning stage" 
        }
        if (stage_increase){BPP(n).ProtoStellarStage += stage_increase; printf("%u promoted to %d \n",P[n].ID,(stage+stage_increase));} //increase evolutionary stage if the particle satisfies the requirements 
    }
    else{// for main sequence stars
        BPP(n).ProtoStellarRadius_inSolar = ps_radius_MS_in_solar(mass); //update the mass if the mass changes (unlikely)
    }
    //Calculate the luminosity of the star
    /*********************************************/
    /* Power based parametrization from ORION (2 params)*/
    //lum_acc = facc * fk * All.G*mass*mdot/r; //luminosity radiated at the accretion shock
    //lum_disk = (1.-fk) * All.G*mass*mdot/r; //luminosity released by material that traverses the inner disk
    //BPP(n).StarLuminosity_Solar = (lum_acc + lum_disk + lum_int) / (SOLAR_LUM / (All.UnitEnergy_in_cgs / All.UnitTime_in_s)) ; //luminosity of the star
    /*********************************************/
    /* Mass flux based parametrization (1 params) */
    /* For our nominal choice of BAL_f_accretion=0.7 this gives very similar results to the ORION parameters of fk=facc=0.5, which is equivalent to 0.75*/
#ifdef SINGLE_STAR_FB_JETS
            double eps_protostar=1.0; // since mdot is already modified by All.BAL_f_accretion
#else
            double eps_protostar=0.75; //fraction of gas that does not get launched out with a jet, default value, although 1.0 would be energy conserving
#endif
    BPP(n).StarLuminosity_Solar = (eps_protostar*All.G*mass*mdot/r + lum_int)/ (SOLAR_LUM / (All.UnitEnergy_in_cgs / All.UnitTime_in_s)); //luminosity of the star in solar units

#endif
        
    } // for(i=0; i<N_active_loc_BHs; i++)
    
    
#if (defined(SINGLE_STAR_PROMOTION) && (SINGLE_STAR_PROTOSTELLAR_EVOLUTION == 0)) 
    MPI_Allreduce(&count_bhelim, &tot_bhelim, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
    All.TotBHs -= tot_bhelim; 
#endif
    
}


#endif // BLACK_HOLES
