/*! \file blackhole_feed.c
 *  \brief This is where particles are marked for gas accretion.
 */
/*
 * This file is largely written by Phil Hopkins (phopkins@caltech.edu) for GIZMO.
 *   It was based on a similar file in GADGET3 by Volker Springel (volker.springel@h-its.org),
 *   but the physical modules for black hole accretion and feedback have been
 *   replaced, and the algorithm for their coupling is new to GIZMO.  This file was modified
 *   on 1/9/15 by Paul Torrey (ptorrey@mit.edu) for clarity by parsing the existing code into
 *   smaller files and routines. Some communication and black hole structures were modified
 *   to reduce memory usage. Cleanup, de-bugging, and consolidation of routines by Xiangcheng Ma
 *   (xchma@caltech.edu) followed on 05/15/15; re-integrated by PFH.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../allvars.h"
#include "../../proto.h"
#include "../../kernel.h"
#include "blackhole_local.h"


void blackhole_feed_loop(void)
{
    int i, j, k, ndone_flag, ndone, ngrp, recvTask, place, nexport, nimport, dummy; MPI_Status status; MyFloat dt;
#ifdef NEWSINK
    double dm=0; int mdotchanged = 0;
#endif
    /* allocate buffers to arrange communication */
    size_t MyBufferSize = All.BufferSize;
    Ngblist = (int *) mymalloc("Ngblist", NumPart * sizeof(int));
    All.BunchSize = (int) ((MyBufferSize * 1024 * 1024) / (sizeof(struct data_index) + sizeof(struct data_nodelist) +
                                                             sizeof(struct blackholedata_in) + sizeof(struct blackholedata_out) +
                                                             sizemax(sizeof(struct blackholedata_in),sizeof(struct blackholedata_out))));
    DataIndexTable = (struct data_index *) mymalloc("DataIndexTable", All.BunchSize * sizeof(struct data_index));
    DataNodeList = (struct data_nodelist *) mymalloc("DataNodeList", All.BunchSize * sizeof(struct data_nodelist));
    
    /* Let's determine which particles may be swallowed by whom, and the weights for feedback */
    i = FirstActiveParticle;
    do
    {
        for(j = 0; j < NTask; j++)
        {
            Send_count[j] = 0;
            Exportflag[j] = -1;
        }
        
        /* do local particles and prepare export list */
        for(nexport = 0; i >= 0; i = NextActiveParticle[i])                      // DAA: can this be replaced by a loop over N_active_loc_BHs
            if(P[i].Type == 5)
                if(blackhole_feed_evaluate(i, 0, &nexport, Send_count) < 0)
                    break;
        
        MYSORT_DATAINDEX(DataIndexTable, nexport, sizeof(struct data_index), data_index_compare);
        MPI_Alltoall(Send_count, 1, MPI_INT, Recv_count, 1, MPI_INT, MPI_COMM_WORLD);
        for(j = 0, nimport = 0, Recv_offset[0] = 0, Send_offset[0] = 0; j < NTask; j++)
        {
            nimport += Recv_count[j];
            if(j > 0)
            {
                Send_offset[j] = Send_offset[j - 1] + Send_count[j - 1];
                Recv_offset[j] = Recv_offset[j - 1] + Recv_count[j - 1];
            }
        }
        BlackholeDataGet = (struct blackholedata_in *) mymalloc("BlackholeDataGet", nimport * sizeof(struct blackholedata_in));
        BlackholeDataIn = (struct blackholedata_in *) mymalloc("BlackholeDataIn", nexport * sizeof(struct blackholedata_in));
        
        for(j = 0; j < nexport; j++)
        {
            place = DataIndexTable[j].Index;
            for(k = 0; k < 3; k++)
            {
                BlackholeDataIn[j].Pos[k] = P[place].Pos[k];
                BlackholeDataIn[j].Vel[k] = P[place].Vel[k];
#if defined(SINKLEFINKLE_J_FEEDBACK)
                BlackholeDataIn[j].BH_Specific_AngMom[k] = BPP(place).BH_Specific_AngMom[k];
#endif
#if defined(BH_PHOTONMOMENTUM) || defined(BH_WIND_CONTINUOUS)
                BlackholeDataIn[j].Jgas_in_Kernel[k] = BlackholeTempInfo[P[place].IndexMapToTempStruc].Jgas_in_Kernel[k];
#endif
            }
#if defined(BH_GRAVCAPTURE_GAS)
            BlackholeDataIn[j].mass_to_swallow_edd = BlackholeTempInfo[P[place].IndexMapToTempStruc].mass_to_swallow_edd;
#ifdef SINGLE_STAR_STRICT_ACCRETION
            BlackholeDataIn[j].SinkRadius = PPP[place].SinkRadius;
#endif	    
#endif
            BlackholeDataIn[j].Hsml = PPP[place].Hsml;
#ifdef ADAPTIVE_GRAVSOFT_FORALL
            BlackholeDataIn[j].AGS_Hsml = PPP[place].AGS_Hsml;	    
#endif	    
            BlackholeDataIn[j].Mass = P[place].Mass;
            BlackholeDataIn[j].BH_Mass = BPP(place).BH_Mass;
#ifdef BH_ALPHADISK_ACCRETION
            BlackholeDataIn[j].BH_Mass_AlphaDisk = BPP(place).BH_Mass_AlphaDisk;
#endif
#ifdef NEWSINK //Copy info on neighbours
            BlackholeDataIn[j].n_neighbor = BlackholeTempInfo[P[place].IndexMapToTempStruc].n_neighbor;
            memcpy(BlackholeDataIn[j].rgas,BlackholeTempInfo[P[place].IndexMapToTempStruc].rgas, SINKLEFINKLE_NEIGHBORMAX * sizeof(MyFloat));
            memcpy(BlackholeDataIn[j].xgas,BlackholeTempInfo[P[place].IndexMapToTempStruc].xgas, SINKLEFINKLE_NEIGHBORMAX * sizeof(MyFloat));
            memcpy(BlackholeDataIn[j].ygas,BlackholeTempInfo[P[place].IndexMapToTempStruc].ygas, SINKLEFINKLE_NEIGHBORMAX * sizeof(MyFloat));
            memcpy(BlackholeDataIn[j].zgas,BlackholeTempInfo[P[place].IndexMapToTempStruc].zgas, SINKLEFINKLE_NEIGHBORMAX * sizeof(MyFloat));
            memcpy(BlackholeDataIn[j].Hsmlgas,BlackholeTempInfo[P[place].IndexMapToTempStruc].Hsmlgas, SINKLEFINKLE_NEIGHBORMAX * sizeof(MyFloat));
            memcpy(BlackholeDataIn[j].mgas,BlackholeTempInfo[P[place].IndexMapToTempStruc].mgas, SINKLEFINKLE_NEIGHBORMAX * sizeof(MyFloat));
            memcpy(BlackholeDataIn[j].gasID,BlackholeTempInfo[P[place].IndexMapToTempStruc].gasID, SINKLEFINKLE_NEIGHBORMAX * sizeof(MyFloat));
            memcpy(BlackholeDataIn[j].isbound,BlackholeTempInfo[P[place].IndexMapToTempStruc].isbound, SINKLEFINKLE_NEIGHBORMAX * sizeof(int));
            memcpy(BlackholeDataIn[j].f_acc,BlackholeTempInfo[P[place].IndexMapToTempStruc].f_acc, SINKLEFINKLE_NEIGHBORMAX * sizeof(MyFloat));
#if defined(SINKLEFINKLE_J_FEEDBACK)
            memcpy(BlackholeDataIn[j].dv_ang_kick_norm,BlackholeTempInfo[P[place].IndexMapToTempStruc].dv_ang_kick_norm, SINKLEFINKLE_NEIGHBORMAX * sizeof(MyFloat));
            BlackholeDataIn[j].t_disc = BlackholeTempInfo[P[place].IndexMapToTempStruc].t_disc;
#endif
#ifdef BH_ALPHADISK_ACCRETION
            BlackholeDataIn[j].Mdot_AlphaDisk = BPP(place).BH_Mdot_AlphaDisk;
#endif
#endif
#if defined(BH_PHOTONMOMENTUM) 	|| defined(BH_WIND_CONTINUOUS)
            BlackholeDataIn[j].BH_disk_hr = P[place].BH_disk_hr;
#endif
            BlackholeDataIn[j].Density = BPP(place).DensAroundStar;
            BlackholeDataIn[j].Mdot = BPP(place).BH_Mdot;
#ifndef WAKEUP
            BlackholeDataIn[j].Dt = (P[place].TimeBin ? (((integertime) 1) << P[place].TimeBin) : 0) * All.Timebase_interval / All.cf_hubble_a;
#else
            BlackholeDataIn[j].Dt = P[place].dt_step * All.Timebase_interval / All.cf_hubble_a;
#endif
            BlackholeDataIn[j].ID = P[place].ID;
            memcpy(BlackholeDataIn[j].NodeList,DataNodeList[DataIndexTable[j].IndexGet].NodeList, NODELISTLENGTH * sizeof(int));
        }
        
        /* exchange particle data */
        for(ngrp = 1; ngrp < (1 << PTask); ngrp++)
        {
            recvTask = ThisTask ^ ngrp;
            if(recvTask < NTask)
            {
                if(Send_count[recvTask] > 0 || Recv_count[recvTask] > 0)
                {
                    /* get the particles */
                    MPI_Sendrecv(&BlackholeDataIn[Send_offset[recvTask]],
                                 Send_count[recvTask] * sizeof(struct blackholedata_in), MPI_BYTE, recvTask, TAG_BH_E,
                                 &BlackholeDataGet[Recv_offset[recvTask]],
                                 Recv_count[recvTask] * sizeof(struct blackholedata_in), MPI_BYTE,
                                 recvTask, TAG_BH_E, MPI_COMM_WORLD, &status);
                }
            }
        }
        myfree(BlackholeDataIn);
        BlackholeDataResult = (struct blackholedata_out *) mymalloc("BlackholeDataResult",nimport * sizeof(struct blackholedata_out));
        BlackholeDataOut = (struct blackholedata_out *) mymalloc("BlackholeDataOut", nexport * sizeof(struct blackholedata_out));
        
        /* now do the particles that were sent to us */
        for(j = 0; j < nimport; j++) {blackhole_feed_evaluate(j, 1, &dummy, &dummy);}
        
        if(i < 0) {ndone_flag = 1;} else {ndone_flag = 0;}
        MPI_Allreduce(&ndone_flag, &ndone, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
        for(ngrp = 1; ngrp < (1 << PTask); ngrp++) /* get the result */
        {
            recvTask = ThisTask ^ ngrp;
            if(recvTask < NTask)
            {
                if(Send_count[recvTask] > 0 || Recv_count[recvTask] > 0)
                {
                    /* send the results */
                    MPI_Sendrecv(&BlackholeDataResult[Recv_offset[recvTask]],
                                 Recv_count[recvTask] * sizeof(struct blackholedata_out),
                                 MPI_BYTE, recvTask, TAG_BH_F,
                                 &BlackholeDataOut[Send_offset[recvTask]],
                                 Send_count[recvTask] * sizeof(struct blackholedata_out),
                                 MPI_BYTE, recvTask, TAG_BH_F, MPI_COMM_WORLD, &status);
                }
            }
        } // for(ngrp = 1; ngrp < (1 << PTask); ngrp++) //
        
        /* add the result to the particles */
        for(j = 0; j < nexport; j++)
        {
            place = DataIndexTable[j].Index;
#ifdef BH_REPOSITION_ON_POTMIN
            if(BPP(place).BH_MinPot > BlackholeDataOut[j].BH_MinPot) {BPP(place).BH_MinPot = BlackholeDataOut[j].BH_MinPot; for(k = 0; k < 3; k++) {BPP(place).BH_MinPotPos[k] = BlackholeDataOut[j].BH_MinPotPos[k];}}
#endif
#if defined(BH_PHOTONMOMENTUM) || defined(BH_WIND_CONTINUOUS)
            BlackholeTempInfo[P[place].IndexMapToTempStruc].BH_angle_weighted_kernel_sum += BlackholeDataOut[j].BH_angle_weighted_kernel_sum;
#endif
#if defined NEWSINK
            dm=0; mdotchanged=0; //update accretion factors in case we decided not to accrete a particle (e.g. if it is bound to two sinks and only one gets it)
            for(k = 0; k < BlackholeTempInfo[P[place].IndexMapToTempStruc].n_neighbor; k++){
                if(BlackholeTempInfo[P[place].IndexMapToTempStruc].f_acc[k] != BlackholeDataOut[j].f_acc[k]) {BlackholeTempInfo[P[place].IndexMapToTempStruc].f_acc[k]=BlackholeDataOut[j].f_acc[k]; mdotchanged=1;}
                dm += BlackholeTempInfo[P[place].IndexMapToTempStruc].mgas[k] * BlackholeTempInfo[P[place].IndexMapToTempStruc].f_acc[k]; //get the total mass to be accreted
            }
            if (mdotchanged){
#ifdef BH_ALPHADISK_ACCRETION
            BPP(place).BH_Mdot_AlphaDisk = dm/dt; //update mdot for disk
#else
            BPP(place).BH_Mdot = dm/dt; //update mdot
#endif
            }
#endif
        }
        myfree(BlackholeDataOut);
        myfree(BlackholeDataResult);
        myfree(BlackholeDataGet);
    }
    while(ndone < NTask);
    myfree(DataNodeList);
    myfree(DataIndexTable);
    myfree(Ngblist);
}







