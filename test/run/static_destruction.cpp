#include <boost/log/trivial.hpp>
#include <boost/log/sources/severity_channel_logger.hpp>

typedef boost::log::trivial::severity_level level;
typedef boost::log::sources::severity_channel_logger_mt<level> logger;

class LogConstructor {
 public:
  LogConstructor() {
    BOOST_LOG_SEV(log_,  boost::log::trivial::debug) << "LogConstructor class constructed";
  }

 private:
  logger log_;
};

class LogDestructor {
 public:
  ~LogDestructor() {
    BOOST_LOG_SEV(log_,  boost::log::trivial::debug) << "LogDestructor class destructed";
  }

 private:
  logger log_;
};


int main(int argc, char* argv[])
{
  static LogDestructor d;
  static LogConstructor c;

  return 0;
}
