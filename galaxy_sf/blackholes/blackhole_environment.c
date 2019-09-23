/*! \file blackhole_environment.c
*  \brief routines for evaluating black hole environment
*/
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <gsl/gsl_math.h>
#include "../../allvars.h"
#include "../../proto.h"
#include "../../kernel.h"
/*
 * This file was largely written by Phil Hopkins (phopkins@caltech.edu) for GIZMO.
 *   It was based on a similar file in GADGET3 by Volker Springel (volker.springel@h-its.org),
 *   but the physical modules for black hole accretion and feedback have been
 *   replaced, and the algorithm for their coupling is new to GIZMO.  This file was modified
 *   by Paul Torrey (ptorrey@mit.edu) on 1/9/15 for clairity.  The main functional difference is that BlackholeTempInfo
 *   is now allocated only for N_active_loc_BHs, rather than NumPart (as was done before).  Some
 *   extra index gymnastics are required to follow this change through in the MPI comm routines.
 *   Cleanup, de-bugging, and consolidation of routines by Xiangcheng Ma
 *   (xchma@caltech.edu) followed on 05/15/15; re-integrated by PFH. Massive updates in 2019
 *   from David Guszejnov and Mike Grudic to incorporate and develop single star modules, and
 *   from PFH to integrate those and re-write the parallelism entirely to conform to the newer
 *   code standards and be properly multi-threaded.
 */


#define MASTER_FUNCTION_NAME blackhole_environment_evaluate /* name of the 'core' function doing the actual inter-neighbor operations. this MUST be defined somewhere as "int MASTER_FUNCTION_NAME(int target, int mode, int *exportflag, int *exportnodecount, int *exportindex, int *ngblist, int loop_iteration)" */
#define CONDITIONFUNCTION_FOR_EVALUATION if(P[i].Type==5) /* function for which elements will be 'active' and allowed to undergo operations. can be a function call, e.g. 'density_is_active(i)', or a direct function call like 'if(P[i].Mass>0)' */
#include "../../system/code_block_xchange_initialize.h" /* pre-define all the ALL_CAPS variables we will use below, so their naming conventions are consistent and they compile together, as well as defining some of the function calls needed */

/* this structure defines the variables that need to be sent -from- the 'searching' element */
struct INPUT_STRUCT_NAME
{
    int NodeList[NODELISTLENGTH]; MyDouble Pos[3]; MyFloat Vel[3], Hsml; MyIDType ID;
#if defined(BH_GRAVCAPTURE_GAS) || (BH_GRAVACCRETION == 8)
    MyDouble Mass;
#endif
#if defined(BH_GRAVCAPTURE_FIXEDSINKRADIUS)
    MyFloat SinkRadius;
#endif  
#if (ADAPTIVE_GRAVSOFT_FORALL & 32)
    MyFloat AGS_Hsml;
#endif
#ifdef BH_WAKEUP_GAS
    MyFloat TimeBin;
#endif
#if defined(BH_RETURN_ANGMOM_TO_GAS)
    MyFloat BH_Specific_AngMom[3];
#endif
}
*DATAIN_NAME, *DATAGET_NAME; /* dont mess with these names, they get filled-in by your definitions automatically */

/* this subroutine assigns the values to the variables that need to be sent -from- the 'searching' element */
static inline void INPUTFUNCTION_NAME(struct INPUT_STRUCT_NAME *in, int i, int loop_iteration)
{
    int k; for(k=0;k<3;k++) {in->Pos[k]=P[i].Pos[k]; in->Vel[k]=P[i].Vel[k];} /* good example - always needed */
    in->Hsml = PPP[i].Hsml; in->ID = P[i].ID;
#if defined(BH_GRAVCAPTURE_GAS) || (BH_GRAVACCRETION == 8)
    in->Mass = P[i].Mass;
#endif
#ifdef BH_GRAVCAPTURE_FIXEDSINKRADIUS
    in->SinkRadius = PPP[i].SinkRadius;
#endif
#if (ADAPTIVE_GRAVSOFT_FORALL & 32)
    in->AGS_Hsml = PPP[i].AGS_Hsml;
#endif
#ifdef BH_WAKEUP_GAS
    in->TimeBin = P[i].TimeBin;
#endif
#if defined(BH_RETURN_ANGMOM_TO_GAS)
    for(k=0;k<3;k++) {in->BH_Specific_AngMom[k]=P[i].BH_Specific_AngMom[k];}
#endif
}


