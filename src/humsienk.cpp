#include "humsienk.h"
#include <algorithm>
#include <cstring>

namespace humsienk {
namespace {
bool allowed(ReadPoint p) {
  return p == ReadPoint::Product || p == ReadPoint::CollectionBoards ||
         p == ReadPoint::Analog || p == ReadPoint::Warnings;
}
std::uint16_t be16(const std::uint8_t* p) { return (std::uint16_t(p[0]) << 8) | p[1]; }
std::uint32_t be32(const std::uint8_t* p) { return (std::uint32_t(be16(p)) << 16) | be16(p+2); }
double current(std::uint16_t raw) {
  double value = (raw & 0x3fff) / ((raw & 0x4000) ? 10.0 : 1.0);
  return (raw & 0x8000) ? -value : value;
}
void ascii(const std::uint8_t* p, char* out) {
  std::size_t n = 0;
  while (n < 20 && p[n]) { out[n] = (p[n] >= 32 && p[n] <= 126) ? char(p[n]) : '?'; ++n; }
  while (n && out[n-1] == ' ') --n;
  out[n] = 0;
}
}
const char* errorName(Error e) {
  switch (e) {
    case Error::Ok: return "ok";
    case Error::InvalidArgument: return "invalid_argument";
    case Error::NotConnected: return "not_connected";
    case Error::Busy: return "busy";
    case Error::Transport: return "transport";
    case Error::Timeout: return "timeout";
    case Error::Overflow: return "overflow";
    case Error::Incomplete: return "incomplete";
    case Error::InvalidFrame: return "invalid_frame";
    case Error::BadCrc: return "bad_crc";
    case Error::WrongResponse: return "wrong_response";
    case Error::UnsupportedVersion: return "unsupported_version";
    case Error::DeviceError: return "device_error";
    case Error::InvalidData: return "invalid_data";
  }
  return "unknown";
}
bool validTarget(const Target& t) {
  if (t.address[17] || (t.type != AddressType::Public && t.type != AddressType::Random)) return false;
  bool nonzero = false;
  for (std::size_t i=0; i<17; ++i) {
    const char c=t.address[i];
    if (i%3 == 2) { if (c != ':') return false; }
    else {
      if (!((c>='0' && c<='9') || (c>='a' && c<='f') || (c>='A' && c<='F'))) return false;
      nonzero = nonzero || c != '0';
    }
  }
  return nonzero;
}
std::uint16_t crc16(const std::uint8_t* data, std::size_t size) {
  std::uint16_t crc=0xffff;
  for (std::size_t i=0; i<size; ++i) {
    crc ^= data[i];
    for (unsigned b=0; b<8; ++b) crc = (crc>>1) ^ ((crc&1) ? 0xa001 : 0);
  }
  return crc;
}
Error buildRead(ReadPoint point, bool extended, std::uint8_t* out,
                std::size_t capacity, std::size_t& size) {
  size=0;
  if (!out || !allowed(point) || (extended && point != ReadPoint::Analog)) return Error::InvalidArgument;
  const std::size_t n=extended ? 18 : 11;
  if (capacity<n) return Error::Overflow;
  const std::uint8_t prefix[]={0x7e,std::uint8_t(extended),1,3,0,std::uint8_t(point),0,0};
  std::memcpy(out,prefix,8);
  if (extended) { const std::uint8_t info[]={0,5,1,0,32,0,32}; std::memcpy(out+8,info,7); }
  const auto crc=crc16(out,n-3);
  out[n-3]=crc>>8; out[n-2]=crc; out[n-1]=0x0d; size=n;
  return Error::Ok;
}
Error validateResponse(const Response& r, ReadPoint expected) {
  if (!allowed(expected)) return Error::InvalidArgument;
  if (r.size>kMaxFrame) return Error::Overflow;
  if (r.size<8) return Error::Incomplete;
  const auto* b=r.bytes.data();
  if (b[0]!=0x7e) return Error::InvalidFrame;
  const std::size_t total=std::size_t(be16(b+6))+11;
  if (total>kMaxFrame) return Error::Overflow;
  if (r.size<total) return Error::Incomplete;
  if (r.size!=total || b[total-1]!=0x0d) return Error::InvalidFrame;
  if (crc16(b,total-3)!=be16(b+total-3)) return Error::BadCrc;
  if (b[2]!=1 || be16(b+4)!=std::uint16_t(expected)) return Error::WrongResponse;
  if (b[1]!=4) return Error::UnsupportedVersion;
  if (b[3]==0x86) return Error::DeviceError;
  if (b[3]!=3) return Error::WrongResponse;
  return Error::Ok;
}
Error decodeProduct(const Response& r, Product& out) {
  const auto e=validateResponse(r,ReadPoint::Product); if (e!=Error::Ok) return e;
  if (r.size!=71) return Error::InvalidData;
  Product next{};
  ascii(r.bytes.data()+8,next.firmware);
  ascii(r.bytes.data()+28,next.manufacturer);
  ascii(r.bytes.data()+48,next.serial);
  out=next; return Error::Ok;
}
Error decodeSnapshot(const Response& r, Snapshot& out) {
  const auto e=validateResponse(r,ReadPoint::Analog); if (e!=Error::Ok) return e;
  const auto* d=r.bytes.data()+8;
  const auto n=r.size-11;
  if (n<1) return Error::InvalidData;
  const unsigned cells=d[0];
  if (!cells || cells>kMaxCells || n<2+2*cells) return Error::InvalidData;
  const unsigned temps=d[1+2*cells];
  const unsigned offset=2+2*cells+2*temps;
  if (temps<2 || temps>kMaxTemperatures || n<offset+14) return Error::InvalidData;
  const auto soc=be16(d+offset+12);
  if (soc>100) return Error::InvalidData;
  Snapshot next{};
  next.cellCount=cells; next.temperatureCount=temps; next.socPercent=soc;
  for (unsigned i=0; i<cells; ++i) next.cellMillivolts[i]=be16(d+1+2*i);
  for (unsigned i=0; i<temps; ++i) next.temperaturesC[i]=(int(be16(d+2+2*cells+2*i))-2730)/10.0;
  next.currentA=current(be16(d+offset)); next.voltageV=be16(d+offset+2)/100.0;
  next.remainingCapacityAh=be16(d+offset+4)/10.0;
  next.totalCapacityAh=be16(d+offset+6)/10.0;
  next.cycles=be16(d+offset+8); next.designCapacityAh=be16(d+offset+10)/10.0;
  if (n>=offset+32) {
    const auto* x=d+offset+14;
    next.experimental.present=true;
    next.experimental.sohPercent=be16(x);
    next.experimental.cumulativeCapacityAh=be32(x+2)/10.0;
    next.experimental.remainingMinutes=be32(x+6);
    next.experimental.balanceCurrentA=current(be16(x+16));
  }
  next.raw=r; out=next; return Error::Ok;
}
double Snapshot::minCellV() const {
  if (!cellCount || cellCount>kMaxCells) return 0;
  return *std::min_element(cellMillivolts.begin(),cellMillivolts.begin()+cellCount)/1000.0;
}
double Snapshot::maxCellV() const {
  if (!cellCount || cellCount>kMaxCells) return 0;
  return *std::max_element(cellMillivolts.begin(),cellMillivolts.begin()+cellCount)/1000.0;
}
void Session::disconnect() { ready_=false; version_=0; transport_.close(); }
Error Session::exchange(ReadPoint p, bool extended, Response& out) {
  if (!transport_.connected()) { ready_=false; return Error::NotConnected; }
  std::uint8_t request[18]{}; std::size_t size=0;
  auto e=buildRead(p,extended,request,sizeof(request),size);
  if (e!=Error::Ok) return e;
  transport_.wait(1000);
  transport_.clearNotifications();
  e=transport_.write(false,request,size);
  Response next{};
  const auto start=transport_.nowMs();
  while (e==Error::Ok && std::uint32_t(transport_.nowMs()-start)<10000) {
    const auto elapsed=std::uint32_t(transport_.nowMs()-start);
    if (elapsed>=10000) break;
    std::size_t received=0;
    e=transport_.receive(next.bytes.data()+next.size,kMaxFrame-next.size,received,10000-elapsed);
    if (e!=Error::Ok) break;
    if (std::uint32_t(transport_.nowMs()-start)>=10000) { e=Error::Timeout; break; }
    if (!received) { e=Error::Transport; break; }
    if (received>kMaxFrame-next.size) { e=Error::Overflow; break; }
    next.size+=received;
    e=validateResponse(next,p);
    if (e==Error::Ok) { out=next; return Error::Ok; }
    if (e!=Error::Incomplete) break;
    e=Error::Ok;
  }
  if (e==Error::Ok) e=Error::Timeout;
  disconnect(); return e;
}
Error Session::connect(const Target& target) {
  if (!validTarget(target)) return Error::InvalidArgument;
  if (transport_.connected()) return Error::Busy;
  ready_=false; version_=0;
  auto e=transport_.open(target);
  if (e!=Error::Ok) { transport_.close(); return e; }
  const std::uint8_t key[]={'H','i','L','i','n','k'};
  e=transport_.write(true,key,sizeof(key));
  if (e!=Error::Ok) { disconnect(); return e; }
  transport_.wait(500);
  std::uint8_t authStatus=0;
  e=transport_.readAuthStatus(authStatus);
  if (e!=Error::Ok || authStatus!=1) {
    disconnect(); return e==Error::Ok ? Error::DeviceError : e;
  }
  Response r{};
  e=exchange(ReadPoint::Product,false,r);
  Product product{};
  if (e==Error::Ok) e=decodeProduct(r,product);
  if (e!=Error::Ok) { disconnect(); return e; }
  version_=r.bytes[1];
  e=exchange(ReadPoint::CollectionBoards,false,r);
  if (e!=Error::Ok) return e;
  // Extended reads select board 1. Do not assume it exists on another model.
  const auto boards=r.bytes[8];
  if (!boards || boards>32 || r.size<12U+boards ||
      std::find(r.bytes.begin()+9,r.bytes.begin()+9+boards,1)==r.bytes.begin()+9+boards) {
    disconnect(); return Error::InvalidData;
  }
  ready_=true; return Error::Ok;
}
Error Session::readRaw(ReadPoint point, Response& out) {
  if (!connected()) return Error::NotConnected;
  return exchange(point,point==ReadPoint::Analog && version_>=4,out);
}
Error Session::readSnapshot(Snapshot& out) {
  Response r{}; auto e=readRaw(ReadPoint::Analog,r);
  if (e==Error::Ok) e=decodeSnapshot(r,out);
  if (e!=Error::Ok && e!=Error::NotConnected) disconnect();
  return e;
}
Error Session::readProduct(Product& out) {
  Response r{}; auto e=readRaw(ReadPoint::Product,r);
  if (e==Error::Ok) e=decodeProduct(r,out);
  if (e!=Error::Ok && e!=Error::NotConnected) disconnect();
  return e;
}
}  // namespace humsienk
