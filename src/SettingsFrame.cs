using System;
using System.ComponentModel;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Reflection;
using System.Runtime.InteropServices;
using Forms = System.Windows.Forms;

// Both settings hosts share one caption, including keyboard-accessible buttons.
class SettingsFrame : Forms.Form {
    const string Repository = "https://github.com/rezhoukang/EZfolder";
    readonly Forms.Panel caption = new Forms.Panel();
    readonly Forms.Label title = new Forms.Label();
    readonly Forms.Button github = new Forms.Button(), close = new Forms.Button();
    readonly Forms.ToolTip tips = new Forms.ToolTip();
    readonly Bitmap mark;
    [DllImport("user32")] static extern bool ReleaseCapture();
    [DllImport("user32")] static extern IntPtr SendMessage(IntPtr w,int msg,IntPtr wp,IntPtr lp);

    protected override Forms.CreateParams CreateParams {
        get {
            var cp=base.CreateParams;
            // Keep the dialog border and system menu. The client caption hosts the link.
            cp.Style=(cp.Style & ~0x00c00000) | 0x00800000 | 0x00080000;
            cp.ExStyle &= ~0x00000001; // WS_EX_DLGMODALFRAME adds a second border.
            return cp;
        }
    }
    public int CaptionHeight { get { return caption.Height; } }
    public event EventHandler CaptionResized {
        add { caption.SizeChanged+=value; }
        remove { caption.SizeChanged-=value; }
    }
    public SettingsFrame() {
        Text="EZFolder 设置";
        FormBorderStyle=Forms.FormBorderStyle.FixedDialog;
        MaximizeBox=MinimizeBox=ShowIcon=false;
        using (var stream=Assembly.GetExecutingAssembly().GetManifestResourceStream("github-mark.png"))
        using (var original=new Bitmap(stream)) mark=new Bitmap(original);
        caption.Dock=Forms.DockStyle.Top;
        title.Text=Text;
        title.Font=SystemFonts.CaptionFont;
        title.TextAlign=ContentAlignment.MiddleLeft;
        title.Dock=Forms.DockStyle.Fill;
        foreach (var button in new[]{github,close}) {
            button.Dock=Forms.DockStyle.Right;
            button.FlatStyle=Forms.FlatStyle.Flat;
            button.FlatAppearance.BorderSize=0;
            button.UseVisualStyleBackColor=false;
        }
        github.Name="GitHub";
        github.AccessibleName="打开 EZFolder 的 GitHub 仓库";
        github.Cursor=Forms.Cursors.Hand;
        tips.SetToolTip(github,"打开 GitHub 仓库");
        github.Paint+=PaintGitHub;
        github.Click+=delegate {
            try { Process.Start(new ProcessStartInfo(Repository) { UseShellExecute=true }); }
            catch (Win32Exception ex) { Forms.MessageBox.Show(this,ex.Message,Text,Forms.MessageBoxButtons.OK,Forms.MessageBoxIcon.Error); }
        };
        close.Name="CloseSettings";
        close.AccessibleName="关闭";
        tips.SetToolTip(close,"关闭");
        close.Paint+=delegate(object sender,Forms.PaintEventArgs e) {
            int arm=Math.Max(3,close.Height/6);
            int x=close.ClientSize.Width/2, y=close.ClientSize.Height/2;
            using (var pen=new Pen(close.ForeColor,Math.Max(1f,close.Height/32f))) {
                e.Graphics.SmoothingMode=SmoothingMode.AntiAlias;
                e.Graphics.DrawLine(pen,x-arm,y-arm,x+arm,y+arm);
                e.Graphics.DrawLine(pen,x-arm,y+arm,x+arm,y-arm);
            }
        };
        close.Click+=delegate { Close(); };
        title.MouseDown+=delegate(object sender,Forms.MouseEventArgs e) {
            if (e.Button==Forms.MouseButtons.Left) {
                ReleaseCapture();
                SendMessage(Handle,0x00a1,(IntPtr)2,IntPtr.Zero); // WM_NCLBUTTONDOWN, HTCAPTION
            }
        };
        caption.Controls.AddRange(new Forms.Control[]{title,github,close});
        Controls.Add(caption);
        ApplyCaptionTheme(false);
    }
    protected override void OnHandleCreated(EventArgs e) {
        base.OnHandleCreated(e);
        using (var g=CreateGraphics()) SizeCaption(g.DpiX);
    }
    void SizeCaption(float dpi) {
        caption.Height=(int)Math.Round(32*dpi/96);
        github.Width=(int)Math.Round(36*dpi/96);
        close.Width=(int)Math.Round(44*dpi/96);
        title.Padding=new Forms.Padding((int)Math.Round(10*dpi/96),0,0,0);
    }
    protected override void WndProc(ref Forms.Message m) {
        base.WndProc(ref m);
        if (m.Msg==0x02e0) SizeCaption(m.WParam.ToInt32() & 0xffff); // WM_DPICHANGED
    }
    protected void ApplyCaptionTheme(bool dark) {
        bool hc=Forms.SystemInformation.HighContrast;
        caption.BackColor=hc ? SystemColors.Window : dark ? Color.FromArgb(39,39,39) : SystemColors.Control;
        caption.ForeColor=hc ? SystemColors.WindowText : dark ? Color.White : SystemColors.ControlText;
        foreach (var b in new[]{github,close}) {
            b.ForeColor=caption.ForeColor; b.BackColor=caption.BackColor;
            b.FlatAppearance.MouseOverBackColor=dark && !hc ? Color.FromArgb(65,65,65) : SystemColors.ControlDark;
        }
        caption.Invalidate(true);
    }
    void PaintGitHub(object sender,Forms.PaintEventArgs e) {
        int side=github.Height/2;
        var rect=new Rectangle((github.Width-side)/2,(github.Height-side)/2,side,side);
        Color ink=github.ForeColor;
        var tint=new ColorMatrix { Matrix00=ink.R/255f,Matrix11=ink.G/255f,Matrix22=ink.B/255f };
        using (var attrs=new ImageAttributes()) {
            attrs.SetColorMatrix(tint);
            e.Graphics.InterpolationMode=InterpolationMode.HighQualityBicubic;
            e.Graphics.PixelOffsetMode=PixelOffsetMode.Half;
            e.Graphics.DrawImage(mark,rect,0,0,mark.Width,mark.Height,GraphicsUnit.Pixel,attrs);
        }
    }
    protected override void Dispose(bool disposing) {
        if (disposing) { tips.Dispose(); mark.Dispose(); }
        base.Dispose(disposing);
    }
}
