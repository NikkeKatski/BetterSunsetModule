#pragma once

#include <SMS/MapObj/MapObjBase.hxx>

#include <BetterSMS/module.hxx>
#include <MarioUtil/ShadowUtil.hxx>

class THideObjBase : public TMapObjBase {
public:
	THideObjBase(const char* name = "隠しオブジェ");

	virtual void load(JSUMemoryInputStream&);
	virtual void loadAfter();
	virtual bool receiveMessage(THitActor* sender, u32 message);
	virtual void appearObj(f32);
	virtual void appearObjFromPoint(const JGeometry::TVec3<f32>&);
	virtual void emitEffect();

public:
	/* 0x138 */ TMapObjBase* unk138;
	/* 0x13C */ f32 unk13C;
	/* 0x140 */ f32 unk140;
	/* 0x144 */ char* unk144;
	/* 0x148 */ s32 unk148;
	/* 0x14C */ bool unk14C;
};

class TBrickBlock : public THideObjBase {
public:
	TBrickBlock(const char* name = "レンガブロック") : THideObjBase(name) {
	}
	virtual void initMapObj();
	virtual bool receiveMessage(THitActor* sender, u32 message);
	virtual void kill();
};

class TTextureBlock : public TBrickBlock {
public:
    BETTER_SMS_FOR_CALLBACK static JDrama::TNameRef *instantiate() {
        return new TTextureBlock("TextureBlock");
    }

	TTextureBlock(const char* name = "TextureBlock") : TBrickBlock(name) {
	}
	virtual void initMapObj();
	virtual void kill();
	virtual void loadBeforeInit(JSUMemoryInputStream&);
	virtual void makeMActors();
	virtual u32 getShadowType();
	virtual void requestShadow();
	//virtual void perform(u32, JDrama::TGraphics*);

public:
	char mLetterName[32];
	//TMBindShadowBody* mBindShadowBody;
};

extern ObjData textureBlockData;