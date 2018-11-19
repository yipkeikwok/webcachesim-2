#include <unordered_map>
#include <limits>
#include <random>
#include <cmath>
#include <cassert>
#include <random>
#include <cmath>
#include <cassert>
#include "lru_variants.h"
#include "../random_helper.h"

// golden section search helpers
#define SHFT2(a,b,c) (a)=(b);(b)=(c);
#define SHFT3(a,b,c,d) (a)=(b);(b)=(c);(c)=(d);

// math model below can be directly copiedx
// static inline double oP1(double T, double l, double p) {
static inline double oP1(double T, double l, double p) {
  return (l * p * T * (840.0 + 60.0 * l * T + 20.0 * l*l * T*T + l*l*l * T*T*T));
}

static inline double oP2(double T, double l, double p) {
  return (840.0 + 120.0 * l * (-3.0 + 7.0 * p) * T + 60.0 * l*l * (1.0 + p) * T*T + 4.0 * l*l*l * (-1.0 + 5.0 * p) * T*T*T + l*l*l*l * p * T*T*T*T);
}

/*
  LRU: Least Recently Used eviction
*/
bool LRUCache::lookup(SimpleRequest* req)
{
	// CacheObject: defined in cache_object.h 
    CacheObject obj(req);
	// _cacheMap defined in class LRUCache in lru_variants.h 
    auto it = _cacheMap.find(obj);
    if (it != _cacheMap.end()) {
        // log hit
        LOG("h", 0, obj.id, obj.size);
        hit(it, obj.size);
        return true;
    }
    return false;
}

void LRUCache::admit(SimpleRequest* req)
{
    const uint64_t size = req->getSize();
    // object feasible to store?
    if (size > _cacheSize) {
        LOG("L", _cacheSize, req->getId(), size);
        return;
    }
    // check eviction needed
    while (_currentSize + size > _cacheSize) {
        evict();
    }
    // admit new object
    CacheObject obj(req);
    _cacheList.push_front(obj);
    _cacheMap[obj] = _cacheList.begin();
    _currentSize += size;
    LOG("a", _currentSize, obj.id, obj.size);
}

void LRUCache::evict(SimpleRequest* req)
{
    CacheObject obj(req);
    auto it = _cacheMap.find(obj);
    if (it != _cacheMap.end()) {
        ListIteratorType lit = it->second;
        LOG("e", _currentSize, obj.id, obj.size);
        _currentSize -= obj.size;
        _cacheMap.erase(obj);
        _cacheList.erase(lit);
    }
}

SimpleRequest* LRUCache::evict_return()
{
    // evict least popular (i.e. last element)
    if (_cacheList.size() > 0) {
        ListIteratorType lit = _cacheList.end();
        lit--;
        CacheObject obj = *lit;
        LOG("e", _currentSize, obj.id, obj.size);
        SimpleRequest* req = new SimpleRequest(obj.id, obj.size);
        _currentSize -= obj.size;
        _cacheMap.erase(obj);
        _cacheList.erase(lit);
        return req;
    }
    return NULL;
}

void LRUCache::evict()
{
    evict_return();
}

// const_iterator: a forward iterator to const value_type, where 
// value_type is pair<const key_type, mapped_type>
void LRUCache::hit(lruCacheMapType::const_iterator it, uint64_t size)
{
	// transfers it->second (i.e., ObjInfo) from _cacheList into 
	// 	*this. The transferred it->second is to be inserted before 
	// 	the element pointed to by _cacheList.begin()
	//
	// _cacheList is defined in class LRUCache in lru_variants.h 
    _cacheList.splice(_cacheList.begin(), _cacheList, it->second);
}

/*
  FIFO: First-In First-Out eviction
*/
void FIFOCache::hit(lruCacheMapType::const_iterator it, uint64_t size)
{
}

/*
  FilterCache (admit only after N requests)
*/
FilterCache::FilterCache()
    : LRUCache(),
      _nParam(2)
{
}

void FilterCache::setPar(std::string parName, std::string parValue) {
    if(parName.compare("n") == 0) {
        const uint64_t n = std::stoull(parValue);
        assert(n>0);
        _nParam = n;
    } else {
        std::cerr << "unrecognized parameter: " << parName << std::endl;
    }
}


