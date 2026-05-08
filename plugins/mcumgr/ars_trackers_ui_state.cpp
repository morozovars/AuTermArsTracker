#include "ars_trackers_ui_state.h"

ArsTrackersSessionsUiStateOutput computeArsTrackersSessionsUiState(
        const ArsTrackersSessionsUiStateInput &input)
{
    const bool hasConnectedTrackers = input.connectedTrackersCount > 0;
    const bool hasSessions = input.sessionsCount > 0;
    const bool conflictingOperation = input.scanRunning ||
                                      input.connectDisconnectRunning ||
                                      input.sessionRefreshRunning ||
                                      input.sessionStartRunning ||
                                      input.sessionStopRunning ||
                                      input.sessionDownloadRunning ||
                                      input.sessionDeleteRunning ||
                                      input.bulkSessionsOperationRunning ||
                                      input.backendBusy;

    ArsTrackersSessionsUiStateOutput output;
    // Product/UI policy:
    // Main "Sessions across trackers" controls are intentionally always enabled.
    // Invalid states are handled in the button handlers, not by disabling buttons.
    output.refreshSessionsEnabled = true;
    output.startSessionEnabled = true;
    output.stopSessionEnabled = true;
    output.downloadAllSessionsEnabled = true;
    output.deleteAllSessionsEnabled = true;
    output.perSessionActionsEnabled =
            hasConnectedTrackers && hasSessions && !conflictingOperation;
    return output;
}
