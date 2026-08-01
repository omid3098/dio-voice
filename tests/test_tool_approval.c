#include "tool_approval.h"

#include <assert.h>

int main(void) {
    assert(
        DIO_UI_TOOL_APPROVAL_DEFAULT_BUTTON ==
        DIO_UI_TOOL_APPROVAL_DENY_BUTTON);
    assert(
        dio_ui_resolve_tool_approval(
            true,
            DIO_UI_TOOL_APPROVAL_ONCE_BUTTON,
            false) == DIO_UI_TOOL_ONCE);
    assert(
        dio_ui_resolve_tool_approval(
            true,
            DIO_UI_TOOL_APPROVAL_ALWAYS_BUTTON,
            true) == DIO_UI_TOOL_ALWAYS);
    assert(
        dio_ui_resolve_tool_approval(
            true,
            DIO_UI_TOOL_APPROVAL_ALWAYS_BUTTON,
            false) == DIO_UI_TOOL_DENY);
    assert(
        dio_ui_resolve_tool_approval(
            true,
            DIO_UI_TOOL_APPROVAL_DENY_BUTTON,
            true) == DIO_UI_TOOL_DENY);
    assert(
        dio_ui_resolve_tool_approval(true, 0, true) ==
        DIO_UI_TOOL_DENY);
    assert(
        dio_ui_resolve_tool_approval(true, 2, true) ==
        DIO_UI_TOOL_DENY);
    assert(
        dio_ui_resolve_tool_approval(
            false,
            DIO_UI_TOOL_APPROVAL_ONCE_BUTTON,
            true) == DIO_UI_TOOL_DENY);
    return 0;
}
