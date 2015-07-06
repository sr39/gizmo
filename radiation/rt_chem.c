#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <gsl/gsl_math.h>

#include "../allvars.h"
#include "../proto.h"

#ifdef RADTRANSFER

#ifndef RT_MULTI_FREQUENCY
void radtransfer_update_chemistry(void)
{
  int i;
  double nH, temp, molecular_weight, rho;
  double nHII;
  double dt, dtime, a3inv, hubble_a, c_light;
  double A, B, CC;
  double n_gamma;
  double alpha_HII, gamma_HI;
  double fac;

#ifdef RT_INCLUDE_HE
  double alpha_HeII, alpha_HeIII, gamma_HeI, gamma_HeII;
  double nHeII, nHeIII;
  double D, E, F, G, J, L;
  double y_fac;
#endif

  fac = All.UnitTime_in_s / pow(All.UnitLength_in_cm, 3) * All.HubbleParam * All.HubbleParam;

  if(All.ComovingIntegrationOn)
    {
      hubble_a = hubble_function(All.Time);
      a3inv = 1.0 / All.Time / All.Time / All.Time;
    }
  else
    hubble_a = a3inv = 1.0;
  
  c_light = C / All.UnitVelocity_in_cm_per_s;

  for(i = FirstActiveParticle; i >= 0; i = NextActiveParticle[i])
    if(P[i].Type == 0)
      {
	dt = (P[i].TimeBin ? (1 << P[i].TimeBin) : 0) * All.Timebase_interval;
	
	if(All.ComovingIntegrationOn)
	  dtime = dt / hubble_a;
	else
	  dtime = dt;
	
	rho = SphP[i].Density * a3inv;
	
	nH = HYDROGEN_MASSFRAC * rho / PROTONMASS * All.UnitMass_in_g / All.HubbleParam;

	molecular_weight = 4 / (1 + 3 * HYDROGEN_MASSFRAC + 4 * HYDROGEN_MASSFRAC * SphP[i].Ne);



//	  if(fabs(temp - 1.0e4) > 1000.0)
//	  	printf("temp: %g\n",temp);

#ifdef RT_ILIEV_TEST1
	temp = 1.0e4;
#else
	temp = GAMMA_MINUS1 * SphP[i].InternalEnergyPred * molecular_weight * PROTONMASS /
		All.UnitMass_in_g * All.HubbleParam / BOLTZMANN * All.UnitEnergy_in_cgs / All.HubbleParam;
#endif

//		if(fabs(temp - 1.0e4) > 3000.0)
//	printf("temp: %g\n",temp);

	/* collisional ionization rate */
	gamma_HI = 5.85e-11 * sqrt(temp) * exp(-157809.1 / temp) / (1.0 + sqrt(temp / 1e5)) * fac;

	/* alpha_B recombination coefficient */
	alpha_HII = 2.59e-13 * pow(temp / 1e4, -0.7) * fac;

	n_gamma = SphP[i].n_gamma[0] / P[i].Mass * a3inv;
	
	/* number of photons should be positive */
	if(n_gamma < 0 || isnan(n_gamma))
	  {
	    printf("NEGATIVE n_gamma: %g %d %d \n", n_gamma, i, ThisTask);
	    printf("n_gamma %g mass %g a3inv %g \n", SphP[i].n_gamma[0], P[i].Mass, a3inv);
	    endrun(111);
	  }
	
	A = dtime * gamma_HI * nH * SphP[i].Ne;
	B = dtime * c_light * n_gamma * rt_sigma_HI[0];
	CC = dtime * alpha_HII * nH * SphP[i].Ne;
	
	/* semi-implicit scheme for ionization */
	nHII = SphP[i].HII + B + A;

	nHII /= 1.0 + B + CC + A;
	
	if(nHII < 0 || nHII > 1 || isnan(nHII))
	  {
	    printf("ERROR nHII %g \n", nHII);
	    endrun(333);
	  }
	
	SphP[i].Ne = nHII;
	
	SphP[i].HII = nHII;

	SphP[i].HI = 1.0 - nHII;
	
#ifdef RT_INCLUDE_HE
	/* collisional ionization rate */
	gamma_HeI = 2.38e-11 * sqrt(temp) * exp(-285335.4 / temp) / (1.0 + sqrt(temp / 1e5)) * fac;
	gamma_HeII = 5.68e-12 * sqrt(temp) * exp(-631515 / temp) / (1.0 + sqrt(temp / 1e5)) * fac;

	/* alpha_B recombination coefficient */
	alpha_HeII = 1.5e-10 * pow(temp, -0.6353) * fac;
	alpha_HeIII = 3.36e-10 / sqrt(temp) * pow(temp / 1e3, -0.2) / (1.0 + pow(temp / 1e6, 0.7)) * fac;

	SphP[i].Ne += SphP[i].HeII + 2.0 * SphP[i].HeIII;

	D = dtime * gamma_HeII * nH * SphP[i].Ne;
	E = dtime * alpha_HeIII * nH * SphP[i].Ne;
	F = dtime * gamma_HeI * nH * SphP[i].Ne;
	J = dtime * alpha_HeII * nH * SphP[i].Ne;
	G = 0.0;
	L = 0.0;

	y_fac = (1.0-HYDROGEN_MASSFRAC)/4.0/HYDROGEN_MASSFRAC;

	nHeII = SphP[i].HeII / y_fac;
	nHeIII = SphP[i].HeIII / y_fac;

	nHeII = nHeII + G + F - ((G + F - E) / (1.0 + E)) * nHeIII;
	
	nHeII /= 1.0 + G + F + D + J + ((G + F - E) / (1.0 + E)) * (D + L);

        if(nHeII < 0 || nHeII > 1 || isnan(nHeII))
          {
            printf("ERROR nHeII %g \n", nHeII);
            endrun(333);
          }

	nHeIII = nHeIII + (D + L) * nHeII;

	nHeIII /= 1.0 + E;

        if(nHeIII < 0 || nHeIII > 1 || isnan(nHeIII))
          {
            printf("ERROR nHeIII %g \n", nHeIII);
            endrun(333);
          }

	SphP[i].Ne = SphP[i].HII + nHeII + 2.0 * nHeIII;
	
        nHeII *= y_fac;
        nHeIII *= y_fac;

        SphP[i].Ne = SphP[i].HII + nHeII + 2.0 * nHeIII;

        SphP[i].HeII = nHeII;
        SphP[i].HeIII = nHeIII;

        SphP[i].HeI = y_fac - SphP[i].HeII - SphP[i].HeIII;

        if(SphP[i].HeI < 0)
          SphP[i].HeI = 0.0;

        if(SphP[i].HeI > y_fac)
          SphP[i].HeI = y_fac;
#endif
      }
  
}

