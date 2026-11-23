// Original binocular meshes and animated TV bands, submitted as native HUD
// triangles. No desktop GL draw calls or replacement artwork are involved.
static std::vector<glm::vec2> caneBinocPositions[2];
static std::vector<uint16_t> caneBinocIndices[2];
static unsigned CaneClipPolyPlane(const BLOTLINEVERTEX* in,unsigned inCount,BLOTLINEVERTEX* out,glm::vec3 plane) {
    if(!inCount)return 0;
    auto distance=[&](const BLOTLINEVERTEX& v){return v.position.x*plane.x+v.position.y*plane.y+plane.z;};
    const BLOTLINEVERTEX* previous=&in[inCount-1];float pd=distance(*previous);
    unsigned outCount=0;
    for(unsigned i=0;i<inCount;++i) {
        const BLOTLINEVERTEX& current=in[i];
        float cd=distance(current);
        if((pd>=0)!=(cd>=0) && outCount<16) {
            float t=pd/(pd-cd);BLOTLINEVERTEX& v=out[outCount++];v={};
            v.position=glm::mix(previous->position,current.position,t);
            v.uv=glm::mix(previous->uv,current.uv,t);v.color=glm::mix(previous->color,current.color,t);
        }
        if(cd>=0 && outCount<16)out[outCount++]=current;
        previous=&current;pd=cd;
    }
    return outCount;
}
static void CaneFan(std::vector<BLOTLINEVERTEX>& out,const BLOTLINEVERTEX* polygon,unsigned count) {
    for(unsigned i=1;i+1<count;++i) {out.push_back(polygon[0]);out.push_back(polygon[i]);out.push_back(polygon[i+1]);}
}
// Cheap triangle test against half-planes (kept side is distance>=0).
// 0: wholly outside one plane (nothing visible), 1: wholly inside every plane
// (emit unchanged), 2: straddles a boundary and needs the exact polygon clip.
static int CaneClassifyTri(const BLOTLINEVERTEX* t,const glm::vec3* planes,unsigned count) {
    bool inside=true;
    for(unsigned p=0;p<count;++p) {
        const glm::vec3& pl=planes[p];
        float d0=t[0].position.x*pl.x+t[0].position.y*pl.y+pl.z;
        float d1=t[1].position.x*pl.x+t[1].position.y*pl.y+pl.z;
        float d2=t[2].position.x*pl.x+t[2].position.y*pl.y+pl.z;
        float mx=std::max(d0,std::max(d1,d2)),mn=std::min(d0,std::min(d1,d2));
        if(mx<1e-5f)return 0;
        if(mn<0)inside=false;
    }
    return inside?1:2;
}
static std::vector<BLOTLINEVERTEX> CaneClipAgainstPlanes(const std::vector<BLOTLINEVERTEX>& input,const glm::vec3* planes,unsigned planeCount) {
    std::vector<BLOTLINEVERTEX> out;out.reserve(input.size());
    for(size_t i=0;i+2<input.size();i+=3) {
        int c=CaneClassifyTri(&input[i],planes,planeCount);
        if(c==0)continue;
        if(c==1) {out.push_back(input[i]);out.push_back(input[i+1]);out.push_back(input[i+2]);continue;}
        BLOTLINEVERTEX bufA[16],bufB[16];
        bufA[0]=input[i];bufA[1]=input[i+1];bufA[2]=input[i+2];
        BLOTLINEVERTEX* src=bufA,*dst=bufB;unsigned n=3;
        for(unsigned p=0;p<planeCount && n;++p) {n=CaneClipPolyPlane(src,n,dst,planes[p]);std::swap(src,dst);}
        CaneFan(out,src,n);
    }
    return out;
}
static std::vector<BLOTLINEVERTEX> CaneClipUi(const std::vector<BLOTLINEVERTEX>& input) {
    const float s=g_gl.uiScale;const auto o=g_gl.uiOrigin;
    // Original triangular compass aperture: (273,9), (367,9), (320,44).
    const glm::vec3 planes[3]={glm::vec3(0,1,-o.y-9*s),
        glm::vec3(35,-47,-35*(o.x+273*s)+47*(o.y+9*s)),
        glm::vec3(-35,-47,35*(o.x+367*s)+47*(o.y+9*s))};
    return CaneClipAgainstPlanes(input,planes,3);
}
static std::vector<BLOTLINEVERTEX> CaneClipRectUi(const std::vector<BLOTLINEVERTEX>& input) {
    const glm::vec3 planes[4]={glm::vec3(1,0,-caneClipRect.x),glm::vec3(0,1,-caneClipRect.y),
        glm::vec3(-1,0,caneClipRect.z),glm::vec3(0,-1,caneClipRect.w)};
    return CaneClipAgainstPlanes(input,planes,4);
}
struct CaneBinocOutVert {glm::vec3 pos;uint16_t i0,i1,i2;float w0,w1,w2;};
static std::vector<BLOTLINEVERTEX> caneBinocBgCache;
static bool caneBinocBgValid=false;
static float caneBinocBgScale=0;static glm::vec2 caneBinocBgOrigin(0);static glm::vec4 caneBinocBgColor(0);
static std::vector<CaneBinocOutVert> caneBinocOutCache;
static std::vector<BLOTLINEVERTEX> caneBinocOutVerts;
static bool caneBinocOutValid=false;
static float caneBinocOutScale=0;static glm::vec2 caneBinocOutOrigin(0);
void CaneCompassClipNative(bool enabled) {caneCompassClip=enabled;}
void CaneRectClipNative(bool enabled,float x0,float y0,float x1,float y1) {caneRectClip=enabled;caneClipRect=glm::vec4(x0,y0,x1,y1);}
void CaneBinocMeshNative(bool outline,const void* data,size_t count,const uint16_t* indices,size_t indexCount) {
    const float* v=static_cast<const float*>(data);auto& points=caneBinocPositions[outline];points.clear();
    for(size_t i=0;i<count;++i)points.emplace_back(v[i*4],v[i*4+1]);
    caneBinocIndices[outline].assign(indices,indices+indexCount);
    if(outline) {caneBinocOutCache.clear();caneBinocOutValid=false;}
    else {caneBinocBgCache.clear();caneBinocBgValid=false;}
}
void CaneBinocMaskNative(bool outline,const glm::vec4* colors) {
    const auto& positions=caneBinocPositions[outline];const auto& indices=caneBinocIndices[outline];
    const float s=g_gl.uiScale,origin=200-320*s;
    const auto uiOrig=g_gl.uiOrigin;
    const float joins[4]={0,origin+240*s,origin+400*s,400};
    // Region half-planes along x: kept side is (x-lo)>=0 and (hi-x)>=0.
    static const glm::vec3 regionPlanes[3][2]={
        {glm::vec3(1,0,10000.f),glm::vec3(-1,0,240.f)},
        {glm::vec3(1,0,-240.f),glm::vec3(-1,0,400.f)},
        {glm::vec3(1,0,-400.f),glm::vec3(-1,0,10000.f)}};
    auto place=[&](BLOTLINEVERTEX v,unsigned region) {
        float x=v.position.x;
        v.position.x=region==0?x*joins[1]/240:region==1?origin+x*s:joins[2]+(x-400)*(400-joins[2])/240;
        v.position.y=uiOrig.y+v.position.y*s;
        return v;
    };
    if(!outline) {
        if(!caneBinocBgValid || caneBinocBgScale!=s || caneBinocBgOrigin!=uiOrig || caneBinocBgColor!=colors[0]) {
            caneBinocBgCache.clear();caneBinocBgCache.reserve(indices.size()*2);
            for(size_t i=0;i+2<indices.size();i+=3) {
                BLOTLINEVERTEX tri[3]{};
                for(unsigned j=0;j<3;++j) {
                    unsigned index=indices[i+j];if(index>=positions.size())throw std::runtime_error("Binocular index out of bounds");
                    tri[j].position=glm::vec3(positions[index],0);tri[j].color=colors[0];tri[j].uv=glm::vec2(0);
                }
                // Stretch only the outer wings; keep the centre's authored proportions.
                for(unsigned region=0;region<3;++region) {
                    int c=CaneClassifyTri(tri,regionPlanes[region],2);
                    if(c==0)continue;
                    if(c==1) {for(unsigned j=0;j<3;++j)caneBinocBgCache.push_back(place(tri[j],region));continue;}
                    BLOTLINEVERTEX bufA[16],bufB[16];
                    bufA[0]=tri[0];bufA[1]=tri[1];bufA[2]=tri[2];
                    unsigned n=CaneClipPolyPlane(bufA,3,bufB,regionPlanes[region][0]);
                    n=CaneClipPolyPlane(bufB,n,bufA,regionPlanes[region][1]);
                    for(unsigned k=0;k<n;++k)bufA[k]=place(bufA[k],region);
                    CaneFan(caneBinocBgCache,bufA,n);
                }
            }
            caneBinocBgScale=s;caneBinocBgOrigin=uiOrig;caneBinocBgColor=colors[0];caneBinocBgValid=true;
        }
        CaneGlyphsNative(caneBinocBgCache,0);
        return;
    }
    if(!caneBinocOutValid || caneBinocOutScale!=s || caneBinocOutOrigin!=uiOrig) {
        caneBinocOutCache.clear();caneBinocOutCache.reserve(indices.size()*2);
        for(size_t i=0;i+2<indices.size();i+=3) {
            uint16_t idx[3]{};BLOTLINEVERTEX tri[3]{};
            for(unsigned j=0;j<3;++j) {
                unsigned index=indices[i+j];if(index>=positions.size())throw std::runtime_error("Binocular index out of bounds");
                idx[j]=uint16_t(index);
                tri[j].position=glm::vec3(positions[index],0);
                tri[j].color=glm::vec4(j==0?1.f:0.f,j==1?1.f:0.f,j==2?1.f:0.f,0.f);
            }
            for(unsigned region=0;region<3;++region) {
                int c=CaneClassifyTri(tri,regionPlanes[region],2);
                if(c==0)continue;
                BLOTLINEVERTEX bufA[16],bufB[16];unsigned n=3;
                bufA[0]=tri[0];bufA[1]=tri[1];bufA[2]=tri[2];
                if(c==2) {
                    n=CaneClipPolyPlane(bufA,3,bufB,regionPlanes[region][0]);
                    n=CaneClipPolyPlane(bufB,n,bufA,regionPlanes[region][1]);
                }
                for(unsigned k=0;k<n;++k)bufA[k]=place(bufA[k],region);
                for(unsigned k=1;k+1<n;++k)for(unsigned p:{0u,k,k+1}) {
                    const auto& v=bufA[p];
                    caneBinocOutCache.push_back({v.position,idx[0],idx[1],idx[2],v.color.r,v.color.g,v.color.b});
                }
            }
        }
        caneBinocOutScale=s;caneBinocOutOrigin=uiOrig;caneBinocOutValid=true;
    }
    caneBinocOutVerts.resize(caneBinocOutCache.size());
    for(size_t i=0;i<caneBinocOutCache.size();++i) {
        const auto& c=caneBinocOutCache[i];auto& v=caneBinocOutVerts[i];
        v.position=c.pos;v.uv=glm::vec2(0);
        v.color=colors[c.i0]*c.w0+colors[c.i1]*c.w1+colors[c.i2]*c.w2;
    }
    CaneGlyphsNative(caneBinocOutVerts,0);
}
void CaneColorTrianglesNative(const float* values,size_t count,const glm::mat4& model) {
    std::vector<BLOTLINEVERTEX> vertices(count);
    for(size_t i=0;i<count;++i) {
        auto& v=vertices[i];v.position=glm::vec3(model*glm::vec4(values[i*6],values[i*6+1],0,1));
        v.uv=glm::vec2(0);v.color=glm::vec4(values[i*6+2],values[i*6+3],values[i*6+4],values[i*6+5]);
    }
    CaneGlyphsNative(vertices,0);
}
static void CaneUiQuad(std::vector<BLOTLINEVERTEX>& out,glm::vec2 a,glm::vec2 b,glm::vec2 c,glm::vec2 d,const glm::vec4& color) {
    for(auto p:{a,b,c,c,b,d}) {BLOTLINEVERTEX v{};v.position=glm::vec3(p,0);v.color=color;out.push_back(v);}
}
void CaneLineNative(glm::vec2 a,glm::vec2 b,const glm::vec4& color) {
    glm::vec2 delta=b-a;float length=glm::length(delta);if(length<.001f)return;
    glm::vec2 n=glm::vec2(-delta.y,delta.x)/length*.5f;
    std::vector<BLOTLINEVERTEX> out;CaneUiQuad(out,a-n,a+n,b-n,b+n,color);CaneGlyphsNative(out,0);
}
void CaneTvNative(TV* tv) {
    if(!tv || !tv->pspeakerDraw || g_binoc.binocs==BINOCS_Confront)return;
    int first,last;GetTvItvbMinMax(tv,&first,&last);if(first==last)return;
    const float s=tv->dy*.5f,x=tv->x+tv->xFlatBase,cx=tv->x+tv->xCurveCenter,cy=tv->y+s;
    std::vector<BLOTLINEVERTEX> aperture,bands,border;
    size_t nBands=size_t(last-first);
    aperture.reserve(nBands*6);bands.reserve(nBands*6);border.reserve((nBands+2)*6);
    float blend=std::clamp((g_clock.tReal-tv->tTvs)*5,0.f,1.f);if(tv->tvs==TVS_Talk)blend=1-blend;
    glm::vec4 edge=glm::mix(RGBA_LightBlue,RGBA_DarkBlue,blend);edge.a=1;
    for(int i=first;i<last;++i) {
        const auto& band=tv->atvb[i];float top=cy+band.yTop*s,bottom=cy+band.yBottom*s;
        float xt=cx+band.xCurveTop*s,xb=cx+band.xCurveBottom*s;
        CaneUiQuad(aperture,{x,top},{xt,top},{x,bottom},{xb,bottom},glm::vec4(.025f,.035f,.05f,1));
        glm::vec4 color=glm::mix(band.rgbaCur,glm::vec4(.502f,.502f,.502f,1),band.uDistort);
        CaneUiQuad(bands,{x,top},{xt,top},{x,bottom},{xb,bottom},color);
        float ds=tv->dsOutline;
        CaneUiQuad(border,{xt,top},{cx+band.xCurveTop*(s+ds),cy+band.yTop*(s+ds)},
                          {xb,bottom},{cx+band.xCurveBottom*(s+ds),cy+band.yBottom*(s+ds)},edge);
        if(i==first)CaneUiQuad(border,{x,top-ds},{xt,top-ds},{x,top},{xt,top},edge);
        if(i+1==last)CaneUiQuad(border,{x,bottom},{xb,bottom},{x,bottom+ds},{xb,bottom+ds},edge);
    }
    const auto oldLayer=caneHudLayer;TV* oldPortrait=canePortraitContext;
    if(caneBottomActive) {caneHudLayer=CaneHudLayer::Dialogue;canePortraitContext=tv;}
    caneHud.push_back({std::move(aperture),0,tv,false,caneHudLayer,canePortraitContext});
    CaneGlyphsNative(bands,0);CaneGlyphsNative(border,0);
    caneHudLayer=oldLayer;canePortraitContext=oldPortrait;
}
void CaneWipeNative() {
    if(!g_psw || g_wipe.wipes==WIPES_Idle)return;
    const float screenWidth=g_gl.width,screenHeight=g_gl.height;
    const float halfWidth=screenWidth*.5f,halfHeight=screenHeight*.5f;
    if(g_wipe.wipek==WIPEK_Keyhole && g_pkeyhole && g_pkeyhole->dx>0.0f) {
        const float uClosed=glm::clamp(g_wipe.uBlack,0.0f,1.0f);
        const float scaleOpen=(screenWidth*2.0f)/g_pkeyhole->dx;
        const float scaleClosed=4.0f/g_pkeyhole->dx;
        const float scale=glm::mix(scaleOpen,scaleClosed,uClosed);
        glm::vec2 center(halfWidth,halfHeight);
        PO* ppo=PpoCur();
        if(ppo && ppo->pvtpo && ppo->pvtpo->pfnJthsCurrentPo &&
           ppo->pvtpo->pfnJthsCurrentPo(ppo)==JTHS_Dead && g_pcm) {
            glm::vec3 posScreen{};
            ConvertCmWorldToScreen(g_pcm,&ppo->xf.posWorld,&posScreen);
            if(std::isfinite(posScreen.x) && std::isfinite(posScreen.y)) {
                const float xTarget=glm::clamp(posScreen.x*halfWidth+halfWidth,10.0f,screenWidth-10.0f);
                const float yTarget=glm::clamp(-posScreen.y*halfHeight+halfHeight,16.0f,screenHeight-16.0f);
                center.x=glm::mix(halfWidth,xTarget,uClosed);
                center.y=glm::mix(halfHeight,yTarget,uClosed);
            }
        }
        const float revealAlpha=glm::clamp(uClosed*2.5f,0.0f,1.0f);
        glm::vec4 backgroundColor(0.0f);
        size_t triCount=0;
        for(int ikpks=0;ikpks<5;++ikpks)if(g_pkeyhole->mpkpks[ikpks].ctri>0)triCount+=size_t(g_pkeyhole->mpkpks[ikpks].ctri);
        std::vector<BLOTLINEVERTEX> verts;
        verts.reserve(triCount*3+30);
        for(int ikpks=4;ikpks>=0;--ikpks) {
            const KS& ks=g_pkeyhole->mpkpks[ikpks];
            glm::vec4 color=ks.rgba;
            if(ikpks==0) {
                color.r*=1.0f-uClosed;
                color.g*=1.0f-uClosed;
                color.b*=1.0f-uClosed;
                color.a=glm::mix(color.a,1.0f,uClosed);
            }
            color.a*=revealAlpha;
            if(ikpks==0)backgroundColor=color;
            if(ks.ctri<=0 || color.a<=0.0f)continue;
            const int nTri=std::min<int>(ks.ctri,int(ks.atri.size()));
            for(int t=0;t<nTri;++t) {
                const TRI& tri=ks.atri[t];
                if(tri.aipos[0]<0 || tri.aipos[1]<0 || tri.aipos[2]<0 ||
                   size_t(tri.aipos[0])>=g_pkeyhole->apos.size() ||
                   size_t(tri.aipos[1])>=g_pkeyhole->apos.size() ||
                   size_t(tri.aipos[2])>=g_pkeyhole->apos.size())continue;
                for(int k=0;k<3;++k) {
                    BLOTLINEVERTEX v{};
                    v.position=glm::vec3(center+glm::vec2(g_pkeyhole->apos[tri.aipos[k]])*scale,0.0f);
                    v.color=color;
                    verts.push_back(v);
                }
            }
        }
        const glm::vec2 tMin=center+glm::vec2(g_pkeyhole->posMin.x,g_pkeyhole->posMin.y)*scale;
        const glm::vec2 tMax=center+glm::vec2(g_pkeyhole->posMax.x,g_pkeyhole->posMax.y)*scale;
        const float xMin=glm::clamp(glm::min(tMin.x,tMax.x),0.0f,screenWidth);
        const float yMin=glm::clamp(glm::min(tMin.y,tMax.y),0.0f,screenHeight);
        const float xMax=glm::clamp(glm::max(tMin.x,tMax.x),0.0f,screenWidth);
        const float yMax=glm::clamp(glm::max(tMin.y,tMax.y),0.0f,screenHeight);
        if(xMax>0.0f && xMin<screenWidth && yMax>0.0f && yMin<screenHeight && backgroundColor.a>0.0f) {
            auto addRect=[&](float x0,float y0,float x1,float y1,const glm::vec4& c) {
                if(x1>x0 && y1>y0 && c.a>0.0f)CaneUiQuad(verts,{x0,y0},{x1,y0},{x0,y1},{x1,y1},c);
            };
            addRect(0.0f,0.0f,screenWidth,yMin,backgroundColor);
            addRect(0.0f,yMax,screenWidth,screenHeight,backgroundColor);
            addRect(0.0f,yMin,xMin,yMax,backgroundColor);
            addRect(xMax,yMin,screenWidth,yMax,backgroundColor);
            const float finalCloseAlpha=glm::clamp((uClosed-0.9f)*10.0f,0.0f,1.0f);
            if(finalCloseAlpha>0.0f) {
                glm::vec4 closeColor=backgroundColor;
                closeColor.a*=finalCloseAlpha;
                addRect(xMin,yMin,xMax,yMax,closeColor);
            }
        }
        if(!verts.empty())CaneGlyphsNative(verts,0);
        return;
    }
    if(g_wipe.wipek!=WIPEK_Keyhole && g_wipe.wipek!=WIPEK_Fade)return;
    const float alpha=glm::clamp(g_wipe.uBlack,0.0f,1.0f);
    if(alpha>0.0f)CaneQuadNative(0.0f,0.0f,screenWidth,screenHeight,0,glm::vec4(0.0f,0.0f,0.0f,alpha));
}
