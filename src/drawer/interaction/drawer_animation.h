#pragma once
#include "../state/drawer_state.h"

void StartAnim(float target, float durMs);
bool TickAnim();
void StartPanelAnim(DrawerPanel target);
bool TickPanelAnim();
bool DrawerAnimation_HandleTimer(HWND hWnd, WPARAM timerId);
