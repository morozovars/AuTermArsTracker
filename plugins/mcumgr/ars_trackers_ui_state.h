#ifndef ARS_TRACKERS_UI_STATE_H
#define ARS_TRACKERS_UI_STATE_H

struct ArsTrackersSessionsUiStateInput
{
    int connectedTrackersCount = 0;
    int sessionsCount = 0;

    bool scanRunning = false;
    bool connectDisconnectRunning = false;

    bool sessionRefreshRunning = false;
    bool sessionStartRunning = false;
    bool sessionStopRunning = false;
    bool sessionDownloadRunning = false;
    bool sessionDeleteRunning = false;
    bool bulkSessionsOperationRunning = false;

    bool backendBusy = false;
};

struct ArsTrackersSessionsUiStateOutput
{
    bool refreshSessionsEnabled = false;
    bool startSessionEnabled = false;
    bool stopSessionEnabled = false;
    bool downloadAllSessionsEnabled = false;
    bool deleteAllSessionsEnabled = false;
    bool perSessionActionsEnabled = false;
};

ArsTrackersSessionsUiStateOutput computeArsTrackersSessionsUiState(
        const ArsTrackersSessionsUiStateInput &input);

#endif // ARS_TRACKERS_UI_STATE_H
