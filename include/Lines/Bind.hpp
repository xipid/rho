#ifndef LINES_BIND_HPP
#define LINES_BIND_HPP

#include <cstdlib>
#include <Rho/Meta.hpp>
#include <Rho/Railway.hpp>
#include <Resource/Path.hpp>
#include <Resource/Socket.hpp>
#include <Xi/Func.hpp>
#include <Collection/String.hpp>
#include <Collection/Array.hpp>
#include <Collection/Map.hpp>

#ifndef ESP32
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#else
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#endif

using namespace Xi;
using namespace Collection;

namespace Rho {

// ---------------------------------------------------------------------------
// TopLevel
//
// Well-known top-level segment identifiers for NumericalAddress.
// ---------------------------------------------------------------------------
struct TopLevel {
  static constexpr u64 Self = 0;
  static constexpr u64 Hijaz = 1;
  static constexpr u64 Central = 2;
  static constexpr u64 DzEast = 3;
  static constexpr u64 DzWest = 4;
  static constexpr u64 Sharq = 5;
  static constexpr u64 DzSouth = 6;
  static constexpr u64 IPv4 = 7;
  static constexpr u64 IPv6 = 8;
};

} // namespace Rho

namespace Lines {

using namespace Rho;
using namespace Xi;

// Forward declaration
class Daemon;
inline Daemon* globalDaemon = nullptr;

// ---------------------------------------------------------------------------
// FileBind
//
// Filesystem-based IPC bind (Unix domain sockets, SOCK_DGRAM).
// ---------------------------------------------------------------------------
class FileBind : public Station {
public:
  String path;
  Resource::SockBind* sock = nullptr;
  bool ownsSocket = true;
  String defaultTarget;

  FileBind(const String& src = "", const String& dest = "") {
    path = src;
    defaultTarget = dest;
    if (!path.isEmpty()) {
      sock = new Resource::SockBind(path);
      _setupSockListener();
    }
    
    this->onCartPushed([this](Cart& c) {
      c.meta.put(Meta::Source, this->path);
      c.hasMeta = true;
      if (!this->sock) return;
      
      String targetPathStr;
      if (c.meta.has(Meta::Target)) {
        targetPathStr = *c.meta.get(Meta::Target);
      } else if (c.meta.has(Meta::Path)) {
        targetPathStr = *c.meta.get(Meta::Path);
      } else {
        targetPathStr = this->defaultTarget;
      }
      this->sock->send(c.toString(), Resource::Path(targetPathStr));
    });
  }

  void update() override {
    if (sock) sock->update();
  }

  void destroy() {
    if (sock && ownsSocket) {
      delete sock;
    }
    sock = nullptr;
  }

  ~FileBind() {
    destroy();
  }

private:
  void _setupSockListener() {
    if (!sock) return;
    sock->onPacket([this](String raw) {
      Cart c(raw);
      this->receive(c);
    });
  }
};

// ---------------------------------------------------------------------------
// Bind
//
// Network binding abstraction.
//
// If target is 7.x (IPv4) or 8.x (IPv6) → use native UDP sockets.
// Otherwise → check for globalLines instance and use that.
// ---------------------------------------------------------------------------
class Bind : public Station {
public:
  bool doesntPreferLoopback = false;
  bool allowRandomPortRetry = true;

  Resource::NumericalAddress address;
  Resource::NumericalAddress defaultTarget;

  Bind() {}

  Bind(const Resource::NumericalAddress& addr, const Resource::NumericalAddress& dest = Resource::NumericalAddress()) {
    address = addr;
    defaultTarget = dest;
    _init();
  }

  // -----------------------------------------------------------------------
  // Sending
  // -----------------------------------------------------------------------
  void send(const Resource::NumericalAddress& target, const String& payload) {
    if (_isNativeIP()) {
      _sendNativeUDP(target, payload);
    }
    // If globalLines is available and target is not native IP, route through it
    // (will be wired up when Lines class is used)
  }

  // -----------------------------------------------------------------------
  // Destroy
  // -----------------------------------------------------------------------
  void destroy() {
    _destroyed = true;

    if (_nativeFd >= 0) {
#ifndef ESP32
      ::close(_nativeFd);
#else
      lwip_close(_nativeFd);
#endif
      _nativeFd = -1;
    }
  }

  ~Bind() {
    if (!_destroyed) destroy();
  }

private:
  bool _destroyed = false;
  int _nativeFd = -1;
  u16 _boundPort = 0;