#else

/*---------------------------------------------------------------------*/
/* if the multi-frequency scheme is used*/
/*---------------------------------------------------------------------*/
void radtransfer_update_chemistry(void)
{
  int i, j;
  double nH, temp, molecular_weight, rho;
  double nHII, c_light, n_gamma;
  double dt, dtime, a3inv, hubble_a;
  double A, B, CC;
  double alpha_HII, gamma_HI;
  double fac;
  double k_HI;

#ifdef RT_INCLUDE_HE
  double alpha_HeII, alpha_HeIII, gamma_HeI, gamma_HeII;
  double nHeII, nHeIII;
  double D, E, F, G, J, L;
  double k_HeI, k_HeII;
  double y_fac;
#endif

  fac = All.UnitTime_in_s / pow(All.UnitLength_in_cm, 3) * All.HubbleParam * All.HubbleParam;

  c_light = C / All.UnitVelocity_in_cm_per_s;

  if(All.ComovingIntegrationOn)
    {
      hubble_a = hubble_function(All.Time);
      a3inv = All.Time / All.Time / All.Time;
    }
  else
    hubble_a = a3inv = 1.0;
  
  for(i = FirstActiveParticle; i >= 0; i = NextActiveParticle[i])
    if(P[i].Type == 0)
      {
	/* get the photo-ionization rates*/
	k_HI = 0.0;
#ifdef RT_INCLUDE_HE
	k_HeI = k_HeII = 0.0;
#endif

	for(j = 0; j < N_RT_FREQ_BINS; j++)
	  {
	    n_gamma = SphP[i].n_gamma[j] / P[i].Mass * a3inv;
	
	    if(nu[j] >= 13.6)
	      k_HI += c_light * rt_sigma_HI[j] * n_gamma;

#ifdef RT_INCLUDE_HE
	    if(nu[j] >= 24.6)
	      k_HeI += c_light * rt_sigma_HeI[j] * n_gamma;
	      	    
	    if(nu[j] >= 54.4)
	      k_HeII += c_light * rt_sigma_HeII[j] * n_gamma;
#endif
	  }

	dt = (P[i].TimeBin ? (1 << P[i].TimeBin) : 0) * All.Timebase_interval;
	
	if(All.ComovingIntegrationOn)
	  dtime = dt / hubble_a;
	else
	  dtime = dt;
	
	rho = SphP[i].Density * a3inv;
	
	nH = HYDROGEN_MASSFRAC * rho / PROTONMASS * All.UnitMass_in_g / All.HubbleParam;

	molecular_weight = 4 / (1 + 3 * HYDROGEN_MASSFRAC + 4 * HYDROGEN_MASSFRAC * SphP[i].Ne);

	temp = GAMMA_MINUS1 * SphP[i].InternalEnergyPred * molecular_weight * PROTONMASS /
		All.UnitMass_in_g * All.HubbleParam / BOLTZMANN * All.UnitEnergy_in_cgs / All.HubbleParam;


	/* collisional ionization rate */
	gamma_HI = 5.85e-11 * sqrt(temp) * exp(-157809.1 / temp) / (1.0 + sqrt(temp / 1e5)) * fac;

	/* alpha_B recombination coefficient */
	alpha_HII = 2.59e-13 * pow(temp / 1e4, -0.7) * fac;
	
	A = dtime * gamma_HI * nH * SphP[i].Ne;
	B = dtime * k_HI;
	CC = dtime * alpha_HII * nH * SphP[i].Ne;
	
	/* semi-implicit scheme for ionization */
	nHII = SphP[i].HII + B + A;

	nHII /= 1.0 + B + CC + A;
	
	if(nHII < 0 || nHII > 1 || isnan(nHII))
	  {
	    printf("ERROR nHII %g \n", nHII);
	    printf("HII %g \n", SphP[i].HII);
	    printf("B %g CC %g A %g \n",B,CC,A);
	    printf("alpha HII %g \n",alpha_HII);
	    printf("nH %g \n",nH);
	    printf("fac %g \n",fac);
	    printf("temp %g \n",temp);
	    printf("pressure %g \n",SphP[i].Pressure);
	    printf("kHI %g \n",k_HI);
	    printf("ngamma %g \n",SphP[i].n_gamma[0]);
	    endrun(333);
	  }
	
	SphP[i].Ne = nHII;
	
	SphP[i].HII = nHII;

	SphP[i].HI = 1.0 - nHII;
	
#ifdef RT_INCLUDE_HE
	/* collisional ionization rate */
	gamma_HeI = 2.38e-11 * sqrt(temp) * exp(-285335.4 / temp) / (1.0 + sqrt(temp / 1e5)) * fac;
	gamma_HeII = 5.68e-12 * sqrt(temp) * exp(-631515 / temp) / (1.0 + sqrt(temp / 1e5)) * fac;

	/* alpha_B recombination coefficient */
	alpha_HeII = 1.5e-10 * pow(temp, -0.6353) * fac;
	alpha_HeIII = 3.36e-10 / sqrt(temp) * pow(temp / 1e3, -0.2) / (1.0 + pow(temp / 1e6, 0.7)) * fac;
	
	SphP[i].Ne += SphP[i].HeII +  2.0 * SphP[i].HeIII;
	
	D = dtime * gamma_HeII * nH * SphP[i].Ne;
	E = dtime * alpha_HeIII * nH * SphP[i].Ne;
	F = dtime * gamma_HeI * nH * SphP[i].Ne;
	J = dtime * alpha_HeII * nH * SphP[i].Ne;
	G = dtime * k_HeI;
	L = dtime * k_HeII;

	y_fac = (1.0-HYDROGEN_MASSFRAC)/4.0/HYDROGEN_MASSFRAC;

	nHeII = SphP[i].HeII / y_fac;
	nHeIII = SphP[i].HeIII / y_fac;

	nHeII = nHeII + G + F - ((G + F - E) / (1.0 + E)) * nHeIII;
	
	nHeII /= 1.0 + G + F + D + J + ((G + F - E) / (1.0 + E)) * (D + L);

	if(nHeII < 0 || nHeII > 1 || isnan(nHeII))
          {
            printf("ERROR nHeII %g %g %g\n", nHeII, temp, k_HeI);
            endrun(333);
          }

	nHeIII = nHeIII + (D + L) * nHeII;

	nHeIII /= 1.0 + E;
	
	if(nHeIII < 0 || nHeIII > 1 || isnan(nHeIII))
          {
            printf("ERROR nHeIII %g %g %g\n", nHeIII, temp, k_HeII);
            endrun(333);
          }

	nHeII *= y_fac;
	nHeIII *= y_fac;

	SphP[i].Ne = SphP[i].HII + nHeII + 2.0 * nHeIII;
	
	SphP[i].HeII = nHeII;
	SphP[i].HeIII = nHeIII;
	
	SphP[i].HeI = y_fac - SphP[i].HeII - SphP[i].HeIII;
	
	if(SphP[i].HeI < 0)
	  SphP[i].HeI = 0.0;
	
	if(SphP[i].HeI > y_fac)
	  SphP[i].HeI = y_fac;
#endif
      }
}
#endif