/* do loop over neighbors to get quantities for accretion */
int blackhole_feed_evaluate(int target, int mode, int *nexport, int *nSend_local)
{
    int startnode, numngb, j, k, n, listindex = 0;
    MyIDType id;
    MyFloat *pos, *velocity, h_i, dt, mdot, rho, mass, bh_mass;
    MyFloat ags_h_i = All.ForceSoftening[5];
    double h_i2, r2, r, u, hinv, hinv3, wk, dwk, vrel, vesc, dpos[3], dvel[3], sink_radius; sink_radius=0;
#if defined(BH_GRAVCAPTURE_GAS) && defined(BH_ENFORCE_EDDINGTON_LIMIT) && !defined(BH_ALPHADISK_ACCRETION)
    double meddington, medd_max_accretable, mass_to_swallow_edd, eddington_factor;
#endif
#if defined(BH_PHOTONMOMENTUM) || defined(BH_WIND_CONTINUOUS)
    double norm, theta, BH_disk_hr, *Jgas_in_Kernel;
    double BH_angle_weighted_kernel_sum=0;
#endif
#if defined(BH_WIND_KICK) && !defined(BH_GRAVCAPTURE_GAS)
    double f_accreted=0; 
#endif
#ifdef BH_THERMALFEEDBACK
    double energy;
#endif
#ifdef BH_REPOSITION_ON_POTMIN
    MyFloat minpotpos[3] = { 0, 0, 0 }, minpot = BHPOTVALUEINIT;
#endif
#ifdef BH_ALPHADISK_ACCRETION
    MyFloat bh_mass_alphadisk;
#endif
#if defined(BH_SWALLOWGAS)
    double w,p,mass_markedswallow,bh_mass_withdisk;
    w=0; p=0; mass_markedswallow=0; bh_mass_withdisk=0;
#endif
#ifdef NEWSINK
    int n_neighbor, *str_isbound; MyIDType *str_gasID;
    MyFloat target_accreted_mass,accr_mass=0, *str_mgas, *str_f_acc;
#endif
    
    /* these are the BH properties */
    if(mode == 0)
    {
        pos = P[target].Pos;
        rho = P[target].DensAroundStar;       // DAA: DensAroundStar is not defined in BHP->BPP...
        mdot = BPP(target).BH_Mdot;
#ifndef WAKEUP
        dt = (P[target].TimeBin ? (((integertime) 1) << P[target].TimeBin) : 0) * All.Timebase_interval / All.cf_hubble_a;
#else
        dt = P[target].dt_step * All.Timebase_interval / All.cf_hubble_a;
#endif
        h_i = PPP[target].Hsml;
#ifdef ADAPTIVE_GRAVSOFT_FORALL
	    ags_h_i = PPP[target].AGS_Hsml;
#endif	
        mass = P[target].Mass;
        bh_mass = BPP(target).BH_Mass;
#ifdef BH_ALPHADISK_ACCRETION
        bh_mass_alphadisk = BPP(target).BH_Mass_AlphaDisk;
#endif
        velocity = P[target].Vel;
        id = P[target].ID;
#if defined(BH_PHOTONMOMENTUM) || defined(BH_WIND_CONTINUOUS)
        Jgas_in_Kernel = BlackholeTempInfo[P[target].IndexMapToTempStruc].Jgas_in_Kernel;
        BH_disk_hr = P[target].BH_disk_hr;
#endif
#if defined(BH_GRAVCAPTURE_GAS) && defined(BH_ENFORCE_EDDINGTON_LIMIT) && !defined(BH_ALPHADISK_ACCRETION)
        mass_to_swallow_edd = BlackholeTempInfo[P[target].IndexMapToTempStruc].mass_to_swallow_edd;
#endif
#ifdef SINGLE_STAR_STRICT_ACCRETION
        sink_radius = P[target].SinkRadius;
#endif
#ifdef NEWSINK
        n_neighbor = BlackholeTempInfo[P[target].IndexMapToTempStruc].n_neighbor;
        str_mgas = BlackholeTempInfo[P[target].IndexMapToTempStruc].mgas;
        str_f_acc = BlackholeTempInfo[P[target].IndexMapToTempStruc].f_acc;
        str_gasID = BlackholeTempInfo[P[target].IndexMapToTempStruc].gasID;
        str_isbound = BlackholeTempInfo[P[target].IndexMapToTempStruc].isbound;
#ifdef BH_ALPHADISK_ACCRETION
        mdot = BPP(target).BH_Mdot_AlphaDisk; //overwrite value with alpha disk mdot
#endif
#endif
    }
    else
    {
        pos = BlackholeDataGet[target].Pos;
        rho = BlackholeDataGet[target].Density;
        mdot = BlackholeDataGet[target].Mdot;
        dt = BlackholeDataGet[target].Dt;
        h_i = BlackholeDataGet[target].Hsml;
#ifdef ADAPTIVE_GRAVSOFT_FORALL
	    ags_h_i = BlackholeDataGet[target].AGS_Hsml;
#endif	
        mass = BlackholeDataGet[target].Mass;
#ifdef SINGLE_STAR_STRICT_ACCRETION
        sink_radius = BlackholeDataGet[target].SinkRadius;
#endif
        bh_mass = BlackholeDataGet[target].BH_Mass;
#ifdef BH_ALPHADISK_ACCRETION
        bh_mass_alphadisk = BlackholeDataGet[target].BH_Mass_AlphaDisk;
#endif
        velocity = BlackholeDataGet[target].Vel;
        id = BlackholeDataGet[target].ID;
#if defined(BH_PHOTONMOMENTUM)  || defined(BH_WIND_CONTINUOUS)
        Jgas_in_Kernel = BlackholeDataGet[target].Jgas_in_Kernel;
        BH_disk_hr = BlackholeDataGet[target].BH_disk_hr;
#endif
#if defined(BH_GRAVCAPTURE_GAS) && defined(BH_ENFORCE_EDDINGTON_LIMIT) && !defined(BH_ALPHADISK_ACCRETION)
        mass_to_swallow_edd = BlackholeDataGet[target].mass_to_swallow_edd;
#endif
#ifdef NEWSINK
        n_neighbor = BlackholeDataGet[target].n_neighbor;
        str_mgas = BlackholeDataGet[target].mgas;
        str_f_acc = BlackholeDataGet[target].f_acc;
        str_gasID = BlackholeDataGet[target].gasID;
        str_isbound = BlackholeDataGet[target].isbound;
        /*copy part of it to output structure*/
        memcpy(BlackholeDataResult[target].f_acc, BlackholeDataGet[target].f_acc, SINKLEFINKLE_NEIGHBORMAX * sizeof(MyFloat));
        BlackholeDataResult[target].Mdot = BlackholeDataGet[target].Mdot;
        BlackholeDataResult[target].Dt = BlackholeDataGet[target].Dt;
#ifdef BH_ALPHADISK_ACCRETION
        mdot = BlackholeDataGet[target].Mdot_AlphaDisk; //overwrite value with alpha disk mdot
        BlackholeDataResult[target].Mdot = BlackholeDataGet[target].Mdot_AlphaDisk;
#endif
#endif
    }
    if((mass<0)||(h_i<=0)) return -1;
    
    /* initialize variables before SPH loop is started */
    h_i2 = h_i * h_i;
    hinv = 1 / h_i;
    hinv3 = hinv * hinv * hinv;
#if defined(BH_GRAVCAPTURE_GAS) && defined(BH_ENFORCE_EDDINGTON_LIMIT) && !defined(BH_ALPHADISK_ACCRETION)
    meddington = bh_eddington_mdot(bh_mass);
    medd_max_accretable = All.BlackHoleEddingtonFactor * meddington * dt;
    eddington_factor = mass_to_swallow_edd / medd_max_accretable;   /* if <1 no problem, if >1, need to not set some swallowIDs */
#endif
#if defined(BH_SWALLOWGAS)
    bh_mass_withdisk = bh_mass;
#ifdef BH_ALPHADISK_ACCRETION
    bh_mass_withdisk += bh_mass_alphadisk;
#endif
#endif
#if defined(BH_SWALLOW_SMALLTIMESTEPS)
    double dt_min_to_accrete = DMAX(0.001 * sqrt(pow(All.SofteningTable[5],3.0)/(All.G * mass)), 20.0*DMAX(All.MinSizeTimestep,All.Timebase_interval) ); //0.001 = tolerance factor for dt_min, defined in part (ii) of 2.3.5 in Hubber 2013.
#endif
#if defined(BH_WIND_KICK) && !defined(BH_GRAVCAPTURE_GAS)
    /* DAA: increase the effective mass-loading of BAL winds to reach the desired momentum flux given the outflow velocity "All.BAL_v_outflow" chosen
       --> appropriate for cosmological simulations where particles are effectively kicked from ~kpc scales
           (i.e. we need lower velocity and higher mass outflow rates compared to accretion disk scales) - */
    f_accreted = All.BAL_f_accretion;
    if((All.BlackHoleFeedbackFactor > 0) && (All.BlackHoleFeedbackFactor != 1.)) {f_accreted /= All.BlackHoleFeedbackFactor;} else {if(All.BAL_v_outflow > 0) f_accreted = 1./(1. + fabs(1.*BH_WIND_KICK)*All.BlackHoleRadiativeEfficiency*(C/All.UnitVelocity_in_cm_per_s)/(All.BAL_v_outflow));}
#endif
    
    /* Now start the actual SPH computation for this BH particle */
    if(mode == 0)
    {
        startnode = All.MaxPart;	/* root node */
    }
    else
    {
        startnode = BlackholeDataGet[target].NodeList[0];
        startnode = Nodes[startnode].u.d.nextnode;	/* open it */
    }
    //int particles_swallowed_this_bh_this_process = 0;
    //int particles_swallowed_this_bh_this_process_max = 1;
#if defined(BH_PHOTONMOMENTUM) || defined(BH_WIND_CONTINUOUS)
    BH_angle_weighted_kernel_sum = 0;
#endif
    
#if defined(NEWSINK) /* Decide for NEWSINK which particles to eat */
    target_accreted_mass = mdot *dt;
    for(k=0;k<n_neighbor;k++){
        if (accr_mass<target_accreted_mass){ //do we still need more gas
            if (str_isbound[k]==1){ //only accrete bound gas
                if ( (accr_mass+str_mgas[k]) <= target_accreted_mass){
                    str_f_acc[k] = 1.0; //safe to take the whole thing
                    accr_mass += str_mgas[k];
                }
                else{
                    str_f_acc[k] = (target_accreted_mass-accr_mass)/str_mgas[k]; //take just what is needed
                    accr_mass = target_accreted_mass;
                }
            }
        }
        else{break;} //no need to continue if we already got enough mass
    }
#endif

    while(startnode >= 0)
    {
        while(startnode >= 0)
        {
            numngb = ngb_treefind_pairs_targeted(pos, h_i, target, &startnode, mode, nexport, nSend_local, BH_NEIGHBOR_BITFLAG); // BH_NEIGHBOR_BITFLAG defines which types of particles we search for
            if(numngb < 0) return -1;
            
            for(n = 0; n < numngb; n++)
            {
                j = Ngblist[n];
                if(P[j].Mass > 0)
                {
                    for(k=0;k<3;k++) {dpos[k] = P[j].Pos[k] - pos[k];}
#ifdef BOX_PERIODIC
                    NEAREST_XYZ(dpos[0],dpos[1],dpos[2],-1);
#endif
                    dvel[0] = P[j].Vel[0]-velocity[0]; dvel[1] = P[j].Vel[1]-velocity[1]; dvel[2] = P[j].Vel[2]-velocity[2];
#ifdef BOX_SHEARING
                    if(pos[0] - P[j].Pos[0] > +boxHalf_X) {dvel[BOX_SHEARING_PHI_COORDINATE] -= Shearing_Box_Vel_Offset;}
                    if(pos[0] - P[j].Pos[0] < -boxHalf_X) {dvel[BOX_SHEARING_PHI_COORDINATE] += Shearing_Box_Vel_Offset;}
#endif
                    r2=0; for(k=0;k<3;k++) {r2 += dpos[k]*dpos[k];}
                    if(r2 < h_i2 || r2 < PPP[j].Hsml*PPP[j].Hsml)
                    {
                        vrel=0; for(k=0;k<3;k++) {vrel += dvel[k]*dvel[k];}
                        r=sqrt(r2); vrel=sqrt(vrel)/All.cf_atime;  /* do this once and use below */
                        vesc=bh_vesc(j,mass,r, ags_h_i);
#if defined(BH_SWALLOW_SMALLTIMESTEPS)
                        if(vrel<vesc) {if(P[j].dt_step*All.Timebase_interval/All.cf_hubble_a<dt_min_to_accrete) {if(P[j].SwallowID<id) {P[j].SwallowID = id;}}} /* Bound particles with very small timestep get eaten to avoid issues. */
#endif

#ifdef BH_REPOSITION_ON_POTMIN
                        /* check if we've found a new potential minimum which is not moving too fast to 'jump' to */
                        double boundedness_function, potential_function; boundedness_function = P[j].Potential + 0.5 * vrel*vrel * All.cf_atime; potential_function = P[j].Potential;
#if (BH_REPOSITION_ON_POTMIN == 2)
                        if( boundedness_function < 0 )
                        {
                            double wt_rsoft = r / (3.*All.ForceSoftening[5]); // normalization arbitrary here, just using for convenience for function below
                            boundedness_function *= 1./(1. + wt_rsoft*wt_rsoft); // this down-weights particles which are very far away, relative to the user-defined force softening scale, which should define some 'confidence radius' of resolution around the BH particle
                        }
                        potential_function = boundedness_function; // jumps based on -most bound- particle, not just deepest potential (down-weights fast-movers)
#endif
                        if(potential_function < minpot)
#if (BH_REPOSITION_ON_POTMIN == 1)
                        if( P[j].Type == 4 && vrel <= vesc )   // DAA: only if it is a star particle & bound
#endif
#if (BH_REPOSITION_ON_POTMIN == 2)
                        if( (P[j].Type != 0) && (P[j].Type != 5) )   // allow stars or dark matter but exclude gas, it's too messy! also exclude BHs, since we don't want to over-merge them
#endif
                        {
                            minpot=potential_function; for(k=0;k<3;k++) {minpotpos[k] = P[j].Pos[k];}
                        }
#endif
			
#if (!defined(SINGLE_STAR_SINK_DYNAMICS) || defined(SINGLE_STAR_MERGERS)) 
                        /* check_for_bh_merger.  Easy.  No Edd limit, just a pos and vel criteria. */
#ifdef SINGLE_STAR_MERGERS
			            if((P[j].Mass < 3*All.MinMassForParticleMerger) && (r < All.ForceSoftening[5])) // only merge away stuff that is within the softening radius, and is no more massive that a few gas particles
#endif			
                        if((id != P[j].ID) && (P[j].Mass > 0) && (P[j].Type == 5))	/* we may have a black hole merger */
                        {
                            if(id != P[j].ID) /* check its not the same bh  (DAA: this is duplicated here...) */
                            {
                                if((vrel < BH_CSND_FRAC_BH_MERGE * vesc) && (bh_check_boundedness(j,vrel,vesc,r,sink_radius)==1))
                                {
#ifndef IO_REDUCED_MODE
                                    printf("MARKING_BH_MERGER: P[j.]ID=%llu to be swallowed by id=%llu \n", (unsigned long long) P[j].ID, (unsigned long long) id);
#endif
                                    if((P[j].SwallowID == 0) && (BPP(j).BH_Mass < bh_mass)) {P[j].SwallowID = id;} // most massive BH swallows the other - simplifies analysis
                                }
                                else
                                {
#ifndef IO_REDUCED_MODE
#ifdef BH_OUTPUT_MOREINFO           // DAA: BH merger info will be saved in a separate output file
                                    printf("ThisTask=%d, time=%g: id=%u would like to swallow %u, but vrel=%g vesc=%g\n", ThisTask, All.Time, id, P[j].ID, vrel, vesc);
#else
                                    fprintf(FdBlackHolesDetails, "ThisTask=%d, time=%g: id=%u would like to swallow %u, but vrel=%g vesc=%g\n", ThisTask, All.Time, id, P[j].ID, vrel, vesc);
#endif
#endif
                                }
                            }
                        } // if(P[j].Type == 5) //
#endif                        
                        
                        
                        /* This is a similar loop to what we already did in blackhole_environment, but here we stochastically
                         reduce GRAVCAPT events in order to (statistically) obey the eddington limit */
#if ( defined(BH_GRAVCAPTURE_GAS) && !defined(NEWSINK) ) || defined(BH_GRAVCAPTURE_NONGAS)
                        if(P[j].Type != 5)
                        {
#ifdef SINGLE_STAR_SINK_DYNAMICS
			                double eps = DMAX(P[j].Hsml/2.8, DMAX(ags_h_i/2.8, r));			    
			                if(eps*eps*eps /(P[j].Mass + mass) <= P[j].SwallowTime)
#endif			      			  
#ifdef SINGLE_STAR_STRICT_ACCRETION
			                if(r < sink_radius) // don't even bother if not in the accretion radius			      
#endif			    
                            if((vrel < vesc)) // && (particles_swallowed_this_bh_this_process < particles_swallowed_this_bh_this_process_max))
                            { /* bound */
#ifdef SINGLE_STAR_STRICT_ACCRETION
                                double spec_mom=0; for(k=0;k<3;k++) {spec_mom += dvel[k]*dpos[k];} // delta_x.delta_v
                                spec_mom = (r2*vrel*vrel - spec_mom*spec_mom*All.cf_a2inv); // specific angular momentum^2 = r^2(delta_v)^2 - (delta_v.delta_x)^2;
				                if(spec_mom < All.G * (mass + P[j].Mass) * sink_radius)  // check Bate 1995 angular momentum criterion (in addition to bounded-ness)
#endif
                                if( bh_check_boundedness(j,vrel,vesc,r,sink_radius)==1 ) { /* apocenter within target distance */
#ifdef BH_GRAVCAPTURE_NONGAS        /* simply swallow non-gas particle if BH_GRAVCAPTURE_NONGAS enabled */
                                    if((P[j].Type != 0) && (P[j].SwallowID < id)) P[j].SwallowID = id;
#endif
#if defined(BH_GRAVCAPTURE_GAS) && !defined(NEWSINK)
                                    /* now deal with gas */
                                    if (P[j].Type == 0){
#if defined(BH_ENFORCE_EDDINGTON_LIMIT) && !defined(BH_ALPHADISK_ACCRETION) /* if Eddington-limited and NO alpha-disk, do this stochastically */
                                        p = 1. / eddington_factor;
#if defined(BH_WIND_CONTINUOUS) || defined(BH_WIND_KICK)
                                        p /= All.BAL_f_accretion; // we need to accrete more, then remove the mass in winds
#endif
                                        w = get_random_number(P[j].ID);
                                        if(w < p)
                                        {
#ifndef IO_REDUCED_MODE
                                            printf("MARKING_BH_FOOD: P[j.]ID=%llu to be swallowed by id=%llu \n", (unsigned long long) P[j].ID, (unsigned long long) id);
#endif
                                            if(P[j].SwallowID < id) P[j].SwallowID = id;
                                        }
#else //if defined(BH_ENFORCE_EDDINGTON_LIMIT) && !defined(BH_ALPHADISK_ACCRETION)
                                        if(P[j].SwallowID < id) {P[j].SwallowID = id;} /* in other cases, just swallow the particle */  //particles_swallowed_this_bh_this_process++;
#endif //else defined(BH_ENFORCE_EDDINGTON_LIMIT) && !defined(BH_ALPHADISK_ACCRETION)
                                    } //if (P[j].Type == 0)
#endif //ifdef BH_GRAVCAPTURE_GAS !defined(NEWSINK)
                                } // if( apocenter in tolerance range )
                            } // if(vrel < vesc)
                        } //if(P[j].Type != 5)
#endif // ( defined(BH_GRAVCAPTURE_GAS) && !defined(NEWSINK) ) || defined(BH_GRAVCAPTURE_NONGAS)
                        
                        


/*DG: Loop for NEWSINK accretion algorithm from Hubber 2013. Starting from closest particle we start swallowing until we reach mdot*dt.*/
/* We should already have the list of particles to swallow so we just use that to mark them*/
#if defined(NEWSINK)
                       if(P[j].Type == 0)
                       {
                        /*Check whether it is on the list and we are supposed to eat it*/
                            for(k=0;k<n_neighbor;k++){
                               if( P[j].ID == str_gasID[k] && str_f_acc[k]>0 && P[j].SwallowID < id )
                               {
                                   /*Check if this is the sink the gas is most bound to, if not, don't accrete */
                                   double eps = DMAX(P[j].Hsml/2.8, DMAX(ags_h_i/2.8, r));
                                   double tff = eps*eps*eps /(mass + P[j].Mass);
                                   if(tff <= P[j].SwallowTime*1.01){
#ifndef IO_REDUCED_MODE
                                    printf("MARKING_BH_FOOD: P[j.]ID=%llu to be swallowed by id=%llu \n", (unsigned long long) P[j].ID, (unsigned long long) id);
#endif
                                    P[j].SwallowID = id; /* marked for eating */
                                   }
                                   else{
                                       mdot = DMAX((mdot-str_f_acc[k]*P[j].Mass/dt),0); //update mdot
                                       if(mode == 0){
#ifdef BH_ALPHADISK_ACCRETION
                                        BPP(target).BH_Mdot_AlphaDisk = mdot;
#else
                                        BPP(target).BH_Mdot = mdot;
#endif
                                       }
                                       else{
#ifdef BH_ALPHADISK_ACCRETION
                                        BlackholeDataResult[target].Mdot_AlphaDisk = mdot;
#else
                                        BlackholeDataResult[target].Mdot = mdot;
#endif
                                        BlackholeDataResult[target].f_acc[k] = 0;
                                       }
#ifndef IO_REDUCED_MODE
                                        printf("ThisTask=%d, Sink assigned to multiple sinks: P[j.]ID=%llu has SwallowTime of %g and had SwallowID of %llu while the freefall time to BH with id=%llu is %g. This reduces mdot by %g to %g for the sink, which has been updated accordingly.\n",ThisTask, (unsigned long long) P[j].ID,P[j].SwallowTime,(unsigned long long) P[j].SwallowID, (unsigned long long) id, tff, str_f_acc[k]*P[j].Mass/dt, mdot);
#endif
                                       str_f_acc[k] = 0; //we don't accrete this
                                   }
                               } /* check list */
                            } /* go over list */
                        } /* is gas */
#endif
                        
                        /* now is the more standard accretion only of gas, according to the mdot calculated before */
                        if(P[j].Type == 0)
                        {
                            /* here we have a gas particle */
                            u = r * hinv; kernel_main(u,hinv3,hinv*hinv3,&wk,&dwk,-1);
#if defined(BH_SWALLOWGAS) && !defined(BH_GRAVCAPTURE_GAS) && !defined(NEWSINK) // this below is only meaningful if !defined(BH_GRAVCAPTURE_GAS)...
                            /* compute accretion probability */
                            if((bh_mass_withdisk - (mass + mass_markedswallow))>0) {p = (bh_mass_withdisk - (mass + mass_markedswallow)) * wk / rho;} else {p = 0;}
#ifdef BH_WIND_KICK
                            /* DAA: for stochastic winds (BH_WIND_KICK) we remove a fraction of mass from gas particles prior to kicking --> need to increase the probability here to balance black hole growth */
                            if(f_accreted>0) 
                            {
                                /* DAA: compute outflow probability when "bh_mass_withdisk < mass" - we don't need to enforce mass conservation in this case, relevant only in low-res sims where the BH seed mass is much lower than the gas particle mass */
                                p /= f_accreted; if((bh_mass_withdisk - mass) < 0) {p = ( (1-f_accreted)/f_accreted ) * mdot * dt * wk / rho;}
                            }
#endif
                            w = get_random_number(P[j].ID);
                            if(w < p)
                            {
#ifndef IO_REDUCED_MODE
                                printf("MARKING_BH_FOOD: j %d w %g p %g TO_BE_SWALLOWED \n",j,w,p);
#endif
                                if(P[j].SwallowID < id)
                                {
                                   P[j].SwallowID = id;
#ifdef BH_WIND_KICK
                                   mass_markedswallow += P[j].Mass*f_accreted;
#else
                                   mass_markedswallow += P[j].Mass;
#endif
                                }
                            } // if(w < p)
#endif // BH_SWALLOWGAS

#if defined(BH_PHOTONMOMENTUM) || defined(BH_WIND_CONTINUOUS)
                            /* calculate the angle-weighting for the photon momentum */
                            if((mdot>0)&&(dt>0)&&(r>0)&&(P[j].SwallowID==0)&&(P[j].Mass>0)&&(P[j].Type==0))
                            {
                                /* cos_theta with respect to disk of BH is given by dot product of r and Jgas */
                                norm=0; for(k=0;k<3;k++) norm+=(dpos[k]/r)*Jgas_in_Kernel[k];
                                norm=fabs(norm); theta=acos(norm);
                                BH_angle_weighted_kernel_sum += bh_angleweight_localcoupling(j,BH_disk_hr,theta,r,h_i);
                            }
#endif
                            
#ifdef BH_THERMALFEEDBACK
                            {
                                energy = bh_lum_bol(mdot, bh_mass, -1) * dt;
                                if(rho > 0) {SphP[j].Injected_BH_Energy += (wk/rho) * energy * P[j].Mass;}
                            }
#endif
                        } // if(P[j].Type == 0)

                    } // if(r2 < h_i2)
                } // if(P[j].Mass > 0)
            } // for(n = 0; n < numngb; n++)
        } // while(startnode >= 0)
        
        if(mode == 1)
        {
            listindex++;
            if(listindex < NODELISTLENGTH)
            {
                startnode = BlackholeDataGet[target].NodeList[listindex];
                if(startnode >= 0)
                    startnode = Nodes[startnode].u.d.nextnode;	/* open it */
            }
        } // mode==1
    } // while(startnode >= 0) (outer of the double-loop)
    
    
    /* Now collect the result at the right place */
    if(mode == 0)
    {
#if defined(BH_PHOTONMOMENTUM) || defined(BH_WIND_CONTINUOUS)
        BlackholeTempInfo[P[target].IndexMapToTempStruc].BH_angle_weighted_kernel_sum += BH_angle_weighted_kernel_sum;  /* need to correct target index */
#endif
#ifdef BH_REPOSITION_ON_POTMIN
        BPP(target).BH_MinPot = minpot; for(k = 0; k < 3; k++) {BPP(target).BH_MinPotPos[k] = minpotpos[k];}
#endif
    }
    else
    {
#if defined(BH_PHOTONMOMENTUM) || defined(BH_WIND_CONTINUOUS)
        BlackholeDataResult[target].BH_angle_weighted_kernel_sum = BH_angle_weighted_kernel_sum;
#endif
#ifdef BH_REPOSITION_ON_POTMIN
        BlackholeDataResult[target].BH_MinPot = minpot; for(k = 0; k < 3; k++) {BlackholeDataResult[target].BH_MinPotPos[k] = minpotpos[k];}
#endif
    }
    return 0;
} /* closes bh_evaluate routine */
