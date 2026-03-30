#include <grass.hxx>

void initGrassShade(TGrassGroup *grassGroup) {
    if (grassGroup->data2[0x8])
        return;
    grassGroup->data2[0x8] = true;
    grassGroup->data2[0x7] = JSysNew(grassGroup->triCount * sizeof(bool));
    for (int i = 0; i < grassGroup->triCount; i++) {
        JGeometry::TVec3<f32>& triVar = grassGroup->tris[i];
        gpMapCollisionData->checkGround(triVar.x, grassGroup->grassFloor + 80.0f, triVar.z, 0, &floorBuffer);
        if (floorBuffer->mValue == 1) {
            shadeList[i] = true;
        } else {
            shadeList[i] = false;
        }
    }
}

#if 1
Mtx empty = {};
SMS_WRITE_32(0x801e9234, 0x60000000);  // make all grass drawNear for now
void altDrawNear(TGrassGroup *grassGroup) {
    initGrassShade(grassGroup);
    //trishort trishVar;
    u8 flrVal;
    bool useAlt;
    u32 *altColor1 = (u32 *)0x8040c958;
    *altColor1     = 0x0a3000ff;

    if (grassGroup->unk3 == 0) {
        gekko_ps_copy12__9JGeometryFPvPv(empty, 0x804045dc, 0);
        GXLoadPosMtxImm(empty, 0x0);

        GXSetVtxAttrFmt(0, 9, 1, 3, 0);
        GXSetVtxAttrFmt(0, 0xb, 1, 5, 0);
        GXSetVtxDesc(9, 1);
        GXSetVtxDesc(0xb, 2);

        GXSetArray(0xb, altColor1, 4);
        GXBegin(0x90, 0, grassGroup->triCount * 3);

        JGeometry::TVec3<f32> marioPos = gpMarioAddress->mTranslation;

        s16 floorS16 = (s16)grassGroup->grassFloor;
        for (int i = 0; i < grassGroup->triCount; i = i + 1) {
            JGeometry::TVec3<f32>& triVar = grassGroup->tris[i];
            useAlt = shadeList[i];

            JGeometry::TVec3<f32> marioDist = triVar - marioPos;
            f32 dist = marioDist.magnitude();
            marioDist.normalize();

            if(dist > 200.0f) {
                dist = 200.0f;
            }

            f32 percentage = 1.0f - dist / 200.0f;
            f32 push = 100.0f * percentage;

            GXPosition3s16(triVar.x - _mDrawVec, floorS16, triVar.z - VertexOffset);
            GXColor1x8(useAlt ? 0 : 3);
            GXPosition3s16(triVar.x + gpMapObjGrassManager->shData[i % 9] + push * marioDist.x, triVar.y, triVar.z + push * marioDist.z);
            GXColor1x8(useAlt ? 0 : 2);
            GXPosition3s16(triVar.x + _mDrawVec, floorS16, triVar.z + VertexOffset);
            GXColor1x8(useAlt ? 0 : 3);
        
        }
    }
    return;
}
SMS_PATCH_B(0x801e99a8, altDrawNear);
#endif