// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#import "LynxEditContextInputClient.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace {

using lynx::editing::EditingPlatformResult;
using lynx::editing::EditingPlatformSession;
using lynx::editing::EditingStateChange;
using lynx::editing::EditingStateSnapshot;
using lynx::editing::NativeTextTransaction;
using lynx::editing::TextRange;

static NSUInteger LynxClampOffset(size_t offset) {
  return static_cast<NSUInteger>(
      std::min(offset, static_cast<size_t>(std::numeric_limits<NSUInteger>::max())));
}

static NSRange LynxNSRangeFromTextRange(const TextRange &range) {
  return NSMakeRange(LynxClampOffset(range.start()), LynxClampOffset(range.length()));
}

static NSString *LynxStringFromUTF16(const std::u16string &text) {
  NSString *result =
      [[NSString alloc] initWithCharacters:reinterpret_cast<const unichar *>(text.data())
                                    length:text.size()];
#if __has_feature(objc_arc)
  return result;
#else
  return [result autorelease];
#endif
}

static std::u16string LynxUTF16FromString(NSString *text) {
  std::u16string result(text.length, u'\0');
  if (!result.empty()) {
    [text getCharacters:reinterpret_cast<unichar *>(result.data())
                  range:NSMakeRange(0, text.length)];
  }
  return result;
}

static BOOL LynxRangeFitsText(NSRange range, size_t length) {
  return range.location != NSNotFound && range.location <= length && range.length <= length - range.location;
}

}  // namespace

@interface LynxEditContextTextPosition : UITextPosition
@property(nonatomic, assign) NSUInteger offset;
+ (instancetype)positionWithOffset:(NSUInteger)offset;
@end

@implementation LynxEditContextTextPosition
+ (instancetype)positionWithOffset:(NSUInteger)offset {
  LynxEditContextTextPosition *position = [LynxEditContextTextPosition new];
  position.offset = offset;
#if !__has_feature(objc_arc)
  [position autorelease];
#endif
  return position;
}
@end

@interface LynxEditContextTextRange : UITextRange
@property(nonatomic, strong) LynxEditContextTextPosition *rangeStart;
@property(nonatomic, strong) LynxEditContextTextPosition *rangeEnd;
+ (instancetype)rangeWithNSRange:(NSRange)range;
@end

@implementation LynxEditContextTextRange
+ (instancetype)rangeWithNSRange:(NSRange)range {
  LynxEditContextTextRange *textRange = [LynxEditContextTextRange new];
  textRange.rangeStart = [LynxEditContextTextPosition positionWithOffset:range.location];
  textRange.rangeEnd =
      [LynxEditContextTextPosition positionWithOffset:NSMaxRange(range)];
#if !__has_feature(objc_arc)
  [textRange autorelease];
#endif
  return textRange;
}
- (UITextPosition *)start {
  return _rangeStart;
}
- (UITextPosition *)end {
  return _rangeEnd;
}
- (BOOL)isEmpty {
  return _rangeStart.offset == _rangeEnd.offset;
}
@end

@interface LynxEditContextSelectionRect : UITextSelectionRect
@property(nonatomic, assign) CGRect selectionRect;
@property(nonatomic, assign) BOOL selectionContainsStart;
@property(nonatomic, assign) BOOL selectionContainsEnd;
@end

@implementation LynxEditContextSelectionRect
- (CGRect)rect {
  return _selectionRect;
}
- (NSWritingDirection)writingDirection {
  return NSWritingDirectionNatural;
}
- (BOOL)containsStart {
  return _selectionContainsStart;
}
- (BOOL)containsEnd {
  return _selectionContainsEnd;
}
- (BOOL)isVertical {
  return NO;
}
@end

@class LynxEditContextInputClient;

namespace {

class DarwinEditingDelegate final : public lynx::editing::EditingPlatformDelegate {
 public:
  explicit DarwinEditingDelegate(LynxEditContextInputClient *client) : client_(client) {}

  void OnStateChanged(const lynx::editing::EditingStateUpdate &update) override;
  void OnActivationChanged(bool active) override;
  void OnGeometryRequested(const TextRange &range, uint64_t state_revision,
                           uint64_t projection_revision) override;

 private:
#if __has_feature(objc_arc)
  __weak LynxEditContextInputClient *client_;
#else
  __unsafe_unretained LynxEditContextInputClient *client_;
#endif
};

}  // namespace

