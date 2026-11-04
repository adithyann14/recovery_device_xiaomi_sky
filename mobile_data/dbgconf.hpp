/* SPDX-License-Identifier: Apache-2.0 */
// Runtime debug switches for the sky cellular provider.
// Plain "key=value" lines (# comments allowed). Looked up, per key, in:
//   /tmp/aera-dbg.conf  ->  /sdcard/aera-dbg.conf  ->  /data/media/0/aera-dbg.conf
// Manage it from the recovery shell with:  aera-dbg help
#pragma once
#include <cstdlib>
#include <fstream>
#include <string>

namespace aera::mobile::sky {

inline std::string DbgTrim(const std::string &text) {
  const char *space=" \t\r\n";
  const std::string::size_type first=text.find_first_not_of(space);
  if(first==std::string::npos)return std::string();
  const std::string::size_type last=text.find_last_not_of(space);
  return text.substr(first,last-first+1);
}

inline std::string DbgGet(const std::string &key,const std::string &fallback) {
  static const char *const files[]={"/tmp/aera-dbg.conf","/sdcard/aera-dbg.conf","/data/media/0/aera-dbg.conf"};
  for(const char *path:files) {
    std::ifstream in(path);
    if(!in)continue;
    std::string line,value;
    bool found=false;
    while(std::getline(in,line)) {
      line=DbgTrim(line);
      if(line.empty()||line[0]=='#')continue;
      const std::string::size_type eq=line.find('=');
      if(eq==std::string::npos)continue;
      if(DbgTrim(line.substr(0,eq))==key){value=DbgTrim(line.substr(eq+1));found=true;}
    }
    if(found)return value;
  }
  return fallback;
}

inline int DbgInt(const std::string &key,int fallback,int low,int high) {
  const std::string text=DbgGet(key,std::string());
  if(text.empty())return fallback;
  char *end=nullptr;
  const long value=std::strtol(text.c_str(),&end,10);
  if(end==text.c_str()||value<low||value>high)return fallback;
  return static_cast<int>(value);
}

}  // namespace aera::mobile::sky
