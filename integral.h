#ifndef INTEGRAL_HEADER_H
#define INTEGRAL_HEADER_H
#include <vector>
#include <string>
#include <iostream>
#include <Eigen/Dense>
#include <map>
#include <utility>
#include "iowrapper.h"
#include <boost/interprocess/managed_shared_memory.hpp>


using namespace std;
using namespace Eigen;
bool myfn(double i, double j);

class compAbs {
 public:
  bool operator()(const double& a, const double& b) const {
    return abs(a) < abs(b);
  }
  bool operator()(const complex<double>& a, const complex<double>& b) const {
    return std::abs(a) < std::abs(b);
  }
};
class oneInt {
 private:
  friend class boost::serialization::access;
  template<class Archive>
    void serialize(Archive & ar, const unsigned int version)
    {
      ar & store & norbs;
    }
 public:
  std::vector<CItype> store;
  int norbs;
  //I explicitly store all elements of the matrix
  //so for normal operator if i and j dont have the same spin
  //then it will just return zero. If we have SOC and
  // i and j have different spin then it can be a complex number.
  inline CItype& operator()(int i, int j) {
    //int A = max(i,j), B = min(i,j);
    return store.at(i*norbs+j);
  }
};

  
class twoInt {
 private:
  friend class boost::serialization::access;
  template<class Archive>
    void serialize(Archive & ar, const unsigned int version)
    {
      ar & maxEntry \
	& Direct \
	& Exchange \
	& zero     \
	& norbs   \
	& ksym;
    }
 public:
  double* store;
  double maxEntry;
  MatrixXd Direct, Exchange;
  double zero ;
  size_t norbs;
  bool ksym;
 twoInt() :zero(0.0),maxEntry(100.) {}
  inline double& operator()(int i, int j, int k, int l) {
    zero = 0.0;
    if (!((i%2 == j%2) && (k%2 == l%2))) {
      return zero;
    }
    int I=i/2;int J=j/2;int K=k/2;int L=l/2;

    if(!ksym) {
      int IJ = max(I,J)*(max(I,J)+1)/2 + min(I,J);
      int KL = max(K,L)*(max(K,L)+1)/2 + min(K,L);
      int A = max(IJ,KL), B = min(IJ,KL);
      return store[A*(A+1)/2+B];
    }
    else {
      int IJ = I*norbs+J, KL = K*norbs+L;
      int A = max(IJ,KL), B = min(IJ,KL);
      return store[A*(A+1)/2+B];
    }
  }
  
};



class twoIntHeatBath {
 public:
  //i,j,a,b are spatial orbitals
  //first pair is i,j (i>j)
  //the map contains a list of integral which are equal to (ia|jb) where a,b are the second pair
  //if the integrals are for the same spin you have to separately store (ia|jb) and (ib|ja)
  //for opposite spin you just have to store for a>b because the integral is (ia|jb) - (ib|ja)
  //now this class is made by just considering integrals that are smaller than threshhold
  std::map<std::pair<int,int>, std::multimap<double, std::pair<int,int>, compAbs > > sameSpin;  
  std::map<std::pair<int,int>, std::multimap<double, std::pair<int,int>, compAbs > > oppositeSpin;  

  double epsilon;
  double zero ;
 twoIntHeatBath(double epsilon_) :zero(0.0),epsilon(abs(epsilon_)) {}

  //the orbs contain all orbitals used to make the ij pair above
  //typically these can be all orbitals of the problem or just the active space ones
  //ab will typically contain all orbitals(norbs)
  void constructClass(std::vector<int>& orbs, twoInt& I2, int norbs) {
    for (int i=0; i<orbs.size(); i++)
      for (int j=0;j<=i;j++) {
	std::pair<int,int> IJ=make_pair(i,j);
	//sameSpin[IJ]=std::map<double, std::pair<int,int> >();
	//oppositeSpin[IJ]=std::map<double, std::pair<int,int> >();
	for (int a=0; a<norbs; a++) {
	  for (int b=0; b<norbs; b++) {
	    //opposite spin
	    if (fabs(I2(2*i, 2*a, 2*j, 2*b)) > epsilon)
	      oppositeSpin[IJ].insert(pair<double, std::pair<int,int> >(I2(2*i, 2*a, 2*j, 2*b), make_pair(a,b)));
	    //samespin
	    if (a>=b && fabs(I2(2*i,2*a,2*j,2*b) - I2(2*i,2*b,2*j,2*a)) > epsilon) {
	      sameSpin[IJ].insert(pair<double, std::pair<int,int> >( I2(2*i,2*a,2*j,2*b) - I2(2*i,2*b,2*j,2*a), make_pair(a,b)));
				  //sameSpin[IJ][fabs(I2(2*i,2*a,2*j,2*b) - I2(2*i,2*b,2*j,2*a))] = make_pair<int,int>(a,b);
	    }
	  }
	}
      }
  }
  
  
};

class twoIntHeatBathSHM {
 public:
  double* sameSpinIntegrals;
  double* oppositeSpinIntegrals;
  size_t* startingIndicesSameSpin;
  size_t* startingIndicesOppositeSpin;
  int* sameSpinPairs;
  int* oppositeSpinPairs;

  double epsilon;
 twoIntHeatBathSHM(double epsilon_) : epsilon(abs(epsilon_)) {}

  void constructClass(int norbs, twoIntHeatBath& I2) ;
};


#ifdef Complex
void readSOCIntegrals(oneInt& I1soc, int norbs);  
#endif

void readIntegrals(string fcidump, twoInt& I2, oneInt& I1, int& nelec, int& norbs, double& coreE,
		   std::vector<int>& irrep);

#endif
