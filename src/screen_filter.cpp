#include "screen_filter.hxx"

#include <GX.h>
#include <JSystem/JUtility/JUTTexture.hxx>
#include <SMS/Enemy/Conductor.hxx>
#include <JSystem/JDrama/JDREfbCtrl.hxx>

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
u32* gDepthBuffer;
bool gUpdateScreenTexture;

void initFilters(TMarDirector *marDirector) {
	gIsFirstUpdate = true;
	gUsesScreenTexture = false;
	gUsesDepthBuffer = false;
	gUpdateScreenTexture = false;
	gInitDepthBuffer = false;
}

void updateFilters(TMarDirector *) {
	if(gIsFirstUpdate) {
		if(gUsesDepthBuffer && !gInitDepthBuffer) {
			gDepthBuffer = new u32[640 * 448 / 2];
		}
		gIsFirstUpdate = false;
	}

	if(gUsesScreenTexture) {
		gUpdateScreenTexture = true;
	}
}

void initDepthMap() {
	if(gUsesDepthBuffer && !gInitDepthBuffer) {
		gDepthBuffer = new u32[640 * 448 / 2];
		gInitDepthBuffer = true;
	}
}


void TScreenFilter::load(JSUMemoryInputStream &stream) {
    JDrama::TNameRef::load(stream);
    stream.readData(&mVisible, sizeof(bool));
}

void TScreenFilter::loadAfter() {
	gpConductor->registerOtherObj(this);


}

// Requires 5 tev stages + KColor3 for fine tuned depth selection
// nearFarSelect = 0 -> Keep everything in near
// nearFarSelect = 255 -> Keep everything in far
// nearFarSelect = 128 -> Keep about 50/50
// Depth map is 24 bit, but only upper 16 really matter, this allows us to fetch map depth with different focus.
// E.g fog usually cares about far
// Depth of field would possibly need just near
void setupDepthMap(u8 stage, u8 nearFarSelect, u8 out, u8 texCoord, u8 texMap = GX_TEXMAP0) {
	
	GXColor nearFarDistanceCol = { nearFarSelect, 0, 0, 0 };
	GXSetTevKColor(GX_KCOLOR3, nearFarDistanceCol);
	
	// Close to camera, scaled such that step size is R
	GXSetTevOrder(GX_TEVSTAGE0 + stage, texCoord, texMap, 0xff);
	GXSetTevColorIn(GX_TEVSTAGE0 + stage, GX_CC_ONE, GX_CC_ZERO, GX_CC_TEXC, GX_CC_ZERO);
	GXSetTevColorOp(GX_TEVSTAGE0 + stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_4,
					GX_TRUE, GX_TEVPREV);
	GXSetTevDirect(GX_TEVSTAGE0 + stage);

	GXSetTevOrder(GX_TEVSTAGE1 + stage, texCoord, texMap, 0xff);
	GXSetTevColorIn(GX_TEVSTAGE1 + stage, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_CPREV);
	GXSetTevColorOp(GX_TEVSTAGE1 + stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_4,
					GX_TRUE, GX_TEVPREV);
	GXSetTevDirect(GX_TEVSTAGE1 + stage);

	GXSetTevOrder(GX_TEVSTAGE2 + stage, texCoord, texMap, 0xff);
	GXSetTevColorIn(GX_TEVSTAGE2 + stage, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_CPREV);
	GXSetTevColorOp(GX_TEVSTAGE2 + stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_4,
					GX_TRUE, GX_TEVPREV);
	GXSetTevDirect(GX_TEVSTAGE2 + stage);

	GXSetTevOrder(GX_TEVSTAGE3 + stage, texCoord, texMap, 0xff);
	GXSetTevColorIn(GX_TEVSTAGE3 + stage, GX_CC_ZERO, GX_CC_KONST, GX_CC_CPREV, GX_CC_ZERO);
	GXSetTevColorOp(GX_TEVSTAGE3 + stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_4,
					GX_TRUE, GX_TEVPREV);
	GXSetTevDirect(GX_TEVSTAGE3 + stage);
	GXSetTevDirect(GX_TEVSTAGE3 + stage);
	 GXSetTevKColorSel(GX_TEVSTAGE3 + stage, GX_TEV_KCSEL_K3_R);
	 
	// Far from camera, scaled such that band size is 0-R
	GXSetTevOrder(GX_TEVSTAGE4 + stage, texCoord, texMap, 0xff);
	GXSetTevColorIn(GX_TEVSTAGE4 + stage, GX_CC_KONST, GX_CC_ZERO, GX_CC_TEXA, GX_CC_CPREV);
	GXSetTevColorOp(GX_TEVSTAGE4 + stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1,
					GX_TRUE, out);
	GXSetTevDirect(GX_TEVSTAGE4 + stage);
	GXSetTevKColorSel(GX_TEVSTAGE4 + stage, GX_TEV_KCSEL_K3_R);

}

constexpr static const char *efbNormalSceneDrawing =
    "\x92\xca\x8f\xed\x83\x56\x81\x5b\x83\x93\x95\x60\x89\xe6\x83\x58\x83\x65\x81\x5b\x83\x57";