/* this structure defines the variables that need to be sent -back to- the 'searching' element */
struct OUTPUT_STRUCT_NAME
{ /* define variables below as e.g. "double X;" */
MyFloat BH_InternalEnergy, Mgas_in_Kernel, Mstar_in_Kernel, Malt_in_Kernel;
MyFloat Jgas_in_Kernel[3], Jstar_in_Kernel[3], Jalt_in_Kernel[3]; // mass/angular momentum for GAS/STAR/TOTAL components computed always now
#ifdef BH_DYNFRICTION
    MyFloat DF_rms_vel, DF_mean_vel[3], DF_mmax_particles;
#endif
#if defined(BH_OUTPUT_MOREINFO)
    MyFloat Sfr_in_Kernel;
#endif
#if defined(BH_PHOTONMOMENTUM) || defined(BH_WIND_CONTINUOUS)
    MyFloat GradRho_in_Kernel[3];
#endif
#if defined(BH_BONDI) || defined(BH_DRAG) || (BH_GRAVACCRETION >= 5)
    MyFloat BH_SurroundingGasVel[3];
#endif
#if (BH_GRAVACCRETION == 8)
    MyFloat hubber_mdot_vr_estimator, hubber_mdot_disk_estimator, hubber_mdot_bondi_limiter;
#endif
#if defined(BH_GRAVCAPTURE_GAS)
    MyFloat mass_to_swallow_edd;
#endif
#if defined(BH_RETURN_ANGMOM_TO_GAS)
    MyFloat angmom_prepass_sum_for_passback[3];
#endif
#if defined(BH_ACCRETE_NEARESTFIRST) && defined(BH_GRAVCAPTURE_GAS)
    MyDouble BH_dr_to_NearestGasNeighbor;
#endif
}
*DATARESULT_NAME, *DATAOUT_NAME; /* dont mess with these names, they get filled-in by your definitions automatically */

/* simple routine to add quantities to BlackholeTempInfo */
static inline void OUTPUTFUNCTION_NAME(struct OUTPUT_STRUCT_NAME *out, int i, int mode, int loop_iteration)
{
    int target = P[i].IndexMapToTempStruc, k=0;
    ASSIGN_ADD(BlackholeTempInfo[target].BH_InternalEnergy,out->BH_InternalEnergy,mode);
    ASSIGN_ADD(BlackholeTempInfo[target].Mgas_in_Kernel,out->Mgas_in_Kernel,mode);
    ASSIGN_ADD(BlackholeTempInfo[target].Mstar_in_Kernel,out->Mstar_in_Kernel,mode);
    ASSIGN_ADD(BlackholeTempInfo[target].Malt_in_Kernel,out->Malt_in_Kernel,mode);
    for(k=0;k<3;k++) {ASSIGN_ADD(BlackholeTempInfo[target].Jgas_in_Kernel[k],out->Jgas_in_Kernel[k],mode);}
    for(k=0;k<3;k++) {ASSIGN_ADD(BlackholeTempInfo[target].Jstar_in_Kernel[k],out->Jstar_in_Kernel[k],mode);}
    for(k=0;k<3;k++) {ASSIGN_ADD(BlackholeTempInfo[target].Jalt_in_Kernel[k],out->Jalt_in_Kernel[k],mode);}
#ifdef BH_DYNFRICTION
    ASSIGN_ADD(BlackholeTempInfo[target].DF_rms_vel,out->DF_rms_vel,mode);
    for(k=0;k<3;k++) {ASSIGN_ADD(BlackholeTempInfo[target].DF_mean_vel[k],out->DF_mean_vel[k],mode);}
    if(mode==0) {BlackholeTempInfo[target].DF_mmax_particles = out->DF_mmax_particles;}
        else {if(out->DF_mmax_particles > BlackholeTempInfo[target].DF_mmax_particles) {BlackholeTempInfo[target].DF_mmax_particles = out->DF_mmax_particles;}}
#endif
#if defined(BH_OUTPUT_MOREINFO)
    ASSIGN_ADD(BlackholeTempInfo[target].Sfr_in_Kernel,out->Sfr_in_Kernel,mode);
#endif
#if defined(BH_PHOTONMOMENTUM) || defined(BH_WIND_CONTINUOUS)
    for(k=0;k<3;k++) {ASSIGN_ADD(BlackholeTempInfo[target].GradRho_in_Kernel[k],out->GradRho_in_Kernel[k],mode);}
#endif
#if defined(BH_BONDI) || defined(BH_DRAG) || (BH_GRAVACCRETION >= 5)
    for(k=0;k<3;k++) {ASSIGN_ADD(BlackholeTempInfo[target].BH_SurroundingGasVel[k],out->BH_SurroundingGasVel[k],mode);}
#endif
#if (BH_GRAVACCRETION == 8)
    ASSIGN_ADD(BlackholeTempInfo[target].hubber_mdot_bondi_limiter,out->hubber_mdot_bondi_limiter,mode);
    ASSIGN_ADD(BlackholeTempInfo[target].hubber_mdot_vr_estimator,out->hubber_mdot_vr_estimator,mode);
    ASSIGN_ADD(BlackholeTempInfo[target].hubber_mdot_disk_estimator,out->hubber_mdot_disk_estimator,mode);
#endif
#if defined(BH_GRAVCAPTURE_GAS)
    ASSIGN_ADD(BlackholeTempInfo[target].mass_to_swallow_edd, out->mass_to_swallow_edd, mode);
#endif
#if defined(BH_RETURN_ANGMOM_TO_GAS)
    for(k=0;k<3;k++) {ASSIGN_ADD(BlackholeTempInfo[target].angmom_prepass_sum_for_passback[k],out->angmom_prepass_sum_for_passback[k],mode);}
#endif
#if defined(BH_ACCRETE_NEARESTFIRST) && defined(BH_GRAVCAPTURE_GAS)
    if(mode==0) {P[i].BH_dr_to_NearestGasNeighbor=out->BH_dr_to_NearestGasNeighbor;} else {if(P[i].BH_dr_to_NearestGasNeighbor > out->BH_dr_to_NearestGasNeighbor;) {P[i].BH_dr_to_NearestGasNeighbor=out->BH_dr_to_NearestGasNeighbor;}}
#endif
}


