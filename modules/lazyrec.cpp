/**
 * recorder.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Lazy wave file recorder
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2006 Scott Tiger
 * Pawel Susicki, 3ci at tiger com pl, Maciej Kaminski maciejka tiger com pl
 *
 */

#include <yatephone.h>
#include <yateclass.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <list>

#define FLUSH_PERIOD 10

using namespace TelEngine;
namespace { // anonymous

static Configuration s_cfg(Engine::configFile("lazyrec"));

  class PacketBucket {
  protected:
    int m_fd;
    String m_notifyid;
    bool m_last;
    size_t m_count;
    char m_buf[1024*1024];
  public:
    PacketBucket(int fd, String& notifyid): 
      m_fd(fd), m_notifyid(notifyid), m_last(false), m_count(0) {}
    bool full(){  return m_count == sizeof(m_buf); }
    size_t write( void *_buf, size_t size );
    void save();
    void setLast() { 
      Debug("LazyRecorder", DebugAll, "setLast!");
      m_last = true; 
    };
    bool isLast() { return m_last; };
    size_t getCount() { return m_count; };
  };

  class BucketWriter : public Thread {
  protected:
    std::list<PacketBucket*> m_buckets;
    Mutex m_mutex;
    unsigned m_sleep;
  public:
    BucketWriter() { 
      m_sleep =  s_cfg.getIntValue("general", "flush_period", FLUSH_PERIOD);
    }
    virtual void run();
    PacketBucket* pop();
    void push(PacketBucket *pb);
  };

  static BucketWriter* s_bucket_writer;

  class LazyConsumer : public DataConsumer
  {
  public:
    LazyConsumer(const String& file, const String& notifyid, unsigned maxlen = 0);
    ~LazyConsumer();
    virtual unsigned long Consume(const DataBlock& data, unsigned long tStamp, unsigned long flags);
  private:
    CallEndpoint* m_chan;
    PacketBucket* m_current;
    String m_file;    
    int m_fd;
    unsigned m_total;
    unsigned m_maxlen;
    String m_notifyid;
    void close();
  };


  size_t PacketBucket::write( void *_buf, size_t size )
  {
    size_t free_space = sizeof(m_buf) - m_count;
    if( free_space >= size ) {
        ::memcpy( m_buf + m_count, _buf, size );
        m_count += size;
        return size;
    } else {
        ::memcpy( m_buf + m_count, _buf, free_space);
        m_count = sizeof(m_buf);
        return free_space;
    }
    return size;
  }

  void PacketBucket::save() {
    Debug("LazyRecorder", DebugAll,"Writing %d bytes.", getCount());
    ::write( m_fd, m_buf, m_count);    
    if(m_last) {
      Debug("LazyRecorder", DebugAll,"Closing file");
      ::close(m_fd);
      if(m_notifyid) {
        Debug("LazyRecorder", DebugAll,"notyfiing: %s", m_notifyid.c_str());
        Message *m = new Message("chan.notify");
        m->addParam("targetid",m_notifyid);
        Engine::enqueue(m);
      }
    }
  }

  void BucketWriter::push( PacketBucket *pb )
  {
    Lock lock(m_mutex);
    m_buckets.push_back(pb);
  }

  PacketBucket *BucketWriter::pop() {
    Lock lock(m_mutex);
    if( m_buckets.empty() ) {
      return 0;
    } else {
      PacketBucket *fb = m_buckets.front();
      m_buckets.pop_front();
      return fb;
    }
    return 0;
  }

  void BucketWriter::run() {
    Debug("LazyRecorder", DebugInfo,"Flush thread started.");
    while(!check(false))
      {
        PacketBucket *pb;
        while( ( pb = pop() ) )
          {
            pb->save();
            delete pb;
          }
        Debug("LazyRecorder", DebugAll,"Flush thread sleeps.");
        Thread::sleep(m_sleep);
      }
    Debug("LazyRecorder", DebugInfo,"Flush thread finished.");
  }

  class RecordHandler : public MessageHandler {
  public:
    RecordHandler() : MessageHandler("chan.lazyrecord") { }
    virtual bool received(Message &msg);
  };

  class LazyRecorderPlugin : public Plugin {
  public:
    LazyRecorderPlugin();
    virtual ~LazyRecorderPlugin();
    virtual void initialize();
  };

  INIT_PLUGIN(LazyRecorderPlugin);


  LazyConsumer::LazyConsumer(const String& file, const String& notifyid, unsigned maxlen)
    : m_current(0), m_fd(0), m_total(0), 
      m_maxlen(maxlen), m_notifyid(notifyid)
  {
    Debug("LazyRecorder", DebugAll,"LazyConsumer::LazyConsumer(\"%s\",%u) [%p]",
          file.c_str(),maxlen,this);
    if (file == "-")
      return;
    else if (file.endsWith(".gsm"))
      m_format = "gsm";
    else if (file.endsWith(".alaw") || file.endsWith(".A"))
      m_format = "alaw";
    else if (file.endsWith(".mulaw") || file.endsWith(".u"))
      m_format = "mulaw";
    else if (file.endsWith(".ilbc20"))
      m_format = "ilbc20";
    else if (file.endsWith(".ilbc30"))
      m_format = "ilbc30";

    m_fd = ::open(file.safe(),O_WRONLY|O_CREAT|O_TRUNC|O_NOCTTY|O_BINARY,S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP);

    if (!m_fd)
      Debug("LazyRecorder", DebugWarn,"Creating '%s': error %d: %s",
	    file.c_str(), errno, ::strerror(errno));
    else
      m_current = new PacketBucket(m_fd, m_notifyid);
  }

