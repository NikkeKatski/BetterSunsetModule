#include "bellgame.hxx"

#include <JSystem/JParticle/JPAResourceManager.hxx>

#include <SMS/MSound/MSound.hxx>
#include <SMS/MSound/MSoundSESystem.hxx>
#include <SMS/Manager/RumbleManager.hxx>
#include <SMS/MarioUtil/TexUtil.hxx>
#include <SMS/Manager/MarioParticleManager.hxx>
#include <SMS/MSound/MSoundSESystem.hxx>
#include <SMS/Manager/ItemManager.hxx>

#include <BetterSMS/libs/constmath.hxx>

#include <raw_fn.hxx>

// TODO: This should be in bettersms
// TODO: Do not duplicate, i was lazy
// fabricated
struct TMapObjCollisionData {
	/* 0x0 */ const char* unk0;
	/* 0x4 */ u16 unk4;
};

// fabricated
struct TMapObjAnimData {
	/* 0x0 */ const char* unk0;
	/* 0x4 */ const char* unk4;
	/* 0x8 */ u8 unk8;
	/* 0xC */ const char* unkC;
	/* 0x10 */ const char* unk10;
};

// the only real name we have, everything else is fabricated
struct TMapObjAnimDataInfo {
	/* 0x0 */ u16 unk0;
	/* 0x2 */ u16 unk2;
	/* 0x4 */ const TMapObjAnimData* unk4;
};

static hit_data BellGame_hit_data{.mAttackRadius  = 75.0f,
                                   .mAttackHeight  = 200.0f,
                                   .mReceiveRadius = 75.0f,
                                   .mReceiveHeight = 200.0f};

static obj_hit_info BellGame_collision_info{
    ._00 = 1, .mType = 0x80000000, .mVisualOfsY = 60.0f, .mHitData = &BellGame_hit_data};

static const TMapObjCollisionData BellGame_map_collision_data[] = {
	{ "BellGame", 2 },
};

static obj_info BellGame_obj_info { ._00 = 1, ._02 = 1, ._04 = (void*)&BellGame_map_collision_data};

constexpr const char *gTerrainObjectManager =
    "\x92\x6e\x8c\x60\x83\x49\x83\x75\x83\x57\x83\x46\x83\x7d\x83\x6c\x81\x5b\x83\x57\x83\x83\x81\x5b\x00\x00\x00\x00";

constexpr const char *gObjectGroup = "\x83\x49\x83\x75\x83\x57\x83\x46\x83\x4e\x83\x67\x83\x4f\x83\x8b\x81\x5b\x83\x76\x00\x00\x00\x00";
ObjData bellgameData{.mMdlName         = "BellGame",
                             .mObjectID        = 0x40000F00,
                             .mLiveManagerName = gTerrainObjectManager,  // const_cast<char *>("木マネージャー")
                             .mObjKey = gObjectGroup,  // const_cast<char *>("GenericRailObj"),
                             .mAnimInfo         = nullptr,
                             .mObjCollisionData = &BellGame_collision_info,
                             .mMapCollisionInfo = &BellGame_obj_info,
                             .mSoundInfo        = nullptr,
                             .mPhysicalInfo     = nullptr,
                             .mSinkData         = nullptr,
                             ._28               = nullptr,
                             .mBckMoveData      = nullptr,
                             ._30               = 100.0f,
                             .mUnkFlags         = 0x4 /*0x02130100*/,
                             .mKeyCode          = cexp_calcKeyCode("BellGame")};


const f32 TIMER_LENGTH = (30.0f * 4.0f * 1.0f); // 5 second animation

