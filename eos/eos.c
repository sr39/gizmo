#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <gsl/gsl_math.h>
#include "../allvars.h"
#include "../proto.h"

/*! Routines for gas equation-of-state terms (collects things like calculation of gas pressure)
 * This file was written by Phil Hopkins (phopkins@caltech.edu) for GIZMO.
 */

/*! this pair of functions: 'return_user_desired_target_density' and 'return_user_desired_target_pressure' should be used
 together with 'HYDRO_GENERATE_TARGET_MESH'. This will attempt to move the mesh and mass
 towards the 'target' pressure profile. Use this to build your ICs.
 The 'desired' pressure and density as a function of particle properties (most commonly, position) should be provided in the function below */
double return_user_desired_target_density(int i)
{
    return 1; // uniform density everywhere -- will try to generate a glass //
    /*
     // this example would initialize a constant-density (density=rho_0) spherical cloud (radius=r_cloud) with a smooth density 'edge' (width=interp_width) surrounded by an ambient medium of density =rho_0/rho_contrast //
     double dx=P[i].Pos[0]-boxHalf_X, dy=P[i].Pos[1]-boxHalf_Y, dz=P[i].Pos[2]-boxHalf_Z, r=sqrt(dx*dx+dy*dy+dz*dz);
     double rho_0=1, r_cloud=0.5*boxHalf_X, interp_width=0.1*r_cloud, rho_contrast=10.;
     return rho_0 * ((1.-1./rho_contrast)*0.5*erfc(2.*(r-r_cloud)/interp_width) + 1./rho_contrast);
     */
}
double return_user_desired_target_pressure(int i)
{
    return 1; // uniform pressure everywhere -- will try to generate a constant-pressure medium //
    /*
     // this example would initialize a radial pressure gradient corresponding to a self-gravitating, spherically-symmetric, infinite power-law
     //   density profile rho ~ r^(-b) -- note to do this right, you need to actually set that power-law for density, too, in 'return_user_desired_target_density' above
     double dx=P[i].Pos[0]-boxHalf_X, dy=P[i].Pos[1]-boxHalf_Y, dz=P[i].Pos[2]-boxHalf_Z, r=sqrt(dx*dx+dy*dy+dz*dz);
     double b = 2.; return 2.*M_PI/fabs((3.-b)*(1.-b)) * pow(return_user_desired_target_density(i),2) * r*r;
     */
}




/*! return the pressure of particle i: this subroutine needs to  set the value of the 'press' variable (pressure), which you can see from the
    templates below can follow an arbitrary equation-of-state. for more general equations-of-state you want to specifically set the soundspeed
    variable as well. */
