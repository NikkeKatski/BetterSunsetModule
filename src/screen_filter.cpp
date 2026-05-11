#include "screen_filter.hxx"

#include <GX.h>
#include <JSystem/JDrama/JDREfbCtrl.hxx>
#include <JSystem/JUtility/JUTTexture.hxx>
#include <SMS/Camera/PolarSubCamera.hxx>
#include <SMS/Enemy/Conductor.hxx>

class TScreenTexture : public JDrama::TNameRef {
public:
    u16 unk;
    JUTTexture *texture;
};

extern TScreenTexture *gpScreenTexture;

bool gIsFirstUpdate;
bool gUsesScreenTexture;
bool gUsesDepthBuffer;
bool gInitDepthBuffer;
u32 *gDepthBuffer;
bool gUpdateScreenTexture;

void initFilters(TMarDirector *marDirector) {
    gIsFirstUpdate       = true;
    gUsesScreenTexture   = false;
    gUsesDepthBuffer     = false;
    gUpdateScreenTexture = false;
    gInitDepthBuffer     = false;
    gDepthBuffer         = nullptr;
}

void updateFilters(TMarDirector *) {
    if (gIsFirstUpdate) {
        if (gUsesDepthBuffer && !gInitDepthBuffer) {
            gDepthBuffer = new u32[640 * 448 / 2];
        }
        gIsFirstUpdate = false;
    }

    if (gUsesScreenTexture) {
        gUpdateScreenTexture = true;
    }
}

void initDepthMap() {
    if (gUsesDepthBuffer && !gInitDepthBuffer) {
        //gDepthBuffer     = new u32[640 * 448 / 2];
        gDepthBuffer = (u32*)JKRHeap::sCurrentHeap->alloc(640 * 448 * 2, 32);
        gInitDepthBuffer = true;
    }
}

void TScreenFilter::load(JSUMemoryInputStream &stream) {
    JDrama::TNameRef::load(stream);
    stream.readData(&mVisible, sizeof(bool));
}

void TScreenFilter::loadAfter() { gpConductor->registerOtherObj(this); }

// Requires 5 tev stages + KColor3 for fine tuned depth selection
// nearFarSelect = 0 -> Keep everything in near
// nearFarSelect = 255 -> Keep everything in far
// nearFarSelect = 128 -> Keep about 50/50
// Depth map is 24 bit, but only upper 16 really matter, this allows us to fetch map depth with
// different focus. E.g fog usually cares about far Depth of field would possibly need just near
void setupDepthMap(u8 stage, u8 nearFarSelect, u8 out, u8 texCoord, u8 texMap = GX_TEXMAP0) {
    u8 g = nearFarSelect * 4;
    GXColor nearFarDistanceCol = {nearFarSelect, nearFarSelect, 0, 0};
    GXSetTevKColor(GX_KCOLOR3, nearFarDistanceCol);
    
    // Close to camera, scaled such that step size is R
    GXSetTevOrder(GX_TEVSTAGE0 + stage, texCoord, texMap, 0xff);
    GXSetTevColorIn(GX_TEVSTAGE0 + stage, GX_CC_ONE, GX_CC_ZERO, GX_CC_TEXC, GX_CC_ZERO);
    GXSetTevColorOp(GX_TEVSTAGE0 + stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_4, GX_TRUE,
                    GX_TEVPREV);
    GXSetTevDirect(GX_TEVSTAGE0 + stage);

    GXSetTevOrder(GX_TEVSTAGE1 + stage, texCoord, texMap, 0xff);
    GXSetTevColorIn(GX_TEVSTAGE1 + stage, GX_CC_ZERO, GX_CC_KONST, GX_CC_CPREV, GX_CC_ZERO);
    GXSetTevColorOp(GX_TEVSTAGE1 + stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_4, GX_TRUE,
                    GX_TEVPREV);
    GXSetTevDirect(GX_TEVSTAGE1 + stage);

    GXSetTevOrder(GX_TEVSTAGE2 + stage, texCoord, texMap, 0xff);
    GXSetTevColorIn(GX_TEVSTAGE2 + stage, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_CPREV);
    GXSetTevColorOp(GX_TEVSTAGE2 + stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_4, GX_TRUE,
                    GX_TEVPREV);
    GXSetTevDirect(GX_TEVSTAGE2 + stage);
    if(nearFarSelect < 64) {
        GXSetTevKColorSel(GX_TEVSTAGE1 + stage, GX_TEV_KCSEL_K3_G);
    } else {
        GXSetTevKColorSel(GX_TEVSTAGE1 + stage, GX_TEV_KCSEL_1);
    }

    GXSetTevOrder(GX_TEVSTAGE3 + stage, texCoord, texMap, 0xff);
    GXSetTevColorIn(GX_TEVSTAGE3 + stage, GX_CC_ZERO, GX_CC_KONST, GX_CC_CPREV, GX_CC_ZERO);
    GXSetTevColorOp(GX_TEVSTAGE3 + stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_4, GX_TRUE,
                    GX_TEVPREV);
    GXSetTevDirect(GX_TEVSTAGE3 + stage);
    GXSetTevDirect(GX_TEVSTAGE3 + stage);
    if(nearFarSelect < 64) {
        GXSetTevKColorSel(GX_TEVSTAGE3 + stage, GX_TEV_KCSEL_1);
    } else {
        GXSetTevKColorSel(GX_TEVSTAGE3 + stage, GX_TEV_KCSEL_K3_R);
    }

    // Far from camera, scaled such that band size is 0-R
    GXSetTevOrder(GX_TEVSTAGE4 + stage, texCoord, texMap, 0xff);
    GXSetTevColorIn(GX_TEVSTAGE4 + stage, GX_CC_KONST, GX_CC_ZERO, GX_CC_TEXA, GX_CC_CPREV);
    GXSetTevColorOp(GX_TEVSTAGE4 + stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, out);
    GXSetTevDirect(GX_TEVSTAGE4 + stage);
    GXSetTevKColorSel(GX_TEVSTAGE4 + stage, GX_TEV_KCSEL_K3_R);
}

void TScreenFilter::inject() {
    if (mInjected)
        return;

    for (auto it = gpMarDirector->mPerformListGXPost->begin();
         it != gpMarDirector->mPerformListGXPost->end(); it = it->mNext) {
        if (strcmp("<TOrthoProj>", ((JDrama::TViewObj *)it->mData)->mKeyName) == 0) {
            auto data  = new TPerformLink(this, 0x80);
            mOrthoProj = (JDrama::TViewObj *)it->mData;
            gpMarDirector->mPerformListGXPost->Insert(
                it, reinterpret_cast<JGadget::TSingleLinkListNode *>((u32)data + 4));
            break;
        }
    }
    gUsesScreenTexture = gUsesScreenTexture || mUsesScreenTexture;
    gUsesDepthBuffer   = gUsesDepthBuffer || mUsesDepthBuffer;

    mInjected = true;
}

constexpr static const char *efbNormalSceneDrawing =
    "\x92\xca\x8f\xed\x83\x56\x81\x5b\x83\x93\x95\x60\x89\xe6\x83\x58\x83\x65\x81\x5b\x83\x57";
void TScreenFilter::perform(u32 flags, JDrama::TGraphics *graphics) {
    if ((flags & 0x1) != 0) {
        inject();
    }

    if ((flags & 0x80) != 0 && mVisible) {
        if (mUsesScreenTexture) {
            JDrama::TEfbCtrlTex *screenEfbTex = (JDrama::TEfbCtrlTex *)gpMarDirector->searchF(
                calcKeyCode(efbNormalSceneDrawing), efbNormalSceneDrawing);
            if (screenEfbTex != nullptr) {
                screenEfbTex->testPerform(flags | 0x8, graphics);
            }
        }
        initDepthMap();
        drawFilter(graphics);
        mOrthoProj->perform(0x14, graphics);
    }
}

void TSunsetFilter::load(JSUMemoryInputStream &stream) {
    TScreenFilter::load(stream);

    stream.readData(&mIntensity, sizeof(f32));
}

