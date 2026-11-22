#include "BrxWalker.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <new>
#include <stdexcept>
#include <unordered_map>

#define STB_DXT_IMPLEMENTATION
#include "stb_dxt.h"

namespace {
void U16(std::vector<uint8_t>& o,uint16_t v){o.push_back(uint8_t(v));o.push_back(uint8_t(v>>8));}
void U32(std::vector<uint8_t>& o,uint32_t v){for(unsigned s=0;s<32;s+=8)o.push_back(uint8_t(v>>s));}
void S32(std::vector<uint8_t>& o,int32_t v){U32(o,uint32_t(v));}
void F32(std::vector<uint8_t>& o,float v){uint32_t b;std::memcpy(&b,&v,4);U32(o,b);}
void ReadSaa(CBinaryInputStream& s){auto t=s.U16Read();if(t==0||t==0xffff)return;if(t>8)throw std::runtime_error("Unknown shader animation type");s.S16Read();s.U16Read();s.Skip(24);}
#ifdef BRX_REBUILDER_3DS
constexpr int kBrxDxtMode = STB_DXT_NORMAL;
#else
constexpr int kBrxDxtMode = STB_DXT_HIGHQUAL;
#endif
enum : uint32_t { TextureRgba8, TextureBc1, TextureBc3 };
struct PcImage{uint32_t w{},h{},zone{},mips{},flags{},offset{},format{},dataSize{};uint64_t hash{};};
constexpr uint32_t kGenerateMipmaps = 1u << 2;
constexpr uint8_t kShdkThreeWay = 0;
constexpr uint8_t kShdkPrelit = 1;

bool BcEligibleShader(uint8_t shaderKind){
 return shaderKind == kShdkThreeWay || shaderKind == kShdkPrelit;
}

std::array<uint8_t,256> BuildCsmTable(){
 std::array<uint8_t,256> csm{};
 for(uint16_t i=0;i<256;i+=32)
  for(uint16_t j=i;j<i+8;++j){
   csm[j]=uint8_t(j);csm[j+8]=uint8_t(j+16);csm[j+16]=uint8_t(j+8);csm[j+24]=uint8_t(j+24);
  }
 return csm;
}

uint64_t HashTextureBytes(const uint8_t* data, size_t count){
 uint64_t h=14695981039346656037ull;
 for(size_t i=0;i<count;++i)h=(h^data[i])*1099511628211ull;
 return h;
}

void DecodeBitmapPixels(const uint8_t* ix, size_t ixSize, const uint8_t* pal, size_t palSize,
 uint32_t width, uint32_t height, uint16_t colorCount, bool flip,
 const std::array<uint8_t,256>& csm, std::vector<uint8_t>& rgba){
 const size_t np = size_t(width) * height;
 rgba.resize(np * 4u);
 for(size_t p=0;p<np;++p){
  uint32_t k;
  if(colorCount>16){
   if(p>=ixSize)throw std::runtime_error("Truncated 8-bit texture");
   k=csm[ix[p]];
  }else{
   if(p/2>=ixSize)throw std::runtime_error("Truncated 4-bit texture");
   k=(ix[p/2]>>((p&1)*4))&15;
  }
  const size_t q=size_t(k)*4u;
  if(q+3>=palSize)throw std::runtime_error("Texture palette index is out of range");
  rgba[p*4]=pal[q];
  rgba[p*4+1]=pal[q+1];
  rgba[p*4+2]=pal[q+2];
  rgba[p*4+3]=uint8_t(std::min(255u,unsigned(pal[q+3])*255u/128u));
 }
 if(flip){
  const size_t row=size_t(width)*4u;
  for(uint32_t y=0;y<height/2;++y)
   std::swap_ranges(rgba.begin()+y*row,rgba.begin()+(y+1)*row,rgba.begin()+(height-1-y)*row);
 }
}

void CompressBcInto(const std::vector<uint8_t>& rgba, uint32_t width, uint32_t height,
 uint32_t mipCount, uint32_t format, std::vector<uint8_t>& out){
 const size_t blockBytes = format == TextureBc1 ? 8u : 16u;
 size_t totalBytes = 0;
 uint32_t w = width, h = height;
 for(uint32_t level=0;level<mipCount;++level){
  const uint32_t blocksX=(w+3)/4, blocksY=(h+3)/4;
  totalBytes += size_t(blocksX) * blocksY * blockBytes;
  w = std::max<uint32_t>(1u, w / 2);
  h = std::max<uint32_t>(1u, h / 2);
 }
 out.resize(totalBytes);
 size_t sourceOffset = 0;
 size_t destOffset = 0;
 w = width; h = height;
 for(uint32_t level=0;level<mipCount;++level){
  const uint32_t blocksX=(w+3)/4, blocksY=(h+3)/4;
  uint8_t* destination = out.data() + destOffset;
  for(uint32_t by=0;by<blocksY;++by)for(uint32_t bx=0;bx<blocksX;++bx){
   uint8_t block[64];
   for(uint32_t y=0;y<4;++y)for(uint32_t x=0;x<4;++x){
    const uint32_t sx=std::min(w-1,bx*4+x), sy=std::min(h-1,by*4+y);
    std::memcpy(block+(y*4+x)*4, rgba.data()+sourceOffset+(size_t(sy)*w+sx)*4, 4);
   }
   stb_compress_dxt_block(destination, block, format == TextureBc3, kBrxDxtMode);
   destination += blockBytes;
  }
  destOffset += size_t(blocksX) * blocksY * blockBytes;
  sourceOffset += size_t(w) * h * 4u;
  w = std::max<uint32_t>(1u, w / 2);
  h = std::max<uint32_t>(1u, h / 2);
 }
 if(sourceOffset != rgba.size())
  throw std::runtime_error("RGBA mip chain size mismatch before BC compression");
}

void EncodeMemoryTexture(const uint8_t* texBase, size_t texSize,
 uint32_t width, uint32_t height, uint32_t bmOff, uint32_t bmSize,
 uint16_t colorCount, uint16_t colorSize, uint32_t clutOff,
 bool flip, bool allowBc, const std::array<uint8_t,256>& csm,
 std::vector<uint8_t>& rgbaScratch, std::vector<uint8_t>& bcScratch,
 const uint8_t*& outPtr, size_t& outSize, uint32_t& outFormat){
 const size_t palSize = size_t(colorCount) * colorSize * 4u;
 if(bmOff > texSize || bmSize > texSize - bmOff ||
    clutOff > texSize || palSize > texSize - clutOff)
  throw std::runtime_error("Texture source block extends past EOF");
 DecodeBitmapPixels(texBase + bmOff, bmSize, texBase + clutOff, palSize,
  width, height, colorCount, flip, csm, rgbaScratch);
 if(flip || !allowBc){
  outFormat = TextureRgba8;
  outPtr = rgbaScratch.data();
  outSize = rgbaScratch.size();
  return;
 }
 bool opaque = true;
 for(size_t alpha=3; alpha<rgbaScratch.size(); alpha+=4){
  if(rgbaScratch[alpha] != 255){opaque=false;break;}
 }
 outFormat = opaque ? TextureBc1 : TextureBc3;
 CompressBcInto(rgbaScratch, width, height, 1u, outFormat, bcScratch);
 outPtr = bcScratch.data();
 outSize = bcScratch.size();
}

struct TexLookupKey {
 uint16_t bi;
 uint16_t ci;
 uint32_t imFlags;
 uint8_t font;
 uint8_t allowBc;
 bool operator==(const TexLookupKey& other) const {
  return bi == other.bi && ci == other.ci && imFlags == other.imFlags &&
         font == other.font && allowBc == other.allowBc;
 }
};
struct TexLookupKeyHash {
 size_t operator()(const TexLookupKey& k) const {
  uint64_t h = (uint64_t(k.bi) << 48) | (uint64_t(k.ci) << 32) | uint64_t(k.imFlags);
  h ^= (uint64_t(k.font) << 1) | uint64_t(k.allowBc);
  h ^= h >> 33;
  h *= 0xff51afd7ed558ccduLL;
  h ^= h >> 33;
  return size_t(h);
 }
};
}

