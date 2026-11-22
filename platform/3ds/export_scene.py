#!/usr/bin/env python3
"""Build an instrumented copy of the upstream walker, never alter its parser."""
from pathlib import Path
import concurrent.futures,os,subprocess,argparse,shutil
ROOT=Path(__file__).resolve().parents[2];PLATFORM=ROOT/'platform/3ds'
SOURCE=ROOT/'File Extractor GUI/NativeTools/BrxRebuilder';OUT=ROOT/'build/scene-tools'
def replace(source,old,new):
    if source.count(old)!=1:raise RuntimeError('Upstream export hook changed: '+old[:80])
    return source.replace(old,new)
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('raw',type=Path);parser.add_argument('output',type=Path);args=parser.parse_args()
    OUT.mkdir(parents=True,exist_ok=True)
    for path in SOURCE.iterdir():
        if path.suffix not in ('.cpp','.h','.inc'):continue
        s=path.read_text()
        if path.name=='ObjectWalker.cpp':
            s='#include "scene_capture.h"\n'+s
            s=replace(s,'    const auto loaderStart = s.GetOffset();','    CaneObjectScope captureObject(cid,oid);\n    const auto loaderStart = s.GetOffset();')
            s=replace(s,'void BrxWalker::LoadAlo(CBinaryInputStream& s, unsigned depth) {','void BrxWalker::LoadAlo(CBinaryInputStream& s, unsigned depth) {\n    CaneTransformScope captureTransform(s);')
            s=replace(s,'    // Emitter origin options belong to this ALO itself, not to descendants.','    captureTransform.optionsActive=false;\n    // Emitter origin options belong to this ALO itself, not to descendants.')
            s=replace(s,'    const auto globCount = s.U16Read();','    const auto globCount = s.U16Read();\n    CaneGlobSet captureSet(boneCount, poseCount);')
            s=replace(s,'        const auto globStart = s.GetOffset();','        const auto globStart = s.GetOffset();\n        CaneGlob captureGlob(captureSet,g);')
            s=replace(s,'        if (instance) { s.S16Read(); s.ReadMatrix4(); }','        if (instance) { auto index=s.S16Read(); captureGlob.instance(index,s); }')
            s=replace(s,'        s.ReadVector(); s.F32Read(); s.S16Read(); s.U8Read(); s.U8Read(); s.U8Read();','        s.ReadVector(); s.F32Read(); s.S16Read(); s.U8Read(); captureGlob.priority=s.U8Read(); s.U8Read();')
            s=replace(s,'        if (instance) continue;','        if (instance) { captureGlob.finish(); continue; }')
            # There are separate mesh and cel-border replacement blocks. Capture only meshes.
            target='            RecordReplacement(pcBlockBegin, s.GetOffset(), std::move(replacement));'
            at=s.index(target);s=s[:at]+'            captureGlob.add(replacement);\n'+s[at:]
            s=replace(s,'    }\n    return poseCount;','        captureGlob.finish();\n    }\n    return poseCount;')
        elif path.name=='OptionWalker.cpp':
            s='#include "scene_capture.h"\n'+s
            s=replace(s,'        const auto& option=schema[id];','        CaneCaptureOption(id,s);\n        const auto& option=schema[id];')
        elif path.name=='BrxWalker.h':
            s=replace(s,'        std::uint8_t shaderKind{};','        std::uint8_t shaderKind{};\n        std::uint16_t shaderIndex{};')
        elif path.name=='AssetWalker.cpp':
            s='#include "scene_capture.h"\n'+s
            s=replace(s,'p.shaderKind=kind;','p.shaderKind=kind;p.shaderIndex=si;')
            target='for(auto& t:pendingTextures_){const bool allowBc='
            hook='''for(auto& t:pendingTextures_){
 if(!t.bitmaps.empty()&&!t.cluts.empty()&&t.bitmaps[0]<sourceBitmaps_.size()) {
  auto ci=t.cluts[t.shaderKind==ShdkThreeWay&&t.cluts.size()>1?1:0];
  if(ci<sourceCluts_.size()) { auto bi=t.bitmaps[0];auto& b=sourceBitmaps_[bi];CaneCaptureTexture(t.shaderIndex,decode(bi,ci,false),b.width,b.height,t.flags); }
 }
 const bool allowBc='''
            s=replace(s,target,hook)
        elif path.name=='BrxWalker.cpp':
            s='#include "scene_capture.h"\n'+s
            s=replace(s,'    const auto outputPath = WriteRebuiltFile(path);','    const auto outputPath = WriteRebuiltFile(path);\n    CaneWriteScene();')
        dest=OUT/path.name
        if not dest.exists() or dest.read_text()!=s:dest.write_text(s)
    sources=list(OUT.glob('*.cpp'))+[PLATFORM/'scene_capture.cpp']
    dependencies=list(OUT.glob('*.h'))+list(OUT.glob('*.inc'))+list(PLATFORM.glob('*.h'))
    def compile_one(source):
        obj=OUT/(source.stem+'.o')
        if not obj.exists() or obj.stat().st_mtime<max(p.stat().st_mtime for p in [source,*dependencies]):
            subprocess.run(['g++','-std=c++20','-O2','-Wall','-Wno-misleading-indentation','-DGLM_FORCE_PURE','-I'+str(OUT),'-I'+str(PLATFORM),'-I'+str(ROOT/'Libraries/include'),'-c',str(source),'-o',str(obj)],check=True)
        return obj
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:objects=list(pool.map(compile_one,sources))
    exe=OUT/'SlySceneExporter'
    if not exe.exists() or exe.stat().st_mtime<max(p.stat().st_mtime for p in objects):subprocess.run(['g++',*[str(p) for p in objects],'-o',str(exe)],check=True)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    subprocess.run([str(exe),str(args.raw.resolve()),'ntsc'],env={**os.environ,'CANE_SCENE_OUTPUT':str(args.output.resolve())},check=True)
if __name__=='__main__':main()