  // -----------------------------------------------------------------------
  // Initialization
  // -----------------------------------------------------------------------
  void _init() {
    if (_isNativeIP()) {
      _initNativeUDP();
    }
    // If not native IP, the user should use globalLines or Gateway directly
  }

  bool _isNativeIP() const {
    return address.size() > 0 &&
           (address[0] == TopLevel::IPv4 || address[0] == TopLevel::IPv6);
  }

  // ---- Native IPv4/IPv6 UDP mode ----

  void _initNativeUDP() {
    u16 port = 0;

    if (address[0] == TopLevel::IPv4 && address.size() >= 6) {
      port = (u16)address[5];
    } else if (address[0] == TopLevel::IPv6 && address.size() >= 10) {
      port = (u16)address[9];
    }

    bool isV6 = (address[0] == TopLevel::IPv6);

#ifndef ESP32
    _nativeFd = ::socket(isV6 ? AF_INET6 : AF_INET, SOCK_DGRAM, 0);
#else
    _nativeFd = lwip_socket(isV6 ? AF_INET6 : AF_INET, SOCK_DGRAM, 0);
#endif
    if (_nativeFd < 0) return;

    // Set non-blocking
#ifndef ESP32
    int flags = fcntl(_nativeFd, F_GETFL, 0);
    fcntl(_nativeFd, F_SETFL, flags | O_NONBLOCK);
#else
    int flags = 1;
    lwip_ioctl(_nativeFd, FIONBIO, &flags);
#endif

    if (!isV6) {
      struct sockaddr_in addr4;
      memset(&addr4, 0, sizeof(addr4));
      addr4.sin_family = AF_INET;
      addr4.sin_port = htons(port);

      if (address.size() >= 5 && !doesntPreferLoopback) {
        char ipStr[32];
        snprintf(ipStr, sizeof(ipStr), "%d.%d.%d.%d",
                 (int)address[1], (int)address[2],
                 (int)address[3], (int)address[4]);
        inet_pton(AF_INET, ipStr, &addr4.sin_addr);
      } else {
        addr4.sin_addr.s_addr = INADDR_ANY;
      }

      int ret = ::bind(_nativeFd, (struct sockaddr*)&addr4, sizeof(addr4));
      if (ret < 0 && allowRandomPortRetry && port != 0) {
        u16 rPort = (u16)(8000 + (millis() % 53000));
        addr4.sin_port = htons(rPort);
        ret = ::bind(_nativeFd, (struct sockaddr*)&addr4, sizeof(addr4));
        if (ret < 0) {
          rPort = (u16)(8000 + ((millis() * 7 + 31) % 53000));
          addr4.sin_port = htons(rPort);
          ret = ::bind(_nativeFd, (struct sockaddr*)&addr4, sizeof(addr4));
        }
      }
      if (ret < 0) {
#ifndef ESP32
        ::close(_nativeFd);
#else
        lwip_close(_nativeFd);
#endif
        _nativeFd = -1;
        return;
      }

      socklen_t addrLen = sizeof(addr4);
      getsockname(_nativeFd, (struct sockaddr*)&addr4, &addrLen);
      _boundPort = ntohs(addr4.sin_port);

    } else {
      struct sockaddr_in6 addr6;
      memset(&addr6, 0, sizeof(addr6));
      addr6.sin6_family = AF_INET6;
      addr6.sin6_port = htons(port);
      addr6.sin6_addr = in6addr_any;

      int ret = ::bind(_nativeFd, (struct sockaddr*)&addr6, sizeof(addr6));
      if (ret < 0 && allowRandomPortRetry && port != 0) {
        u16 rPort = (u16)(8000 + (millis() % 53000));
        addr6.sin6_port = htons(rPort);
        ret = ::bind(_nativeFd, (struct sockaddr*)&addr6, sizeof(addr6));
        if (ret < 0) {
          rPort = (u16)(8000 + ((millis() * 7 + 31) % 53000));
          addr6.sin6_port = htons(rPort);
          ret = ::bind(_nativeFd, (struct sockaddr*)&addr6, sizeof(addr6));
        }
      }
      if (ret < 0) {
#ifndef ESP32
        ::close(_nativeFd);
#else
        lwip_close(_nativeFd);
#endif
        _nativeFd = -1;
        return;
      }

      socklen_t addrLen = sizeof(addr6);
      getsockname(_nativeFd, (struct sockaddr*)&addr6, &addrLen);
      _boundPort = ntohs(addr6.sin6_port);
    }

    this->onCartPushed([this](Cart& c) {
      if (c.target.size() == 0 && this->defaultTarget.size() > 0) {
        c.target = this->defaultTarget;
        c.isAddressed = true;
      }
      if (c.target.size() > 0) {
        _sendNativeUDP(c.target, c.toString());
      }
    });
  }