void rt_write_stats(void)
{
  int i;
  double rho, a3inv;
  double total_nHI, total_V, total_nHI_all, total_V_all;
  total_nHI = 0.0;
  total_V = 0.0;

#ifndef RT_MULTI_FREQUENCY
  double total_ng, total_ng_all, n_gamma;
  total_ng = 0.0;
#endif

#ifdef RT_INCLUDE_HE
  double total_nHeI, total_nHeI_all;
  double total_nHeII, total_nHeII_all;
  total_nHeI = total_nHeII = 0.0;
#endif
  
  if(All.ComovingIntegrationOn)
    a3inv = All.Time / All.Time / All.Time;
  else
    a3inv = 1.0;

  for(i = 0; i < N_gas; i++)
    if(P[i].Type == 0)
      {
	rho = SphP[i].Density * a3inv;

#ifndef RT_MULTI_FREQUENCY
	n_gamma = SphP[i].n_gamma[0] / P[i].Mass * a3inv;
        total_ng += n_gamma / 1e53 * P[i].Mass / rho;	
#endif

#ifdef RT_INCLUDE_HE
        total_nHeI += SphP[i].HeI * P[i].Mass / rho;
        total_nHeII += SphP[i].HeII * P[i].Mass / rho;
#endif
        total_nHI += SphP[i].HI * P[i].Mass / rho;
        total_V += P[i].Mass / rho;
      }

  MPI_Allreduce(&total_nHI, &total_nHI_all, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  MPI_Allreduce(&total_V, &total_V_all, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
#ifndef RT_MULTI_FREQUENCY
  MPI_Allreduce(&total_ng, &total_ng_all, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
#endif
#ifdef RT_INCLUDE_HE
  MPI_Allreduce(&total_nHeI, &total_nHeI_all, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  MPI_Allreduce(&total_nHeII, &total_nHeII_all, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
#endif

  if(ThisTask == 0)
    {
      if(All.Time == All.TimeBegin)
	{
	  fprintf(FdRad, "time, nHI");
#ifndef RT_MULTI_FREQUENCY
	  fprintf(FdRad, ", n_gamma");
#endif
#ifdef RT_INCLUDE_HE
	  fprintf(FdRad, ", nHeI, nHeII \n");
#else
	  fprintf(FdRad, "\n");
#endif
	}

      fprintf(FdRad, "%g %g ", All.Time, total_nHI_all / total_V_all);
#ifndef RT_MULTI_FREQUENCY
      fprintf(FdRad, "%g ", total_ng_all / total_V_all);
#endif
#ifdef RT_INCLUDE_HE
      fprintf(FdRad, "%g %g\n", total_nHeI_all / total_V_all, total_nHeII_all / total_V_all);
#else
      fprintf(FdRad, "\n");
#endif
      fflush(FdRad);
    }

}

#endif
