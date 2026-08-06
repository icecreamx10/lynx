// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#import <XCTest/XCTest.h>

#import "LynxEditContextGeometryCollector.h"

using lynx::editing::EditingBlockBoundaryEdge;
using lynx::editing::EditingGeometrySnapshot;
using lynx::editing::EditingLayoutUnitFlag;
using lynx::editing::EditingProjectionSnapshot;
using lynx::editing::EditingSegment;
using lynx::editing::EditingSegmentKind;

namespace {

bool HasFlag(EditingLayoutUnitFlag flags, EditingLayoutUnitFlag expected) {
  return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(expected)) != 0;
}

EditingSegment Segment(int64_t segmentID, int64_t ownerID, EditingSegmentKind kind,
                       size_t start, size_t end) {
  EditingSegment segment;
  segment.segment_id = segmentID;
  segment.owner_id = ownerID;
  segment.kind = kind;
  segment.start = start;
  segment.end = end;
  return segment;
}

}  // namespace

@interface LynxFakeEditContextGeometryResolver : NSObject <LynxEditContextGeometryResolver>
@property(nonatomic, assign) int64_t unavailableOwner;
@property(nonatomic, strong) NSMutableArray<NSArray<NSNumber *> *> *textRequests;
@end

@implementation LynxFakeEditContextGeometryResolver

- (instancetype)init {
  self = [super init];
  if (self) {
    _textRequests = [NSMutableArray array];
  }
  return self;
}

- (BOOL)editContextMeasureTextSegment:(int64_t)segmentID
                              ownerID:(int64_t)ownerID
                          localOffset:(NSUInteger)localOffset
                               bounds:(CGRect *)bounds
                          rightToLeft:(BOOL *)rightToLeft {
  [self.textRequests addObject:@[ @(segmentID), @(ownerID), @(localOffset) ]];
  if (ownerID == self.unavailableOwner) {
    return NO;
  }
  CGFloat baseX = ownerID == 10 ? 0 : 30;
  *bounds = CGRectMake(baseX + localOffset * 10, 4, 8, 12);
  *rightToLeft = ownerID == 20;
  return YES;
}

- (BOOL)editContextMeasureNode:(int64_t)nodeID bounds:(CGRect *)bounds {
  if (nodeID == 30) {
    *bounds = CGRectMake(20, 2, 9, 16);
    return YES;
  }
  if (nodeID == 40) {
    *bounds = CGRectMake(50, 0, 10, 20);
    return YES;
  }
  return NO;
}

@end

@interface LynxEditContextGeometryCollectorUnitTest : XCTestCase
@end

@implementation LynxEditContextGeometryCollectorUnitTest

- (EditingProjectionSnapshot)projection {
  EditingProjectionSnapshot projection;
  projection.revision = 7;
  projection.length = 6;
  projection.segments.push_back(Segment(1, 10, EditingSegmentKind::kText, 0, 2));
  projection.segments.push_back(Segment(30, -1, EditingSegmentKind::kAtomicObject, 2, 3));
  projection.segments.push_back(Segment(2, 20, EditingSegmentKind::kText, 3, 5));
  EditingSegment boundary =
      Segment(40, -1, EditingSegmentKind::kBlockBoundary, 5, 6);
  boundary.block = true;
  boundary.boundary_edge = EditingBlockBoundaryEdge::kTrailing;
  projection.segments.push_back(boundary);
  return projection;
}

