using System;
using System.Runtime.InteropServices;
using Microsoft.Win32;
using Forms = System.Windows.Forms;

sealed class ShortcutEditor : Forms.IMessageFilter {
    const string RegistryPath = @"Software\EZFolder";
    const int Ctrl=0x100, Shift=0x200, Alt=0x400;
    static readonly int[] Defaults = {Ctrl|'T',Ctrl|'W',Ctrl|9,Ctrl|Shift|9,Ctrl|Shift|'T'};
    public static readonly string[] Names = {"新建标签页","关闭当前标签","下一个标签","上一个标签","恢复上个关闭的标签页"};
    readonly int[] shortcuts = (int[])Defaults.Clone();
    public bool QuickAccess;
    public bool ShowClose=true, CtrlClick=true;
    public int Recording { get; private set; }
    public string HintText { get; private set; }
    public event Action Changed;
    int swallowUp;

    public ShortcutEditor() {
        Recording=-1;
        using (var key=Registry.CurrentUser.OpenSubKey(RegistryPath)) {
            if (key==null) return;
            var data=key.GetValue("PreferencesV1") as byte[];
            if (data!=null && (data.Length==20 || data.Length==24 || data.Length==28) && (uint)BitConverter.ToInt32(data,0)<=1) {
                QuickAccess=BitConverter.ToInt32(data,0)==1;
                Buffer.BlockCopy(data,4,shortcuts,0,Math.Min(data.Length-4,shortcuts.Length*4));
                if (data.Length==28) {
                    int options=BitConverter.ToInt32(data,24);
                    ShowClose=(options&1)!=0;
                    CtrlClick=(options&2)!=0;
                }
                if (data.Length==20 && Array.IndexOf(shortcuts,shortcuts[4],0,4)>=0) shortcuts[4]=0;
            } else QuickAccess=object.Equals(key.GetValue("NewTabLocation"),1);
        }
    }
    public bool Save(Forms.Form owner,bool quick,bool showClose,bool ctrlClick) {
        var data=new byte[(shortcuts.Length+2)*4];
        data[0]=(byte)(quick ? 1 : 0);
        Buffer.BlockCopy(shortcuts,0,data,4,shortcuts.Length*4);
        data[data.Length-4]=(byte)((showClose ? 1 : 0)|(ctrlClick ? 2 : 0));
        try {
            using (var key=Registry.CurrentUser.CreateSubKey(RegistryPath))
                key.SetValue("PreferencesV1",data,RegistryValueKind.Binary);
            return true;
        } catch (Exception ex) { Forms.MessageBox.Show(owner,ex.Message,owner.Text); return false; }
    }
    public string Caption(int i) {
        if (Recording==i) return "请按快捷键…";
        int chord=shortcuts[i], vk=chord&255;
        if (chord==0) return "-";
        string key=vk>='0' && vk<='9' ? ((char)vk).ToString() : ((Forms.Keys)vk).ToString();
        return ((chord&Ctrl)!=0 ? "Ctrl + " : "") + ((chord&Alt)!=0 ? "Alt + " : "") +
               ((chord&Shift)!=0 ? "Shift + " : "") + key;
    }
    void Update(int recording,string hint=null) {
        Recording=recording; HintText=hint;
        if (Changed!=null) Changed();
    }
    public void Begin(int i) { Update(i); }
    public void CancelCapture() { if (Recording>=0) Update(-1); }
    public void Reset() {
        Defaults.CopyTo(shortcuts,0);
        QuickAccess=false;
        Update(-1);
    }
    [DllImport("user32")] static extern short GetKeyState(int vk);
    public bool PreFilterMessage(ref Forms.Message m) {
        if (m.Msg!=0x100 && m.Msg!=0x101 && m.Msg!=0x104 && m.Msg!=0x105) return false;
        int vk=m.WParam.ToInt32();
        if (m.Msg==0x101 || m.Msg==0x105) {
            if (swallowUp!=0 && vk==swallowUp) { swallowUp=0; return true; }
            return Recording>=0;
        }
        if (swallowUp!=0 && vk==swallowUp && (m.LParam.ToInt64() & (1L<<30))!=0) return true;
        if (Recording<0) return false;
        if (vk==16 || vk==17 || vk==18 || (vk>=160 && vk<=165) || vk==91 || vk==92) return true;
        var mods=Forms.Control.ModifierKeys;
        int mask=((mods&Forms.Keys.Control)!=0 ? Ctrl : 0) | ((mods&Forms.Keys.Alt)!=0 ? Alt : 0) |
                 ((mods&Forms.Keys.Shift)!=0 ? Shift : 0);
        bool windowsKey=GetKeyState(91)<0 || GetKeyState(92)<0;
        if (mask==0 && !windowsKey && (vk==27 || vk==46 || vk==8)) {
            shortcuts[Recording]=0;
            swallowUp=vk;
            Update(-1);
            return true;
        }
        bool function=vk>=112 && vk<=135;
        bool supported=function || (vk>='A' && vk<='Z') || (vk>='0' && vk<='9') || vk==9 ||
            (vk>=32 && vk<=40) || vk==45 || vk==46 || vk==8 || (vk>=96 && vk<=111);
        if (!supported || (!function && mask==0) || windowsKey ||
            (vk==9 && (mask==Shift || (mask&Alt)!=0)) || (vk==115 && (mask&Alt)!=0) ||
            (vk==46 && (mask&(Ctrl|Alt))==(Ctrl|Alt))) {
            Update(Recording,"请使用修饰键组合或功能键，避开系统保留键。");
            return true;
        }
        int chord=mask|vk, other=Array.IndexOf(shortcuts,chord);
        if (other>=0 && other!=Recording) {
            Update(Recording,"已用于「"+Names[other]+"」，请换一个组合。");
            return true;
        }
        shortcuts[Recording]=chord;
        swallowUp=vk;
        Update(-1);
        return true;
    }
}
