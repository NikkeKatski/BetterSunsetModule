#pragma once

#include <JSystem/JDrama/JDRViewObj.hxx>
#include <types.h>

#include <SMS/System/MarDirector.hxx>

#include <BetterSMS/module.hxx>

void initFilters(TMarDirector *);
void updateFilters(TMarDirector *);

class TPerformLink {
public:
	TPerformLink(JDrama::TViewObj* param_1, u32 param_2)
	    : unk4(param_1)
	    , unk8(param_2)
	{
	}

public:
	/* 0x0 */ JGadget::TSingleLinkListNode unk0;
	/* 0x4 */ JDrama::TViewObj* unk4;
	/* 0x8 */ u32 unk8;
};

class TScreenFilter : public JDrama::TViewObj {
public:

    TScreenFilter(const char *name) : JDrama::TViewObj(name) {
        mVisible = true;
        mPerformFlags = 0;
        mInjected = false;
    }
    ~TScreenFilter() override = default;
    
    virtual void load(JSUMemoryInputStream &stream) override;
    virtual void loadAfter() override;
    virtual void perform(u32, JDrama::TGraphics *) override;

    virtual void drawFilter(JDrama::TGraphics *graphics) = 0;

    void inject();

    bool mVisible;
    bool mInjected;
    bool mUsesScreenTexture;
    bool mUsesDepthBuffer;
    JDrama::TViewObj* mOrthoProj;
};

class TOutlineFilter : public TScreenFilter {
public:
    BETTER_SMS_FOR_CALLBACK static JDrama::TNameRef *instantiate() {
        return new TOutlineFilter("OutlineFilter");
    }

    TOutlineFilter(const char *name) : TScreenFilter(name) {
        mUsesScreenTexture = true;
        mUsesDepthBuffer = true;
    }

    void load(JSUMemoryInputStream &stream) override;
    void drawFilter(JDrama::TGraphics *graphics) override;

};
class TSubtleOutline : public TScreenFilter {
public:
    BETTER_SMS_FOR_CALLBACK static JDrama::TNameRef *instantiate() {
        return new TSubtleOutline("SubtleOutline");
    }

    TSubtleOutline(const char *name) : TScreenFilter(name) {
        mUsesScreenTexture = true;
        mUsesDepthBuffer = true;
    }
    
    void load(JSUMemoryInputStream &stream) override;
    void drawFilter(JDrama::TGraphics *graphics) override;
};

class TDepthOfField : public TScreenFilter {
public:
    BETTER_SMS_FOR_CALLBACK static JDrama::TNameRef *instantiate() {
        return new TDepthOfField("DepthOfField");
    }

    TDepthOfField(const char *name) : TScreenFilter(name) {
        mUsesScreenTexture = true;
        mUsesDepthBuffer = true;
    }

    void drawFilter(JDrama::TGraphics *graphics) override;
};

class TSpookyFilter : public TScreenFilter {
public:
    BETTER_SMS_FOR_CALLBACK static JDrama::TNameRef *instantiate() {
        return new TSpookyFilter("SpookyFilter");
    }

    TSpookyFilter(const char *name) : TScreenFilter(name) {
        mUsesScreenTexture = true;
        mUsesDepthBuffer = true;
    }
    
    void load(JSUMemoryInputStream &stream) override;
    void drawFilter(JDrama::TGraphics *graphics) override;

};

class TSunsetFilter : public TScreenFilter {
public:
    BETTER_SMS_FOR_CALLBACK static JDrama::TNameRef *instantiate() {
        return new TSunsetFilter("SunsetFilter");
    }

    TSunsetFilter(const char *name) : TScreenFilter(name) {
        mIntensity = 2.0f;
        mUsesScreenTexture = true;
        mUsesDepthBuffer = false;
    }

    void load(JSUMemoryInputStream &stream) override;
    void drawFilter(JDrama::TGraphics *graphics) override;

    f32 mIntensity;
};

class TNokiFilter : public TScreenFilter {
public:
    BETTER_SMS_FOR_CALLBACK static JDrama::TNameRef *instantiate() {
        return new TNokiFilter("NokiFilter");
    }

    TNokiFilter(const char *name) : TScreenFilter(name) {
        mUsesScreenTexture = true;
        mUsesDepthBuffer = true;
    }

    //void load(JSUMemoryInputStream &stream) override;
    void drawFilter(JDrama::TGraphics *graphics) override;

};

class TFogFilter : public TScreenFilter {
public:
    BETTER_SMS_FOR_CALLBACK static JDrama::TNameRef *instantiate() {
        return new TFogFilter("FogFilter");
    }

    TFogFilter(const char *name) : TScreenFilter(name) {
        mUsesScreenTexture = true;
        mUsesDepthBuffer = true;
    }

    void load(JSUMemoryInputStream &stream) override;
    void drawFilter(JDrama::TGraphics *graphics) override;

    u8 mDensity;
    u8 mOpacity;
    u8 mR;
    u8 mG;
    u8 mB;
};


class TFlashBangFilter : public TScreenFilter {
public:
    BETTER_SMS_FOR_CALLBACK static JDrama::TNameRef *instantiate() {
        return new TFlashBangFilter("FlashBangFilter");
    }

    TFlashBangFilter(const char *name) : TScreenFilter(name), mFlashIntensity(0) {
        mUsesScreenTexture = false;
        mUsesDepthBuffer = false;
    }

    virtual void perform(u32, JDrama::TGraphics *) override;
    void drawFilter(JDrama::TGraphics *graphics) override;

    u8 mFlashIntensity;
};