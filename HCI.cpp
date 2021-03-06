#include "global.h"
#include <stdlib.h>
#include <stdio.h>
#include <fstream>
#include "Determinants.h"
#include "input.h"
#include "integral.h"
#include "Hmult.h"
#include "HCIbasics.h"
#include "Davidson.h"
#include <Eigen/Dense>
#include <Eigen/Core>
#include <set>
#include <list>
#include <tuple>
#include "boost/format.hpp"
#ifndef SERIAL
#include <boost/mpi/environment.hpp>
#include <boost/mpi/communicator.hpp>
#include <boost/mpi.hpp>
#endif
#include "communicate.h"
#include <boost/interprocess/managed_shared_memory.hpp>

using namespace Eigen;
using namespace boost;
int HalfDet::norbs = 1; //spin orbitals
int Determinant::norbs = 1; //spin orbitals
int Determinant::EffDetLen = 1;
Eigen::Matrix<size_t, Eigen::Dynamic, Eigen::Dynamic> Determinant::LexicalOrder ;
//get the current time
double getTime() {
  struct timeval start;
  gettimeofday(&start, NULL);
  return start.tv_sec + 1.e-6*start.tv_usec;
}
double startofCalc = getTime();

boost::interprocess::shared_memory_object int2Segment;
boost::interprocess::mapped_region regionInt2;
boost::interprocess::shared_memory_object int2SHMSegment;
boost::interprocess::mapped_region regionInt2SHM;



void readInput(string input, vector<std::vector<int> >& occupied, schedule& schd, int nelec);


