#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <gsl/gsl_math.h>
#include "../allvars.h"
#include "../proto.h"
#include "../kernel.h"

/* this file handles the FIRE short-range radiation-pressure and
    photo-ionization terms. written by Phil Hopkins (phopkins@caltech.edu) for GIZMO.
 */

#if defined(GALSF_FB_FIRE_RT_LOCALRP) /* first the radiation pressure coupled in the immediate vicinity of the star */
void radiation_pressure_winds_consolidated(void)
{
    double age_threshold_in_gyr = 0.15; // don't bother for older populations, they contribute negligibly here //
#ifdef SINGLE_STAR_SINK_DYNAMICS
    age_threshold_in_gyr = 1.0e10; // for the single-star problems want to include everything, for completeness //
#endif
    if(All.RP_Local_Momentum_Renormalization<=0) return;
    Ngblist = (int *) mymalloc("Ngblist",NumPart * sizeof(int));
    PRINT_STATUS("Local Radiation-Pressure acceleration calculation");
    MyDouble *pos; int N_MAX_KERNEL,N_MIN_KERNEL,MAXITER_FB,NITER,startnode,dummy,numngb_inbox,i,j,k,n;
    double h,wt_sum,delta_v_imparted_rp=0,total_n_wind=0,total_mom_wind=0,total_prob_kick=0,avg_v_kick=0,avg_taufac=0;

    for(i = FirstActiveParticle; i >= 0; i = NextActiveParticle[i])
    {
        if((P[i].Type == 4)||((All.ComovingIntegrationOn==0)&&((P[i].Type == 2)||(P[i].Type==3))))
        {
            double star_age = evaluate_stellar_age_Gyr(P[i].StellarAge);
            if( (star_age < age_threshold_in_gyr) && (P[i].Mass > 0) && (P[i].DensAroundStar > 0) )
            {
                /* calculate some basic luminosity properties of the stars */
                double lm_ssp = evaluate_light_to_mass_ratio(star_age, i); // light-to-mass ratio in solar
                double lum_cgs = (lm_ssp * SOLAR_LUM) * (P[i].Mass*UNIT_MASS_IN_SOLAR); // total L in CGS of star particle
                double f_lum_ion = particle_ionizing_luminosity_in_cgs(i) / lum_cgs; f_lum_ion=DMAX(0.,DMIN(1.,f_lum_ion)); // fraction of luminosity in H-ionizing radiation
                double dt = GET_PARTICLE_TIMESTEP_IN_PHYSICAL(i);
                double dE_over_c = All.RP_Local_Momentum_Renormalization * lum_cgs * (dt*UNIT_TIME_IN_CGS) / C_LIGHT; // total photon momentum emitted in timestep, in CGS (= L*dt/c)
                dE_over_c /= (UNIT_MASS_IN_CGS * UNIT_VEL_IN_CGS); // total photon momentum now in code units
                total_prob_kick += dE_over_c; // sum contributions

                /* calculate some pre-amble properties */
                double RtauMax = P[i].Hsml * (5. + 2.0 * rt_kappa(i,RT_FREQ_BIN_FIRE_UV) * P[i].Hsml*P[i].DensAroundStar*All.cf_a2inv); // guess search radius which is a few H, plus larger factor if optically thick //
                RtauMax = DMAX( 1./(UNIT_LENGTH_IN_KPC*All.cf_atime) , DMIN( 10./(UNIT_LENGTH_IN_KPC*All.cf_atime) , RtauMax )); // restrict to 1-10 kpc here

#ifndef GALSF_FB_FIRE_RT_CONTINUOUSRP
                /* if kicks are stochastic, we don't want to waste time doing a neighbor search every timestep; it can be much faster to pre-estimate the kick probabilities */
                double v_wind_threshold = 15. / UNIT_VEL_IN_KMS; // unit mass for kicks
#ifdef SINGLE_STAR_SINK_DYNAMICS
                v_wind_threshold = 0.2 / UNIT_VEL_IN_KMS; // for this module use lower unit mas for kicks
#endif
                double rho_phys=P[i].DensAroundStar*All.cf_a3inv, h_phys=P[i].Hsml*All.cf_atime; // density and h in -physical- units
                double v_grav_guess; v_grav_guess = DMIN( 1.82*(65.748/UNIT_VEL_IN_KMS)*pow(1.+rho_phys*UNIT_DENSITY_IN_NHCGS,-0.25) , sqrt(All.G*(P[i].Mass + NORM_COEFF*rho_phys*h_phys*h_phys*h_phys)/h_phys) ); // don't want to 'under-kick' if there are small local characteristic velocities in the region of interest
                delta_v_imparted_rp = v_wind_threshold; // always couple this 'discrete' kick, to avoid having to couple every single timestep for every single star particle
                double dv_imparted_perpart_guess = (dE_over_c/P[i].Mass); // estimate of summed dv_imparted [in code units] from single-scattering: = momentum/mass of particle
                double tau_IR_guess = rt_kappa(i,RT_FREQ_BIN_FIRE_IR) * rho_phys*h_phys; // guess of IR optical depth. everything in physical code units //
                dv_imparted_perpart_guess += (dE_over_c/P[i].Mass) * tau_IR_guess; // estimate of additional IR term [1+tau_IR]*L/c assumed here as coupling //
                double prob = dv_imparted_perpart_guess / delta_v_imparted_rp; prob *= 2000.; // need to include a buffer for errors in the estimates above
#if (GALSF_FB_FIRE_STELLAREVOLUTION > 2)
                delta_v_imparted_rp = DMAX( v_grav_guess , v_wind_threshold ); // because of re-written layer below, call this less often 
                prob = dv_imparted_perpart_guess / delta_v_imparted_rp; // chance of kick
                if(prob < 1 && prob > 0) {dE_over_c /= prob;} // if assigning low-probability, need to up-weight the kick to statistically couple the right momentum
#endif
                double p_random = get_random_number(P[i].ID+ThisTask+i+2); // master random number for use below
                if(p_random <= prob) // alright, its worth doing the loop!
#endif
                { // within loop
                    /* ok, now open the neighbor list for the star particle */
                    N_MIN_KERNEL=10;N_MAX_KERNEL=256;MAXITER_FB=100;NITER=0;wt_sum=0; startnode=All.MaxPart;dummy=0;numngb_inbox=0;h=1.0*P[i].Hsml;pos=P[i].Pos;
                    if(h<=0) {h=All.SofteningTable[0];} else {if(h>RtauMax) {h=RtauMax;}}
                    do {
                        numngb_inbox = ngb_treefind_pairs_threads(pos, h, -1, &startnode, 0, &dummy, &dummy, &dummy, Ngblist);
                        if((numngb_inbox>=N_MIN_KERNEL)&&(numngb_inbox<=N_MAX_KERNEL))
                        {
                            wt_sum=0; /* note these lines and many below assume 3D sims! */
                            for(n=0; n<numngb_inbox; n++)
                            {
                                j = Ngblist[n];
                                if((P[j].Mass>0) && (SphP[j].Density>0))
                                {
                                    double dp[3],r2=0; for(k=0;k<3;k++) {dp[k]=P[j].Pos[k]-P[i].Pos[k];}
                                    NEAREST_XYZ(dp[0],dp[1],dp[2],1); for(k=0;k<3;k++) {r2+=dp[k]*dp[k];} /* find the closest image in the given box size */
                                    if(r2>=h*h || r2<=0) {continue;}
                                    double h_eff_j = Get_Particle_Size(j); wt_sum += h_eff_j*h_eff_j; // weight factor for neighbors
                                } /* if( (P[j].Mass>0) && (SphP[j].Density>0) ) */
                            } /* for(n=0; n<numngb_inbox; n++) */
                            if(wt_sum <= 0) {h*= 1.2123212335; startnode=All.MaxPart;} /* wt_sum <= 0; no particles found inside corners - expand */
                        } else {
                            startnode=All.MaxPart;
                            if(numngb_inbox<N_MIN_KERNEL) {if(numngb_inbox<=0) {h*=2.0;} else {if(NITER<=5) {h*=pow((float)numngb_inbox/(float)N_MIN_KERNEL,-0.3333);} else {h*=1.26;}}} /* iterate until find appropriate > N_MIN # particles */
                            if(numngb_inbox>N_MAX_KERNEL) {if(NITER<=5) {h*=pow((float)numngb_inbox/(float)N_MAX_KERNEL,-0.3333);} else {h/=1.31;}} /* iterate until find appropriate < N_MAX # particles */
                        }
                        if(h>20.*RtauMax) {h=20.*RtauMax; if(NITER<MAXITER_FB-1) {NITER=MAXITER_FB-1;}} /* if h exceeds the maximum now, set it to that value, and set NITER to maximum to force end of iteration */
                        NITER++;
                    } while( (startnode >= 0) && (NITER<=MAXITER_FB) );
                    
                    if(wt_sum > 0)  /* found at least one massive neighbor, can proceed */
                    {
                        for(n=0; n<numngb_inbox; n++)
                        {
                            j = Ngblist[n];
                            if((P[j].Mass>0) && (SphP[j].Density>0))
                            {
                                double dp[3],r2=0; for(k=0;k<3;k++) {dp[k]=P[j].Pos[k]-P[i].Pos[k];}
                                NEAREST_XYZ(dp[0],dp[1],dp[2],1); for(k=0;k<3;k++) {r2+=dp[k]*dp[k];} /* find the closest image in the given box size */
                                if(r2>=h*h || r2<=0) {continue;}
                                double h_eff_i = DMIN(h, Get_Particle_Size(i)), h_eff_j = Get_Particle_Size(j);
                                r2 += MIN_REAL_NUMBER + (h_eff_i/5.)*(h_eff_i/5.); // just a small number to prevent errors on near-overlaps
                                double wk = h_eff_j*h_eff_j / wt_sum; // dimensionless weight factor

                                /* first -- share out the UV luminosity among the local neighbors, weighted by the gas kernel */
                                double dv_imparted_singlescattering = wk * (dE_over_c / P[j].Mass); // fractional initial photon momentum seen by this neighbor
#if (GALSF_FB_FIRE_STELLAREVOLUTION > 2)
                                /* estimate fraction of the available single-scattering RP that can actually be absorbed in the cell */
                                double sigma_cell_to_total = (1./wk) * (P[j].Mass / (h_eff_j*All.cf_atime * h_eff_j*All.cf_atime)); // code units -- correct back to 'total' column through all neighbors, since thats what determines the total fraction that will be absorbed here //
                                double tau_uv = rt_kappa(j,RT_FREQ_BIN_FIRE_UV) * sigma_cell_to_total, tau_op = rt_kappa(j,RT_FREQ_BIN_FIRE_OPT) * sigma_cell_to_total; // opacity in uv and optical bands
                                double frac_abs = f_lum_ion + (1.-f_lum_ion) * (1. - 0.5*(exp(-tau_uv) + exp(-tau_op))); // absorbed fraction in the actual cell
                                dv_imparted_singlescattering *= frac_abs; // reduce the single-scattering flux by the fraction of that flux actually absorbed
#endif
                                /* velocity imparted by IR acceleration : = kappa*flux/c, flux scales as 1/r2 from source, kappa with metallicity */
                                double kappa_ir_codeunits = rt_kappa(j,RT_FREQ_BIN_FIRE_IR); // opacity in code units
                                double dv_imparted_multiplescattering = All.RP_Local_Momentum_Renormalization * (dE_over_c / P[j].Mass) * kappa_ir_codeunits * (P[j].Mass/(4.*M_PI*r2*All.cf_atime*All.cf_atime));
#if defined(GALSF_FB_FIRE_RT_CONTINUOUSRP) || (GALSF_FB_FIRE_STELLAREVOLUTION > 2)
                                delta_v_imparted_rp = dv_imparted_multiplescattering + dv_imparted_singlescattering; prob = 1;
#else
                                prob = (dv_imparted_multiplescattering+dv_imparted_singlescattering) / delta_v_imparted_rp; if(prob>1) {delta_v_imparted_rp *= prob;}
                                if(n>0) {p_random=get_random_number(P[j].ID+P[i].ID +ThisTask+ 3);} //else p_random=0;
                                if(p_random < prob)
#endif
                                { /* open subloop with wind kick */
                                    if(delta_v_imparted_rp>1.e4/UNIT_VEL_IN_KMS) {delta_v_imparted_rp=1.e4/UNIT_VEL_IN_KMS;} /* limiter */
                                    /* collect numbers to output */
                                    total_n_wind += 1.0; total_mom_wind += P[j].Mass*delta_v_imparted_rp; avg_v_kick += delta_v_imparted_rp;
                                    avg_taufac +=  (P[j].Mass*delta_v_imparted_rp) * (dv_imparted_multiplescattering / (dE_over_c / P[j].Mass));

                                    /* determine the direction of the kick */
                                    double dir[3], norm=0;
#if defined(GALSF_FB_FIRE_RT_CONTINUOUSRP) || (GALSF_FB_FIRE_STELLAREVOLUTION > 2)
                                    delta_v_imparted_rp = dv_imparted_multiplescattering; // ir kick: directed along opacity gradient //
                                    for(k=0;k<3;k++) {dir[k]=-P[j].GradRho[k]; norm+=dir[k]*dir[k];} // based on density gradient near star //
#else
                                    if(dv_imparted_singlescattering > dv_imparted_multiplescattering) {for(k=0;k<3;k++) {dir[k]=dp[k]; norm+=dir[k]*dir[k];}} // if kick is primarily from uv, then orient directly //
                                        else {for(k=0;k<3;k++) {dir[k]=-P[j].GradRho[k]; norm+=dir[k]*dir[k];}} // otherwise, along opacity gradient //
#endif
                                    if(norm>0) {norm=sqrt(norm); for(k=0;k<3;k++) dir[k] /= norm;} else {dir[0]=0; dir[1]=0; dir[2]=1; norm=1;}
                                    for(k=0;k<3;k++) {P[j].Vel[k] += delta_v_imparted_rp * All.cf_atime * dir[k]; SphP[j].VelPred[k] += delta_v_imparted_rp * All.cf_atime * dir[k];} /* apply the kick [put into comoving code units as oppropriate */
                                    
#if defined(GALSF_FB_FIRE_RT_CONTINUOUSRP) || (GALSF_FB_FIRE_STELLAREVOLUTION > 2)
                                    /* if we're not forcing the kick orientation, need to separately apply the UV kick */
                                    delta_v_imparted_rp = dv_imparted_singlescattering; // uv kick: directed from star //
                                    norm=0; for(k=0;k<3;k++) {dir[k]=dp[k]; norm+=dir[k]*dir[k];}
                                    if(norm>0) {norm=sqrt(norm); for(k=0;k<3;k++) {dir[k] /= norm;}} else {dir[0]=0; dir[1]=0; dir[2]=1; norm=1;}
                                    for(k=0; k<3; k++) {P[j].Vel[k] += delta_v_imparted_rp * All.cf_atime * dir[k]; SphP[j].VelPred[k] += delta_v_imparted_rp * All.cf_atime * dir[k];} /* apply the kick */
#endif
                                } /* closes if(get_random_number(P[i].ID + 2) < prob) */
                            } /* if( (P[j].Mass>0) && (SphP[j].Density>0) ) */
                        } /* for(n=0; n<numngb_inbox; n++) */
                    } /* if (rho>0) */
                } // // within loop
            } // star age, mass check:: (star_age < 0.1) && (P[i].Mass > 0) && (P[i].DensAroundStar > 0)
        } // particle type check::  if((P[i].Type == 4)....
    } // main particle loop for(i = FirstActiveParticle; i >= 0; i = NextActiveParticle[i])
    myfree(Ngblist);
    
    double totMPI_n_wind=0,totMPI_mom_wind=0,totMPI_avg_v=0,totMPI_avg_taufac=0,totMPI_prob_kick=0;
    MPI_Reduce(&total_n_wind, &totMPI_n_wind, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&total_mom_wind, &totMPI_mom_wind, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&avg_v_kick, &totMPI_avg_v, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&avg_taufac, &totMPI_avg_taufac, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&total_prob_kick, &totMPI_prob_kick, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
    if(ThisTask == 0)
    {
#ifdef IO_REDUCED_MODE
        if(totMPI_n_wind>0)
#endif
        if(totMPI_prob_kick>0)
        {
            totMPI_avg_v /= MIN_REAL_NUMBER + totMPI_n_wind; totMPI_avg_taufac /= MIN_REAL_NUMBER + totMPI_mom_wind;
            fprintf(FdMomWinds, "%lg %g %g %g %g %g \n", All.Time,totMPI_n_wind,totMPI_prob_kick,totMPI_mom_wind,totMPI_avg_v,totMPI_avg_taufac); fflush(FdMomWinds);
            PRINT_STATUS(" ..momentum coupled: Time=%g Nkicked=%g (L/c)dt=%g Momkicks=%g V_avg=%g tau_j_mean=%g ", All.Time,totMPI_n_wind,totMPI_prob_kick,totMPI_mom_wind,totMPI_avg_v,totMPI_avg_taufac);
        }
    } // if(ThisTask==0)
    if(All.HighestActiveTimeBin == All.HighestOccupiedTimeBin && ThisTask == 0) {fflush(FdMomWinds);}
    PRINT_STATUS(" .. completed local Radiation-Pressure acceleration");
    CPU_Step[CPU_LOCALWIND] += measure_time(); /* collect timings and reset clock for next timing */
} // end routine :: void radiation_pressure_winds_consolidated(void)

#endif /* closes defined(GALSF_FB_FIRE_RT_LOCALRP)  */


    
    
    
/* Routines for simple FIRE local photo-ionization heating feedback model. This file was written by Phil Hopkins (phopkins@caltech.edu) for GIZMO. */
#if defined(GALSF_FB_FIRE_RT_HIIHEATING) && !defined(CHIMES_HII_REGIONS)
void HII_heating_singledomain(void)    /* this version of the HII routine only communicates with particles on the same processor */
{
#ifdef RT_CHEM_PHOTOION
    return; // the work here is done in the actual RT routines if this switch is enabled //
#endif
    if(All.HIIRegion_fLum_Coupled<=0) {return;}
    if(All.Time<=0) {return;}
    MyDouble *pos; MyFloat h_i, dt, rho;
    int startnode, numngb, j, n, i, NITER_HIIFB, MAX_N_ITERATIONS_HIIFB, jnearest,already_ionized,do_ionize,dummy;
    double totMPI_m_ionizing,totMPI_l_ionizing,totMPI_m_ionized,totMPI_m_ionizable,totMPI_avg_RHII,dx, dy, dz, h_i2, r2, r, u, u_to_temp_fac,mionizable,mionized,RHII,RHIIMAX,R_search,rnearest,stellum,uion,prob,rho_j,prandom,m_available,m_effective,RHII_initial,RHIImultiplier, total_l_ionizing,total_m_ionizing,total_m_ionizable,total_m_ionized,avg_RHII;
    total_l_ionizing=total_m_ionized=avg_RHII=total_m_ionizable=total_m_ionizing=0; totMPI_m_ionizing=totMPI_l_ionizing=totMPI_m_ionized=totMPI_m_ionizable=totMPI_avg_RHII=0;
    Ngblist = (int *) mymalloc("Ngblist",NumPart * sizeof(int));
    MAX_N_ITERATIONS_HIIFB = 5; NITER_HIIFB = 0;
    
    for(i = FirstActiveParticle; i >= 0; i = NextActiveParticle[i])
    {
#ifdef BH_HII_HEATING
        if((P[i].Type==5)||(((P[i].Type == 4)||((All.ComovingIntegrationOn==0)&&((P[i].Type == 2)||(P[i].Type==3))))))
#else
        if((P[i].Type == 4)||((All.ComovingIntegrationOn==0)&&((P[i].Type == 2)||(P[i].Type==3))))
#endif
        {
            dt = GET_PARTICLE_TIMESTEP_IN_PHYSICAL(i);
            if(dt<=0) continue; // don't keep going with this loop
            
            stellum = All.HIIRegion_fLum_Coupled * particle_ionizing_luminosity_in_cgs(i);
            if(stellum <= 0) continue;
            pos = P[i].Pos; rho = P[i].DensAroundStar; h_i = PPP[i].Hsml; total_m_ionizing += 1; total_l_ionizing += stellum;
            
            RHII = 4.67e-9*pow(stellum,0.333)*pow(rho*All.cf_a3inv*UNIT_DENSITY_IN_CGS,-0.66667);
            RHII /= All.cf_atime*UNIT_LENGTH_IN_CGS;
            RHIIMAX=240.0*pow(stellum,0.5)/(All.cf_atime*UNIT_LENGTH_IN_CGS); // crude estimate of where flux falls below cosmic background
            if(RHIIMAX < h_i) {RHIIMAX=h_i;}
            if(RHIIMAX > 5.0*h_i) {RHIIMAX=5.*h_i;}
            mionizable=NORM_COEFF*rho*RHII*RHII*RHII;
            double M_ionizing_emitted = (3.05e10 * PROTONMASS) * stellum * (dt * UNIT_TIME_IN_CGS) ; // number of ionizing photons times proton mass, gives max mass ionized
            mionizable = DMIN( mionizable , M_ionizing_emitted/UNIT_MASS_IN_CGS ); // in code units
            if(RHII>RHIIMAX) {RHII=RHIIMAX;}
            if(RHII<0.5*h_i) {RHII=0.5*h_i;}
            RHII_initial=RHII;
            
            prandom = get_random_number(P[i].ID + 7); // pre-calc the (eventually) needed random number
            // guesstimate if this is even close to being interesting for the particle masses of interest
            if(prandom < 2.0*mionizable/P[i].Mass) // prandom > this, won't be able to ionize anything interesting
            {
                mionized=0.0; total_m_ionizable += mionizable; h_i2=h_i*h_i;
                u_to_temp_fac = 0.59 * (5./3.-1.) * U_TO_TEMP_UNITS; /* assume fully-ionized gas with gamma=5/3 */
                uion = HIIRegion_Temp / u_to_temp_fac;
                startnode = All.MaxPart; jnearest=-1; rnearest=MAX_REAL_NUMBER; dummy=0; NITER_HIIFB=0;
                
                do {
                    jnearest=-1; rnearest=MAX_REAL_NUMBER;
                    R_search = RHII; if(h_i>R_search) R_search=h_i;
                    numngb = ngb_treefind_variable_threads(pos, R_search, -1, &startnode, 0, &dummy, &dummy, &dummy, Ngblist);
                    if(numngb>0)
                    {
                        int ngb_list_touse[numngb]; for(n=0; n<numngb; n++) {ngb_list_touse[n]=Ngblist[n];}
#if (GALSF_FB_FIRE_STELLAREVOLUTION > 2) // ??
                        qsort(ngb_list_touse, numngb, sizeof(int), compare_densities_for_sort); // sort on densities before processing, so ionize least-dense-first
#endif
                        for(n = 0; n < numngb; n++)
                        {
                            j = ngb_list_touse[n];
                            if(P[j].Type == 0 && P[j].Mass > 0)
                            {
                                dx = pos[0] - P[j].Pos[0]; dy = pos[1] - P[j].Pos[1]; dz = pos[2] - P[j].Pos[2];
                                NEAREST_XYZ(dx,dy,dz,1); /*  now find the closest image in the given box size */
                                r2 = dx * dx + dy * dy + dz * dz; r=sqrt(r2);
                                /* check whether the particle is already ionized */
                                already_ionized = 0; rho_j = Get_Gas_density_for_energy_i(j);
                                if(SphP[j].InternalEnergy<SphP[j].InternalEnergyPred) {u=SphP[j].InternalEnergy;} else {u=SphP[j].InternalEnergyPred;}
#if (GALSF_FB_FIRE_STELLAREVOLUTION > 2) // ??
                                if((SphP[j].DelayTimeHII>0) || (SphP[i].Ne>0.8) || (u>5.*uion)) {already_ionized=1;} /* already mostly ionized by formal ionization fraction */
#else
                                if((SphP[j].DelayTimeHII > 0)||(u>uion)) {already_ionized=1;}
#endif
                                /* now, if inside RHII and mionized<mionizeable and not already ionized, can be ionized! */
                                do_ionize=0; prob=0;
                                if((r<=RHII)&&(already_ionized==0)&&(mionized<mionizable))
                                {
                                    m_effective = P[j].Mass*(SphP[j].Density/rho);
                                    // weight by density b/c of how the recombination rate in each particle scales
                                    m_available = mionizable-mionized;
                                    if(m_effective<=m_available) {
                                        do_ionize=1; prob = 1.001;
                                    } else {
                                        prob = m_available/m_effective; // determine randomly if ionized
                                        if(prandom < prob) do_ionize=1;
                                    } // if(m_effective<=m_available) {
                                    if(do_ionize==1) {already_ionized=do_the_local_ionization(j,dt,i);}
                                    mionized += prob*m_effective;
                                } // if((r<=RHII)&&(already_ionized==0)&&(mionized<mionizable))
                                
                                /* if nearest un-ionized particle, mark as such */
#if (GALSF_FB_FIRE_STELLAREVOLUTION > 2) // ??
                                if((SphP[j].Density<rnearest)&&(already_ionized==0)) {rnearest = SphP[j].Density; jnearest = j;} // rank by density, not distance
#else
                                if((r<rnearest)&&(already_ionized==0)) {rnearest = r; jnearest = j;}
#endif
                            } // if(P[j].Type == 0 && P[j].Mass > 0)
                        } // for(n = 0; n < numngb; n++)
                    } // if(numngb>0)
                    
                    // if still have photons and jnearest is un-ionized
                    if((mionized<mionizable)&&(jnearest>=0))
                    {
                        j=jnearest; m_effective=P[j].Mass*(SphP[j].Density/rho); m_available=mionizable-mionized; prob=m_available/m_effective; do_ionize=0;
                        if(prandom < prob) {do_ionize=1;}
                        if(do_ionize==1) {already_ionized=do_the_local_ionization(j,dt,i);}
                        mionized += prob*m_effective;
                    } // if((mionized<mionizable)&&(jnearest>=0))
                    
                    /* now check if we have ionized sufficient material, and if not, iterate with larger regions until we do */
                    RHIImultiplier=1.10;
                    if(mionized < 0.95*mionizable)
                    {
                        /* ok, this guy did not find enough gas to ionize, it needs to expand its search */
                        if((RHII >= 30.0*RHII_initial)||(RHII>=RHIIMAX)||(NITER_HIIFB >= MAX_N_ITERATIONS_HIIFB))
                        {
                            /* we're done looping, this is just too big an HII region */
                            mionized = 1.001*mionizable;
                        } else {
                            /* in this case we're allowed to keep expanding RHII */
                            if(mionized <= 0)
                            {
                                RHIImultiplier = 2.0;
                            } else {
                                RHIImultiplier = pow(mionized/mionizable , -0.333);
                                if(RHIImultiplier>5.0) {RHIImultiplier=5.0;}
                                if(RHIImultiplier<1.26) {RHIImultiplier=1.26;}
                            } // if(mionized <= 0)
                            RHII *= RHIImultiplier; if(RHII>1.26*RHIIMAX) {RHII=1.26*RHIIMAX;}
                            startnode=All.MaxPart; // this will trigger the while loop to continue
                        } // if((RHII >= 5.0*RHII_initial)||(RHII>=RHIIMAX)||(NITER_HIIFB >= MAX_N_ITERATIONS_HIIFB))
                    } // if(mionized < 0.95*mionizable)
                    NITER_HIIFB++;
                } while(startnode >= 0);
                total_m_ionized += mionized; avg_RHII += RHII;
            } // if(prandom < 2.0*mionizable/P[i].Mass)
        } // if((P[i].Type == 4)||(P[i].Type == 2)||(P[i].Type == 3))
    } // for(i = FirstActiveParticle; i >= 0; i = NextActiveParticle[i])
    myfree(Ngblist);
    
    MPI_Reduce(&total_m_ionizing, &totMPI_m_ionizing, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&total_l_ionizing, &totMPI_l_ionizing, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&total_m_ionized, &totMPI_m_ionized, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&avg_RHII, &totMPI_avg_RHII, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
    if(ThisTask == 0)
    {
        if(totMPI_m_ionizing>0)
        {
            totMPI_avg_RHII /= totMPI_m_ionizing;
            PRINT_STATUS("HII PhotoHeating: Time=%g: %g sources with L_tot/erg=%g ; M_ionized=%g ; <R_HII>=%g", All.Time,totMPI_m_ionizing,totMPI_l_ionizing,totMPI_m_ionized,totMPI_avg_RHII);
            fprintf(FdHIIHeating, "%lg %g %g %g %g \n",All.Time,totMPI_m_ionizing,totMPI_l_ionizing,totMPI_m_ionized,totMPI_avg_RHII); fflush(FdHIIHeating);
        }
        if(All.HighestActiveTimeBin == All.HighestOccupiedTimeBin) {fflush(FdHIIHeating);}
    } // ThisTask == 0
    CPU_Step[CPU_HIIHEATING] += measure_time();
} // void HII_heating_singledomain(void)



int do_the_local_ionization(int target, double dt, int source)
{
#if (GALSF_FB_FIRE_STELLAREVOLUTION <= 2) // ??
    SphP[target].InternalEnergy = DMAX(SphP[target].InternalEnergy , HIIRegion_Temp / (0.59 * (5./3.-1.) * U_TO_TEMP_UNITS)); /* assume fully-ionized gas with gamma=5/3 */
    SphP[target].InternalEnergyPred = SphP[target].InternalEnergy; /* full reset of the internal energy */
#else
    double delta_U_of_ionization = (20.-13.6) * ((ELECTRONVOLT_IN_ERGS / PROTONMASS) / UNIT_SPECEGY_IN_CGS) * (1.-DMAX(0.,DMIN(1.,SphP[target].Ne/1.5))); /* energy injected per unit mass, in code units, by ionization, assuming each atom absorbs, and mean energy of absorbed photons is given by x=18 eV here (-13.6 for energy of ionization) */
    double Theat_star = 1.38 * 3.2, Z_sol = 1.; // typical IMF-averaged temp of ionizing star=32,000 K, with effective ionization temperature parameter psi=1.38 (temp of ionized e's in units of stellar temp). Then metallicity in solar units.
#ifdef METALS
    Z_sol = P[target].Metallicity[0]/All.SolarAbundances[0]; // set metallicity
#endif
    double t4_eqm = DMIN( 1.5*Theat_star , 3.85/DMAX(log(DMAX(390.*Z_sol/Theat_star,1.001)),0.01) ); // equilibrium H2 region temperature in 1e4 K: 1.5*Theat = eqm temp for pure-H region, while second expression assumes eqm cooling with O+C, etc, but breaks down at low-Z when metals don't dominate cooling.
    double u_eqm = (t4_eqm * HIIRegion_Temp) / (0.59 * (5./3.-1.) * U_TO_TEMP_UNITS); // converted to specific internal energy, assuming full ionization
    double u_post_ion_no_cooling = SphP[target].InternalEnergy + delta_U_of_ionization; // energy after ionization, before any cooling
    double u_final = DMIN( u_post_ion_no_cooling , u_eqm ), du = u_final-SphP[target].InternalEnergy; // don't heat to higher temperature than intial energy of ionization allows
    SphP[target].InternalEnergy = u_final; SphP[target].InternalEnergyPred = DMAX(SphP[target].InternalEnergyPred + du , 1.e-3*SphP[target].InternalEnergyPred); /* add it */
#endif
    SphP[target].DelayTimeHII = DMIN(dt, 10./UNIT_TIME_IN_MYR); /* tell the code to flag this in the cooling subroutine */
    SphP[target].Ne = 1.0 + 2.0*yhelium(target); /* set the cell to fully ionized */
    return 1;
}

#endif // GALSF_FB_FIRE_RT_HIIHEATING



#ifdef CHIMES_HII_REGIONS 
/* This routine is based heavily on the HII_heating_singledomain() routine 
 * used in FIRE for HII heating. I have modified this to make use of the 
 * stellar luminosities used with the CHIMES routines, and it now only flags 
 * gas particles deemed to be within HII regions so that shielding in the CHIMES 
 * routines can be disabled for this particles. This routine does not actually 
 * heat and ionise these particles explicitly. */
void chimes_HII_regions_singledomain(void)
{
  if(All.Time<=0) 
    return;

  MyDouble *pos;
  int startnode, numngb, j, n, i, k;
  int do_ionize,dummy, n_iter_HII, age_bin;
  MyFloat h_i, dt, rho;
  double dx, dy, dz, r2, r, eps_cgs, prandom;
  double mionizable, mionized, RHII, RHIImax, RHIImin, R_search;
  double stellum, stellum_G0, prob, M_ionizing_emitted;
  double m_available, m_effective, RHIImultiplier;
  double stellar_age, stellar_mass, log_age_Myr;
  
  int max_n_iterations_HII = 5; 

  Ngblist = (int *) mymalloc("Ngblist",NumPart * sizeof(int));
    
  for(i = FirstActiveParticle; i >= 0; i = NextActiveParticle[i])
    {
      if((P[i].Type == 4) || ((All.ComovingIntegrationOn==0) && ((P[i].Type == 2) || (P[i].Type==3))))
	{
        dt = GET_PARTICLE_TIMESTEP_IN_PHYSICAL(i);
        if(dt<=0) continue; // don't keep going with this loop

	  stellar_age = evaluate_stellar_age_Gyr(P[i].StellarAge); 
	  stellar_mass = P[i].Mass * UNIT_MASS_IN_SOLAR; 
	  
	  // stellum is the number of H-ionising photons per second 
	  // produced by the star particle 
	  stellum = chimes_ion_luminosity(stellar_age * 1000.0, stellar_mass); 
	  if(stellum <= 0) 
	    continue;
	  
	  // Luminosity in the 6-13.6 eV band. 
	  stellum_G0 = chimes_G0_luminosity(stellar_age * 1000.0, stellar_mass); 

	  // Gravitational Softening (cgs units) 
	  eps_cgs = All.SofteningTable[P[i].Type] * All.cf_atime * UNIT_LENGTH_IN_CGS;
	  
	  // Determine stellar age bin 
	  log_age_Myr = log10(stellar_age * 1000.0); 	  
	  if (log_age_Myr < CHIMES_LOCAL_UV_AGE_LOW) 
	    age_bin = 0; 
	  else if (log_age_Myr < CHIMES_LOCAL_UV_AGE_MID) 
	    age_bin = (int) floor(((log_age_Myr - CHIMES_LOCAL_UV_AGE_LOW) / CHIMES_LOCAL_UV_DELTA_AGE_LOW) + 1); 
	  else 
	    { 
	      age_bin = (int) floor((((log_age_Myr - CHIMES_LOCAL_UV_AGE_MID) / CHIMES_LOCAL_UV_DELTA_AGE_HI) + ((CHIMES_LOCAL_UV_AGE_MID - CHIMES_LOCAL_UV_AGE_LOW) / CHIMES_LOCAL_UV_DELTA_AGE_LOW)) + 1); 
	      if (age_bin > CHIMES_LOCAL_UV_NBINS - 1) 
		age_bin = CHIMES_LOCAL_UV_NBINS - 1; 
	    }
	  
	  pos = P[i].Pos;
	  rho = P[i].DensAroundStar;
	  h_i = PPP[i].Hsml;
	  
	  // Stromgren radius, RHII, computed using a case B recombination coefficient 
	  // at 10^4 K of 2.59e-13 cm^3 s^-1, as used in CHIMES, and assuming a 
	  // Hydrogen mass fraction XH = 0.7. 
	  RHII = 1.7376e-12 * pow(stellum, 0.33333) * pow(rho * All.cf_a3inv * UNIT_DENSITY_IN_CGS, -0.66667);
	  
	  // Convert RHII from cm to code units 
	  RHII /= All.cf_atime*UNIT_LENGTH_IN_CGS;
	  
	  /* Impose a maximum RHII, to prevent the code trying to search 
	   * for neighbours too far away. Unlike the standard FIRE routines, 
	   * I do not base this on an estimate for where the flux falls below 
	   * the cosmic background. Instead, note that, for the maximum ionising 
	   * flux per Msol that we get from the Starburst99 models (which occurs 
	   * at a stellar age of 3.71 Myr), the ratio of ionisable gas mass to 
	   * stellar mass is 286 / nH. In other words, at nH = 1 cm^-3, a single 
	   * star particle can ionise 286 gas particles (assuming equal-mass 
	   * particles). The star particle's smoothing length h_i should contain
	   * DesNumNgb gas particles (typically 32). So if we set RHIImax to 
	   * 10 * h_i, this should be enough to handle HII regions down to 
	   * nH ~ 1 cm^-3. */ 
	  RHIImax = 10.0 * h_i; 
	  RHIImin = 0.5 * h_i; 
	  
	  // Ionizable gas mass in code units, based on the gas density 
	  // evaluated at the position of the star. Prefactor is 4pi/3. 
	  mionizable = 4.18879 * rho * pow(RHII, 3.0);  

	  // number of ionizing photons times proton mass, gives max mass ionized 
	  M_ionizing_emitted = PROTONMASS * stellum * (dt * UNIT_TIME_IN_CGS); // in cgs
	  mionizable = DMIN(mionizable , M_ionizing_emitted/UNIT_MASS_IN_CGS); // in code units
	  
	  // Now limit RHII to be between the min and max defined above. 
	  if(RHII > RHIImax) 
	    RHII = RHIImax;

	  if(RHII < RHIImin) 
	    RHII = RHIImin;

	  /* Skip star particles that can ionise <10% of its own mass (this is  
	   * lower than 50% here, because there can be some variation between 
	   * particle masses, and in gas densities). */ 
	  if(mionizable / P[i].Mass > 0.1) 
	    {	      
	      prandom = get_random_number(P[i].ID + 7); 
	      mionized = 0.0;
	      startnode = All.MaxPart;     /* root node */
	      dummy = 0; 
	      n_iter_HII = 0;
	     
	      do {
		R_search = RHII;
		if(h_i > R_search) 
		  R_search = h_i;
		numngb = ngb_treefind_variable_threads(pos, R_search, -1, &startnode, 0, &dummy, &dummy, &dummy, Ngblist);
		if(numngb>0)
		  {
		    for(n = 0; n < numngb; n++)
		      {
			j = Ngblist[n];
			if(P[j].Type == 0 && P[j].Mass > 0)
			  {
			    dx = pos[0] - P[j].Pos[0];
			    dy = pos[1] - P[j].Pos[1];
			    dz = pos[2] - P[j].Pos[2];
			    NEAREST_XYZ(dx, dy, dz, 1); /*  now find the closest image in the given box size  */
			    r2 = dx * dx + dy * dy + dz * dz;
			    r = sqrt(r2);
			   
			    /* If inside RHII and mionized<mionizeable and not already ionized, can be ionized! */
			    do_ionize=0; 
			    if((r <= RHII) && (SphP[j].DelayTimeHII <= 0) && (mionized < mionizable)) 
			      {
				m_effective = P[j].Mass * (SphP[j].Density / rho);
				// weight by density b/c of how the recomination rate in each particle scales 

				m_available = mionizable - mionized;
				if(m_effective <= m_available) 
				  {
				    // Enough photons to ionise the whole particle. 
				    do_ionize = 1;
				    mionized += m_effective; 
				  }
				else 
				  {
				    // Not enough to ionise a whole particle. 
				    // Use random number to determine whether 
				    // to ionise. 
				    prob = m_available/m_effective; 
				   
				    if(prandom < prob) 
				      do_ionize = 1;

				    mionized += prob * m_effective; 
				  } // if(m_effective<=m_available) 
			       
				if(do_ionize==1) 
				  {
				    SphP[j].DelayTimeHII = dt;
				   
				    for(k = 0; k < CHIMES_LOCAL_UV_NBINS; k++)  {SphP[j].Chimes_fluxPhotIon_HII[k] = 0; SphP[j].Chimes_G0_HII[k] = 0;}
				    
				    SphP[j].Chimes_fluxPhotIon_HII[age_bin] = (1.0 - All.Chimes_f_esc_ion) * stellum / (pow(r * All.cf_atime * UNIT_LENGTH_IN_CGS, 2.0) + pow(eps_cgs, 2.0)) ;
				    SphP[j].Chimes_G0_HII[age_bin] = (1.0 - All.Chimes_f_esc_G0) * stellum_G0 / (pow(r * All.cf_atime * UNIT_LENGTH_IN_CGS, 2.0) + pow(eps_cgs, 2.0));
				  }
			      } // if((r <= RHII) && (SphP[j].DelayTimeHII <= 0) && (mionized<mionizable)) 
			  } // if(P[j].Type == 0 && P[j].Mass > 0)
		      } // for(n = 0; n < numngb; n++)
		  } // if(numngb>0)

		/* now check if we have ionized sufficient material, and if not, 
		   iterate with larger regions until we do */
		RHIImultiplier=1.10;
		if(mionized < 0.95 * mionizable) 
		  {
		    /* ok, this guy did not find enough gas to ionize, it needs to expand its search */
		    if((RHII >= RHIImax) || (n_iter_HII >= max_n_iterations_HII))
		      {
			/* we're done looping, this is just too big an HII region */
			mionized = 1.001*mionizable;
		      } 
		    else 
		      {
			/* in this case we're allowed to keep expanding RHII */
			if(mionized <= 0) 
			  RHIImultiplier = 2.0;
			else 
			  {
			    RHIImultiplier = pow(mionized / mionizable, -0.333);
			    if(RHIImultiplier > 5.0) 
			      RHIImultiplier=5.0;
			    if(RHIImultiplier < 1.26) 
			      RHIImultiplier=1.26;
			  } // if(mionized <= 0) 
		       
			RHII *= RHIImultiplier;
			if(RHII > 1.26*RHIImax) 
			  RHII=1.26*RHIImax;

			startnode=All.MaxPart; // this will trigger the while loop to continue
		      } // if((RHII>=RHIImax) || (n_iter_HII >= max_n_iterations_HII))
		  } // if(mionized < 0.95*mionizable) 
		n_iter_HII++;
	      } while(startnode >= 0);
	    } // if(mionizable / P[i].Mass > 0.1)
	} // if((P[i].Type == 4)||(P[i].Type == 2)||(P[i].Type == 3))
    } // for(i = FirstActiveParticle; i >= 0; i = NextActiveParticle[i])
  myfree(Ngblist);
  CPU_Step[CPU_HIIHEATING] += measure_time(); /* collect timings and reset clock for next timing */
}
#endif // CHIMES_HII_REGIONS 