void TScreenFilter::perform(u32 flags, JDrama::TGraphics *graphics) {
	if((flags & 0x1) != 0 && !mInjected) {
		for(auto it = gpMarDirector->mPerformListGXPost->begin(); it != gpMarDirector->mPerformListGXPost->end(); it = it->mNext) {
			if(strcmp("<TOrthoProj>", ((JDrama::TViewObj*)it->mData)->mKeyName) == 0 ) {
				auto data = new TPerformLink(this, 0x88);
				gpMarDirector->mPerformListGXPost->Insert(it, reinterpret_cast<JGadget::TSingleLinkListNode*>((u32)data + 4));
				break;
			}
		}
		gUsesScreenTexture = gUsesScreenTexture || mUsesScreenTexture;
		gUsesDepthBuffer = gUsesDepthBuffer || mUsesDepthBuffer;

		mInjected = true;
	}


	if((flags & 0x80) != 0 && mVisible) {
		if(mUsesScreenTexture) {
			JDrama::TEfbCtrlTex *screenEfbTex = (JDrama::TEfbCtrlTex *)gpMarDirector->searchF(calcKeyCode(efbNormalSceneDrawing), efbNormalSceneDrawing);
			if(screenEfbTex != nullptr) {
				screenEfbTex->testPerform(flags | 0x8, graphics);
				//gUpdateScreenTexture = false;
			}
		}
		initDepthMap();
		drawFilter();
	}
}

void TSunsetFilter::load(JSUMemoryInputStream &stream) {
	TScreenFilter::load(stream);
	
    stream.readData(&mIntensity, sizeof(f32));
}

