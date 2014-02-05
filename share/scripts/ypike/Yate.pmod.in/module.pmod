#pike __REAL_VERSION__
constant __version=0.1;
constant __author="Marc Dirix <marc@electronics-design.nl";
constant __components=({"Public.pmod/Protocols.pmod/Yate.pmod"});

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#define YATE_DEBUG

class Engine
{
   static Stdio.FILE yate_socket;

   void create(void|string|object server, void|string role, void|int port)
   {

      if(server)
      {
         if(objectp(server))
            yate_socket=server;
         else if(port)
         {
            if(!yate_socket->connect(server, port))
   	    error("Cannot open connection to: %s:%d\n",server,port);
         }
         else
         {
            if(!yate_socket->connect_unix(server))
   	    error("Cannot open connection to: %s\n",server);
         }
      }
      if(role && sizeof(role))
         _yate_print("%%%%>connect:%s\n",role);
      return;
   }

   /**
    * Static function to output astring to Yate's stderr or logfile
    * only if debugging was enabled.
    * @param str String to output if yate_debug is set.
   **/
   void debug(string str, mixed ... args)
   {
   #ifdef YATE_DEBUG
      this->output(str, @args);
   #endif
      return;
   }

   /**
    * Private function to convert a string to its Yate escaped format
    * @param str String to escape
    * @return Yate escaped string
    */
   private string _yate_escape(string str)
   {
       return replace(str,
       ({"%" , "\000" , "\001" , "\002" , "\003" , "\004" , "\005" ,
       "\006" , "\007" , "\008" , "\009" , "\010" , "\011" , "\012" ,
       "\013" , "\014" , "\015" , "\016" , "\017" , "\018" , "\019" ,
       "\020" , "\021" , "\022" , "\023" , "\024" , "\025" , "\026" ,
       "\027" , "\028" , "\029" , "\030" , "\031" , "\072" }),
       ({"%%" , "%\100" , "%\101" , "%\102" , "%\103" , "%\104" , "%\105" ,
       "%\106" , "%\107" , "%\108" , "%\109" , "%\110" , "%\111" , "%\112" ,
       "%\113" , "%\114" , "%\115" , "%\116" , "%\117" , "%\118" , "%\119" ,
       "%\120" , "%\121" , "%\122" , "%\123" , "%\124" , "%\125" , "%\126" ,
       "%\127" , "%\128" , "%\129" , "%\130" , "%\131" , "%\172" }));
   }

   /**
    * Static function to convert an Yate escaped string back to string
    * @param str Yate escaped String
    * @return unescaped string
    */
   private string _yate_unescape(string str)
   {
      string outp="";
      for(int a=0 ; a < sizeof(str) ; a++ )
      {
         int c=str[a];
         if(c == '%')
	 {
	    a++;
	    c=str[a];
	    if ( c != '%' )
	       c = c-64;
	 }
	 outp+=sprintf("%c",c);
      }
      return outp;
   }

   /*
    * install a Yate message handler
    * @param name Name of the messages to handle
    * @param priority (optional) Priority to insert in chain, default 100
    * @param filtname (optional) Name of parameter to filter for
    * @param filtvalue (optional) Matching value of filtered parameter
    */
   void install(string name, void|int priority, void|string filter_name,
                                                       void|string filter_value)
   {
      string filter="";
      name=_yate_escape(name);
      if(zero_type(priority))
         int priority=100;
      if(filter_name)
         filter=":"+_yate_escape(filter_name)+":"+_yate_escape(filter_value||"");
      _yate_print("%%%%>install:%d:%s:%s\n",priority,name,filter);
   }

   /*
    * Uninstall a Yate message handler
    * @param name Name of the messages to handle
    */
   void uninstall(string name)
   {
      name=_yate_escape(name);
      _yate_print("%%%%>install:%s\n",name);
   }


   /*
    * Install a Yate message watcher
    * @param name Name of the messages to watch
    */
   void watch(string name)
   {
      name=_yate_escape(name);
      _yate_print("%%%%>watch:%s\n",name);
   }

   /*
    * Uninstall a Yate message watcher
    * @param name Name of the messages to stop watching
    */
   void unwatch(string name)
   {
      name=_yate_escape(name);
      _yate_print("%%%%>unwatch:%s\n",name);
   }

   /*
    * Dispatch the message to Yate for handling
    * @param message Message object to dispatch
    */
   void dispatch(mapping message)
   {
      if(!message->type)
         message->type="outgoing";
      else if(message->type != "outgoing")
      {
         output("Pike bug: attempt to dispatch message type: %s\n",message->type);
         return UNDEFINED;
      }
      string paramstring = "";
      int origin=time();
      if(message->params)
         paramstring=_yate_mapping2string(message->params);
      string id;
      if(!message->id)
      {
         random_seed(getpid());
	 id=(string) random(99999999);
      }
      _yate_print("%%%%>message:%s:%d:%s:%s%s\n",_yate_escape(id),origin,
                  _yate_escape(message->name),_yate_escape(message->retval||""),
   	       paramstring);
   }


