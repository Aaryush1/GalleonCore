# SPDX-FileCopyrightText: 2026 Paul Woisard
# SPDX-License-Identifier: GPL-3.0-or-later

# Read-only: reads the HID LampArray attributes (lamp count, bounding box, kind,
# minimum update interval) of the Galleon 100 SD's two LampArray interfaces.
#
# Usages come from the HID Usage Tables, "Lighting and Illumination" page (0x59):
# LampCount 0x03, BoundingBoxWidth/Height/Depth 0x04..0x06, LampArrayKind 0x07,
# MinUpdateInterval 0x08. The report layout is not hard-coded: the report ID and
# bit positions come from the device's own descriptor (HidP_GetValueCaps) and the
# values are decoded by HidP_GetUsageValue. The only request sent is a feature
# report GET (HidD_GetFeature). Nothing is written to the device.
Add-Type -TypeDefinition @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

public static class LampArrayProbe
{
    const int ValueCapsSize = 72; // sizeof(HIDP_VALUE_CAPS) on 64-bit Windows

    [StructLayout(LayoutKind.Sequential)]
    struct Caps
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
    [DllImport("hid.dll")] static extern bool HidD_GetFeature(SafeFileHandle h, byte[] buf, int len);
    [DllImport("hid.dll")] static extern int HidP_GetCaps(IntPtr pp, out Caps caps);
    [DllImport("hid.dll")] static extern int HidP_GetValueCaps(int type, byte[] caps, ref ushort len, IntPtr pp);
    [DllImport("hid.dll")]
    static extern int HidP_GetUsageValue(int type, ushort page, ushort link, ushort usage,
                                         out uint value, IntPtr pp, byte[] report, uint reportLen);

    const int Feature = 2;
    const int StatusSuccess = 0x110000;

    static SafeFileHandle Open(string path)
    {
        // Read access first; some drivers also need write access to answer a GET.
        var h = CreateFile(path, 0x80000000, 3, IntPtr.Zero, 3, 0, IntPtr.Zero);
        if (h.IsInvalid) h = CreateFile(path, 0xC0000000, 3, IntPtr.Zero, 3, 0, IntPtr.Zero);
        return h;
    }

    public static string[] Run(string path)
    {
        var lines = new List<string>();
        using (var h = Open(path))
        {
            if (h.IsInvalid) { lines.Add("open failed (error " + Marshal.GetLastWin32Error() + ")"); return lines.ToArray(); }
            IntPtr pp;
            if (!HidD_GetPreparsedData(h, out pp)) { lines.Add("no preparsed data"); return lines.ToArray(); }
            try
            {
                Caps caps;
                if (HidP_GetCaps(pp, out caps) != StatusSuccess) { lines.Add("HidP_GetCaps failed"); return lines.ToArray(); }
                lines.Add("feature report length (with ID): " + caps.FeatureReportByteLength +
                          ", feature value caps: " + caps.NumberFeatureValueCaps);

                ushort n = caps.NumberFeatureValueCaps;
                var raw = new byte[ValueCapsSize * n];
                if (HidP_GetValueCaps(Feature, raw, ref n, pp) != StatusSuccess) { lines.Add("HidP_GetValueCaps failed"); return lines.ToArray(); }

                int attributesReportId = -1;
                for (int i = 0; i < n; i++)
                {
                    int o = i * ValueCapsSize;
                    ushort page = BitConverter.ToUInt16(raw, o);
                    byte reportId = raw[o + 2];
                    bool isRange = raw[o + 12] != 0;
                    ushort bits = BitConverter.ToUInt16(raw, o + 18);
                    ushort count = BitConverter.ToUInt16(raw, o + 20);
                    ushort usage = BitConverter.ToUInt16(raw, o + 56);
                    lines.Add(string.Format("  cap {0}: report {1} page 0x{2:X4} usage 0x{3:X4}{4} bits {5} x {6}",
                                            i, reportId, page, usage, isRange ? " (range)" : "", bits, count));
                    // LampCount also appears in the multi-update report: the attributes
                    // report is the first one that declares it.
                    if (attributesReportId < 0 && page == 0x59 && usage == 0x03 && !isRange)
                        attributesReportId = reportId;
                }

                if (attributesReportId < 0) { lines.Add("no LampCount usage found: not a LampArray?"); return lines.ToArray(); }

                var buf = new byte[caps.FeatureReportByteLength];
                buf[0] = (byte)attributesReportId;
                if (!HidD_GetFeature(h, buf, buf.Length))
                { lines.Add("HidD_GetFeature failed (error " + Marshal.GetLastWin32Error() + ")"); return lines.ToArray(); }

                lines.Add("attributes report ID " + attributesReportId + ", raw: " +
                          BitConverter.ToString(buf, 0, Math.Min(buf.Length, 24)));
                if (buf[0] != attributesReportId)
                {
                    lines.Add("  WARNING: asked for report " + attributesReportId + " but the device answered with ID byte " +
                              buf[0] + "; decoding the payload as report " + attributesReportId + " anyway");
                    buf[0] = (byte)attributesReportId;
                }
                string[] names = { "LampCount", "BoundingBoxWidth(um)", "BoundingBoxHeight(um)",
                                   "BoundingBoxDepth(um)", "LampArrayKind", "MinUpdateInterval(us)" };
                ushort[] usages = { 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 };
                for (int i = 0; i < usages.Length; i++)
                {
                    uint v;
                    int st = HidP_GetUsageValue(Feature, 0x59, 0, usages[i], out v, pp, buf, (uint)buf.Length);
                    lines.Add(string.Format("  {0,-24} {1}", names[i], st == StatusSuccess ? v.ToString() : "n/a (status 0x" + st.ToString("X") + ")"));
                }
            }
            finally { HidD_FreePreparsedData(pp); }
        }
        return lines.ToArray();
    }
}
"@

$guid = '{4d1e55b2-f16f-11cf-88cb-001111000030}'
foreach ($id in 'VID_1B1C&PID_2B0C&MI_04', 'VID_1B1C&PID_2B18&MI_03') {
    $dev = Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -like "HID\$id*" } | Select-Object -First 1
    "== $id"
    if (-not $dev) { '  not present'; continue }
    $path = '\\?\' + ($dev.InstanceId -replace '\\', '#') + '#' + $guid
    [LampArrayProbe]::Run($path)
}