void TSunsetFilter::drawFilter() {
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

void TOutlineFilter::load(JSUMemoryInputStream &stream) {
	TScreenFilter::load(stream);
	
}

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
	f32 offset_x = (1.0f / f_wd);
	f32 offset_y = (1.0f / f_wd);

	GXSetTexCopySrc(f_left, f_top, f_wd, f_ht);
	GXSetCopyFilter(GX_FALSE, 0, GX_TRUE, vFilter);
	GXSetTexCopyDst(640 >> 1, 448 >> 1, GX_TF_Z16, GX_TRUE);
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

	GXSetNumTexGens(2);
	GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, 0x3c, GX_FALSE,
	                  0x7d);
	GXSetTexCoordGen2(GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_TEX1, 0x3c, GX_FALSE,
	                  0x7d);
	
	GXLoadTexObj(&tex_obj, GX_TEXMAP0);
	GXLoadTexObj(&gpScreenTexture->texture->mTexObj, GX_TEXMAP1);

	GXSetNumTevStages(12);

	// First draw pass - far from camera

	// X- pass
	setupDepthMap(GX_TEVSTAGE0, 255, GX_TEVREG1, GX_TEXCOORD0); // 0-5

	//// X+ pass
	setupDepthMap(GX_TEVSTAGE5, 255, GX_TEVREG2, GX_TEXCOORD1); // 5-10


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
	 
	 
	GXColor fog_distance = { 8, 0, 0, 0 };
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
	setupDepthMap(GX_TEVSTAGE0, 64, GX_TEVREG1, GX_TEXCOORD0); // 0-5

	//// X+ pass
	setupDepthMap(GX_TEVSTAGE5, 64, GX_TEVREG2, GX_TEXCOORD1); // 5-10


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
	 
	 
	GXColor fog_distance2 = { 8, 0, 0, 0 };
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
	GXColor tev_color1 = { 76, 150, 29, 0 };
	GXSetTevKColor(GX_KCOLOR1, tev_color1);
	GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP1, 0xff);
	GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_TEXC, GX_CC_KONST, GX_CC_ZERO);
	GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1,
	                GX_FALSE, GX_TEVPREV);
    GXSetTevKColorSel(GX_TEVSTAGE0, GX_TEV_KCSEL_K1_R);
    GXSetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP1, GX_TEV_SWAP1);
    GXSetTevSwapModeTable(GX_TEV_SWAP1, GX_CH_RED, GX_CH_RED, GX_CH_RED, GX_CH_ALPHA);
	GXSetTevDirect(GX_TEVSTAGE0);


	GXSetTevOrder(GX_TEVSTAGE1, GX_TEXCOORD0, GX_TEXMAP1, 0xff);
	GXSetTevColorIn(GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_TEXC, GX_CC_KONST, GX_CC_CPREV);
	GXSetTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1,
	                GX_FALSE, GX_TEVPREV);
    GXSetTevKColorSel(GX_TEVSTAGE1, GX_TEV_KCSEL_K1_G);
    GXSetTevSwapMode(GX_TEVSTAGE1, GX_TEV_SWAP2, GX_TEV_SWAP2);
    GXSetTevSwapModeTable(GX_TEV_SWAP2, GX_CH_GREEN, GX_CH_GREEN, GX_CH_GREEN, GX_CH_ALPHA);
	GXSetTevDirect(GX_TEVSTAGE1);
    

	GXSetTevOrder(GX_TEVSTAGE2, GX_TEXCOORD0, GX_TEXMAP1, 0xff);
	GXSetTevColorIn(GX_TEVSTAGE2, GX_CC_ZERO, GX_CC_TEXC, GX_CC_KONST, GX_CC_CPREV);
	GXSetTevColorOp(GX_TEVSTAGE2, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1,
	                GX_TRUE, GX_TEVREG1);
    GXSetTevKColorSel(GX_TEVSTAGE2, GX_TEV_KCSEL_K1_B);
    GXSetTevSwapMode(GX_TEVSTAGE2, GX_TEV_SWAP3, GX_TEV_SWAP3);
    GXSetTevSwapModeTable(GX_TEV_SWAP3, GX_CH_BLUE, GX_CH_BLUE, GX_CH_BLUE, GX_CH_ALPHA);
	GXSetTevDirect(GX_TEVSTAGE2);
	
	
	GXSetTevOrder(GX_TEVSTAGE3, GX_TEXCOORD1, GX_TEXMAP1, 0xff);
	GXSetTevColorIn(GX_TEVSTAGE3, GX_CC_ZERO, GX_CC_TEXC, GX_CC_KONST, GX_CC_ZERO);
	GXSetTevColorOp(GX_TEVSTAGE3, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1,
	                GX_FALSE, GX_TEVPREV);
    GXSetTevKColorSel(GX_TEVSTAGE3, GX_TEV_KCSEL_K1_R);
    GXSetTevSwapMode(GX_TEVSTAGE3, GX_TEV_SWAP1, GX_TEV_SWAP1);
    GXSetTevSwapModeTable(GX_TEV_SWAP1, GX_CH_RED, GX_CH_RED, GX_CH_RED, GX_CH_ALPHA);
	GXSetTevDirect(GX_TEVSTAGE3);


	GXSetTevOrder(GX_TEVSTAGE4, GX_TEXCOORD1, GX_TEXMAP1, 0xff);
	GXSetTevColorIn(GX_TEVSTAGE4, GX_CC_ZERO, GX_CC_TEXC, GX_CC_KONST, GX_CC_CPREV);
	GXSetTevColorOp(GX_TEVSTAGE4, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1,
	                GX_FALSE, GX_TEVPREV);
    GXSetTevKColorSel(GX_TEVSTAGE4, GX_TEV_KCSEL_K1_G);
    GXSetTevSwapMode(GX_TEVSTAGE4, GX_TEV_SWAP2, GX_TEV_SWAP2);
    GXSetTevSwapModeTable(GX_TEV_SWAP2, GX_CH_GREEN, GX_CH_GREEN, GX_CH_GREEN, GX_CH_ALPHA);
	GXSetTevDirect(GX_TEVSTAGE4);
    

	GXSetTevOrder(GX_TEVSTAGE5, GX_TEXCOORD1, GX_TEXMAP1, 0xff);
	GXSetTevColorIn(GX_TEVSTAGE5, GX_CC_ZERO, GX_CC_TEXC, GX_CC_KONST, GX_CC_CPREV);
	GXSetTevColorOp(GX_TEVSTAGE5, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1,
	                GX_TRUE, GX_TEVREG2);
    GXSetTevKColorSel(GX_TEVSTAGE5, GX_TEV_KCSEL_K1_B);
    GXSetTevSwapMode(GX_TEVSTAGE5, GX_TEV_SWAP3, GX_TEV_SWAP3);
    GXSetTevSwapModeTable(GX_TEV_SWAP3, GX_CH_BLUE, GX_CH_BLUE, GX_CH_BLUE, GX_CH_ALPHA);
	GXSetTevDirect(GX_TEVSTAGE5);
	

	GXSetTevOrder(GX_TEVSTAGE6, GX_TEXCOORD1, GX_TEXMAP1, 0xff);
	GXSetTevColorIn(GX_TEVSTAGE6, GX_CC_ZERO, GX_CC_ONE, GX_CC_C1, GX_CC_C2);
	GXSetTevColorOp(GX_TEVSTAGE6, GX_TEV_SUB, GX_TB_ZERO, GX_CS_SCALE_1,
	                GX_TRUE, GX_TEVPREV);
	GXSetTevDirect(GX_TEVSTAGE6);
	
	GXSetTevOrder(GX_TEVSTAGE7, GX_TEXCOORD1, GX_TEXMAP1, 0xff);
	GXSetTevColorIn(GX_TEVSTAGE7, GX_CC_CPREV, GX_CC_KONST, GX_CC_CPREV, GX_CC_ZERO);
	GXSetTevColorOp(GX_TEVSTAGE7, GX_TEV_COMP_R8_GT, GX_TB_ZERO, GX_CS_SCALE_4,
	                GX_TRUE, GX_TEVPREV);
	GXSetTevDirect(GX_TEVSTAGE7);
	 GXSetTevKColorSel(GX_TEVSTAGE7, GX_TEV_KCSEL_K0_R);
	 
	GXSetTevOrder(GX_TEVSTAGE8, GX_TEXCOORD1, GX_TEXMAP1, 0xff);
	GXSetTevColorIn(GX_TEVSTAGE8, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_CPREV);
	GXSetTevColorOp(GX_TEVSTAGE8, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_4,
	                GX_TRUE, GX_TEVPREV);
	GXSetTevDirect(GX_TEVSTAGE8);
	GXSetTevOrder(GX_TEVSTAGE9, GX_TEXCOORD1, GX_TEXMAP1, 0xff);
	GXSetTevColorIn(GX_TEVSTAGE9, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_CPREV);
	GXSetTevColorOp(GX_TEVSTAGE9, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_2,
	                GX_TRUE, GX_TEVPREV);
	GXSetTevDirect(GX_TEVSTAGE9);
	
	GXColor fog_distance3 = { 2, 0, 0, 0 };
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


