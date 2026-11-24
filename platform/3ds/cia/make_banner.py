#!/usr/bin/env python3
"""Build original extruded text and a looping 3D CGFX HOME Menu banner.

Requires matplotlib, numpy, shapely, mapbox-earcut, gltflib and pycgfx (MIT).
No game model, texture or animation data is used.
"""
from pathlib import Path
import argparse, base64, importlib.util, json, math, sys
import numpy as np
from matplotlib.textpath import TextPath
from matplotlib.font_manager import FontProperties
from shapely.geometry import Polygon, MultiPolygon
from shapely.geometry.polygon import orient
import mapbox_earcut

ROOT=Path(__file__).resolve().parents[3]

def polygons(text,font,width,center_y):
    shapes=None
    for contour in TextPath((0,0),text,size=10,prop=FontProperties(fname=font)).to_polygons():
        p=Polygon(contour)
        if not p.is_valid:p=p.buffer(0)
        shapes=p if shapes is None else shapes.symmetric_difference(p)
    from shapely.affinity import scale,translate
    lo,bot,hi,top=shapes.bounds;factor=width/(hi-lo)
    shapes=scale(shapes,factor,factor,origin=(0,0))
    shapes=translate(shapes,xoff=-(lo+hi)*factor/2,yoff=center_y-(bot+top)*factor/2)
    return shapes.simplify(.025,preserve_topology=True)

def geometry(shapes,back,front):
    vertices=[];normals=[];indices=[]
    def vertex(p,n):vertices.append(p);normals.append(n);return len(vertices)-1
    for polygon in ([shapes] if isinstance(shapes,Polygon) else list(shapes.geoms)):
        polygon=orient(polygon,sign=1)
        rings=[np.asarray(r.coords[:-1],dtype=np.float32) for r in [polygon.exterior,*polygon.interiors]]
        points=np.concatenate(rings);ends=np.cumsum([len(r) for r in rings],dtype=np.uint32)
        triangles=mapbox_earcut.triangulate_float32(points,ends).reshape(-1,3)
        front_ids=[vertex([float(x),float(y),front],[0,0,1]) for x,y in points]
        back_ids=[vertex([float(x),float(y),back],[0,0,-1]) for x,y in points]
        for triangle in triangles:
            a,b,c=map(int,triangle)
            pa,pb,pc=points[[a,b,c]]
            if (pb[0]-pa[0])*(pc[1]-pa[1])-(pb[1]-pa[1])*(pc[0]-pa[0])<0:b,c=c,b
            indices.extend([front_ids[a],front_ids[b],front_ids[c],back_ids[c],back_ids[b],back_ids[a]])
        for ring in rings:
            for a,b in zip(ring,np.roll(ring,-1,axis=0)):
                delta=b-a;length=np.linalg.norm(delta)
                if length<1e-6:continue
                normal=[float(delta[1]/length),float(-delta[0]/length),0]
                ids=[vertex([float(p[0]),float(p[1]),z],normal) for p,z in ((a,back),(b,back),(b,front),(a,front))]
                indices.extend([ids[0],ids[1],ids[2],ids[0],ids[2],ids[3]])
    return np.asarray(vertices,dtype='<f4'),np.asarray(normals,dtype='<f4'),np.asarray(indices,dtype='<u2')