void BrxWalker::LoadShadersFromBrx(CBinaryInputStream& s){
 const auto begin=s.GetOffset();textureTablesBegin_=begin;sourceCluts_.clear();sourceBitmaps_.clear();pendingTextures_.clear();pendingFonts_.clear();
 const auto nc=s.U16Read();sourceCluts_.reserve(nc);for(uint16_t i=0;i<nc;++i){SourceClut c;c.grfzon=s.U32Read();c.colorCount=s.U16Read();c.colorSize=s.U16Read();c.offset=s.U32Read();sourceCluts_.push_back(c);}
 const auto nb=s.U16Read();sourceBitmaps_.reserve(nb);for(uint16_t i=0;i<nb;++i){SourceBitmap b;b.width=s.U16Read();b.height=s.U16Read();b.grfzon=s.U32Read();b.psm=s.S8Read();b.cgsRow=s.S8Read();b.cgsPixels=s.U16Read();b.size=s.U32Read();b.offset=s.U32Read();sourceBitmaps_.push_back(b);}textureTablesEnd_=s.GetOffset();
 const auto ns=s.U16Read();s.U16Read();shaderColors_.assign(ns,{1,1,1,1});
 for(uint16_t si=0;si<ns;++si){auto kind=s.U8Read();s.U8Read();s.S16Read();auto cb=s.GetOffset();std::vector<uint8_t> fc;for(int c=0;c<8;++c){float v=float(s.U8Read())/255.0f;F32(fc,v);if(c<4)shaderColors_[si][c]=v;}RecordReplacement(cb,s.GetOffset(),std::move(fc));s.U32Read();s.U16Read();s.U8Read();auto nt=s.U8Read();ReadSaa(s);
  for(uint8_t ti=0;ti<nt;++ti){PendingTexture p;p.begin=s.GetOffset();p.shaderKind=kind;p.oid=s.S16Read();p.flags=s.S16Read();auto nbi=s.U8Read(),nci=s.U8Read();for(uint8_t i=0;i<nbi;++i)p.bitmaps.push_back(s.U16Read());for(uint8_t i=0;i<nci;++i)p.cluts.push_back(s.U16Read());p.end=s.GetOffset();pendingTextures_.push_back(std::move(p));}}
 const auto nf=s.U16Read();for(uint16_t fi=0;fi<nf;++fi){PendingFont p;p.referenceBegin=s.GetOffset();p.bitmap=s.U16Read();p.clut=s.S16Read();if(p.bitmap>=nb||p.clut<0||p.clut>=nc)throw std::runtime_error("Font texture reference is out of range");pendingFonts_.push_back(p);s.U8Read();s.U8Read();s.U8Read();s.U8Read();s.F32Read();auto ng=s.U32Read();if(ng>0x10000u)throw std::runtime_error("Implausible font glyph count");s.Skip(size_t(ng)*8);}
 Trace("LoadShadersFromBrx",begin,s.GetOffset());
}

