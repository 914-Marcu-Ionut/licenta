using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Threading;
using System.Windows.Forms;
using Newtonsoft.Json;

namespace client_ui
{
    internal static class Program
    {
        public static Main main_ui;
        public static Client client;
        public static string RegisteredStudentName = "";
        public static string RegisteredRunId = "";

        private static List<Thread> threads;
        private static readonly Queue<string> outgoingMessages = new Queue<string>();
        private static readonly object outgoingLock = new object();
        private static readonly ManualResetEvent pipeReadySignal = new ManualResetEvent(false);
        private static Process clientProcess;

        /// <summary>
        /// The main entry point for the application.
        /// </summary>
        [STAThread]
        static void Main()
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);

            if (!StartClientProcess())
                return;

            while (true)
            {
                JoinForm joinForm = new JoinForm();
                DialogResult joinResult = joinForm.ShowDialog();

                if (joinResult != DialogResult.OK)
                    return;

                RegisteredStudentName = joinForm.StudentName ?? "";
                RegisteredRunId = joinForm.RunId ?? "";
                main_ui = new Main();
                threads = new List<Thread>();

                Thread pipeThread = new Thread(PipeThread);
                pipeThread.Start();
                threads.Add(pipeThread);

                Application.Run(main_ui);

                if (main_ui.DialogResult != DialogResult.Retry)
                    return;
            }
        }

        private static bool StartClientProcess()
        {
            try
            {
                string exeDir = AppDomain.CurrentDomain.BaseDirectory;
                string clientPath = System.IO.Path.Combine(exeDir, "client.exe");
                if (!System.IO.File.Exists(clientPath))
                {
                    MessageBox.Show("client.exe not found next to client_ui.exe.\nPath: " + clientPath,
                        "Exam Client", MessageBoxButtons.OK, MessageBoxIcon.Error);
                    return false;
                }

                ProcessStartInfo psi = new ProcessStartInfo
                {
                    FileName = clientPath,
                    WorkingDirectory = exeDir,
                    UseShellExecute = true,
                    Verb = "runas",
                    WindowStyle = ProcessWindowStyle.Hidden
                };
                clientProcess = Process.Start(psi);
                return true;
            }
            catch (Exception ex)
            {
                MessageBox.Show("Failed to start client.exe:\n" + ex.Message,
                    "Exam Client", MessageBoxButtons.OK, MessageBoxIcon.Error);
                return false;
            }
        }

        private static void StopClientProcess()
        {
            try
            {
                if (clientProcess != null && !clientProcess.HasExited)
                {
                    clientProcess.Kill();
                    clientProcess.WaitForExit(3000);
                }
            }
            catch { }
            clientProcess = null;
        }

        private static void PipeClientRun()
        {
            var connectMsg = new ConnectMessage();
            client.writeString(connectMsg.SerializeMessage());
            Console.WriteLine("connect msg sent");

            var reponseStr = client.readString();
            if (reponseStr == null || reponseStr == "")
            {
                throw new Exception("Handshake failed: empty response from core app.");
            }

            Console.WriteLine("Handshake response: " + reponseStr);

            var responseMsg = GeneralMessage.DeserializeMessage(reponseStr) as ResponseMessage;
            if (responseMsg == null)
            {
                throw new Exception("Handshake failed: expected response message.");
            }

            if(responseMsg.code != 0)
            {
                throw new Exception("Servered returned " + responseMsg.code.ToString() + " code to connect");
            }
            client.handshaked = true;
            pipeReadySignal.Set();
            Console.WriteLine("Pipe handshake complete");

            while (main_ui.running)
            {
                FlushOutgoingMessages();

                string msgString;
                bool hasIncoming = client.tryReadString(200, out msgString);
                if (!hasIncoming)
                {
                    continue;
                }

                if (msgString == null)
                {
                    throw new Exception("Pipe closed by core app.");
                }

                if (msgString == "")
                {
                    continue;
                }

                try
                {
                    var msg = GeneralMessage.DeserializeMessage(msgString);
                    Console.WriteLine("Got: " + msgString);

                    var startExamResult = msg as StartExamResultMessage;
                    if (startExamResult != null)
                    {
                        NotifyStartExamResult(startExamResult);
                    }

                    var initExamResponse = msg as InitExamResponseMessage;
                    if (initExamResponse != null)
                    {
                        NotifyInitExamResponse(initExamResponse);
                    }
                }
                catch (Exception ex)
                {
                    Console.WriteLine("Invalid incoming message (ignored): " + ex.Message + " | raw=" + msgString);
                }
            }
        }

        private static void PipeThread()
        {
            client = new Client();
            bool lastStatus = false;

            NotifyPipeConnectionStatus(false);
            while (main_ui.running)
            {
                bool connected = client.connect();
                if (!connected)
                {
                    if (lastStatus)
                    {
                        lastStatus = false;
                        NotifyPipeConnectionStatus(false);
                    }
                    Thread.Sleep(1000);
                    continue;
                }

                if (!lastStatus)
                {
                    lastStatus = true;
                    NotifyPipeConnectionStatus(true);
                }

                Console.WriteLine("Pipe connected");
                try
                {
                    PipeClientRun();
                }catch(Exception ex)
                {
                    Console.WriteLine("Error in pipe client: " + ex.ToString());
                }
                pipeReadySignal.Reset();
                client.disconnect();
                if (lastStatus)
                {
                    lastStatus = false;
                    NotifyPipeConnectionStatus(false);
                }
                Console.WriteLine("Pipe disconnected");
            }
            NotifyPipeConnectionStatus(false);
            Console.WriteLine("Pipe thread finished");
        }

        private static void FlushOutgoingMessages()
        {
            while (true)
            {
                string payload;
                if (!TryDequeueOutgoingMessage(out payload))
                {
                    break;
                }

                if (!pipeReadySignal.WaitOne(0, false))
                {
                    RequeueOutgoingMessage(payload);
                    Console.WriteLine("Outgoing message deferred: pipe is not ready yet.");
                    break;
                }

                try
                {
                    Console.WriteLine("sending " + payload);
                    client.writeString(payload);
                    Console.WriteLine("Outgoing message sent: " + payload);
                }
                catch (Exception ex)
                {
                    Console.WriteLine("Failed to send outgoing message: " + ex.Message);
                    RequeueOutgoingMessage(payload);
                    break;
                }
            }
        }

        private static bool TryDequeueOutgoingMessage(out string payload)
        {
            lock (outgoingLock)
            {
                if (outgoingMessages.Count == 0)
                {
                    payload = null;
                    return false;
                }

                payload = outgoingMessages.Dequeue();
                return true;
            }
        }

        private static void RequeueOutgoingMessage(string payload)
        {
            lock (outgoingLock)
            {
                outgoingMessages.Enqueue(payload);
            }
        }

        public static bool EnqueuePipeMessage(GeneralMessage message)
        {
            if (message == null)
                return false;

            string payload = message.SerializeMessage();
            lock (outgoingLock)
            {
                outgoingMessages.Enqueue(payload);
            }

            Console.WriteLine("Outgoing message queued: " + payload);
            return true;
        }

        private static void NotifyPipeConnectionStatus(bool connected)
        {
            if (main_ui == null || main_ui.IsDisposed)
                return;

            if (main_ui.InvokeRequired)
            {
                main_ui.BeginInvoke((MethodInvoker)(() => main_ui.PipeConnectionStatusChanged(connected)));
                return;
            }

            main_ui.PipeConnectionStatusChanged(connected);
        }

        private static void NotifyStartExamResult(StartExamResultMessage message)
        {
            if (main_ui == null || main_ui.IsDisposed)
                return;

            if (main_ui.InvokeRequired)
            {
                main_ui.BeginInvoke((MethodInvoker)(() => main_ui.ShowStartExamLocalPrograms(message)));
                return;
            }

            main_ui.ShowStartExamLocalPrograms(message);
        }

        private static void NotifyInitExamResponse(InitExamResponseMessage message)
        {
            if (main_ui == null || main_ui.IsDisposed)
                return;

            if (main_ui.InvokeRequired)
            {
                main_ui.BeginInvoke((MethodInvoker)(() => main_ui.ShowInitExamResponse(message)));
                return;
            }

            main_ui.ShowInitExamResponse(message);
        }

        public static void OnUiClose()
        {
            Console.WriteLine("UI closed");
            StopClientProcess();
            Process.GetCurrentProcess().Kill();
        }
    }
}
