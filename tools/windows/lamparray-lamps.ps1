# SPDX-FileCopyrightText: 2026 Paul Woisard
# SPDX-License-Identifier: GPL-3.0-or-later

# Reads the per-lamp attributes (position, purposes, levels) of the Galleon 100 SD's
# two HID LampArray interfaces and writes one CSV per interface.
#
# For each lamp it sends the standard LampAttributesRequest (a feature report SET that
# only selects which lamp the next answer describes, the same query Windows Dynamic
# Lighting makes) and then reads the LampAttributesResponse (feature report GET).
# It never sends a lamp update, a range update or a control report: lighting and
# autonomous mode are not touched.
#
# Usages: HID Usage Tables, "Lighting and Illumination" page (0x59). Report IDs and bit
# positions come from the device's own descriptor.
#
#   .\lamparray-lamps.ps1 -OutDir C:\temp
param([string]$OutDir = '.')

Add-Type -TypeDefinition @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

public class LampRow
{
    public uint Id, X, Y, Z, Purposes, Latency, R, G, B, I, Programmable, InputBinding;
    public int AnswerIdByte;
    public bool Ok;
    public string Error;
}

public static class LampProbe
{
    const int ValueCapsSize = 72;

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
    [DllImport("hid.dll")] static extern bool HidD_SetFeature(SafeFileHandle h, byte[] buf, int len);
    [DllImport("hid.dll")] static extern int HidP_GetCaps(IntPtr pp, out Caps caps);
    [DllImport("hid.dll")] static extern int HidP_GetValueCaps(int type, byte[] caps, ref ushort len, IntPtr pp);
    [DllImport("hid.dll")]
    static extern int HidP_GetUsageValue(int type, ushort page, ushort link, ushort usage,
                                         out uint value, IntPtr pp, byte[] report, uint reportLen);
    [DllImport("hid.dll")]
    static extern int HidP_SetUsageValue(int type, ushort page, ushort link, ushort usage,
                                         uint value, IntPtr pp, byte[] report, uint reportLen);

    const int Feature = 2;
    const int StatusSuccess = 0x110000;

    static uint Get(IntPtr pp, byte[] buf, ushort usage)
    {
        uint v;
        if (HidP_GetUsageValue(Feature, 0x59, 0, usage, out v, pp, buf, (uint)buf.Length) != StatusSuccess)
            throw new Exception("cannot decode usage 0x" + usage.ToString("X"));
        return v;
    }

