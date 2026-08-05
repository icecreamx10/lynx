// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#import <UIKit/UIKit.h>

#ifdef __cplusplus
#include <memory>

#include "core/renderer/editing/editing_platform_contract.h"
#endif

NS_ASSUME_NONNULL_BEGIN

@class LynxEditContextInputClient;

#ifdef __cplusplus
/**
 * Supplies mounted UIKit geometry for a C++-owned logical projection. The
 * source only measures views; it must use segment descriptors supplied by the
 * session and must not invent units for unmounted content.
 */
@protocol LynxEditContextGeometrySource <NSObject>
- (BOOL)editContextInputClient:(LynxEditContextInputClient *)client
       collectGeometryForRange:(NSRange)range
                 stateRevision:(uint64_t)stateRevision
                    projection:(const lynx::editing::EditingProjectionSnapshot &)projection
                      snapshot:(lynx::editing::EditingGeometrySnapshot *)snapshot;
@end

/** UIKit translation layer for the shared editing session. */
@interface LynxEditContextInputClient : UIView <UITextInput, UIKeyInput>

- (instancetype)initWithSession:
    (std::shared_ptr<lynx::editing::EditingPlatformSession>)session NS_DESIGNATED_INITIALIZER;
- (instancetype)initWithFrame:(CGRect)frame NS_UNAVAILABLE;
- (instancetype)initWithCoder:(NSCoder *)coder NS_UNAVAILABLE;

#if __has_feature(objc_arc)
@property(nonatomic, weak, nullable) id<LynxEditContextGeometrySource> geometrySource;
#else
@property(nonatomic, assign, nullable) id<LynxEditContextGeometrySource> geometrySource;
#endif
/** View whose bounds define the Lynx viewport coordinate space. */
#if __has_feature(objc_arc)
@property(nonatomic, weak, nullable) UIView *coordinateSpaceView;
#else
@property(nonatomic, assign, nullable) UIView *coordinateSpaceView;
#endif

- (BOOL)activate;
- (void)deactivate;
- (void)invalidate;
- (void)refreshGeometry;

@end
#endif

NS_ASSUME_NONNULL_END
