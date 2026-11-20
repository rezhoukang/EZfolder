using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

// Sample the compositor separately from the thread that requests a new folder.
// WS_VISIBLE alone also counts frames that DWM is deliberately not presenting.
public sealed class FramePresentation : IDisposable {
    private delegate bool Visit(IntPtr w, IntPtr p);
    [DllImport("user32")] private static extern bool EnumWindows(Visit visit, IntPtr p);
    [DllImport("user32")] private static extern bool IsWindowVisible(IntPtr w);
    [DllImport("user32", CharSet = CharSet.Unicode)] private static extern IntPtr GetProp(IntPtr w, string key);
    [DllImport("user32", CharSet = CharSet.Unicode)]
    private static extern int GetClassName(IntPtr w, StringBuilder name, int size);
    [DllImport("dwmapi")] private static extern int DwmGetWindowAttribute(IntPtr w, int key, out uint value, int size);
    private readonly HashSet<IntPtr> existing = new HashSet<IntPtr>();
    [DllImport("user32")] private static extern IntPtr SendMessageTimeout(IntPtr w, uint msg, IntPtr a, IntPtr b, uint flags, uint ms, out IntPtr result);
    private readonly IntPtr hostTabs;
    private int previousIndex;
    public int ObservedTabSwitches { get; private set; }
    private readonly Stopwatch clock = Stopwatch.StartNew();
    private readonly Thread worker;
    private volatile bool stop;
    private bool visibleLast;
    private double previous;
    public int ComposedSamples { get; private set; }
    public int BeforeBandSamples { get; private set; }
    public int CloakedSamples { get; private set; }
    public double VisibleMs { get; private set; }
    public double FrameMs { get; private set; }
    private double first = -1;

    private static bool ExplorerFrame(IntPtr w) {
        var name = new StringBuilder(32);
        GetClassName(w, name, name.Capacity);
        return name.ToString() == "CabinetWClass" || name.ToString() == "ExploreWClass";
    }
    public FramePresentation() : this(IntPtr.Zero) {}
    public FramePresentation(IntPtr tabs) {
        hostTabs = tabs;
        EnumWindows((w, p) => { if (ExplorerFrame(w)) existing.Add(w); return true; }, IntPtr.Zero);
        worker = new Thread(Sample);
        worker.IsBackground = true;
        worker.Start();
    }
    private void Sample() {
        while (!stop) {
            bool composed = false, seen = false;
            EnumWindows((w, p) => {
                if (existing.Contains(w) || !IsWindowVisible(w) || !ExplorerFrame(w)) return true;
                uint cloak;
                if (DwmGetWindowAttribute(w, 14, out cloak, sizeof(uint)) != 0) return true;
                seen = true;
                if (cloak == 0) {
                    composed = true;
                    ++ComposedSamples;
                    if (GetProp(w, "EZFolder.Band.2") == IntPtr.Zero) ++BeforeBandSamples;
                }
                else ++CloakedSamples;
                return true;
            }, IntPtr.Zero);
            double now = clock.Elapsed.TotalMilliseconds;
            if (visibleLast) VisibleMs += now - previous;
            if (seen && first < 0) first = now;
            if (seen) FrameMs = now - first;
            visibleLast = composed;
            previous = now;
            IntPtr index;
            if (hostTabs != IntPtr.Zero && SendMessageTimeout(hostTabs, 0x130b, IntPtr.Zero, IntPtr.Zero, 2, 5, out index) != IntPtr.Zero &&
                index.ToInt32() >= 0 && index.ToInt32() != previousIndex) {
                ++ObservedTabSwitches;
                previousIndex = index.ToInt32();
            }
            Thread.Sleep(2);
        }
    }
    public void Dispose() { stop = true; worker.Join(); }
}
