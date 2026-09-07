#pragma once
#include <map>
#include <memory>
#include <string>
#include <stdint.h>
#define FILE_READ "r"
#define FILE_WRITE "w"
class File {
public:
 std::shared_ptr<std::string> data;
 size_t cursor=0;
 File() {}
 explicit File(std::shared_ptr<std::string> d):data(d) {}
 explicit operator bool()const { return bool(data); }
 bool isDirectory()const { return false; }
 bool seek(size_t p) { cursor=p; return bool(data); }
 size_t position()const { return cursor; }
 size_t size()const { return data?data->size():0; }
 int read() { return data&&cursor<data->size()?uint8_t((*data)[cursor++]):-1; }
 void close() { data.reset(); }
 size_t write(const uint8_t* p,size_t n) { if(!data)return 0; data->append((const char*)p,n);return n; }
 size_t write(uint8_t c) { return write(&c,1); }
 void flush() {}
 int getWriteError()const { return 0; }
 File openNextFile() { return File(); }
 const char* name()const { return "test.nks"; }
};
namespace fs {
class FS {
public:
 std::map<std::string,std::shared_ptr<std::string>> files;
 File open(const char* p,const char* mode=FILE_READ) {
  if(mode[0]=='w')files[p]=std::make_shared<std::string>();
  auto i=files.find(p); return i==files.end()?File():File(i->second);
 }
 bool exists(const char* p) { return files.count(p); }
 bool mkdir(const char*) { return true; }
 bool remove(const char* p) { return files.erase(p); }
 bool rename(const char* a,const char* b) { if(!exists(a)||exists(b))return false;files[b]=files[a]; files.erase(a);return true; }
};
}