   void acknowledge(mapping message)
   {
      if(message->type != "incoming")
      {
       output("Pike bug: attempt to acknowledge message type: %s\n",message->type);
       return;
      }
      if(!message->handled)
         message->handled="false";
      string paramstring = "";
      if(message->params)
         paramstring=_yate_mapping2string(message->params);
      _yate_print("%%%%<message:%s:%s:%s:%s%s\n",_yate_escape(message->id),
                  _yate_escape(message->handled),_yate_escape(message->name),
   	       _yate_escape(message->retval||""),paramstring);
   }

   void setlocal(string name, string value)
   {
      _yate_print("%%%%>setlocal:%s:%s\n",_yate_escape(name),_yate_escape(value));
   }

   mapping getevent()
   {
      string rawmessage;
      if(yate_socket)
      {
        if(!(rawmessage=yate_socket->gets()))
           return UNDEFINED;
      }
      else
      {
        if(!(rawmessage=Stdio.stdin->gets()))
           return UNDEFINED;
      }
      rawmessage=replace(rawmessage,"\n","");
      if(rawmessage == "")
         return (["type":"empty"]);
      array message_parts = rawmessage/":";
      mapping message=([]);
      switch (message_parts[0])
      {
         case "%%>message":
        /*incoming message str_id:int_time:str_name:str_retval[:key=value...] */
            message->ack=0;
#ifdef YATE_DEBUG
	    message->raw=rawmessage;
#endif
            message->type="incoming";
            message->id=_yate_unescape(message_parts[1]);
   	    message->retval=_yate_unescape(message_parts[4]);
   	    message->name=_yate_unescape(message_parts[3]);
   	    message->origin=0+(int) message_parts[2];
   	    if(sizeof(message_parts) > 4)
   	       message->params=_yate_array2mapping(message_parts[5..]);
   	    break;
         case "%%<message":
            message->type="answer";
   	    message->id=_yate_unescape(message_parts[1]);
   	    message->retval=_yate_unescape(message_parts[4]);
   	    message->name=_yate_unescape(message_parts[3]);
   	    message->handled=_yate_unescape(message_parts[2]);
   	    if(sizeof(message_parts) > 4)
   	       message->params=_yate_array2mapping(message_parts[5..]);
   	    break;
         case "%%<install":
         case "%%<uninstall":
         /* [un]install answer num_priority:str_name:bool_success */
            message->type=message_parts[0][3..];
            message->name=_yate_unescape(message_parts[2]);
   	    message->handled=_yate_unescape(message_parts[3]);
   	    message->priority=(int) message_parts[1];
   	    break;
         case "%%<watch":
         case "%%<unwatch":
         /* [un]watch answer str_name:bool_success */
            message->type=message_parts[0][3..];
   	    message->name=_yate_unescape(message_parts[1]);
   	    message->handled=_yate_unescape(message_parts[2]);
   	    break;
         case "%%<setlocal":
         /* local parameter answer str_name:str_value:bool_success */
            message->type=message_parts[0][3..];
   	    message->name=_yate_unescape(message_parts[1]);
   	    message->retval=_yate_unescape(message_parts[2]);
   	    message->handled=_yate_unescape(message_parts[3]);
   	    break;
         case "Error in":
            message->type="error";
   	    break;
         default:
            message->type="error";
            output("Unable to decode: %s",rawmessage);
      }
      return message;
   }

   void output(string str, mixed ... args)
   {
     if(yate_socket)
        _yate_print("%%%%>output:"+str+"\n", @args);
     else
        Stdio.stderr->write(str+"\n", @args);
   }

   /* Internal function */
   private string _yate_mapping2string(mapping params)
   {
      string param="";
      foreach(indices(params), string key)
      {
        param+=":" + _yate_escape(key) + "=" + _yate_escape(params[key]);
      }
      return param;
   }
   /* Internal function */
   private mapping _yate_array2mapping(array message_params)
   {
      mapping params=([]);
      foreach(message_params, string param)
      {
        if(has_value(param,"="))
        {
          array p = param/"=";
          params+=([_yate_unescape(p[0]):_yate_unescape(p[1])]);
        }
        else
           params+=([_yate_unescape(param):""]);
      }
      return params;
   }
   /* Internal function */
   private void _yate_print(string str, mixed ... args)
   {
      if(yate_socket)
         yate_socket->write(sprintf(str,@args));
      else
         Stdio.stdout->write(sprintf(str,@args));
   }
};

