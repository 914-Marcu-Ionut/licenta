using System;
using System.Drawing;
using System.Threading;
using System.Windows.Forms;

namespace client_ui
{
    public class JoinForm : Form
    {
        public bool IsTestMode { get; private set; }
        public string RunId { get; private set; }
        public string StudentId { get; private set; }
        public string StudentName { get; private set; }

        private Label titleLabel;
        private Label runIdLabel;
        private TextBox runIdBox;
        private Button joinButton;

        private Label examInfoLabel;
        private Button testExamButton;
        private Button realExamButton;

        private Label nameLabel;
        private TextBox nameBox;
        private Button registerButton;

        private Label waitingLabel;
        private System.Windows.Forms.Timer pollTimer;

        public JoinForm()
        {
            InitForm();
            CreateControls();
            ShowStep1();
        }

        private void InitForm()
        {
            this.Text = "Exam Client";
            this.ClientSize = new Size(440, 200);
            this.FormBorderStyle = FormBorderStyle.FixedSingle;
            this.MaximizeBox = false;
            this.StartPosition = FormStartPosition.CenterScreen;
            this.BackColor = Color.FromArgb(245, 247, 252);
        }

        private void CreateControls()
        {
            Font titleFont = new Font("Segoe UI", 14F, FontStyle.Bold);
            Font labelFont = new Font("Segoe UI", 10F, FontStyle.Bold);
            Font inputFont = new Font("Segoe UI", 11F);
            Font btnFont = new Font("Segoe UI", 10F, FontStyle.Bold);
            Color blue = Color.FromArgb(47, 128, 237);
            Color dark = Color.FromArgb(35, 47, 62);

            titleLabel = new Label
            {
                Text = "Exam Client",
                Font = titleFont,
                ForeColor = dark,
                AutoSize = true,
                Location = new Point(20, 15)
            };

            runIdLabel = new Label
            {
                Text = "Enter Exam Run ID:",
                Font = labelFont,
                ForeColor = dark,
                AutoSize = true,
                Location = new Point(20, 60)
            };

            runIdBox = new TextBox
            {
                Font = inputFont,
                Location = new Point(20, 88),
                Size = new Size(280, 30)
            };

            joinButton = new Button
            {
                Text = "Join",
                Font = btnFont,
                ForeColor = Color.White,
                BackColor = blue,
                FlatStyle = FlatStyle.Flat,
                Size = new Size(100, 33),
                Location = new Point(310, 86)
            };
            joinButton.Click += joinButton_Click;

            examInfoLabel = new Label
            {
                Font = new Font("Segoe UI", 10F),
                ForeColor = dark,
                AutoSize = false,
                Size = new Size(400, 30),
                Location = new Point(20, 60),
                Visible = false
            };

            testExamButton = new Button
            {
                Text = "Test Exam",
                Font = btnFont,
                ForeColor = Color.White,
                BackColor = Color.FromArgb(100, 100, 100),
                FlatStyle = FlatStyle.Flat,
                Size = new Size(180, 44),
                Location = new Point(20, 100),
                Visible = false
            };
            testExamButton.Click += testExamButton_Click;

            realExamButton = new Button
            {
                Text = "Real Exam",
                Font = btnFont,
                ForeColor = Color.White,
                BackColor = blue,
                FlatStyle = FlatStyle.Flat,
                Size = new Size(180, 44),
                Location = new Point(230, 100),
                Visible = false
            };
            realExamButton.Click += realExamButton_Click;

            nameLabel = new Label
            {
                Text = "Your name:",
                Font = labelFont,
                ForeColor = dark,
                AutoSize = true,
                Location = new Point(20, 60),
                Visible = false
            };

            nameBox = new TextBox
            {
                Font = inputFont,
                Location = new Point(20, 88),
                Size = new Size(280, 30),
                Visible = false
            };

            registerButton = new Button
            {
                Text = "Register",
                Font = btnFont,
                ForeColor = Color.White,
                BackColor = blue,
                FlatStyle = FlatStyle.Flat,
                Size = new Size(100, 33),
                Location = new Point(310, 86),
                Visible = false
            };
            registerButton.Click += registerButton_Click;

            waitingLabel = new Label
            {
                Font = new Font("Segoe UI", 11F, FontStyle.Bold),
                ForeColor = Color.FromArgb(36, 99, 190),
                AutoSize = true,
                Location = new Point(20, 70),
                Visible = false
            };

            this.Controls.Add(titleLabel);
            this.Controls.Add(runIdLabel);
            this.Controls.Add(runIdBox);
            this.Controls.Add(joinButton);
            this.Controls.Add(examInfoLabel);
            this.Controls.Add(testExamButton);
            this.Controls.Add(realExamButton);
            this.Controls.Add(nameLabel);
            this.Controls.Add(nameBox);
            this.Controls.Add(registerButton);
            this.Controls.Add(waitingLabel);

            pollTimer = new System.Windows.Forms.Timer();
            pollTimer.Interval = 3000;
            pollTimer.Tick += pollTimer_Tick;
        }

        private void ShowStep1()
        {
            runIdLabel.Visible = true;
            runIdBox.Visible = true;
            joinButton.Visible = true;
            examInfoLabel.Visible = false;
            testExamButton.Visible = false;
            realExamButton.Visible = false;
            nameLabel.Visible = false;
            nameBox.Visible = false;
            registerButton.Visible = false;
            waitingLabel.Visible = false;
            this.ClientSize = new Size(440, 140);
        }

