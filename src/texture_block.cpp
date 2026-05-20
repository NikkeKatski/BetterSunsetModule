#include "texture_block.hxx"

#include <JSystem/JParticle/JPAResourceManager.hxx>

#include <SMS/MSound/MSound.hxx>
#include <SMS/MSound/MSoundSESystem.hxx>
#include <SMS/Manager/RumbleManager.hxx>
#include <SMS/MarioUtil/TexUtil.hxx>

#include <raw_fn.hxx>

// TODO: This should be in bettersms
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


constexpr const char *gTerrainObjectManager =
    "\x92\x6e\x8c\x60\x83\x49\x83\x75\x83\x57\x83\x46\x83\x7d\x83\x6c\x81\x5b\x83\x57\x83\x83\x81\x5b\x00\x00\x00\x00";

constexpr const char *gObjectGroup = "\x83\x49\x83\x75\x83\x57\x83\x46\x83\x4e\x83\x67\x83\x4f\x83\x8b\x81\x5b\x83\x76\x00\x00\x00\x00";


static hit_data BrickBlock_hit_data{.mAttackRadius  = 0.0f,
                                   .mAttackHeight  = 0.0f,
                                   .mReceiveRadius = 0.0f,
                                   .mReceiveHeight = 0.0f};

static obj_hit_info BrickBlock_map_collision_info{
    ._00 = 1, .mType = 0, ._08 = 0, .mHitData = &BrickBlock_hit_data};

static const TMapObjCollisionData BrickBlock_map_collision_data[] = {
	{ "TextureBlock", 2 },
};

static obj_info BrickBlock_obj_info { ._00 = 1, ._02 = 1, ._04 = (void*)&BrickBlock_map_collision_data};

static TMapObjAnimData BrickBlock_anim_data[] = {
	{ "TextureBlockA.bmd", nullptr, 0, nullptr, nullptr },
	{ nullptr, nullptr, 0, nullptr, nullptr },
	{ "TextureBlockBreak.bmd", "textureblockbreak", 0, nullptr, nullptr },
};
static const TMapObjAnimDataInfo BrickBlock_anim_info
    = { 3, 2, BrickBlock_anim_data };

ObjData textureBlockData{.mMdlName  = "TextureBlock",
                      .mObjectID = 0x400000CC,
                      .mLiveManagerName = gTerrainObjectManager,
                      .mObjKey           = gObjectGroup,
                      .mAnimInfo         = (void*)&BrickBlock_anim_info,
                      .mObjCollisionData = &BrickBlock_map_collision_info,
                      .mMapCollisionInfo = &BrickBlock_obj_info,
                      .mSoundInfo        = nullptr,
                      .mPhysicalInfo     = nullptr,
                      .mSinkData         = nullptr,
                      ._28               = nullptr,
                      .mBckMoveData      = nullptr,
                      ._30               = 80.0f,
                      .mUnkFlags = 0x00002005,
                      .mKeyCode  = cexp_calcKeyCode("TextureBlock")};


// TODO: Move somewhere more sensible in bettersms
extern bool gParticleFlagLoaded[0x201];

// fabricated
inline static void SMS_LoadParticle(const char* path, u32 id)
{
	if (!gParticleFlagLoaded[id]) {
		gpResourceManager->load(path, id);
		gParticleFlagLoaded[id] = true;
	}
}


void TTextureBlock::initMapObj()
{
	TMapObjBase::initMapObj();
	mObjectID = 0x400002C2; // Hacky, but should work
	SMS_LoadParticle("/scene/mapObj/BrickBlockA.jpa", 0x60);
	// Re-using tinkoopa ids as this should never be in the same stage
	SMS_LoadParticle("/scene/mapObj/TextureBlockB.jpa", 0x1AC);
	SMS_LoadParticle("/scene/mapObj/BrickBlockC.jpa", 0x62);

}

void TTextureBlock::kill()
{
	makeObjDead();
	emitAndScale(0x60, 0, &mTranslation);
	emitAndScale(0x1AC, 0, &mTranslation);
	emitAndScale(0x62, 0, &mTranslation);
	if(gpMSound->gateCheck(0x3878)) {
		MSoundSESystem::MSoundSE::startSoundActor(0x3878, (Vec*)&mTranslation, 0, nullptr, 0, 4);
	}
	SMSRumbleMgr->start(0x15, 0x14, (Vec*)&mTranslation);
	appearObj(100.0f);
}

void TTextureBlock::loadBeforeInit(JSUMemoryInputStream& stream) {
	
	THideObjBase::loadBeforeInit(stream);
	// Sooo... Nintendo is doing some very whack shit here with looking up the object data based on load data
	// In the middle of the load...
	// So to have different models we need to overwrite the lookup object to avoid having to make an object per model
	textureBlockData.mMdlName = mRegisterName;
	textureBlockData.mKeyCode = TNameRef::calcKeyCode(mRegisterName);
}

void TTextureBlock::makeMActors()
{
	snprintf(mLetterName, 32, "%s.bmd", mRegisterName);
	BrickBlock_anim_data[0].unk0 = mLetterName;
	THideObjBase::makeMActors();
	
    char texPath[64];
    snprintf(texPath, 64, "/scene/mapobj/textureblock.bti");
    
    auto *timgdata = JKRFileLoader::getGlbResource(texPath);
    if(timgdata != nullptr) {
        auto *timg = reinterpret_cast<const ResTIMG *>(timgdata);
		mActorData->mModel->unlock();
		SMS_ChangeTextureAll(mActorData->mModel->mModelData, "B_rengaBLOCK", *timg);
		mActorData->mModel->makeDL();
		mActorData->mModel->lock();
	}
}
