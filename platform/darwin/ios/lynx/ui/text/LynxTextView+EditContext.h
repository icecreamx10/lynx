// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#ifndef PLATFORM_DARWIN_IOS_LYNX_UI_TEXT_LYNXTEXTVIEW_EDITCONTEXT_H_
#define PLATFORM_DARWIN_IOS_LYNX_UI_TEXT_LYNXTEXTVIEW_EDITCONTEXT_H_

#import "../../public/ui/text/LynxTextView.h"

#ifdef __cplusplus
#include <memory>

namespace lynx::editing {
class EditingPlatformSession;
}
#endif

NS_ASSUME_NONNULL_BEGIN

// Internal platform lifecycle bridge. Editing state and semantics remain in
// the shared C++ session.
@interface LynxTextView (EditContext)
#ifdef __cplusplus
- (void)attachEditContextSession:
    (std::shared_ptr<lynx::editing::EditingPlatformSession>)session;
#endif
- (BOOL)activateEditContext;
- (void)deactivateEditContext;
- (void)detachEditContext;
- (void)refreshEditContextGeometry;
@end

NS_ASSUME_NONNULL_END

#endif  // PLATFORM_DARWIN_IOS_LYNX_UI_TEXT_LYNXTEXTVIEW_EDITCONTEXT_H_
