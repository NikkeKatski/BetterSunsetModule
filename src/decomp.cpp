#include <BetterSMS/module.hxx>

#include <raw_fn.hxx>
#include <rand.h>
#include <BetterSMS/debug.hxx>
#include <JDrama/JDRActor.hxx>
#include <MarioUtil/MathUtil.hxx>
#include <GX.h>
#include <settings.hxx>
#include <Map/MapCollisionData.hxx>
#include <raw_fn.hxx>
#include <BetterSMS/libs/constmath.hxx>

#define getActorMtx ((void (*)(...))0x8022B10C)
#define _5285       0.8f
#define _4492       50.f

static TLiveActor *mPrevOwner;
u32 testCheckRide(TMario* player) {

    auto Floor = player->mFloorTriangle;
    if (Floor == nullptr) {
        player->mPriorityCollisionOwner = nullptr;
        mPrevOwner = nullptr;
        return 0;
    }

    auto FloorObj = Floor->mOwner;
    if (FloorObj == nullptr) {
        player->mPriorityCollisionOwner = nullptr;
        mPrevOwner = nullptr;
        return 0;
    }

    // Assign owner
    player->mPriorityCollisionOwner = FloorObj;

    // Copy platform base matrix
    Mtx baseMtx;
    getActorMtx(FloorObj, baseMtx);

    // If owner changed, compute local coordinates relative to the platform
    if (mPrevOwner == nullptr || mPrevOwner != FloorObj) {
        player->_30C = FloorObj->mRotation.y;

        // Compute inverse(baseMtx)
        Mtx invMtx;
        PSMTXCopy(baseMtx, invMtx);
        PSMTXInverse(invMtx, invMtx); // safe to call; if it were to fail in this environment, fallback below

        // Transform world -> local: local = invMtx * worldTranslation
        PSMTXMultVec(invMtx, player->mTranslation, player->_2F4);
    }

    // Apply rotation delta if close to platform surface
    if (player->mTranslation.y - player->mFloorBelow <= 4.f)
        player->mAngle.y = player->mAngle.y + convertAngleFloatToS16(FloorObj->mRotation.y - player->_30C);

    // Update stored owner rotation and prev owner
    player->_30C = FloorObj->mRotation.y;
    mPrevOwner = FloorObj;

    // Preserve previous local vector, then compute world translation from local
    player->_300.set(player->_2F4);
    if (player->mTranslation.y - player->mFloorBelow <= 4.f) {
        PSMTXMultVec(baseMtx, player->_2F4, player->mTranslation);
    }

    return 0;
}
SMS_PATCH_B(0x802502b0, testCheckRide);