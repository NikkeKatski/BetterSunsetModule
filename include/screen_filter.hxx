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
        mPerformFlags = 0;
        mInjected = false;
    }
    ~TScreenFilter() override = default;
    
    virtual void load(JSUMemoryInputStream &stream) override;
    virtual void loadAfter() override;
    virtual void perform(u32, JDrama::TGraphics *) override;

    virtual void drawFilter() = 0;

    bool mVisible;
    bool mInjected;
    bool mUsesScreenTexture;
    bool mUsesDepthBuffer;
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
    void drawFilter() override;

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

    void drawFilter() override;

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
    void drawFilter() override;

    f32 mIntensity;
};
