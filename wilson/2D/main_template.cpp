#include <iostream>
#include <fstream>
#include <iomanip>
#include <string.h>
#include <cmath>
#include <complex>
#include <chrono>

using namespace std::chrono;
using namespace std;

#define LX __LX__
#define LY __LY__
#define D 2
#define NEV 24
#define NKR 32
#define PI 3.141592653589793
#define TWO_PI 6.283185307179586

typedef complex<double> Complex;
#define I Complex(0,1.0)
#define cUnit Complex(1.0,0)

#include "utils.h"
#include "latHelpers.h"
#include "measurementHelpers.h"
#include "fermionHelpers.h"
#include "dOpHelpers.h"
#include "inverters.h"
#include "hmcHelpers.h"

#ifdef USE_ARPACK
#include "arpack_interface_wilson.h"
#endif

//Dimension dependent HMC functions defined in main file
//----------------------------------------------------------------------------
void trajectory(double*** mom, Complex*** gauge,
		Complex*** phi, param_t p, int iter);
int hmc(Complex*** gauge, param_t p, int iter);
void forceU(double* fU, Complex*** gauge, param_t p);
void update_mom(double*** fU, double*** fD,
		double*** mom, double dtau);
void update_gauge(Complex*** gauge, double*** mom, double dtau);
//----------------------------------------------------------------------------

//Global variables.
int hmccount = 0;
double expdHAve = 0.0;
double dHAve = 0.0;

global_struct gst;

