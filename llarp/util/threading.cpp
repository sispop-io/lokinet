#include <util/threading.hpp>

#include <util/logger.hpp>

#ifdef POSIX
#include <pthread.h>
#if defined(__FreeBSD__)
#include <pthread_np.h>
#endif
#endif

namespace llarp
{
  namespace util
  {
    void
    SetThreadName(const std::string& name)
    {
#ifdef POSIX
#if defined(__FreeBSD__)
      /* on free bsd this function has void return type */
      pthread_set_name_np(pthread_self(), name.c_str());
#else
#if defined(__MACH__)
      const int rc = pthread_setname_np(name.c_str());
// API present upstream since v2.11.3 and imported downstream
// in CR 8158 <https://www.illumos.org/issues/8158>
#elif defined(__linux__) || defined(__sun)
      const int rc = pthread_setname_np(pthread_self(), name.c_str());
#else
#error "unsupported platform"
#endif
      if(rc)
      {
        LogError("Failed to set thread name to ", name, " errno = ", rc,
                 " errstr = ", strerror(rc));
      }
#endif
#else
      LogInfo("Thread name setting not supported on this platform");
      (void)name;
#endif
    }
  }  // namespace util
}  // namespace llarp