  void _sendNativeUDP(const Resource::NumericalAddress& target, const String& payload) {
    if (_nativeFd < 0) return;

    if (target[0] == TopLevel::IPv4 && target.size() >= 5) {
      struct sockaddr_in dest;
      memset(&dest, 0, sizeof(dest));
      dest.sin_family = AF_INET;

      char ipStr[32];
      snprintf(ipStr, sizeof(ipStr), "%d.%d.%d.%d",
               (int)target[1], (int)target[2],
               (int)target[3], (int)target[4]);
      inet_pton(AF_INET, ipStr, &dest.sin_addr);
      dest.sin_port = htons(target.size() >= 6 ? (u16)target[5] : 80);

#ifndef ESP32
      ::sendto(_nativeFd, (const char*)payload.data(), payload.size(), 0,
               (struct sockaddr*)&dest, sizeof(dest));
#else
      lwip_sendto(_nativeFd, (const char*)payload.data(), payload.size(), 0,
                  (struct sockaddr*)&dest, sizeof(dest));
#endif

    } else if (target[0] == TopLevel::IPv6 && target.size() >= 9) {
      struct sockaddr_in6 dest;
      memset(&dest, 0, sizeof(dest));
      dest.sin6_family = AF_INET6;

      u16 segs[8];
      for (int i = 0; i < 8; i++) segs[i] = (u16)target[i + 1];
      memcpy(&dest.sin6_addr, segs, 16);
      dest.sin6_port = htons(target.size() >= 10 ? (u16)target[9] : 80);

#ifndef ESP32
      ::sendto(_nativeFd, (const char*)payload.data(), payload.size(), 0,
               (struct sockaddr*)&dest, sizeof(dest));
#else
      lwip_sendto(_nativeFd, (const char*)payload.data(), payload.size(), 0,
                  (struct sockaddr*)&dest, sizeof(dest));
#endif
    }
  }

  // Receive from native UDP socket (called from update())
  void _recvNativeUDP() {
    if (_nativeFd < 0) return;

    u8 buf[4096];
    struct sockaddr_storage srcAddr;
    socklen_t addrLen = sizeof(srcAddr);

#ifndef ESP32
    ssize_t n = ::recvfrom(_nativeFd, buf, sizeof(buf), 0,
                           (struct sockaddr*)&srcAddr, &addrLen);
#else
    int n = lwip_recvfrom(_nativeFd, buf, sizeof(buf), 0,
                          (struct sockaddr*)&srcAddr, &addrLen);
#endif
    while (n > 0) {
      String payload;
      payload.pushEach(buf, (usz)n);

      Cart c(payload);
      if (srcAddr.ss_family == AF_INET) {
        struct sockaddr_in* s4 = (struct sockaddr_in*)&srcAddr;
        u8* ip = (u8*)&s4->sin_addr.s_addr;
        c.source.push(TopLevel::IPv4);
        c.source.push(ip[0]);
        c.source.push(ip[1]);
        c.source.push(ip[2]);
        c.source.push(ip[3]);
        c.source.push(ntohs(s4->sin_port));
        c.isAddressed = true;
      } else if (srcAddr.ss_family == AF_INET6) {
        struct sockaddr_in6* s6 = (struct sockaddr_in6*)&srcAddr;
        u16* ip6 = (u16*)&s6->sin6_addr;
        c.source.push(TopLevel::IPv6);
        for (int i = 0; i < 8; i++) c.source.push(ntohs(ip6[i]));
        c.source.push(ntohs(s6->sin6_port));
        c.isAddressed = true;
      }

      this->receive(c);

      addrLen = sizeof(srcAddr);
#ifndef ESP32
      n = ::recvfrom(_nativeFd, buf, sizeof(buf), 0,
                     (struct sockaddr*)&srcAddr, &addrLen);
#else
      n = lwip_recvfrom(_nativeFd, buf, sizeof(buf), 0,
                        (struct sockaddr*)&srcAddr, &addrLen);
#endif
    }
  }

public:
  void update() override {
    if (_nativeFd >= 0) {
      _recvNativeUDP();
    }
  }
};

} // namespace Lines

#endif // LINES_BIND_HPP