double get_pressure(int i)
{
    double soundspeed=0, press=0, gamma_eos_index = GAMMA(i); /* get effective adiabatic index */
    press = (gamma_eos_index-1) * SphP[i].InternalEnergyPred * Get_Gas_density_for_energy_i(i); /* ideal gas EOS (will get over-written it more complex EOS assumed) */
    
    
#ifdef GALSF_EFFECTIVE_EQS /* modify pressure to 'interpolate' between effective EOS and isothermal, with the Springel & Hernquist 2003 'effective' EOS */
    if(SphP[i].Density*All.cf_a3inv >= All.PhysDensThresh) {press = All.FactorForSofterEQS * press + (1 - All.FactorForSofterEQS) * All.cf_afac1 * (gamma_eos_index-1) * SphP[i].Density * All.InitGasU;}
#endif    
    
    
#ifdef EOS_HELMHOLTZ /* pass the necessary quantities to wrappers for the Timms EOS */
    struct eos_input eos_in;
    struct eos_output eos_out;
    eos_in.rho  = SphP[i].Density;
    eos_in.eps  = SphP[i].InternalEnergyPred;
    eos_in.Ye   = SphP[i].Ye;
    eos_in.Abar = SphP[i].Abar;
    eos_in.temp = SphP[i].Temperature;
    int ierr = eos_compute(&eos_in, &eos_out);
    assert(!ierr);
    press      = eos_out.press;
    soundspeed = eos_out.csound;
    SphP[i].Temperature = eos_out.temp;
#endif

    
#ifdef EOS_TILLOTSON
    press = calculate_eos_tillotson(i); soundspeed = SphP[i].SoundSpeed; /* done in subroutine, save for below */
#endif
    
    
#ifdef EOS_ENFORCE_ADIABAT
    press = EOS_ENFORCE_ADIABAT * pow(SphP[i].Density, gamma_eos_index);
#ifdef TURB_DRIVING
    SphP[i].EgyDiss += (SphP[i].InternalEnergy - press / (SphP[i].Density * (gamma_eos_index-1.))); /* save the change in energy imprinted by this enforced equation of state here */
#endif
    SphP[i].InternalEnergy = SphP[i].InternalEnergyPred = press / (SphP[i].Density * (gamma_eos_index-1.)); /* reset internal energy: particles live -exactly- along this relation */
#endif

    
#ifdef EOS_GMC_BAROTROPIC // barytropic EOS calibrated to Masunaga & Inutsuka 2000, eq. 4 in Federrath 2014 Apj 790. Reasonable over the range of densitites relevant to some small-scale star formation problems
    gamma_eos_index=7./5.; double rho=Get_Gas_density_for_energy_i(i), nH_cgs=rho*All.cf_a3inv * ( All.UnitDensity_in_cgs*All.HubbleParam*All.HubbleParam ) / PROTONMASS;
    if(nH_cgs > 2.30181e16) {gamma_eos_index=5./3.;} /* dissociates to atomic if dense enough (hot) */
#if (EOS_GMC_BAROTROPIC==0) // EOS used in Bate Bonnell & Bromm 2003 and related works - isothermal below 6e10 cm^-3, adiabatic above. Assumes c_s = 200m/s at low density
    if (nH_cgs < 6e10) {press = 6.60677e-16 * nH_cgs;} // isothermal below 6e10 cm^-3 (adiabatic gamma=5/3 for soundspeed, etc, but this assumes effective eos from cooling, etc
    else press = 3.964062e-5 * pow(nH_cgs/6e10,1.4);
#else    
    if (nH_cgs < 1.49468e8) {press = 6.60677e-16 * nH_cgs;} // isothermal below ~10^8 cm^-3 (adiabatic gamma=5/3 for soundspeed, etc, but this assumes effective eos from cooling, etc
    else if (nH_cgs < 2.30181e11) {press = 1.00585e-16 * pow(nH_cgs, 1.1);} // 'transition' region
    else if (nH_cgs < 2.30181e16) {press = 3.92567e-20 * pow(nH_cgs, gamma_eos_index);} // adiabatic molecular
    else if (nH_cgs < 2.30181e21) {press = 3.1783e-15 * pow(nH_cgs, 1.1);} // 'transition' region
    else {press = 2.49841e-27 * pow(nH_cgs, gamma_eos_index);} // adiabatic atomic
#endif
    press /= All.UnitPressure_in_cgs;
    /* in this case the EOS is modeling cooling, etc, so -does not- allow shocks or deviations from adiabat, so we reset the internal energy every time this is checked */
    SphP[i].InternalEnergy = SphP[i].InternalEnergyPred = press / (rho * (gamma_eos_index-1.));
#endif
    
    
#ifdef COSMIC_RAYS /* compute the CR contribution to the total pressure and effective soundspeed here */
    double soundspeed2 = gamma_eos_index*(gamma_eos_index-1) * SphP[i].InternalEnergyPred;
    int k_CRegy; for(k_CRegy=0;k_CRegy<N_CR_PARTICLE_BINS;k_CRegy++)
    {
        press += Get_Gas_CosmicRayPressure(i,k_CRegy);
        soundspeed2 += GAMMA_COSMICRAY*GAMMA_COSMICRAY_MINUS1 * SphP[i].CosmicRayEnergyPred[k_CRegy] / P[i].Mass;
#ifdef COSMIC_RAYS_ALFVEN
        press += (GAMMA_ALFVEN_CRS-1) * SphP[i].Density * (SphP[i].CosmicRayAlfvenEnergy[k_CRegy][0]+SphP[i].CosmicRayAlfvenEnergy[k_CRegy][1]);
        soundspeed2 += GAMMA_ALFVEN_CRS*(GAMMA_ALFVEN_CRS-1)*(SphP[i].CosmicRayAlfvenEnergy[k_CRegy][0]+SphP[i].CosmicRayAlfvenEnergy[k_CRegy][1]) / P[i].Mass;
#endif
    }
    soundspeed = sqrt(soundspeed2);
#endif
    
    
#ifdef RT_RADPRESSURE_IN_HYDRO /* add radiation pressure in the Riemann problem directly */
    int k_freq; double gamma_rad=4./3., fluxlim=1; double soundspeed2 = gamma_eos_index*(gamma_eos_index-1) * SphP[i].InternalEnergyPred;
    if(P[i].Mass>0 && SphP[i].Density>0) {for(k_freq=0;k_freq<N_RT_FREQ_BINS;k_freq++)
    {
        press += (gamma_rad-1.) * return_flux_limiter(i,k_freq) * SphP[i].Rad_E_gamma_Pred[k_freq] * SphP[i].Density / P[i].Mass;
        soundspeed2 +=  gamma_rad*(gamma_rad-1.) * SphP[i].Rad_E_gamma_Pred[k_freq] / P[i].Mass;
    }}
    soundspeed = sqrt(soundspeed2);
#endif
    
    
#if defined(EOS_TRUELOVE_PRESSURE) || defined(TRUELOVE_CRITERION_PRESSURE)
    /* add an artificial pressure term to suppress fragmentation at/below the explicit resolution scale */
    double h_eff = DMAX(Get_Particle_Size(i), All.ForceSoftening[0]/2.8); /* need to include latter to account for inter-particle spacing << grav soft cases */
    /* standard finite-volume formulation of this (note there is some geometric ambiguity about whether there should be a "pi" in the equation below, but this 
        can be completely folded into the (already arbitrary) definition of NJeans, so we just use the latter parameter */
    double NJeans = 4; // set so that resolution = lambda_Jeans/NJeans -- fragmentation with Jeans/Toomre scales below this will be artificially suppressed now
    double xJeans = (NJeans * NJeans / gamma_eos_index) * All.G * h_eff*h_eff * SphP[i].Density * SphP[i].Density * All.cf_afac1/All.cf_atime;
    if(xJeans>press) press=xJeans;
#endif
    
    
#if defined(HYDRO_GENERATE_TARGET_MESH)
    press = return_user_desired_target_pressure(i) * (SphP[i].Density / return_user_desired_target_density(i)); // define pressure by reference to 'desired' fluid quantities //
    SphP[i].InternalEnergy = SphP[i].InternalEnergyPred = return_user_desired_target_pressure(i) / ((gamma_eos_index-1) * SphP[i].Density);
#endif
    
    
#ifdef EOS_GENERAL /* need to be sure soundspeed variable is set: if not defined above, set it to the default which is given by the effective gamma */
    if(soundspeed == 0) {SphP[i].SoundSpeed = sqrt(gamma_eos_index * press / Get_Gas_density_for_energy_i(i));} else {SphP[i].SoundSpeed = soundspeed;}
#endif
    return press;
}




