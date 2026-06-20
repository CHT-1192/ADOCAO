#include "TileGeometry.hpp"

Scratch g_sc;

void pushType(std::vector<float>& c, float type, int n) {
    for (int i = 0; i < n; i++) c.push_back(type);
}

void createCircle(float cx, float cy, float radius, float type,
                  Scratch& sc, int res)
{
    unsigned ci = (unsigned)sc.verts.size() / 3;
    sc.verts.insert(sc.verts.end(), {cx, cy, 0.0f});
    sc.types.push_back(type);
    for (int i = 0; i < res; i++) {
        float a = (2.0f * 3.14159265f * i) / res;
        sc.verts.insert(sc.verts.end(), {std::cos(a)*radius + cx, std::sin(a)*radius + cy, 0.0f});
        sc.types.push_back(type);
    }
    for (int i = 1; i < res; i++)
        sc.indices.insert(sc.indices.end(), {ci, ci+(unsigned)i, ci+(unsigned)(i+1)});
    sc.indices.insert(sc.indices.end(), {ci, ci+(unsigned)res, ci+1});
}

void createMidSpinMesh(float angle, Scratch& sc) {
    // Match original game FloorMesh.GetPositions zeroAngle branch:
    // outer polygon = [startMore, 3 arc points, endLess] = 5 vertices
    constexpr float PI = 3.14159265f;
    float m1 = std::cos(angle * PI / 180.0f), m2 = std::sin(angle * PI / 180.0f);
    float rad = TILE_WIDTH;  // arc radius (= cornerCenterRadius = width in original)

    // startMore / endLess: at length along angle, offset +/-width perpendicular
    float len = TILE_WIDTH * 0.5f;  // shortened body (original: length *= 0.667)
    float smx = len * m1 + rad * m2, smy = len * m2 - rad * m1;  // startMore
    float elx = len * m1 - rad * m2, ely = len * m2 + rad * m1;  // endLess

    // 3 arc points spanning 180°: from angle+90° to angle-90°
    float arcStart = angle * PI / 180.0f + PI / 2.0f;
    float arcEnd   = angle * PI / 180.0f - PI / 2.0f;
    float arcPts[6];  // 3 points * (x,y)
    for (int i = 0; i < 3; i++) {
        float a = arcStart + (arcEnd - arcStart) * (float)i / 2.0f;
        arcPts[i * 2]     = std::cos(a) * rad;
        arcPts[i * 2 + 1] = std::sin(a) * rad;
    }

    float arco[6];  // outer arc (stroke radius)
    float arci[6];  // inner arc (fill radius = rad - OUTLINE*2)
    float rOut = rad + OUTLINE, rIn = rad - OUTLINE;
    float lo   = len + OUTLINE, li   = len - OUTLINE;
    for (int i = 0; i < 3; i++) {
        float a = arcStart + (arcEnd - arcStart) * (float)i / 2.0f;
        arco[i * 2]     = std::cos(a) * rOut;
        arco[i * 2 + 1] = std::sin(a) * rOut;
        arci[i * 2]     = std::cos(a) * rIn;
        arci[i * 2 + 1] = std::sin(a) * rIn;
    }

    // Stroke (type=0.0): [startMore, 3 arc points, endLess]
    unsigned b = (unsigned)sc.verts.size() / 3;
    sc.verts.insert(sc.verts.end(), {
        lo * m1 + rOut * m2, lo * m2 - rOut * m1, 0,  // startMore
        arco[0], arco[1], 0, arco[2], arco[3], 0, arco[4], arco[5], 0,
        lo * m1 - rOut * m2, lo * m2 + rOut * m1, 0,  // endLess
    });
    pushType(sc.types, 0.0f, 5);
    sc.indices.insert(sc.indices.end(), {b, b + 1, b + 4, b + 1, b + 2, b + 4,
                                         b + 2, b + 3, b + 4});

    // Fill (type=1.0): same structure, inset
    unsigned b2 = (unsigned)sc.verts.size() / 3;
    sc.verts.insert(sc.verts.end(), {
        li * m1 + rIn * m2, li * m2 - rIn * m1, 0,  // startMore (fill)
        arci[0], arci[1], 0, arci[2], arci[3], 0, arci[4], arci[5], 0,
        li * m1 - rIn * m2, li * m2 + rIn * m1, 0,  // endLess (fill)
    });
    pushType(sc.types, 1.0f, 5);
    sc.indices.insert(sc.indices.end(), {b2, b2 + 1, b2 + 4, b2 + 1, b2 + 2, b2 + 4,
                                         b2 + 2, b2 + 3, b2 + 4});
}

