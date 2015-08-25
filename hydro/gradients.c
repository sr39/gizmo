#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <gsl/gsl_math.h>
#include "../allvars.h"
#include "../proto.h"
#include "../kernel.h"
#ifdef OMP_NUM_THREADS
#include <pthread.h>
#endif



/*! \file gradients.c
 *  \brief calculate gradients of hydro quantities
 *
 *  This file contains the "second hydro loop", where the gas hydro quantity
 *   gradients are calculated. All gradients now use the second-order accurate
 *   moving-least-squares formulation, and are calculated here consistently.
 */
/*
 * This file was written by Phil Hopkins (phopkins@caltech.edu) for GIZMO.
 */


#define ASSIGN_ADD_PRESET(x,y,mode) (x+=y)
#define MINMAX_CHECK(x,xmin,xmax) ((x<xmin)?(xmin=x):((x>xmax)?(xmax=x):(1)))
#define SHOULD_I_USE_SPH_GRADIENTS(condition_number) ((condition_number > CONDITION_NUMBER_DANGER) ? (1):(0))



#if defined(CONSTRAINED_GRADIENT_MHD)
#if (CONSTRAINED_GRADIENT_MHD > 1)
#define NUMBER_OF_GRADIENT_ITERATIONS 3
#else
#define NUMBER_OF_GRADIENT_ITERATIONS 2
#endif
#else
#define NUMBER_OF_GRADIENT_ITERATIONS 1
#endif

#if defined(RT_EVOLVE_EDDINGTON_TENSOR) && !defined(RT_EVOLVE_NGAMMA)
#define E_gamma_Pred E_gamma
#endif


#ifdef OMP_NUM_THREADS
extern pthread_mutex_t mutex_nexport;
extern pthread_mutex_t mutex_partnodedrift;
#define LOCK_NEXPORT     pthread_mutex_lock(&mutex_nexport);
#define UNLOCK_NEXPORT   pthread_mutex_unlock(&mutex_nexport);
#else
#define LOCK_NEXPORT
#define UNLOCK_NEXPORT
#endif

#define NV_MYSIGN(x) (( x > 0 ) - ( x < 0 ))

/* define a common 'gradients' structure to hold
 everything we're going to take derivatives of */
struct Quantities_for_Gradients
{
    MyDouble Density;
    MyDouble Pressure;
    MyDouble Velocity[3];
#ifdef MAGNETIC
    MyDouble B[3];
#ifdef DIVBCLEANING_DEDNER
    MyDouble Phi;
#endif
#endif
#ifdef TURB_DIFF_METALS
    MyDouble Metallicity[NUM_METAL_SPECIES];
#endif
#ifdef RT_EVOLVE_EDDINGTON_TENSOR
    MyFloat E_gamma[N_RT_FREQ_BINS];
    MyFloat E_gamma_ET[N_RT_FREQ_BINS][6];
#endif
#ifdef DOGRAD_INTERNAL_ENERGY
    MyDouble InternalEnergy;
#endif
#ifdef COSMIC_RAYS
    MyDouble CosmicRayPressure;
#endif
#ifdef DOGRAD_SOUNDSPEED
    MyDouble SoundSpeed;
#endif
};

struct kernel_GasGrad
{
    double dp[3],r,wk_i, wk_j, dwk_i, dwk_j,h_i;
};

struct GasGraddata_in
{
    MyDouble Pos[3];
    MyFloat Mass;
    MyFloat Hsml;
    int Timestep;
#ifdef CONSTRAINED_GRADIENT_MHD
    MyFloat NV_T[3][3];
    MyFloat BGrad[3][3];
#ifdef CONSTRAINED_GRADIENT_MHD_FAC_MEDDEV
    MyFloat PhiGrad[3];
#endif
#endif
#ifndef DONOTUSENODELIST
    int NodeList[NODELISTLENGTH];
#endif
#ifdef SPHAV_CD10_VISCOSITY_SWITCH
    MyFloat NV_DivVel;
#endif
    struct Quantities_for_Gradients GQuant;
}
*GasGradDataIn, *GasGradDataGet;



struct GasGraddata_out
{
#ifdef HYDRO_SPH
    MyFloat alpha_limiter;
#ifdef MAGNETIC
#ifdef DIVBCLEANING_DEDNER
    MyFloat divB;
#endif
    MyFloat DtB[3];
#endif
#endif
#ifdef CONSTRAINED_GRADIENT_MHD
    MyFloat Face_Area[3];
    MyFloat FaceDotB;
    MyFloat FaceCrossX[3][3];
#endif
    struct Quantities_for_Gradients Gradients[3];
    struct Quantities_for_Gradients Maxima;
    struct Quantities_for_Gradients Minima;
    MyFloat MaxDistance;
}
*GasGradDataResult, *GasGradDataOut;



struct GasGraddata_out_iter
{
#ifdef CONSTRAINED_GRADIENT_MHD
    MyFloat FaceDotB;
#ifdef CONSTRAINED_GRADIENT_MHD_MIDPOINT
    MyDouble PhiGrad[3];
#endif
#endif
}
*GasGradDataResult_iter, *GasGradDataOut_iter;



/* this is a temporary structure for quantities used ONLY in the loop below,
 for example for computing the slope-limiters (for the Reimann problem) */
static struct temporary_data_topass
{
    struct Quantities_for_Gradients Maxima;
    struct Quantities_for_Gradients Minima;
    MyFloat MaxDistance;
#ifdef CONSTRAINED_GRADIENT_MHD
    MyDouble FaceDotB;
    MyDouble FaceCrossX[3][3];
    MyDouble BGrad[3][3];
#ifdef CONSTRAINED_GRADIENT_MHD_MIDPOINT
    MyDouble PhiGrad[3];
#endif
#endif
#ifdef RT_EVOLVE_EDDINGTON_TENSOR
    MyFloat Gradients_E_gamma[N_RT_FREQ_BINS][3];
#endif
}
*GasGradDataPasser;



static inline void particle2in_GasGrad(struct GasGraddata_in *in, int i, int gradient_iteration);
static inline void out2particle_GasGrad(struct GasGraddata_out *out, int i, int mode, int gradient_iteration);
static inline void out2particle_GasGrad_iter(struct GasGraddata_out_iter *out, int i, int mode, int gradient_iteration);



static inline void particle2in_GasGrad(struct GasGraddata_in *in, int i, int gradient_iteration)
{
    int k;
    for(k = 0; k < 3; k++)
        in->Pos[k] = P[i].Pos[k];
    in->Hsml = PPP[i].Hsml;
    in->Mass = P[i].Mass;
    if(in->Mass < 0) {in->Mass = 0;}
    if(SHOULD_I_USE_SPH_GRADIENTS(SphP[i].ConditionNumber)) {in->Mass *= -1;}
    in->Timestep = (P[i].TimeBin ? (((integertime) 1) << P[i].TimeBin) : 0);
#ifdef CONSTRAINED_GRADIENT_MHD
    if(gradient_iteration > 0)
        if(SphP[i].FlagForConstrainedGradients <= 0)
            in->Mass = 0;
    int j;
    for(j=0;j<3;j++)
    {
        for(k=0;k<3;k++)
        {
            in->BGrad[j][k] = SphP[i].Gradients.B[j][k];
            in->NV_T[j][k] = SphP[i].NV_T[j][k];
        }
#ifdef CONSTRAINED_GRADIENT_MHD_MIDPOINT
        in->PhiGrad[j] = SphP[i].Gradients.Phi[j];
#endif
    }
    if(gradient_iteration > 0)
    {
        for(k = 0; k < 3; k++) {in->GQuant.B[k] = Get_Particle_BField(i,k);}
        in->GQuant.Density = SphP[i].Density;
#ifdef CONSTRAINED_GRADIENT_MHD_MIDPOINT
        in->GQuant.Phi = Get_Particle_PhiField(i);
#endif
    }
#endif
    
    if(gradient_iteration == 0)
    {
        in->GQuant.Density = SphP[i].Density;
        in->GQuant.Pressure = SphP[i].Pressure;
        for(k = 0; k < 3; k++)
            in->GQuant.Velocity[k] = SphP[i].VelPred[k];
#ifdef MAGNETIC
        for(k = 0; k < 3; k++)
            in->GQuant.B[k] = Get_Particle_BField(i,k);
#ifdef DIVBCLEANING_DEDNER
        in->GQuant.Phi = Get_Particle_PhiField(i);
#endif
#endif
#ifdef TURB_DIFF_METALS
        for(k = 0; k < NUM_METAL_SPECIES; k++)
            in->GQuant.Metallicity[k] = P[i].Metallicity[k];
#endif
#ifdef RT_EVOLVE_EDDINGTON_TENSOR
        for(k = 0; k < N_RT_FREQ_BINS; k++) 
        {
        	in->GQuant.E_gamma[k] = SphP[i].E_gamma_Pred[k];
        	int k_et; for(k_et = 0; k_et < 6; k_et++) {in->GQuant.E_gamma_ET[k][k_et] = SphP[i].ET[k][k_et];}
        }
#endif
#ifdef DOGRAD_INTERNAL_ENERGY
        in->GQuant.InternalEnergy = SphP[i].InternalEnergyPred;
#endif
#ifdef COSMIC_RAYS
        in->GQuant.CosmicRayPressure = Get_Particle_CosmicRayPressure(i);
#endif
#ifdef DOGRAD_SOUNDSPEED
        in->GQuant.SoundSpeed = Particle_effective_soundspeed_i(i);
#endif
#ifdef SPHAV_CD10_VISCOSITY_SWITCH
       in->NV_DivVel = SphP[i].NV_DivVel;
#endif
    } // gradient_iteration == 0
}



//#define MAX_ADD(x,y,mode) (mode == 0 ? (x=y) : (((x)<(y)) ? (x=y) : (x))) // these definitions applied before the symmetric re-formulation of this routine
//#define MIN_ADD(x,y,mode) (mode == 0 ? (x=y) : (((x)>(y)) ? (x=y) : (x)))
#define MAX_ADD(x,y,mode) ((y > x) ? (x = y) : (1)) // simpler definition now used
#define MIN_ADD(x,y,mode) ((y < x) ? (x = y) : (1))


static inline void out2particle_GasGrad_iter(struct GasGraddata_out_iter *out, int i, int mode, int gradient_iteration)
{
#ifdef CONSTRAINED_GRADIENT_MHD
    {
        ASSIGN_ADD_PRESET(GasGradDataPasser[i].FaceDotB,out->FaceDotB,mode);
#ifdef CONSTRAINED_GRADIENT_MHD_MIDPOINT
        int k;
        for(k=0;k<3;k++) {ASSIGN_ADD_PRESET(GasGradDataPasser[i].PhiGrad[k],out->PhiGrad[k],mode);}
#endif
    }
#endif
}



