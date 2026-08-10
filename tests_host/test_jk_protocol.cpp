#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>
#include "bms_type.h"
#include "protocol/jk_protocol.h"

static uint32_t g_now = 1234;
uint32_t millis() { return g_now; }

namespace DiagnosticLog {
bool begin() { return true; }
void write(const char*) {}
void printf(const char*, ...) {}
}

static BmsData g_data;
static int g_status_calls = 0;
static char g_hw[17] = {};
static char g_sw[17] = {};
static int g_info_calls = 0;

static void onStatus(const BmsData &d) { g_data = d; ++g_status_calls; }
static void onInfo(const char *hw, const char *sw) {
  std::snprintf(g_hw, sizeof(g_hw), "%s", hw);
  std::snprintf(g_sw, sizeof(g_sw), "%s", sw);
  ++g_info_calls;
}

static void put16(std::vector<uint8_t>& f, size_t p, uint16_t v) {
  f[p] = v & 0xff; f[p+1] = (v >> 8) & 0xff;
}
static void puti16(std::vector<uint8_t>& f, size_t p, int16_t v) { put16(f,p,(uint16_t)v); }
static void put32(std::vector<uint8_t>& f, size_t p, uint32_t v) {
  f[p] = v & 0xff; f[p+1]=(v>>8)&0xff; f[p+2]=(v>>16)&0xff; f[p+3]=(v>>24)&0xff;
}
static void puti32(std::vector<uint8_t>& f, size_t p, int32_t v) { put32(f,p,(uint32_t)v); }
static void finish(std::vector<uint8_t>& f) {
  uint8_t sum=0; for(size_t i=0;i<299;i++) sum=(uint8_t)(sum+f[i]); f[299]=sum;
}
static std::vector<uint8_t> base(uint8_t type) {
  std::vector<uint8_t> f(300,0); f[0]=0x55; f[1]=0xAA; f[2]=0xEB; f[3]=0x90; f[4]=type; return f;
}
static std::vector<uint8_t> infoFrame(const char* hw) {
  auto f=base(0x03); std::memcpy(&f[22],hw,std::strlen(hw)); std::memcpy(&f[30],"V11.20",6); finish(f); return f;
}
static std::vector<uint8_t> status24() {
  auto f=base(0x02);
  for(int i=0;i<20;i++) put16(f,6+i*2,(uint16_t)(3700+i));
  put32(f,54,(1u<<20)-1u);
  put32(f,118,74190); puti32(f,126,-12345);
  puti16(f,130,251); puti16(f,132,263); puti16(f,134,310);
  put16(f,136,0x0123); puti16(f,138,145); f[140]=1; f[141]=78;
  put32(f,142,54321); put32(f,146,70000); put32(f,150,321); put32(f,154,123456);
  f[158]=95; put32(f,162,987654); f[166]=1; f[167]=1;
  puti16(f,222,270); puti16(f,224,280); puti16(f,226,290); f[243]=2;
  finish(f); return f;
}
static std::vector<uint8_t> status32() {
  auto f=base(0x02);
  for(int i=0;i<28;i++) put16(f,6+i*2,(uint16_t)(3300+i));
  put32(f,70,(1u<<28)-1u);
  puti16(f,144,355); put32(f,150,92778); puti32(f,158,4567);
  puti16(f,162,221); puti16(f,164,232); put32(f,166,0x89ABCDEF);
  puti16(f,170,-75); f[172]=2; f[173]=66;
  put32(f,174,100000); put32(f,178,150000); put32(f,182,456); put32(f,186,654321);
  f[190]=91; put32(f,194,1234567); f[198]=1; f[199]=0;
  puti16(f,254,240); puti16(f,256,250); puti16(f,258,260); f[275]=1;
  finish(f); return f;
}
static void feedChunked(JkProtocolDecoder& d, const std::vector<uint8_t>& f) {
  size_t p=0; const size_t chunks[]={7,13,20,55,1,74,130};
  for(size_t n:chunks){ if(p>=f.size()) break; size_t c=std::min(n,f.size()-p); d.feed(f.data()+p,c); p+=c; }
  if(p<f.size()) d.feed(f.data()+p,f.size()-p);
}
int main(){
  BmsType parsedType = BmsType::Ant;
  assert(BmsTypeInfo::parse("jk", parsedType) && parsedType == BmsType::Jikong);
  assert(BmsTypeInfo::parse("ANT", parsedType) && parsedType == BmsType::Ant);
  assert(!BmsTypeInfo::parse("unknown", parsedType));

  uint8_t cmd[20]; JkProtocolDecoder::buildReadCommand(0x96,cmd);
  assert(cmd[0]==0xAA && cmd[1]==0x55 && cmd[2]==0x90 && cmd[3]==0xEB && cmd[4]==0x96);
  for(int i=5;i<19;i++) assert(cmd[i]==0);
  uint8_t sum=0; for(int i=0;i<19;i++) sum=(uint8_t)(sum+cmd[i]); assert(sum==cmd[19]);

  JkProtocolDecoder d(onStatus,onInfo);
  auto info=infoFrame("V11.XW"); feedChunked(d,info);
  assert(g_info_calls==1 && std::strcmp(g_hw,"V11.XW")==0);
  assert(d.detectedVariant()==JkProtocolDecoder::Variant::Jk02_32S);
  auto f32=status32(); feedChunked(d,f32);
  assert(g_status_calls==1); assert(g_data.cellCount==28); assert(g_data.soc==66); assert(g_data.soh==91);
  assert(std::fabs(g_data.totalVoltage-92.778f)<0.002f); assert(std::fabs(g_data.current-4.567f)<0.002f);
  assert(g_data.reportedCycleCount==456); assert(g_data.batteryType==1); assert(g_data.chargeMos==1 && g_data.dischargeMos==0);
  assert(g_data.batteryStatus==0xEF); assert(g_data.temperatureCount==5); assert(g_data.mosTemperature==36);

  d.reset(); g_status_calls=0; g_info_calls=0;
  auto f24=status24();
  std::vector<uint8_t> noisy={0x00,0x55,0x12,0x55,0xAA}; noisy.insert(noisy.end(),f24.begin(),f24.end());
  d.feed(noisy.data(),noisy.size());
  assert(g_status_calls==1); assert(d.detectedVariant()==JkProtocolDecoder::Variant::Jk02_24S);
  assert(g_data.cellCount==20); assert(g_data.soc==78); assert(g_data.reportedCycleCount==321); assert(g_data.batteryType==0);
  assert(std::fabs(g_data.totalVoltage-74.190f)<0.002f); assert(std::fabs(g_data.current+12.345f)<0.002f);
  assert(g_data.batteryStatus==0x23); assert(g_data.temperatureCount==5); assert(g_data.mosTemperature==31);

  auto bad=f24; bad[100]^=0x01; d.feed(bad.data(),bad.size()); assert(g_status_calls==1);

  d.reset(); g_status_calls=0; g_info_calls=0;
  auto misleadingInfo=infoFrame("V10.XW"); feedChunked(d,misleadingInfo);
  assert(d.detectedVariant()==JkProtocolDecoder::Variant::Jk02_24S);
  feedChunked(d,f32);
  assert(g_status_calls==1);
  assert(d.detectedVariant()==JkProtocolDecoder::Variant::Jk02_32S);
  assert(g_data.cellCount==28 && g_data.soc==66);

  std::puts("JK protocol host tests passed");
}
