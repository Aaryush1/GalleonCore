# SPDX-FileCopyrightText: 2026 Paul Woisard
# SPDX-License-Identifier: GPL-3.0-or-later

# Read-only: opens each HID interface with ZERO access rights (like hidapi's
# enumeration) and reads its declared capabilities. Nothing is sent to the device.
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

public static class HidCaps
{
    [StructLayout(LayoutKind.Sequential)]
    public struct Caps
    {
        public ushort Usage, UsagePage, InputReportByteLength, OutputReportByteLength,
                      FeatureReportByteLength;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 17)] public ushort[] Reserved;
        public ushort NumberLinkCollectionNodes, NumberInputButtonCaps, NumberInputValueCaps,
                      NumberInputDataIndices, NumberOutputButtonCaps, NumberOutputValueCaps,
                      NumberOutputDataIndices, NumberFeatureButtonCaps, NumberFeatureValueCaps,
                      NumberFeatureDataIndices;
    }

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern SafeFileHandle CreateFile(string name, uint access, uint share, IntPtr sec,
                                            uint disp, uint flags, IntPtr tmpl);
    [DllImport("hid.dll")] static extern bool HidD_GetPreparsedData(SafeFileHandle h, out IntPtr pp);
    [DllImport("hid.dll")] static extern bool HidD_FreePreparsedData(IntPtr pp);
    [DllImport("hid.dll")] static extern int HidP_GetCaps(IntPtr pp, out Caps caps);

    public static string Query(string path)
    {
        using (var h = CreateFile(path, 0, 3, IntPtr.Zero, 3, 0, IntPtr.Zero))
        {
            if (h.IsInvalid) return "open failed (error " + Marshal.GetLastWin32Error() + ")";
            IntPtr pp;
            if (!HidD_GetPreparsedData(h, out pp)) return "no preparsed data";
            try
            {
                Caps c;
                if (HidP_GetCaps(pp, out c) != 0x110000) return "HidP_GetCaps failed";
                return string.Format(
                    "UsagePage=0x{0:X4} Usage=0x{1:X4} in={2} out={3} feature={4}",
                    c.UsagePage, c.Usage, c.InputReportByteLength,
                    c.OutputReportByteLength, c.FeatureReportByteLength);
            }
            finally { HidD_FreePreparsedData(pp); }
        }
    }
}
"@

$guid = '{4d1e55b2-f16f-11cf-88cb-001111000030}'
Get-PnpDevice -PresentOnly -Class HIDClass,Keyboard,Mouse |
    Where-Object { $_.InstanceId -match '^HID\\VID_1B1C&PID_(2B0C|2B18)' } |
    Sort-Object InstanceId | ForEach-Object {
        $path = '\\?\' + ($_.InstanceId -replace '\\', '#') + '#' + $guid
        $id = ($_.InstanceId -split '\\')[1]
        '{0,-32} {1}' -f $id, [HidCaps]::Query($path)
    }