int main(int argc, char **argv) {

  gst.tot_time = 0.0;
  gst.inv_time = 0.0;
  gst.matmul_time = 0.0;

  param_t p;
  
  p.beta = atof(argv[1]); 
  p.iterHMC = atoi(argv[2]);
  p.therm = atoi(argv[3]);
  p.skip = atoi(argv[4]);
  p.chkpt = atoi(argv[5]);
  p.checkpointStart = atoi(argv[6]);  
  p.nstep = atoi(argv[7]);
  p.tau = atof(argv[8]);
  
  p.smearIter = atoi(argv[9]);
  p.alpha = atof(argv[10]);  
  long iseed = (long)atoi(argv[11]);
  //Pseudo RNG seed
  srand48(iseed);
  
  if(atoi(argv[12]) == 0) 
    p.dynamic = false;
  else
    p.dynamic = true;

  p.m = atof(argv[13]);
  p.maxIterCG = atoi(argv[14]);
  p.eps = atof(argv[15]);
  
  //Arpack params
  p.nKr = NKR;
  p.nEv = NEV;
  p.arpackTol = atof(argv[16]);
  p.arpackMaxiter = atoi(argv[17]);
  p.polyACC = atoi(argv[18]);
  p.amax = atof(argv[19]);
  p.amin = atof(argv[20]);
  p.n_poly = atoi(argv[21]);

  //Measurements
  if(atoi(argv[22]) == 0) p.measPL = false;
  else p.measPL = true;

  if(atoi(argv[23]) == 0) p.measWL = false;
  else p.measWL = true;
  
  if(atoi(argv[24]) == 0) p.measPC = false;
  else p.measPC = true;
  
  if(atoi(argv[25]) == 0) p.measVT = false;
  else p.measVT = true;  
  
  //Topology
  double top = 0.0;
  int top_int = 0;
  int top_old = 0;
  int top_stuck = 0;

  int histL = 41;
  int histQ[histL];
  double plaqSum = 0.0;
  int index = 0;
  for(int i = 0; i < histL; i++) histQ[i] = 0;

  buff_allocs();

  Complex*** gaugex = gst.b01;
  Complex*** buff1 = gst.b02;
  Complex*** buff2 = gst.b03;
  Complex*** buff3 = gst.b04;

  zeroLat(gaugex);

  int count = 0;
  string name;
  fstream outPutFile;
  
  int accept;
  int accepted = 0;
  char fname[256];
  FILE *fp;

  printParams(p);  
  gaussStart(gaugex,p);  // hot start

  //Start simulation
  double time0 = -((double)clock());
  int iter_offset = 0;
  int iter = 0;
  cout << setprecision(16);

  auto start = high_resolution_clock::now();

  if(p.checkpointStart > 0) {

    //Read in gauge field if requested
    //---------------------------------------------------------------------
    name = "gauge/gauge";
    constructName(name, p);
    name += "_traj" + to_string(p.checkpointStart) + ".dat";	
    readGaugeLattice(gaugex, name);
    iter_offset = p.checkpointStart;    
  } else {

    //Thermalise from random start
    //---------------------------------------------------------------------
    for(iter=0; iter<p.therm; iter++){  
      //Perform HMC step
      accept = hmc(gaugex, p, iter);
      double time = time0 + clock();
      cout << fixed << iter+1 << " ";              //Iteration
      cout << time/CLOCKS_PER_SEC << " " << endl;  //Time
    }

    for(iter=p.therm; iter<2*p.therm; iter++){
      //Perform HMC step with accept/reject
      accept = hmc(gaugex, p, iter);
      double time = time0 + clock();
      cout << fixed << iter+1 << " ";             //Iteration
      cout << time/CLOCKS_PER_SEC << " " << endl; //Time
    }
    iter_offset = 2*p.therm;    
  }

  // Measure top charge on mother ensemble
  top = measTopCharge(gaugex, p);
  top_old = round(top);

  //Begin thermalised trajectories
  //---------------------------------------------------------------------
  for(iter=iter_offset; iter<p.iterHMC + iter_offset; iter++){

    //Perform HMC step
    accept = hmc(gaugex, p, iter);

    //HMC acceptance
    accepted += accept;

    //Measure the topological charge if trajectory is accepted
    //---------------------------------------------------------------------
    if(accept == 1) {
      
      top = measTopCharge(gaugex, p);
      top_int = round(top);
      name = "data/top/top_charge";
      constructName(name, p);
      name += ".dat";
      sprintf(fname, "%s", name.c_str());
      fp = fopen(fname, "a");
      fprintf(fp, "%d %d\n", iter, top_int);
      fclose(fp);
      
      index = top_int + (histL-1)/2;
      histQ[index]++;
      if(top_old == top_int) top_stuck++;
      top_old = top_int;
    }

    //Perform Measurements
    //---------------------------------------------------------------------
    if( (iter+1)%p.skip == 0) {

      count++; //Number of measurements taken

      //Checkpoint the gauge field?
      if( (iter+1)%p.chkpt == 0) {	  
	name = "gauge/gauge";
	constructName(name, p);
	name += "_traj" + to_string(iter+1) + ".dat";
	writeGaugeLattice(gaugex, name);
      }
      
      //Plaquette action
      double plaq = measPlaq(gaugex);
      plaqSum += plaq;

      //Dump simulation data to stdout
      double time = time0 + clock();
      cout << fixed << setprecision(16) << iter+1 << " "; //Iteration
      cout << time/CLOCKS_PER_SEC << " ";                 //Time
      cout << plaqSum/count << " ";                       //Action
      cout << (double)top_stuck/(accepted) << " ";        //P(stuck)
      cout << expdHAve/hmccount << " ";                   //Average exp(-dH)
      cout << dHAve/hmccount << " ";                      //Average dH
      cout << (double)accepted/(count*p.skip) << " ";     //Acceptance
      cout << top_int << endl;                            //T charge
	
      //Dump simulation data to file
      name = "data/data/data"; //I cannot make bricks without clay!
      constructName(name, p);
      name += ".dat";	
      sprintf(fname, "%s", name.c_str());	
      fp = fopen(fname, "a");	
      fprintf(fp, "%d %.16e %.16e %.16e %.16e %.16e %.16e %d\n",
	      iter+1,
	      time/CLOCKS_PER_SEC,
	      plaqSum/count,
	      (double)top_stuck/(accepted),
	      expdHAve/hmccount,
	      dHAve/hmccount,
	      (double)accepted/(count*p.skip),
	      top_int);
      fclose(fp);

      //Update topoligical charge histogram
      name = "data/top/top_hist";
      constructName(name, p);
      name += ".dat";
      sprintf(fname, "%s", name.c_str());
      fp = fopen(fname, "w");
      for(int i=0; i<histL; i++) fprintf(fp, "%d %d\n", i - (histL-1)/2, histQ[i]);
      fclose(fp);

      //Physical observables
      //-------------------------------------------------------------      
      //Gauge observables
      //if(p.measPL || p.measWL) measWilsonLoops(gauge, iter, p);
      
      //Pion Correlation
      if(p.measPC) measPionCorrelation(gaugex, top_old, iter, p);
      
      //Vacuum Trace
      //if(p.measVT) measVacuumTrace(gauge, top_old, iter, p);
      //-------------------------------------------------------------
    }
  }

  auto stop = high_resolution_clock::now();
  auto duration = duration_cast<microseconds>(stop - start);
  gst.tot_time += duration.count();

  cout << "Total execution time : " << gst.tot_time/(1.0e6) << endl;
  cout << "Inversions time : " << gst.inv_time/(1.0e6) << endl;
  cout << "Matmuls time (excluding g3 application) : " << gst.matmul_time/(1.0e6) << endl;

  buff_frees();

  return 0;
}

