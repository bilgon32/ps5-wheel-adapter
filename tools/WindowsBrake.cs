// Read-only Windows HID inspection for the connected Arduino Micro (2341:8037).
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Win32.SafeHandles;

public static class WindowsBrake {
    [StructLayout(LayoutKind.Sequential)]
    struct InterfaceData {
        public int Size;
        public Guid ClassGuid;
        public int Flags;
        public IntPtr Reserved;
    }
    [DllImport("hid.dll")] static extern void HidD_GetHidGuid(out Guid guid);
    [DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern IntPtr SetupDiGetClassDevs(ref Guid guid, IntPtr enumerator, IntPtr parent, uint flags);
    [DllImport("setupapi.dll", SetLastError = true)]
    static extern bool SetupDiEnumDeviceInterfaces(IntPtr set, IntPtr device, ref Guid guid, uint index, ref InterfaceData data);
    [DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern bool SetupDiGetDeviceInterfaceDetail(IntPtr set, ref InterfaceData data, IntPtr detail, uint size, out uint required, IntPtr device);
    [DllImport("setupapi.dll")] static extern bool SetupDiDestroyDeviceInfoList(IntPtr set);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern SafeFileHandle CreateFile(string path, uint access, uint share, IntPtr security, uint creation, uint flags, IntPtr template);
    [DllImport("hid.dll")] static extern bool HidD_GetPreparsedData(SafeFileHandle handle, out IntPtr data);
    [DllImport("hid.dll")] static extern bool HidD_FreePreparsedData(IntPtr data);
    [DllImport("hid.dll")] static extern int HidP_GetCaps(IntPtr data, [Out] byte[] caps);
    [DllImport("hid.dll")] static extern int HidP_GetValueCaps(int type, [Out] byte[] caps, ref ushort count, IntPtr data);
    [DllImport("hid.dll")] static extern int HidP_GetUsageValue(int type, ushort page, ushort collection, ushort usage, out uint value, IntPtr data, byte[] report, uint length);
    [DllImport("hid.dll")] static extern int HidP_SetUsageValue(int type, ushort page, ushort collection, ushort usage, uint value, IntPtr data, [In, Out] byte[] report, uint length);

    static ushort U16(byte[] data, int offset) { return BitConverter.ToUInt16(data, offset); }
    static void Check(int status) { if (status != 0x00110000) throw new Exception("HID status " + status.ToString("X8")); }

    public static async Task<List<object>> Inspect(int seconds) {
        var results = new List<object>();
        Guid guid;
        HidD_GetHidGuid(out guid);
        IntPtr set = SetupDiGetClassDevs(ref guid, IntPtr.Zero, IntPtr.Zero, 0x12);
        if (set == new IntPtr(-1)) throw new Win32Exception();
        try {
            for (uint index = 0; ; index++) {
                var iface = new InterfaceData { Size = Marshal.SizeOf<InterfaceData>() };
                if (!SetupDiEnumDeviceInterfaces(set, IntPtr.Zero, ref guid, index, ref iface)) {
                    if (Marshal.GetLastWin32Error() == 259) break;
                    throw new Win32Exception();
                }
                uint required;
                SetupDiGetDeviceInterfaceDetail(set, ref iface, IntPtr.Zero, 0, out required, IntPtr.Zero);
                IntPtr detail = Marshal.AllocHGlobal((int)required);
                string path;
                try {
                    Marshal.WriteInt32(detail, IntPtr.Size == 8 ? 8 : 6);
                    if (!SetupDiGetDeviceInterfaceDetail(set, ref iface, detail, required, out required, IntPtr.Zero)) throw new Win32Exception();
                    path = Marshal.PtrToStringUni(IntPtr.Add(detail, 4));
                } finally { Marshal.FreeHGlobal(detail); }
                if (path.IndexOf("vid_2341&pid_8037", StringComparison.OrdinalIgnoreCase) < 0) continue;
                using (var handle = CreateFile(path, seconds > 0 ? 0x80000000u : 0, 3, IntPtr.Zero, 3, 0x40000000, IntPtr.Zero)) {
                    if (handle.IsInvalid) throw new Win32Exception();
                    IntPtr pp;
                    if (!HidD_GetPreparsedData(handle, out pp)) throw new Win32Exception();
                    try {
                        var caps = new byte[64];
                        Check(HidP_GetCaps(pp, caps));
                        ushort length = U16(caps, 4), count = U16(caps, 48);
                        var values = new byte[count * 72];
                        if (count > 0) Check(HidP_GetValueCaps(0, values, ref count, pp));
                        var axes = new List<object>();
                        ushort rzCollection = 0;
                        byte rzReport = 0;
                        ushort rzBits = 0;
                        bool rzSigned = false;
                        bool hasRz = false;
                        for (int i = 0; i < count; i++) {
                            int o = i * 72;
                            ushort page = U16(values, o), usage = U16(values, o + 56);
                            ushort last = values[o + 12] != 0 ? U16(values, o + 58) : usage;
                            axes.Add(new { UsagePage = page, UsageMin = usage, UsageMax = last,
                                ReportId = values[o + 2], BitSize = U16(values, o + 18), ReportCount = U16(values, o + 20),
                                LogicalMin = BitConverter.ToInt32(values, o + 40), LogicalMax = BitConverter.ToInt32(values, o + 44) });
                            if (page == 1 && usage <= 0x35 && last >= 0x35) {
                                rzCollection = U16(values, o + 6); rzReport = values[o + 2]; hasRz = true;
                                rzBits = U16(values, o + 18); rzSigned = BitConverter.ToInt32(values, o + 40) < 0;
                            }
                        }
                        // SetUsageValue changes an in-memory buffer only, never the device.
                        // Its single set bit reveals Rz's wire offset without guessing a layout.
                        int rzBitOffset = -1;
                        if (hasRz) {
                            var probe = new byte[length]; probe[0] = rzReport;
                            Check(HidP_SetUsageValue(0, 1, rzCollection, 0x35, 1, pp, probe, length));
                            for (int b = 8; b < probe.Length * 8; b++)
                                if ((probe[b / 8] & (1 << (b % 8))) != 0) { rzBitOffset = b - 8; break; }
                        }
                        var samples = new List<object>();
                        int reports = 0;
                        long min = long.MaxValue, max = long.MinValue, previous = long.MaxValue;
                        if (seconds > 0 && hasRz) {
                            using (var stream = new FileStream(handle, FileAccess.Read, length, true))
                            using (var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(seconds))) {
                                var report = new byte[length];
                                try {
                                    while (true) {
                                        int received = await stream.ReadAsync(report, 0, report.Length, timeout.Token).ConfigureAwait(false);
                                        if (received == 0) break;
                                        if (received != length || report[0] != rzReport) continue;
                                        uint value;
                                        Check(HidP_GetUsageValue(0, 1, rzCollection, 0x35, out value, pp, report, length));
                                        long signedValue = value;
                                        if (rzSigned && rzBits > 0 && rzBits <= 32 && (value & (1u << (rzBits - 1))) != 0)
                                            signedValue -= 1L << rzBits;
                                        reports++; min = Math.Min(min, signedValue); max = Math.Max(max, signedValue);
                                        if (signedValue != previous && samples.Count < 100) samples.Add(new { Rz = signedValue, Hex = BitConverter.ToString(report) });
                                        previous = signedValue;
                                    }
                                } catch (OperationCanceledException) { }
                            }
                        }
                        results.Add(new { Vid = "2341", Pid = "8037", InputReportBytes = length,
                            UsagePage = U16(caps, 2), Usage = U16(caps, 0), Axes = axes,
                            RzPayloadBitOffset = rzBitOffset, ReportsRead = reports,
                            Minimum = reports > 0 ? (long?)min : null, Maximum = reports > 0 ? (long?)max : null, Samples = samples });
                    } finally { HidD_FreePreparsedData(pp); }
                }
            }
        } finally { SetupDiDestroyDeviceInfoList(set); }
        if (results.Count == 0) throw new Exception("Arduino Micro 2341:8037 was not found.");
        return results;
    }
}
