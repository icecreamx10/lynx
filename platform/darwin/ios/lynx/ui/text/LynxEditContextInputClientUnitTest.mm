// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#import <XCTest/XCTest.h>

#import "LynxEditContextInputClient.h"

namespace {

class FakeEditingPlatformSession final : public lynx::editing::EditingPlatformSession {
 public:
  void Attach() override { ++attach_count; }
  void Detach() override { ++detach_count; }
  void SetDelegate(lynx::editing::EditingPlatformDelegate* value) override { delegate = value; }
  bool Activate() override {
    active = true;
    return true;
  }
  void Deactivate() override {
    active = false;
    ++deactivate_count;
  }
  bool IsActive() const override { return active; }
  lynx::editing::EditingStateSnapshot Snapshot() const override { return state; }
  lynx::editing::EditingProjectionSnapshot ProjectionSnapshot() const override {
    return projection;
  }
  lynx::editing::EditingPlatformResult ApplyTransaction(
      const lynx::editing::NativeTextTransaction& transaction) override {
    last_transaction = transaction;
    ++transaction_count;
    lynx::editing::EditingPlatformResult result;
    result.status = lynx::editing::EditingOperationStatus::kAccepted;
    result.snapshot = state;
    return result;
  }
  lynx::editing::EditingPlatformResult PerformInput(const std::string& input_type,
                                                    const std::u16string& data,
                                                    uint64_t expected_revision) override {
    last_input_type = input_type;
    last_input_data = data;
    last_expected_revision = expected_revision;
    lynx::editing::EditingPlatformResult result;
    result.status = lynx::editing::EditingOperationStatus::kAccepted;
    result.snapshot = state;
    return result;
  }
  lynx::editing::EditingPlatformResult SetSelection(lynx::editing::TextRange selection,
                                                    uint64_t expected_revision) override {
    last_selection = selection;
    last_expected_revision = expected_revision;
    lynx::editing::EditingPlatformResult result;
    result.status = lynx::editing::EditingOperationStatus::kAccepted;
    result.snapshot = state;
    return result;
  }
  lynx::editing::EditingOperationStatus UpdateGeometry(
      lynx::editing::EditingGeometrySnapshot geometry) override {
    last_geometry = std::move(geometry);
    return lynx::editing::EditingOperationStatus::kAccepted;
  }
  lynx::editing::EditingPlatformResult SetSelectionFromPoint(
      lynx::editing::EditingLayoutPoint point, std::optional<size_t> anchor,
      uint64_t expected_revision) override {
    lynx::editing::EditingPlatformResult result;
    result.status = lynx::editing::EditingOperationStatus::kAccepted;
    result.snapshot = state;
    return result;
  }
  lynx::editing::EditingSelectionRectsResult QuerySelectionRects(
      lynx::editing::TextRange selection, uint64_t expected_revision) override {
    return {};
  }

  bool active{false};
  int attach_count{0};
  int detach_count{0};
  int deactivate_count{0};
  int transaction_count{0};
  lynx::editing::EditingPlatformDelegate* delegate{nullptr};
  lynx::editing::EditingStateSnapshot state;
  lynx::editing::EditingProjectionSnapshot projection;
  lynx::editing::NativeTextTransaction last_transaction;
  lynx::editing::TextRange last_selection;
  lynx::editing::EditingGeometrySnapshot last_geometry;
  std::string last_input_type;
  std::u16string last_input_data;
  uint64_t last_expected_revision{0};
};

}  // namespace

@interface LynxEditContextInputClientUnitTest : XCTestCase
@end
@implementation LynxEditContextInputClientUnitTest

- (void)testLifecycleClearsDelegateBeforeDetaching {
  auto session = std::make_shared<FakeEditingPlatformSession>();
  LynxEditContextInputClient *client =
      [[LynxEditContextInputClient alloc] initWithSession:session];
  XCTAssertEqual(session->attach_count, 1);
  XCTAssertNotEqual(session->delegate, nullptr);

  [client invalidate];

  XCTAssertEqual(session->deactivate_count, 1);
  XCTAssertEqual(session->detach_count, 1);
  XCTAssertEqual(session->delegate, nullptr);
}

- (void)testUITextInputUsesUTF16OffsetsForSelectionAndComposition {
  auto session = std::make_shared<FakeEditingPlatformSession>();
  session->state.text = u"a\U0001F600b";
  session->state.selection = lynx::editing::TextRange(1, 3);
  session->state.revision = 9;
  LynxEditContextInputClient *client =
      [[LynxEditContextInputClient alloc] initWithSession:session];

  UITextRange *selected = client.selectedTextRange;
  XCTAssertEqual([client offsetFromPosition:client.beginningOfDocument
                                toPosition:selected.start],
                 1);
  XCTAssertEqual([client offsetFromPosition:selected.start toPosition:selected.end], 2);
  [client setMarkedText:@"\u4f60\u597d" selectedRange:NSMakeRange(1, 0)];

  XCTAssertEqual(session->transaction_count, 1);
  XCTAssertEqual(session->last_transaction.replacement_range.start(), 1u);
  XCTAssertEqual(session->last_transaction.replacement_range.end(), 3u);
  XCTAssertEqual(session->last_transaction.replacement_text, u"\u4f60\u597d");
  XCTAssertTrue(session->last_transaction.composition.has_value());
  XCTAssertEqual(session->last_transaction.composition->start(), 1u);
  XCTAssertEqual(session->last_transaction.composition->end(), 3u);
  XCTAssertEqual(session->last_transaction.selection.position(), 2u);
  XCTAssertEqual(session->last_transaction.expected_revision, 9u);
}

- (void)testKeyboardDeleteAndNativeSelectionForwardToSharedSession {
  auto session = std::make_shared<FakeEditingPlatformSession>();
  session->state.text = u"abc";
  session->state.selection = lynx::editing::TextRange(2);
  session->state.revision = 4;
  LynxEditContextInputClient *client =
      [[LynxEditContextInputClient alloc] initWithSession:session];

  [client deleteBackward];
  XCTAssertEqual(session->last_input_type, "deleteContentBackward");
  XCTAssertEqual(session->last_expected_revision, 4u);

  UITextPosition *start = [client positionFromPosition:client.beginningOfDocument offset:1];
  UITextPosition *end = [client positionFromPosition:start offset:2];
  client.selectedTextRange = [client textRangeFromPosition:start toPosition:end];
  XCTAssertEqual(session->last_selection.start(), 1u);
  XCTAssertEqual(session->last_selection.end(), 3u);
}

@end
