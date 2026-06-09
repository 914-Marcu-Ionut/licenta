using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.IO;
using System.IO.Pipes;
using System.Threading;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace client_ui
{
    public class Client
    {
        const string pipe_client_name = "ExamAppUiPipe";
        private static readonly UTF8Encoding Utf8NoBom = new UTF8Encoding(false);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool PeekNamedPipe(
            SafePipeHandle hNamedPipe,
            byte[] lpBuffer,
            uint nBufferSize,
            IntPtr lpBytesRead,
            out uint lpTotalBytesAvail,
            IntPtr lpBytesLeftThisMessage
        );

        private NamedPipeClientStream pipe_stream;
        private StreamReader pipe_reader;
        private StreamWriter pipe_writer;

        public bool connected = false;
        public bool handshaked = false;

        public Client() { }

        public bool connect()
        {
            connected = false;
            handshaked = false;
            pipe_stream = new NamedPipeClientStream(
            ".",                // local machine
            pipe_client_name,      // pipe name
            PipeDirection.InOut,
            PipeOptions.None);

            try
            {
                pipe_stream.Connect(2000);
            }
            catch (Exception ex)
            {
                return false;
            }
            pipe_reader = new StreamReader(pipe_stream, Utf8NoBom, detectEncodingFromByteOrderMarks: false);
            pipe_writer = new StreamWriter(pipe_stream, Utf8NoBom);
            pipe_writer.NewLine = "\n";
            pipe_writer.AutoFlush = true;
            connected = true;

            return true;

        }

        public string readString()
        {
            string data = pipe_reader.ReadLine();
            if (data == null)
                return null;

            if (data.Length > 0 && data[0] == '\uFEFF')
                data = data.Substring(1);

            data = data.TrimEnd('\r');
            return data;
        }

        public bool tryReadString(int timeoutMs, out string data)
        {
            data = null;
            if (pipe_stream == null || pipe_reader == null)
                return false;

            int waitedMs = 0;
            const int pollStepMs = 20;

            while (waitedMs < timeoutMs)
            {
                try
                {
                    if (pipe_stream == null || !pipe_stream.IsConnected)
                    {
                        data = null;
                        return true;
                    }

                    uint available;
                    bool ok = PeekNamedPipe(
                        pipe_stream.SafePipeHandle,
                        null,
                        0,
                        IntPtr.Zero,
                        out available,
                        IntPtr.Zero
                    );

                    if (!ok)
                    {
                        data = null;
                        return true;
                    }

                    if (available > 0)
                    {
                        data = readString();
                        return true;
                    }
                }
                catch (Exception ex)
                {
                    data = null;
                    return true;
                }

                Thread.Sleep(pollStepMs);
                waitedMs += pollStepMs;
            }

            return false;
        }

        public bool hasToRead()
        {
            return pipe_stream != null && pipe_stream.IsConnected;
        }

        public void writeString(string data)
        {
            if (data == null)
                throw new ArgumentNullException(nameof(data));

            if (data.IndexOf('\n') >= 0 || data.IndexOf('\r') >= 0)
                throw new InvalidOperationException("Pipe JSON message must be a single line.");

            pipe_writer.Write(data);
            pipe_writer.Write('\n');
            pipe_writer.Flush();
        }

        public void disconnect()
        {
            connected = false;
            handshaked = false;
            try
            {
                pipe_writer.Dispose();
                pipe_reader.Dispose();
                pipe_stream.Dispose();
            }
            catch
            {

            }
            pipe_writer = null;
            pipe_reader = null;
            pipe_stream = null;
        }
    }
}
