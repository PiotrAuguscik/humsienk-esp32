#include "humsienk.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
using namespace humsienk;

// Real anonymous measurement frame: no address, product serial or credentials.
const char* analogHex = "7e040103008c0072100ced0cf10cf00cf10cf10cf00cf10cf20cf20cf20cf00cf10cee0cef0cf00cf0060b7b0b9a0b680b680b670b69407614b40a900bc400050bb80059006400008d540000009d275f00000000c00905cd066a05ba05d9058f0588064d060405a405ee06350578059105cc0624066b1006000016440d";
Response hex(const char* text) {
  Response r; r.size=std::strlen(text)/2; assert(r.size<=kMaxFrame);
  for (std::size_t i=0; i<r.size; ++i) r.bytes[i]=std::stoul(std::string(text+2*i,2),nullptr,16);
  return r;
}
void seal(Response& r) {
  auto crc=crc16(r.bytes.data(),r.size-3); r.bytes[r.size-3]=crc>>8; r.bytes[r.size-2]=crc;
}
Response product() {
  Response r; r.size=71;
  const std::uint8_t prefix[]={0x7e,4,1,3,0,0x92,0,60};
  std::copy(prefix,prefix+8,r.bytes.begin());
  std::memcpy(r.bytes.data()+8,"SYNTHETIC",9);
  std::memcpy(r.bytes.data()+48,"HS04-DEMO",9);
  r.bytes[70]=13; seal(r); return r;
}
void near(double a,double b) { assert(std::fabs(a-b)<0.00001); }

struct Fake : Transport {
  bool online=false, timeout=false, failAuth=false, corrupt=false;
  std::uint32_t clock=0;
  unsigned opens=0, closes=0, auths=0;
  std::vector<ReadPoint> reads;
  Response pending{}; std::size_t offset=0;
  Error open(const Target&) override { online=true; ++opens; return Error::Ok; }
  void close() override { online=false; ++closes; }
  bool connected() const override { return online; }
  Error write(bool auth,const std::uint8_t* data,std::size_t n) override {
    if (auth) {
      assert(n==6 && !std::memcmp(data,"HiLink",6)); ++auths;
      return failAuth ? Error::Transport : Error::Ok;
    }
    assert(data[0]==0x7e && data[3]==3);
    assert(crc16(data,n-3)==((data[n-3]<<8)|data[n-2]));
    auto point=ReadPoint(data[5]); reads.push_back(point);
    if (point==ReadPoint::Product) pending=product();
    else if (point==ReadPoint::CollectionBoards) pending=hex("7e040103001e000c0101000100a700010000000052c60d");
    else if (point==ReadPoint::Analog) {
      assert(n==18 && data[1]==1);
      pending=hex(analogHex);
    } else pending=hex("7e040103008d002710000000000000000000000000000000000600000000000000000001000007de0000000000a00073420d");
    if (corrupt) pending.bytes[pending.size-2]^=1;
    offset=0; return Error::Ok;
  }
  Error receive(std::uint8_t* out,std::size_t capacity,std::size_t& n,std::uint32_t ms) override {
    if (timeout) { clock+=ms; n=0; return Error::Timeout; }
    n=std::min(std::size_t(7),pending.size-offset); // deliberate fragmentation
    if (n>capacity) return Error::Overflow;
    std::memcpy(out,pending.bytes.data()+offset,n); offset+=n; clock+=2; return Error::Ok;
  }
  void clearNotifications() override { offset=0; }
  Error readAuthStatus(std::uint8_t& status) override { status=1; return Error::Ok; }
  void wait(std::uint32_t ms) override { clock+=ms; }
  std::uint32_t nowMs() const override { return clock; }
};
Target target() { Target t; std::strcpy(t.address,"c0:00:00:00:00:01"); return t; }