void BrxWalker::LoadTexturesFromBrx(CBinaryInputStream& s){
 const auto start=s.GetOffset(),size=s.Remaining();const auto csm=BuildCsmTable();
 auto block=[&](uint32_t off,size_t cb){if(off>size||cb>size-off)throw std::runtime_error("Texture source block extends past EOF");std::vector<uint8_t> v(cb);s.Seek(start+off);s.ReadBytes(v.data(),v.size());return v;};
 auto decode=[&](uint16_t bi,uint16_t ci,bool flip){if(bi>=sourceBitmaps_.size()||ci==0xffff||ci>=sourceCluts_.size())throw std::runtime_error("Texture bitmap/CLUT reference is out of range");auto& b=sourceBitmaps_[bi];auto& c=sourceCluts_[ci];auto ix=block(b.offset,b.size);auto pal=block(c.offset,size_t(c.colorCount)*c.colorSize*4);std::vector<uint8_t> rgba;DecodeBitmapPixels(ix.data(),ix.size(),pal.data(),pal.size(),b.width,b.height,c.colorCount,flip,csm,rgba);return rgba;};
 std::vector<PcImage> images;
 std::vector<uint8_t> payload;
 // Retail relocation leaves invalid/null texture references unresolved. The
 // renderer then skips that map; it does not attempt to decode it. Preserve
 // that behavior instead of rejecting otherwise valid BRX files.
 // Store only authored level-zero RGBA. The original loader generated world
 // mipmaps through OpenGL after upload; bit 2 requests that same runtime path.
 // Fonts keep their original single-level behavior.
 // Only ordinary world geometry is eligible for lossy block compression.
 // Keep UI-like, generated and effect textures lossless: those categories
 // contain sharp artwork, masks and gradients which expose BC artifacts.
 auto add=[&](uint16_t bi,uint16_t ci,uint32_t flags,bool font,bool allowBc){
  if(bi==0xffff||ci==0xffff||bi>=sourceBitmaps_.size()||ci>=sourceCluts_.size())return int32_t(-1);
  auto& b=sourceBitmaps_[bi];PcImage im;im.w=b.width;im.h=b.height;im.zone=b.grfzon;im.flags=flags|(font?0u:kGenerateMipmaps);im.mips=1;
  auto rgba=decode(bi,ci,font);
  std::vector<uint8_t> encoded;
  if(font||!allowBc){im.format=TextureRgba8;encoded=std::move(rgba);}
  else{
   bool opaque=true;
   for(size_t alpha=3;alpha<rgba.size();alpha+=4)if(rgba[alpha]!=255){opaque=false;break;}
   im.format=opaque?TextureBc1:TextureBc3;
   CompressBcInto(rgba,im.w,im.h,im.mips,im.format,encoded);
  }
  im.dataSize=uint32_t(encoded.size());
  im.hash=HashTextureBytes(encoded.data(),encoded.size());
  for(size_t i=0;i<images.size();++i)
   if(images[i].w==im.w&&images[i].h==im.h&&images[i].zone==im.zone&&images[i].flags==im.flags&&images[i].format==im.format&&images[i].dataSize==im.dataSize&&images[i].hash==im.hash&&(im.dataSize==0||std::memcmp(payload.data()+images[i].offset,encoded.data(),im.dataSize)==0))
    return int32_t(i);
  im.offset=uint32_t(payload.size());
  payload.insert(payload.end(),encoded.begin(),encoded.end());
  images.push_back(im);return int32_t(images.size()-1);
 };
#ifdef BRX_REBUILDER_3DS
 size_t texIndex=0;
 if(progress_&&!progress_(progressContext_,0,unsigned(pendingTextures_.size())))throw std::runtime_error("CANCELLED");
#endif
 for(auto& t:pendingTextures_){const bool allowBc=BcEligibleShader(t.shaderKind);std::vector<uint8_t> out;U16(out,uint16_t(t.oid));U16(out,uint16_t(t.flags));U16(out,uint16_t(t.bitmaps.size()));for(size_t f=0;f<t.bitmaps.size();++f){int32_t a=-1,d=-1,z=-1;if(t.shaderKind==kShdkThreeWay){size_t b=f*3;if(b+2>=t.cluts.size())throw std::runtime_error("Three-way texture lacks three CLUTs per frame");a=add(t.bitmaps[f],t.cluts[b],t.flags,false,allowBc);d=add(t.bitmaps[f],t.cluts[b+1],t.flags,false,allowBc);z=add(t.bitmaps[f],t.cluts[b+2],t.flags,false,allowBc);}else{if(f>=t.cluts.size())throw std::runtime_error("Texture lacks a CLUT for one frame");d=add(t.bitmaps[f],t.cluts[f],t.flags,false,allowBc);}S32(out,a);S32(out,d);S32(out,z);}RecordReplacement(t.begin,t.end,std::move(out));
#ifdef BRX_REBUILDER_3DS
  ++texIndex;if(progress_&&!progress_(progressContext_,unsigned(texIndex),unsigned(pendingTextures_.size())))throw std::runtime_error("CANCELLED");
#endif
 }
 for(auto& f:pendingFonts_){std::vector<uint8_t> out;S32(out,add(f.bitmap,uint16_t(f.clut),0,true,false));RecordReplacement(f.referenceBegin,f.referenceBegin+4,std::move(out));}
 std::vector<uint8_t> table;U32(table,uint32_t(images.size()));for(auto& im:images){U32(table,im.w);U32(table,im.h);U32(table,im.zone);U32(table,im.mips);U32(table,im.dataSize);U32(table,im.offset);U32(table,im.flags);U32(table,im.format);}RecordReplacement(textureTablesBegin_,textureTablesEnd_,std::move(table));RecordReplacement(start,s.GetSize(),std::move(payload));s.Seek(s.GetSize());Trace("LoadTexturesFromBrx (SHDK-selected RGBA8/BC1/BC3)",start,s.GetOffset());
}