void TSunsetFilter::drawFilter(JDrama::TGraphics *graphics) {
    	Mtx e_m;
	Mtx44 m;

	GXColor tev_color = { 0x03, 0x03, 0x03, 0x00 };
	u8 vFilter[7]     = { 0x15, 0x00, 0x00, 0x16, 0x00, 0x00, 0x15 };

	f32 f_left   = 0;
	f32 f_wd     = 640;
	f32 f_top    = 0;
	f32 f_ht     = 448;
	f32 f_right  = f_left + f_wd;
	f32 f_bottom = f_top + f_ht;
	f32 offset_x = (mIntensity / f_wd);
	f32 offset_y = (mIntensity / f_wd);
	
	C_MTXOrtho(m, f_top, f_bottom, f_left, f_right, 0.0f, 1.0f);
	PSMTXIdentity(e_m);
	GXClearVtxDesc();
	GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
	GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
	GXSetVtxDesc(GX_VA_TEX1, GX_DIRECT);
	GXSetVtxDesc(GX_VA_TEX2, GX_DIRECT);
	GXSetVtxDesc(GX_VA_TEX3, GX_DIRECT);

	GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_U16, 0);
	GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
	GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX1, GX_TEX_ST, GX_F32, 0);
	GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX2, GX_TEX_ST, GX_F32, 0);
	GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX3, GX_TEX_ST, GX_F32, 0);

	GXSetNumChans(0);
	GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_REG, 0, GX_DF_NONE,
	              GX_AF_NONE);
	GXSetChanCtrl(GX_COLOR1A1, GX_FALSE, GX_SRC_REG, GX_SRC_REG, 0, GX_DF_NONE,
	              GX_AF_NONE);

	GXSetNumTexGens(4);
	GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, 0x3c, GX_FALSE,
	                  0x7d);
	GXSetTexCoordGen2(GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_TEX1, 0x3c, GX_FALSE,
	                  0x7d);
	GXSetTexCoordGen2(GX_TEXCOORD2, GX_TG_MTX2x4, GX_TG_TEX2, 0x3c, GX_FALSE,
	                  0x7d);
	GXSetTexCoordGen2(GX_TEXCOORD3, GX_TG_MTX2x4, GX_TG_TEX3, 0x3c, GX_FALSE,
	                  0x7d);
	
	GXLoadTexObj(&gpScreenTexture->texture->mTexObj, GX_TEXMAP0);

	GXSetNumTevStages(4);

	// Stage 0
	GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, 0xFF);
	GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_TEXC, GX_CC_HALF, GX_CC_C0);
	GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1,
	                GX_FALSE, GX_TEVPREV);
	GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO,
	                GX_CA_ZERO);
	GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1,
	                GX_TRUE, GX_TEVPREV);
	GXSetTevDirect(GX_TEVSTAGE0);

	// Stage 1
	GXSetTevOrder(GX_TEVSTAGE1, GX_TEXCOORD1, GX_TEXMAP0, 0xFF);
	GXSetTevColorIn(GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_TEXC, GX_CC_HALF,
	                GX_CC_CPREV);
	GXSetTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1,
	                GX_FALSE, GX_TEVPREV);
	GXSetTevAlphaIn(GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO,
	                GX_CA_ZERO);
	GXSetTevAlphaOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1,
	                GX_TRUE, GX_TEVPREV);
	GXSetTevDirect(GX_TEVSTAGE1);

	// Stage 2
	GXSetTevOrder(GX_TEVSTAGE2, GX_TEXCOORD2, GX_TEXMAP0, 0xFF);
	GXSetTevColorIn(GX_TEVSTAGE2, GX_CC_ZERO, GX_CC_TEXC, GX_CC_HALF,
	                GX_CC_CPREV);
	GXSetTevColorOp(GX_TEVSTAGE2, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1,
	                GX_FALSE, GX_TEVPREV);
	GXSetTevAlphaIn(GX_TEVSTAGE2, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO,
	                GX_CA_ZERO);
	GXSetTevAlphaOp(GX_TEVSTAGE2, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1,
	                GX_TRUE, GX_TEVPREV);
	GXSetTevDirect(GX_TEVSTAGE2);

	// Stage 3
	GXSetTevOrder(GX_TEVSTAGE3, GX_TEXCOORD3, GX_TEXMAP0, 0xFF);
	GXSetTevColorIn(GX_TEVSTAGE3, GX_CC_ZERO, GX_CC_TEXC, GX_CC_HALF,
	                GX_CC_CPREV);
	GXSetTevColorOp(GX_TEVSTAGE3, GX_TEV_ADD, GX_TB_ZERO, GX_CS_DIVIDE_2,
	                GX_TRUE, GX_TEVPREV);
	GXSetTevAlphaIn(GX_TEVSTAGE3, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO,
	                GX_CA_ZERO);
	GXSetTevAlphaOp(GX_TEVSTAGE3, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1,
	                GX_TRUE, GX_TEVPREV);
	GXSetTevDirect(GX_TEVSTAGE3);

	GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_OR, GX_ALWAYS, 0);

	GXSetTevColor(GX_TEVREG0, tev_color);
	GXSetProjection(m, GX_ORTHOGRAPHIC);
	GXSetViewport(f_left, f_top, f_wd, f_ht, 0.0, 1.0);
	GXSetScissor(f_left, f_top, f_wd, f_ht);

	GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
	GXSetAlphaUpdate(GX_FALSE);
	GXSetColorUpdate(GX_TRUE);
	GXLoadPosMtxImm(e_m, 0);
	GXSetCurrentMtx(0);
	GXSetCullMode(GX_CULL_NONE);
	GXSetBlendMode(GX_BM_SUBTRACT, GX_BL_ZERO, GX_BL_ZERO, GX_LO_NOOP);

	GXBegin(GX_QUADS, GX_VTXFMT0, 4);
	GXPosition2u16(f_left, f_top);
	GXTexCoord2f32(-offset_x, 0.0f);
	GXTexCoord2f32(offset_x, 0.0f);
	GXTexCoord2f32(0.0f, -offset_y);
	GXTexCoord2f32(0.0f, offset_y);
	GXPosition2u16(f_left + f_wd, f_top);
	GXTexCoord2f32(1.0f - offset_x, 0.0f);
	GXTexCoord2f32(1.0f + offset_x, 0.0f);
	GXTexCoord2f32(1.0f, -offset_y);
	GXTexCoord2f32(1.0f, offset_y);
	GXPosition2u16(f_left + f_wd, f_top + f_ht);
	GXTexCoord2f32(1.0f - offset_x, 1.0f);
	GXTexCoord2f32(1.0f + offset_x, 1.0f);
	GXTexCoord2f32(1.0f, 1.0f - offset_y);
	GXTexCoord2f32(1.0f, 1.0f + offset_y);
	GXPosition2u16(f_left, f_top + f_ht);
	GXTexCoord2f32(-offset_x, 1.0f);
	GXTexCoord2f32(+offset_x, 1.0f);
	GXTexCoord2f32(0.0f, 1.0f - offset_y);
	GXTexCoord2f32(0.0f, 1.0f + offset_y);
	GXEnd();

	GXSetBlendMode(GX_BM_BLEND, GX_BL_ONE, GX_BL_ONE, GX_LO_NOOP);
	GXBegin(GX_QUADS, GX_VTXFMT0, 4);
	GXPosition2u16(f_left, f_top);
	GXTexCoord2f32(-offset_x, 0.0f);
	GXTexCoord2f32(offset_x, 0.0f);
	GXTexCoord2f32(0.0f, -offset_y);
	GXTexCoord2f32(0.0f, offset_y);
	GXPosition2u16(f_left + f_wd, f_top);
	GXTexCoord2f32(1.0f - offset_x, 0.0f);
	GXTexCoord2f32(1.0f + offset_x, 0.0f);
	GXTexCoord2f32(1.0f, -offset_y);
	GXTexCoord2f32(1.0f, offset_y);
	GXPosition2u16(f_left + f_wd, f_top + f_ht);
	GXTexCoord2f32(1.0f - offset_x, 1.0f);
	GXTexCoord2f32(1.0f + offset_x, 1.0f);
	GXTexCoord2f32(1.0f, 1.0f - offset_y);
	GXTexCoord2f32(1.0f, 1.0f + offset_y);
	GXPosition2u16(f_left, f_top + f_ht);
	GXTexCoord2f32(-offset_x, 1.0f);
	GXTexCoord2f32(+offset_x, 1.0f);
	GXTexCoord2f32(0.0f, 1.0f - offset_y);
	GXTexCoord2f32(0.0f, 1.0f + offset_y);
	GXEnd();
}

void TOutlineFilter::load(JSUMemoryInputStream &stream) { TScreenFilter::load(stream); }