static inline void out2particle_GasGrad(struct GasGraddata_out *out, int i, int mode, int gradient_iteration)
{
#ifdef CONSTRAINED_GRADIENT_MHD
    {
        ASSIGN_ADD_PRESET(GasGradDataPasser[i].FaceDotB,out->FaceDotB,mode);
#ifdef CONSTRAINED_GRADIENT_MHD_MIDPOINT
        int k;
        for(k=0;k<3;k++) {ASSIGN_ADD_PRESET(GasGradDataPasser[i].PhiGrad[k],out->Gradients[k].Phi,mode);}
#endif
    }
#endif
    
    if(gradient_iteration == 0)
    {
        int j,k;
        MAX_ADD(GasGradDataPasser[i].MaxDistance,out->MaxDistance,mode);
#ifdef SPHAV_CD10_VISCOSITY_SWITCH
        ASSIGN_ADD_PRESET(SphP[i].alpha_limiter, out->alpha_limiter, mode);
#endif
        
        MAX_ADD(GasGradDataPasser[i].Maxima.Density,out->Maxima.Density,mode);
        MIN_ADD(GasGradDataPasser[i].Minima.Density,out->Minima.Density,mode);
        MAX_ADD(GasGradDataPasser[i].Maxima.Pressure,out->Maxima.Pressure,mode);
        MIN_ADD(GasGradDataPasser[i].Minima.Pressure,out->Minima.Pressure,mode);
        for(k=0;k<3;k++)
        {
            ASSIGN_ADD_PRESET(SphP[i].Gradients.Density[k],out->Gradients[k].Density,mode);
            ASSIGN_ADD_PRESET(SphP[i].Gradients.Pressure[k],out->Gradients[k].Pressure,mode);
        }
#ifdef DOGRAD_INTERNAL_ENERGY
        MAX_ADD(GasGradDataPasser[i].Maxima.InternalEnergy,out->Maxima.InternalEnergy,mode);
        MIN_ADD(GasGradDataPasser[i].Minima.InternalEnergy,out->Minima.InternalEnergy,mode);
        for(k=0;k<3;k++) {ASSIGN_ADD_PRESET(SphP[i].Gradients.InternalEnergy[k],out->Gradients[k].InternalEnergy,mode);}
#endif
#ifdef COSMIC_RAYS
        MAX_ADD(GasGradDataPasser[i].Maxima.CosmicRayPressure,out->Maxima.CosmicRayPressure,mode);
        MIN_ADD(GasGradDataPasser[i].Minima.CosmicRayPressure,out->Minima.CosmicRayPressure,mode);
        for(k=0;k<3;k++) {ASSIGN_ADD_PRESET(SphP[i].Gradients.CosmicRayPressure[k],out->Gradients[k].CosmicRayPressure,mode);}
#endif
#ifdef DOGRAD_SOUNDSPEED
        MAX_ADD(GasGradDataPasser[i].Maxima.SoundSpeed,out->Maxima.SoundSpeed,mode);
        MIN_ADD(GasGradDataPasser[i].Minima.SoundSpeed,out->Minima.SoundSpeed,mode);
        for(k=0;k<3;k++) {ASSIGN_ADD_PRESET(SphP[i].Gradients.SoundSpeed[k],out->Gradients[k].SoundSpeed,mode);}
#endif
        
        for(j=0;j<3;j++)
        {
            MAX_ADD(GasGradDataPasser[i].Maxima.Velocity[j],out->Maxima.Velocity[j],mode);
            MIN_ADD(GasGradDataPasser[i].Minima.Velocity[j],out->Minima.Velocity[j],mode);
            for(k=0;k<3;k++)
            {
                ASSIGN_ADD_PRESET(SphP[i].Gradients.Velocity[j][k],out->Gradients[k].Velocity[j],mode);
            }
        }
        
#ifdef MAGNETIC
        
#ifdef HYDRO_SPH
#ifdef DIVBCLEANING_DEDNER
        ASSIGN_ADD_PRESET(SphP[i].divB,out->divB, mode);
#endif
        for(k = 0; k < 3; k++)
        {
            ASSIGN_ADD_PRESET(SphP[i].DtB[k],out->DtB[k], mode);
        }
#endif
        
        
#ifdef CONSTRAINED_GRADIENT_MHD
        for(j=0;j<3;j++)
        {
            ASSIGN_ADD_PRESET(SphP[i].Face_Area[j],out->Face_Area[j],mode);
            for(k=0;k<3;k++)
            {
                ASSIGN_ADD_PRESET(GasGradDataPasser[i].BGrad[j][k],out->Gradients[k].B[j],mode);
                ASSIGN_ADD_PRESET(GasGradDataPasser[i].FaceCrossX[j][k],out->FaceCrossX[j][k],mode);
            }
        }
#endif
        
        for(j=0;j<3;j++)
        {
            MAX_ADD(GasGradDataPasser[i].Maxima.B[j],out->Maxima.B[j],mode);
            MIN_ADD(GasGradDataPasser[i].Minima.B[j],out->Minima.B[j],mode);
            for(k=0;k<3;k++)
            {
#ifndef CONSTRAINED_GRADIENT_MHD
                ASSIGN_ADD_PRESET(SphP[i].Gradients.B[j][k],out->Gradients[k].B[j],mode);
#endif
            }
        }
        
#ifdef DIVBCLEANING_DEDNER
        MAX_ADD(GasGradDataPasser[i].Maxima.Phi,out->Maxima.Phi,mode);
        MIN_ADD(GasGradDataPasser[i].Minima.Phi,out->Minima.Phi,mode);
#ifndef CONSTRAINED_GRADIENT_MHD_MIDPOINT
        for(k=0;k<3;k++)
            ASSIGN_ADD_PRESET(SphP[i].Gradients.Phi[k],out->Gradients[k].Phi,mode);
#endif
#endif
#endif // closes MAGNETIC
        
#ifdef TURB_DIFF_METALS
        for(j=0;j<NUM_METAL_SPECIES;j++)
        {
            MAX_ADD(GasGradDataPasser[i].Maxima.Metallicity[j],out->Maxima.Metallicity[j],mode);
            MIN_ADD(GasGradDataPasser[i].Minima.Metallicity[j],out->Minima.Metallicity[j],mode);
            for(k=0;k<3;k++)
            {
                ASSIGN_ADD_PRESET(SphP[i].Gradients.Metallicity[j][k],out->Gradients[k].Metallicity[j],mode);
            }
        }
#endif

#ifdef RT_EVOLVE_EDDINGTON_TENSOR
        for(j=0;j<N_RT_FREQ_BINS;j++)
        {
            MAX_ADD(GasGradDataPasser[i].Maxima.E_gamma[j],out->Maxima.E_gamma[j],mode);
            MIN_ADD(GasGradDataPasser[i].Minima.E_gamma[j],out->Minima.E_gamma[j],mode);
            for(k=0;k<3;k++) {ASSIGN_ADD_PRESET(GasGradDataPasser[i].Gradients_E_gamma[j][k],out->Gradients[k].E_gamma[j],mode);}
        }
		/* the gradient dotted into the Eddington tensor is more complicated: let's handle this below */
        {
        	int k_freq; for(k_freq=0;k_freq<N_RT_FREQ_BINS;k_freq++)
        	{	
        		int k_xyz; for(k_xyz=0;k_xyz<3;k_xyz++)
        		{
        			int j_xyz,i_xyz,k_et_loop[3]; // recall, for ET: 0=xx,1=yy,2=zz,3=xy,4=yz,5=xz
					if(k_xyz==0) {k_et_loop[0]=0; k_et_loop[1]=3; k_et_loop[2]=5;}
					if(k_xyz==1) {k_et_loop[0]=3; k_et_loop[1]=1; k_et_loop[2]=4;}
					if(k_xyz==2) {k_et_loop[0]=5; k_et_loop[1]=4; k_et_loop[2]=2;}
        			for(j_xyz=0;j_xyz<3;j_xyz++)
        			{
        				for(i_xyz=0;i_xyz<3;i_xyz++)
        				{
        					SphP[i].Gradients.E_gamma_ET[k_freq][k_xyz] += SphP[i].NV_T[j_xyz][i_xyz] * out->Gradients[i_xyz].E_gamma_ET[k_freq][k_et_loop[j_xyz]];
						}
        			}
        		}
        	}
        }
#endif
    } // gradient_iteration == 0
}



void local_slopelimiter(double *grad, double valmax, double valmin, double alim, double h, double shoot_tol);

void local_slopelimiter(double *grad, double valmax, double valmin, double alim, double h, double shoot_tol)
{
    int k;
    double d_abs = 0.0;
    for(k=0;k<3;k++) {d_abs += grad[k]*grad[k];}
    if(d_abs > 0)
    {
        double cfac = 1 / (alim * h * sqrt(d_abs));
        double fabs_max = fabs(valmax);
        double fabs_min = fabs(valmin);
        double abs_min = DMIN(fabs_max,fabs_min);
        if(shoot_tol > 0)
        {
            double abs_max = DMAX(fabs_max,fabs_min);
            cfac *= DMIN(abs_min + shoot_tol*abs_max, abs_max);
        } else {
            cfac *= abs_min;
        }
        if(cfac < 1) {for(k=0;k<3;k++) {grad[k] *= cfac;}}
    }
}

void construct_gradient(double *grad, MyIDType i);

void construct_gradient(double *grad, MyIDType i)
{
    /* check if the matrix is well-conditioned: otherwise we will use the 'standard SPH-like' derivative estimation */
    if(SHOULD_I_USE_SPH_GRADIENTS(SphP[i].ConditionNumber))
    {
        /* the condition number was bad, so we used SPH-like gradients */
        int k; for(k=0;k<3;k++) {grad[k] *= PPP[i].DhsmlNgbFactor / SphP[i].Density;}
    } else {
        /* ok, the condition number was good so we used the matrix-like gradient estimator */
        int k; double v_tmp[3];
        for(k=0;k<3;k++) {v_tmp[k] = grad[k];}
        for(k=0;k<3;k++) {grad[k] = SphP[i].NV_T[k][0]*v_tmp[0] + SphP[i].NV_T[k][1]*v_tmp[1] + SphP[i].NV_T[k][2]*v_tmp[2];}
    }
}