void BrxWalker::ResolveTexturesForStreaming(CBinaryInputStream& s, std::vector<ResolvedImage>& images){
 const uint8_t* mem = s.GetMemoryData();
 if(!mem)
  throw std::runtime_error("ResolveTexturesForStreaming requires a memory stream");
 const auto start = s.GetOffset();
 const auto size = s.Remaining();
 const uint8_t* texBase = mem + start;
 const auto csm = BuildCsmTable();

 images.clear();
 std::unordered_map<TexLookupKey, int32_t, TexLookupKeyHash> keyToIndex;
 std::unordered_map<uint64_t, std::vector<size_t>> hashBuckets;
 std::vector<uint8_t> rgbaBuf, bcBuf, compareRgbaBuf, compareBcBuf;
 uint64_t nextOffset = 0;
 size_t cachedBcBytes = 0;

 auto add = [&](uint16_t bi, uint16_t ci, uint32_t flags, bool font, bool allowBc) -> int32_t {
  if(bi==0xffff||ci==0xffff||bi>=sourceBitmaps_.size()||ci>=sourceCluts_.size())
   return int32_t(-1);
  const uint32_t imFlags = flags | (font ? 0u : kGenerateMipmaps);
  const TexLookupKey key{bi, ci, imFlags, uint8_t(font), uint8_t(allowBc)};
  const auto cached = keyToIndex.find(key);
  if(cached != keyToIndex.end())
   return cached->second;

  const auto& b = sourceBitmaps_[bi];
  const auto& c = sourceCluts_[ci];
  const uint8_t* encodedPtr = nullptr;
  size_t encodedSize = 0;
  uint32_t outFormat = 0;
  EncodeMemoryTexture(texBase, size,
   b.width, b.height, b.offset, b.size,
   c.colorCount, c.colorSize, c.offset,
   font, allowBc, csm,
   rgbaBuf, bcBuf, encodedPtr, encodedSize, outFormat);
  if(encodedSize > 0xffffffffu)
   throw std::runtime_error("Encoded texture size exceeds uint32 range");

  ResolvedImage im;
  im.w = b.width;
  im.h = b.height;
  im.zone = b.grfzon;
  im.mips = 1;
  im.flags = imFlags;
  im.format = outFormat;
  im.dataSize = uint32_t(encodedSize);
  im.hash = HashTextureBytes(encodedPtr, encodedSize);
  im.srcBi = bi;
  im.srcCi = ci;
  im.srcFont = font;
  im.srcAllowBc = allowBc;

  const auto bucketIt = hashBuckets.find(im.hash);
  if(bucketIt != hashBuckets.end()){
   for(const size_t idx : bucketIt->second){
    const ResolvedImage& existing = images[idx];
    if(existing.w != im.w || existing.h != im.h || existing.zone != im.zone ||
       existing.flags != im.flags || existing.format != im.format ||
       existing.dataSize != im.dataSize || existing.hash != im.hash)
     continue;
    bool equal = (im.dataSize == 0);
    if(!equal){
     if(existing.hasCachedBc){
      equal = (std::memcmp(existing.cachedBc.data(), encodedPtr, im.dataSize) == 0);
     }else{
      const auto& eb = sourceBitmaps_[existing.srcBi];
      const auto& ec = sourceCluts_[existing.srcCi];
      const uint8_t* existPtr = nullptr;
      size_t existSize = 0;
      uint32_t existFormat = 0;
      EncodeMemoryTexture(texBase, size,
       eb.width, eb.height, eb.offset, eb.size,
       ec.colorCount, ec.colorSize, ec.offset,
       existing.srcFont, existing.srcAllowBc, csm,
       compareRgbaBuf, compareBcBuf, existPtr, existSize, existFormat);
      equal = (existSize == im.dataSize && std::memcmp(existPtr, encodedPtr, im.dataSize) == 0);
     }
    }
    if(equal){
     keyToIndex.emplace(key, int32_t(idx));
     return int32_t(idx);
    }
   }
  }

  if(nextOffset + encodedSize > 0xffffffffull)
   throw std::runtime_error("Combined PCBRX texture payload exceeds uint32 offset range");
  im.offset = uint32_t(nextOffset);
  nextOffset += encodedSize;
  if(im.format != TextureRgba8 && encodedSize > 0 && cachedBcBytes + encodedSize <= maxBcCacheBytes_){
   try{
    im.cachedBc.assign(encodedPtr, encodedPtr + encodedSize);
    im.hasCachedBc = true;
    cachedBcBytes += encodedSize;
   }catch(const std::bad_alloc&){
    std::vector<uint8_t>().swap(im.cachedBc);
    im.hasCachedBc = false;
   }
  }

  const size_t newIdx = images.size();
  images.push_back(std::move(im));
  hashBuckets[images[newIdx].hash].push_back(newIdx);
  keyToIndex.emplace(key, int32_t(newIdx));
  return int32_t(newIdx);
 };

#ifdef BRX_REBUILDER_3DS
 size_t texIndex = 0;
 if(progress_ && !progress_(progressContext_, 0, unsigned(pendingTextures_.size())))
  throw std::runtime_error("CANCELLED");
#endif
 for(auto& t : pendingTextures_){
  const bool allowBc = BcEligibleShader(t.shaderKind);
  std::vector<uint8_t> out;
  out.reserve(6u + t.bitmaps.size() * 12u);
  U16(out, uint16_t(t.oid));
  U16(out, uint16_t(t.flags));
  U16(out, uint16_t(t.bitmaps.size()));
  for(size_t f=0; f<t.bitmaps.size(); ++f){
   int32_t a=-1, d=-1, z=-1;
   if(t.shaderKind == kShdkThreeWay){
    size_t b = f * 3u;
    if(b + 2 >= t.cluts.size())
     throw std::runtime_error("Three-way texture lacks three CLUTs per frame");
    a = add(t.bitmaps[f], t.cluts[b], t.flags, false, allowBc);
    d = add(t.bitmaps[f], t.cluts[b + 1], t.flags, false, allowBc);
    z = add(t.bitmaps[f], t.cluts[b + 2], t.flags, false, allowBc);
   }else{
    if(f >= t.cluts.size())
     throw std::runtime_error("Texture lacks a CLUT for one frame");
    d = add(t.bitmaps[f], t.cluts[f], t.flags, false, allowBc);
   }
   S32(out, a); S32(out, d); S32(out, z);
  }
  RecordReplacement(t.begin, t.end, std::move(out));
#ifdef BRX_REBUILDER_3DS
  ++texIndex;
  if(progress_ && !progress_(progressContext_, unsigned(texIndex), unsigned(pendingTextures_.size())))
   throw std::runtime_error("CANCELLED");
#endif
 }
 for(auto& f : pendingFonts_){
  std::vector<uint8_t> out;
  out.reserve(4u);
  S32(out, add(f.bitmap, uint16_t(f.clut), 0, true, false));
  RecordReplacement(f.referenceBegin, f.referenceBegin + 4u, std::move(out));
 }
 std::vector<uint8_t> table;
 table.reserve(4u + images.size() * 32u);
 U32(table, uint32_t(images.size()));
 for(const auto& im : images){
  U32(table, im.w); U32(table, im.h); U32(table, im.zone); U32(table, im.mips);
  U32(table, im.dataSize); U32(table, im.offset); U32(table, im.flags); U32(table, im.format);
 }
 RecordReplacement(textureTablesBegin_, textureTablesEnd_, std::move(table));
 s.Seek(s.GetSize());
 Trace("ResolveTexturesForStreaming (SHDK-selected RGBA8/BC1/BC3)", start, s.GetOffset());
}