void TOutlineFilter::drawFilter(JDrama::TGraphics *graphics) {

    u16 x  = 0;
    u16 y  = 0;
    u16 wd = 640;
    u16 ht = 448;

    Mtx e_m;
    Mtx44 m;

    GXTexObj tex_obj;

    GXColor tev_color = {0x03, 0x03, 0x03, 0x00};

    f32 f_left   = 0;
    f32 f_wd     = 640;
    f32 f_top    = 0;
    f32 f_ht     = 448;
    f32 f_right  = f_left + f_wd;
    f32 f_bottom = f_top + f_ht;
    f32 offset_x = (1.0f / f_wd);
    f32 offset_y = (1.0f / f_wd);

    GXSetTexCopySrc(f_left, f_top, f_wd, f_ht);
    GXSetCopyFilter(GX_FALSE, 0, GX_FALSE, nullptr);
    GXSetTexCopyDst(640 >> 1, 448 >> 1, GX_TF_Z16, GX_TRUE);
    GXCopyTex(gDepthBuffer, GX_FALSE);
    GXPixModeSync();
    GXInitTexObj(&tex_obj, gDepthBuffer, 640 >> 1, 448 >> 1, GX_TF_IA8, GX_CLAMP, GX_CLAMP, 0);
    GXInitTexObjLOD(&tex_obj, GX_NEAR, GX_NEAR, 0.0, 0.0, 0.0, GX_FALSE, GX_FALSE, GX_ANISO_1);

    C_MTXOrtho(m, f_top, f_bottom, f_left, f_right, 0.0f, 1.0f);
    PSMTXIdentity(e_m);
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GXSetVtxDesc(GX_VA_TEX1, GX_DIRECT);

    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_U16, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX1, GX_TEX_ST, GX_F32, 0);

    GXSetNumTexGens(2);
    GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, 0x3c, GX_FALSE, 0x7d);
    GXSetTexCoordGen2(GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_TEX1, 0x3c, GX_FALSE, 0x7d);

    GXLoadTexObj(&tex_obj, GX_TEXMAP0);
    GXLoadTexObj(&gpScreenTexture->texture->mTexObj, GX_TEXMAP1);

    GXSetNumTevStages(12);

    // First draw pass - far from camera

    // X- pass
    setupDepthMap(GX_TEVSTAGE0, 255, GX_TEVREG1, GX_TEXCOORD0);  // 0-5

    //// X+ pass
    setupDepthMap(GX_TEVSTAGE5, 255, GX_TEVREG2, GX_TEXCOORD1);  // 5-10

    GXSetTevOrder(GX_TEVSTAGE10, GX_TEXCOORD1, GX_TEXMAP0, 0xff);
    GXSetTevColorIn(GX_TEVSTAGE10, GX_CC_ZERO, GX_CC_ONE, GX_CC_C1, GX_CC_C2);
    GXSetTevColorOp(GX_TEVSTAGE10, GX_TEV_SUB, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevDirect(GX_TEVSTAGE10);

    GXSetTevOrder(GX_TEVSTAGE11, GX_TEXCOORD1, GX_TEXMAP0, 0xff);
    GXSetTevColorIn(GX_TEVSTAGE11, GX_CC_CPREV, GX_CC_KONST, GX_CC_ONE, GX_CC_ZERO);
    GXSetTevColorOp(GX_TEVSTAGE11, GX_TEV_COMP_R8_GT, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE,
                    GX_TEVPREV);
    GXSetTevDirect(GX_TEVSTAGE11);
    GXSetTevKColorSel(GX_TEVSTAGE11, GX_TEV_KCSEL_K0_R);

    GXColor fog_distance = {8, 0, 0, 0};
    GXSetTevKColor(GX_KCOLOR0, fog_distance);

    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_OR, GX_ALWAYS, 0);

    GXSetTevColor(GX_TEVREG0, tev_color);
    GXSetProjection(m, GX_ORTHOGRAPHIC);
    GXSetViewport(f_left, f_top, f_wd, f_ht, 0.0, 1.0);
    GXSetScissor(f_left, f_top, f_wd, f_ht);

    GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GXSetAlphaUpdate(GX_FALSE);
    GXSetColorUpdate(GX_TRUE);
    GXLoadPosMtxImm(e_m, 0);
    GXSetCurrentMtx(0);
    GXSetCullMode(GX_CULL_NONE);

    GXSetBlendMode(GX_BM_SUBTRACT, GX_BL_ZERO, GX_BL_ZERO, GX_LO_NOOP);
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition2u16(f_left, f_top);
    GXTexCoord2f32(-offset_x, 0.0f);
    GXTexCoord2f32(offset_x, 0.0f);
    GXPosition2u16(f_left + f_wd, f_top);
    GXTexCoord2f32(1.0f - offset_x, 0.0f);
    GXTexCoord2f32(1.0f + offset_x, 0.0f);
    GXPosition2u16(f_left + f_wd, f_top + f_ht);
    GXTexCoord2f32(1.0f - offset_x, 1.0f);
    GXTexCoord2f32(1.0f + offset_x, 1.0f);
    GXPosition2u16(f_left, f_top + f_ht);
    GXTexCoord2f32(-offset_x, 1.0f);
    GXTexCoord2f32(+offset_x, 1.0f);
    GXEnd();

    GXSetBlendMode(GX_BM_SUBTRACT, GX_BL_ZERO, GX_BL_ZERO, GX_LO_NOOP);
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition2u16(f_left, f_top);
    GXTexCoord2f32(offset_x, 0.0f);
    GXTexCoord2f32(-offset_x, 0.0f);
    GXPosition2u16(f_left + f_wd, f_top);
    GXTexCoord2f32(1.0f + offset_x, 0.0f);
    GXTexCoord2f32(1.0f - offset_x, 0.0f);
    GXPosition2u16(f_left + f_wd, f_top + f_ht);
    GXTexCoord2f32(1.0f + offset_x, 1.0f);
    GXTexCoord2f32(1.0f - offset_x, 1.0f);
    GXPosition2u16(f_left, f_top + f_ht);
    GXTexCoord2f32(+offset_x, 1.0f);
    GXTexCoord2f32(-offset_x, 1.0f);
    GXEnd();

    GXSetBlendMode(GX_BM_SUBTRACT, GX_BL_ZERO, GX_BL_ZERO, GX_LO_NOOP);
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition2u16(f_left, f_top);
    GXTexCoord2f32(0.0f, -offset_y);
    GXTexCoord2f32(0.0f, offset_y);
    GXPosition2u16(f_left + f_wd, f_top);
    GXTexCoord2f32(1.0f, -offset_y);
    GXTexCoord2f32(1.0f, offset_y);
    GXPosition2u16(f_left + f_wd, f_top + f_ht);
    GXTexCoord2f32(1.0f, 1.0f - offset_y);
    GXTexCoord2f32(1.0f, 1.0f + offset_y);
    GXPosition2u16(f_left, f_top + f_ht);
    GXTexCoord2f32(0.0f, 1.0f - offset_y);
    GXTexCoord2f32(0.0f, 1.0f + offset_y);
    GXEnd();
    GXSetBlendMode(GX_BM_SUBTRACT, GX_BL_ZERO, GX_BL_ZERO, GX_LO_NOOP);
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition2u16(f_left, f_top);
    GXTexCoord2f32(0.0f, offset_y);
    GXTexCoord2f32(0.0f, -offset_y);
    GXPosition2u16(f_left + f_wd, f_top);
    GXTexCoord2f32(1.0f, offset_y);
    GXTexCoord2f32(1.0f, -offset_y);
    GXPosition2u16(f_left + f_wd, f_top + f_ht);
    GXTexCoord2f32(1.0f, 1.0f + offset_y);
    GXTexCoord2f32(1.0f, 1.0f - offset_y);
    GXPosition2u16(f_left, f_top + f_ht);
    GXTexCoord2f32(0.0f, 1.0f + offset_y);
    GXTexCoord2f32(0.0f, 1.0f - offset_y);
    GXEnd();

    // Second draw pass - draw close to camera
    // X- pass
    setupDepthMap(GX_TEVSTAGE0, 64, GX_TEVREG1, GX_TEXCOORD0);  // 0-5

    //// X+ pass
    setupDepthMap(GX_TEVSTAGE5, 64, GX_TEVREG2, GX_TEXCOORD1);  // 5-10

    GXSetTevOrder(GX_TEVSTAGE10, GX_TEXCOORD1, GX_TEXMAP0, 0xff);
    GXSetTevColorIn(GX_TEVSTAGE10, GX_CC_ZERO, GX_CC_ONE, GX_CC_C1, GX_CC_C2);
    GXSetTevColorOp(GX_TEVSTAGE10, GX_TEV_SUB, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevDirect(GX_TEVSTAGE10);

    GXSetTevOrder(GX_TEVSTAGE11, GX_TEXCOORD1, GX_TEXMAP0, 0xff);
    GXSetTevColorIn(GX_TEVSTAGE11, GX_CC_CPREV, GX_CC_KONST, GX_CC_ONE, GX_CC_ZERO);
    GXSetTevColorOp(GX_TEVSTAGE11, GX_TEV_COMP_R8_GT, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE,
                    GX_TEVPREV);
    GXSetTevDirect(GX_TEVSTAGE11);
    GXSetTevKColorSel(GX_TEVSTAGE11, GX_TEV_KCSEL_K0_R);

    GXColor fog_distance2 = {8, 0, 0, 0};
    GXSetTevKColor(GX_KCOLOR0, fog_distance2);

    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_OR, GX_ALWAYS, 0);

    GXSetTevColor(GX_TEVREG0, tev_color);
    GXSetProjection(m, GX_ORTHOGRAPHIC);
    GXSetViewport(f_left, f_top, f_wd, f_ht, 0.0, 1.0);
    GXSetScissor(f_left, f_top, f_wd, f_ht);

    GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GXSetAlphaUpdate(GX_FALSE);
    GXSetColorUpdate(GX_TRUE);
    GXLoadPosMtxImm(e_m, 0);
    GXSetCurrentMtx(0);
    GXSetCullMode(GX_CULL_NONE);

    GXSetBlendMode(GX_BM_SUBTRACT, GX_BL_ZERO, GX_BL_ZERO, GX_LO_NOOP);
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition2u16(f_left, f_top);
    GXTexCoord2f32(-offset_x, 0.0f);
    GXTexCoord2f32(offset_x, 0.0f);
    GXPosition2u16(f_left + f_wd, f_top);
    GXTexCoord2f32(1.0f - offset_x, 0.0f);
    GXTexCoord2f32(1.0f + offset_x, 0.0f);
    GXPosition2u16(f_left + f_wd, f_top + f_ht);
    GXTexCoord2f32(1.0f - offset_x, 1.0f);
    GXTexCoord2f32(1.0f + offset_x, 1.0f);
    GXPosition2u16(f_left, f_top + f_ht);
    GXTexCoord2f32(-offset_x, 1.0f);
    GXTexCoord2f32(+offset_x, 1.0f);
    GXEnd();

    GXSetBlendMode(GX_BM_SUBTRACT, GX_BL_ZERO, GX_BL_ZERO, GX_LO_NOOP);
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition2u16(f_left, f_top);
    GXTexCoord2f32(offset_x, 0.0f);
    GXTexCoord2f32(-offset_x, 0.0f);
    GXPosition2u16(f_left + f_wd, f_top);
    GXTexCoord2f32(1.0f + offset_x, 0.0f);
    GXTexCoord2f32(1.0f - offset_x, 0.0f);
    GXPosition2u16(f_left + f_wd, f_top + f_ht);
    GXTexCoord2f32(1.0f + offset_x, 1.0f);
    GXTexCoord2f32(1.0f - offset_x, 1.0f);
    GXPosition2u16(f_left, f_top + f_ht);
    GXTexCoord2f32(+offset_x, 1.0f);
    GXTexCoord2f32(-offset_x, 1.0f);
    GXEnd();

    GXSetBlendMode(GX_BM_SUBTRACT, GX_BL_ZERO, GX_BL_ZERO, GX_LO_NOOP);
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition2u16(f_left, f_top);
    GXTexCoord2f32(0.0f, -offset_y);
    GXTexCoord2f32(0.0f, offset_y);
    GXPosition2u16(f_left + f_wd, f_top);
    GXTexCoord2f32(1.0f, -offset_y);
    GXTexCoord2f32(1.0f, offset_y);
    GXPosition2u16(f_left + f_wd, f_top + f_ht);
    GXTexCoord2f32(1.0f, 1.0f - offset_y);
    GXTexCoord2f32(1.0f, 1.0f + offset_y);
    GXPosition2u16(f_left, f_top + f_ht);
    GXTexCoord2f32(0.0f, 1.0f - offset_y);
    GXTexCoord2f32(0.0f, 1.0f + offset_y);
    GXEnd();
    GXSetBlendMode(GX_BM_SUBTRACT, GX_BL_ZERO, GX_BL_ZERO, GX_LO_NOOP);
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition2u16(f_left, f_top);
    GXTexCoord2f32(0.0f, offset_y);
    GXTexCoord2f32(0.0f, -offset_y);
    GXPosition2u16(f_left + f_wd, f_top);
    GXTexCoord2f32(1.0f, offset_y);
    GXTexCoord2f32(1.0f, -offset_y);
    GXPosition2u16(f_left + f_wd, f_top + f_ht);
    GXTexCoord2f32(1.0f, 1.0f + offset_y);
    GXTexCoord2f32(1.0f, 1.0f - offset_y);
    GXPosition2u16(f_left, f_top + f_ht);
    GXTexCoord2f32(0.0f, 1.0f + offset_y);
    GXTexCoord2f32(0.0f, 1.0f - offset_y);
    GXEnd();

    // Third draw pass - greyscale screen tex
    //
    GXSetNumTevStages(9);

    // Greyscale pass
    offset_x /= 4.0f;
    offset_y /= 4.0f;

    // Stage 0
    // Make greyscale
    GXColor tev_color1 = {76, 150, 29, 0};
    GXSetTevKColor(GX_KCOLOR1, tev_color1);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP1, 0xff);
    GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_TEXC, GX_CC_KONST, GX_CC_ZERO);
    GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVPREV);
    GXSetTevKColorSel(GX_TEVSTAGE0, GX_TEV_KCSEL_K1_R);
    GXSetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP1, GX_TEV_SWAP1);
    GXSetTevSwapModeTable(GX_TEV_SWAP1, GX_CH_RED, GX_CH_RED, GX_CH_RED, GX_CH_ALPHA);
    GXSetTevDirect(GX_TEVSTAGE0);

    GXSetTevOrder(GX_TEVSTAGE1, GX_TEXCOORD0, GX_TEXMAP1, 0xff);
    GXSetTevColorIn(GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_TEXC, GX_CC_KONST, GX_CC_CPREV);
    GXSetTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVPREV);
    GXSetTevKColorSel(GX_TEVSTAGE1, GX_TEV_KCSEL_K1_G);
    GXSetTevSwapMode(GX_TEVSTAGE1, GX_TEV_SWAP2, GX_TEV_SWAP2);
    GXSetTevSwapModeTable(GX_TEV_SWAP2, GX_CH_GREEN, GX_CH_GREEN, GX_CH_GREEN, GX_CH_ALPHA);
    GXSetTevDirect(GX_TEVSTAGE1);

    GXSetTevOrder(GX_TEVSTAGE2, GX_TEXCOORD0, GX_TEXMAP1, 0xff);
    GXSetTevColorIn(GX_TEVSTAGE2, GX_CC_ZERO, GX_CC_TEXC, GX_CC_KONST, GX_CC_CPREV);
    GXSetTevColorOp(GX_TEVSTAGE2, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVREG1);
    GXSetTevKColorSel(GX_TEVSTAGE2, GX_TEV_KCSEL_K1_B);
    GXSetTevSwapMode(GX_TEVSTAGE2, GX_TEV_SWAP3, GX_TEV_SWAP3);
    GXSetTevSwapModeTable(GX_TEV_SWAP3, GX_CH_BLUE, GX_CH_BLUE, GX_CH_BLUE, GX_CH_ALPHA);
    GXSetTevDirect(GX_TEVSTAGE2);

    GXSetTevOrder(GX_TEVSTAGE3, GX_TEXCOORD1, GX_TEXMAP1, 0xff);
    GXSetTevColorIn(GX_TEVSTAGE3, GX_CC_ZERO, GX_CC_TEXC, GX_CC_KONST, GX_CC_ZERO);
    GXSetTevColorOp(GX_TEVSTAGE3, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVPREV);
    GXSetTevKColorSel(GX_TEVSTAGE3, GX_TEV_KCSEL_K1_R);
    GXSetTevSwapMode(GX_TEVSTAGE3, GX_TEV_SWAP1, GX_TEV_SWAP1);
    GXSetTevSwapModeTable(GX_TEV_SWAP1, GX_CH_RED, GX_CH_RED, GX_CH_RED, GX_CH_ALPHA);
    GXSetTevDirect(GX_TEVSTAGE3);

    GXSetTevOrder(GX_TEVSTAGE4, GX_TEXCOORD1, GX_TEXMAP1, 0xff);
    GXSetTevColorIn(GX_TEVSTAGE4, GX_CC_ZERO, GX_CC_TEXC, GX_CC_KONST, GX_CC_CPREV);
    GXSetTevColorOp(GX_TEVSTAGE4, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVPREV);
    GXSetTevKColorSel(GX_TEVSTAGE4, GX_TEV_KCSEL_K1_G);
    GXSetTevSwapMode(GX_TEVSTAGE4, GX_TEV_SWAP2, GX_TEV_SWAP2);
    GXSetTevSwapModeTable(GX_TEV_SWAP2, GX_CH_GREEN, GX_CH_GREEN, GX_CH_GREEN, GX_CH_ALPHA);
    GXSetTevDirect(GX_TEVSTAGE4);

    GXSetTevOrder(GX_TEVSTAGE5, GX_TEXCOORD1, GX_TEXMAP1, 0xff);
    GXSetTevColorIn(GX_TEVSTAGE5, GX_CC_ZERO, GX_CC_TEXC, GX_CC_KONST, GX_CC_CPREV);
    GXSetTevColorOp(GX_TEVSTAGE5, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVREG2);
    GXSetTevKColorSel(GX_TEVSTAGE5, GX_TEV_KCSEL_K1_B);
    GXSetTevSwapMode(GX_TEVSTAGE5, GX_TEV_SWAP3, GX_TEV_SWAP3);
    GXSetTevSwapModeTable(GX_TEV_SWAP3, GX_CH_BLUE, GX_CH_BLUE, GX_CH_BLUE, GX_CH_ALPHA);
    GXSetTevDirect(GX_TEVSTAGE5);

    GXSetTevOrder(GX_TEVSTAGE6, GX_TEXCOORD1, GX_TEXMAP1, 0xff);
    GXSetTevColorIn(GX_TEVSTAGE6, GX_CC_ZERO, GX_CC_ONE, GX_CC_C1, GX_CC_C2);
    GXSetTevColorOp(GX_TEVSTAGE6, GX_TEV_SUB, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevDirect(GX_TEVSTAGE6);

    GXSetTevOrder(GX_TEVSTAGE7, GX_TEXCOORD1, GX_TEXMAP1, 0xff);
    GXSetTevColorIn(GX_TEVSTAGE7, GX_CC_CPREV, GX_CC_KONST, GX_CC_CPREV, GX_CC_ZERO);
    GXSetTevColorOp(GX_TEVSTAGE7, GX_TEV_COMP_R8_GT, GX_TB_ZERO, GX_CS_SCALE_4, GX_TRUE,
                    GX_TEVPREV);
    GXSetTevDirect(GX_TEVSTAGE7);
    GXSetTevKColorSel(GX_TEVSTAGE7, GX_TEV_KCSEL_K0_R);

    GXSetTevOrder(GX_TEVSTAGE8, GX_TEXCOORD1, GX_TEXMAP1, 0xff);
    GXSetTevColorIn(GX_TEVSTAGE8, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_CPREV);
    GXSetTevColorOp(GX_TEVSTAGE8, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_4, GX_TRUE, GX_TEVPREV);
    GXSetTevDirect(GX_TEVSTAGE8);
    GXSetTevOrder(GX_TEVSTAGE9, GX_TEXCOORD1, GX_TEXMAP1, 0xff);
    GXSetTevColorIn(GX_TEVSTAGE9, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_CPREV);
    GXSetTevColorOp(GX_TEVSTAGE9, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_2, GX_TRUE, GX_TEVPREV);
    GXSetTevDirect(GX_TEVSTAGE9);

    GXColor fog_distance3 = {2, 0, 0, 0};
    GXSetTevKColor(GX_KCOLOR0, fog_distance3);

    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_OR, GX_ALWAYS, 0);

    GXSetTevColor(GX_TEVREG0, tev_color);
    GXSetProjection(m, GX_ORTHOGRAPHIC);
    GXSetViewport(f_left, f_top, f_wd, f_ht, 0.0, 1.0);
    GXSetScissor(f_left, f_top, f_wd, f_ht);

    GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GXSetAlphaUpdate(GX_FALSE);
    GXSetColorUpdate(GX_TRUE);
    GXLoadPosMtxImm(e_m, 0);
    GXSetCurrentMtx(0);
    GXSetCullMode(GX_CULL_NONE);

    GXSetBlendMode(GX_BM_SUBTRACT, GX_BL_ZERO, GX_BL_ZERO, GX_LO_NOOP);
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition2u16(f_left, f_top);
    GXTexCoord2f32(-offset_x, 0.0f);
    GXTexCoord2f32(offset_x, 0.0f);
    GXPosition2u16(f_left + f_wd, f_top);
    GXTexCoord2f32(1.0f - offset_x, 0.0f);
    GXTexCoord2f32(1.0f + offset_x, 0.0f);
    GXPosition2u16(f_left + f_wd, f_top + f_ht);
    GXTexCoord2f32(1.0f - offset_x, 1.0f);
    GXTexCoord2f32(1.0f + offset_x, 1.0f);
    GXPosition2u16(f_left, f_top + f_ht);
    GXTexCoord2f32(-offset_x, 1.0f);
    GXTexCoord2f32(+offset_x, 1.0f);
    GXEnd();

    GXSetBlendMode(GX_BM_SUBTRACT, GX_BL_ZERO, GX_BL_ZERO, GX_LO_NOOP);
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition2u16(f_left, f_top);
    GXTexCoord2f32(0.0f, -offset_y);
    GXTexCoord2f32(0.0f, offset_y);
    GXPosition2u16(f_left + f_wd, f_top);
    GXTexCoord2f32(1.0f, -offset_y);
    GXTexCoord2f32(1.0f, offset_y);
    GXPosition2u16(f_left + f_wd, f_top + f_ht);
    GXTexCoord2f32(1.0f, 1.0f - offset_y);
    GXTexCoord2f32(1.0f, 1.0f + offset_y);
    GXPosition2u16(f_left, f_top + f_ht);
    GXTexCoord2f32(0.0f, 1.0f - offset_y);
    GXTexCoord2f32(0.0f, 1.0f + offset_y);
    GXEnd();
}