bool FilterCache::lookup(SimpleRequest* req)
{
    CacheObject obj(req);
    _filter[obj]++;
    return LRUCache::lookup(req);
}

void FilterCache::admit(SimpleRequest* req)
{
    CacheObject obj(req);
    if (_filter[obj] <= _nParam) {
        return;
    }
    LRUCache::admit(req);
}


/*
  ThLRU: LRU eviction with a size admission threshold
*/
ThLRUCache::ThLRUCache()
    : LRUCache(),
      _sizeThreshold(524288)
{
}

void ThLRUCache::setPar(std::string parName, std::string parValue) {
    if(parName.compare("t") == 0) {
        const double t = stof(parValue);
        assert(t>0);
        _sizeThreshold = pow(2.0,t);
    } else {
        std::cerr << "unrecognized parameter: " << parName << std::endl;
    }
}


void ThLRUCache::admit(SimpleRequest* req)
{
    const uint64_t size = req->getSize();
    // admit if size < threshold
    if (size < _sizeThreshold) {
        LRUCache::admit(req);
    }
}


/*
  ExpLRU: LRU eviction with size-aware probabilistic cache admission
*/
ExpLRUCache::ExpLRUCache()
    : LRUCache(),
      _cParam(262144)
{
}

void ExpLRUCache::setPar(std::string parName, std::string parValue) {
    if(parName.compare("c") == 0) {
        const double c = stof(parValue);
        assert(c>0);
        _cParam = pow(2.0,c);
    } else {
        std::cerr << "unrecognized parameter: " << parName << std::endl;
    }
}



void ExpLRUCache::admit(SimpleRequest* req)
{
    const double size = req->getSize();
    // admit to cache with probablity that is exponentially decreasing with size
    double admissionProb = exp(-size/ _cParam);
    std::bernoulli_distribution distribution(admissionProb);
    if (distribution(globalGenerator)) {
        LRUCache::admit(req);
    }
}


AdaptSizeCache::AdaptSizeCache()
	: LRUCache()
	, nextReconfiguration(RECONFIGURATION_INTERVAL)
	, c(1 << 15)
	, statSize(0)
{
	/** 
	randGenerator = new std::mt19937_64(SEED); 
	uniform_int_distribution0 = 
		new std::uniform_int_distribution<unsigned long long>(0, 
		std::numeric_limits<unsigned long long>::max()); 
	*/ 
	v=1.0-r;
}

bool AdaptSizeCache::lookup(SimpleRequest* req)
{
	reconfigure(); 

	CacheObject tmpCacheObject0(req); 
	if(intervalInfo.count(tmpCacheObject0)==0 
		&& ewmaInfo.count(tmpCacheObject0)==0) { 
		// new object 
		statSize += tmpCacheObject0.size;
	}
	// the else block is not necessary as webcachesim treats an object 
	// with size changed as a new object 
	/** 
	} else {
		// keep track of changing object sizes
		if(intervalInfo.count(id)>0 
			&& intervalInfo[id].size != req.size()) {
			// outdated size info in intervalInfo
			statSize -= intervalInfo[id].size;
			statSize += req.size();
		}
		if(ewmaInfo.count(id)>0 && ewmaInfo[id].size != req.size()) {
			// outdated size info in ewma
			statSize -= ewmaInfo[id].size;
			statSize += req.size();
		}
	}
	*/

	// record stats
	auto& info = intervalInfo[tmpCacheObject0]; 
	info.requestCount += 1;
	info.objSize = tmpCacheObject0.size;

	return LRUCache::lookup(req);
}

void AdaptSizeCache::admit(SimpleRequest* req)
{
	double roll = uniform_real_distribution0(randGenerator0);
	double admitProb = std::exp(-1.0 * double(req->getSize())/c); 

	if(roll < admitProb) 
		LRUCache::admit(req); 

}

