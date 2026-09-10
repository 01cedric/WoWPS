#!/usr/bin/env python3
"""Exercise the actual MPQ read method with injected StormLib open/read failures."""
from pathlib import Path
import os, shlex, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[2]
s=(ROOT/'src/pipeline/mpq_asset_source.cpp').read_text()
method=s[s.index('std::vector<uint8_t> MpqAssetSource::readFileBounded('):s.index('std::vector<std::string> MpqAssetSource::listFiles(')]
fixture=r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <vector>
#define LOG_WARNING(...) ((void)0)
using HANDLE=void*;using DWORD=uint32_t;
constexpr unsigned ERROR_NOT_ENOUGH_MEMORY=12,SFILE_OPEN_FROM_MPQ=0;
constexpr DWORD SFILE_INVALID_SIZE=UINT32_MAX;
static unsigned errorCode=0,openFailure=0,readFailure=0,opened=0,closed=0,baseAttempts=0,reads=0;
static DWORD logicalSize=6;
unsigned SErrGetLastError(){return errorCode;}
bool SFileHasFile(HANDLE,const char*){return true;}
bool SFileOpenFileEx(HANDLE archive,const char*,unsigned,HANDLE* file){
 if(archive==reinterpret_cast<void*>(1))++baseAttempts;
 if(archive==reinterpret_cast<void*>(2)&&openFailure){errorCode=openFailure;return false;}
 ++opened;*file=archive;return true;
}
DWORD SFileGetFileSize(HANDLE,DWORD* high){*high=0;return logicalSize;}
bool SFileReadFile(HANDLE file,void* data,DWORD size,DWORD* read,void*){
 ++reads;if(file==reinterpret_cast<void*>(2)&&readFailure){errorCode=readFailure;return false;}
 std::memset(data, file==reinterpret_cast<void*>(2)?'P':'B',size);*read=size;return true;
}
void SFileCloseFile(HANDLE){++closed;}
struct MpqFileCloser{void operator()(void* p)const noexcept{if(p)SFileCloseFile(p);}};
using MpqFileHandle=std::unique_ptr<void,MpqFileCloser>;
std::string lowerBackslash(std::string s){for(char& c:s){if(c=='/')c='\\';else c=std::tolower(static_cast<unsigned char>(c));}return s;}
void logAssetReadLimit(const char*,const std::string&,uint64_t,size_t){}
struct MpqAssetSource{
 mutable std::mutex mutex_;
 std::vector<void*> archives_{reinterpret_cast<void*>(1),reinterpret_cast<void*>(2)};
 std::vector<uint8_t> readFileBounded(const std::string&,size_t)const;
};
'''
cases=r'''
int main(){
 MpqAssetSource source;
 for(unsigned stage=0;stage<2;++stage){
  openFailure=stage==0?12:0;readFailure=stage==1?12:0;
  const unsigned oldOpened=opened,oldClosed=closed,oldBase=baseAttempts;
  bool oom=false;try{(void)source.readFileBounded("world/test.wmo",100);}catch(const std::bad_alloc&){oom=true;}
  assert(oom&&baseAttempts==oldBase&&opened-oldOpened==closed-oldClosed);
 }
 openFailure=readFailure=0;
 auto data=source.readFileBounded("world/test.wmo",100);assert(data.size()==6&&data.front()=='P');
 const auto oldBase=baseAttempts,oldReads=reads;
 data=source.readFileBounded("world/test.wmo",4);assert(data.empty()&&baseAttempts==oldBase&&reads==oldReads);
 openFailure=2;data=source.readFileBounded("world/test.wmo",100);assert(data.front()=='B');
 assert(opened==closed);
 std::puts("PASS: MPQ open/read allocation faults never fall through to older archives; handles closed; valid patch/size precedence and non-OOM fallback preserved");
}
'''
with tempfile.TemporaryDirectory(prefix='wowps-mpq-oom-')as temp:
 path=Path(temp)/'test.cpp';path.write_text(fixture+method+cases)
 binary=Path(temp)/'test';subprocess.run(shlex.split(os.environ.get('CXX','g++'))+['-std=c++20','-O1','-g','-fsanitize=address,undefined',str(path),'-o',str(binary)],check=True);subprocess.run([str(binary)],check=True)