@interface LynxEditContextInputClient () <UITextInteractionDelegate>
- (void)editingStateDidChange:(const lynx::editing::EditingStateUpdate &)update;
- (void)editingActivationDidChange:(BOOL)active;
- (void)editingGeometryRequested:(const TextRange &)range
                   stateRevision:(uint64_t)stateRevision
              projectionRevision:(uint64_t)projectionRevision;
@end

@implementation LynxEditContextInputClient {
  std::shared_ptr<EditingPlatformSession> _session;
  std::unique_ptr<DarwinEditingDelegate> _editingDelegate;
#if __has_feature(objc_arc)
  __weak id<UITextInputDelegate> _inputDelegate;
#else
  __unsafe_unretained id<UITextInputDelegate> _inputDelegate;
#endif
  UITextInputStringTokenizer *_tokenizer;
  NSDictionary<NSAttributedStringKey, id> *_markedTextStyle;
  UITextInteraction *_textInteraction API_AVAILABLE(ios(13.0));
}

@synthesize inputDelegate = _inputDelegate;
@synthesize markedTextStyle = _markedTextStyle;
@synthesize autocapitalizationType = _autocapitalizationType;
@synthesize autocorrectionType = _autocorrectionType;
@synthesize spellCheckingType = _spellCheckingType;
@synthesize smartQuotesType = _smartQuotesType;
@synthesize smartDashesType = _smartDashesType;
@synthesize smartInsertDeleteType = _smartInsertDeleteType;
@synthesize keyboardType = _keyboardType;
@synthesize keyboardAppearance = _keyboardAppearance;
@synthesize returnKeyType = _returnKeyType;
@synthesize enablesReturnKeyAutomatically = _enablesReturnKeyAutomatically;
@synthesize secureTextEntry = _secureTextEntry;
@synthesize textContentType = _textContentType;
@synthesize passwordRules = _passwordRules;

- (instancetype)initWithSession:(std::shared_ptr<EditingPlatformSession>)session {
  self = [super initWithFrame:CGRectZero];
  if (self) {
    NSParameterAssert(session);
    _session = std::move(session);
    _session->Attach();
    _editingDelegate = std::make_unique<DarwinEditingDelegate>(self);
    _session->SetDelegate(_editingDelegate.get());
    _tokenizer = [[UITextInputStringTokenizer alloc] initWithTextInput:self];
    self.backgroundColor = UIColor.clearColor;
    self.opaque = NO;
    self.userInteractionEnabled = YES;
    self.autocapitalizationType = UITextAutocapitalizationTypeSentences;
    self.autocorrectionType = UITextAutocorrectionTypeDefault;
    self.spellCheckingType = UITextSpellCheckingTypeDefault;
    self.keyboardType = UIKeyboardTypeDefault;
    self.returnKeyType = UIReturnKeyDefault;
    if (@available(iOS 13.0, *)) {
      _textInteraction = [UITextInteraction textInteractionForMode:UITextInteractionModeEditable];
      _textInteraction.delegate = self;
      _textInteraction.textInput = self;
      [self addInteraction:_textInteraction];
    }
  }
  return self;
}

- (void)dealloc {
  [self invalidate];
#if !__has_feature(objc_arc)
  [_tokenizer release];
  [_markedTextStyle release];
  [super dealloc];
#endif
}

- (BOOL)canBecomeFirstResponder {
  return _session && _session->IsActive();
}

- (BOOL)activate {
  NSAssert(NSThread.isMainThread, @"EditContext must run on the Lynx UI thread");
  if (!_session || !_session->Activate()) {
    return NO;
  }
  [self refreshGeometry];
  if ([self becomeFirstResponder]) {
    return YES;
  }
  _session->Deactivate();
  return NO;
}

- (void)deactivate {
  NSAssert(NSThread.isMainThread, @"EditContext must run on the Lynx UI thread");
  if (!_session) {
    return;
  }
  if (self.isFirstResponder) {
    [self resignFirstResponder];
  }
  _session->Deactivate();
}

- (void)invalidate {
  if (!_session) {
    return;
  }
  [self deactivate];
  _session->SetDelegate(nullptr);
  _session->Detach();
  _editingDelegate.reset();
  _session.reset();
}

- (void)didMoveToWindow {
  [super didMoveToWindow];
  if (!self.window) {
    [self deactivate];
  }
}

- (BOOL)interactionShouldBegin:(UITextInteraction *)interaction atPoint:(CGPoint)point
    API_AVAILABLE(ios(13.0)) {
  return [self activate];
}

- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
  UITouch *touch = touches.anyObject;
  if (_session && touch) {
    [self refreshGeometry];
    EditingStateSnapshot snapshot = _session->Snapshot();
    CGPoint point = [touch locationInView:self.coordinateSpaceView];
    _session->SetSelectionFromPoint({static_cast<float>(point.x), static_cast<float>(point.y)},
                                    std::nullopt, snapshot.revision);
    [self activate];
  }
  [super touchesEnded:touches withEvent:event];
}

- (void)refreshGeometry {
  if (!_session || !self.geometrySource) {
    return;
  }
  EditingStateSnapshot state = _session->Snapshot();
  lynx::editing::EditingProjectionSnapshot projection = _session->ProjectionSnapshot();
  lynx::editing::EditingGeometrySnapshot geometry;
  if ([self.geometrySource editContextInputClient:self
                         collectGeometryForRange:NSMakeRange(0, projection.length)
                                   stateRevision:state.revision
                                      projection:projection
                                        snapshot:&geometry]) {
    _session->UpdateGeometry(std::move(geometry));
  }
}

#pragma mark - UIKeyInput

- (BOOL)hasText {
  return _session && !_session->Snapshot().text.empty();
}

- (void)insertText:(NSString *)text {
  if (!_session) {
    return;
  }
  EditingStateSnapshot snapshot = _session->Snapshot();
  NativeTextTransaction transaction;
  transaction.input_type = "insertText";
  transaction.updates_text = true;
  transaction.replacement_range = snapshot.has_composition ? snapshot.composition : snapshot.selection;
  transaction.replacement_text = LynxUTF16FromString(text);
  const size_t caret = transaction.replacement_range.start() + transaction.replacement_text.size();
  transaction.selection = TextRange(caret);
  transaction.composition = std::nullopt;
  transaction.expected_revision = snapshot.revision;
  [self applyTransaction:transaction];
}

- (void)deleteBackward {
  if (!_session) {
    return;
  }
  EditingStateSnapshot snapshot = _session->Snapshot();
  [self applyResult:_session->PerformInput("deleteContentBackward", u"", snapshot.revision)];
}

#pragma mark - UITextInput state

- (NSString *)textInRange:(UITextRange *)range {
  if (!_session || ![range isKindOfClass:LynxEditContextTextRange.class]) {
    return @"";
  }
  EditingStateSnapshot snapshot = _session->Snapshot();
  NSRange nsRange = [self nsRangeFromTextRange:range];
  if (!LynxRangeFitsText(nsRange, snapshot.text.size())) {
    return @"";
  }
  return LynxStringFromUTF16(snapshot.text.substr(nsRange.location, nsRange.length));
}

- (void)replaceRange:(UITextRange *)range withText:(NSString *)text {
  if (!_session || ![range isKindOfClass:LynxEditContextTextRange.class]) {
    return;
  }
  EditingStateSnapshot snapshot = _session->Snapshot();
  NSRange replacement = [self nsRangeFromTextRange:range];
  if (!LynxRangeFitsText(replacement, snapshot.text.size())) {
    return;
  }
  NativeTextTransaction transaction;
  transaction.input_type = "insertReplacementText";
  transaction.updates_text = true;
  transaction.replacement_range = TextRange(replacement.location, NSMaxRange(replacement));
  transaction.replacement_text = LynxUTF16FromString(text);
  const size_t caret = replacement.location + transaction.replacement_text.size();
  transaction.selection = TextRange(caret);
  transaction.composition = std::nullopt;
  transaction.expected_revision = snapshot.revision;
  [self applyTransaction:transaction];
}

- (UITextRange *)selectedTextRange {
  if (!_session) {
    return [LynxEditContextTextRange rangeWithNSRange:NSMakeRange(0, 0)];
  }
  return [LynxEditContextTextRange rangeWithNSRange:LynxNSRangeFromTextRange(_session->Snapshot().selection)];
}

- (void)setSelectedTextRange:(UITextRange *)selectedTextRange {
  if (!_session || ![selectedTextRange isKindOfClass:LynxEditContextTextRange.class]) {
    return;
  }
  EditingStateSnapshot snapshot = _session->Snapshot();
  NSRange range = [self nsRangeFromTextRange:selectedTextRange];
  if (!LynxRangeFitsText(range, snapshot.text.size())) {
    return;
  }
  [self applyResult:_session->SetSelection(TextRange(range.location, NSMaxRange(range)),
                                                  snapshot.revision)];
}