void BrxWalker::WriteResolvedTextures(const uint8_t* source, size_t sourceSize,
 size_t texturesStart, std::vector<ResolvedImage>& images){
 if(texturesStart > sourceSize)
  throw std::runtime_error("invalid texture payload start offset");
 const uint8_t* texBase = source + texturesStart;
 const size_t texSize = sourceSize - texturesStart;
 const auto csm = BuildCsmTable();
 std::vector<uint8_t> rgbaBuf, bcBuf;
 const unsigned texTotal = unsigned(pendingTextures_.size());
 std::vector<PendingTexture>().swap(pendingTextures_);
 std::vector<PendingFont>().swap(pendingFonts_);
 std::vector<std::array<float, 4>>().swap(shaderColors_);

 for(size_t i = 0; i < images.size(); ++i){
  if((i & 63u) == 0 && progress_ && !progress_(progressContext_, texTotal, texTotal))
   throw std::runtime_error("CANCELLED");

  ResolvedImage& im = images[i];
  if(im.hasCachedBc){
   StreamWriteBytes(im.cachedBc.data(), im.cachedBc.size());
   std::vector<uint8_t>().swap(im.cachedBc);
   im.hasCachedBc = false;
  }else{
   if(im.srcBi >= sourceBitmaps_.size() || im.srcCi >= sourceCluts_.size())
    throw std::runtime_error("Texture bitmap/CLUT reference is out of range");
   const auto& b = sourceBitmaps_[im.srcBi];
   const auto& c = sourceCluts_[im.srcCi];
   const uint8_t* outPtr = nullptr;
   size_t outSize = 0;
   uint32_t outFormat = 0;
   EncodeMemoryTexture(texBase, texSize,
    b.width, b.height, b.offset, b.size,
    c.colorCount, c.colorSize, c.offset,
    im.srcFont, im.srcAllowBc, csm,
    rgbaBuf, bcBuf, outPtr, outSize, outFormat);
   if(outSize != im.dataSize || outFormat != im.format)
    throw std::runtime_error("Streaming texture re-encode size mismatch");
   StreamWriteBytes(outPtr, outSize);
  }
 }
}