void AdaptSizeCache::reconfigure() {
	--nextReconfiguration;
	if (nextReconfiguration > 0) {
		return;
	} else if(statSize <= getSize()*3) {
		// not enough data has been gathered
		nextReconfiguration+=10000;
		return; 
	} else {
		nextReconfiguration = RECONFIGURATION_INTERVAL;
	}

	// smooth stats for objects 
	for(auto it = ewmaInfo.begin(); 
		it != ewmaInfo.end(); 
		it++) {
		it->second.requestCount *= EWMA_DECAY; 
	} 

	// persist intervalinfo in ewmaInfo 
	for (auto it = intervalInfo.begin(); 
		it != intervalInfo.end();
		it++) {
		auto ewmaIt = ewmaInfo.find(it->first); 
		if(ewmaIt != ewmaInfo.end()) {
			ewmaIt->second.requestCount += (1. - EWMA_DECAY) 
				* it->second.requestCount;
			ewmaIt->second.objSize = it->second.objSize; 
		} else {
			ewmaInfo.insert(*it);
		}
	}
	intervalInfo.clear(); 

	// copy stats into vector for better alignment 
	// and delete small values 
	alignedReqCount.clear(); 
	alignedObjSize.clear();
	double totalReqCount = 0.0; 
	uint64_t totalObjSize = 0.0; 
	for(auto it = ewmaInfo.begin(); 
		it != ewmaInfo.end(); 
		/*none*/) {
		if(it->second.requestCount < 0.1) {
			// delete from stats 
			statSize -= it->second.objSize; 
			it = ewmaInfo.erase(it); 
		} else {
			alignedReqCount.push_back(it->second.requestCount); 
			totalReqCount += it->second.requestCount; 
			alignedObjSize.push_back(it->second.objSize); 
			totalObjSize += it->second.objSize; 
			++it;
		}
	}

	std::cerr << "Reconfiguring over " << ewmaInfo.size() 
	<< " objects - log2 total size " << std::log2(totalObjSize) 
	<< " log2 statsize " << std::log2(statSize) << std::endl; 

	// assert(totalObjSize==statSize); 
	//
	// if(totalObjSize > cacheSize*2) {
	//
	// model hit rate and choose best admission parameter, c
	// search for best parameter on log2 scale of c, between min=x0 and max=x3
	// x1 and x2 bracket our current estimate of the optimal parameter range
	// |x0 -- x1 -- x2 -- x3|
	double x0 = 0; 
	double x1 = std::log2(getSize());
	double x2 = x1;
	double x3 = x1; 

	double bestHitRate = 0.0; 
	// course_granular grid search 
	for(int i=2; i<x3; i+=4) {
		const double next_log2c = i; // 1.0 * (i+1) / NUM_PARAMETER_POINTS;
		const double hitRate = modelHitRate(next_log2c); 
		// printf("Model param (%f) : ohr (%f)\n",
		// 	next_log2c,hitRate/totalReqRate);

		if(hitRate > bestHitRate) {
			bestHitRate = hitRate;
			x1 = next_log2c;
		}
	}

	double h1 = bestHitRate; 
	double h2;
	//prepare golden section search into larger segment 
	if(x3-x1 > x1-x0) {
		// above x1 is larger segment 
		x2 = x1+v*(x3-x1); 
		h2 = modelHitRate(x2);
	} else {
		// below x1 is larger segment 
		x2 = x1; 
		h2 = h1; 
		x1 = x0+v*(x1-x0); 
		h1 = modelHitRate(x1); 
	}
	assert(x1<x2); 

	int curIterations=0; 
	// use termination condition from [Numerical recipes in C]
	while(curIterations++<maxIterations 
		&& fabs(x3-x0)>tol*(fabs(x1)+fabs(x2))) {
		//NAN check 
		if((h1!=h1) || (h2!=h2)) 
			break; 
		// printf("Model param low (%f) : ohr low (%f) | param high (%f) 
		// 	: ohr high (    %f)\n",x1,h1/totalReqRate,x2,
		// 	h2/totalReqRate);

		if(h2>h1) {
			SHFT3(x0,x1,x2,r*x1+v*x3); 
			SHFT2(h1,h2,modelHitRate(x2));
		} else {
			SHFT3(x3,x2,x1,r*x2+v*x0);
			SHFT2(h2,h1,modelHitRate(x1));
		}
	}

	// check result
	if( (h1!=h1) || (h2!=h2) ) {
		// numerical failure
		std::cerr << "ERROR: numerical bug " << h1 << " " << h2 
			<< std::endl;
		// nop
	} else if (h1 > h2) {
		// x1 should is final parameter
		c = pow(2, x1);
		std::cerr << "Choosing c of " << c << " (log2: " << x1 << ")" 
			<< std::endl;
	} else {
		c = pow(2, x2);
		std::cerr << "Choosing c of " << c << " (log2: " << x2 << ")" 
		<< std::endl;
	}
}