u8 outlineDepth = 32;

void TSubtleOutline::drawFilter(JDrama::TGraphics *graphics) {
    

    //if (gpApplication.mGamePads[0]->mButtons.mInput & JUTGamePad::R) {
    //    outlineDepth++;
    //}

    //if (gpApplication.mGamePads[0]->mButtons.mInput & JUTGamePad::L) {
    //    outlineDepth--;
    //}

    Mtx e_m;
    Mtx44 m;

    GXTexObj tex_obj;

    GXColor tev_color = {0x03, 0x03, 0x03, 0x00};

    f32 f_left   = 0;
    f32 f_wd     = 640;
    f32 f_top    = 0;
    f32 f_ht     = 448;
    f32 f_right  = f_left + f_wd;
    f32 f_bottom = f_top + f_ht;
    f32 offset_x = (1.0f / f_wd);
    f32 offset_y = (1.0f / f_ht);

    GXSetTexCopySrc(f_left, f_top, f_wd, f_ht);
    GXSetCopyFilter(GX_FALSE, 0, GX_FALSE, nullptr);
    GXSetTexCopyDst(640 >> 1, 448 >> 1, GX_TF_Z16, GX_TRUE);
    GXCopyTex(gDepthBuffer, GX_FALSE);
    GXPixModeSync();
    GXInitTexObj(&tex_obj, gDepthBuffer, 640 >> 1, 448 >> 1, GX_TF_IA8, GX_CLAMP, GX_CLAMP, 0);
    GXInitTexObjLOD(&tex_obj, GX_NEAR, GX_NEAR, 0.0, 0.0, 0.0, GX_FALSE, GX_FALSE, GX_ANISO_1);

    C_MTXOrtho(m, f_top, f_bottom, f_left, f_right, 0.0f, 1.0f);
    PSMTXIdentity(e_m);
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GXSetVtxDesc(GX_VA_TEX1, GX_DIRECT);

    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_U16, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX1, GX_TEX_ST, GX_F32, 0);

    GXSetNumChans(0);
    GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_REG, 0, GX_DF_NONE, GX_AF_NONE);
    GXSetChanCtrl(GX_COLOR1A1, GX_FALSE, GX_SRC_REG, GX_SRC_REG, 0, GX_DF_NONE, GX_AF_NONE);

    GXSetNumTexGens(2);
    GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, 0x3c, GX_FALSE, 0x7d);
    GXSetTexCoordGen2(GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_TEX1, 0x3c, GX_FALSE, 0x7d);

    GXLoadTexObj(&tex_obj, GX_TEXMAP0);

    // Edge detect on IA8 intensity (closest-focused band only):
    // scale each tap first, then subtract, then threshold.
    GXSetNumTevStages(12);
    setupDepthMap(GX_TEVSTAGE0, outlineDepth, GX_TEVREG0, GX_TEXCOORD0);  // 0-4 inclusive
    
    setupDepthMap(GX_TEVSTAGE5, outlineDepth, GX_TEVREG1, GX_TEXCOORD1);  // 0-4 inclusive

    
    GXSetTevOrder(GX_TEVSTAGE10, GX_TEXCOORD0, GX_TEXMAP0, 0xff);
    GXSetTevColorIn(GX_TEVSTAGE10, GX_CC_ZERO, GX_CC_ONE, GX_CC_C1, GX_CC_C0);
    GXSetTevColorOp(GX_TEVSTAGE10, GX_TEV_SUB, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevDirect(GX_TEVSTAGE10);
    
    GXSetTevOrder(GX_TEVSTAGE11, GX_TEXCOORD1, GX_TEXMAP0, 0xff);
    GXSetTevColorIn(GX_TEVSTAGE11, GX_CC_CPREV, GX_CC_KONST, GX_CC_CPREV, GX_CC_ZERO);
    GXSetTevColorOp(GX_TEVSTAGE11, GX_TEV_COMP_R8_GT, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE,
                    GX_TEVPREV);
    GXSetTevDirect(GX_TEVSTAGE11);
    GXSetTevKColorSel(GX_TEVSTAGE11, GX_TEV_KCSEL_K0_R);

    GXColor edgeThreshold = {4, 0, 0, 0};
    GXSetTevKColor(GX_KCOLOR0, edgeThreshold);

    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_OR, GX_ALWAYS, 0);

    GXSetTevColor(GX_TEVREG0, tev_color);
    GXSetProjection(m, GX_ORTHOGRAPHIC);
    GXSetViewport(f_left, f_top, f_wd, f_ht, 0.0, 1.0);
    GXSetScissor(f_left, f_top, f_wd, f_ht);

    GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GXSetAlphaUpdate(GX_FALSE);
    GXSetColorUpdate(GX_TRUE);
    GXLoadPosMtxImm(e_m, 0);
    GXSetCurrentMtx(0);
    GXSetCullMode(GX_CULL_NONE);
    
    GXSetBlendMode(GX_BM_SUBTRACT, GX_BL_ZERO, GX_BL_ZERO, GX_LO_NOOP);
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition2u16(f_left, f_top);
    GXTexCoord2f32(-offset_x, 0.0f);
    GXTexCoord2f32(offset_x, 0.0f);
    GXPosition2u16(f_left + f_wd, f_top);
    GXTexCoord2f32(1.0f - offset_x, 0.0f);
    GXTexCoord2f32(1.0f + offset_x, 0.0f);
    GXPosition2u16(f_left + f_wd, f_top + f_ht);
    GXTexCoord2f32(1.0f - offset_x, 1.0f);
    GXTexCoord2f32(1.0f + offset_x, 1.0f);
    GXPosition2u16(f_left, f_top + f_ht);
    GXTexCoord2f32(-offset_x, 1.0f);
    GXTexCoord2f32(+offset_x, 1.0f);
    GXEnd();

    GXSetBlendMode(GX_BM_SUBTRACT, GX_BL_ZERO, GX_BL_ZERO, GX_LO_NOOP);
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition2u16(f_left, f_top);
    GXTexCoord2f32(offset_x, 0.0f);
    GXTexCoord2f32(-offset_x, 0.0f);
    GXPosition2u16(f_left + f_wd, f_top);
    GXTexCoord2f32(1.0f + offset_x, 0.0f);
    GXTexCoord2f32(1.0f - offset_x, 0.0f);
    GXPosition2u16(f_left + f_wd, f_top + f_ht);
    GXTexCoord2f32(1.0f + offset_x, 1.0f);
    GXTexCoord2f32(1.0f - offset_x, 1.0f);
    GXPosition2u16(f_left, f_top + f_ht);
    GXTexCoord2f32(+offset_x, 1.0f);
    GXTexCoord2f32(-offset_x, 1.0f);
    GXEnd();

    GXSetBlendMode(GX_BM_SUBTRACT, GX_BL_ZERO, GX_BL_ZERO, GX_LO_NOOP);
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition2u16(f_left, f_top);
    GXTexCoord2f32(0.0f, -offset_y);
    GXTexCoord2f32(0.0f, offset_y);
    GXPosition2u16(f_left + f_wd, f_top);
    GXTexCoord2f32(1.0f, -offset_y);
    GXTexCoord2f32(1.0f, offset_y);
    GXPosition2u16(f_left + f_wd, f_top + f_ht);
    GXTexCoord2f32(1.0f, 1.0f - offset_y);
    GXTexCoord2f32(1.0f, 1.0f + offset_y);
    GXPosition2u16(f_left, f_top + f_ht);
    GXTexCoord2f32(0.0f, 1.0f - offset_y);
    GXTexCoord2f32(0.0f, 1.0f + offset_y);
    GXEnd();
    GXSetBlendMode(GX_BM_SUBTRACT, GX_BL_ZERO, GX_BL_ZERO, GX_LO_NOOP);
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition2u16(f_left, f_top);
    GXTexCoord2f32(0.0f, offset_y);
    GXTexCoord2f32(0.0f, -offset_y);
    GXPosition2u16(f_left + f_wd, f_top);
    GXTexCoord2f32(1.0f, offset_y);
    GXTexCoord2f32(1.0f, -offset_y);
    GXPosition2u16(f_left + f_wd, f_top + f_ht);
    GXTexCoord2f32(1.0f, 1.0f + offset_y);
    GXTexCoord2f32(1.0f, 1.0f - offset_y);
    GXPosition2u16(f_left, f_top + f_ht);
    GXTexCoord2f32(0.0f, 1.0f + offset_y);
    GXTexCoord2f32(0.0f, 1.0f - offset_y);
    GXEnd();
}