void TSubtleOutline::drawFilter() {
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
	f32 offset_x = (1.0f / f_wd);
	f32 offset_y = (1.0f / f_wd);

	GXSetTexCopySrc(f_left, f_top, f_wd, f_ht);
	GXSetCopyFilter(GX_FALSE, 0, GX_TRUE, vFilter);
	GXSetTexCopyDst(640 >> 1, 448 >> 1, GX_TF_Z24X8, GX_TRUE);
	//GXSetTexCopyDst(wd >> 1, ht >> 1, GX_CTF_Z8M, GX_TRUE);
	GXCopyTex(gDepthBuffer, GX_FALSE);
	GXPixModeSync();
	  /* for(int x = 0; x < 640 * 448; ++x) {
		   f32 color24 = (f32)((buffer[x] >> 4) & 0xFFFFFF) / (f32)0xFFFFFF;
		   u8 data = color24 * 255.0;
		   buffer[x] = 0;
	   }*/
	GXInitTexObj(&tex_obj, gDepthBuffer, 640 >> 1, 448 >> 1, GX_TF_RGBA8, GX_CLAMP,
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
	
	GXLoadTexObj(&tex_obj, GX_TEXMAP0);
	
	GXSetNumTevStages(8);

	// X- pass
	GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, 0xff);
	GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ONE, GX_CC_TEXC, GX_CC_ONE);
	GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_SUB, GX_TB_ZERO, GX_CS_SCALE_4,
					GX_TRUE, GX_TEVPREV);
	GXSetTevDirect(GX_TEVSTAGE0);
    GXSetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP3, GX_TEV_SWAP3);
    GXSetTevSwapModeTable(GX_TEV_SWAP3, GX_CH_RED, GX_CH_RED, GX_CH_RED, GX_CH_ALPHA);
	GXSetTevKColorSel(GX_TEVSTAGE0, GX_TEV_KCSEL_K3_R);

	GXSetTevOrder(GX_TEVSTAGE1, GX_TEXCOORD0, GX_TEXMAP0, 0xff);
	GXSetTevColorIn(GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_CPREV);
	GXSetTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_4,
					GX_TRUE, GX_TEVPREV);
	GXSetTevDirect(GX_TEVSTAGE1);

	GXSetTevOrder(GX_TEVSTAGE2, GX_TEXCOORD0, GX_TEXMAP0, 0xff);
	GXSetTevColorIn(GX_TEVSTAGE2, GX_CC_KONST, GX_CC_ZERO, GX_CC_TEXC, GX_CC_CPREV);
	GXSetTevColorOp(GX_TEVSTAGE2, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_2,
					GX_TRUE, GX_TEVREG1);
	GXSetTevDirect(GX_TEVSTAGE2);
	 GXSetTevKColorSel(GX_TEVSTAGE2, GX_TEV_KCSEL_K3_R);
    GXSetTevSwapMode(GX_TEVSTAGE2, GX_TEV_SWAP2, GX_TEV_SWAP2);
    GXSetTevSwapModeTable(GX_TEV_SWAP2, GX_CH_GREEN, GX_CH_GREEN, GX_CH_GREEN, GX_CH_ALPHA);
	
	
	// X- pas+
	GXSetTevOrder(GX_TEVSTAGE3, GX_TEXCOORD1, GX_TEXMAP0, 0xff);
	GXSetTevColorIn(GX_TEVSTAGE3, GX_CC_ZERO, GX_CC_ONE, GX_CC_TEXC, GX_CC_ONE);
	GXSetTevColorOp(GX_TEVSTAGE3, GX_TEV_SUB, GX_TB_ZERO, GX_CS_SCALE_4,
					GX_TRUE, GX_TEVPREV);
	GXSetTevDirect(GX_TEVSTAGE3);
    GXSetTevSwapMode(GX_TEVSTAGE3, GX_TEV_SWAP3, GX_TEV_SWAP3);
    GXSetTevSwapModeTable(GX_TEV_SWAP3, GX_CH_RED, GX_CH_RED, GX_CH_RED, GX_CH_ALPHA);
	GXSetTevKColorSel(GX_TEVSTAGE3, GX_TEV_KCSEL_K3_R);

	GXSetTevOrder(GX_TEVSTAGE4, GX_TEXCOORD1, GX_TEXMAP0, 0xff);
	GXSetTevColorIn(GX_TEVSTAGE4, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_CPREV);
	GXSetTevColorOp(GX_TEVSTAGE4, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_4,
					GX_TRUE, GX_TEVPREV);
	GXSetTevDirect(GX_TEVSTAGE4);

	GXSetTevOrder(GX_TEVSTAGE5, GX_TEXCOORD1, GX_TEXMAP0, 0xff);
	GXSetTevColorIn(GX_TEVSTAGE5, GX_CC_KONST, GX_CC_ZERO, GX_CC_TEXC, GX_CC_CPREV);
	GXSetTevColorOp(GX_TEVSTAGE5, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_2,
					GX_TRUE, GX_TEVREG2);
	GXSetTevDirect(GX_TEVSTAGE5);
	 GXSetTevKColorSel(GX_TEVSTAGE5, GX_TEV_KCSEL_K3_R);
    GXSetTevSwapMode(GX_TEVSTAGE5, GX_TEV_SWAP2, GX_TEV_SWAP2);
    GXSetTevSwapModeTable(GX_TEV_SWAP2, GX_CH_GREEN, GX_CH_GREEN, GX_CH_GREEN, GX_CH_ALPHA);
	
	//GXSetTevOrder(GX_TEVSTAGE2, GX_TEXCOORD0, GX_TEXMAP0, 0xff);
	//GXSetTevColorIn(GX_TEVSTAGE2, GX_CC_KONST, GX_CC_ZERO, GX_CC_TEXC, GX_CC_CPREV);
	//GXSetTevColorOp(GX_TEVSTAGE2, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_4,
	//				GX_TRUE, GX_TEVPREV);
	//GXSetTevDirect(GX_TEVSTAGE2);
	// GXSetTevKColorSel(GX_TEVSTAGE2, GX_TEV_KCSEL_K3_R);
 //   GXSetTevSwapMode(GX_TEVSTAGE2, GX_TEV_SWAP1, GX_TEV_SWAP1);
 //   GXSetTevSwapModeTable(GX_TEV_SWAP1, GX_CH_BLUE, GX_CH_BLUE, GX_CH_BLUE, GX_CH_ALPHA);


	//GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, 0xff);
	//GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_KONST, GX_CC_TEXC, GX_CC_KONST);
	//GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_SUB, GX_TB_ZERO, GX_CS_SCALE_4,
	//				GX_FALSE, GX_TEVPREV);
	//GXSetTevDirect(GX_TEVSTAGE0);
	// GXSetTevKColorSel(GX_TEVSTAGE0, GX_TEV_KCSEL_K3_R);
	 
	GXColor fog_distance2 = { 16, 4, 0, 0 };
	GXSetTevKColor(GX_KCOLOR3, fog_distance2);
	
	//   GXSetTevOrder(GX_TEVSTAGE1, GX_TEXCOORD0, GX_TEXMAP0, 0xff);
	//GXSetTevColorIn(GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_KONST, GX_CC_TEXA, GX_CC_CPREV);
	//GXSetTevColorOp(GX_TEVSTAGE1, GX_TEV_SUB, GX_TB_ZERO, GX_CS_SCALE_4,
	//				GX_FALSE, GX_TEVPREV);
	//GXSetTevDirect(GX_TEVSTAGE1);
	// GXSetTevKColorSel(GX_TEVSTAGE1, GX_TEV_KCSEL_K3_G);

	//GXSetTevOrder(GX_TEVSTAGE2, GX_TEXCOORD0, GX_TEXMAP0, 0xff);
	//GXSetTevColorIn(GX_TEVSTAGE2, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_CPREV);
	//GXSetTevColorOp(GX_TEVSTAGE2, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_4,
	//				GX_FALSE, GX_TEVPREV);
	//GXSetTevDirect(GX_TEVSTAGE2);
	//GXSetTevOrder(GX_TEVSTAGE3, GX_TEXCOORD0, GX_TEXMAP0, 0xff);
	//GXSetTevColorIn(GX_TEVSTAGE3, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_CPREV);
	//GXSetTevColorOp(GX_TEVSTAGE3, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_4,
	//				GX_FALSE, GX_TEVPREV);
	//GXSetTevDirect(GX_TEVSTAGE3);
	//GXSetTevOrder(GX_TEVSTAGE4, GX_TEXCOORD0, GX_TEXMAP0, 0xff);
	//GXSetTevColorIn(GX_TEVSTAGE4, GX_CC_ZERO, GX_CC_ZERO, GX_CC_C2, GX_CC_CPREV);
	//GXSetTevColorOp(GX_TEVSTAGE4, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1,
	//				GX_FALSE, GX_TEVPREV);
	//GXSetTevDirect(GX_TEVSTAGE4);
	//
	//
	//// -x pass
	///*GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, 0xff);
	//GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ONE, GX_CC_TEXC, GX_CC_ONE);
	//GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_SUB, GX_TB_ZERO, GX_CS_SCALE_1,
	//				GX_FALSE, GX_TEVPREV);
	//GXSetTevDirect(GX_TEVSTAGE0);*/
	////GXSetTevOrder(GX_TEVSTAGE1, GX_TEXCOORD0, GX_TEXMAP0, 0xff);
	////GXSetTevColorIn(GX_TEVSTAGE1, GX_CC_TEXC, GX_CC_ZERO, GX_CC_CPREV, GX_CC_ZERO);
	////GXSetTevColorOp(GX_TEVSTAGE1, GX_TEV_COMP_R8_GT, GX_TB_ZERO, GX_CS_SCALE_1,
	////				GX_FALSE, GX_TEVPREV);
	////GXSetTevDirect(GX_TEVSTAGE1);
	////GXSetTevOrder(GX_TEVSTAGE2, GX_TEXCOORD0, GX_TEXMAP0, 0xff);
	////GXSetTevColorIn(GX_TEVSTAGE2, GX_CC_ZERO, GX_CC_KONST, GX_CC_CPREV, GX_CC_KONST);
	////GXSetTevColorOp(GX_TEVSTAGE2, GX_TEV_SUB, GX_TB_ZERO, GX_CS_SCALE_1,
	////				GX_FALSE, GX_TEVPREV);
	////GXSetTevDirect(GX_TEVSTAGE2);
	////   GXSetTevKColorSel(GX_TEVSTAGE2, GX_TEV_KCSEL_7_8);

	//// Upper 8 bits change most closest to camera, lower 8 bits change most far from camera, but repeat close to camera due to upper overflowing into upper bits
	//// This basically only keeps first band of middle bits, we do this by verifying that upper bits is 1 (meaning they are not written to)
	//GXSetTevOrder(GX_TEVSTAGE3, GX_TEXCOORD0, GX_TEXMAP0, 0xff);
	//GXSetTevColorIn(GX_TEVSTAGE3, GX_CC_ONE, GX_CC_TEXC, GX_CC_TEXA, GX_CC_ZERO);
	//GXSetTevColorOp(GX_TEVSTAGE3, GX_TEV_COMP_R8_EQ, GX_TB_ZERO, GX_CS_SCALE_1,
	//				GX_FALSE, GX_TEVPREV);
	//GXSetTevDirect(GX_TEVSTAGE3);
	//GXSetTevOrder(GX_TEVSTAGE4, GX_TEXCOORD0, GX_TEXMAP0, 0xff);
	//GXSetTevColorIn(GX_TEVSTAGE4, GX_CC_ZERO, GX_CC_KONST, GX_CC_CPREV, GX_CC_C0);
	//GXSetTevColorOp(GX_TEVSTAGE4, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1,
	//				GX_TRUE, GX_TEVPREV);
	//GXSetTevDirect(GX_TEVSTAGE4);
	//   GXSetTevKColorSel(GX_TEVSTAGE4, GX_TEV_KCSEL_1_8);


	//// +x pass
	//GXSetTevOrder(GX_TEVSTAGE5, GX_TEXCOORD1, GX_TEXMAP0, 0xff);
	//GXSetTevColorIn(GX_TEVSTAGE5, GX_CC_ZERO, GX_CC_ONE, GX_CC_TEXC, GX_CC_ONE);
	//GXSetTevColorOp(GX_TEVSTAGE5, GX_TEV_SUB, GX_TB_ZERO, GX_CS_SCALE_1,
	//                GX_FALSE, GX_TEVPREV);
	//GXSetTevDirect(GX_TEVSTAGE5);
	//GXSetTevOrder(GX_TEVSTAGE6, GX_TEXCOORD1, GX_TEXMAP0, 0xff);
	//GXSetTevColorIn(GX_TEVSTAGE6, GX_CC_TEXC, GX_CC_ZERO, GX_CC_CPREV, GX_CC_ZERO);
	//GXSetTevColorOp(GX_TEVSTAGE6, GX_TEV_COMP_R8_GT, GX_TB_ZERO, GX_CS_SCALE_1,
	//                GX_FALSE, GX_TEVPREV);
	//GXSetTevDirect(GX_TEVSTAGE6);
	//GXSetTevOrder(GX_TEVSTAGE7, GX_TEXCOORD1, GX_TEXMAP0, 0xff);
	//GXSetTevColorIn(GX_TEVSTAGE7, GX_CC_ZERO, GX_CC_KONST, GX_CC_CPREV, GX_CC_KONST);
	//GXSetTevColorOp(GX_TEVSTAGE7, GX_TEV_SUB, GX_TB_ZERO, GX_CS_SCALE_1,
	//                GX_FALSE, GX_TEVREG0);
	//GXSetTevDirect(GX_TEVSTAGE7);
	//   GXSetTevKColorSel(GX_TEVSTAGE7, GX_TEV_KCSEL_7_8);

	//// Upper 8 bits change most closest to camera, lower 8 bits change most far from camera, but repeat close to camera due to upper overflowing into upper bits
	//// This basically only keeps first band of middle bits, we do this by verifying that upper bits is 1 (meaning they are not written to)
	//GXSetTevOrder(GX_TEVSTAGE8, GX_TEXCOORD1, GX_TEXMAP0, 0xff);
	//GXSetTevColorIn(GX_TEVSTAGE8, GX_CC_ONE, GX_CC_TEXC, GX_CC_TEXA, GX_CC_ZERO);
	//GXSetTevColorOp(GX_TEVSTAGE8, GX_TEV_COMP_R8_EQ, GX_TB_ZERO, GX_CS_SCALE_1,
	//                GX_FALSE, GX_TEVPREV);
	//GXSetTevDirect(GX_TEVSTAGE8);

	//GXSetTevOrder(GX_TEVSTAGE9, GX_TEXCOORD1, GX_TEXMAP0, 0xff);
	//GXSetTevColorIn(GX_TEVSTAGE9, GX_CC_ZERO, GX_CC_KONST, GX_CC_CPREV, GX_CC_C0);
	//GXSetTevColorOp(GX_TEVSTAGE9, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1,
	//                GX_TRUE, GX_TEVPREV);
	//GXSetTevDirect(GX_TEVSTAGE9);
	//   GXSetTevKColorSel(GX_TEVSTAGE9, GX_TEV_KCSEL_1_8);
	//
	// Edge detection

	GXSetTevOrder(GX_TEVSTAGE6, GX_TEXCOORD1, GX_TEXMAP0, 0xff);
	GXSetTevColorIn(GX_TEVSTAGE6, GX_CC_ZERO, GX_CC_ONE, GX_CC_C1, GX_CC_C2);
	GXSetTevColorOp(GX_TEVSTAGE6, GX_TEV_SUB, GX_TB_ZERO, GX_CS_SCALE_1,
	                GX_TRUE, GX_TEVPREV);
	GXSetTevDirect(GX_TEVSTAGE6);

	GXSetTevOrder(GX_TEVSTAGE7, GX_TEXCOORD1, GX_TEXMAP0, 0xff);
	GXSetTevColorIn(GX_TEVSTAGE7, GX_CC_CPREV, GX_CC_KONST, GX_CC_CPREV, GX_CC_ZERO);
	GXSetTevColorOp(GX_TEVSTAGE7, GX_TEV_COMP_R8_GT, GX_TB_ZERO, GX_CS_SCALE_1,
	                GX_TRUE, GX_TEVPREV);
	GXSetTevDirect(GX_TEVSTAGE7);
	 GXSetTevKColorSel(GX_TEVSTAGE7, GX_TEV_KCSEL_K0_R);
	 
	 
	GXColor fog_distance = { 4, 0, 0, 0 };
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

	/*
	GXSetNumTevStages(1);
	GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, 0xff);
	GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ONE, GX_CC_TEXC, GX_CC_ONE);
	GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1,
					GX_FALSE, GX_TEVPREV);
	GXSetTevDirect(GX_TEVSTAGE0);*/

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

	// Upper 8 bits change most closest to camera, lower 8 bits change most far from camera, but repeat close to camera due to upper overflowing into upper bits
	// This basically only keeps first band of middle bits, we do this by verifying that upper bits is 1 (meaning they are not written to)
	GXSetTevOrder(GX_TEVSTAGE3, GX_TEXCOORD0, GX_TEXMAP0, 0xff);
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

	// Upper 8 bits change most closest to camera, lower 8 bits change most far from camera, but repeat close to camera due to upper overflowing into upper bits
	// This basically only keeps first band of middle bits, we do this by verifying that upper bits is 1 (meaning they are not written to)
	GXSetTevOrder(GX_TEVSTAGE8, GX_TEXCOORD1, GX_TEXMAP0, 0xff);
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