/* for new quantities calculated in environment loop, divide out weights and convert to physical units */
void bh_normalize_temp_info_struct_after_environment_loop(int i);
void bh_normalize_temp_info_struct_after_environment_loop(int i)
{
    int k; k=0;
    if(BlackholeTempInfo[i].Mgas_in_Kernel > 0)
    {
        BlackholeTempInfo[i].BH_InternalEnergy /= BlackholeTempInfo[i].Mgas_in_Kernel;
#if defined(BH_BONDI) || defined(BH_DRAG) || (BH_GRAVACCRETION >= 5)
        for(k=0;k<3;k++) {BlackholeTempInfo[i].BH_SurroundingGasVel[k] /= BlackholeTempInfo[i].Mgas_in_Kernel * All.cf_atime;}
#endif
    }
    else {BlackholeTempInfo[i].BH_InternalEnergy = 0;}
    // DAA: add GAS/STAR mass/angular momentum to the TOTAL mass/angular momentum in kernel
    BlackholeTempInfo[i].Malt_in_Kernel += (BlackholeTempInfo[i].Mgas_in_Kernel + BlackholeTempInfo[i].Mstar_in_Kernel);
    for(k=0;k<3;k++) {BlackholeTempInfo[i].Jalt_in_Kernel[k] += (BlackholeTempInfo[i].Jgas_in_Kernel[k] + BlackholeTempInfo[i].Jstar_in_Kernel[k]);}
#ifdef BH_DYNFRICTION  // DAA: normalize by the appropriate MASS in kernel depending on selected option
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


/* routine to return the values we need of the properties of the gas, stars, etc in the vicinity of the BH -- these all factor into the BHAR */
int blackhole_environment_evaluate(int target, int mode, int *exportflag, int *exportnodecount, int *exportindex, int *ngblist, int loop_iteration)
{
    /* initialize variables before loop is started */
    int startnode, numngb, listindex = 0, j, k, n; struct INPUT_STRUCT_NAME local; struct OUTPUT_STRUCT_NAME out; memset(&out, 0, sizeof(struct OUTPUT_STRUCT_NAME)); /* define variables and zero memory and import data for local target*/
    if(mode == 0) {INPUTFUNCTION_NAME(&local, target, loop_iteration);} else {local = DATAGET_NAME[target];} /* imports the data to the correct place and names */
    double ags_h_i, h_i=local.Hsml, hinv=1./h_i, hinv3=hinv*hinv*hinv; ags_h_i=All.ForceSoftening[5];
#if (ADAPTIVE_GRAVSOFT_FORALL & 32)
    ags_h_i = local.AGS_Hsml;
#endif
#ifdef BH_ACCRETE_NEARESTFIRST
    out.BH_dr_to_NearestGasNeighbor = MAX_REAL_NUMBER; // initialize large value
#endif
#if defined(BH_PHOTONMOMENTUM) || defined(BH_WIND_CONTINUOUS) || (BH_GRAVACCRETION == 8)
    MyFloat wk, dwk, u; // initialized here to prevent some annoying compiler warnings
#endif
    /* Now start the actual neighbor computation for this particle */
    if(mode == 0) {startnode = All.MaxPart; /* root node */} else {startnode = DATAGET_NAME[target].NodeList[0]; startnode = Nodes[startnode].u.d.nextnode;    /* open it */}
    while(startnode >= 0) {
        while(startnode >= 0) {
            numngb = ngb_treefind_pairs_threads_targeted(local.Pos, h_i, target, &startnode, mode, exportflag, exportnodecount, exportindex, ngblist, BH_NEIGHBOR_BITFLAG);
            if(numngb < 0) return -1;
            for(n = 0; n < numngb; n++)
            {
                j = Ngblist[n];
#ifdef BH_WAKEUP_GAS
                if (local.Timebin < P[j].LowestBHTimeBin) {P[j].LowestBHTimeBin = local.Timebin;}
#endif
                if( (P[j].Mass > 0) && (P[j].Type != 5) && (P[j].ID != local.ID) )
                {
                    double wt = P[j].Mass;
                    double dP[3], dv[3]; for(k=0;k<3;k++) {dP[k]=P[j].Pos[k]-local.Pos[k]; dv[k]=P[j].Vel[k]-local.Vel[k];}
                    NEAREST_XYZ(dP[0],dP[1],dP[2],-1); /*  find the closest image in the given box size  */
#ifdef BOX_SHEARING
                    if(local.Pos[0] - P[j].Pos[0] > +boxHalf_X) {dv[BOX_SHEARING_PHI_COORDINATE] -= Shearing_Box_Vel_Offset;}
                    if(local.Pos[0] - P[j].Pos[0] < -boxHalf_X) {dv[BOX_SHEARING_PHI_COORDINATE] += Shearing_Box_Vel_Offset;}
#endif

#ifdef BH_DYNFRICTION
#if (BH_DYNFRICTION == 1)    // DAA: dark matter + stars
                    if( !(P[j].Type==0) )
#if (BH_REPOSITION_ON_POTMIN == 2)
                    if( (P[j].Type != 5) )
#endif
#elif (BH_DYNFRICTION == 2)  // DAA: stars only
                    if( P[j].Type==4 || ((P[j].Type==2||P[j].Type==3) && !(All.ComovingIntegrationOn)) )
#endif
                    {
                        double wtfac = wt;
#if (BH_REPOSITION_ON_POTMIN == 2)
                        double rfac = (dP[0]*dP[0] + dP[1]*dP[1] + dP[2]*dP[2]) * (10./(h_i*h_i) + 0.1/(All.ForceSoftening[5]*All.ForceSoftening[5]));
                        wtfac = wt / (1. + rfac); // simple function scaling ~ 1/r^2 for large r, to weight elements closer to the BH, so doesnt get 'pulled' by far-away elements //
#endif
                        if(P[j].Mass>out.DF_mmax_particles) out.DF_mmax_particles=P[j].Mass;
                        for (k=0;k<3;k++)
                        {
                            out.DF_mean_vel[k] += wt*dv[k];
#if (BH_REPOSITION_ON_POTMIN == 2)
                            out.DF_rms_vel += wt;
#else
                            out.DF_rms_vel += wt*dv[k]*dv[k];
#endif
                        }
                    }
#endif
                    
                    /* DAA: compute mass/angular momentum for GAS/STAR/DM components within BH kernel
                            this is done always now (regardless of the specific BH options used) */
                    if(P[j].Type==0)
                    {
                        /* we found gas in BH's kernel */
                        out.Mgas_in_Kernel += wt;
                        out.BH_InternalEnergy += wt*SphP[j].InternalEnergy;
                        out.Jgas_in_Kernel[0] += wt*(dP[1]*dv[2] - dP[2]*dv[1]); out.Jgas_in_Kernel[1] += wt*(dP[2]*dv[0] - dP[0]*dv[2]); out.Jgas_in_Kernel[2] += wt*(dP[0]*dv[1] - dP[1]*dv[0]);
#if defined(BH_OUTPUT_MOREINFO)
                        out.Sfr_in_Kernel += SphP[j].Sfr;
#endif
#if defined(BH_PHOTONMOMENTUM) || defined(BH_WIND_CONTINUOUS)
                        u=0; for(k=0;k<3;k++) {u+=dP[k]*dP[k];}
                        u=sqrt(u)/h_i; kernel_main(u,hinv3,hinv3*hinv,&wk,&dwk,1);
                        dwk /= u*h_i; for(k=0;k<3;k++) out.GradRho_in_Kernel[k] += wt * dwk * fabs(dP[k]);
#endif
#if defined(BH_BONDI) || defined(BH_DRAG) || (BH_GRAVACCRETION >= 5)
                        for(k=0;k<3;k++) {out.BH_SurroundingGasVel[k] += wt*dv[k];}
#endif
#if defined(BH_RETURN_ANGMOM_TO_GAS) /* We need a normalization factor for angular momentum feedback so we will go over all the neighbours */
                        double r2j=dP[0]*dP[0]+dP[1]*dP[1]+dP[2]*dP[2], Lrj=local.BH_Specific_AngMom[0]*dP[0]+local.BH_Specific_AngMom[1]*dP[1]+local.BH_Specific_AngMom[2]*dP[2];
                        for(k=0;k<3;k++) {out.angmom_prepass_sum_for_passback[k] += wt*(local.BH_Specific_AngMom[k]*r2j - dP[k]*Lrj);}
#endif
#if (BH_GRAVACCRETION == 8)
                        u=0; for(k=0;k<3;k++) {u+=dP[k]*dP[k];}
                        u=sqrt(u)/h_i; kernel_main(u,hinv3,hinv3*hinv,&wk,&dwk,-1);
                        double rj=u*h_i*All.cf_atime; double csj=Particle_effective_soundspeed_i(j);
                        double vdotrj=0; for(k=0;k<3;k++) {vdotrj+=-dP[k]*dv[k];}
                        double vr_mdot = 4*M_PI * wt*(wk*All.cf_a3inv) * rj*vdotrj;
                        if(rj < All.ForceSoftening[5]*All.cf_atime)
                        {
                            double bondi_mdot = 4*M_PI*All.G*All.G * local.Mass*local.Mass / pow(csj*csj + (dv[0]*dv[0]+dv[1]*dv[1]+dv[2]*dv[2])*All.cf_a2inv, 1.5) * wt * (wk*All.cf_a3inv);
                            vr_mdot = DMAX(vr_mdot , bondi_mdot); out.hubber_mdot_bondi_limiter += bondi_mdot;
                        }
                        out.hubber_mdot_vr_estimator += vr_mdot; /* physical */
                        out.hubber_mdot_disk_estimator += wt*wk * sqrt(rj) / (SphP[j].Density * csj*csj); /* physical */
#endif
                    }
                    else if( P[j].Type==4 || ((P[j].Type==2||P[j].Type==3) && !(All.ComovingIntegrationOn)) ) /* stars */
                    {
                        out.Mstar_in_Kernel += wt; out.Jstar_in_Kernel[0] += wt*(dP[1]*dv[2] - dP[2]*dv[1]); out.Jstar_in_Kernel[1] += wt*(dP[2]*dv[0] - dP[0]*dv[2]); out.Jstar_in_Kernel[2] += wt*(dP[0]*dv[1] - dP[1]*dv[0]);
                    }
                    else /* dark matter */ // DAA: Jalt_in_Kernel and Malt_in_Kernel are updated in bh_normalize_temp_info_struct() to be TOTAL angular momentum and mass
                    {
                        out.Malt_in_Kernel += wt; out.Jalt_in_Kernel[0] += wt*(dP[1]*dv[2] - dP[2]*dv[1]); out.Jalt_in_Kernel[1] += wt*(dP[2]*dv[0] - dP[0]*dv[2]); out.Jalt_in_Kernel[2] += wt*(dP[0]*dv[1] - dP[1]*dv[0]);
                    }

#if defined(BH_GRAVCAPTURE_GAS) /* XM: I formally distinguish BH_GRAVCAPTURE_GAS and BH_GRAVCAPTURE_NONGAS. The former applies to gas ONLY, as an accretion model. The later can be combined with any accretion model.
                                    Currently, I only allow gas accretion to contribute to BH_Mdot (consistent with the energy radiating away). For star particles, if there is an alpha-disk, they are captured to the disk. If not, they directly go
                                    to the hole, without any contribution to BH_Mdot and feedback. This can be modified in the swallow loop for other purposes. The goal of the following part is to estimate BH_Mdot, which will be used to evaluate feedback strength.
                                    Therefore, we only need it when we enable BH_GRAVCAPTURE_GAS as gas accretion model. */
                    if( (P[j].Mass > 0) && (P[j].Type == 0))
                    {
                        double vrel=0, r2=0; for(k=0;k<3;k++) {vrel+=dv[k]*dv[k]; r2+=dP[k]*dP[k];}
                        double dr_code = sqrt(r2); vrel = sqrt(vrel) / All.cf_atime;
                        double vbound = bh_vesc(j, local.Mass, dr_code, ags_h_i);
                        if(vrel < vbound) { /* bound */
#ifdef BH_GRAVCAPTURE_FIXEDSINKRADIUS
                            double spec_mom=0; for(k=0;k<3;k++) {spec_mom += dv[k]*dP[k];} // delta_x.delta_v
                            spec_mom = (r2*vrel*vrel - spec_mom*spec_mom*All.cf_a2inv);  // specific angular momentum^2 = r^2(delta_v)^2 - (delta_v.delta_x)^2;
                            if(spec_mom < All.G * (local.Mass + P[j].Mass) * local.SinkRadius) // check Bate 1995 angular momentum criterion (in addition to bounded-ness)
#endif
                            if( bh_check_boundedness(j,vrel,vbound,dr_code,local.SinkRadius)==1 )
                            { /* apocenter within 2.8*epsilon (softening length) */
#ifdef SINGLE_STAR_SINK_DYNAMICS
                                double eps = DMAX(P[j].Hsml/2.8, DMAX(dr_code, ags_h_i/2.8));
                                double tff = eps*eps*eps / (local.Mass + P[j].Mass);
                                if(tff < P[j].SwallowTime) {P[j].SwallowTime = tff;}
#endif
#if defined(BH_ACCRETE_NEARESTFIRST)
                                if((out.BH_dr_to_NearestGasNeighbor > dr_code) && (P[j].SwallowID < local.ID)) {out.BH_dr_to_NearestGasNeighbor = dr_code; out.mass_to_swallow_edd = P[j].Mass;}
#else
                                if(P[j].SwallowID < local.ID) {out.mass_to_swallow_edd += P[j].Mass;} /* mark as 'will be swallowed' on next loop, to correct accretion rate */
#endif
                            } /* if( apocenter in tolerance range ) */
                        } /* if(vrel < vbound) */
                    } /* type check */
#endif // BH_GRAVCAPTURE_GAS
                } // ( (P[j].Mass > 0) && (P[j].Type != 5) && (P[j].ID != local.ID) ) - condition for entering primary loop
            } // numngb_inbox loop
        } // while(startnode)
        if(mode == 1) {listindex++; if(listindex < NODELISTLENGTH) {startnode = DATAGET_NAME[target].NodeList[listindex]; if(startnode >= 0) {startnode = Nodes[startnode].u.d.nextnode; /* open it */}}} /* continue to open leaves if needed */
    }
    if(mode == 0) {OUTPUTFUNCTION_NAME(&out, target, 0, loop_iteration);} else {DATARESULT_NAME[target] = out;} /* collects the result at the right place */
    return 0;
}


void blackhole_environment_loop(void)
{
    #include "../../system/code_block_xchange_perform_ops_malloc.h" /* this calls the large block of code which contains the memory allocations for the MPI/OPENMP/Pthreads parallelization block which must appear below */
    #include "../../system/code_block_xchange_perform_ops.h" /* this calls the large block of code which actually contains all the loops, MPI/OPENMP/Pthreads parallelization */
    #include "../../system/code_block_xchange_perform_ops_demalloc.h" /* this de-allocates the memory for the MPI/OPENMP/Pthreads parallelization block which must appear above */
    /* final operations on results */
    {int i; for(i=0; i<N_active_loc_BHs; i++) {bh_normalize_temp_info_struct_after_environment_loop(i);}}
}
#include "../../system/code_block_xchange_finalize.h" /* de-define the relevant variables and macros to avoid compilation errors and memory leaks */








/* -----------------------------------------------------------------------------------------------------
 * DAA: modified versions of blackhole_environment_loop and blackhole_environment_evaluate for a second
 * environment loop. Here we do a Bulge-Disk kinematic decomposition for gravitational torque accretion
 * ----------------------------------------------------------------------------------------------------- */
#if defined(BH_GRAVACCRETION) && (BH_GRAVACCRETION == 0)

#define MASTER_FUNCTION_NAME blackhole_environment_second_evaluate /* name of the 'core' function doing the actual inter-neighbor operations. this MUST be defined somewhere as "int MASTER_FUNCTION_NAME(int target, int mode, int *exportflag, int *exportnodecount, int *exportindex, int *ngblist, int loop_iteration)" */
#define CONDITIONFUNCTION_FOR_EVALUATION if(P[i].Type==5) /* function for which elements will be 'active' and allowed to undergo operations. can be a function call, e.g. 'density_is_active(i)', or a direct function call like 'if(P[i].Mass>0)' */
#include "../../system/code_block_xchange_initialize.h" /* pre-define all the ALL_CAPS variables we will use below, so their naming conventions are consistent and they compile together, as well as defining some of the function calls needed */

/* this structure defines the variables that need to be sent -from- the 'searching' element */
struct INPUT_STRUCT_NAME
{
    int NodeList[NODELISTLENGTH]; MyDouble Pos[3]; MyFloat Vel[3], Hsml, Jgas_in_Kernel[3], Jstar_in_Kernel[3];
}
*DATAIN_NAME, *DATAGET_NAME; /* dont mess with these names, they get filled-in by your definitions automatically */

/* this subroutine assigns the values to the variables that need to be sent -from- the 'searching' element */
static inline void INPUTFUNCTION_NAME(struct INPUT_STRUCT_NAME *in, int i, int loop_iteration)
{
    int k, j_tempinfo = P[i].IndexMapToTempStruc; in->Hsml = PPP[i].Hsml; /* link to the location in the shared structure where this is stored */
    for(k=0;k<3;k++) {in->Pos[k]=P[i].Pos[k]; in->Vel[k]=P[i].Vel[k];} /* good example - always needed */
    for(k=0;k<3;k++) {in->Jgas_in_Kernel[k]=BlackholeTempInfo[j_tempinfo].Jgas_in_Kernel[k]; in->Jstar_in_Kernel[k]=BlackholeTempInfo[j_tempinfo].Jstar_in_Kernel[k];}
}

/* this structure defines the variables that need to be sent -back to- the 'searching' element */
struct OUTPUT_STRUCT_NAME
{ /* define variables below as e.g. "double X;" */
    MyFloat MgasBulge_in_Kernel, MstarBulge_in_Kernel;
}
*DATARESULT_NAME, *DATAOUT_NAME; /* dont mess with these names, they get filled-in by your definitions automatically */

/* simple routine to add quantities to BlackholeTempInfo */
static inline void OUTPUTFUNCTION_NAME(struct OUTPUT_STRUCT_NAME *out, int i, int mode, int loop_iteration)
{
    int target = P[i].IndexMapToTempStruc;
    ASSIGN_ADD(BlackholeTempInfo[target].MgasBulge_in_Kernel,out->MgasBulge_in_Kernel,mode);
    ASSIGN_ADD(BlackholeTempInfo[target].MstarBulge_in_Kernel,out->MstarBulge_in_Kernel,mode);
}

/* this subroutine does the actual neighbor-element calculations (this is the 'core' of the loop, essentially) */
int blackhole_environment_second_evaluate(int target, int mode, int *exportflag, int *exportnodecount, int *exportindex, int *ngblist, int loop_iteration)
{
    int startnode, numngb_inbox, listindex = 0, j, n; struct INPUT_STRUCT_NAME local; struct OUTPUT_STRUCT_NAME out; memset(&out, 0, sizeof(struct OUTPUT_STRUCT_NAME)); /* define variables and zero memory and import data for local target*/
    if(mode == 0) {INPUTFUNCTION_NAME(&local, target, loop_iteration);} else {local = DATAGET_NAME[target];} /* imports the data to the correct place and names */
    if(mode == 0) {startnode = All.MaxPart; /* root node */} else {startnode = DATAGET_NAME[target].NodeList[0]; startnode = Nodes[startnode].u.d.nextnode;    /* open it */}
    while(startnode >= 0) {
        while(startnode >= 0) {
            numngb_inbox = ngb_treefind_pairs_threads_targeted(local.Pos, local.Hsml, target, &startnode, mode, exportflag, exportnodecount, exportindex, ngblist, BH_NEIGHBOR_BITFLAG);
            if(numngb_inbox < 0) {return -1;} /* no neighbors! */
            for(n = 0; n < numngb_inbox; n++) /* neighbor loop */
            {
                j = ngblist[n]; if((P[j].Mass <= 0)||(P[j].Hsml <= 0)||(P[j].Type==5)) {continue;} /* make sure neighbor is valid */
                int k; double dP[3], dv[3]; for(k=0;k<3;k++) {dP[k]=P[j].Pos[k]-local.Pos[k]; dv[k]=P[j].Vel[k]-local.Vel[k];} /* position offset */
                NEAREST_XYZ(dP[0],dP[1],dP[2],-1);
#ifdef BOX_SHEARING
                if(local.Pos[0] - P[j].Pos[0] > +boxHalf_X) {dv[BOX_SHEARING_PHI_COORDINATE] -= Shearing_Box_Vel_Offset;}
                if(local.Pos[0] - P[j].Pos[0] < -boxHalf_X) {dv[BOX_SHEARING_PHI_COORDINATE] += Shearing_Box_Vel_Offset;}
#endif
                double J_tmp[3]; J_tmp[0]=dP[1]*dv[2]-dP[2]*dv[1]; J_tmp[1]=dP[2]*dv[0]-dP[0]*dv[2]; J_tmp[2]=dP[0]*dv[1]-dP[1]*dv[0]; /* just need direction not magnitude */
                if(P[j].Type==0) {if(J_tmp[0]*local.Jgas_in_Kernel[0] + J_tmp[1]*local.Jgas_in_Kernel[1] + J_tmp[2]*local.Jgas_in_Kernel[2] < 0) {out.MgasBulge_in_Kernel += 2*P[j].Mass;}} /* DAA: assume the bulge component contains as many particles with positive azimuthal velocities as with negative azimuthal velocities relative to the angular momentum vector */
                if(P[j].Type==4 || ((P[j].Type==2||P[j].Type==3) && !(All.ComovingIntegrationOn))) {if(J_tmp[0]*local.Jstar_in_Kernel[0] + J_tmp[1]*local.Jstar_in_Kernel[1] + J_tmp[2]*local.Jstar_in_Kernel[2] < 0) {out.MstarBulge_in_Kernel += 2*P[j].Mass;}}
            } // numngb_inbox loop
        } // while(startnode)
        if(mode == 1) {listindex++; if(listindex < NODELISTLENGTH) {startnode = DATAGET_NAME[target].NodeList[listindex]; if(startnode >= 0) {startnode = Nodes[startnode].u.d.nextnode; /* open it */}}} /* continue to open leaves if needed */
    }
    if(mode == 0) {OUTPUTFUNCTION_NAME(&out, target, 0, loop_iteration);} else {DATARESULT_NAME[target] = out;} /* collects the result at the right place */
    return 0;
}

void blackhole_environment_second_loop(void)
{
#include "../../system/code_block_xchange_perform_ops_malloc.h" /* this calls the large block of code which contains the memory allocations for the MPI/OPENMP/Pthreads parallelization block which must appear below */
#include "../../system/code_block_xchange_perform_ops.h" /* this calls the large block of code which actually contains all the loops, MPI/OPENMP/Pthreads parallelization */
#include "../../system/code_block_xchange_perform_ops_demalloc.h" /* this de-allocates the memory for the MPI/OPENMP/Pthreads parallelization block which must appear above */
}
#include "../../system/code_block_xchange_finalize.h" /* de-define the relevant variables and macros to avoid compilation errors and memory leaks */

#endif   //BH_GRAVACCRETION == 0