/*

void TOutlineFilter::drawFilter() {
    u16 x = 0;
    u16 y = 0;
    u16 wd = 640;
    u16 ht = 448;

    Mtx e_m;
    Mtx44 m;

    GXTexObj tex_obj;

    GXColor tev_color = { 0x03, 0x03, 0x03, 0x00 };
    u8 vFilter[7]     = { 0x15, 0x00, 0x00, 0x16, 0x00, 0x00, 0x15 };

    f32 f_left   = 0;
    f32 f_wd     = 640;
    f32 f_top    = 0;
    f32 f_ht     = 448;
    f32 f_right  = f_left + f_wd;
    f32 f_bottom = f_top + f_ht;
    f32 offset_x = (0.5f / f_wd);
    f32 offset_y = (0.5f / f_wd);

    GXSetTexCopySrc(f_left, f_top, f_wd, f_ht);
    GXSetCopyFilter(GX_FALSE, 0, GX_TRUE, vFilter);
    GXSetTexCopyDst(640 >> 1, 448 >> 1, GX_TF_Z16, GX_TRUE);
    //GXSetTexCopyDst(wd >> 1, ht >> 1, GX_CTF_Z8M, GX_TRUE);
    GXCopyTex(gDepthBuffer, GX_FALSE);
    GXPixModeSync();

    GXInitTexObj(&tex_obj, gDepthBuffer, 640 >> 1, 448 >> 1, GX_TF_IA8, GX_CLAMP,
                 GX_CLAMP, 0);
    GXInitTexObjLOD(&tex_obj, GX_NEAR, GX_NEAR, 0.0, 0.0, 0.0, GX_FALSE,
                    GX_FALSE, GX_ANISO_1);

    C_MTXOrtho(m, f_top, f_bottom, f_left, f_right, 0.0f, 1.0f);
    PSMTXIdentity(e_m);
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GXSetVtxDesc(GX_VA_TEX1, GX_DIRECT);

    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_U16, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX1, GX_TEX_ST, GX_F32, 0);

    GXSetNumChans(0);
    GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_REG, 0, GX_DF_NONE,
                  GX_AF_NONE);
    GXSetChanCtrl(GX_COLOR1A1, GX_FALSE, GX_SRC_REG, GX_SRC_REG, 0, GX_DF_NONE,
                  GX_AF_NONE);

    GXSetNumTexGens(4);
    GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, 0x3c, GX_FALSE,
                      0x7d);
    GXSetTexCoordGen2(GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_TEX1, 0x3c, GX_FALSE,
                      0x7d);

    //GXLoadTexObj(&tex_obj, GX_TEXMAP0);
    GXLoadTexObj(&gpScreenTexture->texture->mTexObj, GX_TEXMAP0);

    GXSetNumTevStages(12);

    // -x pass
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, 0xff);
    GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ONE, GX_CC_TEXC, GX_CC_ONE);
    GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_SUB, GX_TB_ZERO, GX_CS_SCALE_1,
                    GX_FALSE, GX_TEVPREV);
    GXSetTevDirect(GX_TEVSTAGE0);
    GXSetTevOrder(GX_TEVSTAGE1, GX_TEXCOORD0, GX_TEXMAP0, 0xff);
    GXSetTevColorIn(GX_TEVSTAGE1, GX_CC_TEXC, GX_CC_ZERO, GX_CC_CPREV, GX_CC_ZERO);
    GXSetTevColorOp(GX_TEVSTAGE1, GX_TEV_COMP_R8_GT, GX_TB_ZERO, GX_CS_SCALE_1,
                    GX_FALSE, GX_TEVPREV);
    GXSetTevDirect(GX_TEVSTAGE1);
    GXSetTevOrder(GX_TEVSTAGE2, GX_TEXCOORD0, GX_TEXMAP0, 0xff);
    GXSetTevColorIn(GX_TEVSTAGE2, GX_CC_ZERO, GX_CC_KONST, GX_CC_CPREV, GX_CC_KONST);
    GXSetTevColorOp(GX_TEVSTAGE2, GX_TEV_SUB, GX_TB_ZERO, GX_CS_SCALE_1,
                    GX_FALSE, GX_TEVREG0);
    GXSetTevDirect(GX_TEVSTAGE2);
       GXSetTevKColorSel(GX_TEVSTAGE2, GX_TEV_KCSEL_7_8);

    // Upper 8 bits change most closest to camera, lower 8 bits change most far from camera, but
repeat close to camera due to upper overflowing into upper bits
    // This basically only keeps first band of middle bits, we do this by verifying that upper bits
is 1 (meaning they are not written to) GXSetTevOrder(GX_TEVSTAGE3, GX_TEXCOORD0, GX_TEXMAP0, 0xff);
    GXSetTevColorIn(GX_TEVSTAGE3, GX_CC_ONE, GX_CC_TEXC, GX_CC_TEXA, GX_CC_ZERO);
    GXSetTevColorOp(GX_TEVSTAGE3, GX_TEV_COMP_R8_EQ, GX_TB_ZERO, GX_CS_SCALE_1,
                    GX_FALSE, GX_TEVPREV);
    GXSetTevDirect(GX_TEVSTAGE3);
    GXSetTevOrder(GX_TEVSTAGE4, GX_TEXCOORD0, GX_TEXMAP0, 0xff);
    GXSetTevColorIn(GX_TEVSTAGE4, GX_CC_ZERO, GX_CC_KONST, GX_CC_CPREV, GX_CC_C0);
    GXSetTevColorOp(GX_TEVSTAGE4, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1,
                    GX_TRUE, GX_TEVREG2);
    GXSetTevDirect(GX_TEVSTAGE4);
       GXSetTevKColorSel(GX_TEVSTAGE4, GX_TEV_KCSEL_1_8);


    // +x pass
    GXSetTevOrder(GX_TEVSTAGE5, GX_TEXCOORD1, GX_TEXMAP0, 0xff);
    GXSetTevColorIn(GX_TEVSTAGE5, GX_CC_ZERO, GX_CC_ONE, GX_CC_TEXC, GX_CC_ONE);
    GXSetTevColorOp(GX_TEVSTAGE5, GX_TEV_SUB, GX_TB_ZERO, GX_CS_SCALE_1,
                    GX_FALSE, GX_TEVPREV);
    GXSetTevDirect(GX_TEVSTAGE5);
    GXSetTevOrder(GX_TEVSTAGE6, GX_TEXCOORD1, GX_TEXMAP0, 0xff);
    GXSetTevColorIn(GX_TEVSTAGE6, GX_CC_TEXC, GX_CC_ZERO, GX_CC_CPREV, GX_CC_ZERO);
    GXSetTevColorOp(GX_TEVSTAGE6, GX_TEV_COMP_R8_GT, GX_TB_ZERO, GX_CS_SCALE_1,
                    GX_FALSE, GX_TEVPREV);
    GXSetTevDirect(GX_TEVSTAGE6);
    GXSetTevOrder(GX_TEVSTAGE7, GX_TEXCOORD1, GX_TEXMAP0, 0xff);
    GXSetTevColorIn(GX_TEVSTAGE7, GX_CC_ZERO, GX_CC_KONST, GX_CC_CPREV, GX_CC_KONST);
    GXSetTevColorOp(GX_TEVSTAGE7, GX_TEV_SUB, GX_TB_ZERO, GX_CS_SCALE_1,
                    GX_FALSE, GX_TEVREG0);
    GXSetTevDirect(GX_TEVSTAGE7);
       GXSetTevKColorSel(GX_TEVSTAGE7, GX_TEV_KCSEL_7_8);

    // Upper 8 bits change most closest to camera, lower 8 bits change most far from camera, but
repeat close to camera due to upper overflowing into upper bits
    // This basically only keeps first band of middle bits, we do this by verifying that upper bits
is 1 (meaning they are not written to) GXSetTevOrder(GX_TEVSTAGE8, GX_TEXCOORD1, GX_TEXMAP0, 0xff);
    GXSetTevColorIn(GX_TEVSTAGE8, GX_CC_ONE, GX_CC_TEXC, GX_CC_TEXA, GX_CC_ZERO);
    GXSetTevColorOp(GX_TEVSTAGE8, GX_TEV_COMP_R8_EQ, GX_TB_ZERO, GX_CS_SCALE_1,
                    GX_FALSE, GX_TEVPREV);
    GXSetTevDirect(GX_TEVSTAGE8);

    GXSetTevOrder(GX_TEVSTAGE9, GX_TEXCOORD1, GX_TEXMAP0, 0xff);
    GXSetTevColorIn(GX_TEVSTAGE9, GX_CC_ZERO, GX_CC_KONST, GX_CC_CPREV, GX_CC_C0);
    GXSetTevColorOp(GX_TEVSTAGE9, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1,
                    GX_TRUE, GX_TEVREG1);
    GXSetTevDirect(GX_TEVSTAGE9);
       GXSetTevKColorSel(GX_TEVSTAGE9, GX_TEV_KCSEL_1_8);

    // Edge detection

    GXSetTevOrder(GX_TEVSTAGE10, GX_TEXCOORD1, GX_TEXMAP0, 0xff);
    GXSetTevColorIn(GX_TEVSTAGE10, GX_CC_ZERO, GX_CC_ONE, GX_CC_C1, GX_CC_C2);
    GXSetTevColorOp(GX_TEVSTAGE10, GX_TEV_SUB, GX_TB_ZERO, GX_CS_SCALE_1,
                    GX_TRUE, GX_TEVPREV);
    GXSetTevDirect(GX_TEVSTAGE10);

    GXSetTevOrder(GX_TEVSTAGE11, GX_TEXCOORD1, GX_TEXMAP0, 0xff);
    GXSetTevColorIn(GX_TEVSTAGE11, GX_CC_CPREV, GX_CC_KONST, GX_CC_ONE, GX_CC_ZERO);
    GXSetTevColorOp(GX_TEVSTAGE11, GX_TEV_COMP_R8_GT, GX_TB_ZERO, GX_CS_SCALE_1,
                    GX_TRUE, GX_TEVPREV);
    GXSetTevDirect(GX_TEVSTAGE11);
     GXSetTevKColorSel(GX_TEVSTAGE11, GX_TEV_KCSEL_K0_R);

    GXColor fog_distance = { 32, 0, 0, 0 };
    GXSetTevKColor(GX_KCOLOR0, fog_distance);

    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_OR, GX_ALWAYS, 0);

    GXSetTevColor(GX_TEVREG0, tev_color);
    GXSetProjection(m, GX_ORTHOGRAPHIC);
    GXSetViewport(f_left, f_top, f_wd, f_ht, 0.0, 1.0);
    GXSetScissor(f_left, f_top, f_wd, f_ht);

    GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GXSetAlphaUpdate(GX_FALSE);
    GXSetColorUpdate(GX_TRUE);
    GXLoadPosMtxImm(e_m, 0);
    GXSetCurrentMtx(0);
    GXSetCullMode(GX_CULL_NONE);

    GXSetBlendMode(GX_BM_SUBTRACT, GX_BL_ZERO, GX_BL_ZERO, GX_LO_NOOP);
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition2u16(f_left, f_top);
    GXTexCoord2f32(-offset_x, 0.0f);
    GXTexCoord2f32(offset_x, 0.0f);
    GXPosition2u16(f_left + f_wd, f_top);
    GXTexCoord2f32(1.0f - offset_x, 0.0f);
    GXTexCoord2f32(1.0f + offset_x, 0.0f);
    GXPosition2u16(f_left + f_wd, f_top + f_ht);
    GXTexCoord2f32(1.0f - offset_x, 1.0f);
    GXTexCoord2f32(1.0f + offset_x, 1.0f);
    GXPosition2u16(f_left, f_top + f_ht);
    GXTexCoord2f32(-offset_x, 1.0f);
    GXTexCoord2f32(+offset_x, 1.0f);
    GXEnd();

    GXSetBlendMode(GX_BM_SUBTRACT, GX_BL_ZERO, GX_BL_ZERO, GX_LO_NOOP);
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition2u16(f_left, f_top);
    GXTexCoord2f32(offset_x, 0.0f);
    GXTexCoord2f32(-offset_x, 0.0f);
    GXPosition2u16(f_left + f_wd, f_top);
    GXTexCoord2f32(1.0f + offset_x, 0.0f);
    GXTexCoord2f32(1.0f - offset_x, 0.0f);
    GXPosition2u16(f_left + f_wd, f_top + f_ht);
    GXTexCoord2f32(1.0f + offset_x, 1.0f);
    GXTexCoord2f32(1.0f - offset_x, 1.0f);
    GXPosition2u16(f_left, f_top + f_ht);
    GXTexCoord2f32(+offset_x, 1.0f);
    GXTexCoord2f32(-offset_x, 1.0f);
    GXEnd();

    GXSetBlendMode(GX_BM_SUBTRACT, GX_BL_ZERO, GX_BL_ZERO, GX_LO_NOOP);
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition2u16(f_left, f_top);
    GXTexCoord2f32(0.0f, -offset_y);
    GXTexCoord2f32(0.0f, offset_y);
    GXPosition2u16(f_left + f_wd, f_top);
    GXTexCoord2f32(1.0f, -offset_y);
    GXTexCoord2f32(1.0f, offset_y);
    GXPosition2u16(f_left + f_wd, f_top + f_ht);
    GXTexCoord2f32(1.0f, 1.0f - offset_y);
    GXTexCoord2f32(1.0f, 1.0f + offset_y);
    GXPosition2u16(f_left, f_top + f_ht);
    GXTexCoord2f32(0.0f, 1.0f - offset_y);
    GXTexCoord2f32(0.0f, 1.0f + offset_y);
    GXEnd();
    GXSetBlendMode(GX_BM_SUBTRACT, GX_BL_ZERO, GX_BL_ZERO, GX_LO_NOOP);
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition2u16(f_left, f_top);
    GXTexCoord2f32(0.0f, offset_y);
    GXTexCoord2f32(0.0f, -offset_y);
    GXPosition2u16(f_left + f_wd, f_top);
    GXTexCoord2f32(1.0f, offset_y);
    GXTexCoord2f32(1.0f, -offset_y);
    GXPosition2u16(f_left + f_wd, f_top + f_ht);
    GXTexCoord2f32(1.0f, 1.0f + offset_y);
    GXTexCoord2f32(1.0f, 1.0f - offset_y);
    GXPosition2u16(f_left, f_top + f_ht);
    GXTexCoord2f32(0.0f, 1.0f + offset_y);
    GXTexCoord2f32(0.0f, 1.0f - offset_y);
    GXEnd();

}



*/