void createTileMesh(float startAngle, float endAngle, Scratch& sc) {
    float width = TILE_WIDTH, length = TILE_LENGTH;
    float m11=std::cos(startAngle*3.14159265f/180), m12=std::sin(startAngle*3.14159265f/180);
    float m21=std::cos(endAngle*3.14159265f/180),   m22=std::sin(endAngle*3.14159265f/180);

    float a0,a1;
    if (fmodWrap(startAngle-endAngle,360) >= fmodWrap(endAngle-startAngle,360))
        {a0=fmodWrap(startAngle,360)*3.14159265f/180;a1=a0+fmodWrap(endAngle-startAngle,360)*3.14159265f/180;}
    else
        {a0=fmodWrap(endAngle,360)*3.14159265f/180;a1=a0+fmodWrap(startAngle-endAngle,360)*3.14159265f/180;}
    float ang=a1-a0,mid=a0+ang/2;

    if (ang < 2.0943952f && ang > 0) {
        float x;
        if (ang<0.08726646f) x=1;
        else if (ang<0.5235988f) x=lerp(1,0.83f,std::pow((ang-0.08726646f)/0.43633235f,0.5f));
        else if (ang<0.7853982f) x=lerp(0.83f,0.77f,std::pow((ang-0.5235988f)/0.2617994f,1));
        else if (ang<1.5707964f) x=lerp(0.77f,0.15f,std::pow((ang-0.7853982f)/0.7853982f,0.7f));
        else x=lerp(0.15f,0,std::pow((ang-1.5707964f)/0.5235988f,0.5f));

        float dist,rad;
        if (x==1){dist=0;rad=width;}else{rad=lerp(0,width,x);dist=(width-rad)/std::sin(ang/2);}
        float cx=-dist*std::cos(mid),cy=-dist*std::sin(mid);

        // Outer (black) - accumulate outline
        width += OUTLINE; length += OUTLINE; rad += OUTLINE;

        createCircle(cx,cy,rad, 0.0f, sc);
        unsigned cnt;

        cnt=(unsigned)sc.verts.size()/3;
        sc.verts.insert(sc.verts.end(),{
            -rad*std::sin(a1)+cx,rad*std::cos(a1)+cy,0,cx,cy,0,
            rad*std::sin(a0)+cx,-rad*std::cos(a0)+cy,0,width*std::sin(a0),-width*std::cos(a0),0,
            0,0,0,-width*std::sin(a1),width*std::cos(a1),0,
        });
        pushType(sc.types, 0.0f,6);
        sc.indices.insert(sc.indices.end(),{cnt,cnt+1,cnt+5,cnt+4,cnt+1,cnt+5,cnt+2,cnt+3,cnt+4,cnt+1,cnt+3,cnt+4});

        cnt=(unsigned)sc.verts.size()/3;
        sc.verts.insert(sc.verts.end(),{
            length*m11+width*m12,length*m12-width*m11,0,length*m11-width*m12,length*m12+width*m11,0,
            -width*m12,width*m11,0,width*m12,-width*m11,0,
            length*m21+width*m22,length*m22-width*m21,0,length*m21-width*m22,length*m22+width*m21,0,
            -width*m22,width*m21,0,width*m22,-width*m21,0,
        });
        pushType(sc.types, 0.0f,8);
        sc.indices.insert(sc.indices.end(),{cnt,cnt+1,cnt+2,cnt+2,cnt+3,cnt,cnt+4,cnt+5,cnt+6,cnt+6,cnt+7,cnt+4});

        // Inner (white) - shrink from accumulated by 2*OUTLINE
        width -= OUTLINE*2; length -= OUTLINE*2; rad -= OUTLINE*2;
        if (rad<0){rad=0;cx=(-width/std::sin(ang/2))*std::cos(mid);cy=(-width/std::sin(ang/2))*std::sin(mid);}
        createCircle(cx,cy,rad, 1.0f, sc);

        cnt=(unsigned)sc.verts.size()/3;
        sc.verts.insert(sc.verts.end(),{
            -rad*std::sin(a1)+cx,rad*std::cos(a1)+cy,0,cx,cy,0,
            rad*std::sin(a0)+cx,-rad*std::cos(a0)+cy,0,width*std::sin(a0),-width*std::cos(a0),0,
            0,0,0,-width*std::sin(a1),width*std::cos(a1),0,
        });
        pushType(sc.types, 1.0f,6);
        sc.indices.insert(sc.indices.end(),{cnt,cnt+1,cnt+5,cnt+4,cnt+1,cnt+5,cnt+2,cnt+3,cnt+4,cnt+1,cnt+3,cnt+4});

        cnt=(unsigned)sc.verts.size()/3;
        sc.verts.insert(sc.verts.end(),{
            length*m11+width*m12,length*m12-width*m11,0,length*m11-width*m12,length*m12+width*m11,0,
            -width*m12,width*m11,0,width*m12,-width*m11,0,
            length*m21+width*m22,length*m22-width*m21,0,length*m21-width*m22,length*m22+width*m21,0,
            -width*m22,width*m21,0,width*m22,-width*m21,0,
        });
        pushType(sc.types, 1.0f,8);
        sc.indices.insert(sc.indices.end(),{cnt,cnt+1,cnt+2,cnt+2,cnt+3,cnt,cnt+4,cnt+5,cnt+6,cnt+6,cnt+7,cnt+4});

    } else if (ang > 0) {
        // Outer (black) - accumulate outline
        width += OUTLINE; length += OUTLINE;
        float cx=(-width/std::sin(ang/2))*std::cos(mid),cy=(-width/std::sin(ang/2))*std::sin(mid);
        unsigned cnt;

        cnt=(unsigned)sc.verts.size()/3;
        sc.verts.insert(sc.verts.end(),{cx,cy,0,width*std::sin(a0),-width*std::cos(a0),0,0,0,0,-width*std::sin(a1),width*std::cos(a1),0});
        pushType(sc.types, 0.0f,4);
        sc.indices.insert(sc.indices.end(),{cnt,cnt+1,cnt+2,cnt+2,cnt+3,cnt});

        cnt=(unsigned)sc.verts.size()/3;
        sc.verts.insert(sc.verts.end(),{
            length*m11+width*m12,length*m12-width*m11,0,length*m11-width*m12,length*m12+width*m11,0,
            -width*m12,width*m11,0,width*m12,-width*m11,0,
            length*m21+width*m22,length*m22-width*m21,0,length*m21-width*m22,length*m22+width*m21,0,
            -width*m22,width*m21,0,width*m22,-width*m21,0,
        });
        pushType(sc.types, 0.0f,8);
        sc.indices.insert(sc.indices.end(),{cnt,cnt+1,cnt+2,cnt+2,cnt+3,cnt,cnt+4,cnt+5,cnt+6,cnt+6,cnt+7,cnt+4});

        // Inner (white) - shrink from accumulated by 2*OUTLINE
        width -= OUTLINE*2; length -= OUTLINE*2;
        cx=(-width/std::sin(ang/2))*std::cos(mid);cy=(-width/std::sin(ang/2))*std::sin(mid);

        cnt=(unsigned)sc.verts.size()/3;
        sc.verts.insert(sc.verts.end(),{cx,cy,0,width*std::sin(a0),-width*std::cos(a0),0,0,0,0,-width*std::sin(a1),width*std::cos(a1),0});
        pushType(sc.types, 1.0f,4);
        sc.indices.insert(sc.indices.end(),{cnt,cnt+1,cnt+2,cnt+2,cnt+3,cnt});

        cnt=(unsigned)sc.verts.size()/3;
        sc.verts.insert(sc.verts.end(),{
            length*m11+width*m12,length*m12-width*m11,0,length*m11-width*m12,length*m12+width*m11,0,
            -width*m12,width*m11,0,width*m12,-width*m11,0,
            length*m21+width*m22,length*m22-width*m21,0,length*m21-width*m22,length*m22+width*m21,0,
            -width*m22,width*m21,0,width*m22,-width*m21,0,
        });
        pushType(sc.types, 1.0f,8);
        sc.indices.insert(sc.indices.end(),{cnt,cnt+1,cnt+2,cnt+2,cnt+3,cnt,cnt+4,cnt+5,cnt+6,cnt+6,cnt+7,cnt+4});

    } else {
        length=width; width+=OUTLINE; length+=OUTLINE;
        float m1=m11,m2=m12,mx=-m1*0.04f,my=-m2*0.04f;
        createCircle(mx,my,width, 0.0f, sc);
        unsigned cnt=(unsigned)sc.verts.size()/3;
        sc.verts.insert(sc.verts.end(),{
            mx+length*m1+width*m2,my+length*m2-width*m1,0,mx+length*m1-width*m2,my+length*m2+width*m1,0,
            mx-width*m2,my+width*m1,0,mx+width*m2,my-width*m1,0,
        });
        pushType(sc.types, 0.0f,4);
        sc.indices.insert(sc.indices.end(),{cnt,cnt+1,cnt+2,cnt+2,cnt+3,cnt});

        width-=OUTLINE*2; length-=OUTLINE*2;
        createCircle(mx,my,width, 1.0f, sc);
        cnt=(unsigned)sc.verts.size()/3;
        sc.verts.insert(sc.verts.end(),{
            mx+length*m1+width*m2,my+length*m2-width*m1,0,mx+length*m1-width*m2,my+length*m2+width*m1,0,
            mx-width*m2,my+width*m1,0,mx+width*m2,my-width*m1,0,
        });
        pushType(sc.types, 1.0f,4);
        sc.indices.insert(sc.indices.end(),{cnt,cnt+1,cnt+2,cnt+2,cnt+3,cnt});
    }
}
