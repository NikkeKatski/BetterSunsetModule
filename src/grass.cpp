#include <grass.hxx>

#if 1
void initGrassShade(TGrassGroup *grassGroup) {
    if (grassGroup->data2[0x8])
        return;
    s16 floorS16 = (s16)grassGroup->grassFloor;

    grassGroup->data2[0x8] = true;
    grassGroup->data2[0x7] = JSysNew(grassGroup->triCount * sizeof(bool));
    grassGroup->data2[0x9] = JSysNew(grassGroup->triCount * sizeof(u16));
    for (int i = 0; i < grassGroup->triCount; i++) {
        JGeometry::TVec3<f32>& triVar = grassGroup->tris[i];
        JGeometry::TVec3<s16>& shTriVar = grassGroup->shTris[i];
        shTriVar.x = (s16)triVar.x;
        shTriVar.y = (s16)triVar.y;
        shTriVar.z = (s16)triVar.z;

        gpMapCollisionData->checkGround(triVar.x, grassGroup->grassFloor + 100.0f, triVar.z, 0, &floorBuffer);
        if (floorBuffer->mValue == 1) {
            shadeList[i] = true;

        } else {
            shadeList[i] = false;
        }
        snapList[i] = floorBuffer->mMinHeight;
        shTriVar.y    = shTriVar.y - floorS16 + snapList[i];
    }
}

Mtx empty = {};
SMS_WRITE_32(0x801e9234, 0x60000000);  // make all grass drawNear
void altDrawNear(TGrassGroup *grassGroup) {
    initGrassShade(grassGroup);
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

        JGeometry::TVec3<f32> marioPos;
        marioPos.set(gpMarioAddress->mTranslation);
        marioPos.y += 35.f; // I think it looks better.

        s16 shDrawVec       = _mDrawVec;
        s16 shVertexOffset  = VertexOffset;

        s16 floorS16 = (s16)grassGroup->grassFloor;
        for (int i = 0; i < grassGroup->triCount; i = i + 1) {
            s16 grassSway = gpMapObjGrassManager->shData[i % 9];

            JGeometry::TVec3<f32>& triVar = grassGroup->tris[i];
            JGeometry::TVec3<f32> tempTri;
            tempTri.set(triVar);
            tempTri.x += grassSway;

            JGeometry::TVec3<s16> &shTriVar = grassGroup->shTris[i];
            // Push should depend on horizontal distance, not height difference.
            JGeometry::TVec3<f32> marioDist = triVar - marioPos;
            marioDist.y = 0.01f;

            useAlt = shadeList[i];

            const f32 maxPush = 75.f;
            const f32 maxDist = 150.f;
            s16 push = 0;
            f32 dist = marioDist.magnitude();
            const bool marioAboveTip = marioPos.y > triVar.y;
            if (!marioAboveTip && dist > 0.0f && dist < maxDist) {
                marioDist.normalize();
                f32 percentage = 1.0f - dist / maxDist;
                push = maxPush * percentage;
            }

            GXPosition3s16(shTriVar.x - shDrawVec, snapList[i], shTriVar.z - shVertexOffset);
            GXColor1x8(useAlt ? 0 : 3);
            if (push == 0) {
                GXPosition3s16(shTriVar.x + grassSway, shTriVar.y, shTriVar.z);
            } else {
                GXPosition3s16(shTriVar.x + grassSway + push * marioDist.x, shTriVar.y, shTriVar.z + push * marioDist.z);
            }
            GXColor1x8(useAlt ? 0 : 2);
            GXPosition3s16(shTriVar.x + shDrawVec, snapList[i], shTriVar.z + shVertexOffset);
            GXColor1x8(useAlt ? 0 : 3);
        }
    }
    return;
}
SMS_PATCH_B(0x801e99a8, altDrawNear);
#endif