// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#import "LynxTextView+EditContext.h"
#import <objc/runtime.h>

#import "LynxEditContextInputClient.h"

#include <algorithm>
#include <utility>

// Keep this adapter independent from renderer/event internals. These are the
// existing public renderer accessors needed to measure mounted glyphs.
@interface LynxTextRenderer : NSObject
@property(nonatomic, readonly) NSLayoutManager *layoutManager;
@property(nonatomic, readonly) NSTextStorage *textStorage;
@property(nonatomic, readonly) CGFloat textContentOffsetX;
- (void)ensureTextRenderLayout;
@end

@protocol LynxEditContextUIContextAccess <NSObject>
@property(nonatomic, readonly, nullable) UIView *rootView;
@end

@protocol LynxEditContextUIAccess <NSObject>
@property(nonatomic, readonly, nullable) id<LynxEditContextUIContextAccess> context;
@end

namespace {

using lynx::editing::EditingBlockBoundaryEdge;
using lynx::editing::EditingLayoutUnitFlag;
using lynx::editing::EditingProjectionSnapshot;
using lynx::editing::EditingSegment;
using lynx::editing::EditingSegmentKind;

EditingLayoutUnitFlag LynxAddFlag(EditingLayoutUnitFlag flags,
                                  EditingLayoutUnitFlag added) {
  return static_cast<EditingLayoutUnitFlag>(static_cast<uint8_t>(flags) |
                                            static_cast<uint8_t>(added));
}

const EditingSegment *LynxSegmentAtOffset(const EditingProjectionSnapshot &projection,
                                          size_t offset) {
  auto found = std::find_if(projection.segments.begin(), projection.segments.end(),
                            [offset](const EditingSegment &segment) {
                              return segment.start <= offset && offset < segment.end;
                            });
  return found == projection.segments.end() ? nullptr : &*found;
}

}  // namespace

@interface LynxTextView (EditContextPrivate) <LynxEditContextGeometrySource>
@property(nonatomic, strong, nullable) LynxEditContextInputClient *lynx_editContextClient;
@end

@implementation LynxTextView (EditContext)

static void *kLynxEditContextClientKey = &kLynxEditContextClientKey;

- (LynxEditContextInputClient *)lynx_editContextClient {
  return objc_getAssociatedObject(self, kLynxEditContextClientKey);
}

