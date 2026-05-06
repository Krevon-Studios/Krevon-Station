// drawer_sound_model.cpp - Animated sound-panel list model synchronization.
#include "drawer_sound_model.h"

void SyncSoundPanelLists()
{
    bool changed = false;

    // Devices
    for (auto& a : g_animEndpoints) {
        if (!a.isRemoving) {
            auto it = std::find_if(g_snap.audioEndpoints.begin(), g_snap.audioEndpoints.end(),
                [&](const AudioEndpoint& e) { return e.id == a.data.id; });
            if (it == g_snap.audioEndpoints.end()) {
                a.isRemoving = true;
                changed = true;
            } else {
                a.data = *it;
            }
        }
    }
    for (const auto& e : g_snap.audioEndpoints) {
        auto it = std::find_if(g_animEndpoints.begin(), g_animEndpoints.end(),
            [&](const AnimEndpoint& a) { return a.data.id == e.id; });
        if (it == g_animEndpoints.end()) {
            g_animEndpoints.push_back({ e, 0.0f, 0.9f, false });
            changed = true;
        } else if (it->isRemoving) {
            it->isRemoving = false;
            it->data = e;
            changed = true;
        }
    }

    // Sessions
    for (auto& a : g_animSessions) {
        if (!a.isRemoving) {
            auto it = std::find_if(g_snap.audioSessions.begin(), g_snap.audioSessions.end(),
                [&](const AudioSession& s) { return s.processId == a.data.processId; });
            if (it == g_snap.audioSessions.end()) {
                a.isRemoving = true;
                changed = true;
            } else {
                a.data = *it;
            }
        }
    }
    for (const auto& s : g_snap.audioSessions) {
        auto it = std::find_if(g_animSessions.begin(), g_animSessions.end(),
            [&](const AnimSession& a) { return a.data.processId == s.processId; });
        if (it == g_animSessions.end()) {
            g_animSessions.push_back({ s, 0.0f, 0.9f, false });
            changed = true;
        } else if (it->isRemoving) {
            it->isRemoving = false;
            it->data = s;
            changed = true;
        }
    }

    if (changed && g_drawerHwnd) {
        SetTimer(g_drawerHwnd, SND_LIST_TIMER, 16, nullptr);
    }
}