/*! this function allows the user to specify an arbitrarily complex adiabatic index. note that for pure adiabatic evolution, one can simply set the pressure to obey some barytropic equation-of-state and use EOS_GENERAL to tell the code to deal with it appropriately.
      but for more general functionality, we want this index here to be appropriately variable. */
double gamma_eos(int i)
{
#ifdef EOS_SUBSTELLAR_ISM
    if(i>=0) {
        if(P[i].Type==0) {
            double T_eff_atomic = 1.23 * (5./3.-1.) * U_TO_TEMP_UNITS * SphP[i].InternalEnergyPred;
            double nH_cgs = SphP[i].Density*All.cf_a3inv * ( All.UnitDensity_in_cgs*All.HubbleParam*All.HubbleParam ) / PROTONMASS;
            double T_transition=DMIN(8000.,nH_cgs), f_mol=1./(1. + T_eff_atomic*T_eff_atomic/(T_transition*T_transition));
            /* double gamma_mol_atom = (29.-8./(2.-f_mol))/15.; // interpolates between 5/3 (fmol=0) and 7/5 (fmol=1) */
            /* return gamma_mol_atom + (5./3.-gamma_mol_atom) / (1 + T_eff_atomic*T_eff_atomic/(40.*40.)); // interpolates back up to 5/3 when temps fall below ~30K [cant excite upper states] */
            
            /* We take a detailed fit from Vaidya et al. A&A 580, A110 (2015) for n_H ~ 10^7, which accounts for collisional dissociation at 2000K and ionization at 10^4K,
               and take the fmol-weighted average with 5./3 at the end to interpolate between atomic/not self-shielding and molecular/self-shielding. Gamma should technically
               really come from calculating the species number-weighted specific heats, but fmol is very approximate so this should be OK */
            double gamma_mol = 5./3, logT = log10(T_eff_atomic);
            gamma_mol -= 0.381374640 * sigmoid_sqrt(5.946*(logT-1.248)); // going down from 5./3 at 10K to the dip at ~1.2
            gamma_mol += 0.220724233 * sigmoid_sqrt(6.176*(logT-1.889)); // peak at ~ 80K
            gamma_mol -= 0.067922267 * sigmoid_sqrt(10.26*(logT-2.235)); // plateau at ~1.4
            gamma_mol -= 0.418671231 * sigmoid_sqrt(7.714*(logT-3.134)); // collisional dissociation, down to ~1.1
            gamma_mol += 0.6472439052 * sigmoid_sqrt(98.87*(logT-4.277)); // back to 5/3 once we're fully dissociated
            // comment out the above line and uncomment the two lines below if you want the exact version from Vaidya+15, which rolls the heat of ionization into the EOS - note that this should NOT be used with the standard cooling module
//            gamma_mol += 0.659888854 / (1 + (logT-4.277)*(logT-4.277)/0.176); // peak at ~5./3 for atomic H after dissoc but before ionization
//            gamma_mol += 0.6472439052 * sigmoid_sqrt(98.87*(logT-5077)); // ionization at 10^4K (note this happens at logT ~ 5 because we're just adopting a simple conversion factor from u to T
            return gamma_mol*f_mol + (1-f_mol)*5./3;
        }
    }
#endif
    return GAMMA_DEFAULT; /* default to universal constant here */
}




