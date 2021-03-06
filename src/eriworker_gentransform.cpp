/*
 *                This source code is part of
 *
 *                     E  R  K  A  L  E
 *                             -
 *                       DFT from Hel
 *
 * Written by Susi Lehtola, 2010-2013
 * Copyright (c) 2010-2013, Susi Lehtola
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "solidharmonics.h"
#include "eriworker.h"

int main(void) {
  enum {i, j, k, l} trans;

  printf("#include \"eriworker.h\"\n");
  printf("#include \"solidharmonics.h\"\n");
  printf("#include \"mathf.h\"\n\n");

  for(int it=0;it<4;it++)  {
    switch(it) {
    case(0):
      trans=i;
      break;
    case(1):
      trans=j;
      break;
    case(2):
	trans=k;
	break;
    case(3):
      trans=l;
      break;
    }

    std::string arg1, arg2, arg3;

    switch(trans) {
    case(i):
      arg1="Nj";
      arg2="Nk";
      arg3="Nl";
      break;
    case(j):
      arg1="Ni";
      arg2="Nk";
      arg3="Nl";
      break;
    case(k):
      arg1="Ni";
      arg2="Nj";
      arg3="Nl";
      break;
    case(l):
      arg1="Ni";
      arg2="Nj";
      arg3="Nk";
      break;
    }

    const char ijkl[]="ijkl";

    // Individual transforms
    for(int am=0;am<LIBINT_MAX_AM;am++) {
      // Get transformation matrix
      arma::mat transmat=Ylm_transmat(am);
      size_t Nsph=transmat.n_rows;
      size_t Ncart=transmat.n_cols;

      // Print function header
      printf("static void transform_%c%i(size_t %s, size_t %s, size_t %s, const std::vector<double> *input, std::vector<double> *output) {\n",ijkl[it],am,arg1.c_str(),arg2.c_str(),arg3.c_str());
      printf("  (*output).clear();\n");
      printf("  (*output).resize(%i*%s*%s*%s,0.0);\n",(int) Nsph,arg1.c_str(),arg2.c_str(),arg3.c_str());

      // Transform loops
      switch(trans) {
      case(i):
	printf("  for(size_t jj=0;jj<Nj;jj++)\n");
	printf("    for(size_t kk=0;kk<Nk;kk++)\n");
	printf("      for(size_t ll=0;ll<Nl;ll++) {\n");
	for(size_t iin=0;iin<Ncart;iin++)
	  for(size_t iout=0;iout<Nsph;iout++)
	    if(transmat(iout,iin)!=0.0)
	      printf("        (*output)[((%2i*Nj+jj)*Nk+kk)*Nl+ll] += % .16e * (*input)[((%2i*Nj+jj)*Nk+kk)*Nl+ll];\n",(int) iout,transmat(iout,iin),(int) iin);
	break;

      case(j):
	printf("  for(size_t ii=0;ii<Ni;ii++)\n");
	printf("    for(size_t kk=0;kk<Nk;kk++)\n");
	printf("      for(size_t ll=0;ll<Nl;ll++) {\n");
	for(size_t jin=0;jin<Ncart;jin++)
	  for(size_t jout=0;jout<Nsph;jout++)
	    if(transmat(jout,jin)!=0.0)
	      printf("        (*output)[((ii*%2i+%2i)*Nk+kk)*Nl+ll] += % .16e * (*input)[((ii*%2i+%2i)*Nk+kk)*Nl+ll];\n",(int) Nsph,(int) jout,transmat(jout,jin),(int) Ncart,(int) jin);
	break;

      case(k):
	printf("  for(size_t ii=0;ii<Ni;ii++)\n");
	printf("    for(size_t jj=0;jj<Nj;jj++)\n");
	printf("      for(size_t ll=0;ll<Nl;ll++) {\n");
	for(size_t kin=0;kin<Ncart;kin++)
	  for(size_t kout=0;kout<Nsph;kout++)
	    if(transmat(kout,kin)!=0.0)
	      printf("        (*output)[((ii*Nj+jj)*%2i+%2i)*Nl+ll] += % .16e * (*input)[((ii*Nj+jj)*%2i+%2i)*Nl+ll];\n",(int) Nsph,(int) kout,transmat(kout,kin),(int) Ncart,(int) kin);
	break;

      case(l):
	printf("  for(size_t ii=0;ii<Ni;ii++)\n");
	printf("    for(size_t jj=0;jj<Nj;jj++)\n");
	printf("      for(size_t kk=0;kk<Nk;kk++) {\n");
	for(size_t lin=0;lin<Ncart;lin++)
	  for(size_t lout=0;lout<Nsph;lout++)
	    if(transmat(lout,lin)!=0.0)
	      printf("        (*output)[((ii*Nj+jj)*Nk+kk)*%2i+%2i] += % .16e * (*input)[((ii*Nj+jj)*Nk+kk)*%2i+%2i];\n",(int) Nsph,(int) lout,transmat(lout,lin),(int) Ncart,(int) lin);
	break;
      }

      printf("      }\n");
      printf("}\n\n");
    }

    /// Main driver
    printf("void IntegralWorker::transform_%c(int am, size_t %s, size_t %s, size_t %s) {\n",ijkl[it],arg1.c_str(),arg2.c_str(),arg3.c_str());
    // Table of drivers
    printf("  static void (*f[%i])(size_t, size_t, size_t, const std::vector<double> *, std::vector<double> *)={\n",LIBINT_MAX_AM);
    for(int am=0;am<LIBINT_MAX_AM;am++) {
      printf("    transform_%c%i",ijkl[it],am);
      if(am<LIBINT_MAX_AM-1)
	printf(",");
      printf("\n");
    }
    printf("  };\n");

    // Call driver
    printf("    f[am](%s,%s,%s,input,output);\n",arg1.c_str(),arg2.c_str(),arg3.c_str());

    // Swap arrays
    printf("  std::swap(input,output);\n");
    printf("}\n");

  }
  return 0;
}