def main():
    p=argparse.ArgumentParser();p.add_argument('--pycgfx',type=Path,default=ROOT/'local/cia-tools/pycgfx');p.add_argument('--gold-font',default='/usr/share/fonts/gsfonts/Z003-MediumItalic.otf');p.add_argument('--white-font',default='/usr/share/fonts/TTF/DejaVuSans-Bold.ttf');a=p.parse_args()
    out=ROOT/'build/3ds/cia';out.mkdir(parents=True,exist_ok=True)
    title=polygons('Thievius',a.gold_font,29,4.1);ctr=polygons('CTR',a.white_font,9,-4.0)
    meshes_data=[geometry(title.buffer(.055,join_style=1),-.48,.20),geometry(title,.21,.38),geometry(ctr.buffer(.065,join_style=1),-.48,.20),geometry(ctr,.21,.38)]
    blob=bytearray();views=[];accessors=[]
    def accessor(values,kind,component):
        while len(blob)%4:blob.append(0)
        at=len(blob);blob.extend(values.tobytes());vi=len(views);views.append({'buffer':0,'byteOffset':at,'byteLength':len(blob)-at})
        item={'bufferView':vi,'componentType':component,'count':len(values),'type':kind}
        if kind=='VEC3':item.update(min=values.min(axis=0).tolist(),max=values.max(axis=0).tolist())
        accessors.append(item);return len(accessors)-1
    primitives=[]
    for i,(positions,normals,indices) in enumerate(meshes_data):
        assert len(positions)<65536
        primitives.append({'attributes':{'POSITION':accessor(positions,'VEC3',5126),'NORMAL':accessor(normals,'VEC3',5126)},'indices':accessor(indices,'SCALAR',5123),'material':i})
    colors=[(.12,.07,.015,1),(1,.65,.08,1),(.025,.045,.08,1),(.96,.98,1,1)]
    model={'asset':{'version':'2.0','generator':'ThieviusCTR original extruded title'},'scene':0,'scenes':[{'nodes':[0,1]}],'nodes':[{'name':'TitleSpin','mesh':0},{'name':'StaticCTR','mesh':1}],'meshes':[{'name':'Thievius','primitives':primitives[:2]},{'name':'CTR','primitives':primitives[2:]}],'materials':[{'name':name,'pbrMetallicRoughness':{'baseColorFactor':list(color),'metallicFactor':.7 if i<2 else .1,'roughnessFactor':.28 if i<2 else .55}} for i,(name,color) in enumerate(zip(('Gold edge','Gold script','CTR edge','White CTR'),colors))],'buffers':[{'byteLength':len(blob),'uri':'data:application/octet-stream;base64,'+base64.b64encode(blob).decode()}],'bufferViews':views,'accessors':accessors}
    gltf_path=out/'banner.gltf';gltf_path.write_text(json.dumps(model,separators=(',',':')))
    sys.path.insert(0,str(a.pycgfx.resolve()))
    spec=importlib.util.spec_from_file_location('banner_converter',a.pycgfx/'main.py');converter=importlib.util.module_from_spec(spec);spec.loader.exec_module(converter)
    import gltflib
    cgfx=converter.convert_gltf(gltflib.GLTF.load(str(gltf_path),load_file_resources=True))
    # Use smooth skinning with one full-weight joint per vertex. The rigid path
    # did not play the intended independent animation on real HOME Menu.
    cmdl=cgfx.data.models['COMMON']
    from cgfx.primitives import VertexStream,VertexAttributeUsage
    from cgfx.sobj import BoneFlag
    bindings=[]
    for shape in cmdl.shapes.data.contents:
        target_bone='TitleSpin' if shape.name.startswith('Thievius_') else 'StaticCTR'
        bone_index=cmdl.skeleton.bones.get_index(target_bone)
        assert bone_index is not None
        for primitive in shape.primitive_sets.data.contents:
            primitive.related_bones.data.contents=[bone_index]
            primitive.skinning_mode=2  # Smooth; each vertex has one full-weight joint.
        # The index is local to the primitive palette, not the skeleton index.
        # Supply it explicitly rather than relying on a missing vertex attribute.
        index_attribute=VertexStream();index_attribute.format_type=0x1401
        index_attribute.usage=VertexAttributeUsage.BoneIndex
        index_attribute.components_count=1;index_attribute.scale=1.
        position_stream=shape.vertex_attributes.data.contents[0]
        vertex_count=len(position_stream.vertex_stream_data)//12
        index_attribute.vertex_stream_data=bytes(vertex_count)
        shape.vertex_attributes.add(index_attribute)
        weight_attribute=VertexStream()
        weight_attribute.usage=VertexAttributeUsage.BoneWeight
        weight_attribute.components_count=1;weight_attribute.scale=1.
        weight_attribute.vertex_stream_data=np.ones(vertex_count,dtype='<f4').tobytes()
        shape.vertex_attributes.add(weight_attribute)
        cmdl.skeleton.bones[target_bone].flags |= BoneFlag.HasSkinningMatrix
        bindings.append({'mesh':shape.name,'bone':target_bone,'index':bone_index})
    assert sum(b['bone']=='TitleSpin' for b in bindings)==2
    from cgfx.canm import CANM,CANMBoneTransform,FloatAnimationCurve,FloatSegment,InterpolationType,QuantizationType,StepLinear64Key
    class BannerBoneTransform(CANMBoneTransform):
        def refresh_struct(self):
            super().refresh_struct()
            # Euler rotation has no quaternion-W curve. Mark the reserved slot
            # absent; leaving its flag clear makes readers follow a null curve.
            self.flags |= 1 << 22
    animation=CANM();animation.name='COMMON';animation.target_animation_group_name='SkeletalAnimation';animation.frame_size=600.;animation.looping=True
    curve=FloatAnimationCurve();curve.start_frame=0.;curve.end_frame=600.
    segment=FloatSegment();segment.start_frame=0.;segment.end_frame=600.;segment.interpolation=InterpolationType.Linear;segment.quantization=QuantizationType.StepLinear64
    segment.keys=[StepLinear64Key(0.,0.),StepLinear64Key(600.,2*math.pi)]
    curve.segments=[segment]
    # Include every bone, in skeleton order, with complete transforms.
    for name in cmdl.skeleton.bones:
        bone=BannerBoneTransform();bone.bone_path=name
        bone.scale_x=bone.scale_y=bone.scale_z=1.
        bone.pos_x=bone.pos_y=bone.pos_z=0.
        bone.rot_x=bone.rot_z=0.;bone.rot_y=curve if name=='TitleSpin' else 0.
        animation.member_animations_data.add(name,bone)
    animated=cmdl.skeleton.bones['TitleSpin']
    animated.flags &= ~(BoneFlag.IsIdentity|BoneFlag.IsRotateZero)
    cmdl.animation_group_descriptions['SkeletalAnimation'].evalution_timing=0
    cgfx.data.skeletal_animations.add('COMMON',animation)
    data=converter.write(cgfx);assert data[:4]==b'CGFX' and len(data)<=0x80000 and b'CANM' in data
    # Check serialized palettes and vertex indices, not just source names.
    import struct
    for shape in cmdl.shapes.data.contents:
        expected=cmdl.skeleton.bones.get_index('TitleSpin' if shape.name.startswith('Thievius_') else 'StaticCTR')
        for primitive in shape.primitive_sets.data.contents:
            assert struct.unpack_from('<i',data,primitive.related_bones.data.offset)[0]==expected
            assert struct.unpack_from('<i',data,primitive.offset+8)[0]==2
        attr=shape.vertex_attributes.data.contents[-2]
        assert struct.unpack_from('<i',data,attr.offset+4)[0]==int(VertexAttributeUsage.BoneIndex)
        stream_field=attr.offset+20
        length,relative=struct.unpack_from('<ii',data,stream_field)
        at=stream_field+4+relative
        assert length>0 and data[at:at+length]==bytes(length)
        weights=shape.vertex_attributes.data.contents[-1]
        field=weights.offset+20
        weight_bytes,relative=struct.unpack_from('<ii',data,field)
        assert weight_bytes==length*4
        assert np.all(np.frombuffer(data,dtype='<f4',count=length,offset=field+4+relative)==1.)
    assert len(animation.member_animations_data.dict.nodes)==4
    for name in animation.member_animations_data:
        member=animation.member_animations_data[name]
        assert struct.unpack_from('<I',data,member.offset)[0] & (1 << 22), 'Reserved Euler rotation slot must be absent'
    target=ROOT/'platform/3ds/cia/banner.cgfx';target.write_bytes(data)
    # Keep the geometry for a reproducible renderer-independent preview.
    np.savez(out/'banner-mesh.npz',**{f'{key}{i}':v for i,mesh in enumerate(meshes_data) for key,v in zip(('p','n','i'),mesh)})
    report={'mesh_bindings':bindings,'cgfx_bytes':len(data),'triangles':sum(len(m[2])//3 for m in meshes_data),'vertices':sum(len(m[0]) for m in meshes_data),'loop_seconds':10,'rotation_degrees':360,'gold_text':'Thievius','white_text':'CTR','rotating_text':'Thievius','stationary_text':'CTR','pycgfx_commit':'6c2e0cd53a5f4c3265b9aa9a1f30806f8291116e'}
    (out/'banner-report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
if __name__=='__main__':main()