/* trivial function to check if particle falls below the minimum allowed temperature */
void check_particle_for_temperature_minimum(int i)
{
    if(All.MinEgySpec)
    {
        if(SphP[i].InternalEnergy < All.MinEgySpec)
        {
            SphP[i].InternalEnergy = All.MinEgySpec;
            SphP[i].DtInternalEnergy = 0;
        }
    }
}



double INLINE_FUNC Get_Gas_density_for_energy_i(int i)
{
#ifdef HYDRO_PRESSURE_SPH
    return SphP[i].EgyWtDensity;
#endif
    return SphP[i].Density;
}


/* calculate the 'total' effective sound speed in a given element [including e.g. cosmic ray pressure and other forms of pressure, if present] */
double INLINE_FUNC Get_Gas_effective_soundspeed_i(int i)
{
#ifdef EOS_GENERAL
    return SphP[i].SoundSpeed;
#else
    /* if nothing above triggers, then we resort to good old-fashioned ideal gas */
    return sqrt(GAMMA(i) * SphP[i].Pressure / Get_Gas_density_for_energy_i(i));
#endif
}


/* calculate the thermal sound speed (using just the InternalEnergy variable) in a given element */
double INLINE_FUNC Get_Gas_thermal_soundspeed_i(int i)
{
    return sqrt(convert_internalenergy_soundspeed2(i,SphP[i].InternalEnergyPred));
}


/* calculate the Alfven speed in a given element */
double Get_Gas_Alfven_speed_i(int i)
{
#if defined(MAGNETIC)
    int k; double bmag=0; for(k=0;k<3;k++) {bmag+=Get_Gas_BField(i,k)*All.cf_a2inv*Get_Gas_BField(i,k)*All.cf_a2inv;}
    if(bmag > 0) {return sqrt(bmag / (MIN_REAL_NUMBER + SphP[i].Density*All.cf_a3inv));}
#endif
    return 0;
}

/* calculate and return the actual B Field of a cell */
double INLINE_FUNC Get_Gas_BField(int i_particle_id, int k_vector_component)
{
#if defined(MAGNETIC)
    return SphP[i_particle_id].BPred[k_vector_component] * SphP[i_particle_id].Density / P[i_particle_id].Mass;
#endif
    return 0;
}