void hydro_gradient_calc(void)
{
    int i, j, k, k1, ngrp, ndone, ndone_flag;
    int recvTask, place;
    double timeall = 0, timecomp1 = 0, timecomp2 = 0, timecommsumm1 = 0, timecommsumm2 = 0, timewait1 = 0, timewait2 = 0;
    double timecomp, timecomm, timewait, tstart, tend, t0, t1;
    int save_NextParticle;
    long long n_exported = 0;
#ifdef SPHAV_CD10_VISCOSITY_SWITCH
    double NV_dt,NV_dummy,NV_limiter,NV_A,divVel_physical,h_eff,alphaloc,cs_nv;
#endif
    
    /* allocate buffers to arrange communication */
    long long NTaskTimesNumPart;
    GasGradDataPasser = (struct temporary_data_topass *) mymalloc("GasGradDataPasser",N_gas * sizeof(struct temporary_data_topass));
    NTaskTimesNumPart = maxThreads * NumPart;
    All.BunchSize = (int) ((All.BufferSize * 1024 * 1024) / (sizeof(struct data_index) + sizeof(struct data_nodelist) +
                                                             sizeof(struct GasGraddata_in) +
                                                             sizeof(struct GasGraddata_out) +
                                                             sizemax(sizeof(struct GasGraddata_in),
                                                                     sizeof(struct GasGraddata_out))));
    CPU_Step[CPU_DENSMISC] += measure_time();
    t0 = my_second();
    
    Ngblist = (int *) mymalloc("Ngblist", NTaskTimesNumPart * sizeof(int));
    DataIndexTable = (struct data_index *) mymalloc("DataIndexTable", All.BunchSize * sizeof(struct data_index));
    DataNodeList = (struct data_nodelist *) mymalloc("DataNodeList", All.BunchSize * sizeof(struct data_nodelist));
    
    /* before doing any operations, need to zero the appropriate memory so we can correctly do pair-wise operations */
    for(i = FirstActiveParticle; i >= 0; i = NextActiveParticle[i])
        if(P[i].Type==0)
        {
            int k2;
            memset(&GasGradDataPasser[i], 0, sizeof(struct temporary_data_topass));
#ifdef HYDRO_SPH
#ifdef MAGNETIC
            for(k = 0; k < 3; k++) {SphP[i].DtB[k] = 0;}
#endif
#ifdef DIVBCLEANING_DEDNER
            SphP[i].divB = 0;
#endif
#ifdef SPHAV_CD10_VISCOSITY_SWITCH
            SphP[i].alpha_limiter = 0;
#endif
#endif
            /* and zero out the gradients structure itself */
            for(k=0;k<3;k++)
            {
                SphP[i].Gradients.Density[k] = 0;
                SphP[i].Gradients.Pressure[k] = 0;
                for(k2=0;k2<3;k2++) {SphP[i].Gradients.Velocity[k2][k] = 0;}
#ifdef DOGRAD_INTERNAL_ENERGY
                SphP[i].Gradients.InternalEnergy[k] = 0;
#endif
#ifdef COSMIC_RAYS
                SphP[i].Gradients.CosmicRayPressure[k] = 0;
#endif
#ifdef DOGRAD_SOUNDSPEED
                SphP[i].Gradients.SoundSpeed[k] = 0;
#endif
#ifdef MAGNETIC
#ifndef CONSTRAINED_GRADIENT_MHD
                for(k2=0;k2<3;k2++) {SphP[i].Gradients.B[k2][k] = 0;}
#else
                SphP[i].Face_Area[k] = 0;
#endif
#if defined(DIVBCLEANING_DEDNER) && !defined(CONSTRAINED_GRADIENT_MHD_MIDPOINT)
                SphP[i].Gradients.Phi[k] = 0;
#endif
#endif
#ifdef TURB_DIFF_METALS
                for(k2=0;k2<NUM_METAL_SPECIES;k2++) {SphP[i].Gradients.Metallicity[k2][k] = 0;}
#endif
#ifdef RT_EVOLVE_EDDINGTON_TENSOR
                for(k2=0;k2<N_RT_FREQ_BINS;k2++) {SphP[i].Gradients.E_gamma_ET[k2][k] = 0;}
#endif
            }
        }
    
    
    
    /* prepare to do the requisite number of sweeps over the particle distribution */
    int gradient_iteration;
    for(gradient_iteration = 0; gradient_iteration < NUMBER_OF_GRADIENT_ITERATIONS; gradient_iteration++)
    {
        // need to zero things used in the iteration (anything appearing in out2particle_GasGrad_iter)
        for(i = FirstActiveParticle; i >= 0; i = NextActiveParticle[i])
            if(P[i].Type==0)
            {
#ifdef CONSTRAINED_GRADIENT_MHD
                GasGradDataPasser[i].FaceDotB = 0;
#ifdef CONSTRAINED_GRADIENT_MHD_MIDPOINT
                for(k=0;k<3;k++) {GasGradDataPasser[i].PhiGrad[k] = 0;}
#endif
#endif
            }
        
        // now we actually begin the main gradient loop //
        NextParticle = FirstActiveParticle;	/* begin with this index */
        do
        {
            
            BufferFullFlag = 0;
            Nexport = 0;
            save_NextParticle = NextParticle;
            
            for(j = 0; j < NTask; j++)
            {
                Send_count[j] = 0;
                Exportflag[j] = -1;
            }
            
            /* do local particles and prepare export list */
            tstart = my_second();
            
#ifdef OMP_NUM_THREADS
            pthread_t mythreads[OMP_NUM_THREADS - 1];
            int threadid[OMP_NUM_THREADS - 1];
            pthread_attr_t attr;
            
            pthread_attr_init(&attr);
            pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
            pthread_mutex_init(&mutex_nexport, NULL);
            pthread_mutex_init(&mutex_partnodedrift, NULL);
            
            TimerFlag = 0;
            
            for(j = 0; j < OMP_NUM_THREADS - 1; j++)
            {
                threadid[j] = j + 1;
                pthread_create(&mythreads[j], &attr, GasGrad_evaluate_primary, &threadid[j]);
            }
#endif
#ifdef _OPENMP
#pragma omp parallel
#endif
            {
#ifdef _OPENMP
                int mainthreadid = omp_get_thread_num();
#else
                int mainthreadid = 0;
#endif
                GasGrad_evaluate_primary(&mainthreadid, gradient_iteration);	/* do local particles and prepare export list */
            }
            
#ifdef OMP_NUM_THREADS
            for(j = 0; j < OMP_NUM_THREADS - 1; j++)
                pthread_join(mythreads[j], NULL);
#endif
            
            
            tend = my_second();
            timecomp1 += timediff(tstart, tend);
            
            if(BufferFullFlag)
            {
                int last_nextparticle = NextParticle;
                
                NextParticle = save_NextParticle;
                
                while(NextParticle >= 0)
                {
                    if(NextParticle == last_nextparticle)
                        break;
                    
                    if(ProcessedFlag[NextParticle] != 1)
                        break;
                    
                    ProcessedFlag[NextParticle] = 2;
                    
                    NextParticle = NextActiveParticle[NextParticle];
                }
                
                if(NextParticle == save_NextParticle)
                {
                    /* in this case, the buffer is too small to process even a single particle */
                    endrun(113308);
                }
                
                int new_export = 0;
                
                for(j = 0, k = 0; j < Nexport; j++)
                    if(ProcessedFlag[DataIndexTable[j].Index] != 2)
                    {
                        if(k < j + 1)
                            k = j + 1;
                        
                        for(; k < Nexport; k++)
                            if(ProcessedFlag[DataIndexTable[k].Index] == 2)
                            {
                                int old_index = DataIndexTable[j].Index;
                                
                                DataIndexTable[j] = DataIndexTable[k];
                                DataNodeList[j] = DataNodeList[k];
                                DataIndexTable[j].IndexGet = j;
                                new_export++;
                                
                                DataIndexTable[k].Index = old_index;
                                k++;
                                break;
                            }
                    }
                    else
                        new_export++;
                
                Nexport = new_export;
                
            }
            
            n_exported += Nexport;
            
            for(j = 0; j < NTask; j++)
                Send_count[j] = 0;
            for(j = 0; j < Nexport; j++)
                Send_count[DataIndexTable[j].Task]++;
            
            MYSORT_DATAINDEX(DataIndexTable, Nexport, sizeof(struct data_index), data_index_compare);
            
            tstart = my_second();
            
            MPI_Alltoall(Send_count, 1, MPI_INT, Recv_count, 1, MPI_INT, MPI_COMM_WORLD);
            
            tend = my_second();
            timewait1 += timediff(tstart, tend);
            
            for(j = 0, Nimport = 0, Recv_offset[0] = 0, Send_offset[0] = 0; j < NTask; j++)
            {
                Nimport += Recv_count[j];
                
                if(j > 0)
                {
                    Send_offset[j] = Send_offset[j - 1] + Send_count[j - 1];
                    Recv_offset[j] = Recv_offset[j - 1] + Recv_count[j - 1];
                }
            }
            
            GasGradDataGet = (struct GasGraddata_in *) mymalloc("GasGradDataGet", Nimport * sizeof(struct GasGraddata_in));
            GasGradDataIn = (struct GasGraddata_in *) mymalloc("GasGradDataIn", Nexport * sizeof(struct GasGraddata_in));
            
            /* prepare particle data for export */
            
            for(j = 0; j < Nexport; j++)
            {
                place = DataIndexTable[j].Index;
                particle2in_GasGrad(&GasGradDataIn[j], place, gradient_iteration);
#ifndef DONOTUSENODELIST
                memcpy(GasGradDataIn[j].NodeList,
                       DataNodeList[DataIndexTable[j].IndexGet].NodeList, NODELISTLENGTH * sizeof(int));
#endif
                
            }
            
            /* exchange particle data */
            tstart = my_second();
            for(ngrp = 1; ngrp < (1 << PTask); ngrp++)
            {
                recvTask = ThisTask ^ ngrp;
                
                if(recvTask < NTask)
                {
                    if(Send_count[recvTask] > 0 || Recv_count[recvTask] > 0)
                    {
                        /* get the particles */
                        MPI_Sendrecv(&GasGradDataIn[Send_offset[recvTask]],
                                     Send_count[recvTask] * sizeof(struct GasGraddata_in), MPI_BYTE,
                                     recvTask, TAG_INTERLOOP_A,
                                     &GasGradDataGet[Recv_offset[recvTask]],
                                     Recv_count[recvTask] * sizeof(struct GasGraddata_in), MPI_BYTE,
                                     recvTask, TAG_INTERLOOP_A, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                    }
                }
            }
            tend = my_second();
            timecommsumm1 += timediff(tstart, tend);
            
            myfree(GasGradDataIn);
            if(gradient_iteration==0)
            {
                GasGradDataResult = (struct GasGraddata_out *) mymalloc("GasGradDataResult", Nimport * sizeof(struct GasGraddata_out));
                GasGradDataOut = (struct GasGraddata_out *) mymalloc("GasGradDataOut", Nexport * sizeof(struct GasGraddata_out));
                //                report_memory_usage(&HighMark_GasGrad, "GRADIENTS_LOOP");
            } else {
                GasGradDataResult_iter = (struct GasGraddata_out_iter *) mymalloc("GasGradDataResult_iter", Nimport * sizeof(struct GasGraddata_out_iter));
                GasGradDataOut_iter = (struct GasGraddata_out_iter *) mymalloc("GasGradDataOut_iter", Nexport * sizeof(struct GasGraddata_out_iter));
            }
            
            /* now do the particles that were sent to us */
            tstart = my_second();
            NextJ = 0;
            
#ifdef OMP_NUM_THREADS
            for(j = 0; j < OMP_NUM_THREADS - 1; j++)
                pthread_create(&mythreads[j], &attr, GasGrad_evaluate_secondary, &threadid[j]);
#endif
#ifdef _OPENMP
#pragma omp parallel
#endif
            {
#ifdef _OPENMP
                int mainthreadid = omp_get_thread_num();
#else
                int mainthreadid = 0;
#endif
                GasGrad_evaluate_secondary(&mainthreadid, gradient_iteration);
            }
            
#ifdef OMP_NUM_THREADS
            for(j = 0; j < OMP_NUM_THREADS - 1; j++)
                pthread_join(mythreads[j], NULL);
            
            pthread_mutex_destroy(&mutex_partnodedrift);
            pthread_mutex_destroy(&mutex_nexport);
            pthread_attr_destroy(&attr);
#endif
            
            tend = my_second();
            timecomp2 += timediff(tstart, tend);
            
            if(NextParticle < 0)
                ndone_flag = 1;
            else
                ndone_flag = 0;
            
            tstart = my_second();
            MPI_Allreduce(&ndone_flag, &ndone, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
            tend = my_second();
            timewait2 += timediff(tstart, tend);
            
            /* get the result */
            tstart = my_second();
            for(ngrp = 1; ngrp < (1 << PTask); ngrp++)
            {
                recvTask = ThisTask ^ ngrp;
                if(recvTask < NTask)
                {
                    if(Send_count[recvTask] > 0 || Recv_count[recvTask] > 0)
                    {
                        /* send the results */
                        if(gradient_iteration==0)
                        {
                            MPI_Sendrecv(&GasGradDataResult[Recv_offset[recvTask]],
                                         Recv_count[recvTask] * sizeof(struct GasGraddata_out),
                                         MPI_BYTE, recvTask, TAG_INTERLOOP_B,
                                         &GasGradDataOut[Send_offset[recvTask]],
                                         Send_count[recvTask] * sizeof(struct GasGraddata_out),
                                         MPI_BYTE, recvTask, TAG_INTERLOOP_B, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                        } else {
                            MPI_Sendrecv(&GasGradDataResult_iter[Recv_offset[recvTask]],
                                         Recv_count[recvTask] * sizeof(struct GasGraddata_out_iter),
                                         MPI_BYTE, recvTask, TAG_INTERLOOP_B,
                                         &GasGradDataOut_iter[Send_offset[recvTask]],
                                         Send_count[recvTask] * sizeof(struct GasGraddata_out_iter),
                                         MPI_BYTE, recvTask, TAG_INTERLOOP_B, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                        }
                    }
                }
            }
            tend = my_second();
            timecommsumm2 += timediff(tstart, tend);
            
            /* add the result to the local particles */
            tstart = my_second();
            for(j = 0; j < Nexport; j++)
            {
                place = DataIndexTable[j].Index;
                if(gradient_iteration==0)
                {
                    out2particle_GasGrad(&GasGradDataOut[j], place, 1, gradient_iteration);
                } else {
                    out2particle_GasGrad_iter(&GasGradDataOut_iter[j], place, 1, gradient_iteration);
                }
            }
            tend = my_second();
            timecomp1 += timediff(tstart, tend);
            if(gradient_iteration==0)
            {
                myfree(GasGradDataOut);
                myfree(GasGradDataResult);
            } else {
                myfree(GasGradDataOut_iter);
                myfree(GasGradDataResult_iter);
            }
            myfree(GasGradDataGet);
        }
        while(ndone < NTask);
        
        
        /* here, we insert intermediate operations on the results, from the iterations we have completed */
#ifdef CONSTRAINED_GRADIENT_MHD
        for(i = FirstActiveParticle; i >= 0; i = NextActiveParticle[i])
            if(P[i].Type == 0)
            {
                SphP[i].FlagForConstrainedGradients = 1;
                /* copy everything from the structure holding B-gradients (needed so they dont change mid-loop) */
                for(k=0;k<3;k++)
                {
                    for(k1=0;k1<3;k1++)
                    {
                        SphP[i].Gradients.B[k][k1] = GasGradDataPasser[i].BGrad[k][k1];
                    }
                }
                /* build the gradient */
                for(k=0;k<3;k++) {construct_gradient(SphP[i].Gradients.B[k],i);}
                /* slope limit it */
                double v_tmp = P[i].Mass / SphP[i].Density;
                double tmp_d = sqrt(1.0e-37 + (2. * All.cf_atime/All.cf_afac1 * SphP[i].Pressure*v_tmp*v_tmp) +
                                    SphP[i].BPred[0]*SphP[i].BPred[0]+SphP[i].BPred[1]*SphP[i].BPred[1]+SphP[i].BPred[2]*SphP[i].BPred[2]);
                double tmp = 3.0e3 * fabs(SphP[i].divB) * PPP[i].Hsml / tmp_d;
                double alim = 1. + DMIN(1.,tmp*tmp);
#if (CONSTRAINED_GRADIENT_MHD <= 1)
                double dbmax=0, dbgrad=0;
                double dh=0.25*PPP[i].Hsml; // need to be more aggressive with new wt_i,wt_j formalism
                for(k=0;k<3;k++)
                {
                    double b0 = Get_Particle_BField(i,k);
                    double dd = 2. * fabs(b0) * DMIN(fabs(GasGradDataPasser[i].Minima.B[k]) , fabs(GasGradDataPasser[i].Maxima.B[k]));
                    dbmax = DMIN(fabs(dbmax+dd),fabs(dbmax-dd));
                    for(k1=0;k1<3;k1++) {dbgrad += 2.*dh * fabs(b0*SphP[i].Gradients.B[k][k1]);}
                }
                dbmax /= dbgrad;
                for(k1=0;k1<3;k1++)
                {
                    double d_abs=0; for(k=0;k<3;k++) {d_abs += SphP[i].Gradients.B[k1][k]*SphP[i].Gradients.B[k1][k];}
                    if(d_abs > 0)
                    {
                        double cfac = 1 / (0.25 * PPP[i].Hsml * sqrt(d_abs));
                        cfac *= DMIN(fabs(GasGradDataPasser[i].Maxima.B[k1]) , fabs(GasGradDataPasser[i].Minima.B[k1]));
                        double c_eff = DMIN( cfac , DMAX(cfac/alim , dbmax) );
                        if(c_eff < 1) {for(k=0;k<3;k++) {SphP[i].Gradients.B[k1][k] *= c_eff;}}
                    } else {
                        for(k=0;k<3;k++) {SphP[i].Gradients.B[k1][k]=0;}
                    }
                }
#endif
                /* check the particle area closure, which will inform whether it is safe to use the constrained gradients */
                double area = fabs(SphP[i].Face_Area[0]) + fabs(SphP[i].Face_Area[1]) + fabs(SphP[i].Face_Area[2]);
                area /= Get_Particle_Expected_Area(PPP[i].Hsml);
                /* set the relevant flags to decide whether or not we use the constrained gradients */
                if(area > 0.5) {SphP[i].FlagForConstrainedGradients = 0;}
                if(SphP[i].ConditionNumber > 1000.) {SphP[i].FlagForConstrainedGradients = 0;}
                if(SHOULD_I_USE_SPH_GRADIENTS(SphP[i].ConditionNumber)) {SphP[i].FlagForConstrainedGradients = 0;} /* this must be here, since in this case the SPH gradients are used, which will not work with this method */
                
                /* now check, and if ok, enter the gradient re-calculation */
                if(SphP[i].FlagForConstrainedGradients == 1)
                {
                    double GB0[3][3];
                    double fsum = 0.0, dmag = 0.0;
                    double h_eff = Get_Particle_Size(i);
                    for(k=0;k<3;k++)
                    {
                        double grad_limiter_mag = Get_Particle_BField(i,k) / h_eff;
                        dmag += grad_limiter_mag * grad_limiter_mag;
                        for(k1=0;k1<3;k1++)
                        {
                            GB0[k][k1] = SphP[i].Gradients.B[k][k1];
                            dmag += GB0[k][k1] * GB0[k][k1];
                            fsum += GasGradDataPasser[i].FaceCrossX[k][k1] * GasGradDataPasser[i].FaceCrossX[k][k1];
                        }
                    }
                    if((fsum <= 0) || (dmag <= 0))
                    {
                        SphP[i].FlagForConstrainedGradients = 0;
                    } else {
                        dmag = 2.0 * sqrt(dmag); // limits the maximum magnitude of the correction term we will allow //
                        fsum = -1 / fsum;
                        int j_gloop;
                        for(j_gloop = 0; j_gloop < 5; j_gloop++)
                        {
                            /* calculate the correction terms */
                            double asum=GasGradDataPasser[i].FaceDotB;
                            for(k=0;k<3;k++)
                            {
                                for(k1=0;k1<3;k1++)
                                {
                                    asum += SphP[i].Gradients.B[k][k1] * GasGradDataPasser[i].FaceCrossX[k][k1];
                                }
                            }
                            double prefac = 1.0 * asum * fsum;
                            double ecorr[3][3];
                            double cmag=0;
                            for(k=0;k<3;k++)
                            {
                                for(k1=0;k1<3;k1++)
                                {
                                    ecorr[k][k1] = prefac * GasGradDataPasser[i].FaceCrossX[k][k1];
                                    double grad_limiter_mag = (SphP[i].Gradients.B[k][k1] + ecorr[k][k1]) - GB0[k][k1];
                                    cmag += grad_limiter_mag * grad_limiter_mag;
                                }
                            }
                            cmag = sqrt(cmag);
                            /* limit the correction term, based on the maximum calculated above */
                            double nnorm = 1.0;
                            if(cmag > dmag) nnorm *= dmag / cmag;
                            /* finally, we can apply the correction */
                            for(k=0;k<3;k++)
                            {
                                for(k1=0;k1<3;k1++)
                                {
                                    SphP[i].Gradients.B[k][k1] = GB0[k][k1] + nnorm*(SphP[i].Gradients.B[k][k1]+ecorr[k][k1] - GB0[k][k1]);
                                }
                                /* slope-limit the corrected gradients again, but with a more tolerant slope-limiter */
#if (CONSTRAINED_GRADIENT_MHD <= 1)
                                local_slopelimiter(SphP[i].Gradients.B[k],
                                                   GasGradDataPasser[i].Maxima.B[k],GasGradDataPasser[i].Minima.B[k],
                                                   0.25, PPP[i].Hsml, 0.25);
#endif
                            }
                        } // closes j_gloop loop
                    } // closes fsum/dmag check
                } // closes FlagForConstrainedGradients check
#ifdef CONSTRAINED_GRADIENT_MHD_MIDPOINT
                double a_limiter = 0.25; if(SphP[i].ConditionNumber>100) a_limiter=DMIN(0.5, 0.25 + 0.25 * (SphP[i].ConditionNumber-100)/100);
                /* copy everything from the structure holding phi-gradients (needed so they dont change mid-loop) */
                for(k=0;k<3;k++) {SphP[i].Gradients.Phi[k] = GasGradDataPasser[i].PhiGrad[k];}
                /* build and limit the gradient */
                construct_gradient(SphP[i].Gradients.Phi,i);
                local_slopelimiter(SphP[i].Gradients.Phi,GasGradDataPasser[i].Maxima.Phi,GasGradDataPasser[i].Minima.Phi,a_limiter,PPP[i].Hsml,0.0);
#endif
            } // closes Ptype == 0 check
#endif
    } // closes gradient_iteration
    
    myfree(DataNodeList);
    myfree(DataIndexTable);
    myfree(Ngblist);
    
    
    /* do final operations on results: these are operations that can be done after the complete set of iterations */
    for(i = FirstActiveParticle; i >= 0; i = NextActiveParticle[i])
        if(P[i].Type == 0)
        {
            /* now we can properly calculate (second-order accurate) gradients of hydrodynamic quantities from this loop */
            construct_gradient(SphP[i].Gradients.Density,i);
            construct_gradient(SphP[i].Gradients.Pressure,i);
            for(k=0;k<3;k++) {construct_gradient(SphP[i].Gradients.Velocity[k],i);}
#ifdef DOGRAD_INTERNAL_ENERGY
            construct_gradient(SphP[i].Gradients.InternalEnergy,i);
#endif
#ifdef COSMIC_RAYS
            construct_gradient(SphP[i].Gradients.CosmicRayPressure,i);
#endif
#ifdef DOGRAD_SOUNDSPEED
            construct_gradient(SphP[i].Gradients.SoundSpeed,i);
#endif
#ifdef MAGNETIC
#ifndef CONSTRAINED_GRADIENT_MHD
            for(k=0;k<3;k++) {construct_gradient(SphP[i].Gradients.B[k],i);}
#endif
#if defined(DIVBCLEANING_DEDNER) && !defined(CONSTRAINED_GRADIENT_MHD_MIDPOINT)
            construct_gradient(SphP[i].Gradients.Phi,i);
#endif
#endif
#ifdef TURB_DIFF_METALS
            for(k=0;k<NUM_METAL_SPECIES;k++) {construct_gradient(SphP[i].Gradients.Metallicity[k],i);}
#endif
#ifdef RT_EVOLVE_EDDINGTON_TENSOR
            for(k=0;k<N_RT_FREQ_BINS;k++) {construct_gradient(GasGradDataPasser[i].Gradients_E_gamma[k],i);}
#endif
            
            /* now the gradients are calculated: below are simply useful operations on the results */
#ifdef DO_DENSITY_AROUND_STAR_PARTICLES
            /* this is here because for the models of BH growth and self-shielding of stars, we
             need to calculate GradRho: we don't bother doing it in density.c if we're already calculating it here! */
            for(k=0;k<3;k++) {P[i].GradRho[k] = SphP[i].Gradients.Density[k];}
#endif
            
#if defined(TURB_DRIVING) || defined(OUTPUT_VORTICITY)
            SphP[i].Vorticity[0] = SphP[i].Gradients.Velocity[1][2] - SphP[i].Gradients.Velocity[2][1];
            SphP[i].Vorticity[1] = SphP[i].Gradients.Velocity[2][0] - SphP[i].Gradients.Velocity[0][2];
            SphP[i].Vorticity[2] = SphP[i].Gradients.Velocity[0][1] - SphP[i].Gradients.Velocity[1][0];
#endif
            
#ifdef SPH_TP12_ARTIFICIAL_RESISTIVITY
            /* use the magnitude of the B-field gradients relative to kernel length to calculate artificial resistivity */
            double GradBMag=0.0;
            double BMag=0.0;
            for(k=0;k<3;k++)
            {
                for(j=0;j<3;j++)
                {
                    GradBMag += SphP[i].Gradients.B[k][j]*SphP[i].Gradients.B[k][j];
                }
                BMag += Get_Particle_BField(i,k)*Get_Particle_BField(i,k);
            }
            SphP[i].Balpha = PPP[i].Hsml * sqrt(GradBMag/(BMag+1.0e-33));
            SphP[i].Balpha = DMIN(SphP[i].Balpha, 0.1 * All.ArtMagDispConst);
            SphP[i].Balpha = DMAX(SphP[i].Balpha, 0.005);
#endif
            
            
#ifdef HYDRO_SPH
            
#ifdef MAGNETIC
            if(SphP[i].Density > 0)
            {
                for(k=0;k<3;k++) SphP[i].DtB[k] *= PPP[i].DhsmlNgbFactor * P[i].Mass / (SphP[i].Density * SphP[i].Density) / All.cf_atime; // induction equation (convert from Bcode*vcode/rcode to Bphy/tphys) //
#ifdef DIVBCLEANING_DEDNER
                /* full correct form of D(phi)/Dt = -ch*ch*div.dot.B - phi/tau - (1/2)*phi*div.dot.v */
                /* PFH: here's the div.dot.B term: make sure div.dot.B def'n matches appropriate grad_phi conjugate pair: recommend direct diff div.dot.B */
                SphP[i].divB *= PPP[i].DhsmlNgbFactor * P[i].Mass / (SphP[i].Density * SphP[i].Density);
                if((!isnan(SphP[i].divB))&&(PPP[i].Hsml>0)&&(SphP[i].divB!=0)&&(SphP[i].Density>0))
                {
                    double tmp_ded = 0.5 * SphP[i].MaxSignalVel * All.cf_afac3; // has units of v_physical now
                    /* do a check to make sure divB isn't something wildly divergent (owing to particles being too close) */
                    double b2_max = 0.0;
                    for(k=0;k<3;k++) {b2_max += Get_Particle_BField(i,k)*Get_Particle_BField(i,k);}
                    b2_max = 100.0 * fabs( sqrt(b2_max) * All.cf_a2inv * P[i].Mass / (SphP[i].Density*All.cf_a3inv) * 1.0 / (PPP[i].Hsml*All.cf_atime) );
                    if(fabs(SphP[i].divB) > b2_max) {SphP[i].divB *= b2_max / fabs(SphP[i].divB);}
                    /* ok now can apply this to get the growth rate of phi */
                    // SphP[i].DtPhi = -tmp_ded * tmp_ded * All.DivBcleanHyperbolicSigma * SphP[i].divB;
                    SphP[i].DtPhi = -tmp_ded * tmp_ded * All.DivBcleanHyperbolicSigma * SphP[i].divB * SphP[i].Density*All.cf_a3inv; // mass-based phi-flux
                    // phiphi above now has units of [Bcode]*[vcode]^2/[rcode]=(Bcode*vcode)*vcode/rcode; needs to have units of [Phicode]*[vcode]/[rcode]
                    // [PhiGrad]=[Phicode]/[rcode] = [DtB] = [Bcode]*[vcode]/[rcode] IFF [Phicode]=[Bcode]*[vcode]; this also makes the above self-consistent //
                    // (implicitly, this gives the correct evolution in comoving, adiabatic coordinates where the sound speed is the relevant speed at which
                    //   the 'damping wave' propagates. another choice (provided everything else is self-consistent) is fine, it just makes different assumptions
                    //   about the relevant 'desired' timescale for damping wave propagation in the expanding box) //
                } else {
                    SphP[i].DtPhi=0; SphP[i].divB=0; for(k=0;k<3;k++) {SphP[i].DtB[k] = 0;}
                }
                SphP[i].divB = 0.0; // now we re-zero it, since a -different- divB definition must be used in hydro to subtract the tensile terms */
#endif
            } else {
                for(k=0;k<3;k++) SphP[i].DtB[k] = 0;
#ifdef DIVBCLEANING_DEDNER
                SphP[i].divB = 0; SphP[i].DtPhi = 0;
#endif
            }
#endif
            
            
#ifdef SPHAV_CD10_VISCOSITY_SWITCH
            SphP[i].alpha_limiter /= SphP[i].Density;
            NV_dt =  (P[i].TimeBin ? (((integertime) 1) << P[i].TimeBin) : 0) * All.Timebase_interval / All.cf_hubble_a; // physical
            NV_dummy = fabs(1.0 * pow(1.0 - SphP[i].alpha_limiter,4.0) * SphP[i].NV_DivVel); // NV_ quantities are in physical units
            NV_limiter = NV_dummy*NV_dummy / (NV_dummy*NV_dummy + SphP[i].NV_trSSt);
            NV_A = DMAX(-SphP[i].NV_dt_DivVel, 0.0);
            divVel_physical = SphP[i].NV_DivVel;
            
            // add a simple limiter here: alpha_loc is 'prepped' but only switches on when the divergence goes negative: want to add hubble flow here //
            if(All.ComovingIntegrationOn) divVel_physical += 3*All.cf_hubble_a; // hubble-flow correction added
            if(divVel_physical>=0.0) NV_A = 0.0;
            
            h_eff = Get_Particle_Size(i) * All.cf_atime / 0.5; // 'default' parameter choices are scaled for a cubic spline //
            cs_nv = Particle_effective_soundspeed_i(i) * All.cf_afac3; // converts to physical velocity units //
            alphaloc = All.ViscosityAMax * h_eff*h_eff*NV_A / (0.36*cs_nv*cs_nv*(0.05/SPHAV_CD10_VISCOSITY_SWITCH) + h_eff*h_eff*NV_A);
            // 0.25 in front of vsig is the 'noise parameter' that determines the relative amplitude which will trigger the switch:
            //    that choice was quite large (requires approach velocity rate-of-change is super-sonic); better to use c_s (above), and 0.05-0.25 //
            // NV_A is physical 1/(time*time), but Hsml and vsig can be comoving, so need appropriate correction terms above //
            
            if(SphP[i].alpha < alphaloc)
                SphP[i].alpha = alphaloc;
            else if (SphP[i].alpha > alphaloc)
                SphP[i].alpha = alphaloc + (SphP[i].alpha - alphaloc) * exp(-NV_dt * (0.5*fabs(SphP[i].MaxSignalVel)*All.cf_afac3)/(0.5*h_eff) * SPHAV_CD10_VISCOSITY_SWITCH);
            
            if(SphP[i].alpha < All.ViscosityAMin)
                SphP[i].alpha = All.ViscosityAMin;
            
            SphP[i].alpha_limiter = DMAX(NV_limiter,All.ViscosityAMin/SphP[i].alpha);
#else
            /* compute the traditional Balsara limiter (now that we have velocity gradients) */
            double divVel = All.cf_a2inv * fabs(SphP[i].Gradients.Velocity[0][0] + SphP[i].Gradients.Velocity[1][1] + SphP[i].Gradients.Velocity[2][2]);
            if(All.ComovingIntegrationOn) divVel += 3*All.cf_hubble_a; // hubble-flow correction added (physical units)
            double CurlVel[3];
            double MagCurl;
            CurlVel[0] = SphP[i].Gradients.Velocity[1][2] - SphP[i].Gradients.Velocity[2][1];
            CurlVel[1] = SphP[i].Gradients.Velocity[2][0] - SphP[i].Gradients.Velocity[0][2];
            CurlVel[2] = SphP[i].Gradients.Velocity[0][1] - SphP[i].Gradients.Velocity[1][0];
            MagCurl = All.cf_a2inv * sqrt(CurlVel[0]*CurlVel[0] + CurlVel[1]*CurlVel[1] + CurlVel[2]*CurlVel[2]);
            double fac_mu = 1 / (All.cf_afac3 * All.cf_atime);
            SphP[i].alpha_limiter = divVel / (divVel + MagCurl + 0.0001 * Particle_effective_soundspeed_i(i) /
                                              (Get_Particle_Size(i)) / fac_mu);
#endif
#endif
            
            

#if defined(CONDUCTION_SPITZER) || defined(VISCOSITY_BRAGINSKII) || (defined(MHD_NON_IDEAL) && defined(COOLING))
            /* get the neutral fraction */
            double nHeII, temperature, u, ne, nh0, mu, yhelium = 0;
            ne = SphP[i].Ne;
            u = DMAX(All.MinEgySpec, SphP[i].InternalEnergy); // needs to be in code units
            AbundanceRatios(u, SphP[i].Density*All.cf_a3inv, &ne, &nh0, &nHeII, i);
            yhelium = (1 - HYDROGEN_MASSFRAC) / (4 * HYDROGEN_MASSFRAC);
            mu = (1 + 4 * yhelium) / (1 + yhelium + ne);
            temperature = GAMMA_MINUS1 / BOLTZMANN * u * PROTONMASS * mu;
#ifdef GALSF_FB_HII_HEATING
            if(SphP[i].DelayTimeHII>0 || SphP[i].Ne>=1.15789) nh0=0;
#endif
            double ion_frac = DMIN(DMAX(0,1.-nh0),1);
#endif
            
            
#ifdef CONDUCTION
            {
                SphP[i].Kappa_Conduction = All.ConductionCoeff;
#ifdef CONDUCTION_SPITZER
                /* calculate the thermal conductivities: use the Spitzer formula */
                SphP[i].Kappa_Conduction *= ion_frac * pow(SphP[i].InternalEnergyPred, 2.5);
                
                /* account for saturation (when the mean free path of electrons is large): estimate whether we're in that limit with the gradients */
                double electron_free_path = All.ElectronFreePathFactor * SphP[i].InternalEnergyPred * SphP[i].InternalEnergyPred / (SphP[i].Density * All.cf_a3inv);
                double du_conduction=0;
                for(k=0;k<3;k++) {du_conduction += SphP[i].Gradients.InternalEnergy[k] * SphP[i].Gradients.InternalEnergy[k];}
                double temp_scale_length = SphP[i].InternalEnergyPred / sqrt(du_conduction) * All.cf_atime;
                SphP[i].Kappa_Conduction /= (1 + 4.2 * electron_free_path / temp_scale_length); // should be in physical units //
#endif
            }
#endif
            
            
            
#ifdef VISCOSITY
            {
                SphP[i].Eta_ShearViscosity = All.ShearViscosityCoeff;
                SphP[i].Zeta_BulkViscosity = All.BulkViscosityCoeff;
#ifdef VISCOSITY_BRAGINSKII
                /* calculate the viscosity coefficients: use the Braginskii shear tensor formulation expanded to first order */
                SphP[i].Eta_ShearViscosity *= ion_frac * pow(SphP[i].InternalEnergyPred, 2.5);
                SphP[i].Zeta_BulkViscosity = 0;
                
                /* again need to account for possible saturation (when the mean free path of ions is large): estimate whether we're in that limit with the gradients */
                double ion_free_path = All.ElectronFreePathFactor * SphP[i].InternalEnergyPred * SphP[i].InternalEnergyPred / (SphP[i].Density * All.cf_a3inv);
                /* need an estimate of the internal energy gradient scale length, which we get by d(P/rho) = P/rho * (dP/P - drho/rho) */
                double dv_magnitude=0, v_magnitude=0;
                for(k=0;k<3;k++)
                {
                    for(k1=0;k1<3;k1++) {dv_magnitude += SphP[i].Gradients.Velocity[k][k1]*SphP[i].Gradients.Velocity[k][k1];}
                    v_magnitude += SphP[i].VelPred[k]*SphP[i].VelPred[k];
                }
                double vel_scale_length = sqrt( v_magnitude / dv_magnitude ) * All.cf_atime;
                SphP[i].Eta_ShearViscosity /= (1 + 4.2 * ion_free_path / vel_scale_length); // should be in physical units //
                /* also limit to saturation magnitude ~ signal_speed / lambda_MFP^2 */
                double cs = Particle_effective_soundspeed_i(i);
#ifdef MAGNETIC
                double vA_2 = 0.0; for(k=0;k<3;k++) {vA_2 += Get_Particle_BField(i,k)*Get_Particle_BField(i,k);}
                vA_2 *= All.cf_afac1 / (All.cf_atime * SphP[i].Density);
                cs = DMIN(1.e4*cs , sqrt(cs*cs+vA_2));
#endif
                cs *= All.cf_afac3;
                double eta_sat = (SphP[i].Density*All.cf_a3inv) * cs / (ion_free_path * (1 + 4.2 * ion_free_path / vel_scale_length));
                if(eta_sat <= 0) SphP[i].Eta_ShearViscosity=0;
                if(SphP[i].Eta_ShearViscosity>0) {SphP[i].Eta_ShearViscosity = 1. / (1./SphP[i].Eta_ShearViscosity + 1./eta_sat);} // again, all physical units //
#endif
            }
#endif

            
            
#ifdef MHD_NON_IDEAL
            {
                double mu_unit = 4.0*M_PI; // 4PI for Gaussian units
#ifdef COOLING
                double n_elec = SphP[i].Ne; // electron number per particle
                double ionized_frac = ion_frac;
                double mean_molecular_weight = mu; // mean molecular weight in g
#else
                double mean_molecular_weight = PROTONMASS; // mean molecular weight in g
                double n_elec = 1;
                double ionized_frac = n_elec;
                double temperature = GAMMA_MINUS1 / BOLTZMANN * (SphP[i].InternalEnergy*All.UnitPressure_in_cgs/All.UnitDensity_in_cgs) * PROTONMASS * mu;
#endif
                double density_cgs = SphP[i].Density * All.cf_a3inv * All.UnitDensity_in_cgs * All.HubbleParam * All.HubbleParam; // mass density (cgs) //
                double density_ions = ionized_frac * density_cgs; // assume all the mass is in ions, with global charge neutrality
                double numden_e = n_elec * density_cgs / mean_molecular_weight; // number density of electrons (cgs) //
                double gizmo2gauss = sqrt(4.*M_PI*All.UnitPressure_in_cgs*All.HubbleParam*All.HubbleParam) / All.UnitMagneticField_in_gauss; // convert to B-field to gauss (units)
                double bmag = 0; for(k=0;k<3;k++) {bmag += Get_Particle_BField(i,k)*Get_Particle_BField(i,k);} // get magnitude of B //
                if(bmag<=0) {bmag=0;} else {bmag = sqrt(bmag) * All.cf_a2inv * gizmo2gaus;} // B-field magnitude in Gauss
                
                double gamma_e = 8.3e-9 * DMAX( 1.0, sqrt(temperature/100.) ) / (2. * mean_molecular_weight);
                double gamma_i = 2.0e-9 * sqrt(PROTONMASS/mean_molecular_weight) / mean_molecular_weight;
                
                double eta_ohmic = (C * C * ELECTRONMASS * mean_molecular_weight) / (mu_unit * gamma_e * ELECTRONCHARGE*ELECTRONCHARGE * n_elec);
                double eta_hall = (C * bmag) / (mu_unit * ELECTRONCHARGE * numden_e);
                double eta_ad = (bmag * bmag) / (mu_unit * gamma_i * density_cgs * density_ions);
                
                double units_cgs_to_code = All.UnitTime_in_s / (All.UnitLength_in_cm * All.UnitLength_in_cm) * All.HubbleParam; // convert coefficients (L^2/t) to code units [physical]
                eta_ohmic *= units_cgs_to_code; eta_hall *= units_cgs_to_code; eta_ad *= units_cgs_to_code;
                
                SphP[i].Eta_MHD_OhmicResistivity_Coeff = eta_ohmic;     /*!< Ohmic resistivity coefficient [physical units of L^2/t] */
                SphP[i].Eta_MHD_HallEffect_Coeff = eta_hall;            /*!< Hall effect coefficient [physical units of L^2/t] */
                SphP[i].Eta_MHD_AmbiPolarDiffusion_Coeff = eta_ad;      /*!< Hall effect coefficient [physical units of L^2/t] */
            }
#endif
            
            
#ifdef RADTRANSFER
            {
                int k_freq;
                for(k_freq = 0; k_freq < N_RT_FREQ_BINS; k_freq++)
                {
                    /* calculate the opacity */
                    SphP[i].Kappa_RT[k_freq] = rt_kappa(i, k_freq); // physical units //
                    double lambda = 1;
#if defined(RT_FLUXLIMITER) && defined(RT_EVOLVE_EDDINGTON_TENSOR)
                    /* compute the flux-limiter for radiation transport: also convenient here to compute the relevant opacities for all particles */
                    if(SphP[i].E_gamma_Pred[k_freq] > 0)
                    {
                        /* estimate: 1/gradient_length_scale */
                        double R = sqrt(GasGradDataPasser[i].Gradients_E_gamma[k_freq][0] * GasGradDataPasser[i].Gradients_E_gamma[k_freq][0] +
                                        GasGradDataPasser[i].Gradients_E_gamma[k_freq][1] * GasGradDataPasser[i].Gradients_E_gamma[k_freq][1] +
                                        GasGradDataPasser[i].Gradients_E_gamma[k_freq][2] * GasGradDataPasser[i].Gradients_E_gamma[k_freq][2]) / (1.e-37 + SphP[i].E_gamma_Pred[k_freq] * SphP[i].Density/(1.e-37+P[i].Mass));

                        /* use the maximum of the above or the gradient dotted into the full Eddington tensor, which we now have */
                        double R_ET = sqrt(SphP[i].Gradients.E_gamma_ET[k_freq][0] * SphP[i].Gradients.E_gamma_ET[k_freq][0] +
                                           SphP[i].Gradients.E_gamma_ET[k_freq][1] * SphP[i].Gradients.E_gamma_ET[k_freq][1] +
                                           SphP[i].Gradients.E_gamma_ET[k_freq][2] * SphP[i].Gradients.E_gamma_ET[k_freq][2]) / (1.e-37 + SphP[i].E_gamma_Pred[k_freq] * SphP[i].Density/(1.e-37+P[i].Mass));
                        R = R_ET; // testing; appears more accurate //
                        R = DMAX(R,R_ET); // R_ET may always be less than R, though
                        R /= (1.e-37 + All.cf_atime * SphP[i].Kappa_RT[k_freq] * (SphP[i].Density*All.cf_a3inv)); /* dimensionless (all in physical) */
                        /* now we can apply the actual slope-limiter function desired */
                        R_ET = 1.*R;
                        lambda = 3. * (2. + R_ET) / (6. + 3.*R_ET + R_ET*R_ET);
                        if(lambda < 1e-30) lambda = 1.e-30;
#ifdef RT_OTVET
                        /* note that the OTVET eddington tensor is close to the correct value for the optically-thin limit. for the diffusion limit 
                            it may be incorrect. we can therefore interpolate using an M1-like relation below, based on the gradients above (used 
                            to determine which limit we are actually in */
                        /* calculate ratio f=|Flux|/(c_eff*Energy_density_rad): f<<1 = diffusion limit, f~1 = free-streaming limit
                            Here we can use the slope-limited f [f lower, so much more quickly our ET becomes isotropic], or 'pre-limit' R [less conservative, but may retain more information in some cases] */
                        double f_flux_to_egy = DMIN(DMAX(R*(2.+R)/(6.+3.*R+R*R),0),1); // converges more slowly to optically thin limit (for R>>10)
                        double chi = DMAX(1./3., DMIN(1. , (3. + 4.*f_flux_to_egy*f_flux_to_egy) / (5. + 2.*sqrt(4. - 3.*f_flux_to_egy*f_flux_to_egy))));
                        /* note that, because of how our slope-limiter appears, we don't want to double-count the 1/3 factor for the
                            diffusion limit (since whatever we come up with here will be multiplied by lambda in the relevant forces/etc: therefore 
                            we need to multiply chifac_iso by a power of 3 (because this goes to I/3, but also when lambda->1/3) */
						//chi=1./3.; // pure isotropic
#ifdef RT_RAD_PRESSURE_EDDINGTON
						chi=1.; // pure optically-thin // may be needed for RP problems ???
#endif
                        double chifac_iso=3.*(1-chi)/2., chifac_ot=(3.*chi-1.)/2.;
#ifdef RT_DIFFUSION_CG
                        for(k=0;k<6;k++) {SphP[i].ET[k_freq][k] *= chifac_ot; if(k<3) {SphP[i].ET[k_freq][k] += chifac_iso/3.;}} // diagonal components // (this only makes sense if ET is freq-dependent) [note this will cause instability in the explicit methods; only use for CG where ET is explicitly called and this is done only on global timesteps]
#endif
                        for(k=0;k<3;k++) {SphP[i].Gradients.E_gamma_ET[k_freq][k] = chifac_ot*SphP[i].Gradients.E_gamma_ET[k_freq][k] + chifac_iso/3.*GasGradDataPasser[i].Gradients_E_gamma[k_freq][k];}
#endif // ifdef otvet
                    }
#endif // ifdef fluxlimiter
                    SphP[i].Lambda_FluxLim[k_freq] = lambda;
                }
            }
#endif // ifdef radtransfer
            
            
            /* finally, we need to apply a sensible slope limiter to the gradients, to prevent overshooting */
            double stol = 0.0;
            double h_lim = PPP[i].Hsml;
//#if (defined(MAGNETIC) && defined(COOLING)) ||
            h_lim = DMAX(PPP[i].Hsml,GasGradDataPasser[i].MaxDistance);
//#else
//            h_lim = DMIN(GasGradDataPasser[i].MaxDistance , 4.0*PPP[i].Hsml);
//#endif
            /* fraction of H at which maximum reconstruction is allowed (=0.5 for 'standard'); for pure hydro we can
             be a little more aggresive and the equations are still stable (but this is as far as you want to push it) */
            double a_limiter = 0.25; if(SphP[i].ConditionNumber>100) a_limiter=DMIN(0.5, 0.25 + 0.25 * (SphP[i].ConditionNumber-100)/100);
            
            local_slopelimiter(SphP[i].Gradients.Density,GasGradDataPasser[i].Maxima.Density,GasGradDataPasser[i].Minima.Density,a_limiter,h_lim,stol);
            local_slopelimiter(SphP[i].Gradients.Pressure,GasGradDataPasser[i].Maxima.Pressure,GasGradDataPasser[i].Minima.Pressure,a_limiter,h_lim,stol);
            for(k1=0;k1<3;k1++)
                local_slopelimiter(SphP[i].Gradients.Velocity[k1],GasGradDataPasser[i].Maxima.Velocity[k1],GasGradDataPasser[i].Minima.Velocity[k1],a_limiter,h_lim,stol);
#ifdef DOGRAD_INTERNAL_ENERGY
            local_slopelimiter(SphP[i].Gradients.InternalEnergy,GasGradDataPasser[i].Maxima.InternalEnergy,GasGradDataPasser[i].Minima.InternalEnergy,a_limiter,h_lim,stol);
#endif
#ifdef COSMIC_RAYS
            local_slopelimiter(SphP[i].Gradients.CosmicRayPressure,GasGradDataPasser[i].Maxima.CosmicRayPressure,GasGradDataPasser[i].Minima.CosmicRayPressure,a_limiter,h_lim,stol);
#endif
#ifdef DOGRAD_SOUNDSPEED
            local_slopelimiter(SphP[i].Gradients.SoundSpeed,GasGradDataPasser[i].Maxima.SoundSpeed,GasGradDataPasser[i].Minima.SoundSpeed,a_limiter,h_lim,stol);
#endif
#ifdef TURB_DIFF_METALS
            for(k1=0;k1<NUM_METAL_SPECIES;k1++)
                local_slopelimiter(SphP[i].Gradients.Metallicity[k1],GasGradDataPasser[i].Maxima.Metallicity[k1],GasGradDataPasser[i].Minima.Metallicity[k1],a_limiter,h_lim,stol);
#endif
#ifdef RT_EVOLVE_EDDINGTON_TENSOR
            for(k1=0;k1<N_RT_FREQ_BINS;k1++)
            {
                //local_slopelimiter(SphP[i].Gradients.E_gamma_ET[k1],GasGradDataPasser[i].Maxima.E_gamma[k1],GasGradDataPasser[i].Minima.E_gamma[k1],a_limiter,h_lim,stol);
                local_slopelimiter(GasGradDataPasser[i].Gradients_E_gamma[k1],GasGradDataPasser[i].Maxima.E_gamma[k1],GasGradDataPasser[i].Minima.E_gamma[k1],a_limiter,h_lim,stol);
            }
#endif
#ifdef MAGNETIC
#ifndef CONSTRAINED_GRADIENT_MHD
            double v_tmp = P[i].Mass / SphP[i].Density;
            double tmp_d = sqrt(1.0e-37 + (2. * All.cf_atime/All.cf_afac1 * SphP[i].Pressure*v_tmp*v_tmp) +
                                SphP[i].BPred[0]*SphP[i].BPred[0]+SphP[i].BPred[1]*SphP[i].BPred[1]+SphP[i].BPred[2]*SphP[i].BPred[2]);
            double q = fabs(SphP[i].divB) * PPP[i].Hsml / tmp_d;
            //double q = 80.0 * fabs(SphP[i].divB) * PPP[i].Hsml / tmp_d; // 300,100 work; 30-50 not great; increased coefficient owing to new formulation with wt_i,wt_j in hydro
            double alim2 = a_limiter * (1. + q*q);
            if(alim2 > 0.5) alim2=0.5;            
            
            for(k1=0;k1<3;k1++)
                local_slopelimiter(SphP[i].Gradients.B[k1],GasGradDataPasser[i].Maxima.B[k1],GasGradDataPasser[i].Minima.B[k1],alim2,h_lim,stol);
#endif
#if defined(DIVBCLEANING_DEDNER) && !defined(CONSTRAINED_GRADIENT_MHD_MIDPOINT)
            local_slopelimiter(SphP[i].Gradients.Phi,GasGradDataPasser[i].Maxima.Phi,GasGradDataPasser[i].Minima.Phi,a_limiter,h_lim,stol);
#endif
#endif

           
            

#ifdef TURB_DIFFUSION
            {
                /* estimate local turbulent diffusion coefficient from velocity gradients using Smagorinsky mixing model: 
                    we do this after slope-limiting to prevent the estimated velocity gradients from being unphysically large */
                double h_turb = Get_Particle_Size(i);
                if(h_turb > 0)
                {
                    // overall normalization //
                    double C_Smagorinsky_Lilly = 0.15; // this is the standard Smagorinsky-Lilly constant, calculated from Kolmogorov theory: should be 0.1-0.2 //
                    double turb_prefactor = All.TurbDiffusion_Coefficient * C_Smagorinsky_Lilly*C_Smagorinsky_Lilly * sqrt(2.0);
                    // then scale with inter-particle spacing //
                    turb_prefactor *= h_turb*h_turb;
                    // calculate frobenius norm of symmetric shear velocity gradient tensor //
                    double shear_factor = sqrt((1./2.)*((SphP[i].Gradients.Velocity[1][0]+SphP[i].Gradients.Velocity[0][1]) *
                                                        (SphP[i].Gradients.Velocity[1][0]+SphP[i].Gradients.Velocity[0][1]) +
                                                        (SphP[i].Gradients.Velocity[2][0]+SphP[i].Gradients.Velocity[0][2]) *
                                                        (SphP[i].Gradients.Velocity[2][0]+SphP[i].Gradients.Velocity[0][2]) +
                                                        (SphP[i].Gradients.Velocity[2][1]+SphP[i].Gradients.Velocity[1][2]) *
                                                        (SphP[i].Gradients.Velocity[2][1]+SphP[i].Gradients.Velocity[1][2])) +
                                               (2./3.)*((SphP[i].Gradients.Velocity[0][0]*SphP[i].Gradients.Velocity[0][0] +
                                                         SphP[i].Gradients.Velocity[1][1]*SphP[i].Gradients.Velocity[1][1] +
                                                         SphP[i].Gradients.Velocity[2][2]*SphP[i].Gradients.Velocity[2][2]) -
                                                        (SphP[i].Gradients.Velocity[1][1]*SphP[i].Gradients.Velocity[2][2] +
                                                         SphP[i].Gradients.Velocity[0][0]*SphP[i].Gradients.Velocity[1][1] +
                                                         SphP[i].Gradients.Velocity[0][0]*SphP[i].Gradients.Velocity[2][2])));
                    // slope-limit and convert to physical units //
                    double shearfac_max = 0.5 * sqrt(SphP[i].VelPred[0]*SphP[i].VelPred[0]+SphP[i].VelPred[1]*SphP[i].VelPred[1]+SphP[i].VelPred[2]*SphP[i].VelPred[2]) / h_turb;
                    shear_factor = DMIN(shear_factor , shearfac_max); // physical units altogether //
                    // ok, combine to get the diffusion coefficient //
                    SphP[i].TD_DiffCoeff = turb_prefactor * shear_factor;
                } else {
                    SphP[i].TD_DiffCoeff = 0;
                }
#ifdef TURB_DIFF_ENERGY
                SphP[i].Kappa_Conduction = All.ConductionCoeff * SphP[i].TD_DiffCoeff * SphP[i].Density * All.cf_a3inv;
#endif
#ifdef TURB_DIFF_VELOCITY
                SphP[i].Eta_ShearViscosity = All.ShearViscosityCoeff * SphP[i].TD_DiffCoeff * SphP[i].Density * All.cf_a3inv;
                SphP[i].Zeta_BulkViscosity = All.BulkViscosityCoeff * SphP[i].TD_DiffCoeff * SphP[i].Density * All.cf_a3inv;
#endif
            }
#endif
            
            
            
#ifdef COSMIC_RAYS
            /* note that because of the way this depends on the gradient scale-length, we should calculate it -after- the slope-limiters are applied */
            if(SphP[i].Density > 0)
            {
                SphP[i].CosmicRayDiffusionCoeff = 0;
                double CRPressureGradScaleLength = Get_CosmicRayGradientLength(i);
#ifndef COSMIC_RAYS_DISABLE_STREAMING
                /* self-consistently calculate the diffusion coefficients for cosmic ray fluids; first the streaming part of this (kappa~v_stream*L_CR_grad)
                 following e.g. Wentzel 1968, Skilling 1971, 1975, Holman 1979, as updated in Kulsrud 2005, Yan & Lazarian 2008, Ensslin 2011 */
                double v_streaming = Get_CosmicRayStreamingVelocity(i);
                /* the diffusivity is now just the product of these two coefficients */
                SphP[i].CosmicRayDiffusionCoeff += v_streaming * CRPressureGradScaleLength; /* all physical units */
#endif
#ifndef COSMIC_RAYS_DISABLE_DIFFUSION
                /* now we calculate the 'traditional' diffusion part of this: kappa~v_CR*r_gyro * B_bulk^2/(B_random[scale~r_gyro]^2)
                 v_CR~c, r_gyro~p*c/(Z*e*B)~1e12 cm * RGV *(3 muG/B)  (RGV~1 is the magnetic rigidity). assuming a Kolmogorov spectrum, this
                 gives kappa~3e28 * (R_driving/100pc)^(2/3) * RGV^(1/3) * (B/3 muG)^(-1/3)
                 this follows Jokipii 1966 */
                double kappa_diff = 3.e28 / (GAMMA_COSMICRAY_MINUS1 * All.UnitVelocity_in_cm_per_s * All.UnitLength_in_cm / All.HubbleParam); /* converts from CGS to code units */
                double R_GV=1.0, p_scale=0, b_muG=1;
#ifdef MAGNETIC
                double gizmo2gauss = sqrt(4.*M_PI*All.UnitPressure_in_cgs*All.HubbleParam*All.HubbleParam) / All.UnitMagneticField_in_gauss;
                double b2_mag = 0.0;
                for(k=0;k<3;k++) {b2_mag += Get_Particle_BField(i,k) * Get_Particle_BField(i,k);}
                b_muG=sqrt(DMAX(b2_mag,0)) * All.cf_a2inv * gizmo2gauss / 1.0e-6; /* B-field in units of physical microGauss */
                b_muG = sqrt(b_muG*b_muG + 1.e-6);

#if 0
                /* need to determine the random part of B^2 at the resolution scale: for this we use a Smagorinski-type approximation:
                 note: this version of the algorithm is deprecated; it is too noisy, even in resolved flows, since many regions have
                 locally 'flat' or 'steep' B-field gradients, giving a misleading estimate of the local variance */
                p_scale = Get_Particle_Size(i); // this is simply the resolution scale (code units), at which the gradient is evaluated //
                double BGrad_mag = 0.0;
                int k2; for(k=0;k<3;k++) {for(k2=0;k2<3;k2++) {BGrad_mag += SphP[i].Gradients.B[k][k2] * SphP[i].Gradients.B[k][k2];}}
                kappa_diff *= b2_mag / (1.e-37 + p_scale * p_scale * BGrad_mag); // should be dimensionless //
#else
                /* alternatively, we don't explicitly use the local B-gradient, but assume a cascade with a driving length equal to 
                    the pressure gradient scale length */
                p_scale = 0.0; for(k=0;k<3;k++) {p_scale += SphP[i].Gradients.Pressure[k]*SphP[i].Gradients.Pressure[k];}
                p_scale = SphP[i].Pressure / (1.e-33 + sqrt(p_scale));
                double p_scale_min = 0.01 * Get_Particle_Size(i); // sets a 'floor' at some multiple of the particle size (unresolved below this) //
                p_scale = sqrt(p_scale_min*p_scale_min + p_scale*p_scale);
                double p_scale_max = 100.*PPP[i].Hsml; // sets a maximum beyond which we have no meaningful information //
                p_scale = 1./(1./p_scale + 1./p_scale_max); // code units here //
#endif
#else
                /* define the driving scale by the pressure scale length */
                for(k=0;k<3;k++) {p_scale += SphP[i].Gradients.Pressure[k]*SphP[i].Gradients.Pressure[k];}
                p_scale = SphP[i].Pressure / (1.e-33 + sqrt(p_scale));
                double p_scale_min = 0.01 * Get_Particle_Size(i); /* sets a 'floor' at some multiple of the particle size (unresolved below this) */
                p_scale = sqrt(p_scale_min*p_scale_min + p_scale*p_scale);
                double p_scale_max = 1000.*PPP[i].Hsml; /* sets a maximum beyond which we have no meaningful information */
                p_scale = 1./(1./p_scale + 1./p_scale_max); /* code units here */
                /* here we need sqrt(P/4e-14 erg/cm^3); convert pressure to physical units and multiply this out */
                b_muG = sqrt( SphP[i].Pressure * All.cf_a3inv * All.UnitPressure_in_cgs*All.HubbleParam*All.HubbleParam / 4.0e-14 );
#endif
                p_scale *= (All.UnitLength_in_cm / All.HubbleParam * All.cf_atime) / (3.086e21); /* physical pressure scale length in units of kpc */
                kappa_diff *= pow( p_scale * p_scale * R_GV / b_muG, 1./3.); /* these should all be dimensionless here */
                /* now we apply a limiter to prevent the coefficient from becoming too large: cosmic rays cannot stream/diffuse with v_diff > c */
                double kappa_diff_vel = kappa_diff * GAMMA_COSMICRAY_MINUS1 / CRPressureGradScaleLength * All.UnitVelocity_in_cm_per_s;
                kappa_diff *= 1 / (1 + kappa_diff_vel/3.e10); /* caps maximum here */
                SphP[i].CosmicRayDiffusionCoeff += kappa_diff; /* should be in physical units */
#endif                
#ifdef COSMIC_RAYS_DIFFUSION_CONSTANT
                SphP[i].CosmicRayDiffusionCoeff = All.CosmicRayDiffusionCoeff / GAMMA_COSMICRAY_MINUS1;
#else
                SphP[i].CosmicRayDiffusionCoeff *= All.CosmicRayDiffusionCoeff;
#endif
                if((SphP[i].CosmicRayDiffusionCoeff<=0)||(isnan(SphP[i].CosmicRayDiffusionCoeff))) {SphP[i].CosmicRayDiffusionCoeff=0;}
            } else {
                SphP[i].CosmicRayDiffusionCoeff = 0;
            }
#endif
        }
    
    
    /* free the temporary structure we created for the MinMax and additional data passing */
    myfree(GasGradDataPasser);
    
    /* collect some timing information */
    t1 = WallclockTime = my_second();
    timeall += timediff(t0, t1);
    timecomp = timecomp1 + timecomp2;
    timewait = timewait1 + timewait2;
    timecomm = timecommsumm1 + timecommsumm2;
    
    CPU_Step[CPU_DENSCOMPUTE] += timecomp;
    CPU_Step[CPU_DENSWAIT] += timewait;
    CPU_Step[CPU_DENSCOMM] += timecomm;
    CPU_Step[CPU_DENSMISC] += timeall - (timecomp + timewait + timecomm);
}


int GasGrad_evaluate(int target, int mode, int *exportflag, int *exportnodecount, int *exportindex,
                     int *ngblist, int gradient_iteration)
{
    int startnode, numngb, listindex = 0;
    int j, k, k2, n, swap_to_j;
    double hinv, hinv3, hinv4, r2, u, hinv_j, hinv3_j, hinv4_j;
    struct kernel_GasGrad kernel;
    struct GasGraddata_in local;
    struct GasGraddata_out out;
    struct GasGraddata_out_iter out_iter;
    if(gradient_iteration==0)
    {
        memset(&out, 0, sizeof(struct GasGraddata_out));
    } else {
        memset(&out_iter, 0, sizeof(struct GasGraddata_out_iter));
    }
    memset(&kernel, 0, sizeof(struct kernel_GasGrad));
    
    if(mode == 0)
        particle2in_GasGrad(&local, target, gradient_iteration);
    else
        local = GasGradDataGet[target];
    
    /* check if we should bother doing a neighbor loop */
    if(local.Hsml <= 0) return 0;
    if(local.Mass == 0) return 0;
    if(gradient_iteration == 0)
        if(local.GQuant.Density <= 0) return 0;
    
    /* now set particle-i centric quantities so we don't do it inside the loop */
    kernel.h_i = local.Hsml;
    double h2_i = kernel.h_i*kernel.h_i;
    kernel_hinv(kernel.h_i, &hinv, &hinv3, &hinv4);
    int sph_gradients_flag_i = 0;
    int sph_gradients_flag_j = 0;
    if(local.Mass < 0) {sph_gradients_flag_i=1; local.Mass*=-1;}
    double V_i;
    V_i = local.Mass / local.GQuant.Density;
    
    int kernel_mode_i = -1; // only need to calculate wk, by default
    if(sph_gradients_flag_i) kernel_mode_i = 0; // for sph, only need dwk
#if defined(HYDRO_SPH)
    kernel_mode_i = 0; // for some circumstances, we require both wk and dwk //
#endif
    
    
    /* Now start the actual neighbor computation for this particle */
    
    if(mode == 0)
    {
        startnode = All.MaxPart;	/* root node */
    }
    else
    {
        startnode = GasGradDataGet[target].NodeList[0];
        startnode = Nodes[startnode].u.d.nextnode;	/* open it */
    }
    
    while(startnode >= 0)
    {
        while(startnode >= 0)
        {
            
            numngb = ngb_treefind_pairs_threads(local.Pos, kernel.h_i, target, &startnode, mode, exportflag, exportnodecount, exportindex, ngblist);
            
            if(numngb < 0)
                return -1;
            
            for(n = 0; n < numngb; n++)
            {
                j = ngblist[n];
                int TimeStep_J = (P[j].TimeBin ? (((integertime) 1) << P[j].TimeBin) : 0);
#ifndef SHEARING_BOX // (shearing box means the fluxes at the boundaries are not actually symmetric, so can't do this) //
                if(local.Timestep > TimeStep_J) continue; /* compute from particle with smaller timestep */
                /* use relative positions to break degeneracy */
                if(local.Timestep == TimeStep_J)
                {
                    int n0=0; if(local.Pos[n0] == P[j].Pos[n0]) {n0++; if(local.Pos[n0] == P[j].Pos[n0]) n0++;}
                    if(local.Pos[n0] < P[j].Pos[n0]) continue;
                }
                swap_to_j = TimeBinActive[P[j].TimeBin];
#else
                swap_to_j = 0;
#endif
                if(P[j].Mass <= 0) continue;
                if(SphP[j].Density <= 0) continue;
                
                kernel.dp[0] = local.Pos[0] - P[j].Pos[0];
                kernel.dp[1] = local.Pos[1] - P[j].Pos[1];
                kernel.dp[2] = local.Pos[2] - P[j].Pos[2];
#ifdef PERIODIC			/*  now find the closest image in the given box size  */
                NEAREST_XYZ(kernel.dp[0],kernel.dp[1],kernel.dp[2],1);
#endif
                r2 = kernel.dp[0] * kernel.dp[0] + kernel.dp[1] * kernel.dp[1] + kernel.dp[2] * kernel.dp[2];
                double h_j = PPP[j].Hsml;
                if(r2 <= 0) continue;
                if((r2 >= h2_i) && (r2 >= h_j * h_j)) continue;
                
                kernel.r = sqrt(r2);
                if(kernel.r < kernel.h_i)
                {
                    u = kernel.r * hinv;
                    kernel_main(u, hinv3, hinv4, &kernel.wk_i, &kernel.dwk_i, kernel_mode_i);
                }
                else
                {
                    kernel.dwk_i = kernel.wk_i = 0;
                }
#if defined(CONSTRAINED_GRADIENT_MHD)
                if(kernel.r < h_j)
#else
                if((kernel.r < h_j) && (swap_to_j))
#endif
                {
                    /* ok, we need the j-particle weights, but first check what kind of gradient we are calculating */
                    sph_gradients_flag_j = SHOULD_I_USE_SPH_GRADIENTS(SphP[j].ConditionNumber);
                    int kernel_mode_j;
#if defined(HYDRO_SPH)
                    kernel_mode_j = 0; // for some circumstances, we require both wk and dwk //
#else
                    if(sph_gradients_flag_j) {kernel_mode_j=0;} else {kernel_mode_j=-1;}
#endif
                    kernel_hinv(h_j, &hinv_j, &hinv3_j, &hinv4_j);
                    u = kernel.r * hinv_j;
                    kernel_main(u, hinv3_j, hinv4_j, &kernel.wk_j, &kernel.dwk_j, kernel_mode_j);
                }
                else
                {
                    kernel.dwk_j = kernel.wk_j = 0;
                }
                
                
#if defined(CONSTRAINED_GRADIENT_MHD)
                double V_j = P[j].Mass / SphP[j].Density;
                double Face_Area_Vec[3];
                double wt_i,wt_j;
#ifdef COOLING
                //wt_i=wt_j = 2.*V_i*V_j / (V_i + V_j); // more conservatively, could use DMIN(V_i,V_j), but that is less accurate
                if((fabs(V_i-V_j)/DMIN(V_i,V_j))/NUMDIMS > 1.25) {wt_i=wt_j=2.*V_i*V_j/(V_i+V_j);} else {wt_i=V_i; wt_j=V_j;}
#else
                //wt_i=wt_j = (V_i*PPP[j].Hsml + V_j*local.Hsml) / (local.Hsml+PPP[j].Hsml); // should these be H, or be -effective sizes- //
                if((fabs(V_i-V_j)/DMIN(V_i,V_j))/NUMDIMS > 1.50) {wt_i=wt_j=(V_i*PPP[j].Hsml+V_j*local.Hsml)/(local.Hsml+PPP[j].Hsml);} else {wt_i=V_i; wt_j=V_j;}
#endif
                for(k=0;k<3;k++)
                {
                    /* calculate the face area between the particles (must match what is done in the actual hydro routine! */
                    Face_Area_Vec[k] = kernel.wk_i * wt_i * (local.NV_T[k][0]*kernel.dp[0] + local.NV_T[k][1]*kernel.dp[1] + local.NV_T[k][2]*kernel.dp[2])
                                     + kernel.wk_j * wt_j * (SphP[j].NV_T[k][0]*kernel.dp[0] + SphP[j].NV_T[k][1]*kernel.dp[1] + SphP[j].NV_T[k][2]*kernel.dp[2]);
                    if(All.ComovingIntegrationOn) {Face_Area_Vec[k] *= All.cf_atime*All.cf_atime;} /* Face_Area_Norm has units of area, need to convert to physical */
                    /* on the first pass, we need to save the face information to be used to correct the gradients; this only needs to be done once */
                    if(gradient_iteration == 0)
                    {
                        out.Face_Area[k] += Face_Area_Vec[k];
                        if(swap_to_j) SphP[j].Face_Area[k] -= Face_Area_Vec[k];
                        
                        for(k2=0;k2<3;k2++)
                        {
                            double q = -0.5 * Face_Area_Vec[k] * kernel.dp[k2];
                            out.FaceCrossX[k][k2] += q;
                            if(swap_to_j) GasGradDataPasser[j].FaceCrossX[k][k2] += q;
                        }
                    }
                    
                    /* now use the gradients to construct the B_L,R states */
                    double Bjk = Get_Particle_BField(j,k);
                    double db_c=0, db_cR=0;
                    for(k2=0;k2<3;k2++)
                    {
                        db_c += 0.5 * SphP[j].Gradients.B[k][k2] * kernel.dp[k2];
                        db_cR -= 0.5 * local.BGrad[k][k2]  * kernel.dp[k2];
                    }
                    
                    /* now we apply our slope-limiter to the B_L,R reconstruction */
                    double Q_L, Q_R;
                    if(Bjk == local.GQuant.B[k])
                    {
                        Q_L = Q_R = Bjk;
                    } else {
                        Q_L = Bjk + db_c;
                        Q_R = local.GQuant.B[k] + db_cR;
                        double Qmax, Qmin, Qmed = 0.5*(local.GQuant.B[k] + Bjk);
                        if(local.GQuant.B[k] < Bjk) {Qmax=Bjk; Qmin=local.GQuant.B[k];} else {Qmax=local.GQuant.B[k]; Qmin=Bjk;}
                        double fac = CONSTRAINED_GRADIENT_MHD_FAC_MINMAX * (Qmax-Qmin);
                        fac += CONSTRAINED_GRADIENT_MHD_FAC_MAX_PM * fabs(Qmed);
                        double Qmax_eff = Qmax + fac;
                        double Qmin_eff = Qmin - fac;
                        fac = CONSTRAINED_GRADIENT_MHD_FAC_MEDDEV * (Qmax-Qmin);
                        fac += CONSTRAINED_GRADIENT_MHD_FAC_MED_PM * fabs(Qmed);
                        double Qmed_max = Qmed + fac;
                        double Qmed_min = Qmed - fac;
                        if(Qmed_max>Qmax_eff) Qmed_max=Qmax_eff;
                        if(Qmed_min<Qmin_eff) Qmed_min=Qmin_eff;
                        if(local.GQuant.B[k] < Bjk)
                        {
                            if(Q_L>Qmax_eff) Q_L=Qmax_eff;
                            if(Q_L<Qmed_min) Q_L=Qmed_min;
                            if(Q_R<Qmin_eff) Q_R=Qmin_eff;
                            if(Q_R>Qmed_max) Q_R=Qmed_max;
                        } else {
                            if(Q_L<Qmin_eff) Q_L=Qmin_eff;
                            if(Q_L>Qmed_max) Q_L=Qmed_max;
                            if(Q_R>Qmax_eff) Q_R=Qmax_eff;
                            if(Q_R<Qmed_min) Q_R=Qmed_min;
                        }
                    }
                    
                    if(gradient_iteration==0)
                    {
                        out.FaceDotB += Face_Area_Vec[k] * (local.GQuant.B[k] + Q_L);
                    } else {
                        out_iter.FaceDotB += Face_Area_Vec[k] * (local.GQuant.B[k] + Q_L);
                    }
                    if(swap_to_j) GasGradDataPasser[j].FaceDotB -= Face_Area_Vec[k] * (Bjk + Q_R);
                }
                
#if defined(CONSTRAINED_GRADIENT_MHD_MIDPOINT)
                /* this will fit the gradient at the -midpoint- as opposed to at the j locations, i.e.
                 attempting to minimize the quantity phi_L - phi_R, at face locations */
                double dphi = Get_Particle_PhiField(j) - local.GQuant.Phi;
                if(gradient_iteration == 0)
                {
                    MINMAX_CHECK(dphi,out.Minima.Phi,out.Maxima.Phi);
                    if(swap_to_j) {MINMAX_CHECK(-dphi,GasGradDataPasser[j].Minima.Phi,GasGradDataPasser[j].Maxima.Phi);}
                }

                // dphi = phi_j - phi_i :: if phi_i = 0, dphi = phi_j //
                double dphi_grad_j = 0, dphi_grad_i = 0;
                for(k=0;k<3;k++)
                {
                    dphi_grad_j += 0.5 * kernel.dp[k] * SphP[j].Gradients.Phi[k];
                    dphi_grad_i -= 0.5 * kernel.dp[k] * local.PhiGrad[k];
                }
                if(dphi > 0)
                {
                    if(dphi_grad_j>0) {dphi_grad_j=0;} else {if(dphi_grad_j<0.5*dphi) dphi_grad_j=0.5*dphi;}
                    if(dphi_grad_i<0) {dphi_grad_i=0;} else {if(dphi_grad_i>0.5*dphi) dphi_grad_i=0.5*dphi;}
                } else {
                    if(dphi_grad_j<0) {dphi_grad_j=0;} else {if(dphi_grad_j>0.5*dphi) dphi_grad_j=0.5*dphi;}
                    if(dphi_grad_i>0) {dphi_grad_i=0;} else {if(dphi_grad_i<0.5*dphi) dphi_grad_i=0.5*dphi;}
                }
                double dphi_j = dphi + dphi_grad_j;
                double dphi_i = dphi - dphi_grad_i;
                if(sph_gradients_flag_i) {dphi_j *= -2*kernel.wk_i;} else {dphi_j *= kernel.dwk_i/kernel.r * P[j].Mass;}
                if(sph_gradients_flag_j) {dphi_i *= -2*kernel.wk_j;} else {dphi_i *= kernel.dwk_j/kernel.r * local.Mass;}
                if(gradient_iteration == 0) {for(k=0;k<3;k++) {out.Gradients[k].Phi += dphi_j * kernel.dp[k];}} else {for(k=0;k<3;k++) {out_iter.PhiGrad[k] += dphi_j * kernel.dp[k];}}
                if(swap_to_j) {for(k=0;k<3;k++) {GasGradDataPasser[j].PhiGrad[k] += dphi_i * kernel.dp[k];}}
#endif
#endif
                
                if(gradient_iteration == 0)
                {
                    /* ------------------------------------------------------------------------------------------------ */
                    /* DIFFERENCE & SLOPE LIMITING: need to check maxima and minima of particle values in the kernel, to avoid
                     'overshoot' with our gradient estimators. this check should be among all interacting pairs */

                    if(kernel.r > out.MaxDistance) {out.MaxDistance = kernel.r;}
                    if(swap_to_j) {if(kernel.r > GasGradDataPasser[j].MaxDistance) {GasGradDataPasser[j].MaxDistance = kernel.r;}}

                    double dd = SphP[j].Density - local.GQuant.Density;
                    MINMAX_CHECK(dd,out.Minima.Density,out.Maxima.Density);
                    if(swap_to_j) {MINMAX_CHECK(-dd,GasGradDataPasser[j].Minima.Density,GasGradDataPasser[j].Maxima.Density);}

                    double dp = SphP[j].Pressure - local.GQuant.Pressure;
                    MINMAX_CHECK(dp,out.Minima.Pressure,out.Maxima.Pressure);
                    if(swap_to_j) {MINMAX_CHECK(-dp,GasGradDataPasser[j].Minima.Pressure,GasGradDataPasser[j].Maxima.Pressure);}

                    double dv[3];
                    for(k=0;k<3;k++)
                    {
                        dv[k] = SphP[j].VelPred[k] - local.GQuant.Velocity[k];
#ifdef SHEARING_BOX
                        if(k==SHEARING_BOX_PHI_COORDINATE)
                        {
                            if(local.Pos[0] - P[j].Pos[0] > +boxHalf_X) {dv[k] -= Shearing_Box_Vel_Offset;}
                            if(local.Pos[0] - P[j].Pos[0] < -boxHalf_X) {dv[k] += Shearing_Box_Vel_Offset;}
                        }
#endif
                        MINMAX_CHECK(dv[k],out.Minima.Velocity[k],out.Maxima.Velocity[k]);
                        if(swap_to_j) {MINMAX_CHECK(-dv[k],GasGradDataPasser[j].Minima.Velocity[k],GasGradDataPasser[j].Maxima.Velocity[k]);}
                    }

#ifdef DOGRAD_INTERNAL_ENERGY
                    double du = SphP[j].InternalEnergyPred - local.GQuant.InternalEnergy;
                    MINMAX_CHECK(du,out.Minima.InternalEnergy,out.Maxima.InternalEnergy);
                    if(swap_to_j) {MINMAX_CHECK(-du,GasGradDataPasser[j].Minima.InternalEnergy,GasGradDataPasser[j].Maxima.InternalEnergy);}
#endif
#ifdef COSMIC_RAYS
                    double dpCR = Get_Particle_CosmicRayPressure(j) - local.GQuant.CosmicRayPressure;
                    MINMAX_CHECK(dpCR,out.Minima.CosmicRayPressure,out.Maxima.CosmicRayPressure);
                    if(swap_to_j) {MINMAX_CHECK(-dpCR,GasGradDataPasser[j].Minima.CosmicRayPressure,GasGradDataPasser[j].Maxima.CosmicRayPressure);}
#endif
#ifdef DOGRAD_SOUNDSPEED
                    double dc = Particle_effective_soundspeed_i(j) - local.GQuant.SoundSpeed;
                    MINMAX_CHECK(dc,out.Minima.SoundSpeed,out.Maxima.SoundSpeed);
                    if(swap_to_j) {MINMAX_CHECK(-dc,GasGradDataPasser[j].Minima.SoundSpeed,GasGradDataPasser[j].Maxima.SoundSpeed);}
#endif
#ifdef MAGNETIC
                    double Bj[3],dB[3];
                    for(k=0;k<3;k++)
                    {
                        Bj[k] = Get_Particle_BField(j,k);
                        dB[k] = Bj[k] - local.GQuant.B[k];
                        MINMAX_CHECK(dB[k],out.Minima.B[k],out.Maxima.B[k]);
                        if(swap_to_j) {MINMAX_CHECK(-dB[k],GasGradDataPasser[j].Minima.B[k],GasGradDataPasser[j].Maxima.B[k]);}
                    }
#endif
#if defined(DIVBCLEANING_DEDNER) && !defined(CONSTRAINED_GRADIENT_MHD_MIDPOINT)
                    double dphi = Get_Particle_PhiField(j) - local.GQuant.Phi;
                    MINMAX_CHECK(dphi,out.Minima.Phi,out.Maxima.Phi);
                    if(swap_to_j) {MINMAX_CHECK(-dphi,GasGradDataPasser[j].Minima.Phi,GasGradDataPasser[j].Maxima.Phi);}
#endif
#ifdef TURB_DIFF_METALS
                    double dmetal[NUM_METAL_SPECIES];
                    for(k = 0; k < NUM_METAL_SPECIES; k++)
                    {
                        dmetal[k] = P[j].Metallicity[k] - local.GQuant.Metallicity[k];
                        MINMAX_CHECK(dmetal[k],out.Minima.Metallicity[k],out.Maxima.Metallicity[k]);
                        if(swap_to_j) {MINMAX_CHECK(-dmetal[k],GasGradDataPasser[j].Minima.Metallicity[k],GasGradDataPasser[j].Maxima.Metallicity[k]);}
                    }
#endif
#ifdef RT_EVOLVE_EDDINGTON_TENSOR
                    double dnET[N_RT_FREQ_BINS][6];
                    double dn[N_RT_FREQ_BINS];
                    double V_i_inv = 1/V_i, V_j_inv = SphP[j].Density/P[j].Mass;
                    for(k = 0; k < N_RT_FREQ_BINS; k++)
                    {
                        int k_dE; for(k_dE=0;k_dE<6;k_dE++) {dnET[k][k_dE] = SphP[j].E_gamma_Pred[k]*SphP[j].ET[k][k_dE]*V_j_inv - local.GQuant.E_gamma[k]*local.GQuant.E_gamma_ET[k][k_dE]*V_i_inv;}
                        dn[k] = SphP[j].E_gamma_Pred[k]*V_j_inv - local.GQuant.E_gamma[k]*V_i_inv;
                        MINMAX_CHECK(dn[k],out.Minima.E_gamma[k],out.Maxima.E_gamma[k]);
                        if(swap_to_j) {MINMAX_CHECK(-dn[k],GasGradDataPasser[j].Minima.E_gamma[k],GasGradDataPasser[j].Maxima.E_gamma[k]);}
                    }
#endif
                    /* end of difference and slope-limiter (min/max) block */
                    /* ------------------------------------------------------------------------------------------------ */


                    /* ------------------------------------------------------------------------------------------------ */
                    /*  Here we insert additional operations we want to fit into the gradients loop. at the moment, all of these 
                            are SPH-specific */
#ifdef HYDRO_SPH
#ifdef SPHAV_CD10_VISCOSITY_SWITCH
                    out.alpha_limiter += NV_MYSIGN(SphP[j].NV_DivVel) * P[j].Mass * kernel.wk_i;
                    SphP[j].alpha_limiter += NV_MYSIGN(local.NV_DivVel) * local.Mass * kernel.wk_j;
#endif
#ifdef MAGNETIC
                    double mji_dwk_r = P[j].Mass * kernel.dwk_i / kernel.r;
                    double mij_dwk_r = local.Mass * kernel.dwk_j / kernel.r;
                    for(k=0;k<3;k++)
                    {
                        for(k2=0;k2<3;k2++)
                        {
                            out.DtB[k] += local.GQuant.B[k2] * mji_dwk_r * kernel.dp[k2] * dv[k];
                            if(swap_to_j) SphP[j].DtB[k] += Bj[k2] * mij_dwk_r * kernel.dp[k2] * dv[k];
                        }
#ifdef DIVBCLEANING_DEDNER
                        out.divB += dB[k] * kernel.dp[k] * mji_dwk_r;
                        if(swap_to_j) SphP[j].divB += dB[k] * kernel.dp[k] * mij_dwk_r;
#endif
                    }
#endif
#endif
                    /* end of additional/miscellaneous operators block */
                    /* ------------------------------------------------------------------------------------------------ */

                    
                    /* ------------------------------------------------------------------------------------------------ */
                    /* Finally, save actual output for GRADIENTS */
                    
                    /* first do particle i */
                    if(kernel.r < kernel.h_i)
                    {
                        if(sph_gradients_flag_i) {kernel.wk_i = -kernel.dwk_i/kernel.r * P[j].Mass;} // sph-like weights for gradients //
                        for(k=0;k<3;k++)
                        {
                            double wk_xyz_i = -kernel.wk_i * kernel.dp[k]; /* sign is important here! */
                            out.Gradients[k].Density += wk_xyz_i * dd;
                            out.Gradients[k].Pressure += wk_xyz_i * dp;
                            for(k2=0;k2<3;k2++) {out.Gradients[k].Velocity[k2] += wk_xyz_i * dv[k2];}
#ifdef DOGRAD_INTERNAL_ENERGY
                            out.Gradients[k].InternalEnergy += wk_xyz_i * du;
#endif
#ifdef COSMIC_RAYS
                            out.Gradients[k].CosmicRayPressure += wk_xyz_i * dpCR;
#endif
#ifdef DOGRAD_SOUNDSPEED
                            out.Gradients[k].SoundSpeed += wk_xyz_i * dc;
#endif
#ifdef MAGNETIC
                            for(k2=0;k2<3;k2++) {out.Gradients[k].B[k2] += wk_xyz_i * dB[k2];}
#if defined(DIVBCLEANING_DEDNER) && !defined(CONSTRAINED_GRADIENT_MHD_MIDPOINT)
                            out.Gradients[k].Phi += wk_xyz_i * dphi;
#endif
#endif
#ifdef TURB_DIFF_METALS
                            for(k2=0;k2<NUM_METAL_SPECIES;k2++) {out.Gradients[k].Metallicity[k2] += wk_xyz_i * dmetal[k2];}
#endif
#ifdef RT_EVOLVE_EDDINGTON_TENSOR
                            for(k2=0;k2<N_RT_FREQ_BINS;k2++) 
                            {
                            	out.Gradients[k].E_gamma[k2] += wk_xyz_i * dn[k2];
                            	int k_et; for(k_et=0;k_et<6;k_et++) out.Gradients[k].E_gamma_ET[k2][k_et] += wk_xyz_i * dnET[k2][k_et];
                            } 
#endif
                        }
                    }
                    
                    /* next do particle j */
                    if((kernel.r < h_j) && (swap_to_j))
                    {
                        if(sph_gradients_flag_j) {kernel.wk_j = -kernel.dwk_j/kernel.r * local.Mass;} // sph-like weights for gradients //
                        for(k=0;k<3;k++)
                        {
                            double wk_xyz_j = -kernel.wk_j * kernel.dp[k]; /* sign is important here! (note dp-dd signs cancel) */
                            SphP[j].Gradients.Density[k] += wk_xyz_j * dd;
                            SphP[j].Gradients.Pressure[k] += wk_xyz_j * dp;
                            for(k2=0;k2<3;k2++) {SphP[j].Gradients.Velocity[k2][k] += wk_xyz_j * dv[k2];}
#ifdef DOGRAD_INTERNAL_ENERGY
                            SphP[j].Gradients.InternalEnergy[k] += wk_xyz_j * du;
#endif
#ifdef COSMIC_RAYS
                            SphP[j].Gradients.CosmicRayPressure[k] += wk_xyz_j * dpCR;
#endif
#ifdef DOGRAD_SOUNDSPEED
                            SphP[j].Gradients.SoundSpeed[k] += wk_xyz_j * dc;
#endif
#ifdef MAGNETIC
#ifdef CONSTRAINED_GRADIENT_MHD
                            for(k2=0;k2<3;k2++) {GasGradDataPasser[j].BGrad[k2][k] += wk_xyz_j * dB[k2];}
#else
                            for(k2=0;k2<3;k2++) {SphP[j].Gradients.B[k2][k] += wk_xyz_j * dB[k2];}
#endif
#if defined(DIVBCLEANING_DEDNER) && !defined(CONSTRAINED_GRADIENT_MHD_MIDPOINT)
                            SphP[j].Gradients.Phi[k] += wk_xyz_j * dphi;
#endif
#endif
#ifdef TURB_DIFF_METALS
                            for(k2=0;k2<NUM_METAL_SPECIES;k2++) {SphP[j].Gradients.Metallicity[k2][k] += wk_xyz_j * dmetal[k2];}
#endif
#ifdef RT_EVOLVE_EDDINGTON_TENSOR
                            for(k2=0;k2<N_RT_FREQ_BINS;k2++) 
                            {
                            	GasGradDataPasser[j].Gradients_E_gamma[k2][k] += wk_xyz_j * dn[k2];
								/* below we have the gradient dotted into the Eddington tensor (more complicated than a scalar gradient, but should recover full anisotropy */
								int k_freq=k2,k_xyz,j_xyz,i_xyz=k,k_et_loop[3]; // recall, for ET: 0=xx,1=yy,2=zz,3=xy,4=yz,5=xz
								for(k_xyz=0;k_xyz<3;k_xyz++)
								{
									if(k_xyz==0) {k_et_loop[0]=0; k_et_loop[1]=3; k_et_loop[2]=5;}
									if(k_xyz==1) {k_et_loop[0]=3; k_et_loop[1]=1; k_et_loop[2]=4;}
									if(k_xyz==2) {k_et_loop[0]=5; k_et_loop[1]=4; k_et_loop[2]=2;}
									for(j_xyz=0;j_xyz<3;j_xyz++)
									{
										SphP[j].Gradients.E_gamma_ET[k_freq][k_xyz] += SphP[j].NV_T[j_xyz][i_xyz] * wk_xyz_j * dnET[k_freq][k_et_loop[j_xyz]];
									}
								}
                            }
#endif
                        }
                    }

                    /* end of GRADIENTS calculation block */
                    /* ------------------------------------------------------------------------------------------------ */
                    
                        
                } // (r2 < h2i || r2 < h2j) && gradient_iteration==0
            } // numngb loop
        } // while(startnode)
        
#ifndef DONOTUSENODELIST
        if(mode == 1)
        {
            listindex++;
            if(listindex < NODELISTLENGTH)
            {
                startnode = GasGradDataGet[target].NodeList[listindex];
                if(startnode >= 0)
                    startnode = Nodes[startnode].u.d.nextnode;	/* open it */
            }
        }
#endif
    }
    
    
    /* ------------------------------------------------------------------------------------------------ */
    /* Now collect the result at the right place */
    if(gradient_iteration==0)
    {
        if(mode == 0)
            out2particle_GasGrad(&out, target, 0, gradient_iteration);
        else
            GasGradDataResult[target] = out;
    } else {
        if(mode == 0)
            out2particle_GasGrad_iter(&out_iter, target, 0, gradient_iteration);
        else
            GasGradDataResult_iter[target] = out_iter;
    }
    /* ------------------------------------------------------------------------------------------------ */
    
    return 0;
}





void *GasGrad_evaluate_primary(void *p, int gradient_iteration)
{
    int thread_id = *(int *) p;
    int i, j;
    int *exportflag, *exportnodecount, *exportindex, *ngblist;
    ngblist = Ngblist + thread_id * NumPart;
    exportflag = Exportflag + thread_id * NTask;
    exportnodecount = Exportnodecount + thread_id * NTask;
    exportindex = Exportindex + thread_id * NTask;
    
    /* Note: exportflag is local to each thread */
    for(j = 0; j < NTask; j++)
        exportflag[j] = -1;
    
    while(1)
    {
        int exitFlag = 0;
        LOCK_NEXPORT;
#ifdef _OPENMP
#pragma omp critical(_nexport_)
#endif
        {
            if(BufferFullFlag != 0 || NextParticle < 0)
            {
                exitFlag = 1;
            }
            else
            {
                i = NextParticle;
                ProcessedFlag[i] = 0;
                NextParticle = NextActiveParticle[NextParticle];
            }
        }
        UNLOCK_NEXPORT;
        if(exitFlag)
            break;
        
        if(P[i].Type == 0)
        {
            if(GasGrad_evaluate(i, 0, exportflag, exportnodecount, exportindex, ngblist, gradient_iteration) < 0)
                break;		/* export buffer has filled up */
        }
        ProcessedFlag[i] = 1; /* particle successfully finished */
    }
    return NULL;
}



void *GasGrad_evaluate_secondary(void *p, int gradient_iteration)
{
    int thread_id = *(int *) p;
    int j, dummy, *ngblist;
    ngblist = Ngblist + thread_id * NumPart;
    while(1)
    {
        LOCK_NEXPORT;
#ifdef _OPENMP
#pragma omp critical(_nexport_)
#endif
        {
            j = NextJ;
            NextJ++;
        }
        UNLOCK_NEXPORT;
        
        if(j >= Nimport)
            break;
        
        GasGrad_evaluate(j, 1, &dummy, &dummy, &dummy, ngblist, gradient_iteration);
    }
    return NULL;
}

