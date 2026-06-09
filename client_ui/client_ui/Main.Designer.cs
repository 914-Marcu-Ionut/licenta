namespace client_ui
{
    partial class Main
    {
        /// <summary>
        /// Required designer variable.
        /// </summary>
        private System.ComponentModel.IContainer components = null;

        /// <summary>
        /// Clean up any resources being used.
        /// </summary>
        /// <param name="disposing">true if managed resources should be disposed; otherwise, false.</param>
        protected override void Dispose(bool disposing)
        {
            if (disposing && (components != null))
            {
                components.Dispose();
            }
            base.Dispose(disposing);
        }

        #region Windows Form Designer generated code

        /// <summary>
        /// Required method for Designer support - do not modify
        /// the contents of this method with the code editor.
        /// </summary>
        private void InitializeComponent()
        {
            this.label1 = new System.Windows.Forms.Label();
            this.mainFrame = new System.Windows.Forms.Panel();
            this.continueButton = new System.Windows.Forms.Button();
            this.consentCheckBox3 = new System.Windows.Forms.CheckBox();
            this.consentCheckBox2 = new System.Windows.Forms.CheckBox();
            this.consentCheckBox1 = new System.Windows.Forms.CheckBox();
            this.consentLabel = new System.Windows.Forms.Label();
            this.warningDetailsLabel = new System.Windows.Forms.Label();
            this.warningIcon = new System.Windows.Forms.PictureBox();
            this.warningLabel = new System.Windows.Forms.Label();
            this.localAppsList = new System.Windows.Forms.ListView();
            this.button1 = new System.Windows.Forms.Button();
            this.mainFrame.SuspendLayout();
            ((System.ComponentModel.ISupportInitialize)(this.warningIcon)).BeginInit();
            this.SuspendLayout();
            // 
            // label1
            // 
            this.label1.AutoSize = true;
            this.label1.Font = new System.Drawing.Font("Segoe UI", 14F, System.Drawing.FontStyle.Bold, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            this.label1.ForeColor = System.Drawing.Color.FromArgb(((int)(((byte)(35)))), ((int)(((byte)(47)))), ((int)(((byte)(62)))));
            this.label1.Location = new System.Drawing.Point(277, 26);
            this.label1.Name = "label1";
            this.label1.Size = new System.Drawing.Size(119, 38);
            this.label1.TabIndex = 0;
            this.label1.Text = "Loading...";
            // 
            // mainFrame
            // 
            this.mainFrame.BackColor = System.Drawing.Color.White;
            this.mainFrame.Controls.Add(this.continueButton);
            this.mainFrame.Controls.Add(this.consentCheckBox3);
            this.mainFrame.Controls.Add(this.consentCheckBox2);
            this.mainFrame.Controls.Add(this.consentCheckBox1);
            this.mainFrame.Controls.Add(this.consentLabel);
            this.mainFrame.Controls.Add(this.warningDetailsLabel);
            this.mainFrame.Controls.Add(this.warningIcon);
            this.mainFrame.Controls.Add(this.localAppsList);
            this.mainFrame.Controls.Add(this.warningLabel);
            this.mainFrame.Controls.Add(this.button1);
            this.mainFrame.Location = new System.Drawing.Point(12, 85);
            this.mainFrame.Name = "mainFrame";
            this.mainFrame.Size = new System.Drawing.Size(735, 656);
            this.mainFrame.TabIndex = 1;
            this.mainFrame.Visible = false;
            // 
            // continueButton
            // 
            this.continueButton.BackColor = System.Drawing.Color.FromArgb(((int)(((byte)(47)))), ((int)(((byte)(128)))), ((int)(((byte)(237)))));
            this.continueButton.Enabled = false;
            this.continueButton.FlatStyle = System.Windows.Forms.FlatStyle.Flat;
            this.continueButton.Font = new System.Drawing.Font("Segoe UI", 11F, System.Drawing.FontStyle.Bold, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            this.continueButton.ForeColor = System.Drawing.Color.White;
            this.continueButton.Location = new System.Drawing.Point(286, 602);
            this.continueButton.Name = "continueButton";
            this.continueButton.Size = new System.Drawing.Size(164, 40);
            this.continueButton.TabIndex = 6;
            this.continueButton.Text = "Continue";
            this.continueButton.UseVisualStyleBackColor = false;
            this.continueButton.Visible = false;
            this.continueButton.Click += new System.EventHandler(this.continueButton_Click);
            // 
            // consentCheckBox3
            // 
            this.consentCheckBox3.AutoSize = true;
            this.consentCheckBox3.Font = new System.Drawing.Font("Segoe UI", 10F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            this.consentCheckBox3.Location = new System.Drawing.Point(20, 566);
            this.consentCheckBox3.Name = "consentCheckBox3";
            this.consentCheckBox3.Size = new System.Drawing.Size(406, 32);
            this.consentCheckBox3.TabIndex = 8;
            this.consentCheckBox3.Text = "I agree to comply with all exam rules.";
            this.consentCheckBox3.UseVisualStyleBackColor = true;
            this.consentCheckBox3.Visible = false;
            this.consentCheckBox3.CheckedChanged += new System.EventHandler(this.consentCheckBox_CheckedChanged);
            // 
            // consentCheckBox2
            // 
            this.consentCheckBox2.AutoSize = true;
            this.consentCheckBox2.Font = new System.Drawing.Font("Segoe UI", 10F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            this.consentCheckBox2.Location = new System.Drawing.Point(20, 534);
            this.consentCheckBox2.Name = "consentCheckBox2";
            this.consentCheckBox2.Size = new System.Drawing.Size(590, 32);
            this.consentCheckBox2.TabIndex = 5;
            this.consentCheckBox2.Text = "I understand that installing applications during the exam is prohibited.";
            this.consentCheckBox2.UseVisualStyleBackColor = true;
            this.consentCheckBox2.Visible = false;
            this.consentCheckBox2.CheckedChanged += new System.EventHandler(this.consentCheckBox_CheckedChanged);
            // 
            // consentCheckBox1
            // 
            this.consentCheckBox1.AutoSize = true;
            this.consentCheckBox1.Font = new System.Drawing.Font("Segoe UI", 10F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            this.consentCheckBox1.Location = new System.Drawing.Point(20, 500);
            this.consentCheckBox1.Name = "consentCheckBox1";
            this.consentCheckBox1.Size = new System.Drawing.Size(666, 32);
            this.consentCheckBox1.TabIndex = 4;
            this.consentCheckBox1.Text = "I confirm that I understand which applications are permitted during the exam.";
            this.consentCheckBox1.UseVisualStyleBackColor = true;
            this.consentCheckBox1.Visible = false;
            this.consentCheckBox1.CheckedChanged += new System.EventHandler(this.consentCheckBox_CheckedChanged);
            // 
            // consentLabel
            // 
            this.consentLabel.AutoSize = true;
            this.consentLabel.Font = new System.Drawing.Font("Segoe UI", 10F, System.Drawing.FontStyle.Bold, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            this.consentLabel.Location = new System.Drawing.Point(16, 470);
            this.consentLabel.Name = "consentLabel";
            this.consentLabel.Size = new System.Drawing.Size(206, 28);
            this.consentLabel.TabIndex = 3;
            this.consentLabel.Text = "Please confirm below:";
            this.consentLabel.Visible = false;
            // 
            // warningDetailsLabel
            // 
            this.warningDetailsLabel.Font = new System.Drawing.Font("Segoe UI", 9F, System.Drawing.FontStyle.Bold, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            this.warningDetailsLabel.ForeColor = System.Drawing.Color.FromArgb(((int)(((byte)(161)))), ((int)(((byte)(38)))), ((int)(((byte)(38)))));
            this.warningDetailsLabel.Location = new System.Drawing.Point(20, 355);
            this.warningDetailsLabel.Name = "warningDetailsLabel";
            this.warningDetailsLabel.Size = new System.Drawing.Size(700, 100);
            this.warningDetailsLabel.TabIndex = 9;
            this.warningDetailsLabel.Text = "Copying files, text, or images from locations outside your Desktop/exam environment may be" +
    " flagged as cheating.\r\nOpening unauthorized applications that could facilitate ch" +
    "eating is strictly forbidden.\r\nYour screen activity is monitored throughout the e" +
    "xam session.";
            this.warningDetailsLabel.Visible = false;
            // 
            // warningIcon
            // 
            this.warningIcon.Location = new System.Drawing.Point(20, 108);
            this.warningIcon.Name = "warningIcon";
            this.warningIcon.Size = new System.Drawing.Size(32, 32);
            this.warningIcon.SizeMode = System.Windows.Forms.PictureBoxSizeMode.StretchImage;
            this.warningIcon.TabIndex = 7;
            this.warningIcon.TabStop = false;
            this.warningIcon.Visible = false;
            // 
            // warningLabel
            // 
            this.warningLabel.Font = new System.Drawing.Font("Segoe UI", 10F, System.Drawing.FontStyle.Bold, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            this.warningLabel.ForeColor = System.Drawing.Color.FromArgb(((int)(((byte)(180)))), ((int)(((byte)(111)))), ((int)(((byte)(0)))));
            this.warningLabel.Location = new System.Drawing.Point(58, 100);
            this.warningLabel.Name = "warningLabel";
            this.warningLabel.Size = new System.Drawing.Size(661, 48);
            this.warningLabel.TabIndex = 2;
            this.warningLabel.Text = "These applications will not be available in your exam, install them globally if you need them in the exam";
            // 
            // localAppsList
            // 
            this.localAppsList.BackColor = System.Drawing.Color.FromArgb(((int)(((byte)(249)))), ((int)(((byte)(251)))), ((int)(((byte)(255)))));
            this.localAppsList.BorderStyle = System.Windows.Forms.BorderStyle.FixedSingle;
            this.localAppsList.Font = new System.Drawing.Font("Segoe UI", 10F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            this.localAppsList.HideSelection = false;
            this.localAppsList.Location = new System.Drawing.Point(20, 151);
            this.localAppsList.MultiSelect = false;
            this.localAppsList.Name = "localAppsList";
            this.localAppsList.Size = new System.Drawing.Size(695, 200);
            this.localAppsList.TabIndex = 1;
            this.localAppsList.UseCompatibleStateImageBehavior = false;
            this.localAppsList.View = System.Windows.Forms.View.Details;
            // 
            // button1
            // 
            this.button1.BackColor = System.Drawing.Color.FromArgb(((int)(((byte)(47)))), ((int)(((byte)(128)))), ((int)(((byte)(237)))));
            this.button1.FlatStyle = System.Windows.Forms.FlatStyle.Flat;
            this.button1.Font = new System.Drawing.Font("Segoe UI", 11F, System.Drawing.FontStyle.Bold, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            this.button1.ForeColor = System.Drawing.Color.White;
            this.button1.Location = new System.Drawing.Point(286, 33);
            this.button1.Name = "button1";
            this.button1.Size = new System.Drawing.Size(164, 48);
            this.button1.TabIndex = 0;
            this.button1.Text = "Start exam";
            this.button1.UseVisualStyleBackColor = false;
            this.button1.Click += new System.EventHandler(this.button1_Click);
            // 
            // Main
            // 
            this.AutoScaleDimensions = new System.Drawing.SizeF(9F, 20F);
            this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
            this.BackColor = System.Drawing.Color.FromArgb(((int)(((byte)(245)))), ((int)(((byte)(247)))), ((int)(((byte)(252)))));
            this.ClientSize = new System.Drawing.Size(760, 754);
            this.Controls.Add(this.mainFrame);
            this.Controls.Add(this.label1);
            this.FormBorderStyle = System.Windows.Forms.FormBorderStyle.FixedSingle;
            this.MaximizeBox = false;
            this.Name = "Main";
            this.Text = "Exam Client";
            this.FormClosing += new System.Windows.Forms.FormClosingEventHandler(this.Main_FormClosing);
            this.FormClosed += new System.Windows.Forms.FormClosedEventHandler(this.Main_FormClosed);
            this.mainFrame.ResumeLayout(false);
            this.mainFrame.PerformLayout();
            ((System.ComponentModel.ISupportInitialize)(this.warningIcon)).EndInit();
            this.ResumeLayout(false);
            this.PerformLayout();

        }

        #endregion

        private System.Windows.Forms.Label label1;
        private System.Windows.Forms.Panel mainFrame;
        private System.Windows.Forms.Button button1;
        private System.Windows.Forms.Label warningLabel;
        private System.Windows.Forms.ListView localAppsList;
        private System.Windows.Forms.Label consentLabel;
        private System.Windows.Forms.CheckBox consentCheckBox1;
        private System.Windows.Forms.CheckBox consentCheckBox2;
        private System.Windows.Forms.Button continueButton;
        private System.Windows.Forms.PictureBox warningIcon;
        private System.Windows.Forms.Label warningDetailsLabel;
        private System.Windows.Forms.CheckBox consentCheckBox3;
    }
}