f32 TBellGame::calculateBellTween() {
            f32 delta = 1.0f / (TIMER_LENGTH * mTimerMult);
            f32 timer = 1.0f - ((f32)mTimer * delta);
            f32 prevTimer = timer - delta;

    const int maxBounces = 8;
    const f32 minBounceHeight = 0.015f;

    f32 peak = mIntensity;
    f32 restitution = 0.18f + mIntensity * 0.62f;

    int bounces = 1;
    f32 h = peak * restitution * restitution;

    while (bounces < maxBounces && h > minBounceHeight) {
        bounces++;
        h *= restitution * restitution;
    }

    f32 totalWeight = 0.0f;
    f32 weight = 1.0f;

    for (int i = 0; i < bounces; i++) {
        totalWeight += weight;
        weight *= restitution;
    }
    
    f32 arcStart = 0.0f;
    f32 height = peak;
    weight = 1.0f;

    for (int i = 0; i < bounces; i++) {
        f32 duration = weight / totalWeight;
        f32 arcEnd = arcStart + duration;
        f32 peakTime = arcStart + duration * 0.5f;

        if (i == 0 && prevTimer < peakTime && timer >= peakTime) {
            if(mIsUltraPounded) {
                Mtx& bellMtx = getModel()->mJointArray[3];
                gpMarioParticleManager->emitAndBindToMtxPtr(0x181, bellMtx, 1, this);
                gpMarioParticleManager->emitAndBindToMtxPtr(0x182, bellMtx, 1, this);
                gpMarioParticleManager->emitAndBindToMtxPtr(0x183, bellMtx, 1, this);

                if(gpMSound->gateCheck(0x38A6)) {
                    JAISound* sound = MSoundSESystem::MSoundSE::startSoundActor(0x38A6, this->mTranslation, 0, nullptr, 0, 4);
                    sound->setPitch(1.5f, 0, 0);
                }

                if(!mSpawnedRedCoin) {
                    TLiveActor* redCoin = gpItemManager->makeObjAppear(0x2000000f);
                    if(redCoin != nullptr) {
                        appearWithoutSound__5TCoinFv(redCoin);
                        redCoin->mTranslation.x = bellMtx[0][3];
                        redCoin->mTranslation.y = bellMtx[1][3] + 20.0f;
                        redCoin->mTranslation.z = bellMtx[2][3];
                    

                        redCoin->mSpeed.x = 2.0f * cosf(mRotation.y);
                        redCoin->mSpeed.y = 2.0f;
                        redCoin->mSpeed.z = 2.0f * -sinf(mRotation.y);

                        redCoin->mStateFlags.asU32 &= ~0x10;
                        mSpawnedRedCoin = true;
                    }
                }
                
            }
        }

        if (timer <= arcEnd || i == bounces - 1) {
            f32 u = (timer - arcStart) / duration;
            return 4.0f * height * u * (1.0f - u);
        }

        arcStart = arcEnd;
        weight *= restitution;
        height *= restitution * restitution;
    }
    return 0.0f;
}

void TBellGame::perform(u32 flags, JDrama::TGraphics* graphics) {
    if(flags & 1) {
        // In case the animation finishes, we just reset it
        // Probably not needed
        if(mActorData->isCurAnmAlreadyEnd(MActor::BCK)) {
           mActorData->setAnimation("bellgame", MActor::BCK);
        }

        if(SMS_IsMarioStatusHipDrop__Fv()) {
            if(!mIsPounding) {
                mGroundPoundHeight = gpMarioOriginal->mTranslation.y;
                mIsPounding = true;
            }
        } else {
            mIsPounding = false;
        }


        J3DFrameCtrl* ctrl = mActorData->getFrameCtrl(MActor::BCK);
        if(mTimer > 0.0f) {
            f32 delta = 1.0f / (TIMER_LENGTH * mTimerMult);
            f32 progress = 1.0f - ((f32)mTimer * delta);

            ctrl->mCurFrame = ctrl->mNumFrames * calculateBellTween();

            mTimer -= 1.0f;
        } else {
            ctrl->mCurFrame = 0;
        }
    }

    TMapObjBase::perform(flags, graphics);
}

void TBellGame::touchPlayer(THitActor *player) {
    if(marioHipAttack() && mTimer <= 0) {

        f32 poundedHeight = (mGroundPoundHeight - gpMarioOriginal->mTranslation.y);

        TMario* mario = (TMario*)player;
        if(mario->mFloorTriangle != nullptr) {
            if(mario->mFloorTriangle->mSoundID == 1) {
                mIsUltraPounded = mario->mSubState == 3; // The ultra ground pound
                mIntensity = poundedHeight / 2000.0f;
                if(mIntensity >= 1.0f) {
                    mIntensity = 0.9f;
                }
                if(mIntensity < 0.1f) {
                    mIntensity = 0.1f;
                }
                if(mIsUltraPounded) {
                    mIntensity = 1.0f;
                }

                mTimerMult = (mIntensity - 0.1f) * 2 + 1;

                mTimer = TIMER_LENGTH * mTimerMult;
            }
        }

    }
}

void TBellGame::loadAfter() {
    TMapObjBase::loadAfter();
    TItemManager::newAndRegisterCoin(200); // Create a new red coin to spawn
    mTranslation.y -= 60.0f;
}