void TSpookyFilter::drawFilter(JDrama::TGraphics *graphics) {
    u16 x  = 0;
    u16 y  = 0;
    u16 wd = 640;
    u16 ht = 448;

    Mtx e_m;
    Mtx44 m;

    GXTexObj tex_obj;

    GXColor tev_color = {0x03, 0x03, 0x03, 0x00};

    f32 f_left   = 0;
    f32 f_wd     = 640;
    f32 f_top    = 0;
    f32 f_ht     = 448;
    f32 f_right  = f_left + f_wd;
    f32 f_bottom = f_top + f_ht;

    GXSetTexCopySrc(f_left, f_top, f_wd, f_ht);
    GXSetCopyFilter(GX_FALSE, 0, GX_FALSE, nullptr);
    GXSetTexCopyDst(640 >> 1, 448 >> 1, GX_TF_Z16, GX_TRUE);
    GXCopyTex(gDepthBuffer, GX_FALSE);
    GXPixModeSync();
    GXInitTexObj(&tex_obj, gDepthBuffer, 640 >> 1, 448 >> 1, GX_TF_IA8, GX_CLAMP, GX_CLAMP, 0);
    GXInitTexObjLOD(&tex_obj, GX_NEAR, GX_NEAR, 0.0, 0.0, 0.0, GX_FALSE, GX_FALSE, GX_ANISO_1);

    C_MTXOrtho(m, f_top, f_bottom, f_left, f_right, 0.0f, 1.0f);
    PSMTXIdentity(e_m);
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);

    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_U16, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);

    GXSetNumTexGens(1);
    GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, 0x3c, GX_FALSE, 0x7d);

    GXLoadTexObj(&tex_obj, GX_TEXMAP0);
    GXLoadTexObj(&gpScreenTexture->texture->mTexObj, GX_TEXMAP1);

    GXSetNumTevStages(7);

    setupDepthMap(GX_TEVSTAGE0, 200, GX_TEVREG0, GX_TEXCOORD0);  // 0-4 inclusive

    GXSetTevOrder(GX_TEVSTAGE5, GX_TEXCOORD0, GX_TEXMAP1, 0xff);
    GXSetTevColorIn(GX_TEVSTAGE5, GX_CC_KONST, GX_CC_ZERO, GX_CC_TEXC, GX_CC_KONST);
    GXSetTevColorOp(GX_TEVSTAGE5, GX_TEV_SUB, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevDirect(GX_TEVSTAGE5);
    GXSetTevKColorSel(GX_TEVSTAGE5, GX_TEV_KCSEL_K0);

    GXSetTevOrder(GX_TEVSTAGE6, GX_TEXCOORD0, GX_TEXMAP1, 0xff);
    GXSetTevColorIn(GX_TEVSTAGE6, GX_CC_ZERO, GX_CC_C0, GX_CC_CPREV, GX_CC_ZERO);
    GXSetTevColorOp(GX_TEVSTAGE6, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevDirect(GX_TEVSTAGE6);

    GXColor fogColor = {129, 81, 138, 255};
    GXSetTevKColor(GX_KCOLOR0, fogColor);

    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_OR, GX_ALWAYS, 0);

    GXSetTevColor(GX_TEVREG0, tev_color);
    GXSetProjection(m, GX_ORTHOGRAPHIC);
    GXSetViewport(f_left, f_top, f_wd, f_ht, 0.0, 1.0);
    GXSetScissor(f_left, f_top, f_wd, f_ht);

    GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GXSetAlphaUpdate(GX_FALSE);
    GXSetColorUpdate(GX_TRUE);
    GXLoadPosMtxImm(e_m, 0);
    GXSetCurrentMtx(0);
    GXSetCullMode(GX_CULL_NONE);

    GXSetBlendMode(GX_BM_NONE, GX_BL_ZERO, GX_BL_ZERO, GX_LO_NOOP);
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition2u16(f_left, f_top);
    GXTexCoord2f32(0.0f, 0.0f);
    GXPosition2u16(f_left + f_wd, f_top);
    GXTexCoord2f32(1.0f, 0.0f);
    GXPosition2u16(f_left + f_wd, f_top + f_ht);
    GXTexCoord2f32(1.0f, 1.0f);
    GXPosition2u16(f_left, f_top + f_ht);
    GXTexCoord2f32(0.0f, 1.0f);
    GXEnd();
}

