/*
  A simple 2D hydro code
  (C) Romain Teyssier : CEA/IRFU           -- original F90 code
  (C) Pierre-Francois Lavallee : IDRIS      -- original F90 code
  (C) Guillaume Colin de Verdiere : CEA/DAM -- for the C version
*/

/*

This software is governed by the CeCILL license under French law and
abiding by the rules of distribution of free software.  You can  use, 
modify and/ or redistribute the software under the terms of the CeCILL
license as circulated by CEA, CNRS and INRIA at the following URL
"http://www.cecill.info". 

As a counterpart to the access to the source code and  rights to copy,
modify and redistribute granted by the license, users are provided only
with a limited warranty  and the software's author,  the holder of the
economic rights,  and the successive licensors  have only  limited
liability. 

In this respect, the user's attention is drawn to the risks associated
with loading,  using,  modifying and/or developing or reproducing the
software by the user in light of its specific status of free software,
that may mean  that it is complicated to manipulate,  and  that  also
therefore means  that it is reserved for developers  and  experienced
professionals having in-depth computer knowledge. Users are therefore
encouraged to load and test the software's suitability as regards their
requirements in conditions enabling the security of their systems and/or 
data to be ensured and,  more generally, to use and operate it in the 
same conditions as regards security. 

The fact that you are presently reading this means that you have had
knowledge of the CeCILL license and that you accept its terms.

*/

#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <stdio.h>

#include "parametres.h"
#include "utils.h"
#include "oclRiemann.h"
#include "oclInit.h"
#include "ocltools.h"

void
oclRiemann(const long narray,
           const double Hsmallr,
           const double Hsmallc, const double Hgamma,
           const long Hniter_riemann, const long Hnvar, const long Hnxyt,
           const int slices, const int Hstep, cl_mem qleft, cl_mem qright, cl_mem qgdnv, cl_mem sgnm) {
  // Local variables
  cl_int err = 0;

  WHERE("riemann");

  OCLSETARG12(ker[Loop1KcuRiemann], qleft, qright, sgnm, qgdnv, Hnxyt, narray, Hsmallc, Hgamma, Hsmallr, Hniter_riemann,
              slices, Hstep);
  oclLaunchKernel(ker[Loop1KcuRiemann], cqueue, Hnxyt * slices, THREADSSZ, __FILE__, __LINE__);

  if (Hnvar > IP + 1) {
    OCLSETARG09(ker[Loop10KcuRiemann], qleft, qright, sgnm, qgdnv, narray, Hnvar, Hnxyt, slices, Hstep);
    oclLaunchKernel(ker[Loop10KcuRiemann], cqueue, Hnxyt * slices, THREADSSZ, __FILE__, __LINE__);
  }
}                               // riemann


//EOF
