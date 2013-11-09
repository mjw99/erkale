/*
 *                This source code is part of
 *
 *                     E  R  K  A  L  E
 *                             -
 *                       DFT from Hel
 *
 * Written by Susi Lehtola, 2010-2011
 * Copyright (c) 2010-2011, Susi Lehtola
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "density_fitting.h"
#include "linalg.h"
#include "scf.h"
#include <sstream>

// Screen integrals? (Direct calculations)
#define SCREENING

// Integral screening cutoff
#define SCRTHR 1e-12

// \delta parameter in Eichkorn et al
#define DELTA 1e-9

DensityFit::DensityFit() {
}

DensityFit::~DensityFit() {
}


void DensityFit::fill(const BasisSet & orbbas, const BasisSet & auxbas, bool dir, double threshold, bool hartreefock) {
  // Construct density fitting basis

  // Store amount of functions
  Nbf=orbbas.get_Nbf();
  Naux=auxbas.get_Nbf();
  Nnuc=orbbas.get_Nnuc();
  direct=dir;
  hf=hartreefock;

  // Fill index helper
  iidx=i_idx(Nbf);
  // Fill list of shell pairs
  orbpairs=orbbas.get_unique_shellpairs();

  // Get orbital shells, auxiliary shells and dummy shell
  orbshells=orbbas.get_shells();
  auxshells=auxbas.get_shells();
  dummy=dummyshell();

  maxorbam=orbbas.get_max_am();
  maxauxam=auxbas.get_max_am();
  maxorbcontr=orbbas.get_max_Ncontr();
  maxauxcontr=auxbas.get_max_Ncontr();
  maxam=std::max(orbbas.get_max_am(),auxbas.get_max_am());
  maxcontr=std::max(orbbas.get_max_Ncontr(),auxbas.get_max_Ncontr());

  // First, compute the two-center integrals
  ab.zeros(Naux,Naux);

  // Get list of unique auxiliary shell pairs
  std::vector<shellpair_t> auxpairs=auxbas.get_unique_shellpairs();

#ifdef _OPENMP
#pragma omp parallel
#endif
  {

    ERIWorker eri(maxauxam,maxauxcontr);
    const std::vector<double> * erip;

#ifdef _OPENMP
#pragma omp for schedule(dynamic)
#endif
    for(size_t ip=0;ip<auxpairs.size();ip++) {
      // The shells in question are
      size_t is=auxpairs[ip].is;
      size_t js=auxpairs[ip].js;

      // Compute (a|b)
      eri.compute(&auxshells[is],&dummy,&auxshells[js],&dummy);
      erip=eri.getp();

      // Store integrals
      size_t Ni=auxshells[is].get_Nbf();
      size_t Nj=auxshells[js].get_Nbf();
      for(size_t ii=0;ii<Ni;ii++) {
	size_t ai=auxshells[is].get_first_ind()+ii;
	for(size_t jj=0;jj<Nj;jj++) {
	  size_t aj=auxshells[js].get_first_ind()+jj;

	  ab(ai,aj)=(*erip)[ii*Nj+jj];
	  ab(aj,ai)=(*erip)[ii*Nj+jj];
	}
      }
    }
  }

  if(hf) {
    // Form ab^-1 and ab^-1/2
    arma::mat abvec;
    arma::vec abval;
    eig_sym_ordered(abval,abvec,ab);

    // Count linearly independent vectors
    size_t Nind=0;
    for(size_t i=0;i<abval.n_elem;i++)
      if(abval(i)>=threshold)
	Nind++;

    // and drop the linearly dependent ones
    abval=abval.subvec(abval.n_elem-Nind,abval.n_elem-1);
    abvec=abvec.submat(0,abvec.n_cols-Nind,abvec.n_rows-1,abvec.n_cols-1);

    // Form matrices
    ab_inv.zeros(abvec.n_rows,abvec.n_rows);
    ab_invh.zeros(abvec.n_rows,abvec.n_rows);
    for(size_t i=0;i<abval.n_elem;i++) {
      ab_inv+=abvec.col(i)*arma::trans(abvec.col(i))/abval(i);
      ab_invh+=abvec.col(i)*arma::trans(abvec.col(i))/sqrt(abval(i));
    }
  } else {
    // Just RI-J, so use faster method from Eichkorn et al to form ab_inv only
    bool invok;
    invok=arma::inv(ab+DELTA,ab_inv);
  }

#ifdef SCREENING
  // Then, form the screening matrix
  if(direct)
    form_screening();
#endif

  // Then, compute the diagonal integrals
  a_mu.resize(Naux*Nbf);
  {
    ERIWorker eri(maxam,maxcontr);
    const std::vector<double> * erip;

    for(size_t ia=0;ia<auxshells.size();ia++)
      for(size_t imu=0;imu<orbshells.size();imu++) {
	// Amount of functions
	size_t Na=auxshells[ia].get_Nbf();
	size_t Nmu=orbshells[imu].get_Nbf();

	// Compute (a|uu)
	eri.compute(&auxshells[ia],&dummy,&orbshells[imu],&orbshells[imu]);
	erip=eri.getp();

	// Store integrals
	for(size_t af=0;af<Na;af++) {
	  size_t inda=auxshells[ia].get_first_ind()+af;

	  for(size_t muf=0;muf<Nmu;muf++) {
	    size_t indmu=orbshells[imu].get_first_ind()+muf;

	    a_mu[inda*Nbf+indmu]=(*erip)[(af*Nmu+muf)*Nmu+muf];
	  }
	}
      }
  }

  // Then, compute the three-center integrals
  if(!direct) {
    a_munu.resize(Naux*Nbf*(Nbf+1)/2);

#ifdef _OPENMP
#pragma omp parallel
#endif
    {

      ERIWorker eri(maxam,maxcontr);
      const std::vector<double> * erip;

      for(size_t ia=0;ia<auxshells.size();ia++)
	for(size_t ip=0;ip<orbpairs.size();ip++) {
	  // Shells in question are
	  size_t imu=orbpairs[ip].is;
	  size_t inu=orbpairs[ip].js;

	  // Amount of functions

	  size_t Na=auxshells[ia].get_Nbf();
	  size_t Nmu=orbshells[imu].get_Nbf();
	  size_t Nnu=orbshells[inu].get_Nbf();

	  // Compute (a|mn)
	  eri.compute(&auxshells[ia],&dummy,&orbshells[imu],&orbshells[inu]);
	  erip=eri.getp();

	  // Store integrals
	  for(size_t af=0;af<Na;af++) {
	    size_t inda=auxshells[ia].get_first_ind()+af;

	    for(size_t muf=0;muf<Nmu;muf++) {
	      size_t indmu=orbshells[imu].get_first_ind()+muf;

	      for(size_t nuf=0;nuf<Nnu;nuf++) {
		size_t indnu=orbshells[inu].get_first_ind()+nuf;

		a_munu[idx(inda,indmu,indnu)]=(*erip)[(af*Nmu+muf)*Nnu+nuf];
	      }
	    }
	  }
	}
    }
  }

}

void DensityFit::form_screening() {
  screen.zeros(orbshells.size(),orbshells.size());
#ifdef _OPENMP
#pragma omp parallel
#endif
  {
    ERIWorker eri(maxam,maxcontr);
    const std::vector<double> * erip;

    for(size_t ip=0;ip<orbpairs.size();ip++) {
      // The shells in question are
      size_t is=orbpairs[ip].is;
      size_t js=orbpairs[ip].js;

      // Compute ERIs
      eri.compute(&orbshells[is],&orbshells[js],&orbshells[is],&orbshells[js]);
      erip=eri.getp();

	// Find out maximum value
      double max=0.0;
      for(size_t i=0;i<(*erip).size();i++)
	if(fabs((*erip)[i])>max)
	  max=(*erip)[i];
      max=sqrt(max);

      // Store value
      screen(is,js)=max;
      screen(js,is)=max;
    }
  }
}

size_t DensityFit::idx(size_t ia, size_t imu, size_t inu) const {
  if(imu<inu)
    std::swap(imu,inu);

  return Naux*(iidx[imu]+inu)+ia;
}

size_t DensityFit::memory_estimate(const BasisSet & orbbas, const BasisSet & auxbas, bool dir) const {

  // Amount of orbital basis functions
  size_t No=orbbas.get_Nbf();
  // Amount of auxiliary functions (for representing the electron density)
  size_t Na=auxbas.get_Nbf();
  // Amount of memory required for calculation
  size_t Nmem=0;

  // Memory taken up by index helper
  Nmem+=No*sizeof(size_t);
  // Memory taken up by  ( \alpha | \mu \nu)
  if(!dir)
    Nmem+=(Na*No*(No+1)/2)*sizeof(double);
#ifdef SCREENING
  else {
    // Memory taken up by screening matrix
    size_t Nsh=orbbas.get_Nshells();
    Nmem+=Nsh*Nsh*sizeof(double);
  }
#endif

  // Memory taken by (\alpha | \beta) and its inverse
  Nmem+=2*Na*Na*sizeof(double);
  // Memory taken by gamma and expansion coefficients
  Nmem+=2*Na*sizeof(double);

  return Nmem;
}

arma::vec DensityFit::compute_expansion(const arma::mat & P) const {
  arma::vec gamma(Naux);
  gamma.zeros();

  // Compute gamma
  if(!direct) {
#ifdef _OPENMP
#pragma omp parallel for
#endif
    for(size_t ia=0;ia<Naux;ia++) {
      gamma(ia)=0.0;

      for(size_t imu=0;imu<Nbf;imu++) {
	// Off-diagonal
	for(size_t inu=0;inu<imu;inu++)
	  gamma(ia)+=2.0*a_munu[idx(ia,imu,inu)]*P(imu,inu);
	// Diagonal
	gamma(ia)+=a_munu[idx(ia,imu,imu)]*P(imu,imu);
      }
    }
  } else {

#ifdef _OPENMP
#pragma omp parallel
#endif
    {

      ERIWorker eri(maxam,maxcontr);
      const std::vector<double> * erip;

#ifdef _OPENMP
      // Worker stack for each matrix
      arma::vec gammawrk(gamma);

#pragma omp for schedule(dynamic)
#endif
      for(size_t ip=0;ip<orbpairs.size();ip++) {
	size_t imus=orbpairs[ip].is;
	size_t inus=orbpairs[ip].js;

#ifdef SCREENING
	// Do we need to compute the integral?
	if(screen(imus,inus)<SCRTHR)
	  continue;
#endif

	size_t Nmu=orbshells[imus].get_Nbf();
	size_t Nnu=orbshells[inus].get_Nbf();

	// If imus==inus, we need to take care that we count
	// every term only once; on the off-diagonal we get
	// every term twice.
	double fac=2.0;
	if(imus==inus)
	  fac=1.0;

	for(size_t ias=0;ias<auxshells.size();ias++) {

	  size_t Na=auxshells[ias].get_Nbf();

	  // Compute (a|mn)
	  eri.compute(&auxshells[ias],&dummy,&orbshells[imus],&orbshells[inus]);
	  erip=eri.getp();

	  // Increment gamma
	  for(size_t iia=0;iia<Na;iia++) {
	    size_t ia=auxshells[ias].get_first_ind()+iia;

	    for(size_t iimu=0;iimu<Nmu;iimu++) {
	      size_t imu=orbshells[imus].get_first_ind()+iimu;

	      for(size_t iinu=0;iinu<Nnu;iinu++) {
		size_t inu=orbshells[inus].get_first_ind()+iinu;

		// The contracted integral
		double res=(*erip)[(iia*Nmu+iimu)*Nnu+iinu]*P(imu,inu);

#ifdef _OPENMP
		gammawrk(ia)+= fac*res;
#else
		gamma(ia)+= fac*res;
#endif
	      }
	    }
	  }
	}
      }

#ifdef _OPENMP
#pragma omp critical
      // Sum results together
      gamma+=gammawrk;
#endif
    } // end parallel section
  }

  // Compute and return c
  if(hf) {
    return ab_inv*gamma;
  } else {
    // Compute x0
    arma::vec x0=ab_inv*gamma;
    // Compute and return c
    return x0+ab_inv*(gamma-ab*x0);
  }
}

std::vector<arma::vec> DensityFit::compute_expansion(const std::vector<arma::mat> & P) const {
  std::vector<arma::vec> gamma(P.size());
  for(size_t i=0;i<P.size();i++)
    gamma[i].zeros(Naux);

  // Compute gamma
  if(!direct) {
#ifdef _OPENMP
#pragma omp parallel for
#endif
    for(size_t ia=0;ia<Naux;ia++) {
      for(size_t ip=0;ip<P.size();ip++)
	gamma[ip](ia)=0.0;

      for(size_t ip=0;ip<P.size();ip++)
	for(size_t imu=0;imu<Nbf;imu++) {
	  // Off-diagonal
	  for(size_t inu=0;inu<imu;inu++)
	    gamma[ip](ia)+=2.0*a_munu[idx(ia,imu,inu)]*P[ip](imu,inu);
	  // Diagonal
	  gamma[ip](ia)+=a_munu[idx(ia,imu,imu)]*P[ip](imu,imu);
	}
    }
  } else {

#ifdef _OPENMP
#pragma omp parallel
#endif
    {
      ERIWorker eri(maxam,maxcontr);
      const std::vector<double> * erip;

#ifdef _OPENMP
      // Worker stack for each matrix
      std::vector<arma::vec> gammawrk(gamma);

#pragma omp for schedule(dynamic)
#endif
      for(size_t ip=0;ip<orbpairs.size();ip++) {
	size_t imus=orbpairs[ip].is;
	size_t inus=orbpairs[ip].js;

#ifdef SCREENING
	// Do we need to compute the integral?
	if(screen(imus,inus)<SCRTHR)
	  continue;
#endif

	size_t Nmu=orbshells[imus].get_Nbf();
	size_t Nnu=orbshells[inus].get_Nbf();

	// If imus==inus, we need to take care that we count
	// every term only once; on the off-diagonal we get
	// every term twice.
	double fac=2.0;
	if(imus==inus)
	  fac=1.0;

	for(size_t ias=0;ias<auxshells.size();ias++) {

	  size_t Na=auxshells[ias].get_Nbf();

	  // Compute (a|mn)
	  eri.compute(&auxshells[ias],&dummy,&orbshells[imus],&orbshells[inus]);
	  erip=eri.getp();

	  // Increment gamma
	  for(size_t iia=0;iia<Na;iia++) {
	    size_t ia=auxshells[ias].get_first_ind()+iia;

	    for(size_t iimu=0;iimu<Nmu;iimu++) {
	      size_t imu=orbshells[imus].get_first_ind()+iimu;

	      for(size_t iinu=0;iinu<Nnu;iinu++) {
		size_t inu=orbshells[inus].get_first_ind()+iinu;

		for(size_t ig=0;ig<P.size();ig++) {
		  // The contracted integral
		  double res=(*erip)[(iia*Nmu+iimu)*Nnu+iinu]*P[ig](imu,inu);

#ifdef _OPENMP
		  gammawrk[ig](ia)+= fac*res;
#else
		  gamma[ig](ia)+= fac*res;
#endif
		}
	      }
	    }
	  }
	}
      }

#ifdef _OPENMP
#pragma omp critical
      // Sum results together
      for(size_t ig=0;ig<P.size();ig++)
	gamma[ig]+=gammawrk[ig];
#endif
    } // end parallel section
  }

  // Compute and return c
  if(hf) {
    for(size_t ig=0;ig<P.size();ig++)
      gamma[ig]=ab_inv*gamma[ig];

  } else {
    for(size_t ig=0;ig<P.size();ig++) {
      // Compute x0
      arma::vec x0=ab_inv*gamma[ig];
      // Compute and return c
      gamma[ig]=x0+ab_inv*(gamma[ig]-ab*x0);
    }
  }

  return gamma;
}

arma::mat DensityFit::invert_expansion(const arma::vec & xcgamma) const {
  arma::mat H(Nbf,Nbf);
  H.zeros();

  // Compute middle result
  arma::vec xcg=arma::trans(xcgamma)*ab_inv;

  // Compute Fock matrix elements
  if(!direct) {
#ifdef _OPENMP
#pragma omp parallel for
#endif
    for(size_t ia=0;ia<Naux;ia++) {
      for(size_t imu=0;imu<Nbf;imu++) {
	// Off-diagonal
	for(size_t inu=0;inu<imu;inu++) {
#ifdef _OPENMP
#pragma omp atomic
#endif
	  H(imu,inu)+=xcg(ia)*a_munu[idx(ia,imu,inu)];
#ifdef _OPENMP
#pragma omp atomic
#endif
	  H(inu,imu)+=xcg(ia)*a_munu[idx(ia,imu,inu)];
	}
	// Diagonal
	H(imu,imu)+=xcg(ia)*a_munu[idx(ia,imu,imu)];
      }
    }
  } else {

#ifdef _OPENMP
#pragma omp parallel
#endif
    {

      ERIWorker eri(maxam,maxcontr);
      const std::vector<double> * erip;

#ifdef _OPENMP
#pragma omp for schedule(dynamic)
#endif
      for(size_t ip=0;ip<orbpairs.size();ip++)
	for(size_t ias=0;ias<auxshells.size();ias++) {

	  size_t imus=orbpairs[ip].is;
	  size_t inus=orbpairs[ip].js;


#ifdef SCREENING
	  // Do we need to compute the integral?
	  if(screen(imus,inus)<SCRTHR)
	    continue;
#endif

	  size_t Na=auxshells[ias].get_Nbf();
	  size_t Nmu=orbshells[imus].get_Nbf();
	  size_t Nnu=orbshells[inus].get_Nbf();

	  double symfac=1.0;
	  if(imus==inus)
	    symfac=0.0;

	  // Compute (a|mn)
	  eri.compute(&auxshells[ias],&dummy,&orbshells[imus],&orbshells[inus]);
	  erip=eri.getp();

	  // Increment H
	  for(size_t iia=0;iia<Na;iia++) {
	    // Account for orbital functions at the beginning of the basis set
	    size_t ia=auxshells[ias].get_first_ind()+iia;

	    for(size_t iimu=0;iimu<Nmu;iimu++) {
	      size_t imu=orbshells[imus].get_first_ind()+iimu;

	      for(size_t iinu=0;iinu<Nnu;iinu++) {
		size_t inu=orbshells[inus].get_first_ind()+iinu;

		// Contract result
		double tmp=(*erip)[(iia*Nmu+iimu)*Nnu+iinu]*xcg(ia);

		H(imu,inu)+=tmp;
		// Need to symmetrize?
		H(inu,imu)+=symfac*tmp;
	      }
	    }
	  }
	}
    }
  }

  return H;
}

arma::vec DensityFit::invert_expansion_diag(const arma::vec & xcgamma) const {
  arma::vec H(Nbf,Nbf);
  H.zeros();

  // Compute middle result
  arma::vec xcg=arma::trans(xcgamma)*ab_inv;

  // Compute Fock matrix elements
#ifdef _OPENMP
#pragma omp parallel for
#endif
  for(size_t ia=0;ia<Naux;ia++) {
    for(size_t imu=0;imu<Nbf;imu++) {
      // Diagonal
      H(imu)+=xcg(ia)*a_mu[ia*Nbf+imu];
    }
  }

  return H;
}

arma::mat DensityFit::calc_J(const arma::mat & P) const {
  // Get the expansion coefficients
  arma::vec c=compute_expansion(P);

  arma::mat J(Nbf,Nbf);
  J.zeros();

  if(!direct) {
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic)
#endif
    for(size_t imu=0;imu<Nbf;imu++)
      for(size_t inu=0;inu<=imu;inu++) {
	J(imu,inu)=0.0;

	for(size_t ia=0;ia<Naux;ia++)
	  J(imu,inu)+=a_munu[idx(ia,imu,inu)]*c(ia);

	J(inu,imu)=J(imu,inu);
      }

  } else {

#ifdef _OPENMP
#pragma omp parallel
#endif
    {
      ERIWorker eri(maxam,maxcontr);
      const std::vector<double> * erip;

#ifdef _OPENMP
#pragma omp for
#endif
      for(size_t ip=0;ip<orbpairs.size();ip++)
	for(size_t ias=0;ias<auxshells.size();ias++) {

	  size_t imus=orbpairs[ip].is;
	  size_t inus=orbpairs[ip].js;


#ifdef SCREENING
	// Do we need to compute the integral?
	  if(screen(imus,inus)<SCRTHR)
	    continue;
#endif

	  size_t Na=auxshells[ias].get_Nbf();
	  size_t Nmu=orbshells[imus].get_Nbf();
	  size_t Nnu=orbshells[inus].get_Nbf();


	  double symfac=1.0;
	  if(imus==inus)
	    symfac=0.0;

	  // Compute (a|mn)
	  eri.compute(&auxshells[ias],&dummy,&orbshells[imus],&orbshells[inus]);
	  erip=eri.getp();

	  // Increment J
	  for(size_t iia=0;iia<Na;iia++) {
	    // Account for orbital functions at the beginning of the basis set
	    size_t ia=auxshells[ias].get_first_ind()+iia;

	    for(size_t iimu=0;iimu<Nmu;iimu++) {
	      size_t imu=orbshells[imus].get_first_ind()+iimu;

	      for(size_t iinu=0;iinu<Nnu;iinu++) {
		size_t inu=orbshells[inus].get_first_ind()+iinu;

		// Contract result
		double tmp=(*erip)[(iia*Nmu+iimu)*Nnu+iinu]*c(ia);

		J(imu,inu)+=tmp;
		// Need to symmetrize?
		J(inu,imu)+=symfac*tmp;
	      }
	    }
	  }
	}
    }
  }

  return J;
}

std::vector<arma::mat> DensityFit::calc_J(const std::vector<arma::mat> & P) const {
  // Get the expansion coefficients
  std::vector<arma::vec> c=compute_expansion(P);

  std::vector<arma::mat> J(P.size());
  for(size_t ip=0;ip<P.size();ip++)
    J[ip].zeros(Nbf,Nbf);

  if(!direct) {
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic)
#endif
    for(size_t imu=0;imu<Nbf;imu++)
      for(size_t ig=0;ig<P.size();ig++)
	for(size_t inu=0;inu<=imu;inu++) {
	  J[ig](imu,inu)=0.0;

	  for(size_t ia=0;ia<Naux;ia++)
	    J[ig](imu,inu)+=a_munu[idx(ia,imu,inu)]*c[ig](ia);

	  J[ig](inu,imu)=J[ig](imu,inu);
	}

  } else {

#ifdef _OPENMP
#pragma omp parallel
#endif
    {
      ERIWorker eri(maxam,maxcontr);
      const std::vector<double> * erip;

#ifdef _OPENMP
#pragma omp for
#endif
      for(size_t ip=0;ip<orbpairs.size();ip++)
	for(size_t ias=0;ias<auxshells.size();ias++) {

	  size_t imus=orbpairs[ip].is;
	  size_t inus=orbpairs[ip].js;


#ifdef SCREENING
	// Do we need to compute the integral?
	  if(screen(imus,inus)<SCRTHR)
	    continue;
#endif

	  size_t Na=auxshells[ias].get_Nbf();
	  size_t Nmu=orbshells[imus].get_Nbf();
	  size_t Nnu=orbshells[inus].get_Nbf();


	  double symfac=1.0;
	  if(imus==inus)
	    symfac=0.0;

	  // Compute (a|mn)
	  eri.compute(&auxshells[ias],&dummy,&orbshells[imus],&orbshells[inus]);
	  erip=eri.getp();

	  // Increment J
	  for(size_t iia=0;iia<Na;iia++) {
	    // Account for orbital functions at the beginning of the basis set
	    size_t ia=auxshells[ias].get_first_ind()+iia;

	    for(size_t iimu=0;iimu<Nmu;iimu++) {
	      size_t imu=orbshells[imus].get_first_ind()+iimu;

	      for(size_t iinu=0;iinu<Nnu;iinu++) {
		size_t inu=orbshells[inus].get_first_ind()+iinu;

		for(size_t ig=0;ig<P.size();ig++) {
		  // Contract result
		  double tmp=(*erip)[(iia*Nmu+iimu)*Nnu+iinu]*c[ig](ia);

		  J[ig](imu,inu)+=tmp;
		  // Need to symmetrize?
		  J[ig](inu,imu)+=symfac*tmp;
		}
	      }
	    }
	  }
	}
    }
  }

  return J;
}

arma::vec DensityFit::force_J(const arma::mat & P) {
  // First, compute the expansion
  arma::vec c=compute_expansion(P);

  // The force
  arma::vec f(3*Nnuc);
  f.zeros();

#ifdef SCREENING
  // Form the screening matrix if not formed already
  if(!direct)
    form_screening();
#endif

  // First part: f = *#* 1/2 c_a (a|b)' c_b *#* - gamma_a' c_a  
#ifdef _OPENMP
#pragma omp parallel
#endif
  {
    dERIWorker deri(maxauxam,maxauxcontr);
    const std::vector<double> * erip;

#ifdef _OPENMP
    // Worker stack for each matrix
    arma::vec fwrk(f);
    fwrk.zeros();

#pragma omp for schedule(dynamic)
#endif
    for(size_t ias=0;ias<auxshells.size();ias++)
      for(size_t jas=0;jas<=ias;jas++) {

	// Symmetry factor
	double fac=0.5;
	if(ias!=jas)
	  fac*=2.0;

	size_t Na=auxshells[ias].get_Nbf();
	size_t anuc=auxshells[ias].get_center_ind();
	size_t Nb=auxshells[jas].get_Nbf();
	size_t bnuc=auxshells[jas].get_center_ind();

	if(anuc==bnuc)
	  // Contributions vanish
	  continue;

	// Compute (a|b)
	deri.compute(&auxshells[ias],&dummy,&auxshells[jas],&dummy);

	// Compute forces
	const static int index[]={0, 1, 2, 6, 7, 8};
	const size_t Nidx=sizeof(index)/sizeof(index[0]);
	arma::vec ders(Nidx);	
	ders.zeros();

	for(size_t iid=0;iid<Nidx;iid++) {
	  // Index is
	  int ic=index[iid];
	  
	  // Increment force, anuc
	  erip=deri.getp(ic);
	  for(size_t iia=0;iia<Na;iia++) {
	    size_t ia=auxshells[ias].get_first_ind()+iia;
	    
	    for(size_t iib=0;iib<Nb;iib++) {
	      size_t ib=auxshells[jas].get_first_ind()+iib;
	      
	      // The integral is
	      double res=(*erip)[iia*Nb+iib];
	      
	      ders(iid)+= res*c(ia)*c(ib);
	    }
	  }
	}
	ders*=fac;

	// Increment forces
	for(int ic=0;ic<3;ic++) {
#ifdef _OPENMP
	  fwrk(3*anuc+ic)+=ders(ic);
	  fwrk(3*bnuc+ic)+=ders(ic+3);
#else
	  f(3*anuc+ic)+=ders(ic);
	  f(3*bnuc+ic)+=ders(ic+3);
#endif
	}
      }
    
#ifdef _OPENMP
#pragma omp critical
    // Sum results together
    f+=fwrk;
#endif
  } // end parallel section


  // Second part: f = 1/2 c_a (a|b)' c_b *#* - gamma_a' c_a *#*  
#ifdef _OPENMP
#pragma omp parallel
#endif
  {
    dERIWorker deri(maxam,maxcontr);
    const std::vector<double> * erip;

#ifdef _OPENMP
    // Worker stack for each matrix
    arma::vec fwrk(f);
    fwrk.zeros();

#pragma omp for schedule(dynamic)
#endif
    for(size_t ip=0;ip<orbpairs.size();ip++) {
      size_t imus=orbpairs[ip].is;
      size_t inus=orbpairs[ip].js;

#ifdef SCREENING
      // Do we need to compute the integral?
      if(screen(imus,inus)<SCRTHR)
	continue;
#endif

      size_t Nmu=orbshells[imus].get_Nbf();
      size_t Nnu=orbshells[inus].get_Nbf();

      size_t inuc=orbshells[imus].get_center_ind();
      size_t jnuc=orbshells[inus].get_center_ind();

      // If imus==inus, we need to take care that we count
      // every term only once; on the off-diagonal we get
      // every term twice.
      double fac=2.0;
      if(imus==inus)
	fac=1.0;

      for(size_t ias=0;ias<auxshells.size();ias++) {
	size_t Na=auxshells[ias].get_Nbf();
	size_t anuc=auxshells[ias].get_center_ind();

	if(inuc==jnuc && jnuc==anuc)
	  // Contributions vanish
	  continue;

	// Compute (a|mn)
	deri.compute(&auxshells[ias],&dummy,&orbshells[imus],&orbshells[inus]);

	// Expansion coefficients
	arma::vec ca=c.subvec(auxshells[ias].get_first_ind(),auxshells[ias].get_last_ind());

	// Compute forces
	const static int index[]={0, 1, 2, 6, 7, 8, 9, 10, 11};
	const size_t Nidx=sizeof(index)/sizeof(index[0]);
	arma::vec ders(Nidx);
	ders.zeros();

	for(size_t iid=0;iid<Nidx;iid++) {
	  // Index is
	  int ic=index[iid];
	  arma::vec hlp(Na);
	  
	  erip=deri.getp(ic);
	  hlp.zeros();
	  for(size_t iia=0;iia<Na;iia++)
	    for(size_t iimu=0;iimu<Nmu;iimu++) {
	      size_t imu=orbshells[imus].get_first_ind()+iimu;
	      for(size_t iinu=0;iinu<Nnu;iinu++) {
		size_t inu=orbshells[inus].get_first_ind()+iinu;
		
		// The contracted integral
		hlp(iia)+=(*erip)[(iia*Nmu+iimu)*Nnu+iinu]*P(imu,inu);
	      }
	    }
	  ders(iid)=fac*arma::dot(hlp,ca);
	}
	
	// Increment forces
	for(int ic=0;ic<3;ic++) {
#ifdef _OPENMP
	  fwrk(3*anuc+ic)-=ders(ic);
	  fwrk(3*inuc+ic)-=ders(ic+3);
	  fwrk(3*jnuc+ic)-=ders(ic+6);
#else
	  f(3*anuc+ic)-=ders(ic);
	  f(3*inuc+ic)-=ders(ic+3);
	  f(3*jnuc+ic)-=ders(ic+6);
#endif
	}

	/*
	std::ostringstream msg;
	msg << "\nias = " << ias << ", imu = " << imus << ", inu = " << inus << ".\n";
	msg << "am_a = " << auxshells[ias].get_am() << ", am_i = " << orbshells[imus].get_am() << ", am_j = " << orbshells[inus].get_am() << ".";
	interpret_force(ders).print(msg.str());
	*/
      }
    }
    