void TSpookyFilter::drawFilter() {
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

	GXSetTexCopySrc(f_left, f_top, f_wd, f_ht);
	GXSetCopyFilter(GX_FALSE, 0, GX_TRUE, vFilter);
	GXSetTexCopyDst(640 >> 1, 448 >> 1, GX_TF_Z16, GX_TRUE);
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

	GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_U16, 0);
	GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);

	GXSetNumTexGens(1);
	GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, 0x3c, GX_FALSE,
	                  0x7d);
	
	GXLoadTexObj(&tex_obj, GX_TEXMAP0);
	GXLoadTexObj(&gpScreenTexture->texture->mTexObj, GX_TEXMAP1);
	
	GXSetNumTevStages(7);

	setupDepthMap(GX_TEVSTAGE0, 200, GX_TEVREG0, GX_TEXCOORD0); // 0-4 inclusive

	GXSetTevOrder(GX_TEVSTAGE5, GX_TEXCOORD0, GX_TEXMAP1, 0xff);
	GXSetTevColorIn(GX_TEVSTAGE5, GX_CC_KONST, GX_CC_ZERO, GX_CC_TEXC, GX_CC_KONST);
	GXSetTevColorOp(GX_TEVSTAGE5, GX_TEV_SUB, GX_TB_ZERO, GX_CS_SCALE_1,
					GX_TRUE, GX_TEVPREV);
	GXSetTevDirect(GX_TEVSTAGE5);
	GXSetTevKColorSel(GX_TEVSTAGE5, GX_TEV_KCSEL_K0);

	GXSetTevOrder(GX_TEVSTAGE6, GX_TEXCOORD0, GX_TEXMAP1, 0xff);
	GXSetTevColorIn(GX_TEVSTAGE6, GX_CC_ZERO, GX_CC_C0, GX_CC_CPREV, GX_CC_ZERO);
	GXSetTevColorOp(GX_TEVSTAGE6, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1,
					GX_TRUE, GX_TEVPREV);
	GXSetTevDirect(GX_TEVSTAGE6);
	
	GXColor fogColor = { 129, 81, 138, 255 };
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

