#!/usr/bin/env pike

#define YATE_DEBUG

#define FAXBASE "/var/spool/fax"

string called,partycallid;
string ourcallid="pfax/" + (string) getpid();
object yate;
Stdio.FILE logtmp;
int state = 1;

void main()
{
   yate = Public.Protocols.Yate.Engine();
   yate->install("chan.notify",100);

   logtmp = Stdio.FILE("/tmp/faxlog","wact");
   while(state)
   {
      mapping message;
      if(!(message = yate->getevent()))
         break;
      switch(message->type)
      {
         case "incoming":
            switch(message->name)
            {
               case "call.execute":
                  called=message->params->called;
                  partycallid=message->params->id;
                  message->params->targetid=ourcallid;
                  message->handled="true";
                  logtmp->write("%O\n",message);
                  yate->acknowledge(message);
                  message->ack=1;
                  mapping answer = (["type":"outgoing",
                                     "name":"call.answered",
                                     "params":(["targetid":partycallid,
                                                  "id":ourcallid])]);
                  yate->dispatch(answer);
                  answer = (["type":"outgoing",
                             "name":"chan.attach",
                             "params":([
                             "source":"fax/receive"+FAXBASE+get_filename(),
                             "notify":ourcallid])]);
/*
                  answer = (["type":"outgoing",
                      "name":"chan.attach",
                      "params":(["source":"wave/play//var/spool/voicemail/greeting.slin",
                               "notify":ourcallid ])]);
*/
                  yate->dispatch(answer);
                  break;
               case "chan.notify":
                  if(message->targetid == ourcallid)
                  {
                     //state=0;
                     message->handled="true";
                  }
                  break;
            }
            if(!message->ack)
              yate->acknowledge(message);
            break;
         case "answer":
            break;
         case "installed":
         case "uninstalled":
            yate->debug("Pike [Un]Installed: %s\n",message->name);
            break;
         default:
            yate->debug("Unknown type: %s\n",message->type);
      }
   }
}

string get_filename()
{
  int filename=0;
  Stdio.File seqf = Stdio.File(FAXBASE+"/seqf","wrc");
  seqf->lock();
  filename = (int) seqf->read();
  seqf->truncate(0);
  seqf->seek(0);
  seqf->write("%d\n",++filename);
  return sprintf("/%d.tiff",filename);
}
