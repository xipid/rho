#ifndef LINES_DAEMON_HPP
#define LINES_DAEMON_HPP

#include <Lines/Gateway.hpp>
#include <Lines/Bind.hpp>
#include <Collection/Array.hpp>
#include <Collection/Map.hpp>
#include <Collection/String.hpp>
#include <cstdlib>

namespace Lines {

using namespace Rho;
using namespace Xi;

// ---------------------------------------------------------------------------
// Daemon
//
// Filesystem-based Gateway configurator API.
// Reads from LINES= env var, defaults to /run/lines.
//
// Directory structure:
//   lines/
//     ports/1, 2, 3...     (FileBind sockets for bound ports)
//     this                 (Socket representing gt.address entry)
//     hook/abcd, efg...    (FileBind sockets → gt.hook)
//     upgrade/xxx.yml      (Which hosts to upgrade with)
//     clients/Name.yml     (Auto-generated, user shouldn't change)
//     routes/Routes.yml    (User-written routing config)
//     hostAs/Name.yml      (Announce-as config)
//     pid                  (Current daemon process PID)
// ---------------------------------------------------------------------------

struct GatewayInfo {
  String name;
  String password;
  String theirPublicKey; // Static identity key (not ephemeral)
  Resource::NumericalAddress address;
  Sec::KeyPair keypair; // Our identity, if needed
};

struct IdentityClaim {
  String publicKey;
  Resource::NumericalAddress address;
};

class Daemon {
public:
  static Daemon* globalDaemon;

  String path; // The lines directory path
  Gateway gateway;
  u32 pid = 0;

  Array<GatewayInfo> hostAs;
  Map<String, Resource::NumericalAddress> claims;
  Array<GatewayInfo> upgrades;

  Daemon() {
    const char* env = getenv("LINES");
    if (env) {
      path = env;
    } else {
      path = "/run/lines";
    }

    // Auto-publish if no global daemon exists
    if (!globalDaemon) {
      publish();
    }
  }

  Daemon(const String& p) : path(p) {
    if (!globalDaemon) {
      publish();
    }
  }

  ~Daemon() {
    if (globalDaemon == this) {
      globalDaemon = nullptr;
    }
  }

  /// List all claimed ports.
  Array<u32> list() {
    Array<u32> ports;
    for (usz i = 0; i < gateway.boundPorts.size(); ++i) {
      ports.push(gateway.boundPorts[i].port);
    }
    return ports;
  }

  /// List all hooked stations.
  Array<Station*> listHooks() {
    Array<Station*> hooks;
    for (usz i = 0; i < gateway.hookedStations.size(); ++i) {
      hooks.push(gateway.hookedStations[i]);
    }
    return hooks;
  }

  /// Bind a specific port. Returns the station for that port.
  Station* bind(u32 port) {
    return gateway.bind(port);
  }

  /// Bind a random port. Returns the station.
  Station* bind() {
    return gateway.bind();
  }

  /// Unbind a port.
  void unbind(u32 port) {
    for (usz i = 0; i < gateway.boundPorts.size(); ++i) {
      if (gateway.boundPorts[i].port == port) {
        gateway.router.unhook(gateway.boundPorts[i].station);
        delete gateway.boundPorts[i].station;
        gateway.boundPorts.splice(i, 1);
        return;
      }
    }
  }

  /// Publish this Daemon instance as globalDaemon.
  void publish() {
    globalDaemon = this;
  }

  /// Start a daemon process. (Implementation depends on platform.)
  void start() {
    // Platform-specific: on Linux, fork() + exec rhod binary
    // The actual daemon logic is in the `rhod` binary
  }

  /// Stop the running daemon.
  void stop() {
    // Read PID from lines/pid and send SIGTERM
  }

  /// Update tick — delegates to the underlying gateway.
  void update() {
    gateway.update();
  }
};

// Static member initialization
inline Daemon* Daemon::globalDaemon = nullptr;

} // namespace Lines

#endif // LINES_DAEMON_HPP
