using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.Linq;
using System.Text;
using System.Windows.Forms;
using System.IO;
using System.IO.Pipes;
using System.Runtime.InteropServices;

namespace client_ui
{
    public partial class Main : Form
    {
        public bool running;
        private bool examLayoutActive;
        private readonly ImageList programIcons = new ImageList();
        private readonly Dictionary<string, int> iconIndexByKey = new Dictionary<string, int>();
        private const string DefaultProgramIconKey = "__default_program_icon__";
        private static readonly Size CompactWindowSize = new Size(460, 230);
        private static readonly Size ExpandedWindowSize = new Size(760, 754);

        [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
        private static extern uint ExtractIconEx(
            string lpszFile,
            int nIconIndex,
            IntPtr[] phiconLarge,
            IntPtr[] phiconSmall,
            uint nIcons
        );

        [DllImport("user32.dll", SetLastError = true)]
        private static extern bool DestroyIcon(IntPtr hIcon);

        public Main()
        {
            running = true;
            InitializeComponent();
            warningIcon.Image = SystemIcons.Warning.ToBitmap();

            programIcons.ColorDepth = ColorDepth.Depth32Bit;
            programIcons.ImageSize = new Size(20, 20);
            programIcons.Images.Add(DefaultProgramIconKey, SystemIcons.Application.ToBitmap());
            localAppsList.SmallImageList = programIcons;
            localAppsList.Columns.Clear();
            localAppsList.Columns.Add("Application", localAppsList.Width - 8);
            localAppsList.HeaderStyle = ColumnHeaderStyle.None;
            localAppsList.FullRowSelect = true;


            examLayoutActive = false;
            ApplyCompactLayout();
            SetLoadingState();
        }
        
        public void PipeConnectionStatusChanged(bool connected)
        {
            if (connected)
            {
                SetConnectedState();
                Console.WriteLine("Pipe status: connected");
            }
            else
            {
                SetLoadingState();
                Console.WriteLine("Pipe status: disconnected");
            }
        }

        private void Main_FormClosing(object sender, FormClosingEventArgs e)
        {

        }

        private void Main_FormClosed(object sender, FormClosedEventArgs e)
        {
            running = false;
            Program.OnUiClose();
        }

        private void button1_Click(object sender, EventArgs e)
        {
            examLayoutActive = true;
            ApplyExpandedLayout();
            button1.Visible = false;
            label1.Text = "Status: Preparing exam environment...";
            label1.ForeColor = Color.FromArgb(36, 99, 190);
            label1.Left = (this.ClientSize.Width - label1.Width) / 2;

            localAppsList.Items.Clear();
            warningLabel.Visible = false;
            warningIcon.Visible = false;
            warningDetailsLabel.Visible = false;
            consentLabel.Visible = false;
            consentCheckBox1.Visible = false;
            consentCheckBox2.Visible = false;
            consentCheckBox3.Visible = false;
            continueButton.Visible = false;
            consentCheckBox1.Checked = false;
            consentCheckBox2.Checked = false;
            consentCheckBox3.Checked = false;
            continueButton.Enabled = false;

            bool queued = Program.EnqueuePipeMessage(new StartExamMessage());
            if (queued)
            {
                Console.WriteLine("Start exam message queued");
            }
            else
            {
                Console.WriteLine("Failed to queue start exam message");
            }
        }

        public void ShowStartExamLocalPrograms(StartExamResultMessage message)
        {
            if (message == null)
                return;

            label1.Text = "Status: Review required before continue";
            label1.ForeColor = Color.FromArgb(180, 111, 0);
            label1.Left = (this.ClientSize.Width - label1.Width) / 2;

            warningLabel.Text = IsBlank(message.warning)
                ? "The applications listed below will not be available during the exam. If required, please install them globally in advance."
                : message.warning;
            warningLabel.Visible = true;
            warningIcon.Visible = true;
            warningDetailsLabel.Visible = true;

            localAppsList.Items.Clear();
            if (message.programs == null || message.programs.Count == 0)
            {
                localAppsList.Items.Add(new ListViewItem(
                    "(No local user-installed applications detected.)",
                    programIcons.Images.IndexOfKey(DefaultProgramIconKey)
                ));
                return;
            }

            foreach (var program in message.programs)
            {
                if (program == null || IsBlank(program.name))
                    continue;

                string line = program.name;
                if (!IsBlank(program.version))
                {
                    line += " (" + program.version + ")";
                }

                localAppsList.Items.Add(new ListViewItem(line, GetIconIndexForProgram(program)));
            }

            consentLabel.Visible = true;
            consentCheckBox1.Visible = true;
            consentCheckBox2.Visible = true;
            consentCheckBox3.Visible = true;
            continueButton.Visible = true;
            continueButton.Enabled = consentCheckBox1.Checked && consentCheckBox2.Checked && consentCheckBox3.Checked;
        }

        public void ShowInitExamResponse(InitExamResponseMessage message)
        {
            if (message == null)
                return;

            if (message.code == 0)
            {
                label1.Text = "Status: Exam environment initialized successfully";
                label1.ForeColor = Color.FromArgb(22, 130, 71);
                label1.Left = (this.ClientSize.Width - label1.Width) / 2;

                string instructions =
                    "To enter the exam, sign out from your current account and log in with:" + Environment.NewLine +
                    "Username: exam_user" + Environment.NewLine +
                    "Password: Password123" + Environment.NewLine + Environment.NewLine +
                    "The exam will start after you log in to that account." + Environment.NewLine +
                    "Make sure you save your work in the your_work folder on Desktop." + Environment.NewLine +
                    "To finish your exam, press on finish exam button and then log out from the exam session.";

                MessageBox.Show(
                    instructions,
                    "Exam Login Instructions",
                    MessageBoxButtons.OK,
                    MessageBoxIcon.Information
                );
                return;
            }

            label1.Text = "Status: Failed to initialize exam environment";
            label1.ForeColor = Color.FromArgb(161, 38, 38);
            label1.Left = (this.ClientSize.Width - label1.Width) / 2;

            string errorText = IsBlank(message.error) ? "Unknown error." : message.error;
            MessageBox.Show(
                "Failed to initialize exam environment." + Environment.NewLine +
                "Error code: " + message.code.ToString() + Environment.NewLine +
                "Details: " + errorText,
                "Exam Initialization Error",
                MessageBoxButtons.OK,
                MessageBoxIcon.Error
            );

            continueButton.Enabled = consentCheckBox1.Checked && consentCheckBox2.Checked && consentCheckBox3.Checked;
        }

        private void consentCheckBox_CheckedChanged(object sender, EventArgs e)
        {
            continueButton.Enabled = consentCheckBox1.Checked && consentCheckBox2.Checked && consentCheckBox3.Checked;
        }

        private void continueButton_Click(object sender, EventArgs e)
        {
            continueButton.Enabled = false;
            label1.Text = "Status: Sending confirmation and starting exam...";
            label1.ForeColor = Color.FromArgb(36, 99, 190);
            label1.Left = (this.ClientSize.Width - label1.Width) / 2;

            bool queued = Program.EnqueuePipeMessage(new ContinueToExamMessage
            {
                registered_name = Program.RegisteredStudentName,
                run_id = Program.RegisteredRunId
            });
            if (queued)
            {
                Console.WriteLine("Continue-to-exam message queued");
                return;
            }

            continueButton.Enabled = consentCheckBox1.Checked && consentCheckBox2.Checked && consentCheckBox3.Checked;
            MessageBox.Show(
                "Failed to send continue confirmation to exam core.",
                "Exam",
                MessageBoxButtons.OK,
                MessageBoxIcon.Warning
            );
        }

        private static bool IsBlank(string value)
        {
            return string.IsNullOrEmpty(value) || value.Trim().Length == 0;
        }

        private int GetIconIndexForProgram(InstalledProgramInfo program)
        {
            string key = BuildIconCacheKey(program);
            int existingIndex;
            if (iconIndexByKey.TryGetValue(key, out existingIndex))
                return existingIndex;

            Bitmap iconBitmap = ExtractProgramIcon(program);
            if (iconBitmap == null)
                iconBitmap = SystemIcons.Application.ToBitmap();

            programIcons.Images.Add(iconBitmap);
            int newIndex = programIcons.Images.Count - 1;
            iconIndexByKey[key] = newIndex;
            return newIndex;
        }

        private static string BuildIconCacheKey(InstalledProgramInfo program)
        {
            if (program == null)
                return DefaultProgramIconKey;

            string iconPath = program.display_icon ?? "";
            string programName = program.name ?? "";
            return iconPath + "|" + programName;
        }

        private static Bitmap ExtractProgramIcon(InstalledProgramInfo program)
        {
            if (program == null || IsBlank(program.display_icon))
                return null;

            string iconPath;
            int iconIndex;
            ParseDisplayIcon(program.display_icon, out iconPath, out iconIndex);
            if (IsBlank(iconPath) || !File.Exists(iconPath))
                return null;

            if (iconPath.EndsWith(".ico", StringComparison.OrdinalIgnoreCase))
            {
                try
                {
                    using (Icon ico = new Icon(iconPath))
                    {
                        return ico.ToBitmap();
                    }
                }
                catch
                {
                    return null;
                }
            }

            IntPtr[] smallIcons = new IntPtr[1];
            try
            {
                uint extracted = ExtractIconEx(iconPath, iconIndex, null, smallIcons, 1);
                if (extracted > 0 && smallIcons[0] != IntPtr.Zero)
                {
                    Icon icon = null;
                    try
                    {
                        icon = (Icon)Icon.FromHandle(smallIcons[0]).Clone();
                        return icon.ToBitmap();
                    }
                    finally
                    {
                        if (icon != null)
                            icon.Dispose();
                    }
                }
            }
            catch
            {
            }
            finally
            {
                if (smallIcons[0] != IntPtr.Zero)
                    DestroyIcon(smallIcons[0]);
            }

            return null;
        }

        private static void ParseDisplayIcon(string rawDisplayIcon, out string iconPath, out int iconIndex)
        {
            iconPath = null;
            iconIndex = 0;

            if (IsBlank(rawDisplayIcon))
                return;

            string value = Environment.ExpandEnvironmentVariables(rawDisplayIcon.Trim());
            int commaPos = value.LastIndexOf(',');
            if (commaPos > 0)
            {
                string potentialIndex = value.Substring(commaPos + 1).Trim();
                int parsedIndex;
                if (int.TryParse(potentialIndex, out parsedIndex))
                {
                    iconIndex = parsedIndex;
                    value = value.Substring(0, commaPos);
                }
            }

            iconPath = value.Trim().Trim('"');
        }

        private void ApplyCompactLayout()
        {
            this.ClientSize = CompactWindowSize;

            const int sideMargin = 12;
            int panelWidth = this.ClientSize.Width - (2 * sideMargin);
            mainFrame.Location = new Point(sideMargin, 85);
            mainFrame.Size = new Size(panelWidth, this.ClientSize.Height - 98);

            label1.Left = (this.ClientSize.Width - label1.Width) / 2;

            button1.Top = 50;
            button1.Left = (mainFrame.Width - button1.Width) / 2;

            UpdateProgramColumnWidth();
        }

        private void ApplyExpandedLayout()
        {
            this.ClientSize = ExpandedWindowSize;

            const int sideMargin = 12;
            const int left = 20;
            const int right = 20;
            int panelWidth = this.ClientSize.Width - (2 * sideMargin);
            int usableWidth = panelWidth - left - right;

            mainFrame.Location = new Point(sideMargin, 85);
            mainFrame.Size = new Size(panelWidth, this.ClientSize.Height - 98);

            label1.Left = (this.ClientSize.Width - label1.Width) / 2;

            button1.Top = 33;
            button1.Left = (mainFrame.Width - button1.Width) / 2;

            warningIcon.Location = new Point(left, 108);

            warningLabel.Location = new Point(warningIcon.Right + 8, 100);
            warningLabel.Size = new Size(mainFrame.Width - warningLabel.Left - right, 48);

            localAppsList.Location = new Point(left, 151);
            localAppsList.Size = new Size(usableWidth, 200);

            warningDetailsLabel.Location = new Point(left, localAppsList.Bottom + 4);
            warningDetailsLabel.Size = new Size(usableWidth, 100);

            consentLabel.Location = new Point(16, warningDetailsLabel.Bottom + 10);
            consentCheckBox1.Location = new Point(left, consentLabel.Bottom + 2);
            consentCheckBox2.Location = new Point(left, consentCheckBox1.Bottom + 2);
            consentCheckBox3.Location = new Point(left, consentCheckBox2.Bottom + 2);

            continueButton.Top = consentCheckBox3.Bottom + 4;
            continueButton.Left = (mainFrame.Width - continueButton.Width) / 2;

            UpdateProgramColumnWidth();
        }

        private void UpdateProgramColumnWidth()
        {
            if (localAppsList.Columns.Count > 0)
            {
                localAppsList.Columns[0].Width = localAppsList.Width - 8;
            }
        }

        private void SetLoadingState()
        {
            examLayoutActive = false;
            ApplyCompactLayout();
            ResetExamContentVisibility();
            button1.Visible = true;

            mainFrame.Visible = false;
            label1.Text = "Status: Loading exam service...";
            label1.ForeColor = Color.FromArgb(180, 111, 0);
            label1.Left = (this.ClientSize.Width - label1.Width) / 2;
        }

        private void SetConnectedState()
        {
            if (!examLayoutActive)
                ApplyCompactLayout();
            if (!examLayoutActive)
            {
                ResetExamContentVisibility();
                button1.Visible = true;
            }

            mainFrame.Visible = true;
            label1.Text = "Status: Connected";
            label1.ForeColor = Color.FromArgb(22, 130, 71);
            label1.Left = (this.ClientSize.Width - label1.Width) / 2;
        }

        private void ResetExamContentVisibility()
        {
            warningLabel.Visible = false;
            warningIcon.Visible = false;
            warningDetailsLabel.Visible = false;
            consentLabel.Visible = false;
            consentCheckBox1.Visible = false;
            consentCheckBox2.Visible = false;
            consentCheckBox3.Visible = false;
            continueButton.Visible = false;
        }
    }
}