#ifdef _OPENMP
#pragma omp critical
    // Sum results together
    f+=fwrk;
#endif
  } // end parallel section

  return f;
}

arma::mat DensityFit::calc_K(const arma::mat & Corig, const std::vector<double> & occo, size_t memlimit) const {
  // Compute orbital block size. The memory required for one orbital
  // is (also need memory for the transformation)
  const size_t mem1=2*Nbf*Naux;
  // so the block size is
  const size_t blocksize=memlimit/(mem1*sizeof(double));

  // Count number of orbitals
  size_t Nmo=0;
  for(size_t i=0;i<occo.size();i++)
    if(occo[i]>0)
      Nmo++;

  // Number of orbital blocks is thus
  const size_t Nblocks=(size_t) ceil(Nmo*1.0/blocksize);

  // Collect orbitals to use
  arma::mat C(Nbf,Nmo);
  std::vector<double> occs(Nmo);
  {
    size_t io=0;
    for(size_t i=0;i<occo.size();i++)
      if(occo[i]>0) {
	// Store orbital and occupation number
	C.col(io)=Corig.col(i);
	occs[io]=occo[i];
	io++;
      }
  }

  // Three-center integrals \f $(i \mu|P)$ \f
  arma::mat iuP(blocksize*Nbf,Naux);
  iuP.zeros();

  // Returned matrix
  arma::mat K(Nbf,Nbf);
  K.zeros();

  // Loop over orbital blocks
  for(size_t iblock=0;iblock<Nblocks;iblock++) {

    // Starting orbital index in the current block
    size_t orbstart=iblock*blocksize;
    // How many orbitals in the current block
    size_t Norb=std::min(blocksize,Nmo);

    //    printf("Orbitals %i - %i\n",(int) orbstart+1,(int) (orbstart+Norb));

    if(direct) {
      // Loop over basis function pairs
      for(size_t ip=0;ip<orbpairs.size();ip++) {
	size_t imus=orbpairs[ip].is;
	size_t inus=orbpairs[ip].js;

#ifdef SCREENING
	// Do we need to compute the integral?
	if(screen(imus,inus)<SCRTHR)
	  continue;
#endif

	// Parallellize auxiliary loop to avoid critical sections.
#ifdef _OPENMP
#pragma omp parallel
#endif
	{
	  ERIWorker eri(maxam,maxcontr);
	  const std::vector<double> * erip;


#ifdef _OPENMP
#pragma omp for schedule(dynamic)
#endif
	  for(size_t ias=0;ias<auxshells.size();ias++) {

	    // Amount of functions on shells
	    size_t Na=auxshells[ias].get_Nbf();
	    size_t Nmu=orbshells[imus].get_Nbf();
	    size_t Nnu=orbshells[inus].get_Nbf();

	    // Compute (a|mn)
	    eri.compute(&auxshells[ias],&dummy,&orbshells[imus],&orbshells[inus]);
	    erip=eri.getp();

	    // Increment iuP. Loop over auxiliary functions.
	    for(size_t iia=0;iia<Na;iia++) {
	      size_t ia=auxshells[ias].get_first_ind()+iia;

	      // Loop over functions on the mu shell
	      for(size_t imu=0;imu<Nmu;imu++) {
		size_t mu=orbshells[imus].get_first_ind()+imu;

		// Loop over orbitals
		for(size_t io=0;io<Norb;io++)

		  // Loop over functions on the nu shell
		  for(size_t inu=0;inu<Nnu;inu++) {
		    size_t nu=orbshells[inus].get_first_ind()+inu;

		    iuP(io*Nbf+mu,ia)+=C(nu,orbstart+io)*(*erip)[(iia*Nmu+imu)*Nnu+inu];
		  }
	      }
	    }

	    // Account for integral symmetry
	    if(imus!=inus) {
	      for(size_t iia=0;iia<Na;iia++) {
		size_t ia=auxshells[ias].get_first_ind()+iia;

		for(size_t imu=0;imu<Nmu;imu++) {
		  size_t mu=orbshells[imus].get_first_ind()+imu;

		  for(size_t io=0;io<Norb;io++)

		    for(size_t inu=0;inu<Nnu;inu++) {
		      size_t nu=orbshells[inus].get_first_ind()+inu;

		      iuP(io*Nbf+nu,ia)+=C(mu,orbstart+io)*(*erip)[(iia*Nmu+imu)*Nnu+inu];
		    }
		}
	      }
	    }
	  }
	}
      }
    } else {
      // Loop over functions
      for(size_t mu=0;mu<Nbf;mu++)
	for(size_t io=0;io<Norb;io++)
	  for(size_t nu=0;nu<Nbf;nu++)
	    for(size_t ia=0;ia<Naux;ia++)
	      iuP(io*Nbf+mu,ia)+=C(nu,orbstart+io)*a_munu[idx(ia,mu,nu)];
    }

    // Plug in the half inverse, so iuP -> BiuQ
    iuP=iuP*ab_invh;

    // Increment the exchange matrix. Loop over functions
    for(size_t mu=0;mu<Nbf;mu++)
      for(size_t nu=0;nu<=mu;nu++) {

	// Compute the matrix element
	for(size_t io=0;io<Norb;io++) {
	  // Kuv -> BiuQ*BivQ
	  //  	  for(size_t ia=0;ia<Naux;ia++)
	  //	    K(mu,nu)+=occs[orbstart+io]*iuP(io*Nbf+mu,ia)*iuP(io*Nbf+nu,ia);

	  K(mu,nu)+=occs[orbstart+io]*arma::dot(iuP.row(io*Nbf+mu),iuP.row(io*Nbf+nu));
	}

	// and symmetrize
	K(nu,mu)=K(mu,nu);
      }

  } // End loop over orbital blocks

  return K;
}

size_t DensityFit::get_Norb() const {
  return Nbf;
}

size_t DensityFit::get_Naux() const {
  return Naux;
}

double DensityFit::get_a_munu(size_t ia, size_t imu, size_t inu) const {
  if(!direct)
    return a_munu[idx(ia,imu,inu)];
  else {
    ERROR_INFO();
    throw std::runtime_error("get_a_munu not implemented for direct calculations!\n");
  }

  // Dummy return clause
  return 0.0;
}

arma::mat DensityFit::get_ab_inv() const {
  return ab_inv;
}