  LazyConsumer::~LazyConsumer()
  {
    Debug("LazyRecorder", DebugAll, 
          "LazyConsumer::~LazyConsumer() [%p] total=%u",this,m_total);
    if (m_fd) {
      Debug("LazyRecorder", DebugAll, "Flushing buffer");
      m_current->setLast();
      s_bucket_writer->push(m_current);
      m_fd = 0;
    }
  }

  unsigned long LazyConsumer::Consume(const DataBlock& data, unsigned long tStamp, unsigned long flags)
  {
    if (!data.null()) {
      if (m_fd) {
        char *buf = (char *)data.data();
        unsigned size = data.length();
        while( size ) {
          if(m_current->full()) {
              Debug("LazyRecorder", DebugAll, "Bucket full![%p]", this);
              s_bucket_writer->push(m_current);
              m_current = new PacketBucket(m_fd, m_notifyid);
          }
          size_t wrote = m_current->write( buf, size );
          size -= wrote;
          buf += wrote;
        }
      }
      m_total += data.length();
      if (m_maxlen && (m_total >= m_maxlen)) {
        m_maxlen = 0;
        if (m_fd >= 0) {
          Debug("LazyRecorder", DebugAll, 
                "Flushing buffer, maxlen exceded [%p]", this);
          m_current->setLast();
          s_bucket_writer->push(m_current);
          m_fd = 0;
        }
      }
    }
    return 0;
  }

  bool RecordHandler::received(Message &msg)
  {
    int more = 2;

    if(!s_bucket_writer->running())
      Debug("LazyRecorder", DebugWarn,"Request to record while recording thread is dead!");

    String c1(msg.getValue("call"));
    if (c1.null())
      more--;
    else {
      Regexp r("^wave/\\([^/]*\\)/\\(.*\\)$");
      if (c1.matches(r)) {
        if (c1.matchString(1) == "record") {
          c1 = c1.matchString(2);
          more--;
        }
        else {
          Debug("LazyRecorder", DebugWarn,"Could not attach call recorder with method '%s', use 'record'",
                c1.matchString(1).c_str());
          c1 = "";
        }
      }
      else
        c1 = "";
    }

    String c2(msg.getValue("peer"));
    if (c2.null())
      more--;
    else {
      Regexp r("^wave/\\([^/]*\\)/\\(.*\\)$");
      if (c2.matches(r)) {
        if (c2.matchString(1) == "record") {
          c2 = c2.matchString(2);
          more--;
        }
        else {
          Debug("LazyRecorder", DebugWarn,"Could not attach peer recorder with method '%s', use 'record'",
                c2.matchString(1).c_str());
          c2 = "";
        }
      }
      else
        c2 = "";
    }

    if (c1.null() && c2.null())
      return false;

    String ml(msg.getValue("maxlen"));
    unsigned maxlen = ml.toInteger(0);

    CallEndpoint *ch = static_cast<CallEndpoint*>(msg.userObject("CallEndpoint"));
    DataEndpoint *de = static_cast<DataEndpoint*>(msg.userObject("DataEndpoint"));

    if (ch && !de)
      de = ch->setEndpoint();

    if (!de) {
      if (!c1.null())
        Debug("LazyRecorder", DebugWarn,"Wave source '%s' call record with no data channel!",c1.c_str());
      if (!c2.null())
        Debug("LazyRecorder", DebugWarn,"Wave source '%s' peer record with no data channel!",c2.c_str());
      return false;
    }

    if (!c1.null()) {
      LazyConsumer* c = new LazyConsumer(c1, msg.getValue("notify_call"), maxlen);
      de->setCallRecord(c);
      c->deref();
    }

    if (!c2.null()) {
      LazyConsumer* c = new LazyConsumer(c2, msg.getValue("notify_peer"), maxlen);
      de->setPeerRecord(c);
      c->deref();
    }

    // Stop dispatching if we handled all requested
    return !more;
  }

  LazyRecorderPlugin::LazyRecorderPlugin()
    : Plugin("regexroute")
  {
    Output("Loaded module LazyRecorder");
  }

  LazyRecorderPlugin::~LazyRecorderPlugin()
  {
    Output("Unloading module LazyRecorder");
    s_bucket_writer->cancel();
  }

  void LazyRecorderPlugin::initialize()
  {
    s_cfg.load();
    Output("Initializing module LazyRecorder");
    Output("Initializing module LazyRecorder");
    Engine::install(new RecordHandler);
    s_bucket_writer = new BucketWriter();
    if(!s_bucket_writer->startup())
      Debug("LazyRecorder", DebugFail,"Can't start file buffer thread!");
  }

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