- (UITextRange *)markedTextRange {
  if (!_session) {
    return nil;
  }
  EditingStateSnapshot snapshot = _session->Snapshot();
  return snapshot.has_composition
             ? [LynxEditContextTextRange rangeWithNSRange:LynxNSRangeFromTextRange(snapshot.composition)]
             : nil;
}

- (void)setMarkedText:(NSString *)markedText selectedRange:(NSRange)selectedRange {
  if (!_session) {
    return;
  }
  if (!markedText) {
    [self unmarkText];
    return;
  }
  EditingStateSnapshot snapshot = _session->Snapshot();
  TextRange replaced = snapshot.has_composition ? snapshot.composition : snapshot.selection;
  std::u16string replacement = LynxUTF16FromString(markedText ?: @"");
  if (!LynxRangeFitsText(selectedRange, replacement.size())) {
    return;
  }
  NativeTextTransaction transaction;
  transaction.input_type = "insertCompositionText";
  transaction.updates_text = true;
  transaction.replacement_range = replaced;
  transaction.replacement_text = std::move(replacement);
  const size_t compositionStart = replaced.start();
  transaction.composition = TextRange(compositionStart, compositionStart + transaction.replacement_text.size());
  transaction.selection = TextRange(compositionStart + selectedRange.location,
                                    compositionStart + NSMaxRange(selectedRange));
  transaction.expected_revision = snapshot.revision;
  [self applyTransaction:transaction];
}

- (void)unmarkText {
  if (!_session) {
    return;
  }
  EditingStateSnapshot snapshot = _session->Snapshot();
  if (!snapshot.has_composition) {
    return;
  }
  NativeTextTransaction transaction;
  transaction.input_type = "insertFromComposition";
  transaction.updates_text = false;
  transaction.selection = snapshot.selection;
  transaction.composition = std::nullopt;
  transaction.expected_revision = snapshot.revision;
  [self applyTransaction:transaction];
}

- (UITextPosition *)beginningOfDocument {
  return [LynxEditContextTextPosition positionWithOffset:0];
}

- (UITextPosition *)endOfDocument {
  return [LynxEditContextTextPosition positionWithOffset:_session ? _session->Snapshot().text.size() : 0];
}

- (UITextRange *)textRangeFromPosition:(UITextPosition *)fromPosition
                            toPosition:(UITextPosition *)toPosition {
  LynxEditContextTextPosition *from = [self position:fromPosition];
  LynxEditContextTextPosition *to = [self position:toPosition];
  if (!from || !to) {
    return nil;
  }
  return [LynxEditContextTextRange
      rangeWithNSRange:NSMakeRange(MIN(from.offset, to.offset),
                                   MAX(from.offset, to.offset) - MIN(from.offset, to.offset))];
}

- (UITextPosition *)positionFromPosition:(UITextPosition *)position offset:(NSInteger)offset {
  // UITextInput requires this synchronous query, while the current shared
  // contract exposes UTF-16 offsets but no read-only grapheme/atom movement
  // primitive. Keep this adapter unit-based; composed-character and atom
  // navigation must move into a future C++ contract extension.
  LynxEditContextTextPosition *typed = [self position:position];
  if (!typed || !_session) {
    return nil;
  }
  const NSInteger length = static_cast<NSInteger>(std::min(
      _session->Snapshot().text.size(), static_cast<size_t>(std::numeric_limits<NSInteger>::max())));
  const NSInteger current = static_cast<NSInteger>(std::min(
      typed.offset, static_cast<NSUInteger>(std::numeric_limits<NSInteger>::max())));
  if ((offset < 0 && (offset == NSIntegerMin || current < -offset)) ||
      (offset > 0 && (offset > length || current > length - offset))) {
    return nil;
  }
  return [LynxEditContextTextPosition
      positionWithOffset:static_cast<NSUInteger>(current + offset)];
}

- (UITextPosition *)positionFromPosition:(UITextPosition *)position
                             inDirection:(UITextLayoutDirection)direction
                                  offset:(NSInteger)offset {
  const NSInteger signedOffset =
      (direction == UITextLayoutDirectionLeft || direction == UITextLayoutDirectionUp) ? -offset : offset;
  return [self positionFromPosition:position offset:signedOffset];
}

- (NSComparisonResult)comparePosition:(UITextPosition *)position toPosition:(UITextPosition *)other {
  NSUInteger lhs = [self position:position].offset;
  NSUInteger rhs = [self position:other].offset;
  return lhs < rhs ? NSOrderedAscending : (lhs > rhs ? NSOrderedDescending : NSOrderedSame);
}

