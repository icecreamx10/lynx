// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include "core/renderer/dom/element_layout_node_manager.h"

#include <memory>

#include "core/renderer/dom/element_manager.h"
#include "core/renderer/dom/fiber/raw_text_element.h"
#include "core/renderer/dom/fiber/text_element.h"
#include "core/renderer/dom/fiber/view_element.h"
#include "core/renderer/dom/testing/fiber_element_test.h"
#include "core/renderer/editing/editing_host_controller.h"
#include "core/renderer/editing/editing_host_registry.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace lynx {
namespace tasm {
namespace testing {

class EditingProjectionBuilderTest : public FiberElementTest {};

TEST_P(EditingProjectionBuilderTest, BuildsIssueRichTextProjection) {
  auto page = manager->CreateFiberPage("page", 11);
  auto host = manager->CreateFiberText("text");
  page->InsertNode(host);

  auto prefix = manager->CreateFiberRawText();
  prefix->SetText(lepus::Value("prefix "));
  host->InsertNode(prefix);

  auto marked = manager->CreateFiberText("text");
  marked->SetAttribute("text", lepus::Value("marked"));
  host->InsertNode(marked);

  auto atom = manager->CreateFiberView();
  atom->SetAttribute("contenteditable", lepus::Value("false"));
  auto atom_label = manager->CreateFiberText("text");
  atom_label->SetAttribute("text", lepus::Value("@lynx"));
  atom->InsertNode(atom_label);
  host->InsertNode(atom);

  auto block = manager->CreateFiberView();
  block->SetStyle(kPropertyIDDisplay, lepus::Value("block"));
  auto block_text = manager->CreateFiberText("text");
  block_text->SetAttribute("text", lepus::Value("block"));
  block->InsertNode(block_text);
  host->InsertNode(block);

  page->FlushActionsAsRoot();

  const editing::EditingProjection projection =
      BuildEditingProjection(host.get());
  EXPECT_EQ(projection.text(), u"prefix marked\uFFFC\nblock\n");
  ASSERT_EQ(projection.segments().size(), 6u);

  const auto& prefix_segment = projection.segments()[0];
  EXPECT_EQ(prefix_segment.owner_id, host->impl_id());
  EXPECT_EQ(prefix_segment.segment_id, prefix->impl_id());
  EXPECT_EQ(prefix_segment.text, u"prefix ");

  const auto& marked_segment = projection.segments()[1];
  EXPECT_EQ(marked_segment.owner_id, host->impl_id());
  EXPECT_EQ(marked_segment.segment_id, marked->impl_id());
  EXPECT_EQ(marked_segment.text, u"marked");

  const auto& atom_segment = projection.segments()[2];
  EXPECT_EQ(atom_segment.kind, editing::EditingSegmentKind::kAtomicObject);
  EXPECT_EQ(atom_segment.segment_id, atom->impl_id());
  EXPECT_EQ(atom_segment.plain_text, u"@lynx");

  EXPECT_EQ(projection.segments()[3].boundary_edge,
            editing::EditingBlockBoundaryEdge::kLeading);
  EXPECT_EQ(projection.segments()[4].owner_id, block_text->impl_id());
  EXPECT_EQ(projection.segments()[4].text, u"block");
  EXPECT_EQ(projection.segments()[5].boundary_edge,
            editing::EditingBlockBoundaryEdge::kTrailing);
}

TEST_P(EditingProjectionBuilderTest, SynchronizesControllerAtStateRevision) {
  auto page = manager->CreateFiberPage("page", 11);
  auto host = manager->CreateFiberText("text");
  host->SetAttribute("text", lepus::Value("single"));
  page->InsertNode(host);
  page->FlushActionsAsRoot();

  auto controller = std::make_shared<editing::EditingHostController>(
      editing::EditContextOptions{u"single", 6, 6});
  ASSERT_TRUE(
      manager->editing_host_registry()->Attach(host->impl_id(), controller));

  manager->SynchronizeEditingHostProjection(host->impl_id());
  EXPECT_EQ(controller->projection().text(), u"single");
  EXPECT_EQ(controller->projection_revision(), controller->Snapshot().revision);

  ASSERT_TRUE(controller->UpdateSelection(0, 3));
  host->SetAttribute("text", lepus::Value("changed"));
  host->FlushActionsAsRoot();
  manager->SynchronizeEditingHostProjections();

  EXPECT_EQ(controller->projection().text(), u"changed");
  EXPECT_EQ(controller->projection_revision(), controller->Snapshot().revision);
}

TEST_P(EditingProjectionBuilderTest, IncludesFiberInlineTextContent) {
  auto page = manager->CreateFiberPage("page", 11);
  auto host = manager->CreateFiberText("text");
  auto inline_text = manager->CreateFiberText("text");
  inline_text->SetAttribute("text", lepus::Value("frontend content"));
  host->InsertNode(inline_text);
  page->InsertNode(host);
  page->FlushActionsAsRoot();

  ASSERT_TRUE(inline_text->is_inline_element());
  const editing::EditingProjection projection =
      BuildEditingProjection(host.get());
  ASSERT_EQ(projection.segments().size(), 1u);
  EXPECT_EQ(projection.text(), u"frontend content");
  EXPECT_EQ(projection.segments()[0].owner_id, host->impl_id());
  EXPECT_EQ(projection.segments()[0].segment_id, inline_text->impl_id());
}

TEST_P(EditingProjectionBuilderTest, IncludesVirtualRawTextContent) {
  auto page = manager->CreateFiberPage("page", 11);
  auto host = manager->CreateFiberText("text");
  auto raw_text = manager->CreateFiberNode("raw-text");
  raw_text->SetAttribute("text", lepus::Value("react string child"));
  host->InsertNode(raw_text);
  page->InsertNode(host);
  page->FlushActionsAsRoot();

  EXPECT_EQ(raw_text->GetTag(), "raw-text");
  const editing::EditingProjection projection =
      BuildEditingProjection(host.get());
  ASSERT_EQ(projection.segments().size(), 1u);
  EXPECT_EQ(projection.text(), u"react string child");
  EXPECT_EQ(projection.segments()[0].owner_id, host->impl_id());
  EXPECT_EQ(projection.segments()[0].segment_id, raw_text->impl_id());
}

INSTANTIATE_TEST_SUITE_P(
    EditingProjectionBuilderTestModule, EditingProjectionBuilderTest,
    ::testing::ValuesIn(fiber_element_generation_params));

}  // namespace testing
}  // namespace tasm
}  // namespace lynx