    public static string Run(string path, List<LampRow> rows)
    {
        var h = CreateFile(path, 0xC0000000, 3, IntPtr.Zero, 3, 0, IntPtr.Zero);
        if (h.IsInvalid) return "open failed (error " + Marshal.GetLastWin32Error() + ")";
        using (h)
        {
            IntPtr pp;
            if (!HidD_GetPreparsedData(h, out pp)) return "no preparsed data";
            try
            {
                Caps caps;
                if (HidP_GetCaps(pp, out caps) != StatusSuccess) return "HidP_GetCaps failed";
                ushort n = caps.NumberFeatureValueCaps;
                var raw = new byte[ValueCapsSize * n];
                if (HidP_GetValueCaps(Feature, raw, ref n, pp) != StatusSuccess) return "HidP_GetValueCaps failed";

                int attrId = -1, reqId = -1, respId = -1;
                for (int i = 0; i < n; i++)
                {
                    int o = i * ValueCapsSize;
                    if (BitConverter.ToUInt16(raw, o) != 0x59 || raw[o + 12] != 0) continue;
                    byte rid = raw[o + 2];
                    ushort usage = BitConverter.ToUInt16(raw, o + 56);
                    if (attrId < 0 && usage == 0x03) attrId = rid;
                    if (reqId < 0 && usage == 0x21) reqId = rid;
                    if (respId < 0 && usage == 0x23) respId = rid;
                }
                if (attrId < 0 || reqId < 0 || respId < 0) return "attributes/request/response reports not found";

                int len = caps.FeatureReportByteLength;
                var buf = new byte[len];
                buf[0] = (byte)attrId;
                if (!HidD_GetFeature(h, buf, len)) return "attributes GET failed (error " + Marshal.GetLastWin32Error() + ")";
                buf[0] = (byte)attrId; // the keyboard part answers with another ID byte, see the notes
                uint count = Get(pp, buf, 0x03);
                if (count == 0 || count > 1024) return "implausible lamp count " + count;

                for (uint lamp = 0; lamp < count; lamp++)
                {
                    var row = new LampRow { Id = lamp };
                    rows.Add(row);
                    var req = new byte[len];
                    req[0] = (byte)reqId;
                    if (HidP_SetUsageValue(Feature, 0x59, 0, 0x21, lamp, pp, req, (uint)len) != StatusSuccess)
                    { row.Error = "cannot encode request"; continue; }
                    if (!HidD_SetFeature(h, req, len))
                    { row.Error = "request SET failed (error " + Marshal.GetLastWin32Error() + ")"; continue; }
                    var resp = new byte[len];
                    resp[0] = (byte)respId;
                    if (!HidD_GetFeature(h, resp, len))
                    { row.Error = "response GET failed (error " + Marshal.GetLastWin32Error() + ")"; continue; }
                    row.AnswerIdByte = resp[0];
                    resp[0] = (byte)respId;
                    try
                    {
                        uint answered = Get(pp, resp, 0x21);
                        if (answered != lamp) { row.Error = "answered for lamp " + answered; continue; }
                        row.X = Get(pp, resp, 0x23); row.Y = Get(pp, resp, 0x24); row.Z = Get(pp, resp, 0x25);
                        row.Purposes = Get(pp, resp, 0x26); row.Latency = Get(pp, resp, 0x27);
                        row.R = Get(pp, resp, 0x28); row.G = Get(pp, resp, 0x29); row.B = Get(pp, resp, 0x2A);
                        row.I = Get(pp, resp, 0x2B); row.Programmable = Get(pp, resp, 0x2C);
                        row.InputBinding = Get(pp, resp, 0x2D);
                        row.Ok = true;
                    }
                    catch (Exception e) { row.Error = e.Message; }
                    System.Threading.Thread.Sleep(5);
                }
                return "ok: attributes report " + attrId + ", request " + reqId + ", response " + respId +
                       ", " + count + " lamps";
            }
            finally { HidD_FreePreparsedData(pp); }
        }
    }
}
"@

$guid = '{4d1e55b2-f16f-11cf-88cb-001111000030}'
New-Item -ItemType Directory -Force $OutDir | Out-Null
foreach ($id in 'VID_1B1C&PID_2B0C&MI_04', 'VID_1B1C&PID_2B18&MI_03') {
    "== $id"
    $dev = Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -like "HID\$id*" } | Select-Object -First 1
    if (-not $dev) { '  not present'; continue }
    $path = '\\?\' + ($dev.InstanceId -replace '\\', '#') + '#' + $guid
    $rows = New-Object 'System.Collections.Generic.List[LampRow]'
    '  ' + [LampProbe]::Run($path, $rows)
    $good = @($rows | Where-Object Ok)
    $bad = @($rows | Where-Object { -not $_.Ok })
    "  decoded $($good.Count) lamps, $($bad.Count) errors"
    $bad | Select-Object -First 5 | ForEach-Object { "    lamp $($_.Id): $($_.Error)" }
    $idBytes = $good | Group-Object AnswerIdByte | ForEach-Object { "$($_.Name) x$($_.Count)" }
    "  ID byte of the answers: $($idBytes -join ', ')"
    $file = Join-Path $OutDir ("lamps-" + ($id -replace '.*PID_(\w+)&.*', '$1').ToLower() + ".csv")
    $good | Select-Object Id,
        @{n='x_um';e={$_.X}}, @{n='y_um';e={$_.Y}}, @{n='z_um';e={$_.Z}},
        Purposes, @{n='latency_us';e={$_.Latency}},
        @{n='red_levels';e={$_.R}}, @{n='green_levels';e={$_.G}}, @{n='blue_levels';e={$_.B}},
        @{n='intensity_levels';e={$_.I}}, Programmable, InputBinding |
        Export-Csv -NoTypeInformation -Path $file
    "  written: $file"
}
