#include "integral.h"
#include <fstream>
#include "string.h"
#include <boost/algorithm/string.hpp>
#include <algorithm>
#include "math.h"
#ifndef SERIAL
#include <boost/mpi/environment.hpp>
#include <boost/mpi/communicator.hpp>
#include <boost/mpi.hpp>
#endif
#include "communicate.h"
#include "global.h"

using namespace boost;
bool myfn(double i, double j) { return fabs(i)<fabs(j); }

#ifdef Complex
void readSOCIntegrals(oneInt& I1, int norbs) {
  if (mpigetrank() == 0) {
    vector<string> tok;
    string msg;

    //Read SOC.X
    {
      ifstream dump("SOC.X");
      int N;
      dump >> N;
      if (N != norbs/2) {
	cout << "number of orbitals in SOC.X should be equal to norbs in the input file."<<endl;
	cout << N <<" != "<<norbs<<endl;
	exit(0);
      }
      
      //I1soc[1].store.resize(N*(N+1)/2, 0.0);
      while(!dump.eof()) {
	std::getline(dump, msg);
	trim(msg);
	boost::split(tok, msg, is_any_of(", \t="), token_compress_on);
	if (tok.size() != 3)
	  continue;
	
	double integral = atof(tok[0].c_str());
	int a=atoi(tok[1].c_str()), b=atoi(tok[2].c_str());
	I1(2*(a-1), 2*(b-1)+1) += integral/2.;  //alpha beta
	I1(2*(a-1)+1, 2*(b-1)) += integral/2.;  //beta alpha
      }      
    }

    //Read SOC.Y
    {
      ifstream dump("SOC.Y");
      int N;
      dump >> N;
      if (N != norbs/2) {
	cout << "number of orbitals in SOC.Y should be equal to norbs in the input file."<<endl;
	cout << N <<" != "<<norbs<<endl;
	exit(0);
      }
      
      //I1soc[2].store.resize(N*(N+1)/2, 0.0);
      while(!dump.eof()) {
	std::getline(dump, msg);
	trim(msg);
	boost::split(tok, msg, is_any_of(", \t="), token_compress_on);
	if (tok.size() != 3)
	  continue;
	
	double integral = atof(tok[0].c_str());
	int a=atoi(tok[1].c_str()), b=atoi(tok[2].c_str());
	I1(2*(a-1), 2*(b-1)+1) += std::complex<double>(0, -integral/2.);  //alpha beta
	I1(2*(a-1)+1, 2*(b-1)) += std::complex<double>(0, integral/2.);  //beta alpha
      }      
    }


    //Read SOC.Z
    {
      ifstream dump("SOC.Z");
      int N;
      dump >> N;
      if (N != norbs/2) {
	cout << "number of orbitals in SOC.Z should be equal to norbs in the input file."<<endl;
	cout << N <<" != "<<norbs<<endl;
	exit(0);
      }
      
      //I1soc[3].store.resize(N*(N+1)/2, 0.0);
      while(!dump.eof()) {
	std::getline(dump, msg);
	trim(msg);
	boost::split(tok, msg, is_any_of(", \t="), token_compress_on);
	if (tok.size() != 3)
	  continue;
	
	double integral = atof(tok[0].c_str());
	int a=atoi(tok[1].c_str()), b=atoi(tok[2].c_str());
	I1(2*(a-1), 2*(b-1)) += integral/2; //alpha, alpha
	I1(2*(a-1)+1, 2*(b-1)+1) += -integral/2; //beta, beta
      }      
    }

  }

}
#endif

