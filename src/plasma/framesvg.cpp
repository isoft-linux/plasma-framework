/*
 *   Copyright 2008-2010 by Aaron Seigo <aseigo@kde.org>
 *   Copyright 2008-2010 Marco Martin <notmart@gmail.com>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Library General Public License as
 *   published by the Free Software Foundation; either version 2, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details
 *
 *   You should have received a copy of the GNU Library General Public
 *   License along with this program; if not, write to the
 *   Free Software Foundation, Inc.,
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "framesvg.h"
#include "private/framesvg_p.h"

#include <QAtomicInt>
#include <QBitmap>
#include <QCryptographicHash>
#include <QPainter>
#include <QRegion>
#include <QSize>
#include <QStringBuilder>
#include <QTimer>

#include <QDebug>

#include "theme.h"
#include "private/svg_p.h"
#include "private/framesvg_helpers.h"
#include "debug_p.h"

namespace Plasma
{

QHash<ThemePrivate *, QHash<QString, FrameData *> > FrameSvgPrivate::s_sharedFrames;

// Any attempt to generate a frame whose width or height is larger than this
// will be rejected
static const int MAX_FRAME_SIZE = 100000;

FrameData::~FrameData()
{
    foreach (FrameSvg *frame, references.keys()) {
        frame->d->frames.remove(prefix);
    }
}


FrameSvg::FrameSvg(QObject *parent)
    : Svg(parent),
      d(new FrameSvgPrivate(this))
{
    connect(this, SIGNAL(repaintNeeded()), this, SLOT(updateNeeded()));
    d->frames.insert(QString(), new FrameData(this, QString()));
}

FrameSvg::~FrameSvg()
{
    delete d;
}

void FrameSvg::setImagePath(const QString &path)
{
    if (path == imagePath()) {
        return;
    }

    bool updateNeeded = true;
    clearCache();

    FrameData *fd = d->frames[d->prefix];
    if (fd->refcount() == 1) {
        // we're the only user of it, let's remove it from the shared keys
        // we don't want to deref it, however, as we'll still be using it
        const QString oldKey = d->cacheId(fd, d->prefix);
        FrameSvgPrivate::s_sharedFrames[fd->theme].remove(oldKey);
    } else {
        // others are using this frame, so deref it for ourselves
        fd->deref(this);
        fd = 0;
    }

    Svg::d->setImagePath(path);

    if (!fd) {
        // we need to replace our frame, start by looking in the frame cache
        FrameData *oldFd = d->frames[d->prefix];
        const QString key = d->cacheId(oldFd, d->prefix);
        fd = FrameSvgPrivate::s_sharedFrames[theme()->d].value(key);

        if (fd) {
            // we found one, so ref it and use it; we also don't need to (or want to!)
            // trigger a full update of the frame since it is already the one we want
            // and likely already rendered just fine
            fd->ref(this);
            updateNeeded = false;
        } else {
            // nothing exists for us in the cache, so create a new FrameData based
            // on the old one
            fd = new FrameData(*oldFd, this);
        }

        d->frames.insert(d->prefix, fd);
    }

    setContainsMultipleImages(true);
    if (updateNeeded) {
        // ensure our frame is in the cache
        const QString key = d->cacheId(fd, d->prefix);
        FrameSvgPrivate::s_sharedFrames[theme()->d].insert(key, fd);
        fd->theme = theme()->d;

        // this will emit repaintNeeded() as well when it is done
        d->updateAndSignalSizes();
    } else {
        emit repaintNeeded();
    }
}

void FrameSvg::setEnabledBorders(const EnabledBorders borders)
{
    if (borders == d->frames[d->prefix]->enabledBorders) {
        return;
    }

    FrameData *fd = d->frames[d->prefix];

    const QString oldKey = d->cacheId(fd, d->prefix);
    const EnabledBorders oldBorders = fd->enabledBorders;
    fd->enabledBorders = borders;
    const QString newKey = d->cacheId(fd, d->prefix);
    fd->enabledBorders = oldBorders;

    //qCDebug(LOG_PLASMA) << "looking for" << newKey;
    FrameData *newFd = FrameSvgPrivate::s_sharedFrames[theme()->d].value(newKey);
    if (newFd) {
        //qCDebug(LOG_PLASMA) << "FOUND IT!" << newFd->refcount;
        // we've found a math, so insert that new one and ref it ..
        newFd->ref(this);
        d->frames.insert(d->prefix, newFd);

        //.. then deref the old one and if it's no longer used, get rid of it
        if (fd->deref(this)) {
            //const QString oldKey = d->cacheId(fd, d->prefix);
            //qCDebug(LOG_PLASMA) << "1. Removing it" << oldKey << fd->refcount;
            FrameSvgPrivate::s_sharedFrames[fd->theme].remove(oldKey);
            delete fd;
        }

        return;
    }

    if (fd->refcount() == 1) {
        // we're the only user of it, let's remove it from the shared keys
        // we don't want to deref it, however, as we'll still be using it
        FrameSvgPrivate::s_sharedFrames[fd->theme].remove(oldKey);
    } else {
        // others are using it, but we wish to change its size. so deref it,
        // then create a copy of it (we're automatically ref'd via the ctor),
        // then insert it into our frames.
        fd->deref(this);
        fd = new FrameData(*fd, this);
        d->frames.insert(d->prefix, fd);
    }

    fd->enabledBorders = borders;
    d->updateAndSignalSizes();
}

FrameSvg::EnabledBorders FrameSvg::enabledBorders() const
{
    if (d->frames.isEmpty()) {
        return NoBorder;
    }

    QHash<QString, FrameData *>::const_iterator it = d->frames.constFind(d->prefix);

    if (it != d->frames.constEnd()) {
        return it.value()->enabledBorders;
    } else {
        return NoBorder;
    }
}

void FrameSvg::setElementPrefix(Plasma::Types::Location location)
{
    switch (location) {
    case Types::TopEdge:
        setElementPrefix(QStringLiteral("north"));
        break;
    case Types::BottomEdge:
        setElementPrefix(QStringLiteral("south"));
        break;
    case Types::LeftEdge:
        setElementPrefix(QStringLiteral("west"));
        break;
    case Types::RightEdge:
        setElementPrefix(QStringLiteral("east"));
        break;
    default:
        setElementPrefix(QString());
        break;
    }

    d->location = location;
}

void FrameSvg::setElementPrefix(const QString &prefix)
{
    const QString oldPrefix(d->prefix);

    if (!hasElement(prefix % QLatin1String("-center"))) {
        d->prefix.clear();
    } else {
        d->prefix = prefix;
        if (!d->prefix.isEmpty()) {
            d->prefix += '-';
        }
    }
    d->requestedPrefix = prefix;

    FrameData *oldFrameData = d->frames.value(oldPrefix);
    if (oldPrefix == d->prefix && oldFrameData) {
        return;
    }

    if (!d->frames.contains(d->prefix)) {
        if (oldFrameData) {
            FrameData *newFd = 0;
            if (!oldFrameData->frameSize.isEmpty()) {
                const QString key = d->cacheId(oldFrameData, d->prefix);
                newFd = FrameSvgPrivate::s_sharedFrames[theme()->d].value(key);
                if (newFd && newFd->devicePixelRatio != devicePixelRatio()) {
                    newFd = 0;
                }
            }

            // we need to put this in the cache if we didn't find it in the shared frames
            // and we have a size; if we don't have a size, we'll catch it later
            const bool cache = !newFd && !oldFrameData->frameSize.isEmpty();
            if (newFd) {
                newFd->ref(this);
            } else  {
                newFd = new FrameData(*oldFrameData, this);
            }

            d->frames.insert(d->prefix, newFd);

            if (cache) {
                // we have to cache after inserting the frame since the cacheId requires the
                // frame to be in the frames collection already
                const QString key = d->cacheId(oldFrameData, d->prefix);
                //qCDebug(LOG_PLASMA) << this << "     1. inserting as" << key;

                FrameSvgPrivate::s_sharedFrames[theme()->d].insert(key, newFd);
                newFd->theme = theme()->d;
            }
        } else {
            // couldn't find anything useful, so we just create something here
            // we don't have a size for it yet, so don't bother trying to share it just yet
            FrameData *newFd = new FrameData(this, d->prefix);
            d->frames.insert(d->prefix, newFd);
        }

        d->updateSizes();
    }

    if (!d->cacheAll) {
        d->frames.remove(oldPrefix);
        if (oldFrameData) {
            if (oldFrameData->deref(this)) {
                const QString oldKey = d->cacheId(oldFrameData, oldPrefix);
                FrameSvgPrivate::s_sharedFrames[oldFrameData->theme].remove(oldKey);
                delete oldFrameData;
            }
        }
    }

    d->location = Types::Floating;
}

bool FrameSvg::hasElementPrefix(const QString &prefix) const
{
    //for now it simply checks if a center element exists,
    //because it could make sense for certain themes to not have all the elements
    if (prefix.isEmpty()) {
        return hasElement(QStringLiteral("center"));
    } else {
        return hasElement(prefix % QLatin1String("-center"));
    }
}

bool FrameSvg::hasElementPrefix(Plasma::Types::Location location) const
{
    switch (location) {
    case Types::TopEdge:
        return hasElementPrefix(QStringLiteral("north"));
        break;
    case Types::BottomEdge:
        return hasElementPrefix(QStringLiteral("south"));
        break;
    case Types::LeftEdge:
        return hasElementPrefix(QStringLiteral("west"));
        break;
    case Types::RightEdge:
        return hasElementPrefix(QStringLiteral("east"));
        break;
    default:
        return hasElementPrefix(QString());
        break;
    }
}

QString FrameSvg::prefix()
{
    return d->requestedPrefix;
}

void FrameSvg::resizeFrame(const QSizeF &size)
{
    if (imagePath().isEmpty()) {
        return;
    }

    if (size.isEmpty()) {
#ifndef NDEBUG
        // qCDebug(LOG_PLASMA) << "Invalid size" << size;
#endif
        return;
    }

    FrameData *fd = d->frames[d->prefix];
    if (size == fd->frameSize) {
        return;
    }

    const QString oldKey = d->cacheId(fd, d->prefix);
    const QSize currentSize = fd->frameSize;
    fd->frameSize = size.toSize();
    const QString newKey = d->cacheId(fd, d->prefix);
    fd->frameSize = currentSize;

    //qCDebug(LOG_PLASMA) << "looking for" << newKey;
    FrameData *newFd = FrameSvgPrivate::s_sharedFrames[theme()->d].value(newKey);
    if (newFd) {
        //qCDebug(LOG_PLASMA) << "FOUND IT!" << newFd->refcount;
        // we've found a math, so insert that new one and ref it ..
        newFd->ref(this);
        d->frames.insert(d->prefix, newFd);

        //.. then deref the old one and if it's no longer used, get rid of it
        if (fd->deref(this)) {
            //const QString oldKey = d->cacheId(fd, d->prefix);
            //qCDebug(LOG_PLASMA) << "1. Removing it" << oldKey << fd->refcount;
            FrameSvgPrivate::s_sharedFrames[fd->theme].remove(oldKey);
            delete fd;
        }

        return;
    }

    if (fd->refcount() == 1) {
        // we're the only user of it, let's remove it from the shared keys
        // we don't want to deref it, however, as we'll still be using it
        FrameSvgPrivate::s_sharedFrames[fd->theme].remove(oldKey);
    } else {
        // others are using it, but we wish to change its size. so deref it,
        // then create a copy of it (we're automatically ref'd via the ctor),
        // then insert it into our frames.
        fd->deref(this);
        fd = new FrameData(*fd, this);
        d->frames.insert(d->prefix, fd);
    }

    d->updateSizes();
    fd->frameSize = size.toSize();
    // we know it isn't in s_sharedFrames due to the check above, so insert it now
    FrameSvgPrivate::s_sharedFrames[theme()->d].insert(newKey, fd);
    fd->theme = theme()->d;
}

QSizeF FrameSvg::frameSize() const
{
    QHash<QString, FrameData *>::const_iterator it = d->frames.constFind(d->prefix);

    if (it == d->frames.constEnd()) {
        return QSize(-1, -1);
    } else {
        return d->frameSize(it.value());
    }
}

qreal FrameSvg::marginSize(const Plasma::Types::MarginEdge edge) const
{
    if (d->frames[d->prefix]->noBorderPadding) {
        return .0;
    }

    switch (edge) {
    case Plasma::Types::TopMargin:
        return d->frames[d->prefix]->topMargin;
        break;

    case Plasma::Types::LeftMargin:
        return d->frames[d->prefix]->leftMargin;
        break;

    case Plasma::Types::RightMargin:
        return d->frames[d->prefix]->rightMargin;
        break;

    //Plasma::BottomMargin
    default:
        return d->frames[d->prefix]->bottomMargin;
        break;
    }
}

qreal FrameSvg::fixedMarginSize(const Plasma::Types::MarginEdge edge) const
{
    if (d->frames[d->prefix]->noBorderPadding) {
        return .0;
    }

    switch (edge) {
    case Plasma::Types::TopMargin:
        return d->frames[d->prefix]->fixedTopMargin;
        break;

    case Plasma::Types::LeftMargin:
        return d->frames[d->prefix]->fixedLeftMargin;
        break;

    case Plasma::Types::RightMargin:
        return d->frames[d->prefix]->fixedRightMargin;
        break;

    //Plasma::BottomMargin
    default:
        return d->frames[d->prefix]->fixedBottomMargin;
        break;
    }
}

void FrameSvg::getMargins(qreal &left, qreal &top, qreal &right, qreal &bottom) const
{
    FrameData *frame = d->frames[d->prefix];

    if (frame->noBorderPadding) {
        left = top = right = bottom = 0;
        return;
    }

    top = frame->topMargin;
    left = frame->leftMargin;
    right = frame->rightMargin;
    bottom = frame->bottomMargin;
}

void FrameSvg::getFixedMargins(qreal &left, qreal &top, qreal &right, qreal &bottom) const
{
    FrameData *frame = d->frames[d->prefix];

    if (frame->noBorderPadding) {
        left = top = right = bottom = 0;
        return;
    }

    top = frame->fixedTopMargin;
    left = frame->fixedLeftMargin;
    right = frame->fixedRightMargin;
    bottom = frame->fixedBottomMargin;
}

QRectF FrameSvg::contentsRect() const
{
    FrameData* frame = d->frames.value(d->prefix);
    if (frame) {
        QRectF rect(QPoint(0,0), frame->frameSize);
        return rect.adjusted(frame->leftMargin, frame->topMargin, -frame->rightMargin, -frame->bottomMargin);
    } else {
        return QRectF();
    }
}

QPixmap FrameSvg::alphaMask() const
{
    //FIXME: the distinction between overlay and
    return d->alphaMask();
}

QRegion FrameSvg::mask() const
{
    FrameData *frame = d->frames[d->prefix];
    QString id = d->cacheId(frame, QString());

    QRegion* obj = frame->cachedMasks.object(id);
    QRegion result;

    if (!obj) {
        obj = new QRegion(QBitmap(d->alphaMask().alphaChannel().createMaskFromColor(Qt::black)));
        result = *obj;
        frame->cachedMasks.insert(id, obj);
    }
    else {
        result = *obj;
    }
    return result;
}

void FrameSvg::setCacheAllRenderedFrames(bool cache)
{
    if (d->cacheAll && !cache) {
        clearCache();
    }

    d->cacheAll = cache;
}

bool FrameSvg::cacheAllRenderedFrames() const
{
    return d->cacheAll;
}

void FrameSvg::clearCache()
{
    FrameData *frame = d->frames[d->prefix];

    // delete all the frames that aren't this one
    QMutableHashIterator<QString, FrameData *> it(d->frames);
    while (it.hasNext()) {
        FrameData *p = it.next().value();
        if (frame != p) {
            //TODO: should we clear from the Theme pixmap cache as well?
            if (p->deref(this)) {
                const QString key = d->cacheId(p, it.key());
                FrameSvgPrivate::s_sharedFrames[p->theme].remove(key);
                p->cachedBackground = QPixmap();
            }

            it.remove();
        }
    }
}

QPixmap FrameSvg::framePixmap()
{
    FrameData *frame = d->frames[d->prefix];
    if (frame->cachedBackground.isNull()) {
        d->generateBackground(frame);
    }

    return frame->cachedBackground;
}

void FrameSvg::paintFrame(QPainter *painter, const QRectF &target, const QRectF &source)
{
    FrameData *frame = d->frames[d->prefix];
    if (frame->cachedBackground.isNull()) {
        d->generateBackground(frame);
        if (frame->cachedBackground.isNull()) {
            return;
        }
    }

    painter->drawPixmap(target, frame->cachedBackground, source.isValid() ? source : target);
}

void FrameSvg::paintFrame(QPainter *painter, const QPointF &pos)
{
    FrameData *frame = d->frames[d->prefix];
    if (frame->cachedBackground.isNull()) {
        d->generateBackground(frame);
        if (frame->cachedBackground.isNull()) {
            return;
        }
    }

    painter->drawPixmap(pos, frame->cachedBackground);
}

//#define DEBUG_FRAMESVG_CACHE
FrameSvgPrivate::~FrameSvgPrivate()
{
#ifdef DEBUG_FRAMESVG_CACHE
#ifndef NDEBUG
    // qCDebug(LOG_PLASMA) << "*************" << q << q->imagePath() << "****************";
#endif
#endif

    QHashIterator<QString, FrameData *> it(frames);
    while (it.hasNext()) {
        it.next();
        if (it.value()) {
            // we remove all references from this widget to the frame, and delete it if we're the
            // last user
            if (it.value()->removeRefs(q)) {
                const QString key = cacheId(it.value(), it.key());
#ifdef DEBUG_FRAMESVG_CACHE
#ifndef NDEBUG
                // qCDebug(LOG_PLASMA) << "2. Removing it" << key << it.value() << it.value()->refcount() << s_sharedFrames[theme()->d].contains(key);
#endif
#endif
                s_sharedFrames[it.value()->theme].remove(key);
                delete it.value();
            }
#ifdef DEBUG_FRAMESVG_CACHE
            else {
#ifndef NDEBUG
                // qCDebug(LOG_PLASMA) << "still shared:" << cacheId(it.value(), it.key()) << it.value() << it.value()->refcount() << it.value()->isUsed();
#endif
            }
        } else {
#ifndef NDEBUG
            // qCDebug(LOG_PLASMA) << "lost our value for" << it.key();
#endif
#endif
        }
    }

#ifdef DEBUG_FRAMESVG_CACHE
    QHashIterator<QString, FrameData *> it2(s_sharedFrames[theme()->d]);
    int shares = 0;
    while (it2.hasNext()) {
        it2.next();
        const int rc = it2.value()->refcount();
        if (rc == 0) {
#ifndef NDEBUG
            // qCDebug(LOG_PLASMA) << "     LOST!" << it2.key() << rc << it2.value();// << it2.value()->references;
#endif
        } else {
#ifndef NDEBUG
            // qCDebug(LOG_PLASMA) << "          " << it2.key() << rc << it2.value();
#endif
            foreach (FrameSvg *data, it2.value()->references.keys()) {
#ifndef NDEBUG
                qCDebug(LOG_PLASMA) << "            " << (void *)data << it2.value()->references[data];
#endif
            }
            shares += rc - 1;
        }
    }
#ifndef NDEBUG
    // qCDebug(LOG_PLASMA) << "#####################################" << s_sharedFrames[theme()->d].count() << ", pixmaps saved:" << shares;
#endif
#endif

    frames.clear();
}

QPixmap FrameSvgPrivate::alphaMask()
{
    FrameData *frame = frames[prefix];
    QString maskPrefix;

    if (q->hasElement(QLatin1String("mask-") % prefix % QLatin1String("center"))) {
        maskPrefix = QStringLiteral("mask-");
    }

    if (maskPrefix.isNull()) {
        if (frame->cachedBackground.isNull()) {
            generateBackground(frame);
            if (frame->cachedBackground.isNull()) {
                return QPixmap();
            }
        }

        return frame->cachedBackground;
    } else {
        QString oldPrefix = prefix;

        // We are setting the prefix only temporary to generate
        // the needed mask image
        prefix = maskPrefix % oldPrefix;

        if (!frames.contains(prefix)) {
            const QString key = cacheId(frame, prefix);
            // see if we can find a suitable candidate in the shared frames
            // if successful, ref and insert, otherwise create a new one
            // and insert that into both the shared frames and our frames.
            FrameData *maskFrame = s_sharedFrames[q->theme()->d].value(key);

            if (maskFrame) {
                maskFrame->ref(q);
            } else {
                maskFrame = new FrameData(*frame, q);
                s_sharedFrames[q->theme()->d].insert(key, maskFrame);
                maskFrame->theme = q->theme()->d;
            }
            maskFrame->enabledBorders = frame->enabledBorders;

            frames.insert(prefix, maskFrame);
            updateSizes();
        }

        FrameData *maskFrame = frames[prefix];
        maskFrame->enabledBorders = frame->enabledBorders;
        if (maskFrame->cachedBackground.isNull() || maskFrame->frameSize != frameSize(frame)) {
            const QString oldKey = cacheId(maskFrame, prefix);
            maskFrame->frameSize = frameSize(frame).toSize();
            const QString newKey = cacheId(maskFrame, prefix);
            if (s_sharedFrames[q->theme()->d].contains(oldKey)) {
                s_sharedFrames[q->theme()->d].remove(oldKey);
                s_sharedFrames[q->theme()->d].insert(newKey, maskFrame);
            }

            maskFrame->cachedBackground = QPixmap();

            generateBackground(maskFrame);
            if (maskFrame->cachedBackground.isNull()) {
                return QPixmap();
            }
        }

        prefix = oldPrefix;
        return maskFrame->cachedBackground;
    }
}

void FrameSvgPrivate::generateBackground(FrameData *frame)
{
    if (!frame->cachedBackground.isNull() || !q->hasElementPrefix(q->prefix())) {
        return;
    }

    const QString id = cacheId(frame, prefix);

    bool frameCached = !frame->cachedBackground.isNull();
    bool overlayCached = false;
    const bool overlayAvailable = !prefix.startsWith(QLatin1String("mask-")) && q->hasElement(prefix % QLatin1String("overlay"));
    QPixmap overlay;
    if (q->isUsingRenderingCache()) {
        frameCached = q->theme()->findInCache(id, frame->cachedBackground) && !frame->cachedBackground.isNull();

        if (overlayAvailable) {
            overlayCached = q->theme()->findInCache(QLatin1String("overlay_") % id, overlay) && !overlay.isNull();
        }
    }

    if (!frameCached) {
        generateFrameBackground(frame);
    }

    //Overlays
    QSize overlaySize;
    QPoint actualOverlayPos = QPoint(0, 0);
    if (overlayAvailable && !overlayCached) {
        overlaySize = q->elementSize(prefix % QLatin1String("overlay"));

        if (q->hasElement(prefix % QLatin1String("hint-overlay-pos-right"))) {
            actualOverlayPos.setX(frame->frameSize.width() - overlaySize.width());
        } else if (q->hasElement(prefix % QLatin1String("hint-overlay-pos-bottom"))) {
            actualOverlayPos.setY(frame->frameSize.height() - overlaySize.height());
            //Stretched or Tiled?
        } else if (q->hasElement(prefix % QLatin1String("hint-overlay-stretch"))) {
            overlaySize = frameSize(frame).toSize();
        } else {
            if (q->hasElement(prefix % QLatin1String("hint-overlay-tile-horizontal"))) {
                overlaySize.setWidth(frameSize(frame).width());
            }
            if (q->hasElement(prefix % QLatin1String("hint-overlay-tile-vertical"))) {
                overlaySize.setHeight(frameSize(frame).height());
            }
        }

        overlay = alphaMask();
        QPainter overlayPainter(&overlay);
        overlayPainter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        //Tiling?
        if (q->hasElement(prefix % QLatin1String("hint-overlay-tile-horizontal")) ||
                q->hasElement(prefix % QLatin1String("hint-overlay-tile-vertical"))) {

            QSize s = q->size();
            q->resize(q->elementSize(prefix % QLatin1String("overlay")));

            overlayPainter.drawTiledPixmap(QRect(QPoint(0, 0), overlaySize), q->pixmap(prefix % QLatin1String("overlay")));
            q->resize(s);
        } else {
            q->paint(&overlayPainter, QRect(actualOverlayPos, overlaySize), prefix % QLatin1String("overlay"));
        }

        overlayPainter.end();
    }

    if (!frameCached) {
        cacheFrame(prefix, frame->cachedBackground, overlayCached ? overlay : QPixmap());
    }

    if (!overlay.isNull()) {
        QPainter p(&frame->cachedBackground);
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        p.drawPixmap(actualOverlayPos, overlay, QRect(actualOverlayPos, overlaySize));
    }
}

void FrameSvgPrivate::generateFrameBackground(FrameData *frame)
{
    //qCDebug(LOG_PLASMA) << "generating background";
    const QSize size = frameSize(frame).toSize() * q->devicePixelRatio();

    if (!size.isValid()) {
#ifndef NDEBUG
        // qCDebug(LOG_PLASMA) << "Invalid frame size" << size;
#endif
        return;
    }
    if (size.width() >= MAX_FRAME_SIZE || size.height() >= MAX_FRAME_SIZE) {
        qCWarning(LOG_PLASMA) << "Not generating frame background for a size whose width or height is more than" << MAX_FRAME_SIZE << size;
        return;
    }

    frame->cachedBackground = QPixmap(size);
    frame->cachedBackground.fill(Qt::transparent);
    QPainter p(&frame->cachedBackground);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    QRect contentRect = contentGeometry(frame, size);
    paintCenter(p, frame, contentRect, size);

    paintCorner(p, frame, FrameSvg::LeftBorder|FrameSvg::TopBorder, contentRect);
    paintCorner(p, frame, FrameSvg::RightBorder|FrameSvg::TopBorder, contentRect);
    paintCorner(p, frame, FrameSvg::LeftBorder|FrameSvg::BottomBorder, contentRect);
    paintCorner(p, frame, FrameSvg::RightBorder|FrameSvg::BottomBorder, contentRect);

    // Sides
    const int leftHeight = q->elementSize(prefix % QLatin1String("left")).height();
    paintBorder(p, frame, FrameSvg::LeftBorder, QSize(frame->leftWidth, leftHeight) * q->devicePixelRatio(), contentRect);
    paintBorder(p, frame, FrameSvg::RightBorder, QSize(frame->rightWidth, leftHeight) * q->devicePixelRatio(), contentRect);

    const int topWidth = q->elementSize(prefix % QLatin1String("top")).width();
    paintBorder(p, frame, FrameSvg::TopBorder, QSize(topWidth, frame->topHeight) * q->devicePixelRatio(), contentRect);
    paintBorder(p, frame, FrameSvg::BottomBorder, QSize(topWidth, frame->bottomHeight) * q->devicePixelRatio(), contentRect);
    p.end();

    frame->cachedBackground.setDevicePixelRatio(q->devicePixelRatio());
}

QRect FrameSvgPrivate::contentGeometry(FrameData* frame, const QSize& size) const
{
    const QSize contentSize(size.width() - frame->leftWidth * q->devicePixelRatio() - frame->rightWidth * q->devicePixelRatio(),
                            size.height() - frame->topHeight * q->devicePixelRatio() - frame->bottomHeight * q->devicePixelRatio());
    QRect contentRect(QPoint(0,0), contentSize);
    if (frame->enabledBorders & FrameSvg::LeftBorder && q->hasElement(prefix % QLatin1String("left"))) {
        contentRect.translate(frame->leftWidth * q->devicePixelRatio(), 0);
    }

    // Corners
    if (frame->enabledBorders & FrameSvg::TopBorder && q->hasElement(prefix % QLatin1String("top"))) {
        contentRect.translate(0, frame->topHeight * q->devicePixelRatio());
    }
    return contentRect;
}

void FrameSvgPrivate::paintCenter(QPainter& p, FrameData* frame, const QRect& contentRect, const QSize& fullSize)
{
    if (!contentRect.isEmpty()) {
        const QString centerElementId = prefix % QLatin1String("center");
        if (frame->tileCenter) {
            QSize centerTileSize = q->elementSize(centerElementId);
            QPixmap center(centerTileSize);
            center.fill(Qt::transparent);

            QPainter centerPainter(&center);
            centerPainter.setCompositionMode(QPainter::CompositionMode_Source);
            q->paint(&centerPainter, QRect(QPoint(0, 0), centerTileSize),centerElementId);

            if (frame->composeOverBorder) {
                p.drawTiledPixmap(QRect(QPoint(0, 0), fullSize), center);
            } else {
                p.drawTiledPixmap(FrameSvgHelpers::sectionRect(FrameSvg::NoBorder, contentRect, fullSize * q->devicePixelRatio()), center);
            }
        } else {
            if (frame->composeOverBorder) {
                q->paint(&p, QRect(QPoint(0, 0), fullSize),
                         centerElementId);
            } else {
                q->paint(&p, FrameSvgHelpers::sectionRect(FrameSvg::NoBorder, contentRect, fullSize * q->devicePixelRatio()), centerElementId);
            }
        }
    }

    if (frame->composeOverBorder) {
        p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        p.drawPixmap(QRect(QPoint(0, 0), fullSize), alphaMask());
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    }
}

void FrameSvgPrivate::paintBorder(QPainter& p, FrameData* frame, const FrameSvg::EnabledBorders borders, const QSize& size, const QRect& contentRect) const
{
    QString side = prefix % FrameSvgHelpers::borderToElementId(borders);
    if (frame->enabledBorders & borders && q->hasElement(side) && !size.isEmpty()) {
        if (frame->stretchBorders) {
            q->paint(&p, FrameSvgHelpers::sectionRect(borders, contentRect, frame->frameSize * q->devicePixelRatio()), side);
        } else {
            QPixmap px(size);
            px.fill(Qt::transparent);

            QPainter sidePainter(&px);
            sidePainter.setCompositionMode(QPainter::CompositionMode_Source);
            q->paint(&sidePainter, QRect(QPoint(0, 0), size), side);

            p.drawTiledPixmap(FrameSvgHelpers::sectionRect(borders, contentRect, frame->frameSize * q->devicePixelRatio()), px);
        }
    }
}

void FrameSvgPrivate::paintCorner(QPainter& p, FrameData* frame, Plasma::FrameSvg::EnabledBorders border, const QRect& contentRect) const
{
    QString corner = prefix % FrameSvgHelpers::borderToElementId(border);
    if (frame->enabledBorders & border && q->hasElement(corner)) {
        q->paint(&p, FrameSvgHelpers::sectionRect(border, contentRect, frame->frameSize * q->devicePixelRatio()), corner);
    }
}

QString FrameSvgPrivate::cacheId(FrameData *frame, const QString &prefixToSave) const
{
    const QSize size = frameSize(frame).toSize();
    const QLatin1Char s('_');
    return QString::number(frame->enabledBorders) % s % QString::number(size.width()) % s % QString::number(size.height()) % s % QString::number(q->scaleFactor()) % s % QString::number(q->devicePixelRatio()) % s % prefixToSave % s % q->imagePath();
}

void FrameSvgPrivate::cacheFrame(const QString &prefixToSave, const QPixmap &background, const QPixmap &overlay)
{
    if (!q->isUsingRenderingCache()) {
        return;
    }

    //insert background
    FrameData *frame = frames.value(prefixToSave);

    if (!frame) {
        return;
    }

    const QString id = cacheId(frame, prefixToSave);

    //qCDebug(LOG_PLASMA)<<"Saving to cache frame"<<id;

    q->theme()->insertIntoCache(id, background, QString::number((qint64)q, 16) % prefixToSave);

    if (!overlay.isNull()) {
        //insert overlay
        q->theme()->insertIntoCache(QLatin1String("overlay_") % id, overlay, QString::number((qint64)q, 16) % prefixToSave % QLatin1String("overlay"));
    }
}

void FrameSvgPrivate::updateSizes() const
{
    //qCDebug(LOG_PLASMA) << "!!!!!!!!!!!!!!!!!!!!!! updating sizes" << prefix;
    FrameData *frame = frames[prefix];
    Q_ASSERT(frame);

    QSize s = q->size();
    q->resize();
    frame->cachedBackground = QPixmap();

    //This has the same size regardless the border is enabled or not
    frame->fixedTopHeight = q->elementSize(prefix % QLatin1String("top")).height();

    if (q->hasElement(prefix % QLatin1String("hint-top-margin"))) {
        frame->fixedTopMargin = q->elementSize(prefix % QLatin1String("hint-top-margin")).height();
    } else {
        frame->fixedTopMargin = frame->fixedTopHeight;
    }

    //The same, but its size depends from the margin being enabled
    if (frame->enabledBorders & FrameSvg::TopBorder) {
        frame->topHeight = q->elementSize(prefix % QLatin1String("top")).height();

        if (q->hasElement(prefix % QLatin1String("hint-top-margin"))) {
            frame->topMargin = q->elementSize(prefix % QLatin1String("hint-top-margin")).height();
        } else {
            frame->topMargin = frame->topHeight;
        }
    } else {
        frame->topMargin = frame->topHeight = 0;
    }

    frame->fixedLeftWidth = q->elementSize(prefix % QLatin1String("left")).width();

    if (q->hasElement(prefix % QLatin1String("hint-left-margin"))) {
        frame->fixedLeftMargin = q->elementSize(prefix % QLatin1String("hint-left-margin")).width();
    } else {
        frame->fixedLeftMargin = frame->fixedLeftWidth;
    }

    if (frame->enabledBorders & FrameSvg::LeftBorder) {
        frame->leftWidth = q->elementSize(prefix % QLatin1String("left")).width();

        if (q->hasElement(prefix % QLatin1String("hint-left-margin"))) {
            frame->leftMargin = q->elementSize(prefix % QLatin1String("hint-left-margin")).width();
        } else {
            frame->leftMargin = frame->leftWidth;
        }
    } else {
        frame->leftMargin = frame->leftWidth = 0;
    }

    frame->fixedRightWidth = q->elementSize(prefix % QLatin1String("right")).width();

    if (q->hasElement(prefix % QLatin1String("hint-right-margin"))) {
        frame->fixedRightMargin = q->elementSize(prefix % QLatin1String("hint-right-margin")).width();
    } else {
        frame->fixedRightMargin = frame->fixedRightWidth;
    }

    if (frame->enabledBorders & FrameSvg::RightBorder) {
        frame->rightWidth = q->elementSize(prefix % QLatin1String("right")).width();

        if (q->hasElement(prefix % QLatin1String("hint-right-margin"))) {
            frame->rightMargin = q->elementSize(prefix % QLatin1String("hint-right-margin")).width();
        } else {
            frame->rightMargin = frame->rightWidth;
        }
    } else {
        frame->rightMargin = frame->rightWidth = 0;
    }

    frame->fixedBottomHeight = q->elementSize(prefix % QLatin1String("bottom")).height();

    if (q->hasElement(prefix % QLatin1String("hint-bottom-margin"))) {
        frame->fixedBottomMargin = q->elementSize(prefix % QLatin1String("hint-bottom-margin")).height();
    } else {
        frame->fixedBottomMargin = frame->fixedBottomHeight;
    }

    if (frame->enabledBorders & FrameSvg::BottomBorder) {
        frame->bottomHeight = q->elementSize(prefix % QLatin1String("bottom")).height();

        if (q->hasElement(prefix % QLatin1String("hint-bottom-margin"))) {
            frame->bottomMargin = q->elementSize(prefix % QLatin1String("hint-bottom-margin")).height();
        } else {
            frame->bottomMargin = frame->bottomHeight;
        }
    } else {
        frame->bottomMargin = frame->bottomHeight = 0;
    }

    frame->composeOverBorder = (q->hasElement(prefix % QLatin1String("hint-compose-over-border")) &&
                                q->hasElement(QLatin1String("mask-") % prefix % QLatin1String("center")));

    //since it's rectangular, topWidth and bottomWidth must be the same
    //the ones that don't have a prefix is for retrocompatibility
    frame->tileCenter = (q->hasElement(QStringLiteral("hint-tile-center")) || q->hasElement(prefix % QLatin1String("hint-tile-center")));
    frame->noBorderPadding = (q->hasElement(QStringLiteral("hint-no-border-padding")) || q->hasElement(prefix % QLatin1String("hint-no-border-padding")));
    frame->stretchBorders = (q->hasElement(QStringLiteral("hint-stretch-borders")) || q->hasElement(prefix % QLatin1String("hint-stretch-borders")));
    q->resize(s);
}

void FrameSvgPrivate::updateNeeded()
{
    q->setElementPrefix(requestedPrefix);
    q->clearCache();
    updateSizes();
}

void FrameSvgPrivate::updateAndSignalSizes()
{
    updateSizes();
    emit q->repaintNeeded();
}

QSizeF FrameSvgPrivate::frameSize(FrameData *frame) const
{
    if (!frame->frameSize.isValid()) {
        updateSizes();
        frame->frameSize = q->size();
    }

    return frame->frameSize;
}

void FrameData::ref(FrameSvg *svg)
{
    references[svg] = references[svg] + 1;
    //qCDebug(LOG_PLASMA) << this << svg << references[svg];
}

bool FrameData::deref(FrameSvg *svg)
{
    references[svg] = references[svg] - 1;
    //qCDebug(LOG_PLASMA) << this << svg << references[svg];
    if (references[svg] < 1) {
        references.remove(svg);
    }

    return references.isEmpty();
}

bool FrameData::removeRefs(FrameSvg *svg)
{
    references.remove(svg);
    return references.isEmpty();
}

bool FrameData::isUsed() const
{
    return !references.isEmpty();
}

int FrameData::refcount() const
{
    return references.count();
}

QString FrameSvg::actualPrefix() const
{
    return d->prefix;
}

} // Plasma namespace

#include "moc_framesvg.cpp"