/* returns the conversion factor to go -approximately- (for really quick estimation) in code units, from internal energy to soundspeed */
double INLINE_FUNC convert_internalenergy_soundspeed2(int i, double u)
{
    double gamma_eos_touse = GAMMA(i);
    return gamma_eos_touse * (gamma_eos_touse-1) * u;
}


/* returns the ionized fraction of gas, meant as a reference for runs outside of the cooling routine which include cooling+other physics */
double Get_Gas_Ionized_Fraction(int i)
{
#ifdef COOLING
    double ne=SphP[i].Ne, nh0=0, nHe0, nHepp, nhp, nHeII, temperature, mu_meanwt=1, rho=SphP[i].Density*All.cf_a3inv, u0=SphP[i].InternalEnergyPred;
    temperature = ThermalProperties(u0, rho, i, &mu_meanwt, &ne, &nh0, &nhp, &nHe0, &nHeII, &nHepp); // get thermodynamic properties
#ifdef GALSF_FB_FIRE_RT_HIIHEATING
    if(SphP[i].DelayTimeHII>0) {nh0=0;} // account for our effective ionization model here
#endif
    double f_ion = DMIN(DMAX(DMAX(DMAX(1-nh0, nhp), ne/1.2), 1.e-8), 1.); // account for different measures above (assuming primordial composition)
    return f_ion;
#endif
    return 1;
}



