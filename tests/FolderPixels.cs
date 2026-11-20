using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Threading;

// Compare only the pixels which distinguish the two marker names. Keep both
// text-on-background and background-on-text samples so an empty view cannot pass.
public sealed class FolderPixels : IDisposable {
    [DllImport("user32")] static extern IntPtr GetForegroundWindow();
    [DllImport("dwmapi")] public static extern int DwmFlush();
    [DllImport("user32")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    [DllImport("user32")] static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32")] static extern IntPtr WindowFromPoint(Point point);
    [DllImport("user32")] static extern IntPtr GetAncestor(IntPtr w,uint flags);
    [DllImport("user32")] static extern bool GetCursorPos(out Point point);
    [DllImport("user32")] static extern bool SetCursorPos(int x,int y);
    [DllImport("user32")] static extern bool GetWindowRect(IntPtr w,out Bounds bounds);
    [DllImport("user32")] static extern bool SetWindowPos(IntPtr w,IntPtr after,int x,int y,int cx,int cy,uint flags);
    [DllImport("user32")] static extern void mouse_event(uint flags,uint x,uint y,uint data,UIntPtr info);
    struct Bounds { public int Left,Top,Right,Bottom; }
    public static void FocusFixture(IntPtr window) {
        if (GetForegroundWindow()==window || SetForegroundWindow(window)) return;
        // A background test runner cannot always activate a frame. Click only the
        // fixture's own title area, and restore its z-order and the cursor.
        Bounds b; Point original;
        if (!GetWindowRect(window,out b) || !GetCursorPos(out original)) throw new InvalidOperationException("Cannot locate test window.");
        var point=new Point((b.Left+b.Right)/2,b.Top+15);
        SetWindowPos(window,new IntPtr(-1),0,0,0,0,0x13);
        try {
            if (GetAncestor(WindowFromPoint(point),2)!=window) throw new InvalidOperationException("Test window is covered.");
            SetCursorPos(point.X,point.Y);
            mouse_event(6,0,0,0,UIntPtr.Zero);
            Thread.Sleep(45);
        } finally {
            SetCursorPos(original.X,original.Y);
            SetWindowPos(window,new IntPtr(-2),0,0,0,0,0x13);
        }
    }
    [DllImport("user32")] static extern IntPtr SendMessageTimeout(IntPtr w,int message,IntPtr a,IntPtr b,int flags,int ms,out IntPtr value);
    public static void Command(IntPtr w,int message,int argument) {
        IntPtr value;
        if (SendMessageTimeout(w,message,(IntPtr)argument,IntPtr.Zero,2,4000,out value)==IntPtr.Zero || value==IntPtr.Zero)
            throw new InvalidOperationException("The prepared-view fixture did not accept the request.");
    }
    readonly Rectangle area;
    readonly Bitmap bitmap;
    readonly Graphics graphics;
    readonly byte[] pixels;
    readonly byte[][] reference = new byte[2][];
    readonly List<int> brighter = new List<int>(), darker = new List<int>();
    Thread worker;
    readonly Stopwatch clock = new Stopwatch();
    Exception error;
    volatile bool stop;
    public double VisibleMs { get; private set; }
    public double ElapsedMs { get { return clock.Elapsed.TotalMilliseconds; } }

    public FolderPixels(Rectangle rect) {
        area = rect;
        bitmap = new Bitmap(rect.Width, rect.Height, PixelFormat.Format32bppRgb);
        graphics = Graphics.FromImage(bitmap);
        pixels = new byte[rect.Width * rect.Height * 4];
    }
    void Grab() {
        graphics.CopyFromScreen(area.Location, Point.Empty, area.Size);
        var bits = bitmap.LockBits(new Rectangle(Point.Empty,area.Size),ImageLockMode.ReadOnly,PixelFormat.Format32bppRgb);
        try { Marshal.Copy(bits.Scan0,pixels,0,pixels.Length); }
        finally { bitmap.UnlockBits(bits); }
    }
    public void Remember(int index, IntPtr window) {
        if (GetForegroundWindow()!=window) throw new InvalidOperationException("Test frame lost foreground during calibration.");
        Grab();
        reference[index] = (byte[])pixels.Clone();
    }
    public void Calibrate() {
        for (int i=1;i<pixels.Length;i+=4) {
            int difference = reference[1][i]-reference[0][i];
            if (difference>80) brighter.Add(i);
            if (difference< -80) darker.Add(i);
        }
        if (Math.Min(brighter.Count,darker.Count)<12) throw new InvalidOperationException("Marker names are not visually distinguishable in this layout.");
    }
    bool Matches(List<int> offsets, byte[] expected) {
        int matched=0;
        foreach (int i in offsets) if (Math.Abs(expected[i]-pixels[i])<=32) ++matched;
        return matched >= offsets.Count*0.90;
    }
    public void Start(int index,IntPtr target) {
        if (GetForegroundWindow()!=target) throw new InvalidOperationException("Test frame lost foreground before timing.");
        Grab();
        if (!Matches(brighter,reference[1-index]) || !Matches(darker,reference[1-index]))
            throw new InvalidOperationException("Starting marker does not match the calibration.");
        error=null; VisibleMs=-1; stop=false;
        worker=new Thread(delegate() {
            var old=SetThreadDpiAwarenessContext(new IntPtr(-4));
            try {
                while (!stop && clock.ElapsedMilliseconds<5000) {
                    if (GetForegroundWindow()==target) {
                        Grab();
                        if (Matches(brighter,reference[index]) && Matches(darker,reference[index])) {
                            VisibleMs=ElapsedMs;
                            break;
                        }
                    }
                    Thread.Sleep(1);
                }
            } catch (Exception e) { error=e; }
            finally { SetThreadDpiAwarenessContext(old); }
        });
        worker.IsBackground=true;
        clock.Restart();
        worker.Start();
    }
    public void Finish() {
        worker.Join();
        worker=null;
        if (error!=null) throw error;
        if (VisibleMs<0) throw new InvalidOperationException("Target marker was not observed in the foreground test frame.");
    }
    public void Dispose() {
        stop=true;
        if (worker!=null) worker.Join();
        graphics.Dispose();
        bitmap.Dispose();
    }
}
