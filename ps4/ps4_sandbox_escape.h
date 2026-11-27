#pragma once

#include <string>

struct OrbisPs4UsbMountResult
{
  bool attempted = false;
  bool mounted = false;
  bool already_visible = false;
  int visible_entries_before = -1;
  int visible_entries_after = -1;
  std::string detail;
};

OrbisPs4UsbMountResult OrbisPs4MountUsb0Direct();
