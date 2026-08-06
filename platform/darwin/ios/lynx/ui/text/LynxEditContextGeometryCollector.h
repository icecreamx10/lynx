// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#import <UIKit/UIKit.h>

#ifdef __cplusplus
#include "core/renderer/editing/editing_platform_contract.h"

NS_ASSUME_NONNULL_BEGIN

// Resolves projection owners to currently mounted UIKit geometry. Offsets are
// local to the owning segment, never offsets into the host text renderer.
@protocol LynxEditContextGeometryResolver <NSObject>
- (BOOL)editContextMeasureTextSegment:(int64_t)segmentID
                              ownerID:(int64_t)ownerID
                        localOffset:(NSUInteger)localOffset
                             bounds:(CGRect *)bounds
                        rightToLeft:(BOOL *)rightToLeft;
- (BOOL)editContextMeasureNode:(int64_t)nodeID bounds:(CGRect *)bounds;
@end

BOOL LynxCollectEditContextGeometry(
    id<LynxEditContextGeometryResolver> resolver, CGRect controlBounds,
    NSRange requestedRange, uint64_t stateRevision,
    const lynx::editing::EditingProjectionSnapshot &projection,
    lynx::editing::EditingGeometrySnapshot *snapshot);

NS_ASSUME_NONNULL_END
#endif
