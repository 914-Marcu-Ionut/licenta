using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using Newtonsoft.Json;
using Newtonsoft.Json.Linq;

namespace client_ui
{
    public class GeneralMessage
    {
        public string type;

        public string SerializeMessage()
        {
            return JsonConvert.SerializeObject(this);
        }

        public static GeneralMessage DeserializeMessage(string json)
        {
            var jObject = JObject.Parse(json);

            string type = jObject["type"] != null
                ? jObject["type"].ToString()
                : null;

            if (type == null)
                throw new Exception("Missing message type");

            GeneralMessage msg;

            switch (type)
            {
                case "connect":
                    msg = jObject.ToObject<ConnectMessage>();
                    break;

                case "start_exam":
                    msg = jObject.ToObject<StartExamMessage>();
                    break;

                case "continue_to_exam":
                    msg = jObject.ToObject<ContinueToExamMessage>();
                    break;

                case "response":
                    msg = jObject.ToObject<ResponseMessage>();
                    break;

                case "start_exam_result":
                    msg = jObject.ToObject<StartExamResultMessage>();
                    break;

                case "init_exam_response":
                    msg = jObject.ToObject<InitExamResponseMessage>();
                    break;
                    
                default:
                    throw new Exception("Unknown message type: " + type);
            }

            return msg;
        }
    }
    public class ConnectMessage : GeneralMessage
    {
        public ConnectMessage()
        {
            type = "connect";
        }
    }

    public class ResponseMessage : GeneralMessage
    {
        public int code;
        public string error;

        public ResponseMessage()
        {
            type = "response";
        }
    }

    public class StartExamMessage : GeneralMessage
    {
        public StartExamMessage()
        {
            type = "start_exam";
        }
    }

    public class ContinueToExamMessage : GeneralMessage
    {
        public string registered_name;
        public string run_id;

        public ContinueToExamMessage()
        {
            type = "continue_to_exam";
        }
    }

    public class InstalledProgramInfo
    {
        public string name;
        public string version;
        public string display_icon;
    }

    public class StartExamResultMessage : GeneralMessage
    {
        public int code;
        public string warning;
        public List<InstalledProgramInfo> programs;

        public StartExamResultMessage()
        {
            type = "start_exam_result";
        }
    }

    public class InitExamResponseMessage : GeneralMessage
    {
        public int code;
        public string error;

        public InitExamResponseMessage()
        {
            type = "init_exam_response";
        }
    }
}