- (NSInteger)offsetFromPosition:(UITextPosition *)from toPosition:(UITextPosition *)toPosition {
  return static_cast<NSInteger>([self position:toPosition].offset) -
         static_cast<NSInteger>([self position:from].offset);
}

- (id<UITextInputTokenizer>)tokenizer {
  return _tokenizer;
}

#pragma mark - UITextInput geometry

- (CGRect)firstRectForRange:(UITextRange *)range {
  NSArray<UITextSelectionRect *> *rects = [self selectionRectsForRange:range];
  return rects.firstObject ? rects.firstObject.rect : CGRectZero;
}

- (CGRect)caretRectForPosition:(UITextPosition *)position {
  LynxEditContextTextPosition *typed = [self position:position];
  if (!typed || !_session) {
    return CGRectZero;
  }
  [self refreshGeometry];
  lynx::editing::EditingSelectionRectsResult query =
      _session->QuerySelectionRects(TextRange(typed.offset), _session->Snapshot().revision);
  if (query.status != lynx::editing::EditingOperationStatus::kAccepted || query.rects.empty()) {
    return CGRectZero;
  }
  const auto &rect = query.rects.front();
  return [self convertRect:CGRectMake(rect.x, rect.y, rect.width, rect.height)
                  fromView:self.coordinateSpaceView];
}

- (NSArray<UITextSelectionRect *> *)selectionRectsForRange:(UITextRange *)range {
  if (!_session || ![range isKindOfClass:LynxEditContextTextRange.class]) {
    return @[];
  }
  [self refreshGeometry];
  NSRange nsRange = [self nsRangeFromTextRange:range];
  lynx::editing::EditingSelectionRectsResult query = _session->QuerySelectionRects(
      TextRange(nsRange.location, NSMaxRange(nsRange)), _session->Snapshot().revision);
  if (query.status != lynx::editing::EditingOperationStatus::kAccepted) {
    return @[];
  }
  NSMutableArray<UITextSelectionRect *> *selectionRects =
      [NSMutableArray arrayWithCapacity:query.rects.size()];
  for (size_t index = 0; index < query.rects.size(); ++index) {
    const auto &rect = query.rects[index];
    LynxEditContextSelectionRect *selectionRect = [LynxEditContextSelectionRect new];
    selectionRect.selectionRect =
        [self convertRect:CGRectMake(rect.x, rect.y, rect.width, rect.height)
                fromView:self.coordinateSpaceView];
    selectionRect.selectionContainsStart = index == 0;
    selectionRect.selectionContainsEnd = index + 1 == query.rects.size();
    [selectionRects addObject:selectionRect];
#if !__has_feature(objc_arc)
    [selectionRect release];
#endif
  }
  return selectionRects;
}

- (UITextPosition *)closestPositionToPoint:(CGPoint)point {
  return [self closestPositionToPoint:point
                          withinRange:[self textRangeFromPosition:self.beginningOfDocument
                                                       toPosition:self.endOfDocument]];
}

- (UITextPosition *)closestPositionToPoint:(CGPoint)point withinRange:(UITextRange *)range {
  if (!_session) {
    return nil;
  }
  [self refreshGeometry];
  EditingStateSnapshot snapshot = _session->Snapshot();
  CGPoint viewportPoint = [self convertPoint:point toView:self.coordinateSpaceView];
  std::optional<size_t> anchor;
  if ([range isKindOfClass:LynxEditContextTextRange.class]) {
    anchor = [self nsRangeFromTextRange:range].location;
  }
  EditingPlatformResult result = _session->SetSelectionFromPoint(
      {static_cast<float>(viewportPoint.x), static_cast<float>(viewportPoint.y)}, anchor,
      snapshot.revision);
  if (!result.accepted()) {
    return nil;
  }
  return [LynxEditContextTextPosition positionWithOffset:result.snapshot.selection.position()];
}

- (UITextRange *)characterRangeAtPoint:(CGPoint)point {
  UITextPosition *position = [self closestPositionToPoint:point];
  UITextPosition *end = [self positionFromPosition:position offset:1] ?: position;
  return [self textRangeFromPosition:position toPosition:end];
}

#pragma mark - UITextInput navigation