int main(int argc, char* argv[]) {
#ifndef SERIAL
  boost::mpi::environment env(argc, argv);
  boost::mpi::communicator world;
#endif
  startofCalc=getTime();
  srand(startofCalc+world.rank());


  //make the shared memory stuff
  //permission.set_unrestricted();
  string hciint2 = "HCIint2" + to_string(static_cast<long long>(time(NULL) % 1000000));
  string hciint2shm = "HCIint2shm" + to_string(static_cast<long long>(time(NULL) % 1000000));
  int2Segment = boost::interprocess::shared_memory_object(boost::interprocess::open_or_create, hciint2.c_str(), boost::interprocess::read_write);
  int2SHMSegment = boost::interprocess::shared_memory_object(boost::interprocess::open_or_create, hciint2shm.c_str(), boost::interprocess::read_write);




  std::cout.precision(15);

  //read the hamiltonian (integrals, orbital irreps, num-electron etc.)
  twoInt I2; oneInt I1; int nelec; int norbs; double coreE, eps;
  std::vector<int> irrep;
  readIntegrals("FCIDUMP", I2, I1, nelec, norbs, coreE, irrep);

  norbs *=2;
  Determinant::norbs = norbs; //spin orbitals
  HalfDet::norbs = norbs; //spin orbitals
  Determinant::EffDetLen = norbs/64+1;
  Determinant::initLexicalOrder(nelec);
  if (Determinant::EffDetLen >DetLen) {
    cout << "change DetLen in global.h to "<<Determinant::EffDetLen<<" and recompile "<<endl;
    exit(0);
  }


  //initialize the heatbath integral
  std::vector<int> allorbs;
  for (int i=0; i<norbs/2; i++)
    allorbs.push_back(i);
  twoIntHeatBath I2HB(1.e-10);
  twoIntHeatBathSHM I2HBSHM(1.e-10);
  if (mpigetrank() == 0) I2HB.constructClass(allorbs, I2, norbs/2);
  I2HBSHM.constructClass(norbs/2, I2HB);

  int num_thrds;
  std::vector<std::vector<int> > HFoccupied; //double epsilon1, epsilon2, tol, dE;
  schedule schd;
  if (mpigetrank() == 0) readInput("input.dat", HFoccupied, schd, nelec); //epsilon1, epsilon2, tol, num_thrds, eps, dE);
  mpi::broadcast(world, schd, 0);

#ifndef Complex
  if (schd.doSOC) {
    cout << "doSOC option only works with the complex coefficients. Uncomment the -Dcomplex in the make file and recompile."<<endl;
    exit(0);
  }
#else
  if (schd.doSOC) 
    readSOCIntegrals(I1, norbs);
#endif

#ifndef SERIAL
  mpi::broadcast(world, HFoccupied, 0);
  mpi::broadcast(world, schd, 0);
#endif

  //have the dets, ci coefficient and diagnoal on all processors
  vector<MatrixXx> ci(schd.nroots, MatrixXx::Zero(HFoccupied.size(),1)); 

  //make HF determinant
  vector<Determinant> Dets(HFoccupied.size());
  for (int d=0;d<HFoccupied.size(); d++) {
    for (int i=0; i<HFoccupied[d].size(); i++) {
      Dets[d].setocc(HFoccupied[d][i], true);
    }
  }

  if (mpigetrank() == 0) {
    for (int j=0; j<ci[0].rows(); j++) 
      ci[0](j,0) = 1.0;
    ci[0] = ci[0]/ci[0].norm();
  }

  mpi::broadcast(world, ci, 0);
  //b.col(i) = b.col(i)/b.col(i).norm();


  vector<double> E0 = HCIbasics::DoVariational(ci, Dets, schd, I2, I2HBSHM, irrep, I1, coreE, nelec, schd.DoRDM);


  std::string efile;
  efile = str(boost::format("%s%s") % schd.prefix.c_str() % "/hci.e" );
  FILE* f = fopen(efile.c_str(), "wb");      
  for(int j=0;j<E0.size();++j) {
    fwrite( &E0[j], 1, sizeof(double), f);
  }
  fclose(f);


  //print the 5 most important determinants and their weights
  for (int root=0; root<schd.nroots; root++) {
    pout << "### IMPORTANT DETERMINANTS FOR STATE: "<<root<<endl;
    MatrixXx prevci = 1.*ci[root];
    for (int i=0; i<5; i++) {
      compAbs comp;
      int m = distance(&prevci(0,0), max_element(&prevci(0,0), &prevci(0,0)+prevci.rows(), comp));
      pout <<"#"<< i<<"  "<<prevci(m,0)<<"  "<<Dets[m]<<endl;
      prevci(m,0) = 0.0;
    }
  }
  pout << "### PERFORMING PERTURBATIVE CALCULATION"<<endl;

  if (schd.quasiQ) {    
    double bkpepsilon2 = schd.epsilon2;
    schd.epsilon2 = schd.quasiQEpsilon;
    for (int root=0; root<schd.nroots;root++) {
      E0[root] += HCIbasics::DoPerturbativeDeterministic(Dets, ci[root], E0[root], I1, I2, I2HBSHM, irrep, schd, coreE, nelec, true);
      ci[root] = ci[root]/ci[root].norm();
    }
    schd.epsilon2 = bkpepsilon2;
  }

  world.barrier();
  boost::interprocess::shared_memory_object::remove(hciint2.c_str());

  //now do the perturbative bit
  if (!schd.stochastic && schd.nblocks == 1) {
    //HCIbasics::DoPerturbativeDeterministicLCC(Dets, ci, E0, I1, I2, I2HB, irrep, schd, coreE, nelec);
    for (int root=0; root<schd.nroots;root++) 
      HCIbasics::DoPerturbativeDeterministic(Dets, ci[root], E0[root], I1, I2, I2HBSHM, irrep, schd, coreE, nelec);
  }
  else if (schd.SampleN != -1 && schd.singleList && abs(schd.epsilon2Large-1000.0) > 1e-5){
    for (int root=0; root<schd.nroots;root++) 
      //HCIbasics::DoPerturbativeStochastic2SingleListDoubleEpsilon2(Dets, ci[root], E0[root], I1, I2, I2HBSHM, irrep, schd, coreE, nelec, root);
      HCIbasics::DoPerturbativeStochastic2SingleListDoubleEpsilon2OMPTogether(Dets, ci[root], E0[root], I1, I2, I2HBSHM, irrep, schd, coreE, nelec, root);
  }
  else if (schd.SampleN != -1 && schd.singleList){
    for (int root=0; root<schd.nroots;root++) 
      HCIbasics::DoPerturbativeStochastic2SingleList(Dets, ci[root], E0[root], I1, I2, I2HBSHM, irrep, schd, coreE, nelec, root);
  }
  /*
  else if (!schd.stochastic) {
    HCIbasics::DoBatchDeterministic(Dets, ci[0], E0[0], I1, I2, I2HB, irrep, schd, coreE, nelec);
  }
  else if (schd.SampleN == -1 && schd.singleList){
    HCIbasics::DoPerturbativeStochasticSingleList(Dets, ci[0], E0[0], I1, I2, I2HB, irrep, schd, coreE, nelec);
  }
  else if (schd.SampleN == -1 && !schd.singleList){
    HCIbasics::DoPerturbativeStochastic(Dets, ci[0], E0[0], I1, I2, I2HB, irrep, schd, coreE, nelec);
  }
  */
  else { 
    world.barrier();
    boost::interprocess::shared_memory_object::remove(hciint2shm.c_str());
    cout << "Error here"<<endl;
    exit(0);
    //Here I will implement the alias method
    //HCIbasics::DoPerturbativeStochastic2(Dets, ci[0], E0[0], I1, I2, I2HB, irrep, schd, coreE, nelec);
  }

  world.barrier();
  boost::interprocess::shared_memory_object::remove(hciint2shm.c_str());

  return 0;
}
