using System;
using System.Drawing;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using ApiInformation = Windows.Foundation.Metadata.ApiInformation;
using Windows.UI.ViewManagement;
using Windows.UI.Xaml;
using Windows.UI.Xaml.Hosting;
using Windows.UI.Xaml.Markup;
using Forms = System.Windows.Forms;
using Controls = Windows.UI.Xaml.Controls;

// Windows hosts and renders the XAML controls inside the settings window.
[ComImport, Guid("E3DCD8C7-3057-4692-99C3-7B7720AFDA31"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IXamlSourceNative2 {
    void AttachToWindow(IntPtr parent);
    IntPtr GetWindowHandle();
    void PreTranslateMessage(ref SettingsWindow.NativeMessage msg, [MarshalAs(UnmanagedType.Bool)] out bool handled);
}

sealed class SettingsWindow : Forms.Form, Forms.IMessageFilter {
    [StructLayout(LayoutKind.Sequential)]
    public struct NativeMessage {
        public IntPtr hwnd;
        public uint message;
        public IntPtr wParam, lParam;
        public uint time;
        public Point point;
        public uint privateData;
    }
    [DllImport("user32")] static extern bool SetWindowPos(IntPtr w,IntPtr after,int x,int y,int width,int height,uint flags);
    [DllImport("dwmapi")] static extern int DwmSetWindowAttribute(IntPtr w,int attribute,ref int value,int size);
    readonly DesktopWindowXamlSource source;
    readonly IXamlSourceNative2 native;
    readonly Controls.Grid page;
    readonly UISettings system = new UISettings();
    readonly IntPtr island;
    readonly ShortcutEditor editor = new ShortcutEditor();

    public SettingsWindow(IntPtr owner) {
        Text = "EZFolder 设置";
        FormBorderStyle = Forms.FormBorderStyle.FixedDialog;
        MaximizeBox = MinimizeBox = false;
        ShowIcon = false;
        StartPosition = Forms.FormStartPosition.Manual;
        Rectangle work = Forms.Screen.FromHandle(owner).WorkingArea;
        Location = work.Location;
        source = new DesktopWindowXamlSource();
        native = (IXamlSourceNative2)(object)source;
        native.AttachToWindow(Handle);
        island = native.GetWindowHandle();
        using (var reader = new StreamReader(Assembly.GetExecutingAssembly().GetManifestResourceStream("settings.xaml")))
            page = (Controls.Grid)XamlReader.Load(reader.ReadToEnd());
        source.Content = page;
        var pc = (Controls.RadioButton)page.FindName("ThisPC");
        var quick = (Controls.RadioButton)page.FindName("QuickAccess");
        var showClose = (Controls.CheckBox)page.FindName("ShowClose");
        var ctrlClick = (Controls.CheckBox)page.FindName("CtrlClick");
        showClose.IsChecked=editor.ShowClose;
        ctrlClick.IsChecked=editor.CtrlClick;
        quick.IsChecked = editor.QuickAccess;
        pc.IsChecked = quick.IsChecked != true;
        var rows=(Controls.StackPanel)page.FindName("Shortcuts");
        var hint=(Controls.TextBlock)page.FindName("ShortcutHint");
        var keys=new Controls.Button[ShortcutEditor.Names.Length];
        for (int i=0;i<keys.Length;i++) {
            int index=i;
            var row=new Controls.Grid { Margin=new Thickness(0,2,0,2) };
            row.ColumnDefinitions.Add(new Controls.ColumnDefinition());
            row.ColumnDefinitions.Add(new Controls.ColumnDefinition { Width=GridLength.Auto });
            row.Children.Add(new Controls.TextBlock {Text=ShortcutEditor.Names[i],VerticalAlignment=VerticalAlignment.Center});
            var button=new Controls.Button { MinWidth=156,HorizontalAlignment=HorizontalAlignment.Stretch };
            Windows.UI.Xaml.Automation.AutomationProperties.SetAutomationId(button,"Binding"+i);
            button.Click+=delegate { editor.Begin(index); };
            button.LostFocus+=delegate { if (editor.Recording==index) editor.CancelCapture(); };
            Windows.UI.Xaml.Controls.Grid.SetColumn(button,1);
            row.Children.Add(button); rows.Children.Add(row); keys[i]=button;
        }
        Action refresh=delegate {
            hint.Text=editor.HintText ?? "";
            hint.Visibility=editor.HintText==null ? Visibility.Collapsed : Visibility.Visible;
            for(int i=0;i<keys.Length;i++) {
                keys[i].Content=editor.Caption(i);
                Windows.UI.Xaml.Automation.AutomationProperties.SetName(keys[i],ShortcutEditor.Names[i]+"，"+editor.Caption(i));
            }
        };
        editor.Changed+=refresh;
        refresh();
        ((Controls.Button)page.FindName("Reset")).Click+=delegate { editor.Reset(); pc.IsChecked=true; showClose.IsChecked=ctrlClick.IsChecked=true; };
        ((Controls.Button)page.FindName("Save")).Click += delegate {
            if (editor.Save(this,quick.IsChecked == true,showClose.IsChecked==true,ctrlClick.IsChecked==true)) Close();
        };
        ((Controls.Button)page.FindName("Cancel")).Click += delegate { Close(); };
        ApplyTheme();
        system.ColorValuesChanged += ColorsChanged;
        Forms.Application.AddMessageFilter(this);
        using (var graphics = CreateGraphics())
            ClientSize = new Size(Math.Min(work.Width-20,(int)Math.Ceiling(416*graphics.DpiX/96)),
                Math.Min(work.Height-64,(int)Math.Ceiling(606*graphics.DpiY/96)));
        Location = new Point(work.Left+(work.Width-Width)/2, work.Top+(work.Height-Height)/2);
        Shown += delegate { (quick.IsChecked == true ? quick : pc).Focus(FocusState.Programmatic); };
    }
    void ApplyTheme() {
        var bg = system.GetColorValue(UIColorType.Background);
        int dark = bg.R+bg.G+bg.B < 384 ? 1 : 0;
        page.RequestedTheme = dark != 0 ? ElementTheme.Dark : ElementTheme.Light;
        BackColor = Color.FromArgb(bg.R,bg.G,bg.B);
        if (DwmSetWindowAttribute(Handle,20,ref dark,sizeof(int)) < 0)
            DwmSetWindowAttribute(Handle,19,ref dark,sizeof(int));
    }
    void ColorsChanged(UISettings sender,object args) {
        try { BeginInvoke((Action)ApplyTheme); } catch (InvalidOperationException) { }
    }
    protected override void OnResize(EventArgs e) {
        base.OnResize(e);
        if (island != IntPtr.Zero) SetWindowPos(island,IntPtr.Zero,0,0,ClientSize.Width,ClientSize.Height,0x40|0x10);
    }
    public bool PreFilterMessage(ref Forms.Message m) {
        if (m.Msg < 0x100 || m.Msg > 0x109) return false;
        if (editor.PreFilterMessage(ref m)) return true;
        if (m.Msg == 0x100 && m.WParam.ToInt32() == 27) { Close(); return true; }
        var msg = new NativeMessage { hwnd=m.HWnd, message=(uint)m.Msg, wParam=m.WParam, lParam=m.LParam };
        bool handled;
        native.PreTranslateMessage(ref msg,out handled);
        return handled;
    }
    protected override void OnFormClosed(Forms.FormClosedEventArgs e) {
        system.ColorValuesChanged -= ColorsChanged;
        Forms.Application.RemoveMessageFilter(this);
        source.Dispose();
        base.OnFormClosed(e);
    }
}

static class SettingsProgram {
    sealed class ExplorerOwner : Forms.IWin32Window {
        public IntPtr Handle { get; private set; }
        public ExplorerOwner(IntPtr handle) { Handle=handle; }
    }
    static void RunIsland(IntPtr owner) {
        using (var manager = WindowsXamlManager.InitializeForCurrentThread())
        using (var window = new SettingsWindow(owner)) {
            if (owner != IntPtr.Zero) window.Show(new ExplorerOwner(owner));
            Forms.Application.Run(window);
        }
    }
    // Windows 10 before 1903 does not host XAML Islands. Keep the same preference accessible.
    static void RunClassic() {
        Forms.Application.EnableVisualStyles();
        using (var form = new Forms.Form { Text="EZFolder 设置", FormBorderStyle=Forms.FormBorderStyle.FixedDialog,
            Font=SystemFonts.MessageBoxFont, MaximizeBox=false, MinimizeBox=false, AutoSize=true,
            AutoSizeMode=Forms.AutoSizeMode.GrowAndShrink, StartPosition=Forms.FormStartPosition.CenterScreen }) {
            var content=new Forms.FlowLayoutPanel { AutoSize=true, FlowDirection=Forms.FlowDirection.TopDown, Padding=new Forms.Padding(16) };
            var editor=new ShortcutEditor();
            var pc=new Forms.RadioButton { Text="此电脑",AutoSize=true };
            var quick=new Forms.RadioButton { Text="快速访问",AutoSize=true,Checked=editor.QuickAccess };
            pc.Checked=!quick.Checked;
            var showClose=new Forms.CheckBox {Text="显示标签关闭按钮",AutoSize=true,Checked=editor.ShowClose};
            var ctrlClick=new Forms.CheckBox {Text="Ctrl＋单击文件夹时在新标签中打开",AutoSize=true,Checked=editor.CtrlClick};
            var buttons=new Forms.TableLayoutPanel { AutoSize=true, ColumnCount=3, Dock=Forms.DockStyle.Fill };
            buttons.ColumnStyles.Add(new Forms.ColumnStyle(Forms.SizeType.AutoSize));
            buttons.ColumnStyles.Add(new Forms.ColumnStyle(Forms.SizeType.AutoSize));
            buttons.ColumnStyles.Add(new Forms.ColumnStyle(Forms.SizeType.Percent,100));
            var ok=new Forms.Button {Text="确定"};
            var cancel=new Forms.Button {Text="取消",DialogResult=Forms.DialogResult.Cancel};
            ok.Click+=delegate { if (editor.Save(form,quick.Checked,showClose.Checked,ctrlClick.Checked)) form.Close(); };
            var reset=new Forms.Button { Text="重置默认值",AutoSize=true,Anchor=Forms.AnchorStyles.Right };
            reset.Click+=delegate { editor.Reset(); pc.Checked=true; showClose.Checked=ctrlClick.Checked=true; };
            buttons.Controls.AddRange(new Forms.Control[]{ok,cancel,reset});
            var hint=new Forms.Label {Visible=false,AutoSize=true};
            content.Controls.AddRange(new Forms.Control[]{new Forms.Label {Text="新标签打开位置",AutoSize=true},pc,quick,showClose,ctrlClick,
                new Forms.Label {Text="快捷键",AutoSize=true},hint});
            for (int i=0;i<ShortcutEditor.Names.Length;i++) {
                int index=i;
                var row=new Forms.FlowLayoutPanel {AutoSize=true};
                var binding=new Forms.Button {Text=editor.Caption(i),Width=150};
                binding.Click+=delegate {editor.Begin(index);};
                binding.Leave+=delegate {if(editor.Recording==index) editor.CancelCapture();};
                editor.Changed+=delegate {binding.Text=editor.Caption(index);};
                row.Controls.AddRange(new Forms.Control[]{new Forms.Label {Text=ShortcutEditor.Names[i]},binding});
                content.Controls.Add(row);
            }
            editor.Changed+=delegate { hint.Text=editor.HintText; hint.Visible=editor.HintText!=null; };
            content.Controls.Add(buttons);
            form.Controls.Add(content);
            form.AcceptButton=ok; form.CancelButton=cancel;
            Forms.Application.AddMessageFilter(editor);
            try { form.ShowDialog(); } finally { Forms.Application.RemoveMessageFilter(editor); }
        }
    }
    [STAThread]
    static int Main(string[] args) {
        try {
            long owner;
            if (args.Length != 1 || !long.TryParse(args[0],out owner)) return 1;
            if (ApiInformation.IsApiContractPresent("Windows.Foundation.UniversalApiContract",8)) RunIsland(new IntPtr(owner));
            else RunClassic();
            return 0;
        } catch (Exception ex) {
            Forms.MessageBox.Show(ex.Message,"EZFolder 设置",Forms.MessageBoxButtons.OK,Forms.MessageBoxIcon.Error);
            return 1;
        }
    }
}