// HMC Routines
//---------------------------------------------------------------------
int hmc(Complex*** gauge, param_t p, int iter) {

  int accept = 0;

  double*** mom = gst.c01;
  
  Complex*** gaugeOld = gst.b02;
  Complex*** phi = gst.b03;
  Complex*** chi = gst.b04;

  double H, Hold;

  copyLat(gaugeOld, gauge);
  zeroLat(mom); 
  zeroField(phi);
  zeroField(chi);
  H = 0.0;
  Hold = 0.0;

  // init mom[LX][LY][D]  <mom^2> = 1;
  gaussReal_F(mom); 
  
  if(p.dynamic == true) {    
    //Create gaussian distributed fermion field chi. chi[LX][LY] E exp(-chi^* chi)
    gaussComplex_F(chi, p);
    //Create pseudo fermion field phi = D chi
    g3Dpsi(phi, chi, gauge, p);    
  }

  if (iter >= p.therm) Hold = measAction(mom, gauge, chi, p, false);

  trajectory(mom, gauge, phi, p, iter);

  if (iter >= p.therm) H = measAction(mom, gauge, phi, p, true);
  
  if (iter >= 2*p.therm) {      
    hmccount++;
    expdHAve += exp(-(H-Hold));
    dHAve += (H-Hold);
  }

  // Metropolis accept/reject step
  if (iter >= p.therm) {    
    if ( drand48() > exp(-(H-Hold)) ) copyLat(gauge, gaugeOld);
    else accept = 1;
  }

  return accept;
}

void trajectory(double*** mom, Complex*** gauge,
		Complex*** phi, param_t p, int iter) {

  Complex*** guess = gst.b05;
  //gauge force
  double*** fU = gst.c02;
  //fermion fermion
  double*** fD = gst.c03;
  //Both arrays are zeroed in forceU/D function call

  double dtau = p.tau/p.nstep;
  double H = 0.0;
#ifdef USE_ARPACK
  zeroField(guess);
  ////deflate using phi as source
  ////Deflation eigenvectors
  //Complex defl_evecs[NEV][LX][LY][2];
  ////Deflation eigenvalues
  //Complex defl_evals[NEV];
  
  //copyField(guess, phi);
  //arpack_solve(gauge, defl_evecs, defl_evals, 0, 0, p);
  //deflate(guess, phi, defl_evecs, defl_evals, p);
#else
  zeroField(guess);
#endif

  //Initial half step.
  //P_{1/2} = P_0 - dtau/2 * (fU - fD)
  forceU(fU, gauge, p);
  forceD(fD, gauge, phi, guess, p);
  update_mom(fU, fD, mom, 0.5*dtau);  
  
  for(int k=1; k<p.nstep; k++) {
    
    //U_{k} = exp(i dtau P_{k-1/2}) * U_{k-1}
    update_gauge(gauge, mom, dtau);
    
    //P_{k+1/2} = P_{k-1/2} - dtau * (fU - fD)
    forceU(fU, gauge, p);
    forceD(fD, gauge, phi, guess, p);
    update_mom(fU, fD, mom, dtau);
  }
  
  //Final half step.
  //U_{n} = exp(i dtau P_{n-1/2}) * U_{n-1}
  update_gauge(gauge, mom, dtau);
  
  //P_{n} = P_{n-1/2} - dtau/2 * (fU - fD)
  forceU(fU, gauge, p);
  forceD(fD, gauge, phi, guess, p);
  update_mom(fU, fD, mom, 0.5*dtau);

  //trajectory complete
}
//-------------------------------------------------------------------------------
