using System;
using System.Diagnostics;
using System.Drawing;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace DynamicIsland
{
    internal static class Program
    {
        [STAThread]
        private static void Main(string[] args)
        {
            // Declare Per-Monitor V2 DPI awareness so all coordinates are in
            // physical pixels. Without this the helper runs DPI-unaware, Windows
            // virtualises its coordinates to 96 DPI, and SHAppBarMessage reserves
            // fewer physical pixels than the Electron window actually occupies,
            // leaving a thin uncovered strip at the bottom of the bar.
            NativeMethods.SetProcessDpiAwarenessContext(
                NativeMethods.DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

            int heightDip = 32; // logical (DIP) height passed from Electron
            int parentPid = 0;

            foreach (string arg in args)
            {
                if (arg.StartsWith("/height:", StringComparison.OrdinalIgnoreCase))
                {
                    int.TryParse(arg.Substring("/height:".Length), out heightDip);
                }
                else if (arg.StartsWith("/parentPid:", StringComparison.OrdinalIgnoreCase))
                {
                    int.TryParse(arg.Substring("/parentPid:".Length), out parentPid);
                }
            }

            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new AppBarContext(heightDip, parentPid));
        }
    }

    internal sealed class AppBarContext : ApplicationContext
    {
        private readonly AppBarForm _form;
        private readonly Timer _parentWatchTimer;

        public AppBarContext(int height, int parentPid)
        {
            _form = new AppBarForm(height);
            _form.FormClosed += delegate { ExitThread(); };

            _parentWatchTimer = new Timer { Interval = 1000 };
            _parentWatchTimer.Tick += delegate
            {
                if (parentPid <= 0) return;
                try
                {
                    Process parent = Process.GetProcessById(parentPid);
                    if (!parent.HasExited) return;
                }
                catch
                {
                    // Parent disappeared.
                }

                _parentWatchTimer.Stop();
                _form.Close();
            };
            _parentWatchTimer.Start();

            _form.Show();
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing)
            {
                _parentWatchTimer.Dispose();
                _form.Dispose();
            }

            base.Dispose(disposing);
        }
    }

    internal sealed class AppBarForm : Form
    {
        private const int WM_ACTIVATE = 0x0006;
        private const int WM_WINDOWPOSCHANGED = 0x0047;
        private const int WM_DISPLAYCHANGE = 0x007E;
        private const int WM_USER = 0x0400;
        private const int ABE_TOP = 1;
        private const int ABM_NEW = 0x00000000;
        private const int ABM_REMOVE = 0x00000001;
        private const int ABM_QUERYPOS = 0x00000002;
        private const int ABM_SETPOS = 0x00000003;
        private const int ABM_ACTIVATE = 0x00000006;
        private const int ABM_WINDOWPOSCHANGED = 0x00000009;
        private const int ABN_POSCHANGED = 0x00000001;
        private const int ABN_FULLSCREENAPP = 0x00000002;

        private readonly int _callbackMessage = WM_USER + 1;
        private readonly int _height;
        private bool _registered;

        public AppBarForm(int heightDip)
        {
            ShowInTaskbar = false;
            FormBorderStyle = FormBorderStyle.None;
            StartPosition = FormStartPosition.Manual;
            BackColor = Color.Magenta;
            TransparencyKey = Color.Magenta;
            Opacity = 0;
            TopMost = false;

            // Convert the logical (DIP) height Electron passed us into physical
            // pixels now that we are PerMonitorV2 aware.
            // GetDpiForMonitor works without a window handle, so we can safely
            // call it in the constructor before CreateHandle() is invoked.
            IntPtr hMon = NativeMethods.MonitorFromPoint(
                new System.Drawing.Point(0, 0),
                NativeMethods.MONITOR_DEFAULTTOPRIMARY);
            uint dpiX, dpiY;
            if (NativeMethods.GetDpiForMonitor(hMon,
                NativeMethods.MDT_EFFECTIVE_DPI, out dpiX, out dpiY) != 0)
            {
                dpiX = 96; // fall back to 100 % if the call fails
            }
            _height = Math.Max(1, (int)Math.Round(heightDip * dpiX / 96.0));

            Bounds = new Rectangle(0, 0, Screen.PrimaryScreen.Bounds.Width, _height);
        }

        protected override CreateParams CreateParams
        {
            get
            {
                CreateParams cp = base.CreateParams;
                cp.ExStyle |= 0x00000080; // WS_EX_TOOLWINDOW
                cp.ExStyle |= 0x08000000; // WS_EX_NOACTIVATE
                cp.ExStyle |= 0x00000020; // WS_EX_TRANSPARENT
                return cp;
            }
        }

        protected override bool ShowWithoutActivation
        {
            get { return true; }
        }

        protected override void OnHandleCreated(EventArgs e)
        {
            base.OnHandleCreated(e);
            RegisterBar();
            ApplyBarPosition();
        }

        protected override void OnFormClosed(FormClosedEventArgs e)
        {
            UnregisterBar();
            base.OnFormClosed(e);
        }

        protected override void WndProc(ref Message m)
        {
            if (m.Msg == _callbackMessage)
            {
                int notification = m.WParam.ToInt32();
                if (notification == ABN_POSCHANGED)
                {
                    ApplyBarPosition();
                }
                else if (notification == ABN_FULLSCREENAPP)
                {
                    // lParam is non-zero when a window entered fullscreen,
                    // zero when it exited. We also filter out system overlays
                    // (Task View, Alt+Tab, Start, desktop) that Windows treats
                    // as "fullscreen" but where the navbar should stay visible.
                    bool entering = m.LParam.ToInt64() != 0;
                    bool suppress = entering && IsSystemOverlay();
                    Console.WriteLine("FULLSCREEN|{0}", (entering && !suppress) ? 1 : 0);
                    Console.Out.Flush();
                }
            }
            else if (m.Msg == WM_ACTIVATE)
            {
                NotifyActivate();
            }
            else if (m.Msg == WM_WINDOWPOSCHANGED)
            {
                NotifyWindowPosChanged();
            }
            else if (m.Msg == WM_DISPLAYCHANGE)
            {
                ApplyBarPosition();
            }

            base.WndProc(ref m);
        }

        private void RegisterBar()
        {
            if (_registered) return;

            APPBARDATA data = NewData();
            data.uCallbackMessage = (uint)_callbackMessage;
            SHAppBarMessage(ABM_NEW, ref data);
            _registered = true;
        }

        private void UnregisterBar()
        {
            if (!_registered || !IsHandleCreated) return;

            APPBARDATA data = NewData();
            SHAppBarMessage(ABM_REMOVE, ref data);
            _registered = false;
        }

        private void NotifyActivate()
        {
            if (!_registered) return;

            APPBARDATA data = NewData();
            SHAppBarMessage(ABM_ACTIVATE, ref data);
        }

        private void NotifyWindowPosChanged()
        {
            if (!_registered) return;

            APPBARDATA data = NewData();
            SHAppBarMessage(ABM_WINDOWPOSCHANGED, ref data);
        }

        private void ApplyBarPosition()
        {
            if (!_registered) return;

            Rectangle screen = Screen.PrimaryScreen.Bounds;

            APPBARDATA data = NewData();
            data.uEdge = ABE_TOP;
            data.rc.left = screen.Left;
            data.rc.top = screen.Top;
            data.rc.right = screen.Right;
            data.rc.bottom = screen.Top + _height;

            SHAppBarMessage(ABM_QUERYPOS, ref data);
            data.rc.bottom = data.rc.top + _height;
            SHAppBarMessage(ABM_SETPOS, ref data);

            Bounds = Rectangle.FromLTRB(data.rc.left, data.rc.top, data.rc.right, data.rc.bottom);
            Console.WriteLine("{0}|{1}|{2}|{3}", data.rc.left, data.rc.top, data.rc.right, data.rc.bottom);
        }

        private APPBARDATA NewData()
        {
            return new APPBARDATA
            {
                cbSize = (uint)Marshal.SizeOf(typeof(APPBARDATA)),
                hWnd = Handle,
            };
        }

        /// <summary>
        /// Returns true if the current foreground window is a Windows system
        /// overlay that should NOT cause the navbar to hide.
        /// </summary>
        private static bool IsSystemOverlay()
        {
            IntPtr hwnd = NativeMethods.GetForegroundWindow();
            if (hwnd == IntPtr.Zero) return true; // no foreground — play it safe

            var sb = new System.Text.StringBuilder(256);
            NativeMethods.GetClassName(hwnd, sb, sb.Capacity);
            string cls = sb.ToString();

            // Class names known to trigger ABN_FULLSCREENAPP even though they
            // are system UI that the user still wants the navbar visible for.
            string[] systemClasses =
            {
                "MultitaskingViewFrame",       // Win+Tab Task View
                "TaskSwitcherWnd",             // Alt+Tab
                "TaskSwitcherOverlayWnd",      // Alt+Tab overlay
                "Windows.UI.Core.CoreWindow",  // Windows 10/11 system UI
                "XamlExplorerHostIslandWindow",// Windows 11 Start / widgets
                "Shell_TrayWnd",               // Shell taskbar
                "WorkerW",                     // Desktop worker window
                "Progman",                     // Program Manager (desktop)
            };

            foreach (string c in systemClasses)
            {
                if (string.Equals(cls, c, StringComparison.OrdinalIgnoreCase))
                    return true;
            }
            return false;
        }

        [DllImport("shell32.dll")]
        private static extern uint SHAppBarMessage(uint dwMessage, ref APPBARDATA pData);

        [StructLayout(LayoutKind.Sequential)]
        private struct APPBARDATA
        {
            public uint cbSize;
            public IntPtr hWnd;
            public uint uCallbackMessage;
            public uint uEdge;
            public RECT rc;
            public IntPtr lParam;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct RECT
        {
            public int left;
            public int top;
            public int right;
            public int bottom;
        }
    }

    /// <summary>P/Invoke declarations shared across the program.</summary>
    internal static class NativeMethods
    {
        // PerMonitorV2 DPI awareness context handle (sentinel value -4).
        public static readonly IntPtr DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 =
            new IntPtr(-4);

        public const uint MONITOR_DEFAULTTOPRIMARY = 1;
        public const uint MDT_EFFECTIVE_DPI = 0;

        [DllImport("user32.dll", SetLastError = true)]
        public static extern bool SetProcessDpiAwarenessContext(IntPtr value);

        [DllImport("user32.dll", SetLastError = true)]
        public static extern uint GetDpiForWindow(IntPtr hWnd);

        [DllImport("user32.dll", SetLastError = true)]
        public static extern IntPtr MonitorFromPoint(
            System.Drawing.Point pt, uint dwFlags);

        // Returns S_OK (0) on success.
        [DllImport("shcore.dll", SetLastError = false)]
        public static extern int GetDpiForMonitor(
            IntPtr hMonitor, uint dpiType,
            out uint dpiX, out uint dpiY);

        [DllImport("user32.dll", SetLastError = true)]
        public static extern IntPtr GetForegroundWindow();

        [DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Auto)]
        public static extern int GetClassName(
            IntPtr hWnd, System.Text.StringBuilder lpClassName, int nMaxCount);
    }
}
