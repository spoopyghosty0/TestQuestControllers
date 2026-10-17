// OpenXR stand-ins for the host renderer: enough for Input::create() to build its rows. Nothing here talks to a
// runtime; handles are just counters and every call succeeds.
#include <openxr/openxr.h>

#include <cstdio>

static uint64_t next_handle = 100;

extern "C" {
XRAPI_ATTR XrResult XRAPI_CALL xrResultToString(XrInstance, XrResult value, char buffer[XR_MAX_RESULT_STRING_SIZE]) {
    std::snprintf(buffer, XR_MAX_RESULT_STRING_SIZE, value == XR_SUCCESS ? "XR_SUCCESS" : "XrResult %d", value);
    return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrStringToPath(XrInstance, const char *, XrPath *path) {
    *path = next_handle++;
    return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrCreateActionSet(XrInstance, const XrActionSetCreateInfo *, XrActionSet *set) {
    *set = (XrActionSet)next_handle++;
    return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrCreateAction(XrActionSet, const XrActionCreateInfo *, XrAction *action) {
    *action = (XrAction)next_handle++;
    return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrSuggestInteractionProfileBindings(XrInstance,
                                                                    const XrInteractionProfileSuggestedBinding *) {
    return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrAttachSessionActionSets(XrSession, const XrSessionActionSetsAttachInfo *) {
    return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrCreateActionSpace(XrSession, const XrActionSpaceCreateInfo *, XrSpace *space) {
    *space = (XrSpace)next_handle++;
    return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrSyncActions(XrSession, const XrActionsSyncInfo *) { return XR_SUCCESS; }
XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStateBoolean(XrSession, const XrActionStateGetInfo *,
                                                       XrActionStateBoolean *) {
    return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStateFloat(XrSession, const XrActionStateGetInfo *, XrActionStateFloat *) {
    return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStateVector2f(XrSession, const XrActionStateGetInfo *,
                                                        XrActionStateVector2f *) {
    return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrLocateSpace(XrSpace, XrSpace, XrTime, XrSpaceLocation *) {
    return XR_ERROR_RUNTIME_FAILURE;
}
XRAPI_ATTR XrResult XRAPI_CALL xrApplyHapticFeedback(XrSession, const XrHapticActionInfo *,
                                                     const XrHapticBaseHeader *) {
    return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrDestroySpace(XrSpace) { return XR_SUCCESS; }
XRAPI_ATTR XrResult XRAPI_CALL xrDestroyActionSet(XrActionSet) { return XR_SUCCESS; }
}