void readIntegrals(string fcidump, twoInt& I2, oneInt& I1, int& nelec, int& norbs, double& coreE,
		   std::vector<int>& irrep) {

  boost::mpi::communicator world;
  ifstream dump(fcidump.c_str());


  if (mpigetrank() == 0) {
    I2.ksym = false;
    bool startScaling = false;
    norbs = -1;
    nelec = -1;
    
    int index = 0;
    vector<string> tok;
    string msg;
    while(!dump.eof()) {
      std::getline(dump, msg);
      trim(msg);
      boost::split(tok, msg, is_any_of(", \t="), token_compress_on);
      
      if (startScaling == false && tok.size() == 1 && (boost::iequals(tok[0],"&END") || boost::iequals(tok[0], "/"))) {
	startScaling = true;
	index += 1;
	break;
      }
      else if(startScaling == false) {
	if (boost::iequals(tok[0].substr(0,4),"&FCI")) {
	  if (boost::iequals(tok[1].substr(0,4), "NORB"))
	    norbs = atoi(tok[2].c_str());
	  
	  if (boost::iequals(tok[3].substr(0,5), "NELEC"))
	    nelec = atoi(tok[4].c_str());
	}
	else if (boost::iequals(tok[0].substr(0,4),"ISYM"))
	  continue;
	else if (boost::iequals(tok[0].substr(0,4),"KSYM"))
	  I2.ksym = true;
	else if (boost::iequals(tok[0].substr(0,6),"ORBSYM")) {
	  for (int i=1;i<tok.size(); i++)
	    irrep.push_back(atoi(tok[i].c_str()));
	}
	else {
	  for (int i=0;i<tok.size(); i++)
	    irrep.push_back(atoi(tok[i].c_str()));
	}
	
	index += 1;
      }
    }
    
    if (norbs == -1 || nelec == -1) {
      std::cout << "could not read the norbs or nelec"<<std::endl;
      exit(0);
    }
    irrep.resize(norbs);
  }

#ifndef SERIAL
  mpi::broadcast(world, nelec, 0);
  mpi::broadcast(world, norbs, 0);
  mpi::broadcast(world, irrep, 0);
  mpi::broadcast(world, I2.ksym, 0);
#endif
  
  long npair = norbs*(norbs+1)/2;
  
  if (I2.ksym) {
    npair = norbs*norbs;
  }
  I2.norbs = norbs;
  
  size_t I2memory = npair*(npair+1)/2; //memory in bytes

  world.barrier();
  int2Segment.truncate((I2memory)*sizeof(double)); 
  regionInt2 = boost::interprocess::mapped_region{int2Segment, boost::interprocess::read_write};
  memset(regionInt2.get_address(), 0., (I2memory)*sizeof(double));
  world.barrier();
  I2.store = static_cast<double*>(regionInt2.get_address());

  if (mpigetrank() == 0) {
    I1.store.resize(2*norbs*(2*norbs),0.0); I1.norbs = 2*norbs;
    coreE = 0.0;

    vector<string> tok;
    string msg;
    while(!dump.eof()) {
      std::getline(dump, msg);
      trim(msg);
      boost::split(tok, msg, is_any_of(", \t"), token_compress_on);
      if (tok.size() != 5)
	continue;
      
      double integral = atof(tok[0].c_str());int a=atoi(tok[1].c_str()), b=atoi(tok[2].c_str()), 
					       c=atoi(tok[3].c_str()), d=atoi(tok[4].c_str());
      
      if(a==b&&b==c&&c==d&&d==0)
	coreE = integral;
      else if (b==c&&c==d&&d==0)
	continue;//orbital energy
      else if (c==d&&d==0) {
	I1(2*(a-1),2*(b-1)) = integral; //alpha,alpha
	I1(2*(a-1)+1,2*(b-1)+1) = integral; //beta,beta
	I1(2*(b-1),2*(a-1)) = integral; //alpha,alpha
	I1(2*(b-1)+1,2*(a-1)+1) = integral; //beta,beta
      }
      else
	I2(2*(a-1),2*(b-1),2*(c-1),2*(d-1)) = integral;
    }

    //exit(0);
    I2.maxEntry = *std::max_element(&I2.store[0], &I2.store[0]+I2memory,myfn);
    I2.Direct = MatrixXd::Zero(norbs, norbs); I2.Direct *= 0.;
    I2.Exchange = MatrixXd::Zero(norbs, norbs); I2.Exchange *= 0.;
    
    for (int i=0; i<norbs; i++)
      for (int j=0; j<norbs; j++) {
	I2.Direct(i,j) = I2(2*i,2*i,2*j,2*j);
	I2.Exchange(i,j) = I2(2*i,2*j,2*j,2*i);
      }
  }

#ifndef SERIAL
  mpi::broadcast(world, I1, 0);

  long intdim = I2memory;
  long  maxint = 26843540; //mpi cannot transfer more than these number of doubles
  long maxIter = intdim/maxint; 
  world.barrier();
  for (int i=0; i<maxIter; i++) {
    MPI::COMM_WORLD.Bcast(&I2.store[i*maxint], maxint, MPI_DOUBLE, 0);
    world.barrier();
  }
  MPI::COMM_WORLD.Bcast(&I2.store[(maxIter)*maxint], I2memory - maxIter*maxint, MPI_DOUBLE, 0);
  world.barrier();


  mpi::broadcast(world, I2.maxEntry, 0);
  mpi::broadcast(world, I2.Direct, 0);
  mpi::broadcast(world, I2.Exchange, 0);
  mpi::broadcast(world, I2.zero, 0);
  mpi::broadcast(world, coreE, 0);

#endif

  return;
    

}


