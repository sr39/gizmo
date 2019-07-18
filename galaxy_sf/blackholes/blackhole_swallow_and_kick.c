/*! \file blackhole_swallow_and_kick.c
 *  \brief routines for gas accretion onto black holes, and black hole mergers
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

static int N_gas_swallowed, N_star_swallowed, N_dm_swallowed, N_BH_swallowed;

void blackhole_swallow_and_kick_loop(void)
{
    int i, j, k;
    int ndone_flag, ndone;
    int ngrp, recvTask, place, nexport, nimport, dummy;
    MPI_Status status;
    
    int Ntot_gas_swallowed, Ntot_star_swallowed, Ntot_dm_swallowed, Ntot_BH_swallowed;
    
    /* allocate buffers to arrange communication */
    size_t MyBufferSize = All.BufferSize;
    Ngblist = (int *) mymalloc("Ngblist", NumPart * sizeof(int));
    All.BunchSize = (int) ((MyBufferSize * 1024 * 1024) / (sizeof(struct data_index) + sizeof(struct data_nodelist) +
                                                             sizeof(struct blackholedata_in) +
                                                             sizeof(struct blackholedata_out) +
                                                             sizemax(sizeof(struct blackholedata_in),sizeof(struct blackholedata_out))));
    DataIndexTable = (struct data_index *) mymalloc("DataIndexTable", All.BunchSize * sizeof(struct data_index));
    DataNodeList = (struct data_nodelist *) mymalloc("DataNodeList", All.BunchSize * sizeof(struct data_nodelist));
    
    N_gas_swallowed = N_star_swallowed = N_dm_swallowed = N_BH_swallowed = 0;
    Ntot_gas_swallowed = Ntot_star_swallowed = Ntot_dm_swallowed = Ntot_BH_swallowed = 0;
    
    i = FirstActiveParticle;	/* first particle for this task */
    do
    {
        for(j = 0; j < NTask; j++)
        {
            Send_count[j] = 0;
            Exportflag[j] = -1;
        }
        /* do local particles and prepare export list */
        for(nexport = 0; i >= 0; i = NextActiveParticle[i])
            if(P[i].Type == 5)
                if(P[i].SwallowID == 0)     /* this particle not being swallowed */
                    if(blackhole_swallow_and_kick_evaluate(i, 0, &nexport, Send_count) < 0)
                        break;
        
        qsort(DataIndexTable, nexport, sizeof(struct data_index), data_index_compare);
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
        
        
        /* populate the struct to be exported */
        for(j = 0; j < nexport; j++)
        {
            place = DataIndexTable[j].Index;
            
            for(k = 0; k < 3; k++)
            {
                BlackholeDataIn[j].Pos[k] = P[place].Pos[k];
                BlackholeDataIn[j].Vel[k] = P[place].Vel[k];
#if defined(NEWSINK_J_FEEDBACK)
                BlackholeDataIn[j].Jsink[k] = BPP(place).Jsink[k];
#endif
#if defined(BH_PHOTONMOMENTUM) || defined(BH_WIND_CONTINUOUS) || defined(BH_WIND_KICK)
                BlackholeDataIn[j].Jgas_in_Kernel[k] = BlackholeTempInfo[P[place].IndexMapToTempStruc].Jgas_in_Kernel[k];
#endif
            }
            BlackholeDataIn[j].Hsml = PPP[place].Hsml;
            BlackholeDataIn[j].ID = P[place].ID;
            BlackholeDataIn[j].Mass = P[place].Mass;
            BlackholeDataIn[j].BH_Mass = BPP(place).BH_Mass;
#ifdef BH_ALPHADISK_ACCRETION
            BlackholeDataIn[j].BH_Mass_AlphaDisk = BPP(place).BH_Mass_AlphaDisk;
#endif
#ifdef NEWSINK
            //Copy info on neighbours
            BlackholeDataIn[j].n_neighbor = BlackholeTempInfo[P[place].IndexMapToTempStruc].n_neighbor;
            memcpy(BlackholeDataIn[j].rgas,BlackholeTempInfo[P[place].IndexMapToTempStruc].rgas, NEWSINK_NEIGHBORMAX * sizeof(MyFloat));
            memcpy(BlackholeDataIn[j].xgas,BlackholeTempInfo[P[place].IndexMapToTempStruc].xgas, NEWSINK_NEIGHBORMAX * sizeof(MyFloat));
            memcpy(BlackholeDataIn[j].ygas,BlackholeTempInfo[P[place].IndexMapToTempStruc].ygas, NEWSINK_NEIGHBORMAX * sizeof(MyFloat));
            memcpy(BlackholeDataIn[j].zgas,BlackholeTempInfo[P[place].IndexMapToTempStruc].zgas, NEWSINK_NEIGHBORMAX * sizeof(MyFloat));
            memcpy(BlackholeDataIn[j].mgas,BlackholeTempInfo[P[place].IndexMapToTempStruc].mgas, NEWSINK_NEIGHBORMAX * sizeof(MyFloat));
            memcpy(BlackholeDataIn[j].Hsmlgas,BlackholeTempInfo[P[place].IndexMapToTempStruc].Hsmlgas, NEWSINK_NEIGHBORMAX * sizeof(MyFloat));
            memcpy(BlackholeDataIn[j].gasID,BlackholeTempInfo[P[place].IndexMapToTempStruc].gasID, NEWSINK_NEIGHBORMAX * sizeof(MyFloat));
            memcpy(BlackholeDataIn[j].isbound,BlackholeTempInfo[P[place].IndexMapToTempStruc].isbound, NEWSINK_NEIGHBORMAX * sizeof(int));
            memcpy(BlackholeDataIn[j].f_acc,BlackholeTempInfo[P[place].IndexMapToTempStruc].f_acc, NEWSINK_NEIGHBORMAX * sizeof(MyFloat));
#if defined(NEWSINK_J_FEEDBACK)
            memcpy(BlackholeDataIn[j].dv_ang_kick_norm,BlackholeTempInfo[P[place].IndexMapToTempStruc].dv_ang_kick_norm, NEWSINK_NEIGHBORMAX * sizeof(MyFloat));
            BlackholeDataIn[j].t_disc = BPP(place).t_disc;
#endif
#endif
#if defined(BH_PHOTONMOMENTUM) || defined(BH_WIND_CONTINUOUS)
            BlackholeDataIn[j].BH_disk_hr = P[place].BH_disk_hr;
            BlackholeDataIn[j].BH_angle_weighted_kernel_sum = BlackholeTempInfo[P[place].IndexMapToTempStruc].BH_angle_weighted_kernel_sum;
#endif
            BlackholeDataIn[j].Mdot = BPP(place).BH_Mdot;
#ifndef WAKEUP
            BlackholeDataIn[j].Dt = (P[place].TimeBin ? (((integertime) 1) << P[place].TimeBin) : 0) * All.Timebase_interval / All.cf_hubble_a;
#else
            BlackholeDataIn[j].Dt = P[place].dt_step * All.Timebase_interval / All.cf_hubble_a;
#endif
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
                                 Send_count[recvTask] * sizeof(struct blackholedata_in), MPI_BYTE,
                                 recvTask, TAG_BH_G,
                                 &BlackholeDataGet[Recv_offset[recvTask]],
                                 Recv_count[recvTask] * sizeof(struct blackholedata_in), MPI_BYTE,
                                 recvTask, TAG_BH_G, MPI_COMM_WORLD, &status);
                }
            }
        }
        myfree(BlackholeDataIn);
        BlackholeDataResult = (struct blackholedata_out *) mymalloc("BlackholeDataResult", nimport * sizeof(struct blackholedata_out));
        BlackholeDataOut = (struct blackholedata_out *) mymalloc("BlackholeDataOut", nexport * sizeof(struct blackholedata_out));
        
        /* do the particles that were sent to us */
        for(j = 0; j < nimport; j++)
            blackhole_swallow_and_kick_evaluate(j, 1, &dummy, &dummy);  /* set BlackholeDataResult based on BlackholeDataGet */
        
        if(i < 0)
            ndone_flag = 1;
        else
            ndone_flag = 0;
        
        MPI_Allreduce(&ndone_flag, &ndone, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
        /* get the result */
        for(ngrp = 1; ngrp < (1 << PTask); ngrp++)
        {
            recvTask = ThisTask ^ ngrp;
            if(recvTask < NTask)
            {
                if(Send_count[recvTask] > 0 || Recv_count[recvTask] > 0)
                {
                    /* send the results */
                    MPI_Sendrecv(&BlackholeDataResult[Recv_offset[recvTask]],
                                 Recv_count[recvTask] * sizeof(struct blackholedata_out),
                                 MPI_BYTE, recvTask, TAG_BH_H,
                                 &BlackholeDataOut[Send_offset[recvTask]],
                                 Send_count[recvTask] * sizeof(struct blackholedata_out),
                                 MPI_BYTE, recvTask, TAG_BH_H, MPI_COMM_WORLD, &status);
                }
            }
        }
        /* add the result to the particles */
        for(j = 0; j < nexport; j++)
        {
            place = DataIndexTable[j].Index;
            
            BlackholeTempInfo[P[place].IndexMapToTempStruc].accreted_Mass += BlackholeDataOut[j].Mass;
            BlackholeTempInfo[P[place].IndexMapToTempStruc].accreted_BH_Mass += BlackholeDataOut[j].BH_Mass;
#ifdef BH_ALPHADISK_ACCRETION
            BPP(place).BH_Mass_AlphaDisk += BlackholeDataOut[j].BH_Mass_AlphaDisk;
#endif
            for(k = 0; k < 3; k++){
                BlackholeTempInfo[P[place].IndexMapToTempStruc].accreted_momentum[k] += BlackholeDataOut[j].accreted_momentum[k];
#if defined(SINGLE_STAR_STRICT_ACCRETION) || defined(NEWSINK)
                BlackholeTempInfo[P[place].IndexMapToTempStruc].accreted_moment[k] += BlackholeDataOut[j].accreted_moment[k];
#endif
#if defined(NEWSINK_J_FEEDBACK)
                BlackholeTempInfo[P[place].IndexMapToTempStruc].accreted_J[k] += BlackholeDataOut[j].accreted_J[k];
#endif		
            }
#ifdef BH_COUNTPROGS
            BPP(place).BH_CountProgs += BlackholeDataOut[j].BH_CountProgs;
#endif
#ifdef GALSF
            if(P[place].StellarAge > BlackholeDataOut[j].Accreted_Age)
                P[place].StellarAge = BlackholeDataOut[j].Accreted_Age;
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
    
    
    MPI_Reduce(&N_gas_swallowed, &Ntot_gas_swallowed, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&N_BH_swallowed, &Ntot_BH_swallowed, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&N_star_swallowed, &Ntot_star_swallowed, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&N_dm_swallowed, &Ntot_dm_swallowed, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
    
    if((ThisTask == 0)&&(Ntot_gas_swallowed+Ntot_star_swallowed+Ntot_dm_swallowed+Ntot_BH_swallowed>0))
    {
        printf("Accretion done: swallowed %d gas, %d star, %d dm, and %d BH particles\n",
               Ntot_gas_swallowed, Ntot_star_swallowed, Ntot_dm_swallowed, Ntot_BH_swallowed);
    }
    
}




int blackhole_swallow_and_kick_evaluate(int target, int mode, int *nexport, int *nSend_local)
{
    int startnode, numngb, j, k, n, bin, listindex = 0;
    MyIDType id;
    MyLongDouble accreted_mass, accreted_BH_mass, accreted_momentum[3];
#if defined(SINGLE_STAR_STRICT_ACCRETION) || defined(NEWSINK)
    MyLongDouble accreted_moment[3];
#endif
#ifdef NEWSINK
    MyFloat f_acc_corr=1.0,mdot_avg;
    MyFloat *str_f_acc;
    int n_neighbor;
    MyIDType *str_gasID;
    MyFloat int_zone_radius;
#endif
#if defined(NEWSINK_J_FEEDBACK)
    MyLongDouble accreted_J[3];
    MyFloat dx[3], dv[3], dr;
    MyFloat Jsinktot, dJsinkpred, Jcrossdr[3];
    MyFloat *Jsink, *str_dv_ang_kick_norm;
    MyFloat tdisc;
    MyDouble dv_ang_kick_norm=0; /*Normalization factor for angular momentum feedback kicks*/ 
#endif
#if defined(NEWSINK_STOCHASTIC_ACCRETION)
    double w; int kicked=0;
#endif
    MyFloat *pos, h_i, bh_mass;
#if (defined(BH_WIND_CONTINUOUS) && !defined(BH_WIND_KICK)) || defined(NEWSINK_J_FEEDBACK)
    MyFloat *velocity, hinv, hinv3;
#endif
    MyFloat f_accreted=0;
#if defined(NEWSINK_J_FEEDBACK) || defined(BH_WIND_KICK)
    MyFloat mass;
#ifdef BH_WIND_KICK
    MyFloat v_kick=0;
    MyFloat bh_mass_withdisk;
#ifdef BH_ALPHADISK_ACCRETION
    MyFloat bh_mass_alphadisk;     // DAA: we need bh_mass_alphadisk for BH_WIND_KICK winds below
#endif
#endif
#endif
#if (defined(BH_PHOTONMOMENTUM) || defined(BH_WIND_CONTINUOUS)) || defined(NEWSINK)
    MyFloat mdot,dt;
#endif
    
    MyFloat dir[3], norm, mom;
    mom=0; norm=0; dir[0]=0;
#if defined(BH_PHOTONMOMENTUM) || defined(BH_WIND_CONTINUOUS) || defined(BH_WIND_KICK)
    MyFloat *Jgas_in_Kernel;
#endif
#if defined(BH_PHOTONMOMENTUM) || defined(BH_WIND_CONTINUOUS)
    double BH_angle_weighted_kernel_sum, mom_wt;
    MyFloat theta,BH_disk_hr,kernel_zero,dwk;
    kernel_main(0.0,1.0,1.0,&kernel_zero,&dwk,-1);
#endif
#ifdef GALSF
    double accreted_age = 1;
#endif
#ifdef BH_ALPHADISK_ACCRETION
    MyFloat accreted_BH_mass_alphadisk;   
#endif
    
    int mod_index = 0;
//printf("%d BH swallow line 285\n", ThisTask);
    if(mode == 0)
    {
        pos = P[target].Pos;
#if (defined(BH_WIND_CONTINUOUS) && !defined(BH_WIND_KICK)) || defined(NEWSINK_J_FEEDBACK)
        velocity = P[target].Vel;
#endif
        h_i = PPP[target].Hsml;
        id = P[target].ID;
#if defined(BH_WIND_KICK) || defined(NEWSINK_J_FEEDBACK)
        mass = P[target].Mass;    
#endif
#if defined(BH_ALPHADISK_ACCRETION) && defined(BH_WIND_KICK)
        bh_mass_alphadisk = BPP(target).BH_Mass_AlphaDisk;
#endif
        bh_mass = BPP(target).BH_Mass;
#if (defined(BH_PHOTONMOMENTUM) || defined(BH_WIND_CONTINUOUS)) || defined(NEWSINK)
        mdot = BPP(target).BH_Mdot;
#ifndef WAKEUP
        dt = (P[target].TimeBin ? (((integertime) 1) << P[target].TimeBin) : 0) * All.Timebase_interval / All.cf_hubble_a;
#else
        dt = P[target].dt_step * All.Timebase_interval / All.cf_hubble_a;
#endif
#endif
#if defined(NEWSINK)
        mdot_avg = BPP(target).BH_Mdot_Avg;
        int_zone_radius = P[target].Hsml * INT_ZONE_TO_HSML;
        n_neighbor = BlackholeTempInfo[P[target].IndexMapToTempStruc].n_neighbor;
        str_f_acc = BlackholeTempInfo[P[target].IndexMapToTempStruc].f_acc;
        str_gasID = BlackholeTempInfo[P[target].IndexMapToTempStruc].gasID;
#if defined(NEWSINK_J_FEEDBACK)
        Jsink = BPP(target).Jsink;
        Jsinktot = sqrt(Jsink[0]*Jsink[0] + Jsink[1]*Jsink[1] +Jsink[2]*Jsink[2]);
        tdisc = BPP(target).t_disc;
        str_dv_ang_kick_norm = BlackholeTempInfo[P[target].IndexMapToTempStruc].dv_ang_kick_norm;
#endif
#endif
#if defined(BH_PHOTONMOMENTUM) || defined(BH_WIND_CONTINUOUS) || defined(BH_WIND_KICK)
        Jgas_in_Kernel = BlackholeTempInfo[P[target].IndexMapToTempStruc].Jgas_in_Kernel;
#endif
#if defined(BH_PHOTONMOMENTUM) || defined(BH_WIND_CONTINUOUS)
        BH_disk_hr = P[target].BH_disk_hr;
        BH_angle_weighted_kernel_sum = BlackholeTempInfo[P[target].IndexMapToTempStruc].BH_angle_weighted_kernel_sum;
#endif
        mod_index = P[target].IndexMapToTempStruc;  /* the index of the BlackholeTempInfo should we modify*/
    }
    else
    {
        pos = BlackholeDataGet[target].Pos;
#if (defined(BH_WIND_CONTINUOUS) && !defined(BH_WIND_KICK)) || defined(NEWSINK_J_FEEDBACK)
        velocity = BlackholeDataGet[target].Vel;
#endif
        h_i = BlackholeDataGet[target].Hsml;
        id = BlackholeDataGet[target].ID;
#if defined(BH_WIND_KICK) || defined(NEWSINK_J_FEEDBACK)
        mass = BlackholeDataGet[target].Mass;
#if defined(BH_ALPHADISK_ACCRETION) && defined(BH_WIND_KICK)
        bh_mass_alphadisk = BlackholeDataGet[target].BH_Mass_AlphaDisk;      
#endif
#endif
        bh_mass = BlackholeDataGet[target].BH_Mass;
#if defined(BH_PHOTONMOMENTUM) || defined(BH_WIND_CONTINUOUS) || defined(NEWSINK)
        mdot = BlackholeDataGet[target].Mdot;
        dt = BlackholeDataGet[target].Dt;
#endif
#if defined(NEWSINK)
        mdot_avg = BlackholeDataGet[target].BH_Mdot_Avg;
        int_zone_radius = BlackholeDataGet[target].Hsml * INT_ZONE_TO_HSML;
        n_neighbor = BlackholeDataGet[target].n_neighbor;
        str_f_acc = BlackholeDataGet[target].f_acc;
        str_gasID = BlackholeDataGet[target].gasID;
#if defined(NEWSINK_J_FEEDBACK)
        Jsink = BlackholeDataGet[target].Jsink;
        Jsinktot = sqrt(Jsink[0]*Jsink[0] + Jsink[1]*Jsink[1] +Jsink[2]*Jsink[2]);
        tdisc = BlackholeDataGet[target].t_disc;
        str_dv_ang_kick_norm = BlackholeDataGet[target].dv_ang_kick_norm;
#endif	
#endif
#if defined(BH_PHOTONMOMENTUM) || defined(BH_WIND_CONTINUOUS) || defined(BH_WIND_KICK)
        Jgas_in_Kernel = BlackholeDataGet[target].Jgas_in_Kernel;
#endif
#if defined(BH_PHOTONMOMENTUM) || defined(BH_WIND_CONTINUOUS)
        BH_disk_hr = BlackholeDataGet[target].BH_disk_hr;
        BH_angle_weighted_kernel_sum = BlackholeDataGet[target].BH_angle_weighted_kernel_sum;
#endif
    }
//printf("%d BH swallow line 369\n", ThisTask);
#ifdef BH_WIND_KICK
    bh_mass_withdisk = bh_mass;
#ifdef BH_ALPHADISK_ACCRETION
    bh_mass_withdisk += bh_mass_alphadisk;
#endif
#endif
    
    accreted_mass = 0;
    accreted_BH_mass = 0;
#ifdef BH_ALPHADISK_ACCRETION
    accreted_BH_mass_alphadisk = 0;
#endif
    accreted_momentum[0] = accreted_momentum[1] = accreted_momentum[2] = 0;
#if defined(SINGLE_STAR_STRICT_ACCRETION) || defined(NEWSINK)
    accreted_moment[0] = accreted_moment[1] = accreted_moment[2] = 0;
#endif
#if defined(NEWSINK_J_FEEDBACK)
    accreted_J[0] = accreted_J[1] = accreted_J[2] = 0;
    if (Jsinktot>0){
        dJsinkpred = Jsinktot * (1.0 - exp(-dt/tdisc));
        /*Sum up normalization factor for angular momentum feedback*/
//printf("%d BH swallow line 388\n", ThisTask);
        for(k=0;k<n_neighbor;k++){
            if (str_f_acc[k]<1.0){
                dv_ang_kick_norm += str_dv_ang_kick_norm[k]; //we only give feedback to particles we don't swallow completely
            }
        }
    }
//printf("%d BH swallow ang_kick normalization calculated: %g  with %d neighbors \n", ThisTask, dv_ang_kick_norm, n_neighbor );
#endif
    
#ifdef BH_COUNTPROGS
    int accreted_BH_progs = 0;
#endif
#if defined(BH_PHOTONMOMENTUM) || defined(BH_WIND_CONTINUOUS)
#ifdef NEWSINK
    mom = bh_lum_bol(mdot_avg, bh_mass, -1) * dt / (C / All.UnitVelocity_in_cm_per_s);
#else
    mom = bh_lum_bol(mdot, bh_mass, -1) * dt / (C / All.UnitVelocity_in_cm_per_s);
#endif
    mom_wt = 0;
#endif
    
#if defined(BH_WIND_CONTINUOUS) && !defined(BH_WIND_KICK)
    hinv=h_i; hinv3=hinv*hinv*hinv;
#endif
    
    if(mode == 0)
    {
        startnode = All.MaxPart;	/* root node */
    }
    else
    {
        startnode = BlackholeDataGet[target].NodeList[0];
        startnode = Nodes[startnode].u.d.nextnode;	/* open it */
    }
    
    while(startnode >= 0)
    {
        while(startnode >= 0)
        {
#if defined(NEWSINK)
            numngb = ngb_treefind_pairs_targeted(pos, int_zone_radius, target, &startnode, mode, nexport, nSend_local, BH_NEIGHBOR_BITFLAG); // BH_NEIGHBOR_BITFLAG defines which types of particles we search for
#else
            numngb = ngb_treefind_pairs_targeted(pos, h_i, target, &startnode, mode, nexport, nSend_local, BH_NEIGHBOR_BITFLAG); // BH_NEIGHBOR_BITFLAG defines which types of particles we search for
#endif
            if(numngb < 0) return -1;
            for(n = 0; n < numngb; n++)
            {
                j = Ngblist[n]; MyIDType OriginallyMarkedSwallowID = P[j].SwallowID; // record this to help prevent double-counting below
#if defined(NEWSINK_J_FEEDBACK)
                dx[0]=P[j].Pos[0]-pos[0]; dx[1]=P[j].Pos[1]-pos[1]; dx[2]=P[j].Pos[2]-pos[2]; dr = sqrt(dx[0]*dx[0] + dx[1]*dx[1] + dx[2]*dx[2]);
#endif
                /* we've found a particle to be swallowed.  This could be a BH merger, DM particle, or baryon w/ feedback */
                if(P[j].SwallowID == id && P[j].Mass > 0)
                {
#ifndef IO_REDUCED_MODE
                    printf("found particle P[j].ID = %llu with P[j].SwallowID = %llu of type P[j].Type = %d nearby id = %llu with P[j].Mass=%g\n",
                           (unsigned long long) P[j].ID, (unsigned long long) P[j].SwallowID, P[j].Type, (unsigned long long) id, P[j].Mass);
#endif
                    if(P[j].Type == 5)  /* this is a BH-BH merger */
                    {
#ifdef BH_OUTPUT_MOREINFO
                        fprintf(FdBhMergerDetails,"%g  %u %g %2.7f %2.7f %2.7f  %u %g %2.7f %2.7f %2.7f\n", All.Time,  id,bh_mass,pos[0],pos[1],pos[2],  P[j].ID,BPP(j).BH_Mass,P[j].Pos[0],P[j].Pos[1],P[j].Pos[2]);
#else
#ifndef IO_REDUCED_MODE
                        fprintf(FdBlackHolesDetails,"ThisTask=%d, time=%g: id=%u swallows %u (%g %g)\n", ThisTask, All.Time, id, P[j].ID, bh_mass, BPP(j).BH_Mass);
#endif
#endif

#ifdef BH_INCREASE_DYNAMIC_MASS
                        /* the true dynamical mass of the merging BH is P[j].Mass/BH_INCREASE_DYNAMIC_MASS unless exceeded by physical growth
                         - in the limit BPP(j).BH_Mass > BH_INCREASE_DYNAMIC_MASS x m_b, then bh_mass=P[j].Mass on average and we are good as well  */
                        accreted_mass    += FLT( DMAX(BPP(j).BH_Mass, P[j].Mass/BH_INCREASE_DYNAMIC_MASS) );
#else
                        accreted_mass    += FLT(P[j].Mass);
#endif
                        accreted_BH_mass += FLT(BPP(j).BH_Mass);
#ifdef BH_ALPHADISK_ACCRETION
                        accreted_BH_mass_alphadisk += FLT(BPP(j).BH_Mass_AlphaDisk);
#endif
#ifdef BH_WIND_SPAWN
#ifdef BH_ALPHADISK_ACCRETION
                        accreted_BH_mass_alphadisk += FLT(BPP(j).unspawned_wind_mass);
#else
                        accreted_BH_mass += FLT(BPP(j).unspawned_wind_mass);
#endif
#endif
                        for(k = 0; k < 3; k++){accreted_momentum[k] += FLT(P[j].Mass * P[j].Vel[k]);}
#if defined(NEWSINK_J_FEEDBACK)
                        dv[0]=BPP(j).Vel[0]-velocity[0];dv[1]=BPP(j).Vel[1]-velocity[1];dv[2]=BPP(j).Vel[2]-velocity[2];
                        accreted_J[0] += FLT(P[j].Mass *(dx[1]*dv[2] - dx[2]*dv[1]) + BPP(j).Jsink[0]);
                        accreted_J[1] += FLT(P[j].Mass *(dx[2]*dv[0] - dx[0]*dv[2]) + BPP(j).Jsink[1]);
                        accreted_J[2] += FLT(P[j].Mass *(dx[0]*dv[1] - dx[1]*dv[0]) + BPP(j).Jsink[2]);
#endif
#ifdef BH_COUNTPROGS
                        accreted_BH_progs += BPP(j).BH_CountProgs;
#endif
                        bin = P[j].TimeBin; TimeBin_BH_mass[bin] -= BPP(j).BH_Mass; TimeBin_BH_dynamicalmass[bin] -= P[j].Mass; TimeBin_BH_Mdot[bin] -= BPP(j).BH_Mdot;
                        if(BPP(j).BH_Mass > 0) {TimeBin_BH_Medd[bin] -= BPP(j).BH_Mdot / BPP(j).BH_Mass;}
                        P[j].Mass = 0; BPP(j).BH_Mass = 0; BPP(j).BH_Mdot = 0;
#ifdef GALSF
                        accreted_age = P[j].StellarAge;
#endif
                        N_BH_swallowed++;
                    } // if(P[j].Type == 5) -- BH + BH merger


#ifdef BH_GRAVCAPTURE_NONGAS /* DM and star particles can only be accreted ifdef BH_GRAVCAPTURE_NONGAS */
                    /* this is a DM particle: In this case, no kick, so just zero out the mass and 'get rid of' the particle (preferably by putting it somewhere irrelevant) */
                    if((P[j].Type == 1) || (All.ComovingIntegrationOn && (P[j].Type==2||P[j].Type==3)) )
                    {
#ifndef IO_REDUCED_MODE
                        printf("BH_swallow_DM: j %d Type(j) %d  M(j) %g V(j).xyz %g/%g/%g P(j).xyz %g/%g/%g p(i).xyz %g/%g/%g \n", j,P[j].Type,P[j].Mass,P[j].Vel[0],P[j].Vel[1],P[j].Vel[2],P[j].Pos[0],P[j].Pos[1],P[j].Pos[2],pos[0],pos[1],pos[2]);
#endif
                        accreted_mass += FLT(P[j].Mass); accreted_BH_mass += FLT(P[j].Mass);
                        P[j].Mass = 0;		// zero out particle mass.  it has now been fully swallowed.
                        N_dm_swallowed++;
                    }

                    /* this is a star particle: If there is an alpha-disk, we let them go to the disk. If there is no alpha-disk, stars go to the BH directly and won't affect feedback. (Can be simply modified if we need something different.) */
                    if((P[j].Type==4) || ((P[j].Type==2||P[j].Type==3) && !(All.ComovingIntegrationOn) ))
                    {
                        accreted_mass += FLT(P[j].Mass);
#ifdef BH_ALPHADISK_ACCRETION
                        accreted_BH_mass_alphadisk += FLT(P[j].Mass);
#else 
                        accreted_BH_mass += FLT(P[j].Mass);   /* mass goes directly to the BH, not just the parent particle */
#endif
                        P[j].Mass = 0;          // zero out particle mass.  it has now been fully swallowed.
                        N_star_swallowed++;
                    }
#endif // #ifdef BH_GRAVCAPTURE_NONGAS -- BH + DM or Star merger



                    /* this is a gas particle: DAA: we need to see if the gas particle has to be accreted in full or not, depending on BH_WIND_KICK
                     the only difference with BH_ALPHADISK_ACCRETION should be that the mass goes first to the alphadisk */
                    if(P[j].Type == 0)                    
                    {
#ifdef BH_WIND_KICK
                        f_accreted = All.BAL_f_accretion;
#ifndef BH_GRAVCAPTURE_GAS
                        if((All.BlackHoleFeedbackFactor > 0) && (All.BlackHoleFeedbackFactor != 1.)) {f_accreted /= All.BlackHoleFeedbackFactor;} else {if(All.BAL_v_outflow > 0) f_accreted = 1./(1. + fabs(1.*BH_WIND_KICK)*All.BlackHoleRadiativeEfficiency*(C/All.UnitVelocity_in_cm_per_s)/(All.BAL_v_outflow*1e5/All.UnitVelocity_in_cm_per_s));}
                        if((bh_mass_withdisk - mass) <= 0) {f_accreted=0;} // DAA: no need to accrete gas particle to enforce mass conservation (we will simply kick),  note that here the particle mass P.Mass is larger than the physical BH mass P.BH_Mass
#endif // #ifdef BH_GRAVCAPTURE_GAS
#else // #ifdef BH_WIND_KICK
                        f_accreted = 1;                           // DAA: no "kick winds" so we need to accrete gas particle in full
#endif

#if defined(NEWSINK)
/*Only take a portion of the mass if we are accreting more than needed*/
                        f_acc_corr = 1.0;
                        for(k=0;k<n_neighbor;k++){ /*Find the accretion factor we prescribed for this particle from list*/
                            if( P[j].ID == str_gasID[k]){
                                f_acc_corr = DMIN( str_f_acc[k], 1.0);
                                if (f_acc_corr < 0) {f_acc_corr=0;}
#if !defined(NEWSINK_STOCHASTIC_ACCRETION)
                                else {if ((1.0-f_acc_corr) < 1e-2) {f_acc_corr=1.0;} //failsafe for weird numerical issues
                                     else {f_accreted *= f_acc_corr;} //change accretion fraction if needed
                                }
#endif
                            }
                        }
#if defined(NEWSINK_STOCHASTIC_ACCRETION) //In this case we stochastically decide whether to accrete the entire particle
                        w = get_random_number(P[j].ID);
                        if(w < f_acc_corr){ f_accreted=1.0;} //this means we fully accrete the particle
                        else{ f_accreted=0.0; } //we don't take this particle
#endif

#ifdef BH_OUTPUT_MOREINFO
                        if ((f_acc_corr != 1.0) && (f_acc_corr != 0.0)) {printf("n=%llu f_acc_corr is: %g for particle with id %llu and mass %g around BH with id %llu\n", (unsigned long long) target, (MyFloat) f_acc_corr,(unsigned long long) P[j].ID, P[j].Mass,(unsigned long long) id);}
#endif
#endif
                        if (f_accreted>0.0){
/* #if defined(NEWSINK_STOCHASTIC_ACCRETION) && defined(BH_WIND_KICK) //We stochastically determine if this "accreted" particle is really accreted and we take its mass or it gets kicked out */
/*                             w = get_random_number(P[j].ID); kicked=0; */
/*                             if(w > All.BAL_f_accretion){ */
/*                                 kicked=1;f_accreted=0.0; */
/*                             } */
/*                             else{ */
/* #endif */
                                accreted_mass += FLT(f_accreted*P[j].Mass);
#ifdef BH_GRAVCAPTURE_GAS
#ifdef BH_ALPHADISK_ACCRETION       /* mass goes into the alpha disk, before going into the BH */
                                accreted_BH_mass_alphadisk += FLT(f_accreted*P[j].Mass);
#else                               /* mass goes directly to the BH, not just the parent particle */
                                accreted_BH_mass += FLT(f_accreted*P[j].Mass);
#endif
                                for(k = 0; k < 3; k++){
                                    accreted_momentum[k] += FLT(f_accreted * P[j].Mass * P[j].Vel[k]);
#if defined(SINGLE_STAR_STRICT_ACCRETION) || defined(NEWSINK)
                                    accreted_moment[k] += FLT(f_accreted * P[j].Mass * P[j].Pos[k]);
#endif
                                }
#if defined(NEWSINK_J_FEEDBACK)
                                dv[0]=P[j].Vel[0]-velocity[0];dv[1]=P[j].Vel[1]-velocity[1];dv[2]=P[j].Vel[2]-velocity[2];
                                accreted_J[0] += FLT(f_accreted * P[j].Mass *(dx[1]*dv[2] - dx[2]*dv[1]) + P[j].Jsink[0]);
                                accreted_J[1] += FLT(f_accreted * P[j].Mass *(dx[2]*dv[0] - dx[0]*dv[2]) + P[j].Jsink[1]);
                                accreted_J[2] += FLT(f_accreted * P[j].Mass *(dx[0]*dv[1] - dx[1]*dv[0]) + P[j].Jsink[2]);
#endif				
/* #ifdef NEWSINK_B_FEEDBACK */
/* 				if(f_accreted == 1.0){ // if the particle is still around after then we leave the flux alone */
/* 				  for(k=0;k<3;k++)   accreted_B[k] += SphP[i].B[k]; */
/* #ifdef DIVBCLEANING_DEDNER */
/* 				  accreted_Phi += SphP[i].Phi; */
/* #endif */
//				}
//#endif				
#endif
                                P[j].Mass *= (1.0-f_accreted);
#ifdef HYDRO_MESHLESS_FINITE_VOLUME
                                SphP[j].MassTrue *= (1.0-f_accreted);
#endif
#ifdef BH_OUTPUT_MOREINFO
                                if ((1.0-f_accreted)>0) {printf("f_accreted is: %g for particle with id %llu and mass %g around BH with id %llu\n", (MyFloat) f_accreted,(unsigned long long) P[j].ID, P[j].Mass,(unsigned long long) id);}
                                else{printf("Particle with id %llu and mass %g swallowed by BH with id %llu\n", (unsigned long long) P[j].ID, P[j].Mass,(unsigned long long) id);}
#endif
/* #if defined(NEWSINK_STOCHASTIC_ACCRETION) && defined(BH_WIND_KICK) */
/*                             }//end of else for determining if the particle is kicked */
/* #endif */
                            
#if defined(NEWSINK_STOCHASTIC_ACCRETION) //check if we actually kick this particle in the stochastic case
                            if (kicked){
#endif
#ifdef BH_WIND_KICK     /* BAL kicking operations. NOTE: we have two separate BAL wind models, particle kicking and smooth wind model. This is where we do the particle kicking BAL model. This should also work when there is alpha-disk. */
                                v_kick=All.BAL_v_outflow*1e5/All.UnitVelocity_in_cm_per_s; //if( !(All.ComovingIntegrationOn) && (All.Time < 0.001)) {v_kick *= All.Time/0.001;}
/* #ifdef SINGLE_STAR_PROTOSTELLAR_EVOLUTION */
/* 				v_kick = sqrt(All.G * bh_mass / (protostellar_radius * 6.957e10 / All.UnitLength_in_cm)); // Kepler velocity at the protostellar radius. Really we'd want v_kick = v_kep * m_accreted / m_kicked to get the right momentum */
/* #endif  */
/* #if defined(NEWSINK) && !defined(NEWSINK_STOCHASTIC_ACCRETION) /\*It is possible to accrete only part of the particle so we need to be more careful about our kicks*\/ */
/*                                 if (f_acc_corr<1.0){ */
/*                                     v_kick *= f_acc_corr*(1.0-All.BAL_f_accretion)/(1.0-All.BAL_f_accretion*f_acc_corr); /\*we wanted to only accrete an f_acc_corr portion, so the imparted momentum is proportional to only f_acc_corr*(1-All.BAL_f_accretion) times the initial mass*\/ */
/*                                 } */
/* #endif */
                                dir[0]=dir[1]=dir[2]=0; for(k=0;k<3;k++) {dir[k]=P[j].Pos[k]-pos[k];} // DAA: default direction is radially outwards
#if defined(BH_COSMIC_RAYS) /* inject cosmic rays alongside wind injection */
                                double dEcr = All.BH_CosmicRay_Injection_Efficiency * P[j].Mass * (All.BAL_f_accretion/(1.-All.BAL_f_accretion)) * (C / All.UnitVelocity_in_cm_per_s)*(C / All.UnitVelocity_in_cm_per_s);
                                SphP[j].CosmicRayEnergy+=dEcr; SphP[j].CosmicRayEnergyPred+=dEcr;
#ifdef COSMIC_RAYS_M1
                                dEcr*=COSMIC_RAYS_M1; for(k=0;k<3;k++) {SphP[j].CosmicRayFlux[k]+=dEcr*dir[k]; SphP[j].CosmicRayFluxPred[k]+=dEcr*dir[k];}
#endif
#endif
#if (BH_WIND_KICK < 0)  /* DAA: along polar axis defined by angular momentum within Kernel (we could add finite opening angle) work out the geometry w/r to the plane of the disk */
/* #if defined(NEWSINK_J_FEEDBACK) /\*Use Jsink instead of Jgas_in_Kernel for direction*\/ */
/*                                 if((dir[0]*Jsink[0] + dir[1]*Jsink[1] + dir[2]*Jsink[2]) > 0){for(k=0;k<3;k++) {dir[k]=Jsink[k];}} else {for(k=0;k<3;k++) {dir[k]=-Jsink[k];}} */
/* #else */
                                if((dir[0]*Jgas_in_Kernel[0] + dir[1]*Jgas_in_Kernel[1] + dir[2]*Jgas_in_Kernel[2]) > 0){for(k=0;k<3;k++) {dir[k]=Jgas_in_Kernel[k];}} else {for(k=0;k<3;k++) {dir[k]=-Jgas_in_Kernel[k];}}
//#endif
#endif
                                for(k=0,norm=0;k<3;k++) {norm+=dir[k]*dir[k];} if(norm<=0) {dir[0]=0;dir[1]=0;dir[2]=1;norm=1;} else {norm=sqrt(norm); dir[0]/=norm;dir[1]/=norm;dir[2]/=norm;}
                                for(k=0;k<3;k++) {P[j].Vel[k]+=v_kick*All.cf_atime*dir[k]; SphP[j].VelPred[k]+=v_kick*All.cf_atime*dir[k];}				
#ifdef GALSF_SUBGRID_WINDS // if sub-grid galactic winds are decoupled from the hydro, we decouple the BH kick winds as well
                                SphP[j].DelayTime = All.WindFreeTravelMaxTimeFactor / All.cf_hubble_a;
#endif  

#ifndef IO_REDUCED_MODE
                                //printf("BAL kick: P[j].ID %llu BH ID dir: %g %g %g, Jsink: %g %g %g reldir: %g %g %g bvect3 %g %g %g \n", (unsigned long long) P[j].ID, (unsigned long long) P[j].SwallowID, dir[0],dir[1],dir[2],Jsink[0],Jsink[1],Jsink[2], reldir[0],reldir[1],reldir[2], b_vect3[0],b_vect3[1],b_vect3[2]);
                                printf("BAL kick: All.BAL_v_outflow %g \t f_acc_corr %g \t v_kick %g\n",(All.BAL_v_outflow*1e5/All.UnitVelocity_in_cm_per_s),f_acc_corr,v_kick);
                                printf("BAL kick: P[j].ID %llu BH ID %llu Type(j) %d All.BAL_f_accretion %g M(j) %g V(j).xyz %g/%g/%g P(j).xyz %g/%g/%g p(i).xyz %g/%g/%g v_out %g \n",
                                       (unsigned long long) P[j].ID, (unsigned long long) P[j].SwallowID,P[j].Type, All.BAL_f_accretion,P[j].Mass,P[j].Vel[0],P[j].Vel[1],P[j].Vel[2],P[j].Pos[0],P[j].Pos[1],P[j].Pos[2],pos[0],pos[1],pos[2],v_kick);
#endif
#ifdef BH_OUTPUT_MOREINFO
                                fprintf(FdBhWindDetails,"%g  %u %g  %2.7f %2.7f %2.7f  %2.7f %2.7f %2.7f  %g %g %g  %u  %2.7f %2.7f %2.7f\n",
                                        All.Time, P[j].ID, P[j].Mass,  P[j].Pos[0],P[j].Pos[1],P[j].Pos[2],  P[j].Vel[0],P[j].Vel[1],P[j].Vel[2],dir[0],dir[1],dir[2], id, pos[0],pos[1],pos[2]);
#endif
#endif   // #ifdef BH_WIND_KICK
#if defined(NEWSINK_STOCHASTIC_ACCRETION) //continuation of the if (kicked) statement
                            }
                            else{N_gas_swallowed++;} //only count it s swallowed if it actually is
#else
                            N_gas_swallowed++;
#endif //defined(NEWSINK_STOCHASTIC_ACCRETION)
                        } // f_accreted>0.0
                    }  // if(P[j].Type == 0)

                    /* DAA: make sure it is not accreted (or ejected) by the same BH again if inactive in the next timestep */
                    P[j].SwallowID = 0; 
                } // if(P[j].SwallowID == id)  -- particles being entirely or partially swallowed!!!
#if defined(NEWSINK_J_FEEDBACK)
		int n;
                if( Jsinktot > 0 && P[j].Mass > 0 && P[j].Type == 0 ){ /*There is angular mom in the sink and this is gas*/
                /*Let's find if it is on the neighbor list*/
                    for(n=0;n<n_neighbor;n++){
                        if( P[j].ID == str_gasID[n] && str_f_acc[n] < 1.0 ){ /*It should be a particle we don't swallow fully*/
                            Jcrossdr[0] = -Jsink[2]*dx[1] + Jsink[1]*dx[2]; Jcrossdr[1] = Jsink[2]*dx[0] - Jsink[0]*dx[2]; Jcrossdr[2] = -Jsink[1]*dx[0] + Jsink[0]*dx[1]; // L x dx cross product
			    for(k=0; k<3; k++) {
			        dv[k] = dJsinkpred * Jcrossdr[k] / dv_ang_kick_norm; 
                                P[j].Vel[k] += dv[k];  //Eq 22 in Hubber 2013
				SphP[j].VelPred[k] += dv[k]; 
				accreted_momentum[k] -= dv[k]*P[j].Mass; // to conserve momentum
			    }
			    accreted_J[0] -= (dv[2]*dx[1] - dv[1]*dx[2])*P[j].Mass; accreted_J[1] -= (-dv[2]*dx[0] + dv[0]*dx[2])*P[j].Mass; accreted_J[2] -= (dv[1]*dx[0] - dv[0]*dx[1])*P[j].Mass;
                        }
                    }
// #ifdef BH_OUTPUT_MOREINFO
                    // printf("swallow n=%llu last Jsink[2] is: %g \n", (unsigned long long) target, (MyFloat) Jsink[2]);
                    // printf("swallow n=%llu last dv_ang_kick_norm is: %g \n", (unsigned long long) target, (MyFloat) dv_ang_kick_norm);
                    // printf("swallow n=%llu last Jsinktot is: %g \n", (unsigned long long) target, (MyFloat) Jsinktot);
                    // printf("swallow n=%llu last dJsinkpred is: %g \n", (unsigned long long) target, (MyFloat) dJsinkpred);
                    // printf("swallow n=%llu last Jcrossdr[0] is: %g \n", (unsigned long long) target, (MyFloat) Jcrossdr[0]);
                    // printf("swallow n=%llu last accreted_J[0] is: %g \n", (unsigned long long) target, (MyFloat) accreted_J[0]);
                    // printf("swallow n=%llu P[j].Vel[0] is: %g \n", (unsigned long long) target, (MyFloat) P[j].Vel[0]);
                    // printf("swallow n=%llu P[j].Vel[1] is: %g \n", (unsigned long long) target, (MyFloat) P[j].Vel[1]);
                    // printf("swallow n=%llu P[j].Vel[2] is: %g \n", (unsigned long long) target, (MyFloat) P[j].Vel[2]);
                    // printf("swallow n=%llu dv[0] is: %g \n", (unsigned long long) target, (MyFloat) dv[0]);
                    // printf("swallow n=%llu dv[1] is: %g \n", (unsigned long long) target, (MyFloat) dv[1]);
                    // printf("swallow n=%llu dv[2] is: %g \n", (unsigned long long) target, (MyFloat) dv[2]);
                    // printf("swallow n=%llu P[j].Mass is: %g \n", (unsigned long long) target, (MyFloat) P[j].Mass);
                    // printf("swallow n=%llu last sink mass is: %g \n", (unsigned long long) target, (MyFloat) mass);
// #endif
                }
#endif
                
#if defined(BH_PHOTONMOMENTUM) || defined(BH_WIND_CONTINUOUS)                
                /* now, do any other feedback "kick" operations (which used the previous loops to calculate weights) */
                if(mom>0 && mdot>0 && dt>0 && OriginallyMarkedSwallowID==0 && P[j].SwallowID==0 && P[j].Mass>0 && P[j].Type==0) // particles NOT being swallowed!
                {
                    double r=0; for(k=0;k<3;k++) {dir[k]=P[j].Pos[k]-pos[k]; r+=dir[k]*dir[k];} // should be away from BH
                    if(r>0)
                            {
                        r=sqrt(r); for(k=0;k<3;k++) {dir[k]/=r;} /* cos_theta with respect to disk of BH is given by dot product of r and Jgas */
                        for(norm=0,k=0;k<3;k++) {norm+=dir[k]*Jgas_in_Kernel[k];}
                                theta = acos(fabs(norm));
                                /* now we get the weight function based on what we calculated earlier */
                        mom_wt = bh_angleweight_localcoupling(j,BH_disk_hr,theta,r,h_i) / BH_angle_weighted_kernel_sum;
                                if(BH_angle_weighted_kernel_sum<=0) mom_wt=0;
                                
#ifdef BH_PHOTONMOMENTUM /* inject radiation pressure: add initial L/c optical/UV coupling to the gas at the dust sublimation radius */
                        double v_kick = All.BH_FluxMomentumFactor * mom_wt * mom / P[j].Mass;
                        for(k=0;k<3;k++) {P[j].Vel[k]+=v_kick*All.cf_atime*dir[k]; SphP[j].VelPred[k]+=v_kick*All.cf_atime*dir[k];}
#endif
#if defined(BH_COSMIC_RAYS) && defined(BH_WIND_CONTINUOUS) /* inject cosmic rays alongside continuous wind injection */
                        double dEcr = All.BH_CosmicRay_Injection_Efficiency * mom_wt * (C / All.UnitVelocity_in_cm_per_s)*(C / All.UnitVelocity_in_cm_per_s) * mdot*dt;
                                SphP[j].CosmicRayEnergy+=dEcr; SphP[j].CosmicRayEnergyPred+=dEcr;
#ifdef COSMIC_RAYS_M1
                                dEcr*=COSMIC_RAYS_M1; for(k=0;k<3;k++) {SphP[j].CosmicRayFlux[k]+=dEcr*dir[k]; SphP[j].CosmicRayFluxPred[k]+=dEcr*dir[k];}
#endif
#endif
#if defined(BH_WIND_CONTINUOUS) && !defined(BH_WIND_KICK) /* inject BAL winds, this is the more standard smooth feedback model */
                                double m_wind = mom_wt * (1-All.BAL_f_accretion)/(All.BAL_f_accretion) * mdot*dt; /* mass to couple */
                                if(BH_angle_weighted_kernel_sum<=0) m_wind=0;
                                
//1. check if (Vw-V0)*rhat <= 0   [ equivalently, check if   |Vw| <= V0*rhat ]
//2. if (1) is False, the wind will catch the particle, couple mass, momentum, energy, according to the equations above
//3. if (1) is True, the wind will not catch the particle, or will only asymptotically catch it. For the sake of mass conservation in the disk, I think it is easiest to treat this like the 'marginal' case where the wind barely catches the particle. In this case, add the mass normally, but no momentum, and no energy, giving:
                        //dm = m_wind, dV = 0, du = -mu*u0   [decrease the thermal energy slightly to account for adding more 'cold' material to it]
                                
                                double dvr_gas_to_bh, dr_gas_to_bh;
                                for(dvr_gas_to_bh=dr_gas_to_bh=0, k=0;k<3;k++)
                                {
                                    dvr_gas_to_bh += (velocity[k]-P[j].Vel[k]) * (pos[k]-P[j].Pos[k]);
                                    dr_gas_to_bh  += (pos[k]-P[j].Pos[k]) * (pos[k]-P[j].Pos[k]);
                                }
                                dvr_gas_to_bh /= dr_gas_to_bh ;
                                
                                /* add wind mass to particle, correcting density as needed */
                                if(P[j].Hsml<=0)
                                {
                                    if(SphP[j].Density>0){SphP[j].Density*=(1+m_wind/P[j].Mass);} else {SphP[j].Density=m_wind*hinv3;}
                                } else {
                                    SphP[j].Density += kernel_zero * m_wind/(P[j].Hsml*P[j].Hsml*P[j].Hsml);
                                }
                                P[j].Mass += m_wind;                                 
#ifdef HYDRO_MESHLESS_FINITE_VOLUME
                                SphP[j].MassTrue += m_wind;
#endif
                                /* now add wind momentum to particle */
                                if(dvr_gas_to_bh < (All.BAL_v_outflow*1e5/All.UnitVelocity_in_cm_per_s))   // gas moving away from BH at v < BAL speed
                                {
                                    double e_wind = 0;
                                    for(k=0;k<3;k++)
                                    {
                                norm = All.cf_atime*(All.BAL_v_outflow*1e5/All.UnitVelocity_in_cm_per_s)*dir[k] + velocity[k]-P[j].Vel[k]; // relative wind-particle velocity (in code units) including BH-particle motion;
                                P[j].Vel[k] += All.BlackHoleFeedbackFactor * norm * m_wind/P[j].Mass; // momentum conservation gives updated velocity
                                        SphP[j].VelPred[k] += All.BlackHoleFeedbackFactor * norm * m_wind/P[j].Mass;
                                e_wind += (norm/All.cf_atime)*(norm/All.cf_atime); // -specific- shocked wind energy
                                    }
                            e_wind *= 0.5*m_wind/P[j].Mass; // make total wind energy, add to particle as specific energy of -particle-
                            SphP[j].InternalEnergy += e_wind; SphP[j].InternalEnergyPred += e_wind;
                        } else {    // gas moving away from BH at wind speed (or faster) already.
                                    if(SphP[j].InternalEnergy * ( P[j].Mass - m_wind ) / P[j].Mass > 0)
                                        SphP[j].InternalEnergy = SphP[j].InternalEnergy * ( P[j].Mass - m_wind ) / P[j].Mass;
                                }
#endif // if defined(BH_WIND_CONTINUOUS) && !defined(BH_WIND_KICK)
                            } // norm > 0
                } // (check if valid gas neighbor of interest)
#endif // defined(BH_PHOTONMOMENTUM) || defined(BH_WIND_CONTINUOUS)                
                

            } // for(n = 0; n < numngb; n++)
        } // while(startnode >= 0)
        
        if(mode == 1)
        {
            listindex++;
            if(listindex < NODELISTLENGTH)
            {
                startnode = BlackholeDataGet[target].NodeList[listindex];
                if(startnode >= 0) {startnode = Nodes[startnode].u.d.nextnode;}	/* open it */
            }
        }
    } // while(startnode >= 0)
    
    /* Now collect the result at the right place */
    if(mode == 0)
    {
        BlackholeTempInfo[mod_index].accreted_Mass = accreted_mass;
        BlackholeTempInfo[mod_index].accreted_BH_Mass = accreted_BH_mass;
#ifdef BH_ALPHADISK_ACCRETION
        // DAA: could be better to include this in BlackholeTempInfo and update BH_Mass_AlphaDisk only at the end (like Mass and BH_Mass)
        BPP(target).BH_Mass_AlphaDisk += accreted_BH_mass_alphadisk;
#endif
        for(k = 0; k < 3; k++) {
            BlackholeTempInfo[mod_index].accreted_momentum[k] = accreted_momentum[k];
#if defined(SINGLE_STAR_STRICT_ACCRETION) || defined(NEWSINK)
	    BlackholeTempInfo[mod_index].accreted_moment[k] = accreted_moment[k];
#endif
#if defined(NEWSINK_J_FEEDBACK)
            BlackholeTempInfo[mod_index].accreted_J[k] = accreted_J[k];
#endif	    
        }
#ifdef BH_COUNTPROGS
        BPP(target).BH_CountProgs += accreted_BH_progs;
#endif
#ifdef GALSF
        if(P[target].StellarAge > accreted_age) {P[target].StellarAge = accreted_age;}
#endif
    }
    else
    {
        BlackholeDataResult[target].Mass = accreted_mass;
        BlackholeDataResult[target].BH_Mass = accreted_BH_mass;
#ifdef BH_ALPHADISK_ACCRETION
        BlackholeDataResult[target].BH_Mass_AlphaDisk = accreted_BH_mass_alphadisk;
#endif
        for(k = 0; k < 3; k++) {
            BlackholeDataResult[target].accreted_momentum[k] = accreted_momentum[k];
#if defined(SINGLE_STAR_STRICT_ACCRETION) || defined(NEWSINK)
            BlackholeDataResult[target].accreted_moment[k] = accreted_moment[k];	    
#endif	    
#if defined(NEWSINK_J_FEEDBACK)
            BlackholeDataResult[target].accreted_J[k] = accreted_J[k];
#endif
        }
#ifdef BH_COUNTPROGS
        BlackholeDataResult[target].BH_CountProgs = accreted_BH_progs;
#endif
#ifdef GALSF
        BlackholeDataResult[target].Accreted_Age = accreted_age;
#endif
    }
    
    return 0;
} /* closes bh_evaluate_swallow */



#ifdef BH_WIND_SPAWN
void spawn_bh_wind_feedback(void)
{
    int i, n_particles_split = 0, MPI_n_particles_split, dummy_gas_tag=0;
    for(i = 0; i < NumPart; i++)
        if(P[i].Type==0)
        {
            dummy_gas_tag=i;
            break;
        }
    
    /* don't loop or go forward if there are no gas particles in the domain, or the code will crash */
    for(i = FirstActiveParticle; i >= 0; i = NextActiveParticle[i])
    {
        //long nmax = (int)(0.9*All.MaxPart); if(All.MaxPart-1000 < nmax) nmax=All.MaxPart-1000; /* stricter criterion for allowing spawns, more relaxed below */
        //if((NumPart+n_particles_split+(int)(2.*(BH_WIND_SPAWN+0.1)) < nmax) && (n_particles_split<1) && (P[i].Type==5))

        long nmax = (int)(0.99*All.MaxPart); if(All.MaxPart-20 < nmax) nmax=All.MaxPart-20;
        if((NumPart+n_particles_split+(int)(2.*(BH_WIND_SPAWN+0.1)) < nmax) && (P[i].Type==5))
        {
            if(BPP(i).unspawned_wind_mass >= (BH_WIND_SPAWN)*All.BAL_wind_particle_mass)
            {
                int j; dummy_gas_tag=-1; double r2=MAX_REAL_NUMBER;
                for(j=0; j<N_gas; j++) /* find the closest gas particle on the domain to act as the dummy */
                {
                    if(P[j].Type==0)
                    {
                        double dx2=(P[j].Pos[0]-P[i].Pos[0])*(P[j].Pos[0]-P[i].Pos[0]) + (P[j].Pos[1]-P[i].Pos[1])*(P[j].Pos[1]-P[i].Pos[1]) + (P[j].Pos[2]-P[i].Pos[2])*(P[j].Pos[2]-P[i].Pos[2]);
                        if(dx2 < r2) {r2=dx2; dummy_gas_tag=j;}
                    }
                }
                if(dummy_gas_tag >= 0)
                {
                    n_particles_split += blackhole_spawn_particle_wind_shell( i , dummy_gas_tag, n_particles_split);
                }
            }
        }
    }
    MPI_Allreduce(&n_particles_split, &MPI_n_particles_split, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
    if(MPI_n_particles_split>0){TreeReconstructFlag = 1;}
    if(ThisTask == 0) {printf("Particle BH spawn check: %d particles spawned \n", MPI_n_particles_split);}

    /* rearrange_particle_sequence -must- be called immediately after this routine! */
    All.TotNumPart += (long long)MPI_n_particles_split;
    All.TotN_gas   += (long long)MPI_n_particles_split;
    Gas_split       = n_particles_split;                    // specific to the local processor //
    
    //rearrange_particle_sequence();
}




/*! this code copies what was used in merge_split.c for the gas particle split case */
int blackhole_spawn_particle_wind_shell( int i, int dummy_sph_i_to_clone, int num_already_spawned )
{
    double total_mass_in_winds = BPP(i).unspawned_wind_mass;
    int n_particles_split   = floor( total_mass_in_winds / All.BAL_wind_particle_mass );
    if( (n_particles_split == 0) || (n_particles_split < 1) ) {return 0;}
    int n0max = DMAX(20 , (int)(3.*(BH_WIND_SPAWN)+0.1));
    if(n_particles_split > n0max) {n_particles_split = n0max;}

    /* here is where the details of the split are coded, the rest is bookkeeping */
    //double mass_of_new_particle = total_mass_in_winds / n_particles_split; /* don't do this, as can produce particles with extremely large masses; instead wait to spawn */
    double mass_of_new_particle = All.BAL_wind_particle_mass;
#ifndef IO_REDUCED_MODE
    printf("Task %d wants to create %g mass in wind with %d new particles each of mass %g \n", ThisTask,total_mass_in_winds, n_particles_split, mass_of_new_particle);
    printf(" splitting BH %d using SphP particle %d\n", i, dummy_sph_i_to_clone);
#endif
    int k=0; long j;
    if(NumPart + num_already_spawned + n_particles_split >= All.MaxPart)
    {
        printf ("On Task=%d with NumPart=%d (+N_spawned=%d) we tried to split a particle, but there is no space left...(All.MaxPart=%d). Try using more nodes, or raising PartAllocFac, or changing the split conditions to avoid this.\n", ThisTask, NumPart, num_already_spawned, All.MaxPart);
        fflush(stdout); endrun(8888);
    }
    
#ifndef WAKEUP
    double dt = (P[i].TimeBin ? (((integertime) 1) << P[i].TimeBin) : 0) * All.Timebase_interval / All.cf_hubble_a;
#else
    double dt = P[i].dt_step * All.Timebase_interval / All.cf_hubble_a;
#endif
    double d_r = 0.25 * KERNEL_CORE_SIZE*PPP[i].Hsml; // needs to be epsilon*Hsml where epsilon<<1, to maintain stability //
    double r2=0; for(k=0;k<3;k++) {r2+=(P[dummy_sph_i_to_clone].Pos[k]-P[i].Pos[k])*(P[dummy_sph_i_to_clone].Pos[k]-P[i].Pos[k]);}
    d_r = DMIN(d_r, 0.5*sqrt(r2));
#ifndef SELFGRAVITY_OFF
    d_r = DMAX(d_r , 2.0*EPSILON_FOR_TREERND_SUBNODE_SPLITTING * All.ForceSoftening[0]);
#endif
#ifdef BH_DEBUG_SPAWN_JET_TEST
    d_r = DMIN(d_r , 0.01); /* PFH: need to write this in a way that does not make assumptions about units/problem structure */
#endif
    long bin, bin_0; for(bin = 0; bin < TIMEBINS; bin++) {if(TimeBinCount[bin] > 0) break;} /* gives minimum active timebin of any particle */
    bin_0 = bin; int i0 = i; /* save minimum timebin, also save ID of BH particle for use below */    

#ifdef BH_DEBUG_SPAWN_JET_TEST
    bin = bin_0; i0 = dummy_sph_i_to_clone; /* make this particle active on the minimum timestep, and order with respect to the cloned particle */
#elif defined(SINGLE_STAR_FB_JETS)
    bin = P[i0].TimeBin; i0 = i; /* make this particle active on the star timestep or the lowest timestep of any gas neighbor */
#else
    bin = P[i0].TimeBin; i0 = i; /* make this particle active on the BH timestep */
#endif
#if defined(SINGLE_STAR_FB_JETS)
        MyFloat b_vect1[3],b_vect2[3],b_vect3[3],reldir[3];//vectors
        //vectors and shorthands for particle velocity direction relative to sink axis in jet
        double jet_theta, theta0=SINGLE_STAR_FB_JETS_THETA0,thetamax=SINGLE_STAR_FB_JETS_MAX_OPENING_ANGLE/180.0*M_PI;
        double sqrttheta0sqplusone=sqrt(1.0+theta0*theta0);
        double Jsinktot=sqrt(P[i].Jsink[0]*P[i].Jsink[0] + P[i].Jsink[1]*P[i].Jsink[1] + P[i].Jsink[2]*P[i].Jsink[2]);
        //Set up base vectors of the coordinate system for the jet, the z axis (b_vect3) is set to be along Jsink
        if (Jsinktot>0){b_vect3[0]= P[i].Jsink[0]/Jsinktot;b_vect3[1]= P[i].Jsink[1]/Jsinktot;b_vect3[2]= P[i].Jsink[2]/Jsinktot;}
        else{b_vect3[0]= 0;b_vect3[1]= 0;b_vect3[2]= 1.0;}//if the sink has no angular momentum, launch in the z direction (arbitrary but should not matter much)
        if(P[i].Jsink[1]*P[i].Jsink[2]>0){//check that Jsink is not the x unit vector
            b_vect1[0] = 0.0; b_vect1[1] = b_vect3[2]; b_vect1[2] = - b_vect3[1]; //We get the first base vector by taking cross product of Jsink with +x unit vector */
        }else{
            b_vect1[0]=0.0;b_vect1[1]=1.0;b_vect1[2]=0.0; //If Jsink is parallel to x, we just take y as the other bas vector
        }
        //second vector is b_vect3 cross b_vect1, and it should be normalized by default as it is the cross product of two orthogonal unit vectors
        b_vect2[0] = b_vect3[1] * b_vect1[2] - b_vect3[2] * b_vect1[1]; 
        b_vect2[1] = b_vect3[0] * b_vect1[2] - b_vect3[2] * b_vect1[0]; 
        b_vect2[2] = b_vect3[0] * b_vect1[1] - b_vect3[1] * b_vect1[0];
#endif
    /* create the  new particles to be added to the end of the particle list :
        i is the BH particle tag, j is the new "spawed" particle's location, dummy_sph_i_to_clone is a dummy SPH particle's tag to be used to init the wind particle */
    for(j = NumPart + num_already_spawned; j < NumPart + num_already_spawned + n_particles_split; j++)
    {   /* first, clone the 'dummy' particle so various fields are set appropriately */
        P[j] = P[dummy_sph_i_to_clone]; SphP[j] = SphP[dummy_sph_i_to_clone]; /* set the pointers equal to one another -- all quantities get copied, we only have to modify what needs changing */

        /* now we need to make sure everything is correctly placed in timebins for the tree */
        P[j].TimeBin = bin; P[j].dt_step = bin ? (((integertime) 1) << bin) : 0; // put this particle into the appropriate timebin
        NextActiveParticle[j] = FirstActiveParticle; FirstActiveParticle = j; NumForceUpdate++;
        TimeBinCount[bin]++; TimeBinCountSph[bin]++; PrevInTimeBin[j] = i0; /* likewise add it to the counters that register how many particles are in each timebin */
#ifndef BH_DEBUG_SPAWN_JET_TEST
        NextInTimeBin[j] = NextInTimeBin[i0]; if(NextInTimeBin[i0] >= 0) {PrevInTimeBin[NextInTimeBin[i0]] = j;}
        NextInTimeBin[i0] = j; if(LastInTimeBin[bin] == i0) {LastInTimeBin[bin] = j;}
#else
        if(FirstInTimeBin[bin] < 0) {FirstInTimeBin[bin]=j; LastInTimeBin[bin]=j; NextInTimeBin[j]=-1; PrevInTimeBin[j]=-1;} /* only particle in this time bin on this task */
            else {NextInTimeBin[j]=FirstInTimeBin[bin]; PrevInTimeBin[j]=-1; PrevInTimeBin[FirstInTimeBin[bin]]=j; FirstInTimeBin[bin]=j;} /* there is already at least one particle; add this one "to the front" of the list */
#endif
        P[j].Ti_begstep = All.Ti_Current; P[j].Ti_current = All.Ti_Current;
#ifdef WAKEUP
        PPPZ[j].wakeup = 1;
#endif
        /* this is a giant pile of variables to zero out. dont need everything here because we cloned a valid particle, but handy anyways */
        P[j].Particle_DivVel = 0; SphP[j].DtInternalEnergy = 0; for(k=0;k<3;k++) {SphP[j].HydroAccel[k] = 0; P[j].GravAccel[k] = 0;}
        P[j].NumNgb=All.DesNumNgb;
#ifdef PMGRID
        for(k=0;k<3;k++) {P[j].GravPM[k] = 0;}
#endif
#ifdef ENERGY_ENTROPY_SWITCH_IS_ACTIVE
        SphP[j].MaxKineticEnergyNgb = 0;
#endif
#ifdef HYDRO_MESHLESS_FINITE_VOLUME
        SphP[j].dMass = 0; SphP[j].DtMass = 0; SphP[j].MassTrue = P[j].Mass; for(k=0;k<3;k++) {SphP[j].GravWorkTerm[k] = 0;}
#endif
#if defined(ADAPTIVE_GRAVSOFT_FORGAS) || defined(ADAPTIVE_GRAVSOFT_FORALL)
        PPPZ[j].AGS_zeta = 0;
#ifdef ADAPTIVE_GRAVSOFT_FORALL
        PPP[j].AGS_Hsml = PPP[j].Hsml;
#endif
#endif
#ifdef CONDUCTION
        SphP[j].Kappa_Conduction = 0;
#endif
#ifdef MHD_NON_IDEAL
        SphP[j].Eta_MHD_OhmicResistivity_Coeff = 0; SphP[j].Eta_MHD_HallEffect_Coeff = 0; SphP[j].Eta_MHD_AmbiPolarDiffusion_Coeff = 0;
#endif
#ifdef VISCOSITY
        SphP[j].Eta_ShearViscosity = 0; SphP[j].Zeta_BulkViscosity = 0;
#endif
#ifdef TURB_DIFFUSION
        SphP[j].TD_DiffCoeff = 0;
#endif
#if defined(GALSF_SUBGRID_WINDS)
#if (GALSF_SUBGRID_WIND_SCALING==1)
        SphP[j].HostHaloMass = 0;
#endif
#endif
#ifdef GALSF_FB_FIRE_RT_HIIHEATING
        SphP[j].DelayTimeHII = 0;
#endif
#ifdef GALSF_FB_TURNOFF_COOLING
        SphP[j].DelayTimeCoolingSNe = 0;
#endif
#ifdef GALSF
        SphP[j].Sfr = 0;
#endif
#ifdef SPHAV_CD10_VISCOSITY_SWITCH
        SphP[j].alpha = 0.0;
#endif
#if defined(BH_THERMALFEEDBACK)
        SphP[j].Injected_BH_Energy = 0;
#endif
#ifdef RADTRANSFER
        for(k=0;k<N_RT_FREQ_BINS;k++)
        {
            SphP[j].E_gamma[k] = 0;
#if defined(RT_EVOLVE_NGAMMA)
            SphP[j].E_gamma_Pred[k] = 0; SphP[j].Dt_E_gamma[k] = 0;
#endif
        }
#endif
        /* note, if you want to use this routine to inject magnetic flux or cosmic rays, do this below */
#ifdef MAGNETIC
        SphP[j].divB = 0; for(k=0;k<3;k++) {SphP[j].B[k]*=1.e-10; SphP[j].BPred[k]*=1.e-10; SphP[j].DtB[k]=0;} /* add magnetic flux here if desired */
#ifdef DIVBCLEANING_DEDNER
        SphP[j].DtPhi = SphP[j].PhiPred = SphP[j].Phi = 0;
#endif
#endif
#ifdef COSMIC_RAYS
        SphP[j].CosmicRayEnergyPred = SphP[j].CosmicRayEnergy = 0; SphP[j].DtCosmicRayEnergy = 0; /* add CR energy here if desired */
#endif
        
        /* now set the real hydro variables. */
        /* set the particle ID */ // unsigned int bits; int SPLIT_GENERATIONS = 4; for(bits = 0; SPLIT_GENERATIONS > (1 << bits); bits++); /* the particle needs an ID: we give it a bit-flip from the original particle to signify the split */
        P[j].ID = All.AGNWindID; /* update:  We are using a fixed wind ID, to allow for trivial wind particle identification */
        P[j].ID_child_number = P[i].ID_child_number; P[i].ID_child_number +=1; P[j].ID_generation = P[i].ID; // this allows us to track spawned particles by giving them unique sub-IDs
        P[j].Mass = mass_of_new_particle; /* assign masses to both particles (so they sum correctly) */
#ifdef HYDRO_MESHLESS_FINITE_VOLUME
        SphP[j].MassTrue = P[j].Mass;
#endif
#ifndef BH_DEBUG_FIX_MASS
        P[i].Mass -= P[j].Mass; /* make sure the operation is mass conserving! */
#endif
        BPP(i).unspawned_wind_mass -= P[j].Mass; /* remove the mass successfully spawned, to update the remaining unspawned mass */
        /* positions */
        double phi = 2.0*M_PI*get_random_number(j+1+ThisTask); // random from 0 to 2pi //
        double cos_theta = 2.0*(get_random_number(j+3+2*ThisTask)-0.5); // random between 1 to -1 //
        double sin_theta=sqrt(1-cos_theta*cos_theta), dx[3]; dx[0]=sin_theta*cos(phi); dx[1]=sin_theta*sin(phi); dx[2]=cos_theta;
        for(k=0;k<3;k++) {P[j].Pos[k]=P[i].Pos[k] + dx[k]*d_r;}
        /* velocities (determined by wind velocity) */
        double dxv[3]; dxv[0]=dx[0]; dxv[1]=dx[1]; dxv[2]=dx[2]; // default to velocity pointed radially away from BH
#if defined(BH_DEBUG_SPAWN_JET_TEST)
        double ct_v=1.-0.00015*(1.-fabs(cos_theta)), st_v=sqrt(1-ct_v*ct_v), vfac=1+0.2*(get_random_number(j+99+3*ThisTask)-0.5); if(cos_theta<0) {ct_v*=-1;}
        dxv[0]=st_v*cos(phi)*vfac; dxv[1]=st_v*sin(phi)*vfac; dxv[2]=ct_v*vfac; // velocities into narrow opening angle in +- z direction, fixed
#elif defined(SINGLE_STAR_FB_JETS)
        //Find the angle of the spawned particle's velocity using the angular distribution of Matzner & McKee 1999 (same as in Eq 20 of Cunningham et al 2011)
        double z=get_random_number(j+7+5*ThisTask); //to use in inverse transform sampling
        if (SINGLE_STAR_FB_JETS_MAX_OPENING_ANGLE<90.0){
            jet_theta=atan(theta0*tan(z*atan(sqrttheta0sqplusone*tan(thetamax)/theta0))/sqrttheta0sqplusone);}
        else{
            jet_theta=atan(theta0*tan(z*M_PI/2.0)/sqrttheta0sqplusone);}//use 90 degree value
        //Determine whether the particle is closer to the north pole or the south pole
        if( (dx[0]*b_vect3[0]+dx[1]*b_vect3[1]+dx[2]*b_vect3[2])<0){jet_theta=M_PI-jet_theta;}
        //Choose the direction of the jet unifromly relative to Jsink (b_vect3) within SINGLE_STAR_FB_JETS_OPENING_ANGLE
        double jet_phi = 2.0*M_PI*get_random_number(j+5+3*ThisTask); // random from 0 to 2pi //
        reldir[0]=sin(jet_theta)*cos(jet_phi); reldir[1]=sin(jet_theta)*sin(jet_phi); reldir[2]=cos(jet_theta);//relative direction of velocity compared to Jsink
        for(k=0;k<3;k++) {dxv[k]=reldir[0]*b_vect1[k]+reldir[1]*b_vect2[k]+reldir[2]*b_vect3[k];} //transforming back to original coordinate system
        //printf("Jet theta %g jet phi %g z %g reldir %g %g %g dxv %g %g %g b_vect1 %g %g %g b_vect3 %g %g %g b_vect3 %g %g %g\n",jet_theta,jet_phi,z,reldir[0],reldir[1],reldir[2],dxv[0],dxv[1],dxv[2],b_vect1[0],b_vect1[1],b_vect1[2],b_vect2[0],b_vect2[1],b_vect2[2],b_vect3[0],b_vect3[1],b_vect3[2]);
#endif
    double v_magnitude; // velocity of the jet
#ifdef SINGLE_STAR_FB_JETS
#ifdef SINGLE_STAR_PROTOSTELLAR_EVOLUTION
    v_magnitude = sqrt(All.G * P[i].BH_Mass / (P[i].ProtoStellar_Radius * 6.957e10 / All.UnitLength_in_cm)) * All.cf_atime; // Kepler velocity at the protostellar radius. Really we'd want v_kick = v_kep * m_accreted / m_kicked to get the right momentum
#ifdef BH_OUTPUT_MOREINFO
    printf("Launching a jet from protostar of mass %g and radius %g R_solar at velocity %g\n", P[i].BH_Mass, P[i].ProtoStellar_Radius, v_magnitude);
#endif	// BH_OUTPUT_MOREINFO	
#else
    v_magnitude = sqrt(All.G * P[i].BH_Mass / (10 * 6.957e10 / All.UnitLength_in_cm)) * All.cf_atime; // assume fiducial protostellar radius of 10, as in Federrath 2014
#ifdef BH_OUTPUT_MOREINFO
    printf("Launching a jet from protostar of mass %g at velocity %g\n", P[i].BH_Mass, v_magnitude);
#endif	// BH_OUTPUT_MOREINFO	
#endif // SINGLE_STAR_PROTOSTELLAR_EVOLUTION
#else
    v_magnitude = (All.BAL_v_outflow*1e5 / All.UnitVelocity_in_cm_per_s)*All.cf_atime;
#endif // SINGLE_STAR_FB_JETS
        for(k=0;k<3;k++) {P[j].Vel[k]=P[i].Vel[k] + dxv[k]*v_magnitude; SphP[j].VelPred[k]=P[j].Vel[k];}
        
        /* condition number, smoothing length, and density */
        SphP[j].ConditionNumber *= 100.0; /* boost the condition number to be conservative, so we don't trigger madness in the kernel */
        //SphP[j].Density *= 1e-10; SphP[j].Pressure *= 1e-10; PPP[j].Hsml = All.SofteningTable[0];  /* set dummy values: will be re-generated anyways [actually better to use nearest-neighbor values to start] */
#ifdef BH_DEBUG_SPAWN_JET_TEST
        PPP[j].Hsml=5.*d_r; SphP[j].Density=mass_of_new_particle/pow(KERNEL_CORE_SIZE*PPP[j].Hsml,NUMDIMS); /* PFH: need to write this in a way that does not make assumptions about units/problem structure */
#endif
        /* internal energy, determined by desired wind temperature */
        SphP[j].InternalEnergy = All.BAL_internal_temperature / (  PROTONMASS / BOLTZMANN * GAMMA_MINUS1 * All.UnitEnergy_in_cgs / All.UnitMass_in_g  ); SphP[j].InternalEnergyPred = SphP[j].InternalEnergy;

#if defined(BH_COSMIC_RAYS) /* inject cosmic rays alongside wind injection */
        double dEcr = All.BH_CosmicRay_Injection_Efficiency * P[j].Mass * (All.BAL_f_accretion/(1.-All.BAL_f_accretion)) * (C / All.UnitVelocity_in_cm_per_s)*(C / All.UnitVelocity_in_cm_per_s);
        SphP[j].CosmicRayEnergy=dEcr; SphP[j].CosmicRayEnergyPred=dEcr;
#ifdef COSMIC_RAYS_M1
        dEcr*=COSMIC_RAYS_M1; for(k=0;k<3;k++) {SphP[j].CosmicRayFlux[k]=dEcr*dx[k]; SphP[j].CosmicRayFluxPred[k]=SphP[j].CosmicRayFlux[k];}
#endif
#endif
        /* Note: New tree construction can be avoided because of  `force_add_star_to_tree()' */
        force_add_star_to_tree(i0, j);// (buggy) /* we solve this by only calling the merge/split algorithm when we're doing the new domain decomposition */
    }    
    if(BPP(i).unspawned_wind_mass < 0) {BPP(i).unspawned_wind_mass=0;}
    return n_particles_split;
}
#endif


