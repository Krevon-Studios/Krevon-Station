#pragma once
#include "../state/drawer_state.h"

int HitButton(D2D1_POINT_2F p);
int HitPill(D2D1_POINT_2F p);
int HitPowerItem(D2D1_POINT_2F p);
void DrawerInput_InstallMouseHook();
void DrawerInput_UninstallMouseHook();
bool DrawerInput_HandleMouseWheel(HWND hWnd, WPARAM wParam, LPARAM lParam);
void DrawerInput_HandleMouseMove(HWND hWnd, WPARAM wParam, LPARAM lParam);
void DrawerInput_HandleLButtonDown(HWND hWnd, WPARAM wParam, LPARAM lParam);
void DrawerInput_HandleLButtonUp(HWND hWnd);
void DrawerInput_HandleMouseLeave(HWND hWnd);