void twoIntHeatBathSHM::constructClass(int norbs, twoIntHeatBath& I2) {

  boost::mpi::communicator world;
  size_t memRequired = 0;
  size_t nonZeroSameSpinIntegrals = 0;
  size_t nonZeroOppositeSpinIntegrals = 0;

  if (mpigetrank() == 0) {
    std::map<std::pair<int,int>, std::multimap<double, std::pair<int,int>, compAbs > >::iterator it1 = I2.sameSpin.begin();
    
    for (;it1!= I2.sameSpin.end(); it1++) {
      nonZeroSameSpinIntegrals += it1->second.size();
    }
    
    std::map<std::pair<int,int>, std::multimap<double, std::pair<int,int>, compAbs > >::iterator it2 = I2.oppositeSpin.begin();
    
    for (;it2!= I2.oppositeSpin.end(); it2++) {
      nonZeroOppositeSpinIntegrals += it2->second.size();
    }
    
    
    //total Memory required
    memRequired += nonZeroSameSpinIntegrals*(sizeof(double)+2*sizeof(int))+ ( (norbs*(norbs+1)/2+1)*sizeof(size_t));
    memRequired += nonZeroOppositeSpinIntegrals*(sizeof(double)+2*sizeof(int))+ ( (norbs*(norbs+1)/2+1)*sizeof(size_t));
  }

  mpi::broadcast(world, memRequired, 0);
  mpi::broadcast(world, nonZeroSameSpinIntegrals, 0);
  mpi::broadcast(world, nonZeroOppositeSpinIntegrals, 0);

  world.barrier();
  int2SHMSegment.truncate(memRequired); 
  regionInt2SHM = boost::interprocess::mapped_region{int2SHMSegment, boost::interprocess::read_write};
  memset(regionInt2SHM.get_address(), 0., memRequired);
  world.barrier();

  sameSpinIntegrals = static_cast<double*>(regionInt2SHM.get_address());
  startingIndicesSameSpin = static_cast<size_t*>(regionInt2SHM.get_address() + nonZeroSameSpinIntegrals*sizeof(double));
  sameSpinPairs = static_cast<int*>(regionInt2SHM.get_address() + nonZeroSameSpinIntegrals*sizeof(double) + (norbs*(norbs+1)/2+1)*sizeof(size_t));
  
  oppositeSpinIntegrals = static_cast<double*>(regionInt2SHM.get_address() + nonZeroSameSpinIntegrals*(sizeof(double)+2*sizeof(int)) +  (norbs*(norbs+1)/2+1)*sizeof(size_t));
  startingIndicesOppositeSpin = static_cast<size_t*>(regionInt2SHM.get_address() + nonZeroOppositeSpinIntegrals*sizeof(double) + nonZeroSameSpinIntegrals*(sizeof(double)+2*sizeof(int)) +  (norbs*(norbs+1)/2+1)*sizeof(size_t));
  oppositeSpinPairs = static_cast<int*>(regionInt2SHM.get_address() + nonZeroOppositeSpinIntegrals*sizeof(double) + (norbs*(norbs+1)/2+1)*sizeof(size_t) + nonZeroSameSpinIntegrals*(sizeof(double)+2*sizeof(int)) +  (norbs*(norbs+1)/2+1)*sizeof(size_t));
  
  if (mpigetrank() == 0) {
    
    startingIndicesSameSpin[0] = 0;
    size_t index = 0, pairIter = 1;
    for (int i=0; i<norbs; i++) {
      for (int j=0; j<=i; j++) {
	std::map<std::pair<int,int>, std::multimap<double, std::pair<int,int>, compAbs > >::iterator it1 = I2.sameSpin.find( std::pair<int,int>(i,j));

	if (it1 != I2.sameSpin.end()) {	  
	  for (std::multimap<double, std::pair<int,int>,compAbs >::reverse_iterator it=it1->second.rbegin(); it!=it1->second.rend(); it++) {
	    sameSpinIntegrals[index] = it->first;
	    sameSpinPairs[2*index] = it->second.first;
	    sameSpinPairs[2*index+1] = it->second.second;
	    index++;
	  }
	}
	startingIndicesSameSpin[pairIter] = index;
	pairIter++;
      }
    }
    I2.sameSpin.clear();


    startingIndicesOppositeSpin[0] = 0;
    index = 0; pairIter = 1;
    for (int i=0; i<norbs; i++) {
      for (int j=0; j<=i; j++) {
	std::map<std::pair<int,int>, std::multimap<double, std::pair<int,int>, compAbs > >::iterator it1 = I2.oppositeSpin.find( std::pair<int,int>(i,j));

	if (it1 != I2.oppositeSpin.end()) {	  
	  for (std::multimap<double, std::pair<int,int>,compAbs >::reverse_iterator it=it1->second.rbegin(); it!=it1->second.rend(); it++) {
	    oppositeSpinIntegrals[index] = it->first;
	    oppositeSpinPairs[2*index] = it->second.first;
	    oppositeSpinPairs[2*index+1] = it->second.second;
	    index++;
	  }
	}
	startingIndicesOppositeSpin[pairIter] = index;
	pairIter++;
      }
    }
    I2.oppositeSpin.clear();
  }
  

  long intdim = memRequired;
  long  maxint = 26843540; //mpi cannot transfer more than these number of doubles
  long maxIter = intdim/maxint; 
  world.barrier();
  char* shrdMem = static_cast<char*>(regionInt2SHM.get_address());
  for (int i=0; i<maxIter; i++) {
    MPI::COMM_WORLD.Bcast(shrdMem+i*maxint, maxint, MPI_CHAR, 0);
    world.barrier();
  }
  MPI::COMM_WORLD.Bcast(shrdMem+(maxIter)*maxint, memRequired - maxIter*maxint, MPI_CHAR, 0);
  world.barrier();

}
