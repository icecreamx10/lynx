// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#import "LynxTextView+EditContext.h"
#import <objc/runtime.h>

#import "LynxEditContextInputClient.h"
#import "LynxEditContextGeometryCollector.h"
#import <Lynx/LynxBaseTextShadowNode.h>
#import <Lynx/LynxUI.h>
#import <Lynx/LynxUIContext.h>
#import <Lynx/LynxUIOwner.h>
#import <Lynx/LynxUIText.h>

#include <utility>

@protocol LynxEditContextUIContextAccess <NSObject>
@property(nonatomic, readonly, nullable) UIView *rootView;
@end

@protocol LynxEditContextUIAccess <NSObject>
@property(nonatomic, readonly, nullable) id<LynxEditContextUIContextAccess> context;
@end

@interface LynxTextView (EditContextPrivate) <LynxEditContextGeometrySource,
                                                  LynxEditContextGeometryResolver>
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
                    projection:(const lynx::editing::EditingProjectionSnapshot &)projection
                      snapshot:(lynx::editing::EditingGeometrySnapshot *)snapshot {
  CGRect controlRect = [self convertRect:self.bounds toView:client.coordinateSpaceView];
  return LynxCollectEditContextGeometry(self, controlRect, requestedRange, stateRevision,
                                        projection, snapshot);
}

- (LynxUI *)editContextUIForID:(int64_t)nodeID {
  if (nodeID < NSIntegerMin || nodeID > NSIntegerMax || !self.ui) {
    return nil;
  }
  if (self.ui.sign == (NSInteger)nodeID) {
    return self.ui;
  }
  return [self.ui.context.uiOwner findUIBySign:(NSInteger)nodeID];
}

- (BOOL)editContextMeasureTextSegment:(int64_t)segmentID
                              ownerID:(int64_t)ownerID
                          localOffset:(NSUInteger)localOffset
                               bounds:(CGRect *)bounds
                          rightToLeft:(BOOL *)rightToLeft {
  LynxUI *owner = [self editContextUIForID:ownerID];
  LynxTextView *textView = [owner isKindOfClass:LynxUIText.class] &&
                                   [owner.view isKindOfClass:LynxTextView.class]
                               ? (LynxTextView *)owner.view
                               : nil;
  if (!textView && self.ui.sign == ownerID) {
    textView = self;
  }
  LynxTextRenderer *renderer = textView.textRenderer;
  if (!renderer) {
    return NO;
  }

  NSUInteger characterOffset = localOffset;
  if (segmentID != ownerID) {
    // Inline text descendants are virtual on Darwin. ownerID selects the
    // renderer that carries the glyphs, while segmentID selects the attributed
    // run. Local offsets restart at zero for every segment.
    __block NSUInteger ownerLocalOffset = 0;
    __block NSUInteger matchedOffset = NSNotFound;
    [renderer.textStorage
        enumerateAttribute:LynxInlineTextShadowNodeSignKey
                   inRange:NSMakeRange(0, renderer.textStorage.length)
                   options:0
                usingBlock:^(id value, NSRange range, BOOL *stop) {
                  NSInteger attributedSegment = value ? [value sign] : ownerID;
                  if (attributedSegment != segmentID || matchedOffset != NSNotFound) {
                    return;
                  }
                  if (localOffset < ownerLocalOffset + range.length) {
                    matchedOffset = range.location + localOffset - ownerLocalOffset;
                    *stop = YES;
                    return;
                  }
                  ownerLocalOffset += range.length;
                }];
    if (matchedOffset != NSNotFound) {
      characterOffset = matchedOffset;
    }
  }
  if (!renderer || characterOffset >= renderer.textStorage.length) {
    return NO;
  }
  [renderer ensureTextRenderLayout];
  NSLayoutManager *layoutManager = renderer.layoutManager;
  NSTextContainer *textContainer = layoutManager.textContainers.firstObject;
  if (!textContainer) {
    return NO;
  }
  NSRange glyphRange = [layoutManager glyphRangeForCharacterRange:NSMakeRange(characterOffset, 1)
                                            actualCharacterRange:nil];
  if (glyphRange.length == 0) {
    return NO;
  }
  CGRect localBounds = [layoutManager boundingRectForGlyphRange:glyphRange
                                                inTextContainer:textContainer];
  localBounds = CGRectOffset(localBounds,
                             textView.padding.left + textView.border.left +
                                 renderer.textContentOffsetX,
                             textView.padding.top + textView.border.top);
  *bounds = [textView convertRect:localBounds
                          toView:self.lynx_editContextClient.coordinateSpaceView];
  NSParagraphStyle *paragraph =
      [renderer.textStorage attribute:NSParagraphStyleAttributeName
                              atIndex:characterOffset
                       effectiveRange:nil];
  *rightToLeft = paragraph.baseWritingDirection == NSWritingDirectionRightToLeft;
  return YES;
}

- (BOOL)editContextMeasureNode:(int64_t)nodeID bounds:(CGRect *)bounds {
  LynxUI *node = [self editContextUIForID:nodeID];
  if (node && node.view && (node.view.superview || node.view == self)) {
    *bounds = [node.view convertRect:node.view.bounds
                              toView:self.lynx_editContextClient.coordinateSpaceView];
    return YES;
  }
  for (LynxTextAttachmentInfo *attachment in self.textRenderer.attachments) {
    if (attachment.sign != nodeID || CGRectIsEmpty(attachment.frame)) {
      continue;
    }
    CGRect localBounds =
        CGRectOffset(attachment.frame, self.padding.left + self.border.left,
                     self.padding.top + self.border.top);
    *bounds = [self convertRect:localBounds
                        toView:self.lynx_editContextClient.coordinateSpaceView];
    return YES;
  }
  return NO;
}

@end