u8 focalDepthMin = 32;
u8 focalDepthMax = 255;

void TDepthOfField::drawFilter(JDrama::TGraphics *graphics) {

    if (gpApplication.mGamePads[0]->mButtons.mInput & JUTGamePad::DPAD_LEFT) {
        focalDepthMin--;
    }

    if (gpApplication.mGamePads[0]->mButtons.mInput & JUTGamePad::DPAD_RIGHT) {
        focalDepthMin++;
    }

    if (gpApplication.mGamePads[0]->mButtons.mInput & JUTGamePad::R) {
        focalDepthMax++;
    }

    if (gpApplication.mGamePads[0]->mButtons.mInput & JUTGamePad::L) {
        focalDepthMax--;
    }

    OSReport("min %d max %d\n", focalDepthMin, focalDepthMax);

    Mtx e_m;
    Mtx44 m;

    GXTexObj tex_obj;

    GXColor tev_color = {0x03, 0x03, 0x03, 0x00};

    f32 f_left   = 0;
    f32 f_wd     = 640;
    f32 f_top    = 0;
    f32 f_ht     = 448;
    f32 f_right  = f_left + f_wd;
    f32 f_bottom = f_top + f_ht;
    f32 offset_x = (1.5f / f_wd);
    f32 offset_y = (1.5f / f_wd);

    GXSetTexCopySrc(f_left, f_top, f_wd, f_ht);
    GXSetCopyFilter(GX_FALSE, 0, GX_FALSE, nullptr);
    GXSetTexCopyDst(640 >> 1, 448 >> 1, GX_TF_Z16, GX_TRUE);
    GXCopyTex(gDepthBuffer, GX_FALSE);
    GXPixModeSync();
    GXInitTexObj(&tex_obj, gDepthBuffer, 640 >> 1, 448 >> 1, GX_TF_IA8, GX_CLAMP, GX_CLAMP, 0);
    GXInitTexObjLOD(&tex_obj, GX_NEAR, GX_NEAR, 0.0, 0.0, 0.0, GX_FALSE, GX_FALSE, GX_ANISO_1);

    C_MTXOrtho(m, f_top, f_bottom, f_left, f_right, 0.0f, 1.0f);
    PSMTXIdentity(e_m);
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GXSetVtxDesc(GX_VA_TEX1, GX_DIRECT);
    GXSetVtxDesc(GX_VA_TEX2, GX_DIRECT);
    GXSetVtxDesc(GX_VA_TEX3, GX_DIRECT);
    GXSetVtxDesc(GX_VA_TEX4, GX_DIRECT);

    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_U16, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX1, GX_TEX_ST, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX2, GX_TEX_ST, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX3, GX_TEX_ST, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX4, GX_TEX_ST, GX_F32, 0);

    GXSetNumTexGens(5);
    GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, 0x3c, GX_FALSE, 0x7d);
    GXSetTexCoordGen2(GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_TEX1, 0x3c, GX_FALSE, 0x7d);
    GXSetTexCoordGen2(GX_TEXCOORD2, GX_TG_MTX2x4, GX_TG_TEX2, 0x3c, GX_FALSE, 0x7d);
    GXSetTexCoordGen2(GX_TEXCOORD3, GX_TG_MTX2x4, GX_TG_TEX3, 0x3c, GX_FALSE, 0x7d);
    GXSetTexCoordGen2(GX_TEXCOORD4, GX_TG_MTX2x4, GX_TG_TEX4, 0x3c, GX_FALSE, 0x7d);

    GXLoadTexObj(&gpScreenTexture->texture->mTexObj, GX_TEXMAP0);
    GXLoadTexObj(&tex_obj, GX_TEXMAP1);

    GXSetNumTevStages(11);

    setupDepthMap(GX_TEVSTAGE0, focalDepthMax, GX_TEVREG1, GX_TEXCOORD0, GX_TEXMAP1);  // 0-5

    // Stage 0
    GXSetTevOrder(GX_TEVSTAGE5, GX_TEXCOORD1, GX_TEXMAP0, 0xFF);
    GXSetTevColorIn(GX_TEVSTAGE5, GX_CC_ZERO, GX_CC_TEXC, GX_CC_HALF, GX_CC_C0);
    GXSetTevColorOp(GX_TEVSTAGE5, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVPREV);
    GXSetTevAlphaIn(GX_TEVSTAGE5, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
    GXSetTevAlphaOp(GX_TEVSTAGE5, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevDirect(GX_TEVSTAGE5);

    // Stage 1
    GXSetTevOrder(GX_TEVSTAGE6, GX_TEXCOORD2, GX_TEXMAP0, 0xFF);
    GXSetTevColorIn(GX_TEVSTAGE6, GX_CC_ZERO, GX_CC_TEXC, GX_CC_HALF, GX_CC_CPREV);
    GXSetTevColorOp(GX_TEVSTAGE6, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVPREV);
    GXSetTevAlphaIn(GX_TEVSTAGE6, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
    GXSetTevAlphaOp(GX_TEVSTAGE6, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevDirect(GX_TEVSTAGE6);

    // Stage 2
    GXSetTevOrder(GX_TEVSTAGE7, GX_TEXCOORD3, GX_TEXMAP0, 0xFF);
    GXSetTevColorIn(GX_TEVSTAGE7, GX_CC_ZERO, GX_CC_TEXC, GX_CC_HALF, GX_CC_CPREV);
    GXSetTevColorOp(GX_TEVSTAGE7, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVPREV);
    GXSetTevAlphaIn(GX_TEVSTAGE7, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
    GXSetTevAlphaOp(GX_TEVSTAGE7, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevDirect(GX_TEVSTAGE7);

    // Stage 3
    GXSetTevOrder(GX_TEVSTAGE8, GX_TEXCOORD4, GX_TEXMAP0, 0xFF);
    GXSetTevColorIn(GX_TEVSTAGE8, GX_CC_ZERO, GX_CC_TEXC, GX_CC_HALF, GX_CC_CPREV);
    GXSetTevColorOp(GX_TEVSTAGE8, GX_TEV_ADD, GX_TB_ZERO, GX_CS_DIVIDE_2, GX_TRUE, GX_TEVPREV);
    GXSetTevAlphaIn(GX_TEVSTAGE8, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
    GXSetTevAlphaOp(GX_TEVSTAGE8, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevDirect(GX_TEVSTAGE8);

    GXSetTevOrder(GX_TEVSTAGE9, GX_TEXCOORD0, GX_TEXMAP0, 0xFF);
    GXSetTevColorIn(GX_TEVSTAGE9, GX_CC_CPREV, GX_CC_ZERO, GX_CC_C1, GX_CC_ZERO);
    GXSetTevColorOp(GX_TEVSTAGE9, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevAlphaIn(GX_TEVSTAGE9, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
    GXSetTevAlphaOp(GX_TEVSTAGE9, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevDirect(GX_TEVSTAGE9);

    GXSetTevOrder(GX_TEVSTAGE10, GX_TEXCOORD0, GX_TEXMAP0, 0xFF);
    GXSetTevColorIn(GX_TEVSTAGE10, GX_CC_KONST, GX_CC_ZERO, GX_CC_C1, GX_CC_CPREV);
    GXSetTevColorOp(GX_TEVSTAGE10, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevAlphaIn(GX_TEVSTAGE10, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
    GXSetTevAlphaOp(GX_TEVSTAGE10, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevKColorSel(GX_TEVSTAGE10, GX_TEV_KCSEL_K0);
    GXSetTevDirect(GX_TEVSTAGE10);

    // rgb(115, 36, 59)

    GXColor fogColor = {42, 13, 22, 255};
    GXSetTevKColor(GX_KCOLOR0, fogColor);

    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_OR, GX_ALWAYS, 0);

    GXSetTevColor(GX_TEVREG0, tev_color);
    GXSetProjection(m, GX_ORTHOGRAPHIC);
    GXSetViewport(f_left, f_top, f_wd, f_ht, 0.0, 1.0);
    GXSetScissor(f_left, f_top, f_wd, f_ht);

    GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GXSetAlphaUpdate(GX_FALSE);
    GXSetColorUpdate(GX_TRUE);
    GXLoadPosMtxImm(e_m, 0);
    GXSetCurrentMtx(0);
    GXSetCullMode(GX_CULL_NONE);
    GXSetBlendMode(GX_BM_SUBTRACT, GX_BL_ZERO, GX_BL_ZERO, GX_LO_NOOP);

    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition2u16(f_left, f_top);
    GXTexCoord2f32(0.0f, 0.0f);
    GXTexCoord2f32(-offset_x, 0.0f);
    GXTexCoord2f32(offset_x, 0.0f);
    GXTexCoord2f32(0.0f, -offset_y);
    GXTexCoord2f32(0.0f, offset_y);
    GXPosition2u16(f_left + f_wd, f_top);
    GXTexCoord2f32(1.0f, 0.0f);
    GXTexCoord2f32(1.0f - offset_x, 0.0f);
    GXTexCoord2f32(1.0f + offset_x, 0.0f);
    GXTexCoord2f32(1.0f, -offset_y);
    GXTexCoord2f32(1.0f, offset_y);
    GXPosition2u16(f_left + f_wd, f_top + f_ht);
    GXTexCoord2f32(1.0f, 1.0f);
    GXTexCoord2f32(1.0f - offset_x, 1.0f);
    GXTexCoord2f32(1.0f + offset_x, 1.0f);
    GXTexCoord2f32(1.0f, 1.0f - offset_y);
    GXTexCoord2f32(1.0f, 1.0f + offset_y);
    GXPosition2u16(f_left, f_top + f_ht);
    GXTexCoord2f32(0.0f, 1.0f);
    GXTexCoord2f32(-offset_x, 1.0f);
    GXTexCoord2f32(+offset_x, 1.0f);
    GXTexCoord2f32(0.0f, 1.0f - offset_y);
    GXTexCoord2f32(0.0f, 1.0f + offset_y);
    GXEnd();

    GXSetBlendMode(GX_BM_BLEND, GX_BL_ONE, GX_BL_ONE, GX_LO_NOOP);
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition2u16(f_left, f_top);
    GXTexCoord2f32(0.0f, 0.0f);
    GXTexCoord2f32(-offset_x, 0.0f);
    GXTexCoord2f32(offset_x, 0.0f);
    GXTexCoord2f32(0.0f, -offset_y);
    GXTexCoord2f32(0.0f, offset_y);
    GXPosition2u16(f_left + f_wd, f_top);
    GXTexCoord2f32(1.0f, 0.0f);
    GXTexCoord2f32(1.0f - offset_x, 0.0f);
    GXTexCoord2f32(1.0f + offset_x, 0.0f);
    GXTexCoord2f32(1.0f, -offset_y);
    GXTexCoord2f32(1.0f, offset_y);
    GXPosition2u16(f_left + f_wd, f_top + f_ht);
    GXTexCoord2f32(1.0f, 1.0f);
    GXTexCoord2f32(1.0f - offset_x, 1.0f);
    GXTexCoord2f32(1.0f + offset_x, 1.0f);
    GXTexCoord2f32(1.0f, 1.0f - offset_y);
    GXTexCoord2f32(1.0f, 1.0f + offset_y);
    GXPosition2u16(f_left, f_top + f_ht);
    GXTexCoord2f32(0.0f, 1.0f);
    GXTexCoord2f32(-offset_x, 1.0f);
    GXTexCoord2f32(+offset_x, 1.0f);
    GXTexCoord2f32(0.0f, 1.0f - offset_y);
    GXTexCoord2f32(0.0f, 1.0f + offset_y);
    GXEnd();
}

struct J3DSys {
public:
    Mtx mViewMtx;
};

static J3DSys *j3dSys = (J3DSys *)0x804045dc;

void TNokiFilter::drawFilter(JDrama::TGraphics *graphics) {
    Mtx e_m;
    Mtx44 m;

    f32 f_left   = 0;
    f32 f_wd     = 640;
    f32 f_top    = 0;
    f32 f_ht     = 448;
    f32 f_right  = f_left + f_wd;
    f32 f_bottom = f_top + f_ht;

    // C_MTXOrtho(m, f_top, f_bottom, f_left, f_right, 0.0f, 1.0f);
    // PSMTXIdentity(e_m);
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);

    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);

    GXSetNumChans(0);
    GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_REG, 0, GX_DF_NONE, GX_AF_NONE);
    GXSetChanCtrl(GX_COLOR1A1, GX_FALSE, GX_SRC_REG, GX_SRC_REG, 0, GX_DF_NONE, GX_AF_NONE);

    GXSetNumTexGens(1);
    GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, 0x3c, GX_FALSE, 0x7d);
    GXLoadTexObj(&gpScreenTexture->texture->mTexObj, GX_TEXMAP0);

    GXSetNumTevStages(1);
    // Stage 0
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, 0xFF);
    GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_TEXC);
    GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVPREV);
    GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
    GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevDirect(GX_TEVSTAGE0);

    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_OR, GX_ALWAYS, 0);
    // GXSetProjection(graphics->mProjMtx, GX_PERSPECTIVE);
    // GXSetViewport(f_left, f_top, f_wd, f_ht, 0.0, 1.0);
    // GXSetScissor(f_left, f_top, f_wd, f_ht);

    // MTXInverse(j3dSys->mViewMtx, e_m);
    PSMTXIdentity(e_m);
    GXLoadPosMtxImm(j3dSys->mViewMtx, 0);
    GXSetCurrentMtx(0);

    GXSetZCompLoc(GX_TRUE);
    GXSetZMode(GX_TRUE, GX_LEQUAL, GX_FALSE);
    GXSetAlphaUpdate(GX_FALSE);
    GXSetColorUpdate(GX_TRUE);
    GXSetCullMode(GX_CULL_NONE);
    GXSetBlendMode(GX_BM_NONE, GX_BL_ZERO, GX_BL_ZERO, GX_LO_NOOP);

    // -100, -4000, 1000
    TVec3f pos(-100.0f, -4000.0f, 1000.0f);
    // TVec3f pos(0.0f, 0.0f, 1.0f);

    TVec3f &marioPos = gpCamera->mTranslation;

    // --- Forward (ONLY XZ plane) ---
    TVec3f forward = marioPos - pos;
    forward.y      = 0.0f;
    forward.normalize();

    // Handle edge case (same position)
    if (forward.magnitude() == 0.0f) {
        forward = TVec3f::forward();
    }

    // --- World up stays constant ---
    TVec3f up = TVec3f::up();

    // --- Right = up � forward ---
    TVec3f right;
    up.cross(forward, right);
    right.normalize();

    // --- Quad size ---
    float wd = 2000.0f;
    float ht = 45000.0f;

    float hw = wd * 0.5f;
    float hh = ht * 0.5f;

    // --- Build vertices ---
    TVec3f v0 = pos;
    TVec3f v1 = pos;
    TVec3f v2 = pos;
    TVec3f v3 = pos;

    TVec3f temp;

    temp = right;
    temp.scale(-hw);
    v0 += temp;
    temp = up;
    temp.scale(-hh);
    v0 += temp;

    temp = right;
    temp.scale(hw);
    v1 += temp;
    temp = up;
    temp.scale(-hh);
    v1 += temp;

    temp = right;
    temp.scale(hw);
    v2 += temp;
    temp = up;
    temp.scale(hh);
    v2 += temp;

    temp = right;
    temp.scale(-hw);
    v3 += temp;
    temp = up;
    temp.scale(hh);
    v3 += temp;

    GXBegin(GX_QUADS, GX_VTXFMT0, 4);

    GXPosition3f32(v0.x, v0.y, v0.z);
    GXTexCoord2f32(0.0f, 1.0f);

    GXPosition3f32(v1.x, v1.y, v1.z);
    GXTexCoord2f32(1.0f, 1.0f);

    GXPosition3f32(v2.x, v2.y, v2.z);
    GXTexCoord2f32(1.0f, 0.0f);

    GXPosition3f32(v3.x, v3.y, v3.z);
    GXTexCoord2f32(0.0f, 0.0f);

    GXEnd();
}

void TFogFilter::load(JSUMemoryInputStream &stream) {
    TScreenFilter::load(stream);
    stream.readData(&mDensity, sizeof(u8));
    stream.readData(&mOpacity, sizeof(u8));
    stream.readData(&mR, sizeof(u8));
    stream.readData(&mG, sizeof(u8));
    stream.readData(&mB, sizeof(u8));
}

void TFogFilter::drawFilter(JDrama::TGraphics *graphics) {

    u16 x  = 0;
    u16 y  = 0;
    u16 wd = 640;
    u16 ht = 448;

    Mtx e_m;
    Mtx44 m;

    GXTexObj tex_obj;

    GXColor tev_color = {0x03, 0x03, 0x03, 0x00};

    f32 f_left   = 0;
    f32 f_wd     = 640;
    f32 f_top    = 0;
    f32 f_ht     = 448;
    f32 f_right  = f_left + f_wd;
    f32 f_bottom = f_top + f_ht;

    GXSetTexCopySrc(f_left, f_top, f_wd, f_ht);
    GXSetCopyFilter(GX_FALSE, 0, GX_FALSE, nullptr);
    GXSetTexCopyDst(640 >> 1, 448 >> 1, GX_TF_Z16, GX_TRUE);
    GXCopyTex(gDepthBuffer, GX_FALSE);
    GXInitTexObj(&tex_obj, gDepthBuffer, 640 >> 1, 448 >> 1, GX_TF_IA8, GX_CLAMP, GX_CLAMP, 0);
    GXInitTexObjLOD(&tex_obj, GX_NEAR, GX_NEAR, 0.0, 0.0, 0.0, GX_FALSE, GX_FALSE, GX_ANISO_1);

    C_MTXOrtho(m, f_top, f_bottom, f_left, f_right, 0.0f, 1.0f);
    PSMTXIdentity(e_m);
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);

    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_U16, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);

    GXSetNumTexGens(1);
    GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, 0x3c, GX_FALSE, 0x7d);

    GXLoadTexObj(&tex_obj, GX_TEXMAP0);
    GXLoadTexObj(&gpScreenTexture->texture->mTexObj, GX_TEXMAP1);

    GXSetNumTevStages(7);

    setupDepthMap(GX_TEVSTAGE0, mDensity, GX_TEVREG0, GX_TEXCOORD0);  // 0-4 inclusive

    GXSetTevOrder(GX_TEVSTAGE5, GX_TEXCOORD0, GX_TEXMAP1, 0xff);
    GXSetTevColorIn(GX_TEVSTAGE5, GX_CC_KONST, GX_CC_ZERO, GX_CC_C0, GX_CC_ZERO);
    GXSetTevColorOp(GX_TEVSTAGE5, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVREG0);
    GXSetTevDirect(GX_TEVSTAGE5);
    GXSetTevKColorSel(GX_TEVSTAGE5, GX_TEV_KCSEL_K0_A);

    GXSetTevOrder(GX_TEVSTAGE6, GX_TEXCOORD0, GX_TEXMAP1, 0xff);
    GXSetTevColorIn(GX_TEVSTAGE6, GX_CC_TEXC, GX_CC_KONST, GX_CC_C0, GX_CC_ZERO);
    GXSetTevColorOp(GX_TEVSTAGE6, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevDirect(GX_TEVSTAGE6);
    GXSetTevKColorSel(GX_TEVSTAGE6, GX_TEV_KCSEL_K0);

    GXColor fogColor = {mR, mG, mB, mOpacity};
    GXSetTevKColor(GX_KCOLOR0, fogColor);

    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_OR, GX_ALWAYS, 0);

    GXSetTevColor(GX_TEVREG0, tev_color);
    GXSetProjection(m, GX_ORTHOGRAPHIC);
    GXSetViewport(f_left, f_top, f_wd, f_ht, 0.0, 1.0);
    GXSetScissor(f_left, f_top, f_wd, f_ht);

	GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
	GXSetAlphaUpdate(GX_FALSE);
	GXSetColorUpdate(GX_TRUE);
	GXLoadPosMtxImm(e_m, 0);
	GXSetCurrentMtx(0);
	GXSetCullMode(GX_CULL_NONE);
	
	GXSetBlendMode(GX_BM_NONE, GX_BL_ZERO, GX_BL_ZERO, GX_LO_NOOP);
	GXBegin(GX_QUADS, GX_VTXFMT0, 4);
	GXPosition2u16(f_left, f_top);
	GXTexCoord2f32(0.0f, 0.0f);
	GXPosition2u16(f_left + f_wd, f_top);
	GXTexCoord2f32(1.0f, 0.0f);
	GXPosition2u16(f_left + f_wd, f_top + f_ht);
	GXTexCoord2f32(1.0f, 1.0f);
	GXPosition2u16(f_left, f_top + f_ht);
	GXTexCoord2f32(0.0f, 1.0f);
	GXEnd();
	
}
