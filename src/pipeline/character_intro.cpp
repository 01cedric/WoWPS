#include "pipeline/character_intro.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
namespace wowee::pipeline {
namespace {
constexpr size_t kMaxTableBytes = 8u * 1024u * 1024u;
constexpr size_t kMaxModelBytes = 4u * 1024u * 1024u;
constexpr size_t kMaxDecodedBytes = 4u * 1024u * 1024u;

constexpr uint32_t kMaxKeys = 8192;
constexpr uint32_t kMaxDurationMs = 10u * 60u * 1000u;
// A std::string rather than a const char*, so a failure can name the file it
// could not read. Every reason used to be a literal, which made "intro asset is
// missing" identical whether the missing thing was a DBC or one camera model -
// and that is the difference between "this install has no intro data" and "this
// one race's camera was not extracted".
struct Invalid { std::string reason; };
struct Bytes {
    const std::vector<uint8_t>& data;
    bool has(size_t at, size_t count) const { return at <= data.size() && count <= data.size()-at; }
    template<class T> T get(size_t at) const {
        if (!has(at,sizeof(T))) throw Invalid{"asset offset is outside file"};
        T v; std::memcpy(&v,data.data()+at,sizeof(v)); return v;
    }
    uint32_t u32(size_t at) const { return get<uint32_t>(at); }
    float number(size_t at) const {
        const float v=get<float>(at);
        if (!std::isfinite(v)) throw Invalid{"camera contains nonfinite numbers"};
        return v;
    }
    glm::vec3 vec(size_t at) const { return {number(at),number(at+4),number(at+8)}; }
};
std::string normalized(std::string path) {
    if (path.empty() || path.size()>512) throw Invalid{"missing or oversized asset path"};
    for (char& c:path) { if(c=='\\')c='/'; else if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a'); }
    if (path.front()=='/' || path.find(':')!=std::string::npos) throw Invalid{"absolute asset path rejected"};
    size_t p=0;
    while(p<path.size()) { const auto end=path.find('/',p);const auto part=path.substr(p,end-p);
        if(part==".." || part==".")throw Invalid{"relative asset traversal rejected"};
        if(end==std::string::npos)break;p=end+1;
    }
    return path;
}
std::vector<uint8_t> readBounded(const IntroReadFile& read,const std::string& path,size_t limit) {
    auto bytes=read(path,limit);
    if(bytes.empty())throw Invalid{"intro asset is missing: "+path};
    if(bytes.size()>limit)throw Invalid{"intro asset exceeds read budget"};
    return bytes;
}
class Dbc {
public:
    Dbc(const IntroReadFile& read,const char* name,uint32_t fields)
        :data_(readBounded(read,std::string("DBFilesClient/")+name+".dbc",kMaxTableBytes)),view_{data_},fields_(fields) {
        if(!view_.has(0,20) || std::memcmp(data_.data(),"WDBC",4)!=0) throw Invalid{"intro requires WDBC tables"};
        rows_=view_.u32(4);const uint32_t strings=view_.u32(16);
        if(view_.u32(8)!=fields || view_.u32(12)!=fields*4u)throw Invalid{"intro DBC schema differs from build 12340"};
        const uint64_t end=20ull+uint64_t(rows_)*fields*4u;
        if(end>data_.size() || strings>data_.size()-end)throw Invalid{"intro DBC is truncated"};
        stringsAt_=static_cast<size_t>(end);stringsSize_=strings;
    }
    uint32_t row(uint32_t id) const {
        for(uint32_t i=0;i<rows_;++i)if(u(i,0)==id)return i;
        throw Invalid{"intro DBC reference does not exist"};
    }
    uint32_t u(uint32_t row,uint32_t field) const {return view_.u32(offset(row,field));}
    float f(uint32_t row,uint32_t field) const {return view_.number(offset(row,field));}
    std::string s(uint32_t row,uint32_t field) const {
        const uint32_t off=u(row,field);
        if(off>=stringsSize_)throw Invalid{"intro DBC string offset is invalid"};
        const char* begin=reinterpret_cast<const char*>(data_.data()+stringsAt_+off);
        const size_t available=stringsSize_-off;
        const auto* end=static_cast<const char*>(std::memchr(begin,0,available));
        if(!end || end-begin>512)throw Invalid{"intro DBC string is invalid"};
        return {begin,end};
    }
private:
    size_t offset(uint32_t row,uint32_t field)const{
        if(row>=rows_ || field>=fields_)throw Invalid{"intro DBC field is invalid"};
        return 20+size_t(row)*fields_*4+field*4;
    }
    std::vector<uint8_t> data_;Bytes view_;uint32_t fields_,rows_=0;size_t stringsAt_=0,stringsSize_=0;
};
// ---------------------------------------------------------------------------
// Rebuilding a camera track's spline handles from its own keys.
//
// B34 stopped the flyover overshooting by ignoring the serialized handles for
// the position and target curves and rebuilding the motion from limited C1
// slopes instead (rendering/cinematic_camera.hpp). Its own note says what it
// left alone: linear and constant tracks, and the roll evaluation. Roll is
// still read straight out of the file's handles by the ordinary Hermite
// evaluator, and a handle pair that overshoots carries the value past its own
// key and back again - which on the roll channel is the camera rocking.
//
// Nothing about that is per race, and that is exactly why it looks per race.
// Whether a flight rocks depends only on what its own Cameras\*.m2 happens to
// carry, and sequence 81 - the human one, one shot of 87,467 ms, the only
// sequence this client has ever been run against on hardware - is the shape
// the B34 fix was accepted on. Every other race gets whatever is in its file.
//
// So the handles are rebuilt here instead, once, at load: for every cubic or
// bezier track in the camera, on every channel including roll. Keys, key times,
// the interpolation kind and the authored duration are untouched - no camera
// data is invented or replaced - and the derivatives written in are the ones
// the stabilized curve already implies, so position and target keep the motion
// they have today and roll finally joins them. The two evaluators, one that
// reads handles and one that reconstructs slopes, then describe one curve.
//
// The slope rules are Fritsch-Carlson, the same pair cinematic_camera.hpp uses:
// a harmonic mean of the neighbouring secants, zero where they disagree in
// sign, and a bounded three-point estimate at the ends. Under them the cubic
// cannot leave the interval between its two key values, which is the property
// the overshoot was breaking.
double limitedSlope(double a,double b,double ha,double hb){
    if(a==0 || b==0 || std::signbit(a)!=std::signbit(b))return 0;
    const double w1=2*hb+ha,w2=hb+2*ha;
    return (w1+w2)/(w1/a+w2/b);
}
double endpointSlope(double d0,double d1,double h0,double h1){
    double m=((2*h0+h1)*d0-h0*d1)/(h0+h1);
    if(m==0 || d0==0 || std::signbit(m)!=std::signbit(d0))return 0;
    if(std::signbit(d0)!=std::signbit(d1) && std::abs(m)>3*std::abs(d0))m=3*d0;
    return m;
}
// One channel of a value, so a vec3 track and a float track share the rule.
float channel(const glm::vec3& v,int c){return v[c];}
float channel(float v,int){return v;}
void setChannel(glm::vec3& v,int c,float x){v[c]=x;}
void setChannel(float& v,int,float x){v=x;}
constexpr int channels(const glm::vec3&){return 3;}
constexpr int channels(float){return 1;}
template<class Value>
void stabilizeHandles(uint16_t kind,const std::vector<uint32_t>& times,
                      const std::vector<Value>& values,
                      std::vector<Value>& firstHandles,std::vector<Value>& secondHandles){
    // Only the cubic kinds have handles at all; kind 0 holds its key and kind 1
    // walks straight between two of them, and both are already free of this.
    if(kind!=2 && kind!=3)return;
    const size_t count=std::min(times.size(),values.size());
    if(count<2 || firstHandles.size()<count || secondHandles.size()<count)return;
    const int width=channels(values.front());
    for(size_t i=0;i<count;++i){
        // The interval each handle belongs to. firstHandles[i] is only ever
        // read as the start of the segment leaving key i, secondHandles[i] only
        // as the end of the segment arriving at it, so each has one h and they
        // are different where the key spacing is uneven - which it is.
        const double after=i+1<count?double(times[i+1])-times[i]:0.0;
        const double before=i>0?double(times[i])-times[i-1]:0.0;
        const auto secant=[&](size_t k,int c)->double{
            const double dt=double(times[k+1])-times[k];
            return dt>0?(double(channel(values[k+1],c))-channel(values[k],c))/dt:0.0;
        };
        for(int c=0;c<width;++c){
            double slope=0;
            if(i==0){
                slope=count>2 && after>0 && double(times[2])-times[1]>0
                    ? endpointSlope(secant(0,c),secant(1,c),after,double(times[2])-times[1])
                    : (after>0?secant(0,c):0.0);
            }else if(i+1==count){
                slope=count>2 && before>0 && double(times[i-1])-times[i-2]>0
                    ? endpointSlope(secant(i-1,c),secant(i-2,c),before,double(times[i-1])-times[i-2])
                    : (before>0?secant(i-1,c):0.0);
            }else{
                slope=limitedSlope(secant(i-1,c),secant(i,c),before,after);
            }
            // Hermite handles are the derivative over the segment's own
            // normalized interval; bezier handles are the control points a third
            // of the way along that derivative. Which one the evaluator wants is
            // the track's own kind, and writing the wrong one is a curve three
            // times too fast at every key.
            const float start=kind==2?float(slope*after)
                                     :float(channel(values[i],c)+slope*after/3.0);
            const float end=kind==2?float(slope*before)
                                   :float(channel(values[i],c)-slope*before/3.0);
            setChannel(firstHandles[i],c,std::isfinite(start)?start:0.0f);
            setChannel(secondHandles[i],c,std::isfinite(end)?end:0.0f);
        }
    }
}
void parseTrack(const Bytes& b,size_t at,bool vector,M2AnimationTrack& out,
                uint32_t& duration,size_t& decoded) {
    out.interpolationType=b.get<uint16_t>(at);out.globalSequence=b.get<int16_t>(at+2);
    if(out.interpolationType>3)throw Invalid{"unsupported camera interpolation"};
    const uint32_t nTimes=b.u32(at+4),times=b.u32(at+8),nValues=b.u32(at+12),values=b.u32(at+16);
    if(nTimes==0 && nValues==0)return;
    if(out.globalSequence!=-1)throw Invalid{"cinematic uses an unsupported global sequence"};
    // Cinematic M2s contain one authored camera sequence. Reject a different
    // sequence shape instead of joining unrelated animation timelines.
    if(nTimes!=1 || nValues!=1)throw Invalid{"camera has unsupported animation sequence count"};
    const uint32_t count=b.u32(times),timeOffset=b.u32(times+4),valueCount=b.u32(values),valueOffset=b.u32(values+4);
    if(count==0 && valueCount==0)return;
    if(count!=valueCount || count>kMaxKeys)throw Invalid{"camera key count is invalid"};
    const size_t stride=vector?36u:12u;
    if(!b.has(timeOffset,size_t(count)*4u) || !b.has(valueOffset,size_t(count)*stride))throw Invalid{"camera keys are outside file"};
    const size_t bytes=size_t(count)*(stride+sizeof(uint32_t));
    if(bytes>kMaxDecodedBytes-decoded)throw Invalid{"camera decoded memory budget exceeded"};
    decoded+=bytes;
    out.sequences.resize(1);auto& keys=out.sequences[0];keys.timestamps.reserve(count);
    if(vector){keys.vec3Values.reserve(count);keys.spline().vec3InTangents.reserve(count);keys.spline().vec3OutTangents.reserve(count);}
    else{keys.floatValues.reserve(count);keys.spline().floatInTangents.reserve(count);keys.spline().floatOutTangents.reserve(count);}
    uint32_t previous=0;
    for(uint32_t i=0;i<count;++i){
        const uint32_t time=b.u32(size_t(timeOffset)+i*4u);
        if(time>kMaxDurationMs || (i && time<=previous))throw Invalid{"camera timestamps are not strictly increasing"};
        previous=time;duration=std::max(duration,time);keys.timestamps.push_back(time);
        const size_t key=size_t(valueOffset)+i*stride;
        if(vector){keys.vec3Values.push_back(b.vec(key));keys.spline().vec3InTangents.push_back(b.vec(key+12));keys.spline().vec3OutTangents.push_back(b.vec(key+24));}
        else{keys.floatValues.push_back(b.number(key));keys.spline().floatInTangents.push_back(b.number(key+4));keys.spline().floatOutTangents.push_back(b.number(key+8));}
    }
    if(vector)stabilizeHandles(out.interpolationType,keys.timestamps,keys.vec3Values,
                               keys.spline().vec3InTangents,keys.spline().vec3OutTangents);
    else stabilizeHandles(out.interpolationType,keys.timestamps,keys.floatValues,
                          keys.spline().floatInTangents,keys.spline().floatOutTangents);
}
IntroSound sound(const Dbc& sounds,uint32_t id){
    IntroSound result;result.id=id;if(!id)return result;
    const auto row=sounds.row(id);const auto directory=sounds.s(row,23);
    for(uint32_t slot=0;slot<10;++slot){
        const auto file=sounds.s(row,3+slot);if(file.empty())continue;
        result.path=normalized(directory+(directory.empty() || directory.back()=='/' || directory.back()=='\\'?"":"/")+file);
        result.volume=std::clamp(sounds.f(row,24),0.0f,1.0f);return result;
    }
    throw Invalid{"cinematic sound has no authored filename"};
}
}
bool parseCharacterIntroCamera(const std::vector<uint8_t>& bytes,M2Camera& out,
                               uint32_t& durationMs,size_t& decodedBytes,std::string& reason){
    out={};durationMs=0;decodedBytes=0;reason.clear();
    try{
        const Bytes b{bytes};
        if(bytes.size()>kMaxModelBytes || !b.has(0,304) || std::memcmp(bytes.data(),"MD20",4)!=0)throw Invalid{"invalid cinematic M2 header or size"};
        if(b.u32(4)!=264)throw Invalid{"cinematic camera requires WotLK M2 version 264"};
        if(b.u32(272)!=1)throw Invalid{"cinematic M2 must contain one camera"};
        const size_t at=b.u32(276);if(!b.has(at,100))throw Invalid{"cinematic camera header outside file"};
        M2Camera camera;camera.type=b.u32(at);camera.fov=b.number(at+4);camera.farClip=b.number(at+8);camera.nearClip=b.number(at+12);
        if(camera.fov<=0 || camera.fov>=3.1415927f)throw Invalid{"invalid cinematic field of view"};
        camera.positionBase=b.vec(at+36);camera.targetBase=b.vec(at+68);
        uint32_t duration=0;size_t decoded=0;
        parseTrack(b,at+16,true,camera.positions,duration,decoded);
        parseTrack(b,at+48,true,camera.targets,duration,decoded);
        parseTrack(b,at+80,false,camera.roll,duration,decoded);
        if(duration==0 || camera.positions.sequences.empty())throw Invalid{"cinematic contains no camera flight"};
        out=std::move(camera);durationMs=duration;decodedBytes=decoded;return true;
    }catch(const Invalid& invalid){reason=invalid.reason;return false;}
    catch(const std::bad_alloc&){reason="not enough memory for cinematic camera";return false;}
}
bool loadCharacterIntro(uint8_t race,uint8_t classId,uint32_t mapId,const IntroReadFile& read,
                        CharacterIntroPlan& out,std::string& reason){
    out={};reason.clear();if(!read){reason="intro asset reader is unavailable";return false;}
    try{
        CharacterIntroPlan plan;plan.mapId=mapId;
        {const Dbc classes(read,"ChrClasses",60);plan.sequenceId=classes.u(classes.row(classId),58);}
        if(!plan.sequenceId){const Dbc races(read,"ChrRaces",69);plan.sequenceId=races.u(races.row(race),12);}
        if(!plan.sequenceId)throw Invalid{"character has no cinematic sequence"};
        uint32_t sequenceSound=0;std::vector<uint32_t> cameraIds;
        {const Dbc sequences(read,"CinematicSequences",10);const auto row=sequences.row(plan.sequenceId);sequenceSound=sequences.u(row,1);
            for(uint32_t col=2;col<10;++col){const auto id=sequences.u(row,col);if(id)cameraIds.push_back(id);}}
        if(cameraIds.empty())throw Invalid{"cinematic sequence has no cameras"};
        {const Dbc cameras(read,"CinematicCamera",7);
            for(auto id:cameraIds){const auto row=cameras.row(id);CharacterIntroShot shot;shot.cameraId=id;
                shot.modelPath=normalized(cameras.s(row,1));const auto dot=shot.modelPath.find_last_of('.');
                if(dot==std::string::npos)throw Invalid{"cinematic model has no extension"};
                const auto ext=shot.modelPath.substr(dot);if(ext!=".m2" && ext!=".mdx")throw Invalid{"unsupported cinematic model extension"};
                shot.modelPath.replace(dot,std::string::npos,".m2");shot.narration.id=cameras.u(row,2);
                shot.originServer={cameras.f(row,3),cameras.f(row,4),cameras.f(row,5)};shot.originFacing=cameras.f(row,6);
                plan.shots.push_back(std::move(shot));}}
        if(sequenceSound || std::any_of(plan.shots.begin(),plan.shots.end(),[](const auto& s){return s.narration.id!=0;})){
            const Dbc sounds(read,"SoundEntries",30);plan.sequenceSound=sound(sounds,sequenceSound);
            for(auto& shot:plan.shots)shot.narration=sound(sounds,shot.narration.id);
        }
        for(auto& shot:plan.shots){
            size_t decoded=0;auto bytes=readBounded(read,shot.modelPath,kMaxModelBytes);
            if(!parseCharacterIntroCamera(bytes,shot.camera,shot.durationMs,decoded,reason))return false;
            if(decoded>kMaxDecodedBytes-plan.decodedBytes || shot.durationMs>kMaxDurationMs-plan.durationMs)throw Invalid{"cinematic sequence exceeds duration or memory budget"};
            plan.decodedBytes+=decoded;plan.durationMs+=shot.durationMs;
        }
        out=std::move(plan);return true;
    }catch(const Invalid& invalid){reason=invalid.reason;return false;}
    catch(const std::exception&){reason="cinematic asset read or allocation failed";return false;}
}
} // namespace wowee::pipeline