void TDepthOfField::drawFilter() {

	if(gpApplication.mGamePads[0]->mButtons.mInput & JUTGamePad::DPAD_LEFT) {
		focalDepthMin--;
	}
	
	if(gpApplication.mGamePads[0]->mButtons.mInput & JUTGamePad::DPAD_RIGHT) {
		focalDepthMin++;
	}
	
	if(gpApplication.mGamePads[0]->mButtons.mInput & JUTGamePad::R) {
		focalDepthMax++;
	}
	
	if(gpApplication.mGamePads[0]->mButtons.mInput & JUTGamePad::L) {
		focalDepthMax--;
	}


	OSReport("min %d max %d\n", focalDepthMin, focalDepthMax);

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
	f32 offset_x = (1.5f / f_wd);
	f32 offset_y = (1.5f / f_wd);
	
	GXSetTexCopySrc(f_left, f_top, f_wd, f_ht);
	GXSetCopyFilter(GX_FALSE, 0, GX_TRUE, vFilter);
	GXSetTexCopyDst(640 >> 1, 448 >> 1, GX_TF_Z16, GX_TRUE);
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
	GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, 0x3c, GX_FALSE,
	                  0x7d);
	GXSetTexCoordGen2(GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_TEX1, 0x3c, GX_FALSE,
	                  0x7d);
	GXSetTexCoordGen2(GX_TEXCOORD2, GX_TG_MTX2x4, GX_TG_TEX2, 0x3c, GX_FALSE,
	                  0x7d);
	GXSetTexCoordGen2(GX_TEXCOORD3, GX_TG_MTX2x4, GX_TG_TEX3, 0x3c, GX_FALSE,
	                  0x7d);
	GXSetTexCoordGen2(GX_TEXCOORD4, GX_TG_MTX2x4, GX_TG_TEX4, 0x3c, GX_FALSE,
	                  0x7d);
	
	GXLoadTexObj(&gpScreenTexture->texture->mTexObj, GX_TEXMAP0);
	GXLoadTexObj(&tex_obj, GX_TEXMAP1);

	GXSetNumTevStages(11);

	
	setupDepthMap(GX_TEVSTAGE0, focalDepthMax, GX_TEVREG1, GX_TEXCOORD0, GX_TEXMAP1); // 0-5

	// Stage 0
	GXSetTevOrder(GX_TEVSTAGE5, GX_TEXCOORD1, GX_TEXMAP0, 0xFF);
	GXSetTevColorIn(GX_TEVSTAGE5, GX_CC_ZERO, GX_CC_TEXC, GX_CC_HALF, GX_CC_C0);
	GXSetTevColorOp(GX_TEVSTAGE5, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1,
	                GX_FALSE, GX_TEVPREV);
	GXSetTevAlphaIn(GX_TEVSTAGE5, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO,
	                GX_CA_ZERO);
	GXSetTevAlphaOp(GX_TEVSTAGE5, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1,
	                GX_TRUE, GX_TEVPREV);
	GXSetTevDirect(GX_TEVSTAGE5);

	// Stage 1
	GXSetTevOrder(GX_TEVSTAGE6, GX_TEXCOORD2, GX_TEXMAP0, 0xFF);
	GXSetTevColorIn(GX_TEVSTAGE6, GX_CC_ZERO, GX_CC_TEXC, GX_CC_HALF,
	                GX_CC_CPREV);
	GXSetTevColorOp(GX_TEVSTAGE6, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1,
	                GX_FALSE, GX_TEVPREV);
	GXSetTevAlphaIn(GX_TEVSTAGE6, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO,
	                GX_CA_ZERO);
	GXSetTevAlphaOp(GX_TEVSTAGE6, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1,
	                GX_TRUE, GX_TEVPREV);
	GXSetTevDirect(GX_TEVSTAGE6);

	// Stage 2
	GXSetTevOrder(GX_TEVSTAGE7, GX_TEXCOORD3, GX_TEXMAP0, 0xFF);
	GXSetTevColorIn(GX_TEVSTAGE7, GX_CC_ZERO, GX_CC_TEXC, GX_CC_HALF,
	                GX_CC_CPREV);
	GXSetTevColorOp(GX_TEVSTAGE7, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1,
	                GX_FALSE, GX_TEVPREV);
	GXSetTevAlphaIn(GX_TEVSTAGE7, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO,
	                GX_CA_ZERO);
	GXSetTevAlphaOp(GX_TEVSTAGE7, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1,
	                GX_TRUE, GX_TEVPREV);
	GXSetTevDirect(GX_TEVSTAGE7);

	// Stage 3
	GXSetTevOrder(GX_TEVSTAGE8, GX_TEXCOORD4, GX_TEXMAP0, 0xFF);
	GXSetTevColorIn(GX_TEVSTAGE8, GX_CC_ZERO, GX_CC_TEXC, GX_CC_HALF,
	                GX_CC_CPREV);
	GXSetTevColorOp(GX_TEVSTAGE8, GX_TEV_ADD, GX_TB_ZERO, GX_CS_DIVIDE_2,
	                GX_TRUE, GX_TEVPREV);
	GXSetTevAlphaIn(GX_TEVSTAGE8, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO,
	                GX_CA_ZERO);
	GXSetTevAlphaOp(GX_TEVSTAGE8, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1,
	                GX_TRUE, GX_TEVPREV);
	GXSetTevDirect(GX_TEVSTAGE8);
	
	GXSetTevOrder(GX_TEVSTAGE9, GX_TEXCOORD0, GX_TEXMAP0, 0xFF);
	GXSetTevColorIn(GX_TEVSTAGE9, GX_CC_CPREV, GX_CC_ZERO, GX_CC_C1,
	                GX_CC_ZERO);
	GXSetTevColorOp(GX_TEVSTAGE9, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1,
	                GX_TRUE, GX_TEVPREV);
	GXSetTevAlphaIn(GX_TEVSTAGE9, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO,
	                GX_CA_ZERO);
	GXSetTevAlphaOp(GX_TEVSTAGE9, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1,
	                GX_TRUE, GX_TEVPREV);
	GXSetTevDirect(GX_TEVSTAGE9);
	
	GXSetTevOrder(GX_TEVSTAGE10, GX_TEXCOORD0, GX_TEXMAP0, 0xFF);
	GXSetTevColorIn(GX_TEVSTAGE10, GX_CC_KONST, GX_CC_ZERO, GX_CC_C1,
	                GX_CC_CPREV);
	GXSetTevColorOp(GX_TEVSTAGE10, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1,
	                GX_TRUE, GX_TEVPREV);
	GXSetTevAlphaIn(GX_TEVSTAGE10, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO,
	                GX_CA_ZERO);
	GXSetTevAlphaOp(GX_TEVSTAGE10, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1,
	                GX_TRUE, GX_TEVPREV);
	 GXSetTevKColorSel(GX_TEVSTAGE10, GX_TEV_KCSEL_K0);
	GXSetTevDirect(GX_TEVSTAGE10);

	//rgb(115, 36, 59)
	
	GXColor fogColor = { 42, 13, 22, 255 };
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
