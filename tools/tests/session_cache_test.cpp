#include "core/release_cache_storage.hpp"
#include <cassert>
#include <cstddef>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cstdio>
struct Counts { size_t live=0, peak=0; };
template<class T> struct CountingAllocator {
    using value_type=T; Counts* counts;
    explicit CountingAllocator(Counts* p):counts(p){}
    template<class U> CountingAllocator(const CountingAllocator<U>& a):counts(a.counts){}
    T* allocate(size_t n) { T* p=std::allocator<T>{}.allocate(n);counts->live+=n*sizeof(T);if(counts->live>counts->peak)counts->peak=counts->live;return p; }
    void deallocate(T* p,size_t n) { counts->live-=n*sizeof(T);std::allocator<T>{}.deallocate(p,n); }
    template<class U> bool operator==(const CountingAllocator<U>& a)const{return counts==a.counts;}
    template<class U> bool operator!=(const CountingAllocator<U>& a)const{return !(*this==a);}
};
int main(){
    Counts mapCounts,vectorCounts,setCounts;
    using Pair=std::pair<const unsigned,unsigned>;
    std::unordered_map<unsigned,unsigned,std::hash<unsigned>,std::equal_to<unsigned>,CountingAllocator<Pair>> map{CountingAllocator<Pair>(&mapCounts)};
    std::vector<unsigned,CountingAllocator<unsigned>> vector{CountingAllocator<unsigned>(&vectorCounts)};
    std::unordered_set<unsigned,std::hash<unsigned>,std::equal_to<unsigned>,CountingAllocator<unsigned>> set{CountingAllocator<unsigned>(&setCounts)};
    size_t clearRetained=0;
    for(unsigned session=0;session<8;++session){
        for(unsigned i=0;i<12000+session*500;++i){map.emplace(i,i+session);vector.push_back(i);set.insert(i);}
        map.clear();vector.clear();set.clear();
        clearRetained=mapCounts.live+vectorCounts.live+setCounts.live;
        assert(clearRetained>0); // Real old behavior retains backing allocations.
        wowee::core::releaseCacheStorage(map);wowee::core::releaseCacheStorage(vector);wowee::core::releaseCacheStorage(set);
        assert(mapCounts.live==0&&vectorCounts.live==0&&setCounts.live==0);
        assert(map.empty()&&vector.empty()&&set.empty());
        assert(map.get_allocator().counts==&mapCounts);
        assert(vector.get_allocator().counts==&vectorCounts);
        assert(set.get_allocator().counts==&setCounts);
        map[7]=42;vector.push_back(42);set.insert(42);
        assert(map.at(7)==42&&vector.front()==42&&set.count(42));
    }
    std::printf("PASS 8 session cycles, actual tracked retained bytes after clear=%zu; release returns every owned allocation; reload and stateful allocator ownership preserved\n",clearRetained);
}