- (UITextPosition *)positionWithinRange:(UITextRange *)range
                    farthestInDirection:(UITextLayoutDirection)direction {
  LynxEditContextTextRange *typed = [range isKindOfClass:LynxEditContextTextRange.class]
                                        ? (LynxEditContextTextRange *)range
                                        : nil;
  if (!typed) {
    return nil;
  }
  return (direction == UITextLayoutDirectionLeft || direction == UITextLayoutDirectionUp)
             ? typed.start
             : typed.end;
}

- (UITextRange *)characterRangeByExtendingPosition:(UITextPosition *)position
                                       inDirection:(UITextLayoutDirection)direction {
  UITextPosition *other = [self positionFromPosition:position
                                         inDirection:direction
                                              offset:1] ?: position;
  return [self textRangeFromPosition:position toPosition:other];
}

- (NSWritingDirection)baseWritingDirectionForPosition:(UITextPosition *)position
                                          inDirection:(UITextStorageDirection)direction {
  return NSWritingDirectionNatural;
}

- (void)setBaseWritingDirection:(NSWritingDirection)writingDirection
                        forRange:(UITextRange *)range {
}

- (NSDictionary<NSAttributedStringKey, id> *)textStylingAtPosition:(UITextPosition *)position
                                                       inDirection:(UITextStorageDirection)direction {
  return @{};
}

#pragma mark - Delegate callbacks and helpers

- (void)editingStateDidChange:(const lynx::editing::EditingStateUpdate &)update {
  if (lynx::editing::HasStateChange(update.changes, EditingStateChange::kText)) {
    [_inputDelegate textWillChange:self];
    [_inputDelegate textDidChange:self];
  }
  if (lynx::editing::HasStateChange(update.changes, EditingStateChange::kSelection) ||
      lynx::editing::HasStateChange(update.changes, EditingStateChange::kComposition)) {
    [_inputDelegate selectionWillChange:self];
    [_inputDelegate selectionDidChange:self];
  }
  if (update.restart_input && self.isFirstResponder) {
    [self reloadInputViews];
  }
}

- (void)editingActivationDidChange:(BOOL)active {
  if (!active && self.isFirstResponder) {
    dispatch_async(dispatch_get_main_queue(), ^{
      [self resignFirstResponder];
    });
  }
}

- (void)editingGeometryRequested:(const TextRange &)range
                   stateRevision:(uint64_t)stateRevision
              projectionRevision:(uint64_t)projectionRevision {
  id<LynxEditContextGeometrySource> source = self.geometrySource;
  if (!source || !_session) {
    return;
  }
  lynx::editing::EditingGeometrySnapshot geometry;
  lynx::editing::EditingProjectionSnapshot projection = _session->ProjectionSnapshot();
  if (projection.revision != projectionRevision) {
    return;
  }
  if ([source editContextInputClient:self
             collectGeometryForRange:LynxNSRangeFromTextRange(range)
                       stateRevision:stateRevision
                          projection:projection
                            snapshot:&geometry]) {
    // Contract callbacks are non-reentrant. Submit after the callback returns.
    dispatch_async(dispatch_get_main_queue(), ^{
      if (self->_session) {
        self->_session->UpdateGeometry(std::move(geometry));
      }
    });
  }
}

- (void)applyTransaction:(const NativeTextTransaction &)transaction {
  [self applyResult:_session->ApplyTransaction(transaction)];
}

- (void)applyResult:(const EditingPlatformResult &)result {
  if (result.restart_input && self.isFirstResponder) {
    [self reloadInputViews];
  }
}

- (LynxEditContextTextPosition *)position:(UITextPosition *)position {
  return [position isKindOfClass:LynxEditContextTextPosition.class]
             ? (LynxEditContextTextPosition *)position
             : nil;
}

- (NSRange)nsRangeFromTextRange:(UITextRange *)range {
  LynxEditContextTextRange *typed = (LynxEditContextTextRange *)range;
  return NSMakeRange(typed.rangeStart.offset, typed.rangeEnd.offset - typed.rangeStart.offset);
}

@end

namespace {

void DarwinEditingDelegate::OnStateChanged(const lynx::editing::EditingStateUpdate &update) {
  [client_ editingStateDidChange:update];
}

void DarwinEditingDelegate::OnActivationChanged(bool active) {
  [client_ editingActivationDidChange:active];
}

void DarwinEditingDelegate::OnGeometryRequested(const TextRange &range, uint64_t state_revision,
                                                uint64_t projection_revision) {
  [client_ editingGeometryRequested:range
                      stateRevision:state_revision
                 projectionRevision:projection_revision];
}

}  // namespace
