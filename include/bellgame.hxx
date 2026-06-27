#pragma once

#include <SMS/MapObj/MapObjBase.hxx>
#include <BetterSMS/module.hxx>

class TBellGame : public TMapObjBase {
public:
    BETTER_SMS_FOR_CALLBACK static JDrama::TNameRef *instantiate() {
        return new TBellGame("BellGame");
    }

	TBellGame(const char* name = "BellGame") : TMapObjBase(name), mGroundPoundHeight(0.0f), mIsPounding(false), mIsUltraPounded(false), mSpawnedRedCoin(false), mIntensity(0.0f), mTimer(0.0f), mTimerMult(0.0f) {
	}
	//virtual void initMapObj();
	//virtual void loadBeforeInit(JSUMemoryInputStream&);
	//virtual void makeMActors();
	
	virtual void perform(u32, JDrama::TGraphics*);
    virtual void touchPlayer(THitActor *);
	void loadAfter();

	f32 calculateBellTween();

public:
	f32 mGroundPoundHeight;
	bool mIsPounding;
	bool mIsUltraPounded;
	bool mSpawnedRedCoin;
	f32 mIntensity;
	f32 mTimer;
	f32 mTimerMult;
};

extern ObjData bellgameData;