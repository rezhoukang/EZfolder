using System;
using System.Runtime.InteropServices;
using Microsoft.Win32;
using Forms = System.Windows.Forms;

sealed class ShortcutEditor : Forms.IMessageFilter {
    const string RegistryPath = @"Software\EZFolder";
    const int Ctrl=0x100, Shift=0x200, Alt=0x400;
    static readonly int[] Defaults = {Ctrl|'T',Ctrl|'W',Ctrl|9,Ctrl|Shift|9};
    public static readonly string[] Names = {"新建标签页","关闭当前标签","下一个标签","上一个标签"};
    public const string Hint = "关闭标签页在当前资源管理器窗口内生效，其他快捷键在标签栏内生效。点击按键组合即可修改。";
    readonly int[] shortcuts = (int[])Defaults.Clone();
    public bool QuickAccess;
    public int Recording { get; private set; }
    public string HintText { get; private set; }
    public event Action Changed;
    int swallowUp;

    public ShortcutEditor() {
        Recording=-1;
        HintText=Hint;
        using (var key=Registry.CurrentUser.OpenSubKey(RegistryPath)) {
            if (key==null) return;
            var data=key.GetValue("PreferencesV1") as byte[];
            if (data!=null && data.Length==20 && (uint)BitConverter.ToInt32(data,0)<=1) {
                QuickAccess=BitConverter.ToInt32(data,0)==1;
                Buffer.BlockCopy(data,4,shortcuts,0,16);
            } else QuickAccess=object.Equals(key.GetValue("NewTabLocation"),1);
        }
    }
    public bool Save(Forms.Form owner,bool quick) {
        var data=new byte[20];
        data[0]=(byte)(quick ? 1 : 0);
        Buffer.BlockCopy(shortcuts,0,data,4,16);
        try {
            using (var key=Registry.CurrentUser.CreateSubKey(RegistryPath))
                key.SetValue("PreferencesV1",data,RegistryValueKind.Binary);
            return true;
        } catch (Exception ex) { Forms.MessageBox.Show(owner,ex.Message,owner.Text); return false; }
    }
    public string Caption(int i) {
        if (Recording==i) return "请按快捷键…";
        int chord=shortcuts[i], vk=chord&255;
        string key=vk>='0' && vk<='9' ? ((char)vk).ToString() : ((Forms.Keys)vk).ToString();
        return ((chord&Ctrl)!=0 ? "Ctrl + " : "") + ((chord&Alt)!=0 ? "Alt + " : "") +
               ((chord&Shift)!=0 ? "Shift + " : "") + key;
    }
    void Update(int recording,string hint) {
        Recording=recording; HintText=hint;
        if (Changed!=null) Changed();
    }
    public void Begin(int i) { Update(i,"按下新组合，或按 Esc 取消录入。"); }
    public void CancelCapture() { if (Recording>=0) Update(-1,Hint); }
    public void Reset() {
        Defaults.CopyTo(shortcuts,0);
        QuickAccess=false;
        Update(-1,Hint);
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
        if (vk==27) { swallowUp=vk; CancelCapture(); return true; }
        if (vk==16 || vk==17 || vk==18 || (vk>=160 && vk<=165) || vk==91 || vk==92) return true;
        var mods=Forms.Control.ModifierKeys;
        int mask=((mods&Forms.Keys.Control)!=0 ? Ctrl : 0) | ((mods&Forms.Keys.Alt)!=0 ? Alt : 0) |
                 ((mods&Forms.Keys.Shift)!=0 ? Shift : 0);
        bool function=vk>=112 && vk<=135;
        bool supported=function || (vk>='A' && vk<='Z') || (vk>='0' && vk<='9') || vk==9 ||
            (vk>=32 && vk<=40) || vk==45 || vk==46 || vk==8 || (vk>=96 && vk<=111);
        if (!supported || (!function && mask==0) || GetKeyState(91)<0 || GetKeyState(92)<0 ||
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
        Update(-1,Hint);
        return true;
    }
}