        private void ShowStep2(ExamRunResponse run)
        {
            runIdLabel.Visible = false;
            runIdBox.Visible = false;
            joinButton.Visible = false;

            examInfoLabel.Text = "Exam: " + run.exam_name + "   (Run: " + run.id + ", " + run.status + ")";
            examInfoLabel.Visible = true;

            if (run.status == "finished")
            {
                testExamButton.Visible = false;
                realExamButton.Visible = false;
                waitingLabel.Text = "This exam has already finished.";
                waitingLabel.ForeColor = Color.FromArgb(161, 38, 38);
                waitingLabel.Visible = true;
                this.ClientSize = new Size(440, 160);
                return;
            }

            testExamButton.Visible = true;
            realExamButton.Visible = true;
            this.ClientSize = new Size(440, 160);
        }

        private void ShowStep3()
        {
            examInfoLabel.Visible = false;
            testExamButton.Visible = false;
            realExamButton.Visible = false;

            nameLabel.Visible = true;
            nameBox.Visible = true;
            registerButton.Visible = true;
            nameBox.Focus();
            this.ClientSize = new Size(440, 140);
        }

        private void ShowStep4(string name)
        {
            nameLabel.Visible = false;
            nameBox.Visible = false;
            registerButton.Visible = false;

            waitingLabel.Text = "Registered as: " + name + "\nWaiting for exam to start...";
            waitingLabel.Visible = true;
            this.ClientSize = new Size(440, 140);
        }

        private void joinButton_Click(object sender, EventArgs e)
        {
            string id = runIdBox.Text.Trim();
            if (string.IsNullOrEmpty(id))
            {
                MessageBox.Show("Please enter a Run ID.", "Exam Client",
                    MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }

            joinButton.Enabled = false;
            joinButton.Text = "...";

            ThreadPool.QueueUserWorkItem(delegate
            {
                try
                {
                    ExamRunResponse run = BackendClient.GetExamRun(id);
                    BeginInvoke((MethodInvoker)delegate
                    {
                        RunId = run.id;
                        joinButton.Enabled = true;
                        joinButton.Text = "Join";
                        ShowStep2(run);
                    });
                }
                catch (Exception ex)
                {
                    BeginInvoke((MethodInvoker)delegate
                    {
                        joinButton.Enabled = true;
                        joinButton.Text = "Join";
                        MessageBox.Show("Run not found or server error.\n" + ex.Message,
                            "Exam Client", MessageBoxButtons.OK, MessageBoxIcon.Error);
                    });
                }
            });
        }

        private void testExamButton_Click(object sender, EventArgs e)
        {
            IsTestMode = true;
            this.DialogResult = DialogResult.OK;
            this.Close();
        }

        private void realExamButton_Click(object sender, EventArgs e)
        {
            IsTestMode = false;
            titleLabel.Text = "Register for Exam";
            ShowStep3();
        }

        private void registerButton_Click(object sender, EventArgs e)
        {
            string name = nameBox.Text.Trim();
            if (string.IsNullOrEmpty(name))
            {
                MessageBox.Show("Please enter your name.", "Exam Client",
                    MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }

            registerButton.Enabled = false;
            registerButton.Text = "...";

            ThreadPool.QueueUserWorkItem(delegate
            {
                try
                {
                    RegisteredStudentResponse student = BackendClient.RegisterStudent(RunId, name);
                    BeginInvoke((MethodInvoker)delegate
                    {
                        StudentId = student.id;
                        StudentName = student.name;
                        titleLabel.Text = "Status: Waiting";
                        ShowStep4(student.name);
                        pollTimer.Start();
                    });
                }
                catch (Exception ex)
                {
                    BeginInvoke((MethodInvoker)delegate
                    {
                        registerButton.Enabled = true;
                        registerButton.Text = "Register";
                        MessageBox.Show("Registration failed.\n" + ex.Message,
                            "Exam Client", MessageBoxButtons.OK, MessageBoxIcon.Error);
                    });
                }
            });
        }

        private void pollTimer_Tick(object sender, EventArgs e)
        {
            pollTimer.Stop();

            ThreadPool.QueueUserWorkItem(delegate
            {
                try
                {
                    ExamRunResponse run = BackendClient.GetExamRun(RunId);
                    BeginInvoke((MethodInvoker)delegate
                    {
                        if (run.status == "running")
                        {
                            pollTimer.Stop();
                            this.DialogResult = DialogResult.OK;
                            this.Close();
                        }
                        else if (run.status == "finished")
                        {
                            pollTimer.Stop();
                            waitingLabel.Text = "This exam has already finished.";
                            waitingLabel.ForeColor = Color.FromArgb(161, 38, 38);
                        }
                        else
                        {
                            pollTimer.Start();
                        }
                    });
                }
                catch
                {
                    BeginInvoke((MethodInvoker)delegate
                    {
                        pollTimer.Start();
                    });
                }
            });
        }

        protected override void OnFormClosed(FormClosedEventArgs e)
        {
            if (pollTimer != null)
                pollTimer.Stop();
            base.OnFormClosed(e);
        }
    }
}
