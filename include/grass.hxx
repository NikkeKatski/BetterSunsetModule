#pragma once
#include <BetterSMS/module.hxx>

#include <raw_fn.hxx>
#include <rand.h>
#include <BetterSMS/debug.hxx>
#include <JDrama/JDRActor.hxx>
#include <MarioUtil/MathUtil.hxx>
#include <GX.h>
#include <settings.hxx>
#include <Map/MapCollisionData.hxx>

const u32 SunsetGrass[3]     = {0x7E8736ff, 0x216118ff, 0x0};
const u32 SunsetGrassDark[3] = {0x5C581Dff, 0x172502ff, 0x006100ff};
const u32 TwilightGrass[3]   = {0x316C3Eff, 0x105216ff, 0x0};

struct triangle {
public:
    float x;
    float y;
    float z;
};

struct trishort {
public:
    short x;
    short y;
    short z;
};

class grassObj {
public:
    u8 data[0x14];     // use 0x13 for something
    float grassFloor;  // 0x14
    u32 data2[0x14];   // 0x18 We can use 0x6 - 0x9
    u32 triCount;      // 0x68
    triangle *tris;    // 0x6c
    trishort *shTris;  // 0x70
    short *shHeight;   // 0x74
    u32 unk3;          // 0x78
};

class grassManager {
public:
    u8 unk[0x20];
    float *data;    // 0x20
    short *shData;  // 0x24
};

#define _mDrawVec            *(float *)0x803fa2a8
#define VertexOffset         *(float *)0x803fa2b0
#define gpMapObjGrassManager (*((grassManager **)0x8040df7c))
//#define _mDrawVecS16         *(s16 *)0x8040df74
//#define s16VertexOffset      *(s16 *)0x8040df78

static const TBGCheckData *floorBuffer = new TBGCheckData;

#define JSysNew   ((int (*)(...))0x802c3ca4)
#define shadeList ((bool *)grassGroup->data2[0x7])

void initGrassShade(grassObj *grassGroup);

void altDrawNear(grassObj *grassGroup);
