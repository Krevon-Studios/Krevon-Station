#pragma once
#include "../state/drawer_state.h"

float DrawerPanel_GetHeight(DrawerPanel panel);
void DrawerPanel_Render(DrawerPanel panel, float opacity);
void DrawerPanel_UpdateHover(DrawerPanel panel, D2D1_POINT_2F p);
bool DrawerPanel_HandleClick(DrawerPanel panel, HWND hWnd, D2D1_POINT_2F p);
bool DrawerPanel_HandleWheel(DrawerPanel panel, HWND hWnd, D2D1_POINT_2F p, float delta);
void DrawerPanel_Reset(DrawerPanel panel);

void MainPanel_Render(float opacity);
void PowerPanel_Render(float opacity);
void SoundPanel_UpdateMetrics();
void SoundPanel_Render(float opacity);
void WifiPanel_UpdateMetrics();
void WifiPanel_Render(float opacity);
void BluetoothPanel_UpdateMetrics();
void BluetoothPanel_Render(float opacity);