int main() {
  assert(crc16(reinterpret_cast<const std::uint8_t*>("123456789"),9)==0x4b37);
  std::uint8_t out[18]; std::size_t n;
  auto expected=hex("7e000103009200009f220d");
  assert(buildRead(ReadPoint::Product,false,out,sizeof(out),n)==Error::Ok);
  assert(n==expected.size && !std::memcmp(out,expected.bytes.data(),n));
  expected=hex("7e010103008c0000000501002000208b420d");
  assert(buildRead(ReadPoint::Analog,true,out,sizeof(out),n)==Error::Ok);
  assert(n==expected.size && !std::memcmp(out,expected.bytes.data(),n));
  assert(buildRead(ReadPoint::Analog,true,out,17,n)==Error::Overflow && n==0);
  assert(buildRead(ReadPoint::Product,true,out,18,n)==Error::InvalidArgument);
  assert(buildRead(static_cast<ReadPoint>(0x82),false,out,18,n)==Error::InvalidArgument);
  assert(buildRead(ReadPoint::Analog,false,nullptr,18,n)==Error::InvalidArgument);
  auto frame=hex(analogHex);
  Snapshot s; assert(decodeSnapshot(frame,s)==Error::Ok);
  assert(s.socPercent==89 && s.cellCount==16 && s.temperatureCount==6);
  near(s.voltageV,53); near(s.currentA,11.8); near(s.remainingCapacityAh,270.4);
  near(s.minCellV(),3.309); near(s.maxCellV(),3.314);
  near(s.temperaturesC[0],20.9); near(s.temperaturesC[1],24);
  assert(s.experimental.present && s.experimental.sohPercent==100);
  near(s.experimental.balanceCurrentA,-0.9);
  assert(s.raw.size==125);
  // Anonymous measurement from the actual NimBLE qualification, full/idle pack.
  const auto idle = hex("7e040103008c0072100cdf0ce00ce00cdf0ce10ce10cdf0ce00cdf0cdf0ce00ce40ce00cdf0ce10cdf060b920bad0b7a0b7a0b780b7a4000149a0bc50bc500050bb80064006400008e8900000000273c00000000400005cd066a05ba05d9058f0588064d060405a405ee06350578059105cc0624066b100600007c160d");
  Snapshot full;
  assert(decodeSnapshot(idle,full)==Error::Ok);
  assert(full.socPercent==100 && full.cellCount==16 && full.temperatureCount==6);
  near(full.currentA,0); near(full.voltageV,52.74);
  near(full.remainingCapacityAh,301.3); near(full.totalCapacityAh,301.3);
  near(full.designCapacityAh,300); assert(full.cycles==5);
  near(full.minCellV(),3.295); near(full.maxCellV(),3.300);
  near(full.temperaturesC[0],23.2); near(full.temperaturesC[1],25.9);
  for (std::size_t i=0;i<frame.size;++i) {
    auto shortened=frame; shortened.size=i;
    s.socPercent=42;
    assert(decodeSnapshot(shortened,s)!=Error::Ok && s.socPercent==42);
  }
  auto bad=frame; std::swap(bad.bytes[122],bad.bytes[123]);
  assert(validateResponse(bad,ReadPoint::Analog)==Error::BadCrc);
  bad=frame; bad.bytes[2]=2; seal(bad);
  assert(validateResponse(bad,ReadPoint::Analog)==Error::WrongResponse);
  bad=frame; bad.bytes[1]=5; seal(bad);
  assert(validateResponse(bad,ReadPoint::Analog)==Error::UnsupportedVersion);
  bad=frame; bad.bytes[3]=0x86; seal(bad);
  assert(validateResponse(bad,ReadPoint::Analog)==Error::DeviceError);
  bad=frame; bad.bytes[8]=33; seal(bad);
  assert(decodeSnapshot(bad,s)==Error::InvalidData);
  bad=frame; bad.bytes[8+58]=0; bad.bytes[8+59]=101; seal(bad);
  assert(decodeSnapshot(bad,s)==Error::InvalidData);
  bad=frame; bad.bytes[6]=0xff;
  assert(validateResponse(bad,ReadPoint::Analog)==Error::Overflow);
  bad=frame; bad.size=kMaxFrame+1;
  assert(validateResponse(bad,ReadPoint::Analog)==Error::Overflow);
  bad=frame; bad.bytes[8+46]|=0x80; seal(bad);
  assert(decodeSnapshot(bad,s)==Error::Ok); near(s.currentA,-11.8);
  Product p; assert(decodeProduct(product(),p)==Error::Ok);
  assert(!std::strcmp(p.serial,"HS04-DEMO"));
  assert(validTarget(target())); assert(!validTarget(Target{}));
  auto invalid=target(); invalid.type=AddressType(4); assert(!validTarget(invalid));
  Fake transport; Session session(transport);
  assert(session.readSnapshot(s)==Error::NotConnected);
  assert(session.connect(Target{})==Error::InvalidArgument && transport.opens==0);
  assert(session.connect(target())==Error::Ok && session.connected());
  assert(transport.reads.size()==2 && transport.auths==1);
  assert(session.connect(target())==Error::Busy);
  assert(session.readSnapshot(s)==Error::Ok && s.socPercent==89);
  assert(session.readSnapshot(s)==Error::Ok && transport.opens==1);
  Response raw; assert(session.readRaw(ReadPoint::Warnings,raw)==Error::Ok);
  transport.timeout=true; s.socPercent=42;
  assert(session.readSnapshot(s)==Error::Timeout && !session.connected() && s.socPercent==42);
  transport.timeout=false;
  assert(session.connect(target())==Error::Ok && transport.opens==2);
  transport.clock=0xfffffc00u; // clock rollover during fragmented response
  assert(session.readSnapshot(s)==Error::Ok);
  transport.corrupt=true;
  assert(session.readSnapshot(s)==Error::BadCrc && !session.connected());
  transport.corrupt=false; transport.failAuth=true;
  assert(session.connect(target())==Error::Transport && !session.connected());
  std::cout << "Protocol vectors, captures, malformed frames, fragmentation, stale-output, timeout and reconnect tests passed\n";
}