double AdaptSizeCache::modelHitRate(double log2c) {
  // this code is adapted from the AdaptSize git repo
  // github.com/dasebe/AdaptSize
  double old_T, the_T, the_C;
  double sum_val = 0.;
  double thparam = log2c;

  for(size_t i=0; i<alignedReqCount.size(); i++) {
    sum_val += alignedReqCount[i] * (exp(-alignedObjSize[i]/ pow(2,thparam))) * alignedObjSize[i];
  }
  if(sum_val <= 0) {
    return(0);
  }
  the_T = getSize() / sum_val;
  // prepare admission probabilities
  alignedAdmProb.clear();
  for(size_t i=0; i<alignedReqCount.size(); i++) {
      alignedAdmProb.push_back(exp(-alignedObjSize[i]/ pow(2.0,thparam)));
  }
  // 20 iterations to calculate TTL
  
  for(int j = 0; j<10; j++) {
    the_C = 0;
    if(the_T > 1e70) {
      break;
    }
    for(size_t i=0; i<alignedReqCount.size(); i++) {
      const double reqTProd = alignedReqCount[i]*the_T;
      if(reqTProd>150) {
          // cache hit probability = 1, but numerically inaccurate to calculate
          the_C += alignedObjSize[i];
      } else {
          const double expTerm = exp(reqTProd) - 1;
          const double expAdmProd = alignedAdmProb[i] * expTerm;
          const double tmp = expAdmProd / (1 + expAdmProd);
          the_C += alignedObjSize[i] * tmp;
      }
    }
    old_T = the_T;
    the_T = getSize() * old_T/the_C;
  }

  // calculate object hit ratio
  double weighted_hitratio_sum = 0;
  for(size_t i=0; i<alignedReqCount.size(); i++) {
      const double tmp01= oP1(the_T,alignedReqCount[i],alignedAdmProb[i]);
      const double tmp02= oP2(the_T,alignedReqCount[i],alignedAdmProb[i]);
      double tmp;
      if(tmp01!=0 && tmp02==0)
          tmp = 0.0;
      else tmp= tmp01/tmp02;
      if(tmp<0.0)
          tmp = 0.0;
      else if (tmp>1.0)
          tmp = 1.0;
      weighted_hitratio_sum += alignedReqCount[i] * tmp;
  }
  return (weighted_hitratio_sum);
}

/*
  S4LRU
*/

void S4LRUCache::setSize(uint64_t cs) {
    uint64_t total = cs;
    for(int i=0; i<4; i++) {
        segments[i].setSize(cs/4);
        total -= cs/4;
        std::cerr << "setsize " << i << " : " << cs/4 << "\n";
    }
    if(total>0) {
        segments[0].setSize(cs/4+total);
        std::cerr << "bonus setsize " << 0 << " : " << cs/4 + total << "\n";
    }
}

bool S4LRUCache::lookup(SimpleRequest* req)
{
    for(int i=0; i<4; i++) {
        if(segments[i].lookup(req)) {
            // hit
            if(i<3) {
                // move up
                segments[i].evict(req);
                segment_admit(i+1,req);
            }
            return true;
        }
    }
    return false;
}

void S4LRUCache::admit(SimpleRequest* req)
{
    segments[0].admit(req);
}

void S4LRUCache::segment_admit(uint8_t idx, SimpleRequest* req)
{
    if(idx==0) {
        segments[idx].admit(req);
    } else {
        while(segments[idx].getCurrentSize() + req->getSize()
              > segments[idx].getSize()) {
            // need to evict from this partition first
            // find least popular item in this segment
            auto nreq = segments[idx].evict_return();
            segment_admit(idx-1,nreq);
        }
        segments[idx].admit(req);
    }
}

void S4LRUCache::evict(SimpleRequest* req)
{
    for(int i=0; i<4; i++) {
        segments[i].evict(req);
    }
}

void S4LRUCache::evict()
{
    segments[0].evict();
}


