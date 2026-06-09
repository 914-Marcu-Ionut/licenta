using System;
using System.IO;
using System.Net;
using System.Net.Security;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using Newtonsoft.Json;
using Newtonsoft.Json.Linq;

namespace client_ui
{
    public class ExamRunResponse
    {
        public string id;
        public string exam_id;
        public string status;
        public string exam_name;
    }

    public class RegisteredStudentResponse
    {
        public string id;
        public string run_id;
        public string name;
    }

    public static class BackendClient
    {
        public static string BackendHost = "192.168.0.199";
        public static int BackendPort = 8443;
        public static string BaseUrl = "https://" + BackendHost + ":" + BackendPort;

        static BackendClient()
        {
            LoadConfig();
            ServicePointManager.SecurityProtocol = (SecurityProtocolType)3072;
            ServicePointManager.ServerCertificateValidationCallback = AcceptAllCerts;
        }

        private static void LoadConfig()
        {
            try
            {
                string configPath = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "client_config.json");
                if (File.Exists(configPath))
                {
                    string json = File.ReadAllText(configPath);
                    var cfg = JObject.Parse(json);
                    if (cfg["backend_host"] != null)
                        BackendHost = cfg["backend_host"].ToString();
                    if (cfg["backend_port"] != null)
                        BackendPort = (int)cfg["backend_port"];
                    BaseUrl = "https://" + BackendHost + ":" + BackendPort;
                }
            }
            catch { }
        }

        private static bool AcceptAllCerts(object sender, X509Certificate cert,
            X509Chain chain, SslPolicyErrors errors)
        {
            return true;
        }

        public static ExamRunResponse GetExamRun(string runId)
        {
            string json = HttpGet(BaseUrl + "/exam/run?id=" + Uri.EscapeDataString(runId));
            return JsonConvert.DeserializeObject<ExamRunResponse>(json);
        }

        public static RegisteredStudentResponse RegisterStudent(string runId, string name)
        {
            string body = JsonConvert.SerializeObject(new { run_id = runId, name = name });
            string json = HttpPost(BaseUrl + "/exam/run/register", body);
            return JsonConvert.DeserializeObject<RegisteredStudentResponse>(json);
        }

        private static string HttpGet(string url)
        {
            HttpWebRequest req = (HttpWebRequest)WebRequest.Create(url);
            req.Method = "GET";
            req.ContentType = "application/json";
            return ReadResponse(req);
        }

        private static string HttpPost(string url, string jsonBody)
        {
            HttpWebRequest req = (HttpWebRequest)WebRequest.Create(url);
            req.Method = "POST";
            req.ContentType = "application/json";
            byte[] data = Encoding.UTF8.GetBytes(jsonBody);
            req.ContentLength = data.Length;
            using (Stream s = req.GetRequestStream())
            {
                s.Write(data, 0, data.Length);
            }
            return ReadResponse(req);
        }

        private static string ReadResponse(HttpWebRequest req)
        {
            using (HttpWebResponse resp = (HttpWebResponse)req.GetResponse())
            using (Stream stream = resp.GetResponseStream())
            using (StreamReader reader = new StreamReader(stream, Encoding.UTF8))
            {
                return reader.ReadToEnd();
            }
        }
    }
}
