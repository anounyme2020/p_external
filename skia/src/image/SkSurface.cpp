/*
 * Copyright 2012 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkAtomics.h"
#include "SkSurface_Base.h"
#include "SkImagePriv.h"
#include "SkCanvas.h"

#include "SkFontLCDConfig.h"
static SkPixelGeometry compute_default_geometry() {
    SkFontLCDConfig::LCDOrder order = SkFontLCDConfig::GetSubpixelOrder();
    if (SkFontLCDConfig::kNONE_LCDOrder == order) {
        return kUnknown_SkPixelGeometry;
    } else {
        // Bit0 is RGB(0), BGR(1)
        // Bit1 is H(0), V(1)
        const SkPixelGeometry gGeo[] = {
            kRGB_H_SkPixelGeometry,
            kBGR_H_SkPixelGeometry,
            kRGB_V_SkPixelGeometry,
            kBGR_V_SkPixelGeometry,
        };
        int index = 0;
        if (SkFontLCDConfig::kBGR_LCDOrder == order) {
            index |= 1;
        }
        if (SkFontLCDConfig::kVertical_LCDOrientation == SkFontLCDConfig::GetSubpixelOrientation()){
            index |= 2;
        }
        return gGeo[index];
    }
}

SkSurfaceProps::SkSurfaceProps() : fFlags(0), fPixelGeometry(kUnknown_SkPixelGeometry) {}

SkSurfaceProps::SkSurfaceProps(InitType) : fFlags(0), fPixelGeometry(compute_default_geometry()) {}

SkSurfaceProps::SkSurfaceProps(uint32_t flags, InitType)
    : fFlags(flags)
    , fPixelGeometry(compute_default_geometry())
{}

SkSurfaceProps::SkSurfaceProps(uint32_t flags, SkPixelGeometry pg)
    : fFlags(flags), fPixelGeometry(pg)
{}

SkSurfaceProps::SkSurfaceProps(const SkSurfaceProps& other)
    : fFlags(other.fFlags)
    , fPixelGeometry(other.fPixelGeometry)
{}

///////////////////////////////////////////////////////////////////////////////

SkSurface_Base::SkSurface_Base(int width, int height, const SkSurfaceProps* props)
    : INHERITED(width, height, props) {
}

SkSurface_Base::SkSurface_Base(const SkImageInfo& info, const SkSurfaceProps* props)
    : INHERITED(info, props) {
}

SkSurface_Base::~SkSurface_Base() {
    // in case the canvas outsurvives us, we null the callback
    if (fCachedCanvas) {
        fCachedCanvas->setSurfaceBase(nullptr);
    }
}

void SkSurface_Base::onDraw(SkCanvas* canvas, SkScalar x, SkScalar y, const SkPaint* paint) {
    auto image = this->makeImageSnapshot();
    if (image) {
        canvas->drawImage(image, x, y, paint);
    }
}

bool SkSurface_Base::outstandingImageSnapshot() const {
    return fCachedImage && !fCachedImage->unique();
}

void SkSurface_Base::aboutToDraw(ContentChangeMode mode) {
    this->dirtyGenerationID();

    SkASSERT(!fCachedCanvas || fCachedCanvas->getSurfaceBase() == this);

    if (fCachedImage) {
        // the surface may need to fork its backend, if its sharing it with
        // the cached image. Note: we only call if there is an outstanding owner
        // on the image (besides us).
        bool unique = fCachedImage->unique();
        if (!unique) {
            this->onCopyOnWrite(mode);
        }

        // regardless of copy-on-write, we must drop our cached image now, so
        // that the next request will get our new contents.
        fCachedImage.reset();

        if (unique) {
            // Our content isn't held by any image now, so we can consider that content mutable.
            // Raster surfaces need to be told it's safe to consider its pixels mutable again.
            // We make this call after the ->unref() so the subclass can assert there are no images.
            this->onRestoreBackingMutability();
        }
    } else if (kDiscard_ContentChangeMode == mode) {
        this->onDiscard();
    }
}

uint32_t SkSurface_Base::newGenerationID() {
    SkASSERT(!fCachedCanvas || fCachedCanvas->getSurfaceBase() == this);
    static int32_t gID;
    return sk_atomic_inc(&gID) + 1;
}

static SkSurface_Base* asSB(SkSurface* surface) {
    return static_cast<SkSurface_Base*>(surface);
}

///////////////////////////////////////////////////////////////////////////////

SkSurface::SkSurface(int width, int height, const SkSurfaceProps* props)
    : fProps(SkSurfacePropsCopyOrDefault(props)), fWidth(width), fHeight(height)
{
    SkASSERT(fWidth > 0);
    SkASSERT(fHeight > 0);
    fGenerationID = 0;
}

SkSurface::SkSurface(const SkImageInfo& info, const SkSurfaceProps* props)
    : fProps(SkSurfacePropsCopyOrDefault(props)), fWidth(info.width()), fHeight(info.height())
{
    SkASSERT(fWidth > 0);
    SkASSERT(fHeight > 0);
    fGenerationID = 0;
}

uint32_t SkSurface::generationID() {
    if (0 == fGenerationID) {
        fGenerationID = asSB(this)->newGenerationID();
    }
    return fGenerationID;
}

void SkSurface::notifyContentWillChange(ContentChangeMode mode) {
    asSB(this)->aboutToDraw(mode);
}

SkCanvas* SkSurface::getCanvas() {
    return asSB(this)->getCachedCanvas();
}

sk_sp<SkImage> SkSurface::makeImageSnapshot() {
    return asSB(this)->refCachedImage();
}

sk_sp<SkImage> SkSurface::makeImageSnapshot(const SkIRect& srcBounds) {
    const SkIRect surfBounds = { 0, 0, fWidth, fHeight };
    SkIRect bounds = srcBounds;
    if (!bounds.intersect(surfBounds)) {
        return nullptr;
    }
    SkASSERT(!bounds.isEmpty());
    if (bounds == surfBounds) {
        return this->makeImageSnapshot();
    } else {
        return asSB(this)->onNewImageSnapshot(&bounds);
    }
}

sk_sp<SkSurface> SkSurface::makeSurface(const SkImageInfo& info) {
    return asSB(this)->onNewSurface(info);
}

void SkSurface::draw(SkCanvas* canvas, SkScalar x, SkScalar y,
                     const SkPaint* paint) {
    return asSB(this)->onDraw(canvas, x, y, paint);
}

bool SkSurface::peekPixels(SkPixmap* pmap) {
    return this->getCanvas()->peekPixels(pmap);
}

bool SkSurface::readPixels(const SkPixmap& pm, int srcX, int srcY) {
    return this->getCanvas()->readPixels(pm, srcX, srcY);
}

bool SkSurface::readPixels(const SkImageInfo& dstInfo, void* dstPixels, size_t dstRowBytes,
                           int srcX, int srcY) {
    return this->readPixels({dstInfo, dstPixels, dstRowBytes}, srcX, srcY);
}

bool SkSurface::readPixels(const SkBitmap& bitmap, int srcX, int srcY) {
    SkPixmap pm;
    return bitmap.peekPixels(&pm) && this->readPixels(pm, srcX, srcY);
}

void SkSurface::writePixels(const SkPixmap& pmap, int x, int y) {
    if (pmap.addr() == nullptr || pmap.width() <= 0 || pmap.height() <= 0) {
        return;
    }

    const SkIRect srcR = SkIRect::MakeXYWH(x, y, pmap.width(), pmap.height());
    const SkIRect dstR = SkIRect::MakeWH(this->width(), this->height());
    if (SkIRect::Intersects(srcR, dstR)) {
        ContentChangeMode mode = kRetain_ContentChangeMode;
        if (srcR.contains(dstR)) {
            mode = kDiscard_ContentChangeMode;
        }
        asSB(this)->aboutToDraw(mode);
        asSB(this)->onWritePixels(pmap, x, y);
    }
}

void SkSurface::writePixels(const SkBitmap& src, int x, int y) {
    SkPixmap pm;
    if (src.peekPixels(&pm)) {
        this->writePixels(pm, x, y);
    }
}

GrBackendObject SkSurface::getTextureHandle(BackendHandleAccess access) {
    return asSB(this)->onGetTextureHandle(access);
}

bool SkSurface::getRenderTargetHandle(GrBackendObject* obj, BackendHandleAccess access) {
    return asSB(this)->onGetRenderTargetHandle(obj, access);
}

void SkSurface::prepareForExternalIO() {
    this->flush();
}

void SkSurface::flush() {
    asSB(this)->onFlush(0, nullptr);
}

GrSemaphoresSubmitted SkSurface::flushAndSignalSemaphores(int numSemaphores,
                                                          GrBackendSemaphore signalSemaphores[]) {
    return asSB(this)->onFlush(numSemaphores, signalSemaphores);
}

bool SkSurface::wait(int numSemaphores, const GrBackendSemaphore* waitSemaphores) {
    return asSB(this)->onWait(numSemaphores, waitSemaphores);
}

bool SkSurface::characterize(SkSurfaceCharacterization* characterization) const {
    return asSB(const_cast<SkSurface*>(this))->onCharacterize(characterization);
}

bool SkSurface::draw(SkDeferredDisplayList* ddl) {
    return asSB(this)->onDraw(ddl);
}

//////////////////////////////////////////////////////////////////////////////////////
#include "SkNoDrawCanvas.h"

class SkNullSurface : public SkSurface_Base {
public:
    SkNullSurface(int width, int height) : SkSurface_Base(width, height, nullptr) {}

protected:
    SkCanvas* onNewCanvas() override {
        return new SkNoDrawCanvas(this->width(), this->height());
    }
    sk_sp<SkSurface> onNewSurface(const SkImageInfo& info) override {
        return MakeNull(info.width(), info.height());
    }
    sk_sp<SkImage> onNewImageSnapshot(const SkIRect* subsetOrNull) override { return nullptr; }
    void onWritePixels(const SkPixmap&, int x, int y) override {}
    void onDraw(SkCanvas*, SkScalar x, SkScalar y, const SkPaint*) override {}
    void onCopyOnWrite(ContentChangeMode) override {}
};

sk_sp<SkSurface> SkSurface::MakeNull(int width, int height) {
    if (width < 1 || height < 1) {
        return nullptr;
    }
    return sk_sp<SkSurface>(new SkNullSurface(width, height));
}

//////////////////////////////////////////////////////////////////////////////////////

#if !SK_SUPPORT_GPU

sk_sp<SkSurface> SkSurface::MakeRenderTarget(GrContext*, SkBudgeted, const SkImageInfo&, int,
                                             GrSurfaceOrigin, const SkSurfaceProps*, bool) {
    return nullptr;
}

sk_sp<SkSurface> SkSurface::MakeFromBackendTexture(GrContext*, const GrBackendTexture&,
                                                   GrSurfaceOrigin origin, int sampleCnt,
                                                   sk_sp<SkColorSpace>, const SkSurfaceProps*) {
    return nullptr;
}

sk_sp<SkSurface> SkSurface::MakeFromBackendTexture(GrContext*, const GrBackendTexture&,
                                                   GrSurfaceOrigin origin, int sampleCnt,
                                                   SkColorType, sk_sp<SkColorSpace>,
                                                   const SkSurfaceProps*) {
    return nullptr;
}

sk_sp<SkSurface> SkSurface::MakeFromBackendRenderTarget(GrContext*,
                                                        const GrBackendRenderTarget&,
                                                        GrSurfaceOrigin origin,
                                                        sk_sp<SkColorSpace>,
                                                        const SkSurfaceProps*) {
    return nullptr;
}

sk_sp<SkSurface> SkSurface::MakeFromBackendRenderTarget(GrContext*,
                                                        const GrBackendRenderTarget&,
                                                        GrSurfaceOrigin origin,
                                                        SkColorType,
                                                        sk_sp<SkColorSpace>,
                                                        const SkSurfaceProps*) {
    return nullptr;
}

sk_sp<SkSurface> SkSurface::MakeFromBackendTextureAsRenderTarget(GrContext*,
                                                                 const GrBackendTexture&,
                                                                 GrSurfaceOrigin origin,
                                                                 int sampleCnt,
                                                                 sk_sp<SkColorSpace>,
                                                                 const SkSurfaceProps*) {
    return nullptr;
}

sk_sp<SkSurface> SkSurface::MakeFromBackendTextureAsRenderTarget(GrContext*,
                                                                 const GrBackendTexture&,
                                                                 GrSurfaceOrigin origin,
                                                                 int sampleCnt,
                                                                 SkColorType,
                                                                 sk_sp<SkColorSpace>,
                                                                 const SkSurfaceProps*) {
    return nullptr;
}

#endif