double Get_Gas_Molecular_Mass_Fraction(int i, double temperature, double neutral_fraction, double urad_from_uvb_in_G0, double clumping_factor)
{
    /* if tracking chemistry explicitly, return the explicitly-evolved H2 fraction */
#ifdef CHIMES // use the CHIMES molecular network for H2
    return DMIN(1,DMAX(0, ChimesGasVars[i].abundances[H2] * 2.0)); // factor 2 converts to mass fraction in molecular gas, as desired
#endif
    
#if (COOL_GRACKLE_CHEMISTRY >= 2) // Use GRACKLE explicitly-tracked H2 [using the molecular network if this is valid]
    return DMIN(1,DMAX(0, SphP[i].grH2I + SphP[i].grH2II)); // include both states of H2 tracked
#endif
    
#if (GALSF_FB_FIRE_STELLAREVOLUTION == 3) /* ?? if not tracking chemistry explicitly, return a simple estimate of H2 fraction following the KMT [Apj 2009 693, 216] model */
    /* get neutral fraction [given by call to this program] */
    double f_neutral=1; if(neutral_fraction>=0) {f_neutral=neutral_fraction;}
    /* get metallicity */
    double Z_Zsol=1, urad_G0=1; // assumes spatially uniform Habing field for incident FUV radiation unless reset below //
#ifdef METALS
    Z_Zsol = DMAX( P[i].Metallicity[0]/All.SolarAbundances[0], 1.e-6 ); // metallicity in solar units [scale to total Z, since this mixes dust and C opacity], and enforce a low-Z floor to prevent totally unphysical behaviors at super-low Z [where there is still finite opacity in reality]
#endif
    /* get incident radiation field */
#ifdef GALSF_FB_FIRE_RT_UVHEATING
    urad_G0 = SphP[i].Rad_Flux_UV;
#endif
#if defined(RT_PHOTOELECTRIC) || defined(RT_LYMAN_WERNER)
    int whichbin = RT_FREQ_BIN_LYMAN_WERNER;
#if !defined(RT_LYMAN_WERNER)
    whichbin = RT_FREQ_BIN_PHOTOELECTRIC; // use photo-electric bin as proxy (very close) if don't evolve LW explicitly
#endif
    urad_G0 = SphP[i].Rad_E_gamma[whichbin] * (SphP[i].Density*All.cf_a3inv/P[i].Mass) * All.UnitPressure_in_cgs * All.HubbleParam*All.HubbleParam / 3.9e-14; // convert to Habing field //
#endif
    urad_G0 += urad_from_uvb_in_G0; // include whatever is contributed from the meta-galactic background, fed into this routine
    /* get estimate of mass column density integrated away from this location for self-shielding */
    double surface_density_Msun_pc2 = evaluate_NH_from_GradRho(SphP[i].Gradients.Density,PPP[i].Hsml,SphP[i].Density,PPP[i].NumNgb,1,i) * All.UnitDensity_in_cgs * All.HubbleParam * All.UnitLength_in_cm / 0.000208854; // approximate column density with Sobolev or Treecol methods as appropriate; converts to M_solar/pc^2
    /* get estimate of local density and clumping factor */
    double nH_cgs = SphP[i].Density * All.cf_a3inv * All.UnitDensity_in_cgs / PROTONMASS; // get nH defined as in KMT [number of nucleons per cm^3]
    double clumping_factor_for_unresolved_densities = clumping_factor; // Gnedin et al. add a large clumping factor to account for inability to resolve high-densities, here go with what is resolved
    /* now actually do the relevant calculation with the KMT fitting functions */
    double chi = 0.766 * (1. + 3.1*pow(Z_Zsol,0.365)); // KMT estimate of chi, useful if we do -not- know anything about actual radiation field
    if(urad_G0 > 0) {chi = 71. * urad_G0 / (clumping_factor_for_unresolved_densities * nH_cgs);} // their actual fiducial value including radiation information
    double psi = chi * (1.+0.4*chi)/(1.+1.08731*chi); // slightly-transformed chi variable
    double s = Z_Zsol * surface_density_Msun_pc2 / (MIN_REAL_NUMBER + psi); // key variable controlling shielding in the KMT approximaton
    double q = s * (125. + s) / (11. * (96. + s)); // convert to more useful form from their Eq. 37
    double fH2 = 1. - pow(1.+q*q*q , -1./3.); // full KMT expression [unlike log-approximation, this extrapolates physically at low-q]
    if(q<0.2) {fH2 = q*q*q * (1. - 2.*q*q*q/3.)/3.;} // catch low-q limit more accurately [prevent roundoff error problems]
    if(q>10.) {fH2 = 1. - 1./q;} // catch high-q limit more accurately [prevent roundoff error problems]
    return DMIN(1,DMAX(0, fH2 * f_neutral)); // multiple by neutral fraction, as this is ultimately the fraction of the -neutral- gas in H2
#endif
    
#if (SINGLE_STAR_SINK_FORMATION & 256) || defined(GALSF_SFR_MOLECULAR_CRITERION) /* estimate f_H2 with Krumholz & Gnedin 2010 fitting function, assuming simple scalings of radiation field, clumping, and other factors with basic gas properties so function only of surface density and metallicity, truncated at low values (or else it gives non-sensical answers) */
    double fH2_kg=0, tau_fmol = (0.1 + P[i].Metallicity[0]/All.SolarAbundances[0]) * evaluate_NH_from_GradRho(P[i].GradRho,PPP[i].Hsml,SphP[i].Density,PPP[i].NumNgb,1,i) * 434.78 * All.UnitDensity_in_cgs * All.UnitLength_in_cm * All.HubbleParam; // convert units for surface density. also limit to Z>=0.1, where their fits were actually good, or else get unphysically low molecular fractions
    if(tau_fmol>0) {double y = 0.756 * (1 + 3.1*pow(P[i].Metallicity[0]/All.SolarAbundances[0],0.365)) / clumping_factor; // this assumes all the equilibrium scalings of radiation field, density, SFR, etc, to get a trivial expression
        y = log(1 + 0.6*y + 0.01*y*y) / (0.6*tau_fmol); y = 1 - 0.75*y/(1 + 0.25*y); fH2_kg=DMIN(1,DMAX(0,fH2_kg));}
    return fH2_kg * neutral_fraction;
#endif
    
#if defined(COOLING) /* if none of the above is set, default to a wildly-oversimplified scaling set by fits to the temperature below which gas at a given density becomes molecular from cloud simulations in Glover+Clark 2012 */
    double T_mol = DMAX(1.,DMIN(8000., SphP[i].Density*All.cf_a3inv*All.UnitDensity_in_cgs/PROTONMASS));
    return neutral_fraction / (1. + temperature*temperature/(T_mol*T_mol));
#endif
    
    return 0; // catch //
}