- (void)setLynx_editContextClient:(LynxEditContextInputClient *)client {
  objc_setAssociatedObject(self, kLynxEditContextClientKey, client,
                           OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

- (void)attachEditContextSession:
    (std::shared_ptr<lynx::editing::EditingPlatformSession>)session {
  [self detachEditContext];
  if (!session) {
    return;
  }
  LynxEditContextInputClient *client =
      [[LynxEditContextInputClient alloc] initWithSession:std::move(session)];
  client.geometrySource = self;
  id<LynxEditContextUIAccess> ui = (id<LynxEditContextUIAccess>)self.ui;
  client.coordinateSpaceView = ui.context.rootView;
  client.frame = self.bounds;
  client.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
  [self addSubview:client];
  self.lynx_editContextClient = client;
  [client refreshGeometry];
#if !__has_feature(objc_arc)
  [client release];
#endif
}

- (BOOL)activateEditContext {
  return [self.lynx_editContextClient activate];
}

- (void)deactivateEditContext {
  [self.lynx_editContextClient deactivate];
}

- (void)detachEditContext {
  LynxEditContextInputClient *client = self.lynx_editContextClient;
  self.lynx_editContextClient = nil;
  client.geometrySource = nil;
  [client invalidate];
  [client removeFromSuperview];
}

- (void)refreshEditContextGeometry {
  [self.lynx_editContextClient refreshGeometry];
}

- (BOOL)editContextInputClient:(LynxEditContextInputClient *)client
       collectGeometryForRange:(NSRange)requestedRange
                 stateRevision:(uint64_t)stateRevision
                    projection:(const EditingProjectionSnapshot &)projection
                      snapshot:(lynx::editing::EditingGeometrySnapshot *)snapshot {
  LynxTextRenderer *renderer = self.textRenderer;
  if (!snapshot || !renderer || projection.length != renderer.textStorage.length ||
      requestedRange.location == NSNotFound || requestedRange.location > projection.length ||
      requestedRange.length > projection.length - requestedRange.location) {
    return NO;
  }

  [renderer ensureTextRenderLayout];
  NSLayoutManager *layoutManager = renderer.layoutManager;
  NSTextContainer *textContainer = layoutManager.textContainers.firstObject;
  if (!textContainer) {
    return NO;
  }

  CGRect controlRect = [self convertRect:self.bounds toView:client.coordinateSpaceView];
  snapshot->state_revision = stateRevision;
  snapshot->projection_revision = projection.revision;
  snapshot->projection_length = projection.length;
  snapshot->coverage = lynx::editing::TextRange(requestedRange.location,
                                                NSMaxRange(requestedRange));
  snapshot->control_bounds = {static_cast<float>(controlRect.origin.x),
                              static_cast<float>(controlRect.origin.y),
                              static_cast<float>(controlRect.size.width),
                              static_cast<float>(controlRect.size.height)};
  snapshot->units.clear();
  snapshot->units.reserve(requestedRange.length);

  const CGPoint textOrigin =
      CGPointMake(self.padding.left + self.border.left + renderer.textContentOffsetX,
                  self.padding.top + self.border.top);
  for (NSUInteger offset = requestedRange.location; offset < NSMaxRange(requestedRange);
       ++offset) {
    const EditingSegment *segment = LynxSegmentAtOffset(projection, offset);
    if (!segment) {
      return NO;
    }
    NSRange glyphRange = [layoutManager glyphRangeForCharacterRange:NSMakeRange(offset, 1)
                                              actualCharacterRange:nil];
    if (glyphRange.length == 0) {
      return NO;
    }
    CGRect localBounds = [layoutManager boundingRectForGlyphRange:glyphRange
                                                  inTextContainer:textContainer];
    localBounds = CGRectOffset(localBounds, textOrigin.x, textOrigin.y);
    CGRect viewportBounds = [self convertRect:localBounds toView:client.coordinateSpaceView];

    EditingLayoutUnitFlag flags = EditingLayoutUnitFlag::kNone;
    if (segment->kind == EditingSegmentKind::kAtomicObject) {
      flags = LynxAddFlag(flags, EditingLayoutUnitFlag::kAtomic);
    } else if (segment->kind == EditingSegmentKind::kBlockBoundary) {
      flags = LynxAddFlag(flags, EditingLayoutUnitFlag::kBlock);
    }
    NSParagraphStyle *paragraph =
        [renderer.textStorage attribute:NSParagraphStyleAttributeName
                                atIndex:offset
                         effectiveRange:nil];
    if (paragraph.baseWritingDirection == NSWritingDirectionRightToLeft) {
      flags = LynxAddFlag(flags, EditingLayoutUnitFlag::kHasDirection);
      flags = LynxAddFlag(flags, EditingLayoutUnitFlag::kRightToLeft);
    }

    lynx::editing::EditingLayoutUnit unit;
    unit.projection_offset = offset;
    unit.segment_id = segment->segment_id;
    unit.owner_id = segment->owner_id;
    unit.local_start = offset - segment->start;
    unit.local_end = unit.local_start + 1;
    unit.bounds = {static_cast<float>(viewportBounds.origin.x),
                   static_cast<float>(viewportBounds.origin.y),
                   static_cast<float>(viewportBounds.size.width),
                   static_cast<float>(viewportBounds.size.height)};
    unit.flags = flags;
    unit.boundary_edge = segment->boundary_edge;
    snapshot->units.push_back(unit);
  }
  return YES;
}

@end