- (void)testCollectsTextFromEachOwnerUsingSegmentLocalOffsets {
  LynxFakeEditContextGeometryResolver *resolver =
      [[LynxFakeEditContextGeometryResolver alloc] init];
  EditingGeometrySnapshot snapshot;

  XCTAssertTrue(LynxCollectEditContextGeometry(
      resolver, CGRectMake(1, 2, 100, 40), NSMakeRange(0, 6), 11, [self projection],
      &snapshot));
  XCTAssertEqual(snapshot.state_revision, 11u);
  XCTAssertEqual(snapshot.projection_revision, 7u);
  XCTAssertEqual(snapshot.coverage.start(), 0u);
  XCTAssertEqual(snapshot.coverage.end(), 6u);
  XCTAssertEqual(snapshot.units.size(), 6u);
  XCTAssertEqual(snapshot.units[0].local_start, 0u);
  XCTAssertEqual(snapshot.units[1].local_start, 1u);
  XCTAssertEqual(snapshot.units[3].local_start, 0u);
  XCTAssertEqual(snapshot.units[4].local_start, 1u);
  XCTAssertEqualWithAccuracy(snapshot.units[3].bounds.x, 30, 0.01);
  XCTAssertTrue(HasFlag(snapshot.units[3].flags,
                        EditingLayoutUnitFlag::kRightToLeft));
}

- (void)testTreatsNonEditableDescendantAsAtomicMountedView {
  LynxFakeEditContextGeometryResolver *resolver =
      [[LynxFakeEditContextGeometryResolver alloc] init];
  EditingGeometrySnapshot snapshot;

  XCTAssertTrue(LynxCollectEditContextGeometry(
      resolver, CGRectZero, NSMakeRange(2, 1), 1, [self projection], &snapshot));
  const auto &atom = snapshot.units[2];
  XCTAssertEqual(atom.segment_id, 30);
  XCTAssertEqualWithAccuracy(atom.bounds.width, 9, 0.01);
  XCTAssertTrue(HasFlag(atom.flags, EditingLayoutUnitFlag::kAtomic));
  const auto &boundary = snapshot.units[5];
  XCTAssertEqualWithAccuracy(boundary.bounds.x, 60, 0.01);
  XCTAssertEqualWithAccuracy(boundary.bounds.width, 0, 0.01);
  XCTAssertTrue(HasFlag(boundary.flags, EditingLayoutUnitFlag::kBlock));
}

- (void)testReturnsContiguousPartialCoverageWhenDescendantIsUnmounted {
  LynxFakeEditContextGeometryResolver *resolver =
      [[LynxFakeEditContextGeometryResolver alloc] init];
  resolver.unavailableOwner = 20;
  EditingGeometrySnapshot snapshot;

  XCTAssertTrue(LynxCollectEditContextGeometry(
      resolver, CGRectZero, NSMakeRange(0, 6), 1, [self projection], &snapshot));
  XCTAssertEqual(snapshot.coverage.start(), 0u);
  XCTAssertEqual(snapshot.coverage.end(), 3u);
  XCTAssertEqual(snapshot.units.size(), 3u);
  XCTAssertTrue(snapshot.IsStructurallyValid());
}

- (void)testSameOwnerSegmentsRestartTheirLocalOffsetsAndKeepSegmentIdentity {
  EditingProjectionSnapshot projection;
  projection.revision = 3;
  projection.length = 4;
  projection.segments.push_back(Segment(101, 10, EditingSegmentKind::kText, 0, 2));
  projection.segments.push_back(Segment(202, 10, EditingSegmentKind::kText, 2, 4));
  LynxFakeEditContextGeometryResolver *resolver =
      [[LynxFakeEditContextGeometryResolver alloc] init];
  EditingGeometrySnapshot snapshot;

  XCTAssertTrue(LynxCollectEditContextGeometry(
      resolver, CGRectZero, NSMakeRange(0, 4), 1, projection, &snapshot));
  XCTAssertEqualObjects(resolver.textRequests,
                        (@[ @[ @101, @10, @0 ], @[ @101, @10, @1 ],
                              @[ @202, @10, @0 ], @[ @202, @10, @1 ] ]));
  XCTAssertEqual(snapshot.units[0].segment_id, 101);
  XCTAssertEqual(snapshot.units[2].segment_id, 202);
  XCTAssertEqual(snapshot.units[0].local_start, 0u);
  XCTAssertEqual(snapshot.units[2].local_start, 0u);
}

@end
